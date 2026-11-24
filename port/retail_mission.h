#pragma once

#include "city_assets.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class RetailScriptState { locked, frozen, active, waitingForChild };
class RetailMissionConditions;
struct SaveBlock;

struct RetailEscapePositions {
    std::array<float, 3> player;
    std::array<float, 3> target;
};

bool retailEscapeReached(const RetailEscapePositions& positions, float distanceCm);

struct RetailTravelHooks {
    std::function<void(std::string_view, bool)> setActive;
    std::function<bool(const CityScriptCheckpoint&)> reached;
    std::function<std::optional<RetailEscapePositions>(std::string_view)> escapePositions;
    std::function<std::string_view()> occupiedVehicle;
    std::function<void(std::string_view, bool)> trackVehicle;
};

enum class RetailCinematicPhase {
    gameplay, enteringMission, mission, leavingMission, enteringStartup, startup, leavingStartup
};

class RetailWantedState {
public:
    void setLevel(int32_t value);
    void setPursuers(unsigned count);
    bool update(double deltaMs);
    int32_t level() const { return stars; }
    bool beingPursued() const { return pursuers != 0; }
    bool escaping() const { return phase == Phase::blinking; }

private:
    enum class Phase { inactive, start, waiting, blinking };
    Phase phase = Phase::inactive;
    unsigned pursuers = 0;
    int32_t stars = 0;
    double phaseMs = 0;
};

// Values supplied by the native world, never a view of an original ARM object.
struct RetailScriptObjectMotion {
    int32_t category; // LevelObject::s_type[objectType], not getSpawnType().
    // PlayerTest +240, category-1 object +236, or category-5 NPC state +356.
    uint32_t flags;
    float speed; // The result of virtual GetSpeed(), used only by categories 2 and 3.
    bool player;

    bool stopped() const;
};

struct RetailMissionHooks {
    RetailMissionConditions* conditions = nullptr;
    std::function<void(uint32_t)> showObjective;
    std::function<void()> finishMission;
    std::function<RetailCinematicPhase()> cinematicPhase;
    std::function<void(RetailCinematicPhase, bool)> setCinematicPhase;
    std::function<void(const CityScriptCamera&)> selectCamera;
    std::function<void(uint8_t)> setFadeOpacity;
    std::function<void(int32_t)> selectCinematicAnimation;
    std::function<void(const CityScriptActorPlacement&)> placeActor;
    // The native world owns movement, path/AI state, and completion flags. Destination
    // commands skipped by the script use placeActor instead; path commands still run.
    std::function<void(const CityScriptActorMovement&)> actorMove;
    std::function<void(const CityScriptActorAnimation&)> animateActor;
    std::function<int32_t()> missionState;
    std::function<void(uint32_t, int32_t)> startMission;
    std::function<void(const CityScriptDialogue&)> showDialogue;
    std::function<bool()> dialogueComplete;
    std::function<void()> clearDialogue;
    std::function<void(int32_t)> limitHudWeapon;
    std::function<int32_t()> weaponManagerLimit;
    std::function<void(std::string_view)> explodeVehicle;
    std::function<void(std::string_view)> actorGetOut;
    std::function<bool(std::string_view)> actorKilled;
    std::function<void(std::string_view)> clearKilledTarget;
    std::function<bool(std::string_view)> actorLocked;
    std::function<void(std::string_view, bool)> markLockTarget;
    std::function<void(const CityActorAttack&)> actorAttack;
    std::function<void(std::string_view, bool)> disableActor;
    std::function<void(std::string_view, bool)> setShopUnlocked;
    std::function<bool()> tutorialsEnabled;
    std::function<void(const CityScriptTutorial&)> showTutorial;
    std::function<bool(const CityScriptTutorial&)> blockingTutorial;
    std::function<void()> hideTutorial;
    std::function<void(int32_t)> blinkHud;
    std::function<void()> recordChapterEnd;
    std::function<void(std::string_view, bool)> setFightSide;
    std::function<void()> clearActorAttacks;
    std::function<void(std::string_view)> tagKillTarget;
    std::function<int32_t()> wantedLevel;
    std::function<RetailScriptObjectMotion(std::string_view)> objectMotion;
};

class RetailScriptExecution {
public:
    explicit RetailScriptExecution(CityScriptProgram program);
    RetailScriptState state(uint32_t id) const { return scripts.at(id).state; }
    void reset(int32_t playerContext, bool restoringSave, std::optional<uint32_t> startScript = {},
               const RetailTravelHooks* travel = nullptr);
    void unlock(uint32_t id, int32_t context, int32_t playerContext);
    void unlockFrozenScripts(int32_t playerContext);
    void restoreUnlocks(const std::vector<SaveBlock>& blocks, int32_t resetContext);
    void tick(int32_t elapsedMs, int32_t playerContext,
              const std::function<bool(const CityScriptCommand&, int32_t)>& dispatch,
              const std::function<void()>& refreshEvents,
              const std::function<void()>& saveGame, bool skipBlockingCommands = false,
              const RetailTravelHooks* travel = nullptr,
              const std::function<void(const CityAudioCue&)>& playAudio = {},
              const RetailMissionHooks* mission = nullptr);
    void execute(uint32_t id, int32_t elapsedMs, int32_t playerContext,
                 const std::function<bool(const CityScriptCommand&, int32_t)>& dispatch,
                 const std::function<void()>& refreshEvents,
                 const std::function<void()>& saveGame, bool skipBlockingCommands = false,
                 const RetailTravelHooks* travel = nullptr,
                 const std::function<void(const CityAudioCue&)>& playAudio = {},
                 const RetailMissionHooks* mission = nullptr);

private:
    struct Script {
        RetailScriptState state = RetailScriptState::locked;
        int32_t context = -1;
        size_t next = 0;
        std::optional<uint32_t> parent;
        std::optional<int64_t> waitRemainingMs;
        std::optional<CityScriptCheckpoint> checkpoint;
        std::optional<std::string> trackedVehicle;
        bool dialogueShown = false;
        std::vector<std::string> killTargets;
    };
    static void releaseCheckpoint(Script& script, const RetailTravelHooks* travel);
    void freezeRunningScripts(uint32_t except, int32_t playerContext, const RetailTravelHooks* travel);
    CityScriptProgram program;
    std::vector<Script> scripts;
    std::array<std::optional<int64_t>, 2> fadeRemaining;
};

struct RetailMissionConditionResult {
    bool passes;
    std::optional<int32_t> timerMs;
    bool clearPhotoTargets = false;
};

class RetailBeQuick {
public:
    explicit RetailBeQuick(int32_t durationMs) : remaining(durationMs) {}
    RetailMissionConditionResult evaluate(int32_t elapsedMs, bool cinematic);

private:
    int32_t remaining;
};

class RetailPhotographQuickly {
public:
    explicit RetailPhotographQuickly(int32_t durationMs) : remaining(durationMs) {}
    RetailMissionConditionResult evaluate(int32_t elapsedMs, bool targetPhotographed);

private:
    int32_t remaining;
    bool photographed = false;
};

enum class RetailProtectionTarget { character, vehicle };
bool retailProtectionPasses(RetailProtectionTarget kind, bool dead, bool exploded);
std::string formatRetailMissionTimer(int32_t durationMs);

struct RetailStayWithin {
    explicit RetailStayWithin(int32_t distanceCm, int32_t warningTextId);

    bool evaluate(const std::array<float, 3>& position, const std::array<float, 3>& target, int32_t elapsedMs);
    std::optional<int32_t> warningText() const;
    int32_t remainingMs() const { return timer; }

private:
    static constexpr int32_t graceMs = 10000;
    int64_t radiusSquared;
    int32_t textId;
    int32_t timer = graceMs;
    bool outside = false;
};

struct RetailStayBetween {
    int32_t maximumCm, minimumCm;
    bool targetRemoved = false;

    std::optional<float> gauge(const std::array<float, 3>& position, const std::array<float, 3>& target) const;
};

struct RetailMissionTargetState {
    std::array<float, 3> position;
    bool dead = false;
    bool exploded = false;
    bool photographed = false;
};

using RetailMissionCondition = std::variant<RetailBeQuick, RetailPhotographQuickly,
                                            RetailStayWithin, RetailStayBetween, RetailProtectionTarget>;

struct RetailMissionConditionUpdate {
    std::optional<int32_t> failureCause;
    std::optional<int32_t> timerMs;
    std::optional<int32_t> warningText;
    bool clearPhotoTargets = false;
    std::optional<float> spyGauge;
};

class RetailMissionConditions {
public:
    bool add(RetailMissionCondition condition, std::string target = {}, int32_t failureCause = -1);
    bool add(const CityMissionConditionCommand& command);
    bool apply(const CityScriptCommand& command);
    void clear() { conditions.clear(); }
    void targetRemoved(std::string_view name);
    RetailMissionConditionUpdate update(int32_t elapsedMs, bool cinematic, const std::array<float, 3>& playerPosition,
        const std::function<std::optional<RetailMissionTargetState>(std::string_view)>& findTarget);

private:
    struct Entry {
        RetailMissionCondition condition;
        std::string target;
        int32_t failureCause;
    };
    std::vector<Entry> conditions;
};
