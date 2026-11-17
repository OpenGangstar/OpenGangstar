#include "retail_achievements.h"
#include "FileSystemBase.h"
#include "IFileStream.h"
#include "retail_save.h"
#include "retail_constants.h"
#include "filesystem.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
class AchievementReader {
public:
    explicit AchievementReader(FileSystemBase& files) : file(files.openResource("Achievements.gmap"))
    {
        if (!file || file->size() > 1024 * 1024) fail();
    }

    int32_t word()
    {
        uint8_t bytes[4];
        if (file->read(bytes, sizeof bytes) != sizeof bytes) fail();
        uint32_t bits = 0;
        for (const auto byte : bytes) bits = bits << 8 | byte;
        int32_t value;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }

    size_t count(size_t recordMinimum)
    {
        const auto value = word();
        if (value < 0 || static_cast<size_t>(value) > (file->size() - file->tell()) / recordMinimum) fail();
        return static_cast<size_t>(value);
    }

    void recordSize(size_t expected)
    {
        const auto value = word();
        if (value < 0 || static_cast<size_t>(value) != expected) fail();
    }

    RetailAchievement achievement()
    {
        const auto length = word();
        if (length < 16 || static_cast<uint32_t>(length) > file->size() - file->tell()) fail();
        RetailAchievement entry;
        entry.category = word();
        const auto rules = count(4);
        if (16 + rules * 4 != static_cast<size_t>(length)) fail();
        for (size_t i = 0; i < rules; ++i) entry.rules.push_back(word());
        entry.textId1 = word();
        entry.textId2 = word();
        return entry;
    }

    std::vector<uint8_t> trailer()
    {
        std::vector<uint8_t> bytes(static_cast<size_t>(file->size() - file->tell()));
        if (!bytes.empty() && file->read(bytes.data(), bytes.size()) != bytes.size()) fail();
        return bytes;
    }

    [[noreturn]] static void fail() { throw std::runtime_error("Invalid or missing Achievements.gmap."); }

private:
    std::unique_ptr<IFileStream> file;
};
}

bool RetailAchievementRule::matches(int32_t value) const
{
    switch (operation) {
    case 1: return value == threshold;
    case 2: return value > threshold;
    case 3: return value >= threshold;
    case 4: return value < threshold;
    case 5: return value <= threshold;
    case 8: return (value & threshold) == threshold;
    default: return false;
    }
}

RetailAchievements readRetailAchievements(FileSystemBase& files)
{
    AchievementReader reader(files);
    RetailAchievements data;
    const auto rules = reader.count(16);
    for (size_t i = 0; i < rules; ++i) {
        reader.recordSize(12);
        data.rules.push_back({reader.word(), reader.word(), reader.word()});
    }
    const auto achievements = reader.count(20);
    for (size_t i = 0; i < achievements; ++i) data.achievements.push_back(reader.achievement());
    const auto stats = reader.count(16);
    for (size_t i = 0; i < stats; ++i) {
        reader.recordSize(12);
        data.stats.push_back({reader.word(), reader.word(), reader.word()});
    }
    for (const auto& rule : data.rules)
        if (rule.stat < 0 || static_cast<size_t>(rule.stat) >= stats) AchievementReader::fail();
    for (const auto& achievement : data.achievements)
        for (const auto rule : achievement.rules)
            if (rule < 0 || static_cast<size_t>(rule) >= rules) AchievementReader::fail();
    data.trailer = reader.trailer();
    if (const std::unique_ptr<IFileStream> gameplay(files.openResource("Gameplay_pycst.bin")); gameplay) {
        const auto constants = readRetailConstants(files, "Gameplay_pycst.bin");
        data.missionCount = retailInteger(constants, "AchievementsCst", "NumMissions");
        data.missionPercent = retailInteger(constants, "AchievementsCst", "MissionPercentForProgress");
        if (data.missionCount <= 0 || data.missionPercent < 0 || data.missionPercent > 100)
            throw std::runtime_error("Invalid retail achievement progress constants.");
    }
    return data;
}

RetailAchievementState::RetailAchievementState(RetailAchievements definitions, bool gameLoaded)
    : catalog(std::move(definitions)), statRules(catalog.stats.size()),
      ruleAchievements(catalog.rules.size()), stats(catalog.stats.size()),
      ruleComplete(catalog.rules.size()), achievementComplete(catalog.achievements.size()),
      gameLoaded(gameLoaded)
{
    if (catalog.missionCount <= 0 || catalog.missionPercent < 0 || catalog.missionPercent > 100)
        throw std::runtime_error("Invalid retail achievement progress constants.");
    for (size_t id = 0; id < catalog.rules.size(); ++id)
        statRules.at(catalog.rules[id].stat).push_back(id);
    for (size_t id = 0; id < catalog.achievements.size(); ++id)
        for (const auto rule : catalog.achievements[id].rules)
            ruleAchievements.at(rule).push_back(id);
}

void RetailAchievementState::reset()
{
    std::fill(stats.begin(), stats.end(), 0);
    std::fill(ruleComplete.begin(), ruleComplete.end(), false);
    std::fill(achievementComplete.begin(), achievementComplete.end(), false);
    notifications.clear();
}

void RetailAchievementState::restore(const std::vector<SaveBlock>& blocks)
{
    reset();
    for (const auto& block : blocks) {
        if (block.id == 8) {
            const auto update = std::get<SavePair>(block.value);
            if (update.first >= 0 && static_cast<size_t>(update.first) < stats.size())
                setStat(update.first, update.second);
        } else if (block.id == 16) {
            const auto id = std::get<int32_t>(block.value);
            if (id >= 0 && static_cast<size_t>(id) < achievementComplete.size()) complete(id);
        }
    }
    notifications.clear();
}

void RetailAchievementState::capture(RetailSavedState& state) const
{
    // SampleAndSave writes every stat, then the completed achievement IDs, in ID order.
    state.statUpdates.clear();
    state.statUpdates.reserve(stats.size());
    for (size_t id = 0; id < stats.size(); ++id)
        state.statUpdates.push_back({static_cast<int32_t>(id), stats[id]});
    state.completedAchievements.clear();
    for (size_t id = 0; id < achievementComplete.size(); ++id)
        if (achievementComplete[id]) state.completedAchievements.push_back(static_cast<int32_t>(id));
}

bool RetailAchievementState::evaluateRule(size_t id)
{
    const auto& rule = catalog.rules[id];
    if (!ruleComplete[id]) ruleComplete[id] = rule.matches(stats[rule.stat]);
    return ruleComplete[id];
}

void RetailAchievementState::setStat(size_t id, int32_t value)
{
    stats.at(id) = value;
    for (const auto rule : statRules[id]) {
        if (!evaluateRule(rule)) continue;
        for (const auto achievement : ruleAchievements[rule]) {
            if (achievementComplete[achievement]) continue;
            const auto& required = catalog.achievements[achievement].rules;
            if (std::all_of(required.begin(), required.end(), [this](int32_t index) {
                return evaluateRule(index);
            })) complete(achievement);
        }
    }
}

void RetailAchievementState::recordJump(int32_t meters)
{
    setMaxStat(16, meters);
}

void RetailAchievementState::recordVehicleSpeed(int32_t kilometersPerHour)
{
    setMaxStat(14, kilometersPerHour);
}

void RetailAchievementState::recordDamage(int32_t healthLoss)
{
    setMaxStat(18, healthLoss);
}

void RetailAchievementState::recordKilometerDriven()
{
    constexpr size_t distanceDrivenStat = 17;
    if (distanceDrivenStat < stats.size() && stats[distanceDrivenStat] < std::numeric_limits<int32_t>::max())
        setStat(distanceDrivenStat, stats[distanceDrivenStat] + 1);
}

void RetailAchievementState::recordChapterEnd()
{
    constexpr size_t chapterEndStat = 24;
    if (chapterEndStat < stats.size() && stats[chapterEndStat] < std::numeric_limits<int32_t>::max())
        setStat(chapterEndStat, stats[chapterEndStat] + 1);
}

void RetailAchievementState::setMaxStat(size_t id, int32_t value)
{
    if (id < stats.size() && value > stats[id]) setStat(id, value);
}

void RetailAchievementState::recordJumpRamps(uint32_t mask)
{
    constexpr size_t jumpRampStat = 41;
    if (jumpRampStat >= stats.size()) return;
    const auto previous = static_cast<uint32_t>(stats[jumpRampStat]);
    const auto merged = previous | mask;
    if (merged != previous) setStat(jumpRampStat, static_cast<int32_t>(merged));
}

void RetailAchievementState::complete(size_t id)
{
    achievementComplete.at(id) = true;
    notifications.push_back(id);
    if (gameLoaded) updateProgress();
}

size_t RetailAchievementState::completedCount() const
{
    return std::count(achievementComplete.begin(), achievementComplete.end(), true);
}

void RetailAchievementState::updateProgress()
{
    constexpr size_t missionStat = 49;
    if (stats.size() <= missionStat || achievementComplete.empty()) return;
    const auto missions = std::min(stats[missionStat], catalog.missionCount);
    const auto completed = completedCount();
    const float missionProgress = double(missions) * catalog.missionPercent / catalog.missionCount;
    const float achievementProgress = double(completed) * (100 - catalog.missionPercent) / achievementComplete.size();
    const double progress = missionProgress + achievementProgress;
    stats[0] = static_cast<int32_t>(std::max(progress, double(std::numeric_limits<int32_t>::min())));
}

std::deque<size_t> RetailAchievementState::takeNotifications()
{
    return std::exchange(notifications, {});
}

std::optional<RetailAchievementState> loadMenuAchievementState(ResourceFileSystem& files)
{
    const std::unique_ptr<IFileStream> save(files.openSavedFile(retailSaveFilename));
    if (!save || !save->size()) return std::nullopt;
    RetailAchievementState state(readRetailAchievements(files), false);
    state.restore(readRetailSave(*save));
    return state;
}
