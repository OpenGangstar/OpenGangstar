#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

class FileSystemBase;
class ResourceFileSystem;
struct SaveBlock;
struct RetailSavedState;

struct RetailAchievementRule {
    int32_t operation, stat, threshold;
    bool matches(int32_t value) const;
};

struct RetailAchievement {
    int32_t category;
    std::vector<int32_t> rules;
    int32_t textId1, textId2;
};

struct RetailAchievementStat { int32_t type, format, textId; };

struct RetailAchievements {
    std::vector<RetailAchievementRule> rules;
    std::vector<RetailAchievement> achievements;
    std::vector<RetailAchievementStat> stats;
    std::vector<uint8_t> trailer;
    // Defaults match the recovered manager when the constants asset is unavailable.
    int32_t missionCount = 63;
    int32_t missionPercent = 90;
};

RetailAchievements readRetailAchievements(FileSystemBase& files);

class RetailAchievementState {
public:
    explicit RetailAchievementState(RetailAchievements catalog, bool gameLoaded = true);
    void reset();
    void restore(const std::vector<SaveBlock>& blocks);
    void capture(RetailSavedState& state) const;
    void setStat(size_t id, int32_t value);
    void recordJump(int32_t meters);
    void recordVehicleSpeed(int32_t kilometersPerHour);
    void recordDamage(int32_t healthLoss);
    void recordKilometerDriven();
    void recordChapterEnd();
    void recordJumpRamps(uint32_t mask);
    void complete(size_t id);
    int32_t stat(size_t id) const { return stats.at(id); }
    bool completed(size_t id) const { return achievementComplete.at(id); }
    size_t completedCount() const;
    std::deque<size_t> takeNotifications();

private:
    bool evaluateRule(size_t id);
    void updateProgress();
    void setMaxStat(size_t id, int32_t value);

    RetailAchievements catalog;
    std::vector<std::vector<size_t>> statRules, ruleAchievements;
    std::vector<int32_t> stats;
    std::vector<bool> ruleComplete, achievementComplete;
    std::deque<size_t> notifications;
    bool gameLoaded;
};

std::optional<RetailAchievementState> loadMenuAchievementState(ResourceFileSystem& files);
