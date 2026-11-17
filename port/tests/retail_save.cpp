#include "filesystem.h"
#include "retail_save.h"
#include "retail_achievements.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void verifyCodec()
{
    // Independently laid out weapon block, followed by an unknown block.
    const SaveBytes original{
        2, 0, 0, 0, 17, 0, 0, 0,
        4, 0, 0, 0, 10, 0, 0, 0, 6, 0, 0, 0, 1, 0, 0, 0, 0,
        99, 0, 0, 0, 2, 0, 0, 0, 0x42, 0x00
    };
    const auto decoded = decodeRetailSave(original);
    if (decoded.size() != 2 || decoded[0].id != 2 || decoded[1].id != 99)
        throw std::runtime_error("Unexpected save block IDs.");
    const auto weapon = std::get<SaveWeapon>(decoded[0].value);
    if (weapon.id != 4 || weapon.clipSize != 10 || weapon.clip != 6 || weapon.context != 1 || weapon.locked != 0 ||
        std::get<SaveBytes>(decoded[1].value) != SaveBytes{0x42, 0x00} || encodeRetailSave(decoded) != original)
        throw std::runtime_error("Weapon layout or unknown save payload changed.");

    const std::vector<SaveBlock> values{
        {-1, int32_t{1}}, {1, int32_t{-3}}, {3, SaveAmmo{5, 1, 19}},
        {4, SavePosition{-1.25f, 3.f, -0.f}}, {5, std::string("a\0b", 3)},
        {8, SavePair{3, 70}}
    };
    const auto encoded = encodeRetailSave(values);
    if (encodeRetailSave(decodeRetailSave(encoded)) != encoded)
        throw std::runtime_error("Save block round trip failed.");

    auto broken = original;
    broken.pop_back();
    try {
        decodeRetailSave(broken);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Truncated save block was accepted.");
}

void verifyRestore()
{
    for (int32_t operation : {1, 2, 3, 4, 5, 8}) {
        const RetailAchievementRule rule{operation, 0, 3};
        const int32_t match = operation == 2 ? 4 : operation == 4 ? 2 : operation == 8 ? 7 : 3;
        const int32_t miss = operation == 4 || operation == 5 ? 4 : 1;
        if (!rule.matches(match) || rule.matches(miss))
            throw std::runtime_error("Retail achievement comparison failed.");
    }
    const auto state = restoreRetailSavedState({
        {0, int32_t{25}}, {1, int32_t{50}}, {2, SaveWeapon{4, 10, 6, 1, 0}},
        {2, SaveWeapon{4, 12, 3, 1, 1}}, {3, SaveAmmo{5, 1, 19}},
        {4, SavePosition{1.f, 2.f, 3.f}}, {8, SavePair{3, 70}},
        {17, SavePair{1, 240}}, {12, int32_t{1}}, {17, SavePair{0, 150}},
        {14, int32_t{0}}, {15, SavePair{26, 1}}, {16, int32_t{7}},
        {18, int32_t{-1}}, {19, int32_t{1}}, {99, SaveBytes{2}}
    }, 0);
    const auto weapon = state.weapons.at({1, 4});
    if (state.cash != 50 || state.context != 1 || state.armor[0] != 200 || state.armor[1] != 150 ||
        !state.playerFlag383 || state.cameraTutorial != false || state.sniperTutorial != true ||
        state.wantedTutorial || state.position != SavePosition{1.f, 2.f, 3.f} ||
        weapon.locked || weapon.clip != 3 || weapon.clipSize != 12 || state.ammo.at({1, 5}) != 19 ||
        state.completedScripts.size() != 2 || state.completedScripts[0].context != -1 ||
        state.completedScripts[1].script != 26 || state.completedScripts[1].context != 1 ||
        state.statUpdates.size() != 1 || state.statUpdates[0].second != 70 ||
        state.completedAchievements != std::vector<int32_t>{7})
        throw std::runtime_error("Retail save state restoration failed.");
    try {
        restoreRetailSavedState({{12, int32_t{2}}}, 0);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Invalid save context was accepted.");
}

void verifyAchievementState()
{
    RetailAchievements catalog;
    catalog.stats.resize(50);
    catalog.missionCount = 10;
    catalog.missionPercent = 80;
    catalog.rules = {{3, 1, 5}, {1, 2, 6}};
    catalog.achievements = {{0, {0, 1}, 0, 0}, {0, {}, 0, 0}};
    RetailAchievementState menuState(catalog, false);
    menuState.restore({{8, SavePair{0, 33}}, {16, int32_t{1}}});
    if (menuState.stat(0) != 33 || menuState.completedCount() != 1)
        throw std::runtime_error("Main menu recalculated saved progress before game load.");
    RetailAchievementState state(std::move(catalog));
    state.recordMissionCash(250);
    state.recordMissionEnded();
    state.recordCashSpent(100);
    state.recordCashSpent(200);
    if (state.stat(8) != 250 || state.stat(9) != 250 || state.stat(11) != 300 ||
        state.stat(49) != 1 || state.stat(0) != 8)
        throw std::runtime_error("Mission completion or spending did not update retail stats and progress.");
    state.setStat(49, 63);
    state.recordJump(12);
    state.recordJump(7);
    state.recordVehicleSpeed(130);
    state.recordVehicleSpeed(95);
    state.recordDamage(35);
    state.recordDamage(12);
    state.recordKilometerDriven();
    state.recordKilometerDriven();
    state.recordJumpRamps(1u << 4);
    state.recordJumpRamps(1u << 2);
    state.recordJumpRamps(1u << 4);
    if (state.stat(41) != ((1 << 4) | (1 << 2)))
        throw std::runtime_error("Ramp visits did not preserve previously recorded bits.");
    if (state.stat(16) != 12)
        throw std::runtime_error("Longest jump did not keep the maximum meter count.");
    state.setStat(1, 5);
    state.setStat(1, 0);
    if (state.completed(0)) throw std::runtime_error("Achievement completed before all rules matched.");
    state.setStat(2, 6);
    state.setStat(2, 6);
    if (!state.completed(0) || state.stat(0) != 90 || state.takeNotifications() != std::deque<size_t>{0})
        throw std::runtime_error("Achievement rule latching or notification failed.");

    RetailSavedState snapshot;
    snapshot.cash = 250;
    state.capture(snapshot);
    if (snapshot.cash != 250 || snapshot.statUpdates.size() != 50 ||
        snapshot.statUpdates.at(16).first != 16 || snapshot.statUpdates.at(16).second != 12 ||
        snapshot.statUpdates.at(41).second != ((1 << 4) | (1 << 2)) ||
        snapshot.completedAchievements != std::vector<int32_t>{0})
        throw std::runtime_error("Achievement capture lost retail stat or completion records.");
    const auto blocks = decodeRetailSave(encodeRetailSave(sampleRetailSave(snapshot, 0x30500, 0)));
    state.restore(blocks);
    if (!state.completed(0) || state.stat(14) != 130 || state.stat(16) != 12 || state.stat(17) != 2 ||
        state.stat(18) != 35 || state.stat(41) != ((1 << 4) | (1 << 2)) ||
        !state.takeNotifications().empty())
        throw std::runtime_error("Achievement snapshot failed to round-trip through the retail save codec.");

    state.restore({
        {8, SavePair{49, 63}}, {16, int32_t{1}}, {8, SavePair{0, 12}},
        {8, SavePair{99, 7}}, {16, int32_t{-1}}
    });
    if (state.completed(0) || !state.completed(1) || state.stat(0) != 12 || !state.takeNotifications().empty())
        throw std::runtime_error("Achievement save order, reset, or notification suppression failed.");
}

void verifyFile(const std::filesystem::path& path, const std::filesystem::path& saveDirectory)
{
    const auto full = std::filesystem::absolute(path);
    ResourceFileSystem files(full.parent_path());
    const auto name = full.filename().u8string();
    const std::unique_ptr<IFileStream> file(files.openResource(name.c_str()));
    if (!file) throw std::runtime_error("Could not open retail save.");
    const auto blocks = readRetailSave(*file);
    const auto restored = restoreRetailSavedState(blocks, 0);
    if (std::filesystem::exists(full.parent_path() / "Achievements.gmap")) {
        const auto catalog = readRetailAchievements(files);
        for (const auto stat : restored.statUpdates)
            if (static_cast<size_t>(stat.first) >= catalog.stats.size())
                throw std::runtime_error("Save stat ID is outside the retail catalog.");
        for (const auto achievement : restored.completedAchievements)
            if (achievement < 0 || static_cast<size_t>(achievement) >= catalog.achievements.size())
                throw std::runtime_error("Save achievement ID is outside the retail catalog.");
        RetailAchievementState state(catalog);
        state.restore(blocks);
        for (const auto achievement : restored.completedAchievements)
            if (!state.completed(achievement))
                throw std::runtime_error("Saved achievement was not restored.");
        std::cout << catalog.achievements.size() << " achievements, " << catalog.rules.size() << " rules, "
                  << catalog.stats.size() << " stats validated against the save.\n";
    }
    if (!saveDirectory.empty()) {
        ResourceFileSystem menuFiles(full.parent_path(), saveDirectory);
        auto state = loadMenuAchievementState(menuFiles);
        if (!state || !state->takeNotifications().empty())
            throw std::runtime_error("Main-menu save restore failed.");
        for (const auto achievement : restored.completedAchievements)
            if (!state->completed(achievement))
                throw std::runtime_error("Main-menu restore lost a completed achievement.");
        std::cout << state->completedCount() << " achievements restored through the main-menu loader.\n";
    }
    const auto serialized = encodeRetailSave(blocks);
    SaveBytes original(static_cast<size_t>(file->size()));
    file->seek(0);
    if (file->read(original.data(), original.size()) != original.size() || serialized != original)
        throw std::runtime_error("Retail save round trip changed the file bytes.");
    std::cout << blocks.size() << " save blocks round-tripped byte for byte.\n";
}

void verifySaveDirectory()
{
    namespace fs = std::filesystem;
    struct Scratch {
        fs::path path;
        ~Scratch()
        {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } scratch{fs::temp_directory_path() / ("OpenGangstar-save-" + std::to_string(GetCurrentProcessId()) + '-' +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    const auto data = scratch.path / "retail";
    const auto saves = scratch.path / "saves";
    fs::create_directories(data);
    std::ofstream(data / "Gangstar2.sav", std::ios::binary) << "retail";
    ResourceFileSystem files(data, saves);
    const SaveBytes preferences{
        4, 0, 0, 0, 4, 0, 0, 0, 35, 0, 0, 0,
        99, 0, 0, 0, 2, 0, 0, 0, 1, 2,
        5, 0, 0, 0, 4, 0, 0, 0, 120, 0, 0, 0
    };
    files.replaceSaveFile("Gangstar2.preferences", preferences);
    {
        const std::unique_ptr<IFileStream> file(files.openSavedFile("Gangstar2.preferences"));
        if (!file) throw std::runtime_error("Could not open retail preferences fixture.");
        const auto volumes = readRetailAudioPreferences(*file);
        if (volumes.effects != 0.35f || volumes.music != 1)
            throw std::runtime_error("Retail volume preferences were misrouted or not clamped.");
    }

    saveRetailAudioPreferences(files, {0.72f, 0.15f});
    const auto preferenceBytes = [&] {
        const std::unique_ptr<IFileStream> file(files.openSavedFile(retailPreferencesFilename));
        SaveBytes bytes(static_cast<size_t>(file->size()));
        if (file->read(bytes.data(), bytes.size()) != bytes.size())
            throw std::runtime_error("Could not read back written preferences.");
        return bytes;
    };
    auto expected = preferences;
    expected[8] = 72;
    expected[30] = 15;
    if (preferenceBytes() != expected)
        throw std::runtime_error("Writing preferences changed unrelated blocks.");
    {
        const std::unique_ptr<IFileStream> file(files.openSavedFile(retailPreferencesFilename));
        const auto volumes = readRetailAudioPreferences(*file);
        if (volumes.effects != 0.72f || volumes.music != 0.15f)
            throw std::runtime_error("Saved preferences did not round-trip.");
    }
    saveRetailLanguagePreference(files, RetailLanguage::japanese);
    {
        const std::unique_ptr<IFileStream> file(files.openSavedFile(retailPreferencesFilename));
        if (readRetailLanguagePreference(*file) != RetailLanguage::japanese)
            throw std::runtime_error("Retail language pack did not round-trip.");
        const auto volumes = readRetailAudioPreferences(*file);
        if (volumes.effects != 0.72f || volumes.music != 0.15f)
            throw std::runtime_error("Language selection changed saved volume levels.");
    }
    const auto withLanguage = preferenceBytes();
    const SaveBytes expectedLanguage{2, 0, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0};
    if (withLanguage.size() != expected.size() + expectedLanguage.size() ||
        !std::equal(expected.begin(), expected.end(), withLanguage.begin()) ||
        !std::equal(expectedLanguage.begin(), expectedLanguage.end(), withLanguage.begin() + expected.size()))
        throw std::runtime_error("Language preference did not preserve the other retail blocks.");
    const SaveBytes damagedPreferences{4, 0, 0, 0, 4, 0, 0, 0, 10};
    files.replaceSaveFile(retailPreferencesFilename, damagedPreferences);
    bool damagedRejected = false;
    try {
        saveRetailAudioPreferences(files, {0, 0});
    } catch (const std::runtime_error&) {
        damagedRejected = true;
    }
    if (!damagedRejected || preferenceBytes() != damagedPreferences)
        throw std::runtime_error("Writing preferences replaced a malformed original.");
    const auto first = encodeRetailSave({{1, int32_t{10}}});
    const auto replacement = encodeRetailSave({{1, int32_t{20}}, {99, SaveBytes{2}}});
    files.replaceSaveFile("Gangstar2.sav", first);
    const auto read = [&] {
        const std::unique_ptr<IFileStream> file(files.openSavedFile("Gangstar2.sav"));
        if (!file || file->canWrite()) throw std::runtime_error("Save read opened a writable or missing file.");
        return encodeRetailSave(readRetailSave(*file));
    };
    if (read() != first) throw std::runtime_error("Could not read the first save.");
    files.replaceSaveFile("Gangstar2.sav", replacement);
    if (read() != replacement) throw std::runtime_error("Save replacement did not publish the new bytes.");

    struct Lock {
        HANDLE handle;
        ~Lock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    };
    bool failed = false;
    {
        const Lock lock{CreateFileW((saves / "Gangstar2.sav").c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (lock.handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Could not lock test save.");
        try {
            files.replaceSaveFile("Gangstar2.sav", first);
        } catch (const std::system_error&) {
            failed = true;
        }
    }
    if (!failed || read() != replacement)
        throw std::runtime_error("A failed rename changed the previous save.");
    for (const auto& entry : fs::directory_iterator(saves))
        if (entry.path().extension() == L".tmp") throw std::runtime_error("Failed write left a temporary file.");

    RetailSavedState snapshot;
    snapshot.cash = 381;
    snapshot.armor = {20, 55};
    snapshot.position = SavePosition{1.f, -2.f, 3.f};
    snapshot.context = 0;
    snapshot.callContactTutorial = false;
    snapshot.wantedTutorial = true;
    snapshot.weapons.emplace(std::make_pair(1, 4), SaveWeapon{4, 12, 6, 1, 0});
    snapshot.ammo[{0, 2}] = 19;
    snapshot.completedScripts = {{17, -1}, {26, 1}};
    snapshot.statUpdates = {{3, 70}};
    snapshot.completedAchievements = {15, 18};
    writeRetailSave(files, snapshot, 0x30500, 0);
    const auto sampled = decodeRetailSave(read());
    const auto restored = restoreRetailSavedState(sampled, 0);
    if (sampled.front().id != -1 || std::get<int32_t>(sampled.front().value) != 0x30500 ||
        restored.cash != snapshot.cash || restored.position != snapshot.position ||
        restored.armor != snapshot.armor || restored.context != snapshot.context ||
        restored.callContactTutorial != false || restored.wantedTutorial != true ||
        restored.weapons.at({1, 4}).clip != 6 || restored.ammo != snapshot.ammo ||
        restored.completedScripts.size() != 2 || restored.completedScripts[0].context != -1 ||
        restored.completedScripts[1].context != 1 || restored.completedScripts[1].script != 26 ||
        restored.statUpdates.size() != 1 || restored.statUpdates[0].second != 70 ||
        restored.completedAchievements != snapshot.completedAchievements)
        throw std::runtime_error("Writing a sampled game state lost retail save fields.");
    RetailSavedState checkpoint;
    checkpoint.cash = 250;
    checkpoint.context = 0;
    checkpoint.position = SavePosition{10.f, 20.f, 30.f};
    checkpoint.armor[0] = 0;
    checkpoint.completedScripts = {{windowsOpeningCheckpointScript, -1}};
    checkpoint.statUpdates = {{49, 1}};
    if (readWindowsOpeningCheckpoint(files))
        throw std::runtime_error("A retail save incorrectly enabled the Windows checkpoint.");
    writeWindowsOpeningCheckpoint(files, checkpoint);
    const auto resumed = readWindowsOpeningCheckpoint(files);
    if (!resumed || resumed->cash != checkpoint.cash || resumed->position != checkpoint.position ||
        resumed->armor != checkpoint.armor || resumed->statUpdates.size() != 1 || read() != encodeRetailSave(sampled))
        throw std::runtime_error("Windows checkpoint lost state or replaced the imported retail save.");

    if (files.openSavedFile("../retail/Gangstar2.sav") || files.openSavefile("../retail/Gangstar2.sav"))
        throw std::runtime_error("A save filename escaped the save directory.");
    bool overlapRejected = false;
    try {
        ResourceFileSystem overlapping(data / "", data / "nested" / "saves");
    } catch (const std::runtime_error&) {
        overlapRejected = true;
    }
    if (!overlapRejected) throw std::runtime_error("Save directory overlapped retail resources.");

    {
        const std::unique_ptr<IFileStream> stream(files.openSavefile("compatibility.sav"));
        if (!stream || !stream->canWrite() || stream->write(first.data(), first.size()) != first.size() ||
            stream->size() != first.size()) throw std::runtime_error("The retail writable stream contract failed.");
        stream->seek(0);
        if (encodeRetailSave(readRetailSave(*stream)) != first)
            throw std::runtime_error("Could not read back the writable save stream.");
    }
    std::ifstream retail(data / "Gangstar2.sav", std::ios::binary);
    std::string marker;
    retail >> marker;
    if (marker != "retail") throw std::runtime_error("Save operation wrote into retail data.");
}
}

int wmain(int argc, wchar_t** argv)
try {
    if (argc > 3) {
        std::cerr << "Usage: retail_save_test [Gangstar2.sav [Windows save directory]]\n";
        return 2;
    }
    verifyCodec();
    verifyRestore();
    verifyAchievementState();
    verifySaveDirectory();
    if (argc >= 2) verifyFile(argv[1], argc == 3 ? argv[2] : L"");
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
