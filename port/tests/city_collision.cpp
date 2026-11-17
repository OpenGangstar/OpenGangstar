#include "city_assets.h"
#include "city_particles.h"
#include "city_view.h"
#include "filesystem.h"
#include "retail_mission.h"
#include "retail_achievements.h"
#include "retail_minimap.h"
#include "retail_save.h"
#include "retail_sprite.h"
#include "texture.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
CityTerrain flatTerrain()
{
    CityTerrain terrain;
    terrain.ground = {
        {{{-2000, -2000, 0}, {2000, -2000, 0}, {2000, 2000, 0}}},
        {{{-2000, -2000, 0}, {2000, 2000, 0}, {-2000, 2000, 0}}}
    };
    return terrain;
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void verifyScriptExecution()
{
    RetailScriptExecution execution({
        {{1, {0, 0, 0, 1}}, {3, {0, 0, 0, 2}}, {66, {}}},
        {{60, {}}},
        {{60, {}}}
    });
    using State = RetailScriptState;
    execution.unlock(0, 1, 0);
    require(execution.state(0) == State::frozen, "Script ignored its requested player context.");
    execution.unlock(0, -1, 0);
    int refreshes = 0, saves = 0, callbacks = 0;
    bool waiting = true;
    const auto dispatch = [&](const CityScriptCommand& command, int32_t elapsed) {
        require(elapsed == 16, "Script command received the wrong elapsed time.");
        ++callbacks;
        return command.opcode != 66 || !waiting;
    };
    const auto refresh = [&] { ++refreshes; };
    const auto save = [&] { ++saves; };
    const auto step = [&](uint32_t id) { execution.execute(id, 16, 0, dispatch, refresh, save); };
    step(0);
    step(0);
    require(execution.state(0) == State::waitingForChild && execution.state(1) == State::active &&
            callbacks == 0 && refreshes == 1 && saves == 0, "PlayScript did not suspend the parent.");
    step(1);
    require(execution.state(1) == State::locked && execution.state(0) == State::active &&
            callbacks == 1 && refreshes == 2 && saves == 0, "Child completion did not resume its parent.");
    step(0);
    require(execution.state(2) == State::active && callbacks == 2 && refreshes == 3 && saves == 0,
            "UnlockScript or the rejected command advanced incorrectly.");
    waiting = false;
    step(0);
    require(execution.state(0) == State::locked && callbacks == 3 && refreshes == 4 && saves == 1,
            "Root completion did not trigger a save.");
    execution.unlock(0, 0, 0);
    step(0);
    require(execution.state(1) == State::active, "Completed script did not rewind.");
}

void verifyScriptChapterCommands()
{
    RetailScriptExecution scripts({{{106, {0, 0, 0, 3, 'C', 'a', 'r'}},
                                     {47, {0, 0, 0x30, 0x67, 0, 0, 0x30, 0x66}},
                                     {111, {0, 0, 0, 4}},
                                     {99, {0, 0, 0, 6, 'P', 'l', 'a', 'y', 'e', 'r'}},
                                     {48, {}}, {113, {}}}});
    RetailAchievements catalog;
    catalog.stats.resize(25);
    RetailAchievementState achievements(std::move(catalog));
    RetailMissionHooks mission;
    std::string car;
    unsigned blinks = 0, saves = 0, shown = 0, hidden = 0, exits = 0;
    bool tutorials = false;
    mission.explodeVehicle = [&](std::string_view name) { car = name; };
    mission.tutorialsEnabled = [&] { return tutorials; };
    mission.showTutorial = [&](const CityScriptTutorial& tutorial) {
        require(tutorial.title == 0x3066 && tutorial.text == 0x3067, "Script tutorial IDs were reversed.");
        ++shown;
    };
    mission.actorGetOut = [&](std::string_view actor) {
        require(actor == "Player", "ActorGetOut did not decode the named actor.");
        ++exits;
    };
    mission.hideTutorial = [&] {
        require(exits == hidden + 1, "ActorGetOut did not dispatch before HideTutorial.");
        ++hidden;
    };
    mission.blinkHud = [&](int32_t target) {
        require(target == 4, "HUD blink target did not decode.");
        ++blinks;
    };
    mission.recordChapterEnd = [&] { achievements.recordChapterEnd(); };
    const auto run = [&](bool skip) {
        scripts.unlock(0, 0, 0);
        scripts.execute(0, 16, 0, [](const auto&, int32_t) { return false; }, [] {},
                        [&] { ++saves; }, skip, nullptr, {}, &mission);
    };
    run(false);
    require(car == "Car" && blinks == 0 && shown == 0 && hidden == 1 && achievements.stat(24) == 1 && saves == 1,
            "Vehicle explosion or chapter end did not dispatch with tutorials disabled.");
    tutorials = true;
    run(true);
    require(blinks == 1 && shown == 1 && hidden == 2 && achievements.stat(24) == 2 && saves == 2,
            "Cinema skip skipped a nonblocking HUD or chapter command.");
}

void verifyWaitForLock()
{
    RetailScriptExecution scripts({{{108, {0, 0, 0, 7, 'L', 'o', 'c', 'o', '_', '0', '0'}},
                                    {29, {0, 0, 0, 7, 'L', 'o', 'c', 'o', '_', '0', '0',
                                          0, 0, 0, 6, 'P', 'l', 'a', 'y', 'e', 'r'}}}});
    RetailMissionHooks mission;
    bool locked = false, killed = false, marked = false;
    unsigned saves = 0, attacks = 0;
    mission.actorLocked = [&](std::string_view target) { return locked && target == "Loco_00"; };
    mission.actorKilled = [&](std::string_view target) { return killed && target == "Loco_00"; };
    mission.markLockTarget = [&](std::string_view target, bool visible) {
        require(target == "Loco_00", "WaitForLock marked the wrong actor.");
        marked = visible;
    };
    mission.actorAttack = [&](const CityActorAttack& attack) {
        require(!marked && attack.attacker == "Loco_00" && attack.target == "Player",
                "ActorAttack dispatched before the marker cleared or reversed its actors.");
        ++attacks;
    };
    const auto step = [&](bool skip = false) {
        scripts.execute(0, 16, 0, [](const auto&, int32_t) { return false; }, [] {},
                        [&] { ++saves; }, skip, nullptr, {}, &mission);
    };
    scripts.unlock(0, 0, 0);
    step();
    require(marked && saves == 0 && attacks == 0, "WaitForLock did not mark and wait for the actor.");
    locked = true;
    step();
    require(!marked && saves == 1 && attacks == 1, "Target lock did not clear the mark and release ActorAttack.");
    locked = false;
    scripts.unlock(0, 0, 0);
    step();
    killed = true;
    step();
    require(!marked && saves == 2, "WaitForLock did not release after the actor died.");
    killed = false;
    scripts.unlock(0, 0, 0);
    step();
    step(true);
    require(!marked && saves == 3, "WaitForLock skip left its mark active.");
}

void verifyWaitKilled()
{
    const std::vector<uint8_t> loco{0, 0, 0, 7, 'L', 'o', 'c', 'o', '_', '0', '0'};
    RetailScriptExecution scripts({{{87, loco}, {48, {}}, {23, loco}}});
    RetailMissionHooks mission;
    unsigned cleared = 0, hidden = 0, saves = 0;
    std::vector<bool> immediateDisables;
    bool killed = false;
    mission.actorKilled = [&](std::string_view target) {
        require(target == "Loco_00", "WaitKilled did not resolve its named actor.");
        return killed;
    };
    mission.clearKilledTarget = [&](std::string_view target) {
        require(target == "Loco_00", "WaitKilled cleared the wrong target.");
        ++cleared;
    };
    mission.hideTutorial = [&] {
        require(cleared == hidden + 1, "WaitKilled did not clear the target before resuming.");
        ++hidden;
    };
    mission.disableActor = [&](std::string_view target, bool immediate) {
        require(target == "Loco_00" && hidden == immediateDisables.size() + 1,
                "Actor cleanup used the wrong target or ran before the tutorial was cleared.");
        immediateDisables.push_back(immediate);
    };
    const auto step = [&](bool skip) {
        scripts.execute(0, 16, 0, [](const auto&, int32_t) { return false; }, [] {},
                        [&] { ++saves; }, skip, nullptr, {}, &mission);
    };
    scripts.unlock(0, 0, 0);
    step(false);
    step(false);
    require(cleared == 0 && hidden == 0 && saves == 0, "WaitKilled advanced while its actor was alive.");
    killed = true;
    step(false);
    require(cleared == 1 && hidden == 1 && saves == 1, "WaitKilled did not resume after the actor died.");
    scripts.unlock(0, 0, 0);
    killed = false;
    step(true);
    require(cleared == 2 && hidden == 2 && saves == 2, "WaitKilled skip failed to clear the target.");
    require(immediateDisables == std::vector<bool>{false, true},
            "Actor cleanup did not distinguish normal playback from skip.");
}

void verifyScriptWait()
{
    RetailScriptExecution execution({
        {{57, {0, 0, 0, 32}}, {2, {0, 0, 0, 1}}},
        {{57, {0, 0, 0, 32}}}
    });
    using State = RetailScriptState;
    int refreshes = 0, saves = 0;
    const auto dispatch = [](const CityScriptCommand&, int32_t) {
        throw std::runtime_error("Native script command reached the host dispatcher.");
        return false;
    };
    const auto step = [&](uint32_t id, int32_t elapsed, bool skip = false) {
        execution.execute(id, elapsed, 0, dispatch, [&] { ++refreshes; }, [&] { ++saves; }, skip);
    };
    execution.unlock(0, -1, 0);
    execution.unlock(1, -1, 0);
    step(0, 16);
    step(1, 16);
    step(0, 16);
    require(execution.state(0) == State::active && refreshes == 0,
            "Retail Wait advanced when its remainder reached exactly zero.");
    step(0, 1);
    require(execution.state(0) == State::locked && execution.state(1) == State::locked &&
            refreshes == 2 && saves == 1, "Wait completion or LockScript state changed.");
    execution.unlock(1, -1, 0);
    step(1, 16);
    require(execution.state(1) == State::active && refreshes == 2,
            "LockScript lost the blocked script's remaining time.");
    step(1, 0, true);
    require(execution.state(1) == State::locked && refreshes == 3 && saves == 2,
            "Cutscene skip did not release the blocked Wait.");
    execution.unlock(1, -1, 0);
    step(1, 16);
    require(execution.state(1) == State::active && refreshes == 3,
            "Completed Wait did not reset its duration.");
}

void simulateParticles()
{
    CityParticleEmitter emitter{};
    emitter.maxParticles = 2;
    emitter.birthRate = 10;
    emitter.life = 1;
    emitter.targetSize = 20;
    emitter.sizeGrowthTime = 0.2f;
    emitter.sizeFadeTime = 0.25f;
    emitter.speed = 10;
    CityParticleSimulation simulation;
    emitter.direction = {0, 0, 0.5f};
    const CityParticleFrame origin{{0, 0, 50}};
    simulation.advance(emitter, 0.05f, 0, origin);
    require(simulation.particles().empty(), "Fractional particle birth was rounded up.");
    simulation.advance(emitter, 0.05f, 0, origin);
    const auto& first = simulation.particles().front();
    require(simulation.particles().size() == 1 && first.position[2] == 50.5f && first.size == 5,
            "Fractional birth, movement, or size growth changed.");
    constexpr CityCameraMatrix camera{{{2, 0, 0, 0}, {0, 3, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    std::array<CityVertex, 6> quad{};
    poseCityParticles(simulation.particles(), camera, quad.data());
    require(quad[0].position == std::array<float, 3>{-2.5f, -2.5f, 50.5f} &&
            quad[5].position == std::array<float, 3>{2.5f, 2.5f, 50.5f} &&
            quad[0].uv == std::array<float, 2>{0, 0} && quad[5].uv == std::array<float, 2>{1, 1},
            "Particle billboard was stretched by the camera projection or lost its UVs.");
    simulation.advance(emitter, 0.2f, 0, origin);
    require(simulation.particles().size() == 2, "Particle limit was exceeded.");
    emitter.birthRate = 0;
    simulation.advance(emitter, 0.65f, 0, origin);
    require(std::abs(simulation.particles().front().size - 8) < 0.001f, "Particle size did not fade.");
    simulation.advance(emitter, 0.25f, 0, origin);
    require(simulation.particles().empty(), "Expired particles were not removed.");
    const CityParticleFrame tilted{{100, 200, 300}, {{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}}};
    emitter.birthRate = 10;
    simulation.advance(emitter, 0.1f, 0, tilted);
    require(simulation.particles().size() == 1 &&
            simulation.particles().front().velocity == std::array<float, 3>{10, 0, 0} &&
            simulation.particles().front().position == std::array<float, 3>{101, 200, 300},
            "Particle motion stayed in local space after emitter rotation.");
    CityParticleSimulation retailRounding;
    emitter.birthRate = 50;
    retailRounding.advance(emitter, 0.02f, 0, origin);
    require(retailRounding.particles().size() == 1,
            "Particle generation missed the retail single-precision birth boundary.");

    emitter.maxParticles = 50;
    emitter.speed = 0;
    emitter.radius = 4;
    emitter.height = 12;
    for (const auto shape : {1u, 2u}) {
        emitter.type = shape;
        CityParticleSimulation volume;
        volume.advance(emitter, 0.5f, 0, {});
        require(volume.particles().size() == 25, "Particle volume generated the wrong birth count.");
        bool reachedUpperHalf = false;
        bool reachedLowerHalf = false;
        for (const auto& particle : volume.particles()) {
            const auto [x, y, z] = particle.position;
            const auto radialSquared = x * x + z * z;
            require(shape == 1 ? radialSquared + y * y <= 16.001f
                               : radialSquared <= 16.001f && std::abs(y) <= 6,
                    "Particle spawn escaped its emission volume.");
            reachedUpperHalf |= y > 0;
            reachedLowerHalf |= y < 0;
        }
        require(reachedUpperHalf && reachedLowerHalf, "Particle volume did not span both sides of its center.");
    }
}

void verifyScriptCheckpoints()
{
    RetailScriptExecution execution({
        {{105, {0, 0, 0, 4, 'G', 'a', 't', 'e', 0, 0, 0, 3, 'C', 'a', 'r'}}},
        {{2, {0, 0, 0, 0}}},
        {{64, {0, 0, 0, 4, 'G', 'a', 't', 'e'}}}
    });
    using State = RetailScriptState;
    int active = 0, inactive = 0, queries = 0, saves = 0;
    std::string vehicle;
    const RetailTravelHooks hooks{
        [&](std::string_view name, bool enabled) {
            require(name == "Gate", "Wrong checkpoint bound to script.");
            if (enabled) ++active;
            else ++inactive;
        },
        [&](const CityScriptCheckpoint& checkpoint) {
            ++queries;
            return !checkpoint.vehicle || *checkpoint.vehicle == vehicle;
        }
    };
    const auto step = [&](uint32_t id, bool skip = false) {
        execution.execute(id, 16, 0, [](const CityScriptCommand&, int32_t) { return false; },
                          [] {}, [&] { ++saves; }, skip, &hooks);
    };
    execution.unlock(0, -1, 0);
    step(0);
    step(0);
    require(active == 1 && queries == 2 && inactive == 0 && saves == 0,
            "Waiting for the named vehicle reactivated or consumed the checkpoint.");
    execution.unlock(1, -1, 0);
    step(1);
    require(execution.state(0) == State::locked && inactive == 1 && saves == 1,
            "LockScript did not release the target's checkpoint.");
    vehicle = "Car";
    execution.unlock(0, -1, 0);
    step(0);
    require(active == 2 && inactive == 2 && queries == 3 && saves == 2,
            "Unlocked checkpoint did not reactivate and complete for the named vehicle.");
    execution.unlock(2, -1, 0);
    step(2, true);
    require(execution.state(2) == State::locked && active == 2 && inactive == 2 && saves == 3,
            "Skipping an unbound checkpoint changed its marker state.");
}

void verifyScriptEscape()
{
    const CityScriptCommand command{101, {0, 0, 0, 4, 'G', 'a', 't', 'e', 0, 0, 0, 5}};
    RetailScriptExecution execution(CityScriptProgram{{command}});
    std::optional<RetailEscapePositions> positions;
    int lookups = 0, saves = 0;
    const RetailTravelHooks travel{{}, {}, [&](std::string_view name) {
        require(name == "Gate", "WaitEscape resolved the wrong target.");
        ++lookups;
        return positions;
    }};
    const auto step = [&](bool skip = false) {
        execution.execute(0, 16, 0, [](const CityScriptCommand&, int32_t) { return false; },
                          [] {}, [&] { ++saves; }, skip, &travel);
    };
    execution.unlock(0, -1, 0);
    step();
    positions = RetailEscapePositions{{10, 10, 14}, {10, 10, 10}};
    step();
    require(lookups == 2 && saves == 0, "WaitEscape advanced without a target or below its radius.");
    positions->player[2] = 15;
    step();
    require(lookups == 3 && saves == 1, "WaitEscape rejected the exact vertical boundary.");
    execution.unlock(0, -1, 0);
    step(true);
    require(lookups == 3 && saves == 2, "Skipping WaitEscape still required a world lookup.");
}

void verifyScriptAudio()
{
    const std::vector<CityScriptCommand> commands{
        {60, {0, 0, 1, 16}}, {61, {0, 0, 1, 16}}, {62, {0, 0, 0, 7}}, {63, {0, 0, 0, 7}}
    };
    RetailScriptExecution execution({commands});
    std::vector<std::pair<CityAudioAction, uint32_t>> audio;
    const auto run = [&](bool skip) {
        execution.unlock(0, -1, 0);
        execution.execute(0, 16, 0, [](const CityScriptCommand&, int32_t) { return false; },
                          [] {}, [] {}, skip, nullptr,
                          [&](const CityAudioCue& cue) { audio.emplace_back(cue.action, cue.soundId); });
    };
    using Action = CityAudioAction;
    run(false);
    require(audio == decltype(audio){{Action::playOnce, 272}, {Action::stop, 272},
                                     {Action::playPriorityLoop, 7}, {Action::stop, 7}},
            "Retail sound opcodes lost their playback mode, order, or sound ID.");
    audio.clear();
    run(true);
    require(audio == decltype(audio){{Action::stop, 272}, {Action::stop, 7}},
            "Skipping a retail cutscene suppressed stop commands or started audio.");
}

void verifyScriptConditions()
{
    RetailScriptExecution execution({
        {{70, {0, 0, 0, 3, 'C', 'a', 'r', 0, 0, 0, 10}}, {57, {0, 0, 0, 0}},
         {68, {}}, {66, {0, 0, 0x40, 0x10}}}
    });
    RetailMissionConditions conditions;
    std::optional<uint32_t> objective;
    const RetailMissionHooks mission{&conditions, [&](uint32_t textId) { objective = textId; }};
    execution.unlock(0, -1, 0);
    const auto step = [&](int32_t elapsed) {
        execution.execute(0, elapsed, 0, [](const CityScriptCommand&, int32_t) { return false; },
                          [] {}, [] {}, false, nullptr, {}, &mission);
    };
    int lookups = 0;
    const auto update = [&] {
        return conditions.update(16, false, {}, [&](std::string_view name) {
            require(name == "Car", "Wrong protection target dispatched.");
            ++lookups;
            return std::optional{RetailMissionTargetState{{}, false, false}};
        });
    };
    step(0);
    update();
    require(lookups == 1 && !objective, "Interpreter advanced past Wait while registering ProtectCar.");
    step(1);
    update();
    require(lookups == 1 && execution.state(0) == RetailScriptState::locked,
            "MissionClearConditions did not remove the registered condition.");
    require(objective == 0x4010, "MissionObjective did not dispatch the packed retail text ID.");
}

void verifyMissionCompletion()
{
    RetailScriptExecution execution({{{67, {}}}, {{60, {}}}, {{60, {}}}, {{60, {}}}});
    using State = RetailScriptState;
    execution.restoreUnlocks({{0, int32_t{0}}, {15, SavePair{1, 1}},
                              {12, int32_t{1}}, {15, SavePair{2, 0}},
                              {0, int32_t{3}}, {0, int32_t{100}}}, 0);
    require(execution.state(0) == State::active && execution.state(1) == State::frozen &&
            execution.state(2) == State::frozen && execution.state(3) == State::active,
            "Saved scripts did not respect the ordered player context.");
    int finishes = 0, saves = 0;
    const RetailMissionHooks mission{nullptr, {}, [&] {
        require(execution.state(1) == State::frozen, "Script woke before the mission completion callback.");
        ++finishes;
    }};
    execution.execute(0, 16, 1, [](const CityScriptCommand&, int32_t) { return false; },
                      [] {}, [&] { ++saves; }, false, nullptr, {}, &mission);
    require(finishes == 1 && saves == 1 && execution.state(1) == State::active &&
            execution.state(2) == State::frozen, "MissionOver did not release the matching frozen scripts.");
    execution.unlockFrozenScripts(0);
    require(execution.state(2) == State::active, "Frozen script could not resume in its saved context.");
}

void verifyScriptReset()
{
    RetailScriptExecution execution({{{4, {}}, {57, {0, 0, 0, 10}}}, {{60, {}}}, {{4, {}}}});
    using State = RetailScriptState;
    int refreshes = 0, saves = 0;
    const auto step = [&](uint32_t id, int32_t elapsed) {
        execution.execute(id, elapsed, 0, [](const CityScriptCommand&, int32_t) { return false; },
                          [&] { ++refreshes; }, [&] { ++saves; });
    };
    execution.reset(0, false);
    require(execution.state(0) == State::active && execution.state(1) == State::locked &&
            execution.state(2) == State::active, "New game did not select all opcode-4 startup scripts.");
    step(0, 9);
    require(refreshes == 1 && saves == 0, "Startup marker did not advance to the timer.");
    execution.reset(0, false);
    step(0, 2);
    require(refreshes == 2 && saves == 0, "Game reset preserved the previous command or timer.");
    execution.reset(0, true);
    require(execution.state(0) == State::locked && execution.state(2) == State::locked,
            "Save loading activated default new-game startup scripts.");
    execution.reset(0, true, 1);
    require(execution.state(0) == State::locked && execution.state(1) == State::active &&
            execution.state(2) == State::locked, "Explicit startup did not activate just its selected script.");
}

void verifyScriptTick()
{
    RetailScriptExecution execution({
        {{60, {0}}},
        {{3, {0, 0, 0, 0}}, {1, {0, 0, 0, 2}}, {60, {1}}},
        {{60, {2}}}
    });
    using State = RetailScriptState;
    std::vector<uint8_t> visited;
    int saves = 0, refreshes = 0;
    const auto tick = [&] {
        execution.tick(16, 0, [&](const CityScriptCommand& command, int32_t elapsed) {
            require(elapsed == 16, "Script tick changed elapsed time between scripts.");
            visited.push_back(command.payload.at(0));
            return true;
        }, [&] { ++refreshes; }, [&] { ++saves; });
    };
    execution.unlock(1, -1, 0);
    tick();
    require(visited == std::vector<uint8_t>{2} && refreshes == 3 && saves == 0 &&
            execution.state(0) == State::active && execution.state(1) == State::active,
            "Script tick missed a newly active later script or reran an earlier index.");
    tick();
    require(visited == std::vector<uint8_t>{2, 0, 1} && saves == 2 && refreshes == 5,
            "Script tick did not finish the earlier script and resume the parent in index order.");
}

void verifyCinematicCommands()
{
    RetailScriptExecution execution({{{15, {}}, {16, {}}, {17, {}}, {18, {}}}});
    using Phase = RetailCinematicPhase;
    auto phase = Phase::gameplay;
    std::vector<Phase> requests;
    int refreshes = 0, saves = 0;
    const RetailMissionHooks mission{nullptr, {}, {}, [&] { return phase; },
        [&](Phase requested, bool skip) {
            requests.push_back(requested);
            phase = skip && requested == Phase::leavingStartup ? Phase::gameplay : requested;
        }};
    const auto tick = [&](bool skip = false) {
        execution.tick(16, 0, [](const CityScriptCommand&, int32_t) { return false; },
                       [&] { ++refreshes; }, [&] { ++saves; }, skip, nullptr, {}, &mission);
    };
    execution.unlock(0, -1, 0);
    tick();
    tick();
    require(requests == std::vector{Phase::enteringMission} && refreshes == 0,
            "Cinematic entry advanced before the transition or requested it twice.");
    phase = Phase::mission;
    tick();
    require(requests.back() == Phase::leavingMission && refreshes == 1 && saves == 0,
            "Cinematic exit did not wait for gameplay.");
    phase = Phase::gameplay;
    tick(true);
    require(requests == std::vector{Phase::enteringMission, Phase::leavingMission,
                                    Phase::startup, Phase::leavingStartup} &&
            phase == Phase::gameplay && refreshes == 4 && saves == 1,
            "Skipping the startup cinematic did not complete its entry and exit.");
}

void verifyScriptCameras()
{
    RetailScriptExecution execution({{{12, {0, 0, 0, 3, 'C', 'a', 'm', 0, 0, 1, 244}},
                                      {13, {0, 0, 3, 232}}}});
    std::vector<CityScriptCamera> cameras;
    RetailMissionHooks mission{};
    mission.selectCamera = [&](const CityScriptCamera& camera) { cameras.push_back(camera); };
    const auto run = [&](bool skip) {
        execution.unlock(0, -1, 0);
        execution.tick(16, 0, [](const CityScriptCommand&, int32_t) { return false; },
                       [] {}, [] {}, skip, nullptr, {}, &mission);
    };
    run(false);
    run(true);
    require(cameras.size() == 4 && cameras[0].scene == "Cam" && cameras[0].transitionMs == 500 &&
            !cameras[1].scene && cameras[1].transitionMs == 1000 &&
            cameras[2].scene == "Cam" && cameras[2].transitionMs == 0 &&
            !cameras[3].scene && cameras[3].transitionMs == 1000,
            "Script camera selection, blend timing, or skip behavior changed.");
}

void verifyScriptFades()
{
    require(retailMissionRevealBrightness(0) == 0 && retailMissionRevealBrightness(500) == 128.f / 255 &&
            retailMissionRevealBrightness(1000) == 1 && retailMissionRevealBrightness(1500) == 1,
            "Mission reveal did not preserve its one-second duration or integer alpha.");
    RetailScriptExecution execution({{{41, {0, 0, 0, 10}}, {42, {0, 0, 0, 10}}}});
    std::vector<uint8_t> opacity;
    RetailMissionHooks mission{};
    mission.setFadeOpacity = [&](uint8_t alpha) { opacity.push_back(alpha); };
    int saves = 0;
    const auto tick = [&](int32_t elapsed, bool skip = false) {
        execution.tick(elapsed, 0, [](const CityScriptCommand&, int32_t) { return false; },
                       [] {}, [&] { ++saves; }, skip, nullptr, {}, &mission);
    };
    execution.unlock(0, -1, 0);
    tick(8);
    tick(5);
    tick(10);
    require(opacity == std::vector<uint8_t>{255, 127, 0} && saves == 0,
            "FadeIn consumed its entry tick, rounded alpha incorrectly, or advanced early.");
    tick(0);
    require(opacity == std::vector<uint8_t>{255, 127, 0, 0, 0},
            "FadeOut did not start transparent after FadeIn completed.");
    tick(5);
    require(opacity.back() == 128 && saves == 0, "FadeOut half opacity or timing changed.");
    tick(0, true);
    require(opacity.back() == 255 && saves == 1, "Skipping FadeOut did not finish opaque.");
}

void verifyScriptActors()
{
    const std::vector<uint8_t> markerAndActor{0, 0, 0, 4, 'G', 'a', 't', 'e', 0, 0, 0, 1, 'P'};
    RetailScriptExecution execution({{{21, markerAndActor}, {24, markerAndActor},
        {6, {0, 0, 0, 1, 'A', 0, 0, 0, 1, 'P'}},
        {7, {0, 0, 0, 1, 'A', 0, 0, 0, 1, 'B', 0, 0, 0, 1, 'P'}},
        {22, {0, 0, 0, 1, 'P'}}}});
    using Action = CityScriptActorAction;
    std::vector<Action> placements;
    std::vector<std::optional<std::string>> loops;
    RetailMissionHooks mission{};
    mission.placeActor = [&](const CityScriptActorPlacement& placement) {
        require(placement.actor == "P" && placement.marker == (placement.action == Action::disable ? "" : "Gate"),
                "Actor command decoded its marker and actor in the wrong order.");
        placements.push_back(placement.action);
    };
    mission.animateActor = [&](const CityScriptActorAnimation& animation) {
        require(animation.actor == "P" && animation.first == "A" && placements.size() == 2,
                "Script animation decoded incorrectly or ran before actor placement.");
        loops.push_back(animation.loop);
    };
    execution.unlock(0, -1, 0);
    execution.tick(16, 0, [](const CityScriptCommand&, int32_t) { return false; },
                   [] {}, [] {}, false, nullptr, {}, &mission);
    require(placements == decltype(placements){Action::enable, Action::moveTo, Action::disable} &&
            loops == decltype(loops){std::nullopt, std::string("B")} &&
            execution.state(0) == RetailScriptState::locked,
            "Script actor placement and animation commands did not all dispatch.");
}

void verifyMissionStart()
{
    RetailScriptExecution scripts({
        {{64, {0, 0, 0, 4, 'G', 'a', 't', 'e'}}},
        {{65, {0, 0, 0, 7}}, {67, {}}},
        {{57, {0, 0, 0, 20}}}
    });
    using State = RetailScriptState;
    unsigned activated = 0, released = 0, finished = 0;
    const RetailTravelHooks travel{
        [&](std::string_view name, bool enabled) {
            require(name == "Gate", "MissionStart released the wrong checkpoint.");
            if (enabled) ++activated;
            else ++released;
        },
        [](const CityScriptCheckpoint&) { return false; }
    };
    int32_t phase = 0;
    std::vector<int32_t> durations;
    RetailMissionHooks mission{};
    mission.missionState = [&] { return phase; };
    mission.startMission = [&](uint32_t id, int32_t duration) {
        require(id == 7, "MissionStart decoded the wrong mission index.");
        durations.push_back(duration);
        phase = 1;
    };
    mission.finishMission = [&] { ++finished; phase = 0; };
    const auto step = [&](uint32_t id, int32_t context, bool skip = false) {
        scripts.execute(id, 16, context, [](const CityScriptCommand&, int32_t) { return false; },
                        [] {}, [] {}, skip, &travel, {}, &mission);
    };
    scripts.unlock(0, -1, 0);
    scripts.unlock(1, -1, 0);
    scripts.unlock(2, -1, 1);
    step(0, 0);
    step(1, 0);
    step(1, 0);
    require(activated == 1 && released == 1 && durations == std::vector<int32_t>{-1} &&
            scripts.state(0) == State::frozen && scripts.state(1) == State::active &&
            scripts.state(2) == State::active && finished == 0,
            "MissionStart repeated, advanced early, or froze the wrong scripts.");
    phase = 3;
    step(1, 0);
    require(scripts.state(0) == State::active && scripts.state(1) == State::locked && finished == 1,
            "Mission aborted its transition or failed to resume the checkpoint script.");
    scripts.unlock(1, -1, 0);
    step(1, 0, true);
    require(durations == std::vector<int32_t>{-1, 0} && finished == 2,
            "Skipped MissionStart did not use a zero-duration title and advance.");
}

void verifyScriptDialogueAndWeaponHud()
{
    RetailScriptExecution scripts({{{80, {0, 0, 0, 0}},
        {56, {0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0x40, 2}}, {81, {}}}});
    unsigned shown = 0, cleared = 0, saved = 0;
    int32_t weaponManagerLimit = 0;
    std::vector<int32_t> hudLimits;
    bool dialogueFinished = false;
    RetailMissionHooks mission{};
    mission.showDialogue = [&](const CityScriptDialogue& dialogue) {
        require(dialogue.durationMs == 0 && dialogue.speaker == 5 && dialogue.textId == 0x4002,
                "Dialog did not preserve its manual-advance flag, speaker, and text ID.");
        ++shown;
    };
    mission.dialogueComplete = [&] { return dialogueFinished; };
    mission.clearDialogue = [&] { ++cleared; };
    mission.limitHudWeapon = [&](int32_t limit) { hudLimits.push_back(limit); };
    mission.weaponManagerLimit = [&] { return weaponManagerLimit; };
    const auto tick = [&](bool skip = false) {
        scripts.tick(16, 0, [](const CityScriptCommand&, int32_t) { return false; },
                     [] {}, [&] { ++saved; }, skip, nullptr, {}, &mission);
    };
    scripts.unlock(0, -1, 0);
    tick();
    tick();
    require(shown == 1 && cleared == 0 && saved == 0 && hudLimits == std::vector<int32_t>{0},
            "Dialog advanced before dismissal or played twice.");
    dialogueFinished = true;
    tick();
    require(shown == 1 && cleared == 1 && saved == 1 && hudLimits == std::vector<int32_t>{0, -1},
            "Dialog did not clear or release the weapon HUD limit.");
    weaponManagerLimit = -1;
    scripts.unlock(0, -1, 0);
    tick(true);
    require(shown == 1 && cleared == 2 && saved == 2 && hudLimits == std::vector<int32_t>{0, -1, 0},
            "Skipped Dialog showed a caption or reset an already-unlimited weapon HUD.");
}

void verifyWaitStolen()
{
    const CityScriptCommand wait{89, {0, 0, 0, 4, 'C', 'a', 'r', 's'}};
    RetailScriptExecution scripts(CityScriptProgram{{wait}});
    std::string occupied;
    std::vector<bool> tracking;
    const RetailTravelHooks travel{{}, {}, {}, [&]() -> std::string_view { return occupied; },
        [&](std::string_view name, bool visible) {
            require(name == "Cars", "WaitStolen highlighted the wrong vehicle.");
            tracking.push_back(visible);
        }};
    const auto tick = [&](bool skip = false) {
        scripts.tick(16, 0, [](const CityScriptCommand&, int32_t) { return false; },
                     [] {}, [] {}, skip, &travel);
    };
    scripts.unlock(0, -1, 0);
    tick();
    occupied = "cars";
    tick();
    require(tracking == std::vector<bool>{true} && scripts.state(0) == RetailScriptState::active,
            "WaitStolen accepted the wrong vehicle or republished the marker.");
    occupied = "Cars";
    tick();
    require(tracking == std::vector<bool>{true, false} && scripts.state(0) == RetailScriptState::locked,
            "Entering the named vehicle did not clear WaitStolen.");
    scripts.unlock(0, -1, 0);
    occupied.clear();
    tick(true);
    require(tracking.size() == 2 && scripts.state(0) == RetailScriptState::locked,
            "Skipping WaitStolen activated a vehicle marker.");
}

void walkAndSlide()
{
    CityOpening world;
    world.walkSpeed = 140;
    CityActor player{};
    player.name = "Player";
    player.position = {-150, 0, 0};
    world.actors.push_back(player);
    auto terrain = flatTerrain();
    terrain.walls = {
        {{{0, -2000, 0}, {0, 2000, 0}, {0, 2000, 200}}},
        {{{0, -2000, 0}, {0, 2000, 200}, {0, -2000, 200}}}
    };

    CityPlayback playback;
    for (int i = 0; i < 100; ++i) playback.walk(world, terrain, {1, 0}, 20);
    const auto atWall = playback.player->position;
    require(atWall[0] <= -28 && atWall[0] > -32, "Player crossed or stopped too far from a wall.");

    for (int i = 0; i < 20; ++i) playback.walk(world, terrain, {1, 1}, 20);
    require(playback.player->position[0] <= -28 && playback.player->position[1] < -30,
            "Player could not slide along a wall.");

    for (auto& wall : terrain.walls)
        for (auto& vertex : wall) vertex[2] -= 250;
    for (int i = 0; i < 40; ++i) playback.walk(world, terrain, {1, 0}, 20);
    require(playback.player->position[0] > 0, "Obstacle below the deck blocked movement.");
}

void driveAndReverse(CityTerrain::VehicleFootprint footprint = {}, float damageScale = 0.3f)
{
    CityOpening world;
    world.checkpoint.emplace();
    world.vehicleActor = 0;
    CityActor car{};
    car.position = {0, 500, 0};
    car.vehicleHealth = 500;
    car.collisionDamageScale = damageScale;
    car.gearProfile = 1;
    world.actors.push_back(car);
    auto terrain = flatTerrain();
    terrain.vehicleFootprint = footprint;
    terrain.walls = {
        {{{-2000, 0, 0}, {2000, 0, 0}, {2000, 0, 200}}},
        {{{-2000, 0, 0}, {2000, 0, 200}, {-2000, 0, 200}}}
    };

    CityPlayback playback;
    playback.player = CityPlayerPose{{0, 600, 0}, 0};
    playback.timeAfterCheckpointMs = 0;
    playback.drive(world, terrain, {}, 20, true);
    playback.drive(world, terrain, {0, 1}, 20, false);
    playback.drive(world, terrain, {0, -1}, 20, false);
    require(playback.vehicle->lightEffects == std::array{true, false},
            "Forward braking did not select the first pair of lights.");
    require(playback.vehicle->lightTimeMs == 0, "Selecting a light pair did not restart its timeline.");
    playback.drive(world, terrain, {0, -1}, 20, false);
    require(playback.vehicle->lightEffects == std::array{true, false},
            "Holding the brake at rest did not retain the selected light pair.");
    require(playback.vehicle->lightTimeMs == 20, "A retained light pair did not advance its timeline.");
    for (int i = 0; i < 100; ++i) playback.drive(world, terrain, {0, 1}, 20, false);
    require(playback.vehicle->lightEffects == std::array{false, false},
            "Releasing the brake did not hide both pairs of lights.");
    require(playback.vehicle->lightTimeMs == 0, "Brake release did not reset the light timeline.");
    const auto atWall = playback.vehicle->position;
    const auto bumper = footprint.longitudinalCenters.back() + footprint.radius;
    require(atWall[1] >= bumper && atWall[1] < bumper + 16,
            "Car bumper crossed or stopped too far from a wall.");
    require(playback.vehicle->speed == 0, "Collision did not stop the car.");
    require(playback.vehicle->health < 500, "Hard wall impact did not reduce vehicle health.");
    require(playback.vehicle->peakCollisionDamage == 500 - playback.vehicle->health,
            "The first wall hit did not record its applied health loss.");
    auto unoccupied = *playback.vehicle;
    unoccupied.occupied = false;
    unoccupied.peakCollisionDamage = 0;
    unoccupied.applyWallImpact({10, 0, 0});
    require(unoccupied.peakCollisionDamage == 0, "An unoccupied collision was recorded for the player.");
    require(playback.vehicle->rearDeformation == 0, "A front wall impact deformed the rear.");
    require(playback.vehicle->frontDeformation > 0, "A damaging front impact did not deform the front.");
    const auto healthAtWall = playback.vehicle->health;
    for (int i = 0; i < 10; ++i) playback.drive(world, terrain, {0, 1}, 20, false);
    require(playback.vehicle->health == healthAtWall, "Low-speed wall contact caused repeated damage.");
    const auto travelAtWall = playback.vehicle->wheelTravel;
    playback.drive(world, terrain, {0, 0}, 20, false);
    require(playback.vehicle->wheelTravel == travelAtWall, "Stopped vehicle accumulated wheel travel.");
    for (int i = 0; i < 25; ++i) playback.drive(world, terrain, {0, -1}, 20, false);
    require(playback.vehicle->lightEffects == std::array{false, true},
            "Reversing with the brake held did not select the second pair of lights.");
    require(playback.vehicle->position[1] > atWall[1] + 40, "Car could not reverse away from a wall.");
    require(playback.vehicle->wheelTravel < travelAtWall, "Reversing did not roll the wheels backward.");
    require(playback.vehicle->gear == -1, "Retail gearbox did not select reverse when driving backward.");

    auto& vehicle = *playback.vehicle;
    const float rearWall = vehicle.position[1] - footprint.longitudinalCenters.front() + footprint.radius + 250;
    terrain.walls = {
        {{{-2000, rearWall, 0}, {2000, rearWall, 0}, {2000, rearWall, 200}}},
        {{{-2000, rearWall, 0}, {2000, rearWall, 200}, {-2000, rearWall, 200}}}
    };
    for (int i = 0; i < 100; ++i) playback.drive(world, terrain, {0, -1}, 20, false);
    const float firstDeformation = 0.126f * damageScale;
    require(vehicle.speed == 0 && std::abs(vehicle.rearDeformation - firstDeformation) < 0.0001f,
            "The reverse wall impact did not use the retail velocity conversion.");
    vehicle.speed = -800;
    playback.drive(world, terrain, {}, 20, false);
    const float expectedDeformation = firstDeformation + 0.288f * std::exp(-3.2f * 0.02f) * damageScale;
    require(vehicle.speed == 0 && std::abs(vehicle.rearDeformation - expectedDeformation) < 0.0001f,
            "A damaging rear wall impact did not accumulate the projected deformation.");

    vehicle.applyWallImpact({0, 1000, 0});
    require(vehicle.rearDeformation == 1.f, "Retail rear deformation did not saturate at one.");
    vehicle.lightEffects = {false, true};
    vehicle.speed = 100;
    vehicle.updateLights(true);
    require(vehicle.lightEffects == std::array{false, true},
            "The rear deformation gate did not retain the previous pair.");
    vehicle.updateLights(false);
    vehicle.updateLights(true);
    require(vehicle.lightEffects == std::array{false, false},
            "A deformed rear activated a light pair after brake release.");

    vehicle.speed = -100;
    vehicle.shiftGears(1);
    vehicle.speed = 0;
    vehicle.shiftGears(1);
    require(vehicle.gear == 1, "Retail gearbox did not return to first from reverse.");
    vehicle.speed = 1249;
    vehicle.shiftGears(1);
    require(vehicle.gear == 1, "Lincoln shifted below its retail 45 km/h threshold.");
    vehicle.speed = 1250;
    vehicle.shiftGears(1);
    require(vehicle.gear == 2, "Lincoln did not shift at its retail 45 km/h threshold.");
    vehicle.speed = 3000;
    vehicle.shiftGears(1);
    require(vehicle.gear == 3, "Retail gearbox skipped a gear in one update.");
    vehicle.speed = 1249;
    vehicle.shiftGears(1);
    require(vehicle.gear == 2, "Retail gearbox did not downshift one gear in one update.");
}

void poseRetailDoor(const std::filesystem::path& root)
{
    ResourceFileSystem assets(root);
    const auto inventory = auditCityScripts(assets);
    RetailScriptExecution scripts(loadCityScriptProgram(assets));
    using ScriptState = RetailScriptState;
    scripts.reset(0, false);
    require(inventory.chapterStarts == std::vector<uint32_t>{94, 32, 45, 48, 52, 67} &&
            scripts.state(94) == ScriptState::active && scripts.state(17) == ScriptState::locked,
            "Retail chapter startup data or default new-game activation changed.");
    std::vector<uint32_t> startupDispatch;
    unsigned startupRefreshes = 0;
    using Phase = RetailCinematicPhase;
    auto startupPhase = Phase::gameplay;
    std::vector<CityScriptCamera> startupCameras;
    std::vector<uint8_t> startupOpacity;
    std::vector<int32_t> startupAnimations;
    std::vector<std::pair<CityAudioAction, uint32_t>> startupAudio;
    unsigned startupSaves = 0;
    const RetailMissionHooks startupMission{nullptr, {}, {}, [&] { return startupPhase; },
        [&](Phase phase, bool) { startupPhase = phase; },
        [&](const CityScriptCamera& camera) { startupCameras.push_back(camera); },
        [&](uint8_t opacity) { startupOpacity.push_back(opacity); },
        [&](int32_t animation) { startupAnimations.push_back(animation); }};
    const auto tickStartup = [&] { scripts.tick(16, 0, [&](const CityScriptCommand& command, int32_t) {
        startupDispatch.push_back(command.opcode);
        return false;
    }, [&] { ++startupRefreshes; }, [&] { ++startupSaves; }, false, nullptr,
        [&](const CityAudioCue& cue) { startupAudio.emplace_back(cue.action, cue.soundId); },
        &startupMission); };
    tickStartup();
    require(startupDispatch.empty() && startupRefreshes == 1 && startupPhase == Phase::enteringStartup,
            "Retail startup did not request its cinematic transition.");
    startupPhase = Phase::startup;
    tickStartup();
    require(startupDispatch.empty() && startupRefreshes == 5 &&
            startupCameras.size() == 2 && startupCameras[0].scene == "SceneCamera_C2M0_08" &&
            startupCameras[0].transitionMs == 0 && startupCameras[1].scene == "SceneCamera_C2M0_07" &&
            startupCameras[1].transitionMs == 20000,
            "Retail startup did not dispatch its two scene cameras in authored order.");
    for (int frame = 0; frame < 2500 && scripts.state(94) == ScriptState::active; ++frame) {
        if (startupPhase == Phase::leavingStartup) startupPhase = Phase::gameplay;
        tickStartup();
    }
    require(startupDispatch.empty() && startupRefreshes == 15 && startupSaves == 1 &&
            scripts.state(94) == ScriptState::locked && scripts.state(17) == ScriptState::active &&
            startupAnimations == std::vector<int32_t>{1, -1} &&
            startupAudio == decltype(startupAudio){{CityAudioAction::playOnce, 4}, {CityAudioAction::stop, 4}} &&
            !startupOpacity.empty() && startupOpacity.front() == 255 && startupOpacity.back() == 255,
            "Retail startup failed to reach the scripted handoff with its fade, sound, and animation commands.");
    int32_t openingMissionState = 0;
    std::optional<uint32_t> openingMissionId;
    auto openingHost = startupMission;
    openingHost.missionState = [&] { return openingMissionState; };
    openingHost.startMission = [&](uint32_t id, int32_t duration) {
        require(duration == -1, "Retail mission did not request its default title duration.");
        openingMissionId = id;
        openingMissionState = 1;
    };
    scripts.tick(16, 0, [](const CityScriptCommand&, int32_t) -> bool {
        throw std::runtime_error("Retail opening advanced past the mission title gate.");
    }, [] {}, [] {}, false, nullptr, {}, &openingHost);
    require(openingMissionId == 0 && openingMissionState == 1 && startupPhase == Phase::gameplay,
            "Script 17 did not start retail mission 0 and wait for its title.");
    for (const auto id : inventory.chapterStarts) {
        scripts.reset(0, false, id);
        require(scripts.state(id) == ScriptState::active, "Retail chapter startup could not be activated.");
    }
    if (const std::unique_ptr<IFileStream> save(assets.openResource(retailSaveFilename)); save) {
        scripts.reset(0, true);
        scripts.restoreUnlocks(readRetailSave(*save), 0);
        require(scripts.state(17) == ScriptState::active && scripts.state(94) == ScriptState::locked,
                "Supplied retail save did not restore script 17 in place of new-game startup.");
    }
    const auto city = loadCityAssets(assets);
    require(!city.spawnGroups.empty(), "Retail spawn group table was empty.");
    for (const auto& district : city.districts) {
        for (const auto& point : district.spawnPoints) {
            const auto& group = city.spawnGroups.at(point.group);
            require(group.objectType == point.objectType && group.metatypes.size() == point.values.size() &&
                    std::equal(group.metatypes.begin(), group.metatypes.end(), point.values.begin()),
                    "Retail spawn point did not bind to its authored group.");
            require(group.choose(0) == group.metatypes.front() &&
                    group.choose(static_cast<uint32_t>(group.metatypes.size() * 2 - 1)) == group.metatypes.back(),
                    "Retail spawn group modulo selection changed.");
        }
    }
    const auto opening = loadCityOpening(assets, city);
    require(opening.heightMap.meshes.size() == 1, "Retail opening heightmap mesh changed.");
    const auto& heightMesh = opening.heightMap.meshes.front();
    const auto& openingPoints = city.districts.at(opening.district).spawnPoints;
    require(!openingPoints.empty(), "Missing retail opening spawn points.");
    size_t linkedTriangles = 0, totalSpawns = 0;
    for (const auto& district : city.districts) {
        totalSpawns += district.spawnPoints.size();
        for (const auto& point : district.spawnPoints)
            linkedTriangles += point.terrainTriangle(heightMesh).has_value();
    }
    std::cout << linkedTriangles << '/' << totalSpawns << " world spawn terrain links resolved.\n";
    require(linkedTriangles == totalSpawns, "Retail spawn point had an invalid world terrain triangle.");
    size_t roadFlagCount = 0, movingPoints = 0;
    using Material = CityNavigation::Material;
    const auto& navigation = opening.navigation;
    require(navigation.materialBuffers.at(static_cast<size_t>(Material::road)) == 1 &&
            navigation.materialBuffers.at(static_cast<size_t>(Material::cliff)) == 3 &&
            navigation.cliffVertexKeys.size() == 14542 && navigation.cliffTriangles.size() == 15156,
            "Retail navigation material or cliff tables changed.");
    size_t cliffFaces = 0, cliffVertices = 0;
    for (size_t vertex = 0; vertex < heightMesh.positions.size(); ++vertex) {
        const auto faces = navigation.cliffFaces(static_cast<uint16_t>(vertex));
        cliffVertices += !faces.empty();
        cliffFaces += faces.size();
        for (const auto face : faces)
            require(face.vertices(heightMesh).has_value(), "A retail cliff face did not resolve.");
    }
    require(cliffVertices == 3850 && cliffFaces == 15156, "Retail cliff face links changed.");
    std::cout << cliffFaces << " cliff face links across " << cliffVertices << " heightmap vertices.\n";
    size_t overlapCount = 0;
    for (const auto& buffer : navigation.buffers)
        for (const auto& overlaps : buffer.overlaps) overlapCount += overlaps.size();
    require(overlapCount == 27472, "Retail navigation overlap table changed.");
    std::cout << overlapCount << " retail navigation overlap links resolved.\n";
    size_t edgeLinks = 0, boundaryEdges = 0, waypoints = 0;
    for (size_t buffer = 0; buffer < navigation.buffers.size(); ++buffer) {
        for (size_t triangle = 0; triangle < navigation.buffers[buffer].roadFlags.size(); ++triangle) {
            const CityNavigation::TriangleRef source{static_cast<uint16_t>(buffer),
                                                     static_cast<uint16_t>(triangle * 3)};
            for (size_t edge = 0; edge < 3; ++edge) {
                const auto target = navigation.neighbor(source, edge);
                edgeLinks += target.has_value();
                boundaryEdges += !target;
                if (target) waypoints += CityNavigation::edgeWaypoint(heightMesh, source, *target, {}).has_value();
            }
        }
    }
    require(edgeLinks && boundaryEdges && !navigation.neighbor({0, 1}, 0),
            "Retail navigation edge links or boundary sentinel changed.");
    std::cout << edgeLinks << " retail navigation edge links, " << boundaryEdges << " boundaries.\n";
    require(waypoints > 0 && waypoints <= edgeLinks, "Retail navigation edge waypoints were unavailable.");
    std::vector<CityNavigation::TriangleRef> movingTriangles;
    for (const auto& point : openingPoints)
        if (point.objectType == 3) movingTriangles.push_back({point.terrainBuffer, point.terrainIndexOffset});
    require(movingTriangles.size() > 1, "Missing retail moving spawn endpoints.");
    const auto spawnRoute = navigation.findRoute(heightMesh, movingTriangles.front(), movingTriangles.back());
    require(spawnRoute.size() > 2 && spawnRoute.front().indexOffset == movingTriangles.front().indexOffset &&
            spawnRoute.back().indexOffset == movingTriangles.back().indexOffset,
            "Could not find a route between retail moving spawn points.");
    for (size_t i = 1; i < spawnRoute.size(); ++i)
        require(CityNavigation::edgeWaypoint(heightMesh, spawnRoute[i - 1], spawnRoute[i], {}).has_value(),
                "Retail route included a transition without an edge waypoint.");
    require(navigation.findRoute(heightMesh, {0, 1}, movingTriangles.back()).empty(),
            "Route accepted an unaligned triangle offset.");
    std::cout << spawnRoute.size() << " triangles in retail spawn-point route.\n";
    std::cout << waypoints << '/' << edgeLinks << " retail edge links yielded shared-vertex waypoints.\n";
    std::set<unsigned> roadSpeeds;
    for (const auto& buffer : opening.navigation.buffers) roadFlagCount += buffer.roadFlags.size();
    for (const auto& district : city.districts) {
        for (const auto& point : district.spawnPoints) {
            if (point.objectType != 3) continue;
            const auto flags = opening.navigation.roadFlags(point);
            require(flags.has_value(), "Moving spawn point had no world road flag.");
            roadSpeeds.insert(*flags >> 6);
            ++movingPoints;
        }
    }
    require(movingPoints > 0 && roadFlagCount > movingPoints && !roadSpeeds.empty(),
            "Retail moving spawn road metadata was empty.");
    std::cout << roadFlagCount << " retail triangle flag bytes, " << movingPoints
              << " moving spawn points with road flags, " << roadSpeeds.size() << " speed codes.\n";
    const auto minimap = loadRetailMinimap(assets);
    const auto hud = loadRetailStatusHud(assets);
    const auto& armor = hud.gauges[armorGauge];
    const auto& life = hud.gauges[lifeGauge];
    require(armor.layers[1].image.width == 171 && armor.layers[1].y == 17 &&
            armor.layers[2].x == 543 &&
            life.layers[1].image.width == 171 && life.layers[1].y == 8 &&
            std::any_of(armor.layers[1].image.bgra.begin(), armor.layers[1].image.bgra.end(),
                        [](uint32_t pixel) { return (pixel >> 24) != 0; }) &&
            std::any_of(life.layers[1].image.bgra.begin(), life.layers[1].image.bgra.end(),
                        [](uint32_t pixel) { return (pixel >> 24) != 0; }) &&
            retailGaugeFillLeft(171, 100, 200) == 86 && retailGaugeFillLeft(171, 200, 200) == 0 &&
            retailGaugeFillLeft(171, 0, 300) == 171, "Retail HUD gauge frames or integer fill clipping changed.");
    for (const auto& glyph : hud.cashGlyphs)
        require(std::any_of(glyph.bgra.begin(), glyph.bgra.end(), [](uint32_t pixel) { return pixel >> 24; }),
                "Retail cash glyph has no visible pixels.");
    require(formatRetailCash(0, RetailLanguage::english) == "$0" &&
            formatRetailCash(1234567, RetailLanguage::english) == "$1,234,567" &&
            formatRetailCash(9999, RetailLanguage::french) == "9999 $" &&
            formatRetailCash(10000, RetailLanguage::french) == "10 000 $" &&
            formatRetailCash(1234, RetailLanguage::german) == "1.234 $" &&
            formatRetailCash(1234, RetailLanguage::italian) == "1 234$" &&
            formatRetailCash(1234, RetailLanguage::japanese) == "1.234 $" &&
            formatRetailCash(12345, RetailLanguage::spanish) == "$12 345",
            "Retail cash currency placement or grouping changed.");
    RetailHealthBlink blink;
    blink.update(149, 499);
    require(!blink.hideFill, "Health fill blinked before 500 ms.");
    blink.update(149, 1);
    require(blink.hideFill, "Low health fill remained visible at 500 ms.");
    blink.update(149, 500);
    require(!blink.hideFill, "Low health fill did not return at 1000 ms.");
    blink.update(150, 500);
    require(!blink.hideFill, "Health fill blinked at the healthy threshold.");
    const auto benchLod = readCityLodGroup(loadCityScene(assets, "bench_wood_00.bdae"));
    require(benchLod.levels.size() == 3 && benchLod.select(1000.f * 1000) != benchLod.select(6000.f * 6000) &&
            !benchLod.select(7100.f * 7100), "Retail bench LOD thresholds did not decode.");
    const auto dealerSign = readCityLodGroup(loadCityScene(assets, "dale_deal_sign.bdae"));
    require(dealerSign.levels.size() == 3 && dealerSign.maximumSquared == 8000.f * 8000 &&
            dealerSign.select(8000.f * 8000).has_value() && !dealerSign.select(8001.f * 8001),
            "Retail dealer sign lost its adjacent LOD assignments.");
    require(opening.vehicleObjective && opening.vehicleObjective->icon == 46 &&
            minimap.checkpointAnimationId == 46 && opening.checkpoint,
            "WaitStolen highlight did not resolve to the retail minimap animation.");
    const auto factory = loadCityScene(assets, "s_factory_03.bdae");
    const auto factoryMesh = std::find_if(factory.meshes.begin(), factory.meshes.end(),
        [](const CityMesh& mesh) { return mesh.name == "s_factory_05-mesh"; });
    require(factoryMesh != factory.meshes.end() && !factoryMesh->colors.empty() &&
            factoryMesh->secondaryColors.size() == factoryMesh->positions.size() &&
            factoryMesh->colors.front() == std::array<float, 4>{1, 1, 1, 1} &&
            factoryMesh->secondaryColors.front() == std::array<float, 4>{68.f / 255, 56.f / 255, 56.f / 255, 1},
            "Factory mesh lost its distinct secondary BGRA vertex colors.");
    const auto suburbs = loadCityScene(assets, "suburbs_low.bdae");
    const auto suburbsMesh = std::find_if(suburbs.meshes.begin(), suburbs.meshes.end(),
        [](const CityMesh& mesh) { return mesh.name == "Object72-mesh"; });
    require(suburbsMesh != suburbs.meshes.end() && !suburbsMesh->auxiliaryTexCoords.empty() &&
            suburbsMesh->auxiliaryTexCoords.size() == suburbsMesh->positions.size() &&
            std::abs(suburbsMesh->auxiliaryTexCoords.front()[0] - 6216.0876f) < 0.002f &&
            std::abs(suburbsMesh->auxiliaryTexCoords.front()[1] - 2658.3771f) < 0.002f,
            "Suburbs mesh did not decode the distinct auxiliary texture coordinates.");
    const auto hospitalSign = loadCityScene(assets, "hospital_logo_00.bdae");
    const auto& hospitalMaterial = hospitalSign.materials.at("hospital_logo_00_mat");
    require(hospitalMaterial.alphaTest && hospitalMaterial.texture == "props_alpha_02.tga" &&
            hospitalMaterial.additionalTextures == std::vector<std::string>{"props_alpha_01.tga"},
            "Hospital sign lost one of its distinct authored texture bindings.");
    const auto parkingLight = loadCityScene(assets, "light_park_00.bdae");
    require(!parkingLight.meshes.empty() && parkingLight.meshes.front().colors.size() > 1 &&
            parkingLight.meshes.front().colors[1] == std::array<float, 4>{172.f / 255, 1, 1, 1},
            "Retail parking light lost its BGRA color channels.");
    const auto& highlight = *opening.vehicleObjective;
    require(std::filesystem::path(highlight.model.path).filename() == "target_car.bdae" &&
            !highlight.model.nodes.empty() && highlight.animation.durationMs &&
            !highlight.animation.channels.empty(),
            "WaitStolen did not load the animated retail vehicle highlight.");
    CityPlayback approach;
    require(!approach.vehicleObjectivePosition(opening), "Vehicle marker was visible before the walking checkpoint.");
    approach.timeAfterCheckpointMs = opening.checkpoint->nextTutorial.startMs;
    require(approach.vehicleObjectivePosition(opening) == opening.actors.at(*opening.vehicleActor).position,
            "WaitStolen marker did not appear over the authored vehicle.");
    approach.vehicle = CityVehiclePose{{100, 200, 300}, 0};
    require(approach.vehicleObjectivePosition(opening) == approach.vehicle->position,
            "WaitStolen marker ignored the live vehicle position.");
    CityOpening staticOpening;
    staticOpening.district = opening.district;
    const auto staticCity = prepareCityView(city, staticOpening);
    require(staticCity.terrain.walls.size() > 100, "Retail external collision planes did not reach the world.");
    require(!staticCity.terrain.jumpRamps.empty(), "Retail jump surfaces did not reach the world.");
    std::cout << staticCity.terrain.jumpRamps.size() << " authored jump ramps.\n";
    std::cout << staticCity.terrain.walls.size() << " authored district collision-plane triangles.\n";
    auto markerDraw = prepareCityView(city, opening);
    require(!markerDraw.billboards.empty(), "Retail billboard tags did not reach draw geometry.");
    size_t propCutouts = 0;
    for (const auto& surface : markerDraw.surfaces) {
        const auto name = std::filesystem::path(surface.texture).filename();
        if (name != "props_alpha.tga" && name != "props_alpha_01.tga") continue;
        require(surface.tintAndCutoff[3] == 0.5f, "Retail prop alpha test was not applied.");
        ++propCutouts;
    }
    require(propCutouts > 0, "No retail prop alpha-test surfaces were found.");
    std::cout << propCutouts << " retail prop alpha-test surfaces.\n";
    require(markerDraw.terrain.groundNavigation.size() == markerDraw.terrain.ground.size(),
            "Opening ground triangles lost their navigation references.");
    const auto cliffBuffer = navigation.materialBuffers.at(static_cast<size_t>(Material::cliff));
    const std::set<uint16_t> cliffOffsets(navigation.cliffTriangles.begin(), navigation.cliffTriangles.end());
    size_t authoredWalls = 0;
    for (size_t i = 0; i < markerDraw.terrain.groundNavigation.size(); ++i) {
        const auto reference = markerDraw.terrain.groundNavigation[i];
        if (reference.buffer != cliffBuffer || !cliffOffsets.count(reference.indexOffset)) continue;
        require(authoredWalls < markerDraw.terrain.walls.size() &&
                markerDraw.terrain.walls[authoredWalls] == markerDraw.terrain.ground[i],
                "Authored cliff face was omitted from live wall collision.");
        ++authoredWalls;
    }
    require(authoredWalls == cliffOffsets.size(), "Retail cliff collision wall count changed.");
    std::cout << authoredWalls << " authored cliff faces in wall collision.\n";
    size_t districtSurfaces = 0, renderedDistricts = 0;
    for (const auto& district : city.districts) {
        size_t surfaces = 0;
        for (const auto& node : district.scene.nodes) {
            if (node.name.find("Plane") != std::string::npos || node.name.find("Jump") != std::string::npos)
                continue;
            for (const auto& instance : node.geometry) {
                const auto& library = cityGeometryLibrary(city, district.scene, instance);
                const auto mesh = std::find_if(library.meshes.begin(), library.meshes.end(),
                    [&](const CityMesh& candidate) { return candidate.name == instance.meshName; });
                require(mesh != library.meshes.end(), "Missing retail district mesh.");
                surfaces += mesh->buffers.size();
            }
        }
        districtSurfaces += surfaces;
        renderedDistricts += surfaces != 0;
    }
    const auto alternatives = static_cast<size_t>(std::count_if(markerDraw.surfaces.begin(),
        markerDraw.surfaces.end(), [](const CitySurface& surface) { return surface.lodAlternative; }));
    require(alternatives > 0, "No compatible retail district LOD alternatives were prepared.");
    std::cout << alternatives << " alternate retail district LOD surfaces.\n";
    require(renderedDistricts > 1 && markerDraw.districtSurfaceCount == districtSurfaces + alternatives,
            "City draw data omitted high-detail districts.");
    require(std::all_of(markerDraw.surfaces.begin(),
                        markerDraw.surfaces.begin() + markerDraw.districtSurfaceCount,
                        [](const CitySurface& surface) { return surface.bounds.has_value(); }),
            "A district surface lost its frustum bounds.");
    require(std::any_of(markerDraw.surfaces.begin(),
                        markerDraw.surfaces.begin() + markerDraw.districtSurfaceCount,
                        [](const CitySurface& surface) { return surface.lodLimit.has_value(); }),
            "Retail district LOD maximum distances were not carried into draw surfaces.");
    require(markerDraw.lowDetailEndSurface > markerDraw.districtSurfaceCount &&
            std::all_of(markerDraw.surfaces.begin() + markerDraw.districtSurfaceCount,
                        markerDraw.surfaces.begin() + markerDraw.lowDetailEndSurface,
                        [](const CitySurface& surface) { return surface.bounds.has_value(); }),
            "Retail distant scenes did not reach the dedicated draw range with bounds.");
    const auto& markerMeshes = markerDraw.checkpointMeshes[2];
    require(opening.vehicleActor && markerDraw.vehicleObjectiveFirstSurface && !markerMeshes.empty(),
            "Retail vehicle marker was not added to the city draw data.");
    const auto& car = opening.actors.at(*opening.vehicleActor);
    require(car.model.morphControllers.size() == 1, "Retail Lincoln lost its body morph controller.");
    const auto& morph = car.model.morphControllers.front();
    require(morph.name == "car_body-mesh-morpher" && morph.source == "car_body-mesh" &&
            morph.method == 0 && morph.targets.size() == 2, "Retail Lincoln body morph header did not decode.");
    const auto base = std::find_if(car.model.meshes.begin(), car.model.meshes.end(),
        [&](const CityMesh& mesh) { return mesh.name == morph.source; });
    require(base != car.model.meshes.end(), "Retail Lincoln body morph lost the source mesh.");
    constexpr std::array targetNames{"car_body_damage_back-mesh", "car_body_damage_front-mesh"};
    for (size_t i = 0; i < targetNames.size(); ++i) {
        const auto& target = morph.targets.at(i);
        const auto& mesh = car.model.meshes.at(target.mesh);
        require(mesh.name == targetNames[i] && target.weight == 0 &&
                mesh.positions.size() == base->positions.size() && mesh.positions != base->positions,
                "Retail Lincoln damage target lost its geometry, default weight, or authored order.");
    }
    require(car.lights.has_value() &&
            car.model.nodes.at(car.lights->mountNodes[0]).name == "dummy_light_01-node" &&
            car.model.nodes.at(car.lights->mountNodes[1]).name == "dummy_light_02-node",
            "Retail Lincoln light mounts did not resolve.");
    for (const auto& effect : car.lights->effects) {
        require(effect.diffuseColors.size() == 6 && effect.diffuseColors.back().timeMs == 166 &&
                sampleCityDiffuseColor(effect, 0)[3] == 0 && sampleCityDiffuseColor(effect, 166)[3] == 1 &&
                std::abs(sampleCityDiffuseColor(effect, 49.5f)[3] - 57.5f / 255) < 0.0001f,
                "Retail light opacity keys did not decode and interpolate.");
        size_t surfaces = 0;
        for (const auto& node : effect.nodes) for (const auto& geometry : node.geometry) {
            const auto& library = cityGeometryLibrary(city, effect, geometry);
            const auto mesh = std::find_if(library.meshes.begin(), library.meshes.end(),
                [&](const CityMesh& candidate) { return candidate.name == geometry.meshName; });
            require(mesh != library.meshes.end() && !mesh->positions.empty() &&
                    mesh->texCoords.size() == mesh->positions.size(), "Retail light mesh lost its UVs.");
            for (size_t i = 0; i < mesh->buffers.size(); ++i) {
                const auto material = resolveCityMaterial(city, effect, library, geometry, *mesh, i);
                require(material.texture == "gloweffects.tga" && material.diffuse == 0xff959595,
                        "Retail light material lost its atlas binding or diffuse tint.");
                ++surfaces;
            }
        }
        require(surfaces > 0, "Retail Lincoln light effect has no drawable surfaces.");
    }
    const std::unique_ptr<IFileStream> lightAtlas(assets.openResource("gloweffects.tga"));
    require(lightAtlas != nullptr, "Retail light atlas could not be opened.");
    const auto lightImage = readTexture(*lightAtlas, true);
    require(!lightImage.bgra.empty(), "Retail light atlas could not be decoded.");
    auto& posedCar = *approach.vehicle;
    posedCar.position = car.position;
    posedCar.yaw = car.rotationDegrees[2];
    const auto lightFrames = cityVehicleLightFrames(car, posedCar);
    require(lightFrames && (*lightFrames)[0].origin != (*lightFrames)[1].origin,
            "Retail Lincoln lights shared a mount position.");
    auto turnedCar = posedCar;
    turnedCar.position = {130, -70, 35};
    turnedCar.yaw += 90;
    const auto turnedLights = cityVehicleLightFrames(car, turnedCar);
    for (size_t light = 0; light < lightFrames->size(); ++light) {
        const auto& start = lightFrames->at(light);
        const auto& turned = turnedLights->at(light);
        require(std::abs(turned.origin[0] - (turnedCar.position[0] - (start.origin[1] - posedCar.position[1]))) < 0.01f &&
                std::abs(turned.origin[1] - (turnedCar.position[1] + (start.origin[0] - posedCar.position[0]))) < 0.01f &&
                std::abs(turned.origin[2] - (turnedCar.position[2] + (start.origin[2] - posedCar.position[2]))) < 0.01f,
                "Retail Lincoln light mount did not follow the vehicle pose.");
        for (size_t axis = 0; axis < start.axes.size(); ++axis)
            require(std::abs(turned.axes[axis][0] + start.axes[axis][1]) < 0.001f &&
                    std::abs(turned.axes[axis][1] - start.axes[axis][0]) < 0.001f &&
                    std::abs(turned.axes[axis][2] - start.axes[axis][2]) < 0.001f,
                    "Retail Lincoln light mount basis did not rotate with the vehicle.");
    }
    const auto firstVertex = markerMeshes.front().firstVertex;
    const auto poseMarker = [&](double time) {
        poseCityVehicleObjective(opening, &posedCar, markerMeshes, time, markerDraw.vertices.data());
        return markerDraw.vertices.at(firstVertex).position;
    };
    const auto initial = poseMarker(0);
    const auto initialNormal = markerDraw.vertices.at(firstVertex).normal;
    require(initialNormal != std::array<float, 3>{}, "Retail marker lost its authored normal.");
    posedCar.yaw += 90;
    poseMarker(0);
    const auto turnedNormal = markerDraw.vertices.at(firstVertex).normal;
    require(std::abs(turnedNormal[0] + initialNormal[1]) < 0.001f &&
            std::abs(turnedNormal[1] - initialNormal[0]) < 0.001f &&
            std::abs(turnedNormal[2] - initialNormal[2]) < 0.001f,
            "Retail marker normal did not rotate with the vehicle.");
    posedCar.yaw = car.rotationDegrees[2];
    posedCar.position[0] += 100;
    require(std::abs(poseMarker(0)[0] - initial[0] - 100) < 0.1f,
            "Retail vehicle marker vertices did not follow the car.");
    posedCar.position = car.position;
    require(poseMarker(highlight.animation.durationMs) == initial &&
            poseMarker(highlight.animation.durationMs / 2.0) != initial,
            "Retail vehicle marker did not animate and loop.");
    approach.enteredVehicle = true;
    require(!approach.vehicleObjectivePosition(opening), "Vehicle marker stayed visible after entry.");
    require(opening.dialogue.size() >= 3 && opening.dialogue[0].portrait == 0 &&
            opening.dialogue[1].portrait == 1 && opening.dialogue[2].portrait == 0,
            "Opening captions lost their retail speaker portrait IDs.");
    const auto pthug = rasterRetailDialoguePortrait(assets, 0);
    const auto juan = rasterRetailDialoguePortrait(assets, 1);
    const auto hasPixels = [](const Image& image) {
        return std::any_of(image.bgra.begin(), image.bgra.end(), [](uint32_t pixel) { return pixel >> 24; });
    };
    require(hasPixels(pthug) && hasPixels(juan) && pthug.bgra != juan.bgra,
            "Retail speaker slots did not rasterize distinct portraits.");
    CityStartupPlayback startup(loadCityScriptProgram(assets), opening.startupScript);
    bool reachedHandoff = false, visible = false, showedBars = false;
    std::vector<std::pair<CityAudioAction, uint32_t>> playbackCues;
    for (int frame = 0; frame < 2500 && !reachedHandoff; ++frame) {
        reachedHandoff = startup.advance(opening, 16, [&](const CityAudioCue& cue) {
            playbackCues.emplace_back(cue.action, cue.soundId);
        });
        visible |= startup.brightness() == 1;
        showedBars |= startup.showsBars();
    }
    require(reachedHandoff && visible && startup.brightness() == 0 && startup.camera() &&
            startup.camera()->durationMs == 20000 &&
            playbackCues == decltype(playbackCues){{CityAudioAction::playOnce, 4}, {CityAudioAction::stop, 4}} &&
            std::none_of(opening.audio.begin(), opening.audio.end(), [&](const CityAudioCue& cue) {
                return cue.timeMs < opening.handoffMs;
            }), "Windows startup playback did not deliver the interpreter's camera, fades, and sound.");
    require(opening.missionTitle && startup.beginMission(opening.nextScript) == opening.missionTitle->id &&
            opening.missionTitle->id == 0 && opening.missionTitle->text == "NOBODY WALKS IN L.A." &&
            opening.missionTitle->animation == 0,
            "Live mission title did not use the retail script and catalog.");
    require(showedBars && !startup.showsBars(), "Startup animation command did not show and clear the bars.");
    const auto bars = rasterRetailSpriteFrame(assets, "cinematics_hud.bsprite", "BARS", 480, 320);
    const auto barsUp = rasterRetailSpriteAnimation(assets, "cinematics_hud.bsprite", "BARS_UP", 480, 320);
    const auto barsDown = rasterRetailSpriteAnimation(assets, "cinematics_hud.bsprite", "BARS_DOWN", 480, 320);
    require(barsUp.size() == 1 && barsDown.size() == 1 &&
            barsUp.front().image.bgra == bars.bgra && barsDown.front().image.bgra == bars.bgra &&
            std::any_of(bars.bgra.begin(), bars.bgra.end(), [](uint32_t pixel) { return pixel >> 24; }),
            "Startup bars texture does not match the retail animation frames.");
    const auto startupMatrix = cityCameraMatrix(opening, 1.5f, 0, nullptr, nullptr, {}, {}, &startup);
    require(startupMatrix != cityCameraMatrix(opening, 1.5f, 0),
            "Windows camera matrix ignored the interpreter's startup camera time.");
    CityStartupPlayback skipped(loadCityScriptProgram(assets), opening.startupScript);
    std::vector<CityAudioCue> skippedAudio;
    const auto collectAudio = [&](const CityAudioCue& cue) { skippedAudio.push_back(cue); };
    require(!skipped.advance(opening, 16, collectAudio, true),
            "Escape skipped before the startup cinematic entered.");
    require(skipped.advance(opening, 16, collectAudio, true) && skipped.brightness() == 0 &&
            !skipped.showsBars() && skipped.camera() && skipped.camera()->durationMs == 0 &&
            skippedAudio.size() == 1 && skippedAudio.front().soundId == 4 &&
            skippedAudio.front().action == CityAudioAction::stop,
            "Escape did not apply retail skip rules and hand off immediately.");
    require(opening.vehicleActor.has_value(), "Opening vehicle is missing.");
    require(opening.vehicleCharacterClips.has_value(), "Player vehicle clips are missing.");
    require(opening.vehicleSeated && opening.vehicleSeated->name == "car_seated_idle_low",
            "Retail Lincoln did not select the low seated clip.");
    auto geometry = prepareCityView(city, opening);
    size_t animatedMarkers = 0;
    const auto verifyMarker = [&](const CityCheckpoint& marker, const std::vector<CityCheckpointMesh>& meshes) {
        if (!marker.animation) return;
        ++animatedMarkers;
        require(!meshes.empty(), "Animated checkpoint has no uploaded geometry.");
        const auto duration = marker.animation->durationMs;
        poseCityCheckpoint(marker, meshes, 0, geometry.vertices.data());
        const auto start = geometry.vertices;
        poseCityCheckpoint(marker, meshes, duration / 3.0, geometry.vertices.data());
        size_t moved = 0;
        for (const auto& mesh : meshes) {
            for (size_t i = mesh.firstVertex; i < mesh.firstVertex + mesh.positions.size(); ++i) {
                const auto& a = start[i].position;
                const auto& b = geometry.vertices[i].position;
                moved += std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) > 0.1;
            }
        }
        require(moved > 0, "Checkpoint clip did not move any geometry.");
        poseCityCheckpoint(marker, meshes, duration, geometry.vertices.data());
        for (const auto& mesh : meshes)
            for (size_t i = mesh.firstVertex; i < mesh.firstVertex + mesh.positions.size(); ++i)
                require(start[i].position == geometry.vertices[i].position, "Checkpoint clip did not loop.");
        std::cout << marker.name << " animated " << moved << " vertices.\n";
    };
    if (opening.checkpoint) verifyMarker(*opening.checkpoint, geometry.checkpointMeshes[0]);
    if (opening.payphone) verifyMarker(opening.payphone->destination, geometry.checkpointMeshes[1]);
    require(animatedMarkers > 0, "No opening checkpoint used a retail animation.");
    const auto& footprint = geometry.terrain.vehicleFootprint;
    const auto width = footprint.radius * 2;
    const auto length = footprint.longitudinalCenters.back() - footprint.longitudinalCenters.front() + width;
    require(width > 160 && width < 250 && length > 400 && length < 640,
            "Vehicle footprint was outside the expected Lincoln proportions.");
    require(width != 188 || length != 488, "Vehicle footprint fell back to the synthetic defaults.");
    const auto& actor = opening.actors[*opening.vehicleActor];
    require(actor.suspension && actor.suspension->wheelMass == 425 && actor.suspension->springRate == 35000 &&
            actor.suspension->damping == 5 && actor.suspension->upperTravel == 15 &&
            std::abs(actor.suspension->lowerTravel - 130.f * 1000 / 3600) < 0.001f,
            "Lincoln suspension settings did not decode from the retail vehicle fields.");
    require(actor.gearProfile == 1, "Lincoln did not select its authored gear profile.");
    require(actor.drive && actor.drive->massKg == 1700 && actor.drive->forwardForce == 5000 &&
            actor.drive->reverseForce == 25000 && actor.drive->topSpeedKmh == 130,
            "Lincoln drivetrain settings did not decode.");
    CityOpening driveWorld;
    driveWorld.checkpoint.emplace();
    driveWorld.vehicleActor = 0;
    CityActor driveActor{};
    driveActor.gearProfile = actor.gearProfile;
    driveActor.drive = actor.drive;
    driveActor.vehicleHealth = actor.vehicleHealth;
    driveActor.vehicleMaxHealth = actor.vehicleMaxHealth;
    driveWorld.actors.push_back(driveActor);
    auto straight = flatTerrain();
    for (auto& triangle : straight.ground)
        for (auto& vertex : triangle) vertex[1] *= 50;

    CityPlayback driver;
    driver.player = CityPlayerPose{{0, 100, 0}, 0};
    driver.timeAfterCheckpointMs = 0;
    driver.drive(driveWorld, straight, {}, 20, true);
    driver.drive(driveWorld, straight, {0, 1}, 20, false);
    require(driver.vehicle && std::abs(driver.vehicle->speed - 28'000.f / 1700) < 0.001f,
            "Lincoln did not accelerate using its first gear power.");
    for (int i = 0; i < 100; ++i) driver.drive(driveWorld, straight, {0, 1}, 20, false);
    require(driver.vehicle->gear == 2, "Lincoln did not shift into second gear under throttle.");
    for (int i = 0; i < 600; ++i) driver.drive(driveWorld, straight, {0, 1}, 20, false);
    const float topSpeed = 130.f * (1000.f / 36.f);
    require(std::abs(driver.vehicle->speed - topSpeed) < 0.001f && driver.vehicle->gear == 5,
            "Lincoln did not reach authored top speed in its final gear.");
    require(driver.highestVehicleSpeedKmh == 130, "Driving did not record the highest whole km/h.");
    driver.drivenCentimeters = 99'995;
    driver.drive(driveWorld, straight, {0, 1}, 20, false);
    require(driver.drivenKilometers == 1 && driver.drivenCentimeters == 0,
            "Distance stat did not increment and reset at one kilometer.");
    const float fullBrake = 100'000.f / 1700;
    driver.drive(driveWorld, straight, {0, -0.5f}, 20, false);
    require(std::abs(driver.vehicle->speed - (topSpeed - fullBrake * 0.5f)) < 0.001f,
            "Half brake input did not apply half the Lincoln's authored force.");
    driver.drive(driveWorld, straight, {0, -1}, 20, false);
    require(std::abs(driver.vehicle->speed - (topSpeed - fullBrake * 1.5f)) < 0.001f,
            "Full brake input did not apply the Lincoln's authored force.");
    driver.vehicle->speed = 5;
    driver.drive(driveWorld, straight, {0, -1}, 20, false);
    require(driver.vehicle->speed == 0 && driver.vehicle->lightEffects == std::array{true, false},
            "Braking crossed zero before reverse drive was applied.");
    driver.drive(driveWorld, straight, {0, -1}, 20, false);
    require(std::abs(driver.vehicle->speed + 100'000.f / 1700) < 0.001f && driver.vehicle->gear == -1,
            "Lincoln did not accelerate using its authored reverse force.");
    driver.drive(driveWorld, straight, {0, -1}, 20, false);
    require(driver.vehicle->lightEffects == std::array{false, true},
            "Reverse drive did not select the reverse light pair.");
    driver.vehicle->speed = -100;
    driver.drive(driveWorld, straight, {0, 1}, 20, false);
    require(std::abs(driver.vehicle->speed - (-100 + fullBrake)) < 0.001f,
            "Forward input did not brake reverse motion with authored force.");
    driver.vehicle->speed = -5;
    driver.drive(driveWorld, straight, {0, 1}, 20, false);
    require(driver.vehicle->speed == 0 && driver.vehicle->gear == 1,
            "Forward input crossed zero before reverse motion stopped.");
    driver.vehicle->speed = topSpeed;
    driver.vehicle->health = driver.vehicle->maxHealth / 4;
    driver.drive(driveWorld, straight, {0, 1}, 20, false);
    require(driver.vehicle->speed < topSpeed && driver.vehicle->speed > topSpeed * 0.5f,
            "A reduced speed limit did not let an overspeed Lincoln coast down.");
    for (int i = 0; i < 30; ++i) driver.drive(driveWorld, straight, {0, 1}, 20, false);
    require(std::abs(driver.vehicle->speed - topSpeed * 0.5f) < 0.001f,
            "Quarter health did not halve the Lincoln's forward speed limit.");
    driver.vehicle->health = driver.vehicle->maxHealth / 10;
    for (int i = 0; i < 30; ++i) driver.drive(driveWorld, straight, {0, 1}, 20, false);
    require(std::abs(driver.vehicle->speed - topSpeed * 0.3f) < 0.001f,
            "Low health did not retain the minimum forward speed limit.");
    require(actor.v6EngineSpeedRange == 130, "Lincoln did not select its retail V6 engine sound profile.");
    CityVehiclePose engine{{}, 0};
    require(engine.v6EngineRpm(1, 130) == 1000, "Lincoln V6 idle RPM is incorrect.");
    engine.speed = 1000;
    require(engine.v6EngineRpm(1, 130) == 1830, "Lincoln V6 did not map first-gear speed to retail RPM.");
    require(actor.vehicleCamera && actor.vehicleCamera->setback == 820 &&
            actor.vehicleCamera->eyeHeight == 290 && actor.vehicleCamera->targetHeight == 250 &&
            actor.vehicleCamera->targetForward == 0 && actor.vehicleCamera->minimumSetback == 600,
            "Lincoln camera settings did not decode from the retail vehicle fields.");
    CityVehiclePose cameraCar{{100, 200, 300}, 90};
    cameraCar.occupied = true;
    const auto followReadyMs = double(*opening.followCameraMs) + opening.followCameraBlendMs;
    require(opening.followCameraBlendMs == 0, "Retail opening CamDefault was expected to switch immediately.");
    const auto matrix = cityCameraMatrix(opening, 1.5f, followReadyMs, nullptr, &cameraCar);
    const auto project = [&](size_t row, float x, float y, float z) {
        return matrix[row][0] * x + matrix[row][1] * y + matrix[row][2] * z + matrix[row][3];
    };
    require(std::abs(project(3, -720, 200, 590)) < 0.001f &&
            std::abs(project(1, -720, 200, 590)) < 0.001f &&
            std::abs(project(0, 100, 200, 550)) < 0.001f &&
            std::abs(project(1, 100, 200, 550)) < 0.001f,
            "Vehicle camera eye or center did not follow the retail offsets at 90 degrees.");
    require(actor.collisionDamageScale == 19660.f / 65536,
            "Lincoln impact multiplier did not decode from the retail record.");
    driveAndReverse(footprint, actor.collisionDamageScale);
    std::cout << "Retail vehicle footprint " << width << " x " << length
              << " cm passed the isolated wall and reverse checks.\n";
    require(opening.vehicleProtection && opening.vehicleProtection->target == actor.name &&
            opening.vehicleProtection->startMs >= opening.handoffMs && actor.vehicleHealth == 500 &&
            actor.vehicleMaxHealth == 500 && actor.vehicleDamageEnabled,
            "Opening vehicle protection or retail health did not resolve.");
    CityPlayback protectedCar;
    protectedCar.elapsedMs = opening.vehicleProtection->startMs - 1;
    protectedCar.updateMission(opening, 16);
    require(!protectedCar.protectionRegistered, "Vehicle protection was registered before its command.");
    ++protectedCar.elapsedMs;
    protectedCar.updateMission(opening, 16);
    require(protectedCar.protectionRegistered && !protectedCar.missionFailure,
            "Vehicle protection did not register against the placed Lincoln.");
    protectedCar.vehicle = CityVehiclePose{actor.position, actor.rotationDegrees[2]};
    auto& protectedPose = *protectedCar.vehicle;
    protectedPose.health = *actor.vehicleHealth;
    protectedPose.maxHealth = actor.vehicleMaxHealth;
    using DamageEffect = CityVehiclePose::DamageEffect;
    protectedPose.health = 150;
    require(protectedPose.damageEffect() == DamageEffect::none, "Smoke activated at 30 percent health.");
    protectedPose.health = 149;
    require(protectedPose.damageEffect() == DamageEffect::smoke, "Smoke did not activate below 30 percent health.");
    protectedPose.health = *actor.vehicleHealth;
    protectedPose.damageEnabled = actor.vehicleDamageEnabled;
    protectedPose.applyDamage(1);
    require(protectedPose.health == 499, "Retail damage flag did not permit damage while unoccupied.");
    protectedPose.damageEnabled = false;
    protectedPose.applyDamage(500);
    require(protectedPose.health == 499, "Undriven vehicle ignored its damage enable flag.");
    protectedPose.occupied = true;
    protectedPose.speed = 600;
    protectedPose.steering = 25;
    protectedPose.applyDamage(499);
    protectedCar.elapsedMs = std::max(protectedCar.elapsedMs, followReadyMs);
    protectedCar.updateMission(opening, 4999);
    require(protectedPose.health == 0 && protectedPose.damageEffect() == DamageEffect::fire &&
            !protectedPose.exploded && !protectedCar.missionFailure,
            "Protected vehicle failed before its explosion event.");
    protectedCar.updateMission(opening, 1);
    require(protectedPose.exploded && protectedCar.missionFailure == opening.vehicleProtection->failureCause,
            "Protected vehicle explosion did not report the retail failure cause.");
    require(protectedPose.speed == 0 && protectedPose.steering == 0 &&
            protectedPose.damageEffect() == DamageEffect::none,
            "Exploded vehicle did not stop.");
    require(protectedCar.cameraShake.remainingMs == 999,
            "Vehicle explosion did not start the camera shake.");
    protectedCar.updateMission(opening, 16);
    require(protectedCar.cameraShake.remainingMs == 983,
            "An exploded vehicle restarted the camera shake.");
    require(protectedCar.missionFailureCount == 1, "Latched mission failure was counted more than once.");
    require(opening.vehicleProtection->failureTitle == "MISSION FAILED" &&
            opening.vehicleProtection->failureCause == -1,
            "Opening protection did not select its retail title and empty reason.");
    require(opening.vehicleProtection->failurePrompt == "Touch the screen to continue",
            "Opening failure selected the wrong retail input prompt.");
    require(!protectedCar.acknowledgeFailure(opening, 999, true, true) &&
            !protectedCar.acknowledgeFailure(opening, 1, true, false) &&
            !protectedCar.acknowledgeFailure(opening, 1, false, true),
            "Opening failure accepted input before both gates or without a press.");
    require(protectedCar.acknowledgeFailure(opening, 0, true, true) &&
            protectedCar.elapsedMs == opening.handoffMs && !protectedCar.vehicle &&
            !protectedCar.protectionRegistered && !protectedCar.missionFailure,
            "Opening retry did not restore the authored script handoff.");
    protectedCar.elapsedMs = opening.vehicleProtection->startMs;
    protectedCar.updateMission(opening, 16);
    require(protectedCar.protectionRegistered && !protectedCar.missionFailure,
            "Opening retry reused the failed car's state.");
    require(protectedCar.missionFailureCount == 1, "Opening retry lost the mission failure count.");
    protectedCar.vehicle = CityVehiclePose{actor.position, actor.rotationDegrees[2]};
    protectedCar.updateMission(opening, 5000);
    require(protectedCar.missionFailureCount == 2, "Second mission failure did not increment the failure count.");

    require(opening.payphone && opening.payphone->clearConditionsOnDeparture,
            "Opening route lost its MissionClearConditions command.");
    const auto& payphoneTutorial = opening.payphone->destination.nextTutorial;
    require(payphoneTutorial.speaker == "Mini-Map" &&
            payphoneTutorial.text == "GO TO THE HOTSPOT INDICATED ON THE MINI-MAP",
            "Payphone checkpoint lost its retail tutorial title or objective after WaitEscape.");
    CityPlayback departing;
    departing.elapsedMs = opening.vehicleProtection->startMs;
    departing.updateMission(opening, 0);
    require(departing.protectionRegistered, "Departure fixture did not activate protection.");
    departing.enteredVehicle = true;
    const auto& route = *opening.payphone;
    require(route.gameplayFadeMs == 250, "Payphone handoff lost its authored gameplay fade duration.");
    const auto& fists = city.weaponMetatypes.at(50);
    require(city.weaponMetatypes.size() >= 10 && fists.name == "Fists" && fists.weaponType == 0 &&
            fists.range == 150 && fists.damage == 12 && fists.cone == 25 &&
            route.handRangeCm == 150 && route.handDamage == 12,
            "Retail weapon metatype values did not reach the live fight setup.");
    require(decodeCityWaitForLock({108, route.lockCommandPayload}) == "Loco_00",
            "Payphone handoff lost its authored lock target.");
    require(decodeCityDeferredDisable({23, route.disableCommandPayload}) == "Loco_00",
            "Payphone fight lost its authored actor cleanup target.");
    const auto& postFight = route.postFight;
    require(postFight.nextCheckpoint && postFight.nextCheckpoint->marker.name == "CheckpointMission_LC" &&
            postFight.nextCheckpoint->marker.radius > 0 && postFight.nextCheckpoint->marker.animation &&
            postFight.nextCheckpoint->fadeMs == 350 && postFight.nextCheckpoint->title.id == 1 &&
            !postFight.nextCheckpoint->title.text.empty() &&
            decodeCityScriptCheckpoint({64, postFight.nextCheckpoint->payload}).object == "CheckpointMission_LC",
            "Script 18 did not resolve its authored checkpoint gate.");
    const auto& nextScene = postFight.nextCheckpoint->cinematic;
    require(postFight.nextCheckpoint->targetVehicle == "aHummer_00" &&
            !postFight.nextCheckpoint->vehicleObjective.empty(),
            "Script 18 lost its retail vehicle objective.");
    const auto& hummerRoute = *postFight.nextCheckpoint;
    const auto shopGate = decodeCityScriptCheckpoint({105, hummerRoute.weaponShopDriveWait});
    require(hummerRoute.weaponShopDriveCheckpoint && shopGate.object == "CheckpointDrive_C2M5weapon" &&
            shopGate.vehicle == "aHummer_00" && !hummerRoute.weaponShopObjective.empty() &&
            markerDraw.weaponShopDriveFirstSurface && !markerDraw.checkpointMeshes[5].empty(),
            "Weapon shop drive did not resolve its objective, Hummer gate, and animated marker.");
    const auto& shopScene = hummerRoute.weaponShopCinematic;
    const auto approachGate = decodeCityScriptCheckpoint({64, hummerRoute.weaponShopApproachWait});
    require(hummerRoute.weaponShopApproach && approachGate.object == "CheckpointArrow_ShopTuto" &&
            !approachGate.vehicle && hummerRoute.weaponShopEntrance == "CheckpointShopWeapon_santamonica00" &&
            !hummerRoute.weaponShopTutorial.text.empty() &&
            markerDraw.weaponShopApproachFirstSurface && !markerDraw.checkpointMeshes[6].empty(),
            "Weapon shop approach lost its retail gate, arrow, entrance name, or tutorial.");
    const auto enterShop = decodeCityScriptCheckpoint({64, hummerRoute.weaponShopEntranceWait});
    require(hummerRoute.weaponShopEntranceMarker && hummerRoute.weaponShopEntranceMarker->radius > 0 &&
            markerDraw.weaponShopEntranceFirstSurface && enterShop.object == hummerRoute.weaponShopEntrance &&
            decodeCityShopLock({77, hummerRoute.weaponShopUnlock}) == enterShop.object &&
            decodeCityScriptTutorial({47, hummerRoute.weaponShopTutorialCommand}).title == 0x3072,
            "Weapon shop unlock did not resolve the retail entrance checkpoint and tutorial.");
    const auto& entrance = *hummerRoute.weaponShopEntranceMarker;
    const CityScriptCommand returnToHummer{89, hummerRoute.returnToHummerWait};
    const CityScriptCommand returnToLc{105, hummerRoute.returnToLcWait};
    const auto returnGate = decodeCityScriptCheckpoint(returnToLc);
    require(hummerRoute.returnToLcCheckpoint && returnGate.object == "CheckpointDrive_LC" &&
            decodeCityWaitStolen(returnToHummer) == hummerRoute.targetVehicle &&
            returnGate.vehicle == hummerRoute.targetVehicle && markerDraw.returnToLcFirstSurface &&
            !markerDraw.checkpointMeshes[8].empty() && !hummerRoute.returnToHummerObjective.empty() &&
            !hummerRoute.returnToLcObjective.empty(), "Retail return to LC lost its Hummer gate or destination.");
    const auto& lcClosing = hummerRoute.lcClosingCinematic;
    require(hummerRoute.returnToLcFadeMs == 200 && hummerRoute.returnToLcWaitMs == 200 &&
            cityTrackBrightness(lcClosing.fades, 0) == 0, "Retail LC arrival fade or wait changed.");
    require(hummerRoute.lcUnlockedScript == 25, "Retail LC closing script link changed.");
    const auto postLcGate = decodeCityScriptCheckpoint({64, hummerRoute.postLcCheckpointWait});
    require(postLcGate.object == hummerRoute.marker.name && postLcGate.object == "CheckpointMission_LC" &&
            hummerRoute.postLcFadeMs == 350 && hummerRoute.postLcTitle && hummerRoute.postLcTitle->id == 8,
            "Script 25 lost its retail checkpoint, fade, or mission title.");
    const auto& postLcIntro = hummerRoute.postLcIntro;
    require(postLcIntro.cameras.size() >= 5 && postLcIntro.dialogue.size() == 3 &&
            postLcIntro.motions.size() == 3 && !hummerRoute.postLcDriveObjective.empty() &&
            cityTrackBrightness(postLcIntro.fades, 0) == 0,
            "Script 25 lost its introductory cameras, dialogue, movements, or first objective.");
    const auto drivePostLc = decodeCityScriptCheckpoint({105, hummerRoute.postLcDriveWait});
    const auto protectPostLc = decodeCityMissionCondition({70, hummerRoute.postLcVehicleProtection});
    require(decodeCityWaitStolen({89, hummerRoute.postLcVehicleWait}) == hummerRoute.targetVehicle &&
            protectPostLc && protectPostLc->target == hummerRoute.targetVehicle &&
            drivePostLc.object == "CheckpointDrive_C2M201" && drivePostLc.vehicle == hummerRoute.targetVehicle &&
            hummerRoute.postLcDriveCheckpoint && markerDraw.postLcDriveFirstSurface &&
            !markerDraw.checkpointMeshes[9].empty(), "Script 25 lost its playable drive gates.");
    require(lcClosing.cameras.size() >= 4 && lcClosing.dialogue.size() == 3,
            "Retail LC camera or dialogue cues changed.");
    require(lcClosing.motions.size() == 3 && lcClosing.actorAnimations.at("LC_00").size() == 4,
            "Retail LC movement or gestures changed.");
    CityPlayback shopVisitor;
    const auto& missionEnd = opening.payphone->postFight;
    require(opening.missionTitle && opening.missionTitle->cashReward == 250 &&
            opening.missionTitle->completionText == "FIND L.C." &&
            missionEnd.missionOverMs == missionEnd.durationMs,
            "Opening mission lost its retail reward or completion cue.");
    const auto rewardFrames = rasterRetailSpriteAnimation(assets, "huds.bsprite", "MISSION_END", 480, 320, true);
    require(rewardFrames.size() == 6 && rewardFrames.front().duration == 1 &&
            rewardFrames.front().image.bgra != rewardFrames.back().image.bgra,
            "Opening mission reward lost its retail animation frames.");
    const auto rewardCash = rasterRetailMissionCash(assets, 250, RetailLanguage::english);
    size_t visibleCashPixels = 0;
    for (size_t i = 0; i < rewardCash.bgra.size(); ++i) {
        if (!(rewardCash.bgra[i] >> 24)) continue;
        const auto x = i % rewardCash.width;
        const auto y = i / rewardCash.width;
        require(x >= 8 && x < 60 && y >= 152 && y < 172, "Retail reward cash left its authored bounds.");
        ++visibleCashPixels;
    }
    require(visibleCashPixels > 0 &&
            rewardCash.bgra != rasterRetailMissionCash(assets, 250, RetailLanguage::french).bgra,
            "Retail reward cash did not render its localized glyph layout.");
    const auto objectiveIn = rasterRetailSpriteAnimation(assets, "huds.bsprite", "OBJECTIVE", 480, 320, true);
    const auto objectiveOut = rasterRetailSpriteAnimation(assets, "huds.bsprite", "OBJECTIVE_END", 480, 320, true);
    require(objectiveIn.size() == 6 && objectiveOut.size() == 3 &&
            objectiveIn.back().duration == 2 &&
            objectiveIn.back().image.bgra == objectiveOut.front().image.bgra &&
            objectiveIn.front().image.bgra == objectiveOut.back().image.bgra &&
            objectiveIn.front().image.bgra != objectiveIn.back().image.bgra,
            "Retail mission objective opening and closing artwork changed.");
    require(shopVisitor.creditOpeningMission(0, opening.missionTitle->cashReward) &&
            !shopVisitor.creditOpeningMission(0, opening.missionTitle->cashReward) && shopVisitor.playerCash == 250,
            "Opening mission reward was omitted or paid twice.");
    require(hummerRoute.title.id == 1 && hummerRoute.title.cashReward == 100 &&
            hummerRoute.title.completionText == "FETCH LITTLE T.", "LC completion reward data changed.");
    require(shopVisitor.creditOpeningMission(1, hummerRoute.title.cashReward) &&
            !shopVisitor.creditOpeningMission(1, hummerRoute.title.cashReward) && shopVisitor.playerCash == 350,
            "LC completion lost its once-only reward.");
    require(std::any_of(lcClosing.placements.begin(), lcClosing.placements.end(), [&](const auto& placement) {
                return placement.actor == hummerRoute.targetVehicle && !placement.visible;
            }), "LC completion lost its Hummer cleanup.");
    shopVisitor.playerCash = 250;
    require(!shopVisitor.canEnterShop(entrance), "Shop opened before the player spawned.");
    shopVisitor.player = CityPlayerPose{entrance.position, 0};
    require(shopVisitor.canEnterShop(entrance), "Player on foot could not enter the shop.");
    shopVisitor.vehicle.emplace();
    shopVisitor.vehicle->occupied = true;
    require(!shopVisitor.canEnterShop(entrance), "Shop opened while driving.");
    shopVisitor.vehicle.reset();
    shopVisitor.player->position[0] += 2 * entrance.radius;
    require(!shopVisitor.canEnterShop(entrance), "Shop opened from outside the marker.");
    const auto& shopInventory = hummerRoute.weaponShopInventory;
    require(shopInventory.size() == 8, "Weapon shop inventory lost retail rows.");
    const std::array<uint32_t, 8> shopPrices{100, 200, 250, 750, 1000, 1500, 2500, 5000};
    for (size_t i = 0; i < shopInventory.size(); ++i) {
        require(shopInventory[i].price == shopPrices[i] && !shopInventory[i].label.empty(),
                "Weapon shop item lost its retail price or localized label.");
    }
    shopVisitor.playerHealth = 35;
    const auto healthPrice = shopInventory.front().price;
    require(shopVisitor.buyShopService(CityShopService::health, healthPrice) &&
            shopVisitor.playerHealth == CityPlayback::playerMaxHealth &&
            shopVisitor.playerCash == 150, "Retail health purchase did not restore health and charge $100.");
    shopVisitor.playerCash = 99;
    shopVisitor.playerHealth = 35;
    require(!shopVisitor.buyShopService(CityShopService::health, healthPrice) && shopVisitor.playerCash == 99 &&
            shopVisitor.playerHealth == 35, "An unaffordable purchase changed player state.");
    const auto vestPrice = shopInventory[1].price;
    shopVisitor.playerCash = 250;
    require(shopVisitor.buyShopService(CityShopService::vest, vestPrice) && shopVisitor.playerArmor == 200 &&
            shopVisitor.playerCash == 50 && shopVisitor.playerHealth == 35, "Vest purchase did not equip armor.");
    shopVisitor.damagePlayer(180);
    require(shopVisitor.playerArmor == 20 && shopVisitor.playerHealth == 35, "Armor did not absorb the hit.");
    shopVisitor.damagePlayer(5, true);
    require(shopVisitor.playerArmor == 20 && shopVisitor.playerHealth == 30, "Bypass hit did not reach health.");
    shopVisitor.damagePlayer(35);
    require(shopVisitor.playerArmor == 0 && shopVisitor.playerHealth == 15, "Damage beyond armor was lost.");
    require(shopScene.cameras.size() == 2 && shopScene.cameras.front().startMs == 1000 &&
            shopScene.dialogue.size() == 1 && shopScene.dialogue.front().startMs == 2500 &&
            shopScene.followCameraMs == 3000 && shopScene.durationMs == 3250 &&
            shopScene.placements.front().actor == "aHummer_00" && shopScene.placements.front().timeMs == 1000 &&
            shopScene.actorAnimations.count("Matadore_00") == 1,
            "Weapon shop cinematic lost its retail staging, dialogue, or camera handoff.");
    require(shopScene.motions.size() == 3, "Weapon shop cinematic lost one of its three walks.");
    for (const std::string_view actor : {"Player", "Juan_00", "Matadore_00"}) {
        const auto* motion = cityCinematicMotion(shopScene, actor, 1000);
        require(motion && motion->clip.name == "move_walk" && motion->travel.durationMs > 0 &&
                !cityCinematicMotion(shopScene, actor, 999) &&
                cityCinematicTravelPosition(motion->travel, 1000) == motion->travel.from &&
                cityCinematicTravelPosition(motion->travel, 1000. + motion->travel.durationMs) == motion->travel.to,
                "Weapon shop walk did not bind its actor and authored travel endpoints.");
    }
    const auto& pierScene = hummerRoute.pierCinematic;
    require(pierScene.cameras.size() == 4 && pierScene.cameras.front().startMs == 700 &&
            pierScene.dialogue.size() == 2 && pierScene.dialogue.front().startMs == 3200 &&
            pierScene.dialogue.back().startMs == 9800 && pierScene.followCameraMs == 10050 &&
            pierScene.durationMs == 10300 && pierScene.actorAnimations.at("Matadore_00").size() == 1,
            "Hummer pier cinematic lost its retail camera, dialogue, actor, or handoff timing.");
    require(pierScene.motions.size() == 1 && pierScene.motions.front().travel.actor == "Matadore_00" &&
            pierScene.motions.front().travel.startMs == 3600 && pierScene.motions.front().travel.durationMs > 0 &&
            pierScene.motions.front().travel.from != pierScene.motions.front().travel.to &&
            pierScene.motions.front().clip.name == "move_run",
            "Pier cinematic did not bind Matadore's authored run marker.");
    require(hummerRoute.vehicleCheckpoint &&
            hummerRoute.vehicleCheckpoint->name == "CheckpointDrive_C2M1_Pier_Park" &&
            decodeCityScriptCheckpoint({105, hummerRoute.vehicleCheckpointWait}).vehicle == "aHummer_00",
            "Script 18 lost its Hummer-only pier checkpoint.");
    require(markerDraw.hummerRouteFirstSurface && !markerDraw.checkpointMeshes[4].empty() &&
            hummerRoute.vehicleCheckpoint->animation,
            "Hummer pier checkpoint did not prepare animated world geometry.");
    RetailScriptExecution hummerScript(CityScriptProgram{{{89, hummerRoute.vehicleEntryWait},
        {57, hummerRoute.vehicleRouteDelay}, {112, hummerRoute.vehicleTutorialCommand},
        {70, hummerRoute.vehicleProtectionCommand},
        {105, hummerRoute.vehicleCheckpointWait}}});
    RetailTravelHooks travel;
    std::string_view occupiedVehicle;
    bool atPier = false, pierMarker = false;
    bool tutorialShown = false, tutorialDismissed = false;
    RetailMissionHooks tutorialHost;
    RetailMissionConditions hummerProtection;
    tutorialHost.conditions = &hummerProtection;
    tutorialHost.tutorialsEnabled = [] { return true; };
    tutorialHost.blockingTutorial = [&](const CityScriptTutorial& cue) {
        require(hummerRoute.vehicleTutorial.speaker == "Radio Station" && cue.title == 0x3076 && cue.text == 0x3077,
                "Hummer tutorial lost its retail title or body ID.");
        tutorialShown = true;
        return tutorialDismissed;
    };
    travel.occupiedVehicle = [&] { return occupiedVehicle; };
    travel.trackVehicle = [](std::string_view, bool) {};
    travel.setActive = [&](std::string_view name, bool active) {
        require(name == hummerRoute.vehicleCheckpoint->name, "Hummer route marked the wrong checkpoint.");
        pierMarker = active;
    };
    travel.reached = [&](const CityScriptCheckpoint& target) {
        return atPier && target.vehicle == occupiedVehicle;
    };
    const auto tickHummer = [&](int32_t ms) {
        hummerScript.execute(0, ms, 0, [](const auto&, int32_t) { return false; }, [] {}, [] {}, false,
                             &travel, {}, &tutorialHost);
    };
    hummerScript.unlock(0, 0, 0);
    tickHummer(1000);
    require(!pierMarker, "Pier was marked before Hummer entry.");
    occupiedVehicle = "aHummer_00";
    tickHummer(500);
    require(!pierMarker, "Pier was marked before the retail wait elapsed.");
    tickHummer(1);
    require(tutorialShown && !pierMarker, "Pier checkpoint did not wait for the radio tutorial.");
    const auto verifyProtection = [&] {
        return hummerProtection.update(16, false, {}, [](std::string_view target) {
            require(target == "aHummer_00", "ProtectCar checked the wrong vehicle.");
            return RetailMissionTargetState{{}, true, true};
        }).failureCause;
    };
    require(!verifyProtection(), "Hummer protection started before the tutorial was dismissed.");
    tutorialDismissed = true;
    tickHummer(1);
    require(verifyProtection() == -1, "Hummer destruction lost its authored failure code.");
    require(pierMarker, "Pier marker did not activate after the retail wait.");
    atPier = true;
    occupiedVehicle = {};
    tickHummer(1);
    require(pierMarker, "Pier checkpoint cleared on foot.");
    occupiedVehicle = "aHummer_00";
    tickHummer(1);
    require(!pierMarker && hummerScript.state(0) == RetailScriptState::locked,
            "Hummer arrival did not release the pier checkpoint.");
    require(nextScene.cameras.size() == 8 && nextScene.cameras.front().startMs == 0 &&
            nextScene.dialogue.size() == 11 && nextScene.dialogue.front().portrait == 2 &&
            nextScene.placements.size() == 8 && nextScene.placements.front().actor == "aHummer_00" &&
            nextScene.audio.size() == 3 && nextScene.audio.front().soundId == 2 &&
            nextScene.audio.back().action == CityAudioAction::stop && nextScene.nextScript == 84 &&
            nextScene.followCameraMs && nextScene.durationMs == *nextScene.followCameraMs + 250,
            "Script 18 opening cinematic lost its retail track or return-to-gameplay timing.");
    const auto& lcClips = nextScene.actorAnimations.at("LC_00");
    require(nextScene.actorAnimations.at("Player").size() == 3 &&
            nextScene.actorAnimations.at("Juan_00").size() == 4 && lcClips.size() == 7 &&
            lcClips.front().first.name == "cut_gesture_talk_angryhand" &&
            lcClips[5].first.name == "cut_gesture_pointright",
            "Script 18 opening cinematic lost a player, Juan, or LC animation.");
    for (const std::string_view name : {"LC_00", "aHummer_00"}) {
        const auto actor = std::find_if(opening.actors.begin(), opening.actors.end(), [&](const auto& entry) {
            return entry.payphoneOnly && entry.name == name;
        });
        require(actor != opening.actors.end() && !actor->model.meshes.empty() &&
                (name != "LC_00" || !actor->skins.empty()),
                "Script 18 opening cinematic lost the LC or Hummer model.");
        CitySurface surface{};
        surface.payphoneActor = actor - opening.actors.begin();
        CityPlayback resumed;
        require(!citySurfaceVisible(surface, opening, resumed, route.cinematicDurationMs),
                "LC mission actor appeared before the cinematic.");
        resumed.nextMissionVehicleActive = true;
        require(citySurfaceVisible(surface, opening, resumed, route.cinematicDurationMs) == (name == "aHummer_00"),
                "LC mission did not retain only the objective vehicle after the cinematic.");
        if (name == "aHummer_00") {
            require(markerDraw.nextVehicleGeometry && markerDraw.nextVehicleFootprint &&
                    markerDraw.nextVehicleGeometry->vehicleVertexCount > 0 &&
                    markerDraw.nextVehicleGeometry->vehicleFirstVertex > markerDraw.vehicleFirstVertex &&
                    markerDraw.nextVehicleGeometry->wheels.size() == 4 &&
                    markerDraw.nextVehicleFootprint->radius > 0,
                    "Hummer geometry did not resolve its own wheels and collision footprint.");
            const auto& geometry = *markerDraw.nextVehicleGeometry;
            const auto begin = markerDraw.vertices.begin() + geometry.vehicleFirstVertex;
            const std::vector<CityVertex> source(begin, begin + geometry.vehicleVertexCount);
            auto posed = source;
            CityVehiclePose shifted{actor->position, actor->rotationDegrees[2]};
            shifted.position[0] += 100;
            poseCityVehicle(*actor, shifted, source, geometry.wheels, geometry.vehicleMorphs, posed.data());
            require(std::abs(posed.front().position[0] - source.front().position[0] - 100) < 0.05f,
                    "Hummer render geometry did not follow its vehicle pose.");
            require(actor->drive && actor->suspension && actor->vehicleHealth && actor->vehicleCamera &&
                    actor->gearProfile && actor->drive->massKg > 0 && !actor->doorClips[0].empty(),
                    "Hummer lost its authored vehicle tuning or left door clips.");
            CityOpening world;
            auto& hummer = world.actors.emplace_back(*actor);
            hummer.position = {0, 0, 0};
            hummer.rotationDegrees = {};
            world.vehicleActor = 0;
            world.checkpoint.emplace();
            CityPlayback driver;
            driver.timeAfterCheckpointMs = 0;
            driver.player = CityPlayerPose{{100, 0, 0}, 0};
            const auto terrain = flatTerrain();
            driver.drive(world, terrain, {}, 0, true);
            require(driver.vehicle && driver.vehicle->occupied &&
                    driver.vehicle->health == *hummer.vehicleHealth,
                    "Hummer did not initialize or accept entry with its authored data.");
            for (unsigned frame = 0; frame < 120; ++frame)
                driver.drive(world, terrain, {0, 1}, 16, false);
            require(driver.vehicle->speed > 0 && driver.vehicle->position != hummer.position,
                    "Hummer did not drive using the decoded tuning.");
            driver.nextMissionVehicleActive = true;
            driver.nextMissionVehicleAcquired = true;
            driver.missionConditions.apply({70, hummerRoute.vehicleProtectionCommand});
            driver.vehicle->health = 0;
            driver.vehicle->exploded = true;
            driver.updateMission(world, 16);
            require(driver.missionFailure == -1 && driver.missionFailureCount == 1,
                    "Live Hummer state did not trigger ProtectCar.");
            require(driver.acknowledgeFailure(world, 1000, true, true) &&
                    driver.nextMissionVehicleActive && !driver.nextMissionVehicleAcquired && !driver.vehicle &&
                    !driver.missionFailure && driver.missionFailureCount == 1,
                    "Retry did not retain the active Hummer mission and reset the route.");
        }
    }
    require(postFight.placements.size() == 6 && postFight.placements[0].actor == "Juan_00" &&
            postFight.placements[1].actor == "Player" && postFight.placements[0].timeMs == 1500 &&
            postFight.placements[1].timeMs == 1500 &&
            postFight.placements[0].position != postFight.placements[1].position,
            "Post-fight staging lost Juan or the player marker.");
    const auto& juanClips = postFight.actorAnimations.at("Juan_00");
    const auto& playerClips = postFight.actorAnimations.at("Player");
    require(juanClips.size() == 5 && juanClips.front().first.name == "cut_cellphonetalk_idle" &&
            juanClips[1].startMs == 4000 && juanClips[2].first.name == "cut_cellphonetalk_hangup" &&
            playerClips.size() == 4 && playerClips.front().first.name == "cut_gesture_annoyed_idle" &&
            playerClips.back().loop.name == "move_idle",
            "Post-fight Juan or player animation cues did not resolve.");
    require(postFight.cameras.size() == 2 && postFight.cameras[1].startMs == 1500 &&
            postFight.cameras[1].durationMs == 20000 && postFight.fades.size() == 4 &&
            postFight.fades[0].startMs == 1000 && !postFight.fades[0].fadeIn &&
            postFight.fades[1].startMs == 1500 && postFight.fades[1].fadeIn &&
            postFight.fades[2].startMs == 8225 && !postFight.fades[2].fadeIn &&
            postFight.fades[3].startMs == 8725 && postFight.fades[3].fadeIn && postFight.followCameraMs == 8725,
            "Post-fight scene lost its retail camera and fade timing.");
    require(postFight.dialogue.size() == 7 && postFight.dialogue.front().startMs == 3000 &&
            postFight.dialogue.back().startMs == 8225 && postFight.durationMs == 8975 &&
            postFight.dialogue.front().portrait == 1 && postFight.dialogue.back().portrait == 0 &&
            postFight.audio.size() == 2 && postFight.audio.front().timeMs == 1500 &&
            postFight.audio.front().soundId == 16 && postFight.audio.back().action == CityAudioAction::stop &&
            postFight.audio.back().timeMs == 8975 && postFight.nextScript == 84 && postFight.unlockedScript == 18,
            "Post-fight dialogue, audio, or subscript handoff changed.");
    CityPayphonePlayback postFightPlayback;
    unsigned postFightLoops = 0;
    for (size_t line = 0; line < postFight.dialogue.size(); ++line) {
        require(!postFightPlayback.advance(postFight, postFight.durationMs, false,
                    [&](const auto&) { ++postFightLoops; }) && postFightPlayback.activeDialogue() == line,
                "Post-fight playback skipped a blocking line.");
        require(!postFightPlayback.advance(postFight, 0, true, {}),
                "Post-fight playback skipped the return-to-gameplay subscript.");
    }
    require(postFightPlayback.advance(postFight, 750, false, [&](const auto&) { ++postFightLoops; }) &&
            postFightLoops == 2, "Post-fight audio loop start and stop did not dispatch once each.");
    const auto attack = decodeCityActorAttack({29, route.attackCommandPayload});
    require(attack.attacker == "Loco_00" && attack.target == "Player",
            "Payphone handoff lost its authored attacker or victim.");
    require(route.gameplayObjective == "GIVE THE PUNK A BEATIN'" &&
            route.gameplayTutorial.speaker == "Combat" &&
            route.gameplayTutorial.text.find("attack icon") != std::string::npos,
            "Payphone handoff lost its retail fight objective or tutorial.");
    require(route.walk && route.walk->actor == "Juan_00" && route.walk->from != route.walk->to &&
            route.walk->startMs == 10600 && route.walk->durationMs > 0,
            "Payphone scene lost Juan's retail WalkTo marker.");
    const auto walkMs = route.walk->durationMs;
    const auto* farPlayer = cityPayphonePlacement(route, "Player", 500);
    const auto* closePlayer = cityPayphonePlacement(route, "Player", 8800);
    require(!cityPayphonePlacement(route, "Player", 499) && farPlayer && closePlayer &&
            farPlayer->visible && closePlayer->visible && farPlayer->position != closePlayer->position &&
            cityPayphonePlacement(route, "Player", 8799) == farPlayer,
            "Payphone cinematic lost its two authored player placements.");
    const auto* closeJuan = cityPayphonePlacement(route, "Juan_00", 8800);
    const auto* hiddenJuan = cityPayphonePlacement(route, "Juan_00", route.cinematicDurationMs);
    require(route.cinematicPlacements.size() == 7 &&
            closeJuan && closeJuan->visible && hiddenJuan && !hiddenJuan->visible,
            "Payphone cinematic lost Juan's placement or disable cue.");
    const auto* phoneLoco = cityPayphonePlacement(route, "Loco_00", route.cinematicDurationMs - 1);
    const auto* fightLoco = cityPayphonePlacement(route, "Loco_00", route.cinematicDurationMs);
    require(phoneLoco && fightLoco && fightLoco->visible && phoneLoco->position != fightLoco->position,
            "Payphone handoff did not move Loco to the fight marker.");
    for (const std::string_view name : {"Juan_00", "Loco_00"}) {
        const auto actor = std::find_if(opening.actors.begin(), opening.actors.end(), [&](const auto& entry) {
            return entry.payphoneOnly && entry.name == name;
        });
        require(actor != opening.actors.end() && !actor->skins.empty() && actor->animations.size() > 1,
                "Payphone NPC model or animation clips are missing.");
        if (name == "Loco_00")
            require(actor->animations.back().startMs == route.cinematicDurationMs &&
                    actor->animations.back().first.name == "idle_to_combat_hand" &&
                    actor->handPunches && (*actor->handPunches)[0].name == "combat_hand_rightpunch" &&
                    (*actor->handPunches)[1].name == "combat_hand_leftpunch" &&
                    actor->defeatAnimation && actor->defeatAnimation->first.name == "move_idle_to_dead" &&
                    actor->defeatAnimation->loop.name == "move_dead_idle",
                    "Loco lost his scripted combat stance, punch clips, or defeat clips.");
        CitySurface surface{};
        surface.payphoneActor = actor - opening.actors.begin();
        require(!citySurfaceVisible(surface, opening, departing) &&
                !citySurfaceVisible(surface, opening, departing, 499) &&
                citySurfaceVisible(surface, opening, departing, 500) &&
                citySurfaceVisible(surface, opening, departing, route.cinematicDurationMs) == (name == "Loco_00"),
                "Payphone NPC visibility did not match the authored cues.");
    }
    const auto& shots = route.cinematicCameras;
    require(shots.size() == 4 && shots[1].startMs == 500 && shots[1].durationMs == 6000 &&
            shots[3].startMs == 8800 && shots[3].durationMs == 15000 &&
            shots[1].first.eye == opening.namedCameras.at("SceneCamera_payphoneFar_00").eye &&
            shots[3].second.eye == opening.namedCameras.at("SceneCamera_payphoneClose02").eye,
            "Payphone cinematic lost its retail camera timing or endpoints.");
    const auto& fades = route.cinematicFades;
    require(fades.size() == 5 && fades.front().startMs == 0 && fades.front().durationMs == 500 &&
            !fades.front().fadeIn && fades[3].startMs == 8800 && fades[3].fadeIn &&
            fades.back().startMs == 15400 + walkMs && !fades.back().fadeIn,
            "Payphone cinematic lost its authored fade sequence.");
    const auto& lines = route.cinematicDialogue;
    require(lines.size() == 2 && lines[0].startMs == 14600 + walkMs && lines[0].portrait == 18 &&
            lines[1].startMs == 15400 + walkMs && lines[1].portrait == 1 &&
            lines[0].durationMs == 0 && lines[1].durationMs == 0 &&
            !lines[0].text.empty() && !lines[1].text.empty(),
            "Payphone cinematic lost its localized blocking dialogue cues.");
    const auto& audio = route.cinematicAudio;
    require(audio.size() == 2 && audio[0].timeMs == 500 && audio[1].timeMs == 15900 + walkMs &&
            audio[0].soundId == 42 && audio[1].soundId == 42 &&
            audio[0].action == CityAudioAction::playPriorityLoop && audio[1].action == CityAudioAction::stop,
            "Payphone cinematic lost its priority sound start or stop.");
    CityPayphonePlayback payphone;
    std::vector<CityAudioAction> played;
    const auto advancePayphone = [&](double delta, bool pressed = false) {
        return payphone.advance(route, delta, pressed, [&](const auto& cue) { played.push_back(cue.action); });
    };
    require(!advancePayphone(route.cinematicDurationMs) && payphone.elapsedMs() == lines[0].startMs &&
            payphone.activeDialogue() == 0 && played == std::vector{CityAudioAction::playPriorityLoop},
            "Payphone timeline passed its first dialogue or played the stop too early.");
    require(!advancePayphone(1000) && payphone.elapsedMs() == lines[0].startMs && payphone.activeDialogue() == 0,
            "Payphone timeline advanced during manual dialogue.");
    require(!advancePayphone(1000, true) && !payphone.activeDialogue() &&
            !advancePayphone(800) && payphone.activeDialogue() == 1,
            "Payphone timeline did not wait for a separate dismissal of its second line.");
    require(!advancePayphone(0, true) && advancePayphone(500) && route.cinematicDurationMs == 15900 + walkMs &&
            played == std::vector{CityAudioAction::playPriorityLoop, CityAudioAction::stop},
            "Payphone timeline did not complete with the authored audio stop.");
    auto outside = route.departure;
    outside[2] += route.departureRadius - 1;
    departing.player = CityPlayerPose{outside, 0};
    departing.advancePayphone(opening, route.departureDelayMs);
    require(!departing.payphoneActive, "WaitEscape advanced below its authored distance.");
    departing.player->position[2] += 1;
    departing.advancePayphone(opening, 0);
    departing.vehicle = CityVehiclePose{actor.position, actor.rotationDegrees[2]};
    departing.vehicle->exploded = true;
    departing.updateMission(opening, 0);
    require(departing.payphoneActive && !departing.missionFailure,
            "Opening still failed on vehicle destruction after clearing conditions.");
    departing.player->position = route.destination.position;
    departing.player->position[2] += route.destination.radius;
    departing.advancePayphone(opening, 0);
    require(!departing.payphoneReached, "Checkpoint accepted the exact radius boundary.");
    departing.player->position[2] -= 1;
    departing.advancePayphone(opening, 0);
    require(departing.payphoneReached, "Checkpoint did not accept a position inside its radius.");

    const auto failure = rasterRetailSpriteAnimation(assets, "cinematics_hud.bsprite", "FAILED", 480, 320);
    require(failure.size() == 5 && failure.back().duration == 1 &&
            std::all_of(failure.begin(), failure.end() - 1, [](const auto& step) { return step.duration == 3; }),
            "Retail failure animation timing changed.");
    require(failure.back().image.bgra != failure[failure.size() - 2].image.bgra &&
            std::any_of(failure.back().image.bgra.begin(), failure.back().image.bgra.end(),
                        [](uint32_t pixel) { return pixel >> 24; }),
            "Retail failure artwork was not composed.");
    std::cout << "Opening ProtectCar command registered and gated failure on the explosion flag.\n";
    CityVehiclePose anchored{actor.position, actor.rotationDegrees[2]};
    const auto origin = cityVehicleSmokeFrame(actor, anchored);
    anchored.position[0] += 50;
    const auto movedOrigin = cityVehicleSmokeFrame(actor, anchored);
    require(origin && movedOrigin && std::abs(movedOrigin->origin[0] - origin->origin[0] - 50) < 0.01f,
            "Smoke anchor did not follow the retail Lincoln.");
    for (const auto& [path, emitter] : std::array<std::pair<const char*, const char*>, 3>{{
        {"p_smoke_00.bdae", "IrrPCloud01-emitter"},
        {"p_fire_00.bdae", "p_fire_00-emitter"},
        {"p_explosion_00.bdae", "dirt02-emitter"}
    }}) {
        const auto effect = loadCityScene(assets, path);
        require(effect.particleEmitters.size() == 1 && effect.particleEmitters.front().name == emitter,
                "Retail vehicle effect emitter did not decode.");
        require(cityParticleTexture(effect) == (std::string_view(path) == "p_smoke_00.bdae" ? "smoke.tga" : "fire.tga"),
                "Retail vehicle effect texture did not bind.");
        const auto& definition = effect.particleEmitters.front();
        const auto node = std::find_if(effect.nodes.begin(), effect.nodes.end(), [&](const CityNode& entry) {
            return std::find(entry.particleEmitters.begin(), entry.particleEmitters.end(), definition.name) !=
                   entry.particleEmitters.end();
        });
        require(node != effect.nodes.end(), "Particle instance did not bind to a scene node.");
        const auto clip = readCityParticleClip(effect);
        const auto nodeIndex = static_cast<size_t>(node - effect.nodes.begin());
        const auto& birthRate = definition.tracks.at(CityParticleParameter::birthRate);
        const bool fire = definition.name == "p_fire_00-emitter";
        const bool explosion = definition.name == "dirt02-emitter";
        require(effect.diffuseColors.size() == (fire ? 31 : explosion ? 34 : 101),
                "Particle material track did not retain its retail key count.");
        CityParticle colorSample{};
        colorSample.lifetime = 1;
        colorSample.animationScale = 1;
        const auto startColor = cityParticleColor(effect, colorSample, 0);
        require(startColor == (fire || explosion ? std::array<float, 4>{1, 1, 1, 1}
                                                : std::array<float, 4>{0, 0, 0, 1}),
                "Particle material initial color changed.");
        colorSample.age = 0.95f;
        const auto lateColor = cityParticleColor(effect, colorSample, 0);
        require(lateColor[3] < 1 && lateColor[3] > 0, "Particle material opacity did not fade with age.");
        require(definition.direction == std::array<float, 3>{0, 0, fire ? 0.5f : 1.f} &&
                definition.directionVariation == (explosion ? 0.3f : fire ? 0.25f : 0.1f),
                "Retail particle direction or direction variation changed.");
        require(definition.type == 0 && definition.maxParticles > 0 && definition.targetSize >= 200 &&
                definition.dimensions[0] > 0 && birthRate.front().timeMs == 0 && birthRate.front().value == 0 &&
                birthRate.at(1).timeMs == 33 && birthRate.at(1).value > 0,
                "Retail vehicle emitter parameters or birth-rate track changed.");
        if (explosion) {
            const auto first = cityParticleFrame(effect, nodeIndex, clip, 0, {});
            const auto later = cityParticleFrame(effect, nodeIndex, clip, clip.durationMs, {});
            require(first.origin[2] != later.origin[2] && clip.durationMs == 1100,
                    "Retail explosion scene did not move its emitter along its Z track.");
            require(definition.tracks.size() == 5 && birthRate.at(1).value == 300 &&
                    birthRate.at(7).timeMs == 233 && birthRate.at(7).value == 0,
                    "Explosion emission did not match its retail burst.");
            CityParticleSimulation burst;
            burst.advance(definition, 0.1f, 100, {});
            require(burst.particles().size() == 30, "Explosion track did not reach its retail particle limit.");
            require(std::any_of(burst.particles().begin(), burst.particles().end(), [](const CityParticle& particle) {
                return std::abs(particle.velocity[0]) > 1 && std::abs(particle.velocity[1]) > 1;
            }), "Explosion emitted only along its authored axis.");
            require(std::all_of(burst.particles().begin(), burst.particles().end(), [](const CityParticle& particle) {
                const auto& v = particle.velocity;
                const auto speed = std::hypot(v[0], v[1], v[2]);
                return v[2] > 0 && speed >= 225 && speed <= 375;
            }), "Explosion direction variance changed the authored speed range.");
            burst.advance(definition, 1.1f, 1100, {});
            require(burst.particles().empty(), "Explosion burst persisted beyond its retail lifetime.");
        }
    }
    const auto first = geometry.vertices.begin() + geometry.vehicleFirstVertex;
    const std::vector<CityVertex> rest(first, first + geometry.vehicleVertexCount);
    require(geometry.wheels.size() == 4 &&
            std::count_if(geometry.wheels.begin(), geometry.wheels.end(),
                          [](const CityWheelRange& wheel) { return wheel.steers; }) == 2,
            "Lincoln wheel geometry did not bind to two front and two rear wheels.");
    CityVehiclePose wheelPose{actor.position, actor.rotationDegrees[2]};
    std::vector<CityVertex> posed(rest.size());
    {
        CityTerrain slope;
        const auto point = [&](float x, float y) {
            return std::array{actor.position[0] + x, actor.position[1] + y, actor.position[2] + 0.07f*x + 0.03f*y};
        };
        slope.ground = {{point(-500, -500), point(500, -500), point(500, 500)},
                        {point(-500, -500), point(500, 500), point(-500, 500)}};
        const auto offsets = cityVehicleWheelOffsets(actor, wheelPose, geometry.wheels, slope);
        const auto mounts = cityVehicleLightFrames(actor, wheelPose);
        const auto raisedMounts = cityVehicleLightFrames(actor, wheelPose, geometry.wheels, offsets);
        poseCityVehicle(actor, wheelPose, rest, geometry.wheels, geometry.vehicleMorphs, posed.data(), offsets);
        std::array<bool, 2> matched{};
        for (size_t i = 0; i < geometry.wheels.size(); ++i) {
            const auto& wheel = geometry.wheels[i];
            const auto height = 0.07f*(wheel.pivot[0] - actor.position[0]) + 0.03f*(wheel.pivot[1] - actor.position[1]);
            require(std::abs(offsets.at(i) - height) < 0.005f &&
                    std::abs(posed[wheel.firstVertex].position[2] - rest[wheel.firstVertex].position[2] - height) < 0.005f,
                    "Retail Lincoln wheel did not follow its sampled height.");
            if (wheel.steers) continue;
            const auto side = wheel.left ? 1u : 0u;
            require(!matched[side] && std::abs(raisedMounts->at(side).origin[2] - mounts->at(side).origin[2] - height) < 0.005f,
                    "Retail Lincoln light mount did not follow its own rear wheel.");
            matched[side] = true;
        }
        require(matched[0] && matched[1], "Retail Lincoln did not bind both rear wheel heights.");
        auto running = wheelPose;
        updateCityVehicleSuspension(actor, running, geometry.wheels, slope, 20);
        const auto suspended = cityVehicleWheelOffsets(actor, running, geometry.wheels, slope);
        require(std::hypot(running.chassisTilt[0], running.chassisTilt[1]) > 1,
                "The four wheel contacts did not tilt the Lincoln chassis.");
        const auto tiltedMounts = cityVehicleLightFrames(actor, running);
        require(tiltedMounts->front().axes != mounts->front().axes,
                "The light mount did not follow the Lincoln chassis rotation.");
        poseCityVehicle(actor, running, rest, geometry.wheels, geometry.vehicleMorphs, posed.data());
        const auto firstBodyVertex = geometry.vehicleMorphs.front().firstVertex;
        require(posed[firstBodyVertex].position != rest[firstBodyVertex].position &&
                posed[firstBodyVertex].normal != rest[firstBodyVertex].normal,
                "The Lincoln chassis rotation did not affect the body pose.");
        for (size_t i = 0; i < offsets.size(); ++i) {
            require(std::abs(running.suspension.at(i).speed + 0.196f) < 0.0001f &&
                    std::abs(suspended.at(i) + offsets[i] + 0.00392f) < 0.0001f,
                    "Retail suspension impulse or ground change was integrated incorrectly.");
        }
        updateCityVehicleSuspension(actor, running, geometry.wheels, slope, 20);
        for (size_t i = 0; i < offsets.size(); ++i) {
            const float speed = -0.196f + 0.02f * (-35000.f / 425 * suspended[i] + 5 * 0.196f - 9.8f);
            require(std::abs(running.suspension.at(i).speed - speed) < 0.0001f,
                    "Retail spring and damper did not carry velocity into the next tick.");
        }
        CityTerrain rampTerrain;
        rampTerrain.jumpRamps.push_back({slope.ground[0], slope.ground[1]});
        const auto flat = [&](float x, float y) {
            return std::array{actor.position[0] + x, actor.position[1] + y, actor.position[2] - 25};
        };
        rampTerrain.ground = {{flat(-500, -500), flat(500, -500), flat(500, 500)},
                              {flat(-500, -500), flat(500, 500), flat(-500, 500)}};
        const auto rampOffsets = cityVehicleWheelOffsets(actor, wheelPose, geometry.wheels, rampTerrain);
        require(rampOffsets == offsets, "Wheel contacts selected the ground below the jump ramp.");
        auto rampPose = wheelPose;
        updateCityVehicleSuspension(actor, rampPose, geometry.wheels, rampTerrain, 20);
        require(rampPose.chassisTilt == running.chassisTilt, "Ramp contact did not carry the authored slope into chassis tilt.");
        rampPose.airborne = true;
        updateCityVehicleSuspension(actor, rampPose, geometry.wheels, rampTerrain, 20);
        require(rampPose.chassisTilt == running.chassisTilt &&
                rampPose.suspension.front().lastGround == rampOffsets.front(),
                "Airborne suspension picked up a new terrain contact.");
        const auto previousTilt = running.chassisTilt;
        updateCityVehicleSuspension(actor, running, geometry.wheels, CityTerrain{}, 0);
        require(running.chassisTilt == previousTilt && running.suspension.front().lastGround == offsets.front(),
                "A missing wheel contact injected a false terrain sample.");
    }
    const auto checkWheelPose = [&](const CityVehiclePose& pose, bool frontOnly) {
        poseCityVehicle(actor, pose, rest, geometry.wheels, geometry.vehicleMorphs, posed.data());
        size_t moved = 0, expected = 0;
        for (const auto& wheel : geometry.wheels) {
            require(wheel.radius > 20 && wheel.radius < 60, "Unexpected retail tire radius.");
            size_t wheelMoved = 0;
            for (size_t i = wheel.firstVertex; i < wheel.firstVertex + wheel.vertexCount; ++i) {
                const auto& a = rest[i].position;
                const auto& b = posed[i].position;
                wheelMoved += std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) > 0.1;
            }
            const bool shouldMove = !frontOnly || wheel.steers;
            require((wheelMoved > 0) == shouldMove, "Wheel pose moved the wrong nodes.");
            expected += wheelMoved;
        }
        for (size_t i = 0; i < rest.size(); ++i) {
            const auto& a = rest[i].position;
            const auto& b = posed[i].position;
            moved += std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) > 0.1;
        }
        require(moved == expected, "Wheel pose deformed a non-wheel part.");
    };
    wheelPose.steering = 25;
    checkWheelPose(wheelPose, true);
    wheelPose.steering = 0;
    wheelPose.wheelTravel = 50;
    checkWheelPose(wheelPose, false);
    CityPlayback playback;
    playback.elapsedMs = actor.startMs;
    playback.vehicle = CityVehiclePose{actor.position, actor.rotationDegrees[2]};
    require(geometry.vehicleMorphs.size() == 1, "Retail Lincoln lost its body morph draw range.");
    const auto& bodyRange = geometry.vehicleMorphs.front();
    const auto& bodyMorph = actor.model.morphControllers.at(bodyRange.controller);
    require(std::any_of(geometry.skins.begin(), geometry.skins.end(), [&](const CitySkinRange& skin) {
        return skin.actor == *opening.vehicleActor &&
               skin.firstVertex == geometry.vehicleFirstVertex + bodyRange.firstVertex;
    }), "Retail Lincoln body morph did not bind to the animated body skin.");
    const auto checkBodyMorph = [&](size_t targetIndex, float weight, bool animateDoor) {
        auto& vehicle = *playback.vehicle;
        vehicle.doorAction.reset();
        if (animateDoor) vehicle.doorAction = CityVehiclePose::DoorAction{0, false, 400};
        const auto pose = [&] {
            poseCityVehicle(actor, vehicle, rest, geometry.wheels, geometry.vehicleMorphs,
                            geometry.vertices.data() + geometry.vehicleFirstVertex);
            poseCityActors(opening, geometry.skins, playback, geometry.vertices.data());
        };
        vehicle.rearDeformation = vehicle.frontDeformation = 0;
        pose();
        const auto first = geometry.vertices.begin() + geometry.vehicleFirstVertex + bodyRange.firstVertex;
        const auto& shape = actor.model.meshes.at(bodyMorph.targets.at(targetIndex).mesh);
        const std::vector<CityVertex> before(first, first + shape.positions.size());
        (targetIndex == 0 ? vehicle.rearDeformation : vehicle.frontDeformation) = weight;
        pose();
        size_t moved = 0, changedNormals = 0;
        for (size_t i = 0; i < shape.positions.size(); ++i) {
            const auto& vertex = first[i];
            const auto& local = shape.positions[i];
            moved += vertex.position != before[i].position;
            changedNormals += vertex.normal != before[i].normal;
            if (animateDoor) continue;
            for (size_t axis = 0; axis < 3; ++axis) {
                const auto& bind = bodyRange.bindTransform;
                const float full = bind[3][axis] + local[0]*bind[0][axis] + local[1]*bind[1][axis] + local[2]*bind[2][axis];
                const float expected = before[i].position[axis] * (1 - weight) + full * weight;
                require(std::abs(vertex.position[axis] - expected) < 0.005f,
                        "Vehicle damage did not interpolate toward the authored body shape.");
            }
        }
        require(moved > 0 && changedNormals > 0, "The damage morph did not reach the body positions and normals.");
        vehicle.doorAction.reset();
        vehicle.rearDeformation = vehicle.frontDeformation = 0;
        pose();
    };
    checkBodyMorph(0, 1.f, false);
    checkBodyMorph(1, 0.5f, false);
    checkBodyMorph(0, 1.f, true);
    const auto countInterior = [&](bool visible) {
        return std::count_if(geometry.surfaces.begin(), geometry.surfaces.end(), [&](const CitySurface& surface) {
            return !surface.payphoneActor && surface.vehicleInterior &&
                citySurfaceVisible(surface, opening, playback) == visible;
        });
    };
    const auto interior = countInterior(false);
    require(interior > 0 && countInterior(true) == 0, "Closed Lincoln exposed its interior surfaces.");
    const auto exterior = std::count_if(geometry.surfaces.begin(), geometry.surfaces.end(),
        [](const CitySurface& surface) { return !surface.payphoneActor && surface.vehicleExterior; });
    const auto countCulled = [&] {
        return std::count_if(geometry.surfaces.begin(), geometry.surfaces.end(),
            [&](const CitySurface& surface) { return !surface.payphoneActor && citySurfaceCullBackfaces(surface, playback); });
    };
    require(exterior > 0 && countCulled() == exterior, "Closed Lincoln exterior did not cull backfaces.");
    float roofArea = 0;
    for (const auto& surface : geometry.surfaces) {
        if (surface.payphoneActor || !surface.vehicleExterior) continue;
        for (size_t i = surface.firstIndex; i < surface.firstIndex + surface.indexCount; i += 3) {
            const auto& a = geometry.vertices[geometry.indices[i]].position;
            const auto& b = geometry.vertices[geometry.indices[i + 1]].position;
            const auto& c = geometry.vertices[geometry.indices[i + 2]].position;
            if (std::min({a[2], b[2], c[2]}) - actor.position[2] < 125) continue;
            roofArea += (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        }
    }
    require(roofArea > 0, "Lincoln roof winding would cull the outward faces.");

    for (const size_t side : {0u, 1u}) {
        for (const double sampleMs : {400.0, 700.0, 566.0}) {
            auto& vehicle = *playback.vehicle;
            const bool exiting = sampleMs == 566.0;
            vehicle.doorAction = CityVehiclePose::DoorAction{side, exiting, sampleMs};
            require(countInterior(true) == interior && countInterior(false) == 0,
                    "Door transition did not render the interior surfaces.");
            require(countCulled() == 0, "Door transition still culled the exterior backfaces.");
            poseCityVehicle(actor, vehicle, rest, geometry.wheels, geometry.vehicleMorphs,
                            geometry.vertices.data() + geometry.vehicleFirstVertex);
            poseCityActors(opening, geometry.skins, playback, geometry.vertices.data());
            size_t moved = 0, stationary = 0;
            for (size_t i = 0; i < rest.size(); ++i) {
                const auto& a = rest[i].position;
                const auto& b = geometry.vertices[geometry.vehicleFirstVertex + i].position;
                const auto distance = std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
                require(std::isfinite(distance) && distance < 250, "Door animation displaced a vertex outside the vehicle.");
                if (distance > 1) ++moved;
                else ++stationary;
            }
            require(moved && stationary, "Door animation did not move part of the vehicle.");
            const char* phase = exiting ? " exit" : sampleMs == 400.0 ? " opening" : " closing";
            std::cout << (side == 0 ? "Left" : "Right") << phase
                      << " displaced " << moved << " of " << rest.size() << " vehicle vertices.\n";
        }
    }
    const auto playerRange = std::find_if(geometry.skins.begin(), geometry.skins.end(),
        [&](const CitySkinRange& range) { return opening.actors[range.actor].name == "Player"; });
    require(playerRange != geometry.skins.end(), "Opening player skin is missing.");
    const auto& player = opening.actors[playerRange->actor];
    playback.elapsedMs = std::max(player.startMs, actor.startMs);
    playback.player = CityPlayerPose{actor.position, actor.rotationDegrees[2]};
    for (const size_t side : {0u, 1u}) {
        const auto& clips = (*opening.vehicleCharacterClips)[side];
        const auto& skin = player.skins[playerRange->skin];
        const auto samplePlayer = [&](bool exiting, double ms) {
            playback.vehicle->doorAction = CityVehiclePose::DoorAction{side, exiting, ms};
            poseCityActors(opening, geometry.skins, playback, geometry.vertices.data());
            const auto begin = geometry.vertices.begin() + playerRange->firstVertex;
            return std::vector<CityVertex>(begin, begin + skin.vertices.size());
        };
        const auto baseline = samplePlayer(false, 0);
        const auto sampleChangesPose = [&](bool exiting, double ms) {
            const auto posed = samplePlayer(exiting, ms);
            size_t moved = 0;
            for (size_t i = 0; i < posed.size(); ++i) {
                const auto& position = posed[i].position;
                const auto& rest = baseline[i].position;
                const auto distance = std::hypot(position[0] - rest[0], position[1] - rest[1], position[2] - rest[2]);
                require(std::isfinite(distance) && distance < 500, "Unbounded player vehicle pose.");
                moved += distance > 1;
            }
            require(moved > posed.size() / 2, "Player vehicle clip did not update the skin.");
        };
        sampleChangesPose(false, clips.open.durationMs / 2.0);
        sampleChangesPose(false, clips.open.durationMs + clips.enter.durationMs / 2.0);
        sampleChangesPose(false, clips.open.durationMs + clips.enter.durationMs + clips.close.durationMs / 2.0);
        sampleChangesPose(true, clips.exit.durationMs / 2.0);
        std::cout << (side == 0 ? "Left" : "Right") << " player door, entry, close, and exit poses passed.\n";
    }
    auto& vehicle = *playback.vehicle;
    vehicle.doorAction.reset();
    vehicle.occupied = true;
    require(countInterior(false) == interior, "Closed interior stayed visible while driving.");
    require(countCulled() == exterior, "Exterior culling did not resume while driving.");
    vehicle.seatedMs = 125;
    poseCityActors(opening, geometry.skins, playback, geometry.vertices.data());
    const auto start = geometry.vertices[playerRange->firstVertex].position;
    vehicle.position[0] += 120;
    vehicle.seatedMs += opening.vehicleSeated->durationMs;
    poseCityActors(opening, geometry.skins, playback, geometry.vertices.data());
    const auto end = geometry.vertices[playerRange->firstVertex].position;
    require(std::abs(end[0] - start[0] - 120) < 0.05f &&
            std::abs(end[1] - start[1]) < 0.05f && std::abs(end[2] - start[2]) < 0.05f,
            "Seated player did not follow the vehicle or loop its pose.");
    std::cout << "Seated player stays attached to the vehicle across an idle loop.\n";
    vehicle.occupied = false;
    const auto& gestures = opening.payphone->playerAnimations;
    require(gestures.size() == 1 && gestures.front().startMs == 2300 &&
            gestures.front().first.name == "cut_gesture_talk_angryhand" &&
            gestures.front().loop.name == "move_idle",
            "Payphone player gesture or idle transition did not resolve.");
    const auto sampleGesture = [&](double time) {
        poseCityActors(opening, geometry.skins, playback, geometry.vertices.data(), time);
        return geometry.vertices[playerRange->firstVertex].position;
    };
    const auto gestureStart = sampleGesture(gestures.front().startMs);
    const auto gestureMiddle = sampleGesture(gestures.front().startMs + gestures.front().first.durationMs / 2.);
    require(gestureStart != gestureMiddle, "Payphone gesture did not change the player skin pose.");
}

void verifyPayphonePursuit()
{
    CityOpening opening;
    opening.walkSpeed = 400;
    auto& route = opening.payphone.emplace();
    route.handRangeCm = 100;
    route.handDamage = 12;
    route.cinematicPlacements.push_back({0, "Loco_00", {0, 0, 0}, {}, true});
    CityPlayback playback;
    playback.player = CityPlayerPose{{0, -400, 0}, 0};
    auto terrain = flatTerrain();
    playback.pursuePayphoneTarget(opening, terrain, 50);
    require(!playback.payphonePursuer, "Pursuit began before ActorAttack.");
    playback.payphoneAttack = CityActorAttack{"Loco_00", "Player"};
    for (unsigned i = 0; i < 20; ++i) playback.pursuePayphoneTarget(opening, terrain, 50);
    require(playback.payphonePursuer && playback.payphonePursuer->position[1] == -300 &&
            !playback.payphonePursuer->walking,
            "Pursuit did not stop at hand-combat range.");
    auto& attacker = opening.actors.emplace_back();
    attacker.name = "Loco_00";
    attacker.payphoneOnly = true;
    attacker.handPunches = std::array{CityAnimationClip{"right", 100, {}}, CityAnimationClip{"left", 200, {}}};
    playback.pursuePayphoneTarget(opening, terrain, 50);
    require(playback.payphonePunchMs == 0 && playback.payphonePunchIndex == 0,
            "NPC did not start the first punch in range.");
    require(playback.playerHealth == 300, "Punch applied damage before contact.");
    playback.pursuePayphoneTarget(opening, terrain, 50);
    require(playback.payphonePunchMs == 50 && playback.payphonePunchIndex == 0,
            "NPC changed punch clips before the first one finished.");
    require(playback.playerHealth == 288, "Punch missed its contact time.");
    playback.pursuePayphoneTarget(opening, terrain, 50);
    require(playback.payphonePunchMs == 0 && playback.payphonePunchIndex == 1,
            "NPC did not switch to the other punch after the first one finished.");
    require(playback.playerHealth == 288, "Punch applied damage more than once.");
    playback.player->position[1] -= 200;
    playback.pursuePayphoneTarget(opening, terrain, 50);
    playback.pursuePayphoneTarget(opening, terrain, 50);
    require(playback.playerHealth == 288, "Punch hit after the player moved out of reach.");
    playback.player->position[1] += 200;
    playback.playerHealth = 4;
    playback.payphonePunchMs = 0;
    playback.payphonePunchIndex = 0;
    playback.pursuePayphoneTarget(opening, terrain, 50);
    playback.pursuePayphoneTarget(opening, terrain, 50);
    require(playback.playerHealth == 0 && !playback.payphonePunchMs,
            "Lethal damage did not clamp health or stop further punches.");
    playback.playerHealth = 100;
    playback.payphonePunchMs.reset();
    playback.payphonePursuer.reset();
    terrain.walls = {{{{-100, -40, 0}, {100, -40, 0}, {100, -40, 200}}},
                     {{{-100, -40, 0}, {100, -40, 200}, {-100, -40, 200}}}};
    for (unsigned i = 0; i < 20; ++i) playback.pursuePayphoneTarget(opening, terrain, 50);
    require(playback.payphonePursuer->position[1] > -40, "Pursuit crossed a solid wall.");
    require(!playback.payphonePunchMs, "NPC punched through a wall.");
    CityPlayback fighter;
    fighter.player = CityPlayerPose{{0, 0, 0}, 0};
    fighter.payphonePursuer = CityPlayerPose{{0, 80, 0}, 0};
    auto& player = opening.actors.emplace_back();
    player.name = "Player";
    player.handPunches = opening.actors.front().handPunches;
    fighter.payphoneOpponentHealth = 20;
    fighter.punchPayphoneOpponent(opening, terrain, 0, true, true);
    require(fighter.payphoneOpponentHealth == 20, "Player punch landed before its impact time.");
    fighter.punchPayphoneOpponent(opening, terrain, 50, false, true);
    fighter.punchPayphoneOpponent(opening, terrain, 50, false, true);
    require(fighter.payphoneOpponentHealth == 8, "Player punch did not apply exactly one hit.");
    fighter.punchPayphoneOpponent(opening, terrain, 0, true, true);
    fighter.punchPayphoneOpponent(opening, terrain, 50, false, false);
    fighter.punchPayphoneOpponent(opening, terrain, 50, false, false);
    require(fighter.payphoneOpponentHealth == 8, "Unlocked player punch damaged the opponent.");
    fighter.punchPayphoneOpponent(opening, terrain, 50, false, false);
    fighter.punchPayphoneOpponent(opening, terrain, 50, false, false);
    fighter.punchPayphoneOpponent(opening, terrain, 0, true, true);
    fighter.punchPayphoneOpponent(opening, terrain, 50, false, true);
    require(fighter.payphoneOpponentHealth == 0 && fighter.payphoneDefeatMs == 0,
            "Lethal punch did not start defeat at the impact time.");
    fighter.payphoneAttack = CityActorAttack{"Loco_00", "Player"};
    const auto defeatedPosition = fighter.payphonePursuer->position;
    fighter.pursuePayphoneTarget(opening, terrain, 50);
    require(fighter.payphoneDefeatMs == 50 && fighter.payphonePursuer->position == defeatedPosition &&
            !fighter.payphonePunchMs, "Defeated NPC did not stay in place while its animation advanced.");
}

void verifyDoorSounds()
{
    CityOpening world;
    world.checkpoint.emplace();
    world.vehicleActor = 0;
    world.followCameraMs = 0;
    const CityCamera initial{{0, 450, 330}, {0, 0, 150}, 2000, 60, 1};
    world.cameras.push_back({initial, initial, 0, 0});
    CityActor car{};
    for (auto& side : car.doorClips)
        side = {{"car_open_door", 500, {}}, {"car_close_door", 750, {}}, {"car_exit_car", 1000, {}}};
    world.actors.push_back(std::move(car));
    CityActor player{};
    player.name = "Player";
    player.scale = {1, 1, 1};
    world.actors.push_back(std::move(player));
    const auto terrain = flatTerrain();
    CityPlayback playback;
    playback.player = CityPlayerPose{{100, 0, 0}, 0};
    playback.timeAfterCheckpointMs = 0;
    const auto check = [&](double delta, bool interact, bool expectOpening, bool expectClosed) {
        playback.elapsedMs += delta;
        const auto sounds = playback.drive(world, terrain, {}, delta, interact);
        require(sounds.opening == expectOpening && sounds.closed == expectClosed,
                "Unexpected vehicle door sound event.");
    };
    const auto verifyHandoff = [&](double duration) {
        require(playback.cameraTransition && playback.cameraTransition->durationMs == duration,
                "Vehicle interaction used the wrong camera transition duration.");
        const auto& transition = *playback.cameraTransition;
        const auto midpoint = transition.startMs + duration / 2;
        const auto end = transition.startMs + duration;
        require(transition.active(transition.startMs) && !transition.active(end),
                "Camera handoff did not respect its time interval.");
        const auto pose = transition.sample(midpoint);
        for (size_t axis = 0; axis < 3; ++axis) {
            require(std::abs(2 * pose.eye[axis] - transition.from.eye[axis] - transition.to.eye[axis]) < 0.001f &&
                    std::abs(2 * pose.target[axis] - transition.from.target[axis] - transition.to.target[axis]) < 0.001f,
                    "Camera handoff did not linearly interpolate the eye and target.");
        }
        const auto matrix = cityCameraMatrix(world, 1.5f, midpoint, &*playback.player, &*playback.vehicle,
                                              playback.cameraTransition);
        const auto project = [&](size_t row, const std::array<float, 3>& point) {
            return matrix[row][0] * point[0] + matrix[row][1] * point[1] +
                   matrix[row][2] * point[2] + matrix[row][3];
        };
        require(std::abs(project(3, pose.eye)) < 0.001f &&
                std::abs(project(0, pose.target)) < 0.001f && std::abs(project(1, pose.target)) < 0.001f,
                "Projection did not use the blended camera.");
    };
    check(0, true, true, false);
    verifyHandoff(100);
    check(500, false, false, false);
    check(749, false, false, false);
    check(1, false, false, true);
    check(1, false, false, false);
    check(0, true, true, false);
    verifyHandoff(300);
    check(999, false, false, false);
    check(1, false, false, true);
    check(1, false, false, false);
    check(0, true, true, false);
    check(1250, false, false, true);
    require(playback.vehicle->occupied, "Scripted exit fixture did not enter the vehicle.");
    auto& route = world.payphone.emplace();
    route.departure = {0, 1000, 0};
    route.departureRadius = 500;
    route.destination.position = playback.vehicle->position;
    route.destination.radius = 200;
    playback.vehicle->speed = 500;
    playback.advancePayphone(world, 0);
    require(playback.payphoneReached && playback.scriptedVehicleExit,
            "Payphone checkpoint did not request the scripted vehicle exit.");
    check(0, false, true, false);
    require(!playback.vehicle->occupied && !playback.scriptedVehicleExit,
            "Scripted exit did not dismount the player or clear the request.");
    verifyHandoff(300);
}

void verifyScriptedCameraHandoff()
{
    CityOpening world;
    world.followCameraMs = 100;
    world.followCameraBlendMs = 400;
    const CityCamera scripted{{100, 700, 400}, {100, 0, 100}, 2000, 60, 1};
    world.cameras.push_back({scripted, scripted, 0, 0});
    CityActor player{};
    player.name = "Player";
    player.scale = {1, 1, 1};
    world.actors.push_back(std::move(player));
    const auto check = [&](double time, const std::array<float, 3>& eye,
                           const std::array<float, 3>& target, const CityPlayerPose* pose = nullptr) {
        const auto matrix = cityCameraMatrix(world, 1.5f, time, pose);
        const auto project = [&](size_t row, const std::array<float, 3>& point) {
            return matrix[row][0] * point[0] + matrix[row][1] * point[1] +
                   matrix[row][2] * point[2] + matrix[row][3];
        };
        require(std::abs(project(3, eye)) < 0.001f &&
                std::abs(project(0, target)) < 0.001f && std::abs(project(1, target)) < 0.001f,
                "CamDefault did not project from the expected eye and target.");
    };
    check(99, scripted.eye, scripted.target);
    check(100, scripted.eye, scripted.target);
    check(300, {50, 575, 275}, {50, 0, 50});
    check(500, {0, 450, 150}, {0, 0, 0});
    CityPlayerPose moved{{200, 50, 30}, 0};
    moved.followCameraYaw = 90.f;
    check(501, {-250, 50, 180}, moved.position, &moved);
    CityPlayback playback;
    playback.player = moved;
    world.walkSpeed = 200;
    playback.walk(world, flatTerrain(), {0, 1}, 50);
    require(std::abs(playback.player->position[0] - 210) < 0.001f &&
            std::abs(playback.player->position[1] - 50) < 0.001f,
            "Forward walking did not follow the restored camera heading.");

    playback.elapsedMs = 300;
    playback.orbitCamera(world, {80, 0}, 100);
    require(playback.player->followCameraYaw == 90.f,
            "Camera drag changed heading during a scripted handoff.");
    playback.elapsedMs = 500;
    playback.orbitCamera(world, {80, 0}, 100);
    require(playback.player->followCameraYaw == 92.f,
            "On-foot camera did not apply double horizontal sensitivity.");
    playback.vehicle = CityVehiclePose{{100, 200, 10}, 0};
    auto& car = *playback.vehicle;
    car.occupied = true;
    car.updateCamera(CityVehicleCamera{820, 290, 250, 0, 600}, 0);
    playback.orbitCamera(world, {80, 0}, 100);
    require(std::abs(car.camera->eye[0] - 85.6888f) < 0.001f &&
            std::abs(car.camera->eye[1] - 1019.8751f) < 0.001f && car.camera->eye[2] == 300,
            "Vehicle camera did not orbit at the retail sensitivity and radius.");
    playback.orbitCamera(world, {0, 160}, 100);
    const auto tiltedHeight = car.camera->eyeHeight;
    require(tiltedHeight > 330 && tiltedHeight < 335,
            "Vertical drag did not tilt the vehicle camera.");
    car.updateCamera(CityVehicleCamera{820, 290, 250, 0, 600}, 16);
    require(std::abs(car.camera->eye[2] - car.position[2] - tiltedHeight) < 0.001f,
            "The next vehicle camera update erased pitch.");
    world.vehicleActor = world.actors.size();
    CityActor carActor{};
    carActor.vehicleCamera = CityVehicleCamera{820, 290, 250, 0, 600};
    world.actors.push_back(std::move(carActor));
    playback.releaseCameraTap(world, 1000);
    require(!playback.cameraTransition, "Single tap reset the camera.");
    playback.releaseCameraTap(world, 1500);
    require(!playback.cameraTransition, "A 500 ms gap counted as a double tap.");
    car.speed = 10;
    playback.releaseCameraTap(world, 1750);
    require(!playback.cameraTransition, "Double tap reset a moving vehicle camera.");
    car.speed = 0;
    const auto tiltedEye = car.camera->eye;
    playback.releaseCameraTap(world, 1999);
    require(playback.cameraTransition && playback.cameraTransition->durationMs == 300 &&
            playback.cameraTransition->from.eye == tiltedEye &&
            playback.cameraTransition->to.eye == std::array<float, 3>{100, 1020, 300},
            "Stationary double tap did not blend back to the default camera.");
    playback.elapsedMs += 300;
    car.occupied = false;
    playback.orbitCamera(world, {0, 160}, 100);
    const auto walkingHeight = playback.player->followCameraHeight;
    require(walkingHeight > 170 && walkingHeight < 175,
            "Vertical drag did not tilt the on-foot camera.");
    playback.orbitCamera(world, {0, 320}, 1000);
    require(playback.player->followCameraHeight == walkingHeight,
            "On-foot camera accepted a step past its pitch limit.");
}

void verifyExplosionCameraShake()
{
    CityCameraShake shake;
    shake.update(100, true, true);
    require(shake.remainingMs == 1000 && shake.offset == std::array<float, 3>{},
            "Camera transition did not suspend the explosion shake.");
    shake.update(500, false, false);
    require(shake.remainingMs == 500 && shake.offset == std::array<float, 3>{9, 43.5f, 45.5f},
            "Explosion shake lost its retail decay or displacement scaling.");

    CityOpening world;
    world.followCameraMs = 0;
    const CityCamera initial{{0, 450, 330}, {0, 0, 150}, 2000, 60, 1};
    world.cameras.push_back({initial, initial, 0, 0});
    CityActor player{};
    player.name = "Player";
    player.scale = {1, 1, 1};
    world.actors.push_back(std::move(player));
    const auto stable = cityCameraMatrix(world, 1.5f, 0);
    const auto displaced = cityCameraMatrix(world, 1.5f, 0, nullptr, nullptr, {}, shake.offset);
    for (size_t row = 0; row < 4; ++row) {
        float change = 0;
        for (size_t axis = 0; axis < 3; ++axis) {
            require(std::abs(stable[row][axis] - displaced[row][axis]) < 0.00001f,
                    "Explosion shake changed the camera orientation.");
            change += stable[row][axis] * shake.offset[axis];
        }
        require(std::abs(stable[row][3] - displaced[row][3] - change) < 0.001f,
                "Explosion shake did not move the projected camera.");
    }
    shake.update(500, false, false);
    require(shake.remainingMs == 0 && shake.offset == std::array<float, 3>{},
            "Explosion shake did not expire after one second.");
}

void verifyStayWithin()
{
    RetailStayWithin condition(375, 12367);
    const std::array<float, 3> target{10, 20, 30};
    require(condition.evaluate({325, 20, 30}, target, 1000) && !condition.warningText() &&
            condition.remainingMs() == 10000, "StayWithin lost the retail rounded distance threshold.");
    require(condition.evaluate({10, 20, 347}, target, 9000) && condition.warningText() == 12367 &&
            condition.remainingMs() == 1000, "StayWithin did not count vertical distance or show its warning.");
    require(condition.evaluate(target, target, 2000) && !condition.warningText() &&
            condition.remainingMs() == 10000, "StayWithin did not reset when the player returned.");
    require(!condition.evaluate({10, 20, 347}, target, 10000) && condition.remainingMs() == 0,
            "StayWithin did not fail at zero.");
}

void verifyMissionConditions()
{
    RetailBeQuick timer(1500);
    auto state = timer.evaluate(700, false);
    require(state.passes && state.timerMs == 1500, "Mission timer did not report the value before the update.");
    state = timer.evaluate(12000, true);
    require(state.passes && !state.timerMs, "Mission timer did not pause and hide during a cinematic.");
    state = timer.evaluate(800, false);
    require(state.passes && state.timerMs == 800 && !timer.evaluate(0, false).passes,
            "Mission timer failed at the wrong update.");

    RetailPhotographQuickly photograph(300);
    require(photograph.evaluate(300, false).timerMs == 300, "Photo timer was not displayed.");
    state = photograph.evaluate(0, true);
    require(state.passes && !state.timerMs && state.clearPhotoTargets,
            "Photo condition did not accept a photograph before checking expiry.");
    state = photograph.evaluate(5000, false);
    require(state.passes && !state.timerMs && !state.clearPhotoTargets,
            "Photo condition did not latch success.");
    RetailPhotographQuickly missed(0);
    require(!missed.evaluate(0, false).passes, "Photo condition did not fail without a photograph.");

    using Target = RetailProtectionTarget;
    require(retailProtectionPasses(Target::character, false, false) &&
            !retailProtectionPasses(Target::character, true, false), "NPC protection used the wrong death rule.");
    require(retailProtectionPasses(Target::vehicle, true, false) &&
            !retailProtectionPasses(Target::vehicle, true, true), "Vehicle protection used the wrong explosion rule.");
    require(formatRetailMissionTimer(62199) == "01:02:1" && formatRetailMissionTimer(0) == "00:00:0",
            "Mission timer lost its retail format.");
}

void verifyVehicleCamera()
{
    const CityVehicleCamera offsets{820, 290, 250, 0, 600};
    CityVehiclePose car{{0, 0, 0}, 0};
    car.occupied = true;
    car.updateCamera(offsets, 0);
    require(car.camera && car.camera->eye == std::array<float, 3>{0, 820, 290},
            "Vehicle camera did not initialize behind the car.");

    car.yaw = 15;
    car.speed = 1000;
    car.updateCamera(offsets, 100);
    require(std::abs(car.camera->setback - 817.84f) < 0.001f && car.camera->eye[0] < 0,
            "Forward acceleration did not shorten and steer the trailing camera.");
    require(car.camera->eye[0] > -150,
            "Vehicle camera snapped to the new heading.");

    car.yaw = 170;
    car.speed = -1800;
    const float oldX = car.camera->eye[0];
    car.updateCamera(offsets, 100);
    require(car.camera->setback == offsets.setback && car.camera->eye[0] > oldX,
            "Reverse acceleration did not restore the setback and rotate the camera.");

    car.yaw = 15;
    car.speed = 8000;
    car.camera->setback = 601;
    car.updateCamera(offsets, 100);
    require(car.camera->setback == offsets.minimumSetback,
            "Vehicle camera crossed the minimum setback.");

    car.occupied = false;
    car.updateCamera(offsets, 0);
    require(!car.camera, "Vehicle camera state survived leaving the car.");
}

void verifyMissionConditionOrder()
{
    RetailMissionConditions conditions;
    require(conditions.add(RetailProtectionTarget::vehicle, "car", 81), "Could not register vehicle protection.");
    require(conditions.add(RetailProtectionTarget::character, "contact", 82), "Could not register NPC protection.");
    require(conditions.add(RetailPhotographQuickly(500), "camera"), "Could not register photo condition.");
    std::vector<std::string> queried;
    const auto findTarget = [&](std::string_view name) -> std::optional<RetailMissionTargetState> {
        queried.emplace_back(name);
        return RetailMissionTargetState{{}, true, name == "car", false};
    };
    const auto failed = conditions.update(16, false, {}, findTarget);
    require(failed.failureCause == 81 && queried == std::vector<std::string>{"car"},
            "Mission conditions did not stop at the first failure.");
    const auto cleared = conditions.update(16, false, {}, findTarget);
    require(!cleared.failureCause && queried.size() == 1, "Mission failure did not clear active conditions.");

    for (size_t i = 0; i < 8; ++i)
        require(conditions.add(RetailBeQuick(1000)), "Mission rejected a condition below capacity.");
    require(!conditions.add(RetailBeQuick(1000)), "Mission accepted more than eight conditions.");
    conditions.clear();
    require(conditions.add(RetailStayWithin(300, 12367), "marker"), "Could not register StayWithin.");
    const auto warning = conditions.update(1000, false, {600, 0, 0}, findTarget);
    require(!warning.failureCause && warning.timerMs == 9000 && warning.warningText == 12367 &&
            queried.back() == "marker", "Mission did not return the StayWithin timer and warning.");
    const auto timedOut = conditions.update(9000, false, {600, 0, 0}, findTarget);
    require(timedOut.failureCause == -1 && !timedOut.timerMs && !timedOut.warningText,
            "Mission timeout did not return the default failure cause and clear its HUD values.");
}
}

static void checkSpawnRules()
{
    const CitySpawnGroup car{3, {7, 9, 38}}, civilian{5, {7, 9, 48}};
    const CitySpawnGroup parked{11, {7, 9}}, prisoner{16, {7, 9}};
    CitySpawnTimer timer(car, 0);
    std::vector<int> calls;
    unsigned draw = 0;
    const auto random = [&]() {
        calls.push_back(-1);
        return draw++;
    };
    const auto spawn = [&](uint8_t metatype) { calls.push_back(metatype); };
    timer.advance(500, car, true, 0, 0, false, random, spawn);
    require(timer.remainingMs() == 0 && calls.empty(), "Spawn ran on the frame the timer expired.");
    timer.advance(16, car, true, 0, 0, false, random, spawn);
    require(calls == std::vector<int>{-1, 7, -1} && timer.remainingMs() == 2001,
            "Spawn attempt and respawn delay used the wrong random order.");
    calls.clear();
    timer.advance(3000, car, false, 0, 0, false, random, spawn);
    timer.advance(16, car, false, 0, 0, false, random, spawn);
    require(calls == std::vector<int>{-1} && timer.remainingMs() == 502,
            "Blocked spawn did not use the retry delay.");

    require(car.chooseForSpawn(1, 0, 1, false) == 38 &&
            civilian.chooseForSpawn(1, 0, 1, false) == 48 &&
            civilian.chooseForSpawn(1, 0, 1, true) == 161 &&
            parked.chooseForSpawn(1, 0, 1, false) == 9 &&
            prisoner.chooseForSpawn(1, 0, 1, false) == 9,
            "Wanted-level spawn substitution failed.");
    require(!car.chooseForSpawn(2, 1, 1, false) && !civilian.chooseForSpawn(2, 0, 0, true) &&
            car.chooseForSpawn(0, 0, 1, false) == 7,
            "Police cap or even-draw spawn selection changed.");
    require(car.spawnDelayMs(false, 500) == 500 && parked.spawnDelayMs(true, 1999) == 3999 &&
            civilian.spawnDelayMs(false, 999) == 1999 && prisoner.spawnDelayMs(true, 3000) == 5000,
            "Retail spawn delay bounds or modulo changed.");

    const std::array<float, 3> eye{10, 20, 30}, target{13, 24, 30};
    const auto moving = car.spawnCenters(eye, target, 55, 100.f);
    const auto walking = car.spawnCenters(eye, target, 55, 99.f);
    const auto parkedCenters = parked.spawnCenters(eye, target, 55, 100.f);
    const auto civilianCenters = civilian.spawnCenters(eye, target, 55, 100.f);
    require(std::abs(moving[0][0] - 2433.4f) < 0.01f && std::abs(moving[1][1] - 3940.f) < 0.01f &&
            std::abs(walking[0][0] - 1833.4f) < 0.01f && moving[0][2] == 55 && moving[1][2] == 55,
            "Retail moving-car spawn centers or speed threshold changed.");
    require(parkedCenters[0][0] == 1510.f && parkedCenters[1][1] == 3220.f &&
            civilianCenters[0][0] == 610.f && civilianCenters[1][1] == 1380.f &&
            civilian.spawnCenters(eye, eye, 55, std::nullopt)[0] == std::array<float, 3>{10, 20, 55},
            "Retail parked-car or civilian spawn centers changed.");

    CitySpawnPoint point{};
    CitySpawnContext context{{2500, 0, 0}, {3200, 0, 0}, {2, 2, 1, 0}};
    require(parked.canSpawn(point, context) && !car.canSpawn(point, context),
            "Spawn radius boundaries or moving vehicle gate changed.");
    context.traffic = CitySpawnTraffic{0, {0, -3000, 0}, {0, -1, 0}, 0};
    require(car.canSpawn(point, context), "Eligible moving vehicle was rejected.");
    context.traffic->actorSpeed = 30;
    require(!car.canSpawn(point, context), "A spawn separating from the faster actor was accepted.");
    context.traffic->roadFlags = 0x80;
    require(car.canSpawn(point, context), "The road speed bits did not affect the traffic check.");
    point.rotation[2] = 180;
    require(!car.canSpawn(point, context), "The spawn point's heading was ignored.");
    point.rotation[2] = 0;
    context.activeCounts[2] = 2;
    require(!parked.canSpawn(point, context) && !car.canSpawn(point, context),
            "Spawn class and global population caps were not enforced.");
    context.activeCounts[2] = 1;
    context.nearbyObjects = {
        CitySpawnNeighbor{{1, 0, 0}, 6, 1},
        CitySpawnNeighbor{{1, 0, 0}, 3, 0},
        CitySpawnNeighbor{{1000, 0, 0}, 5, 1}
    };
    require(parked.canSpawn(point, context) && car.canSpawn(point, context),
            "Checkpoint, inactive object, or radius boundary blocked spawning.");
    context.nearbyObjects.back().position = {999, 0, 5000};
    require(!parked.canSpawn(point, context) && !car.canSpawn(point, context),
            "Active object in the horizontal radius did not block vehicle spawning.");
    context.outerCenter = {3000, 0, 0};
    context.innerCenter = {2000, 0, 299};
    require(civilian.canSpawn(point, context), "Civilian spawn radius or clearance changed.");
    context.innerCenter[2] = 300;
    require(!civilian.canSpawn(point, context), "Spawn height cutoff is exclusive.");
}

static void checkNavigationWaypoint()
{
    CityMesh mesh;
    mesh.positions = {{0, 0, 0}, {4, 2, 6}, {0, 2, 0}, {5, 5, 0}, {7, 7, 0}};
    mesh.buffers = {{"", {0, 1, 2}}, {"", {1, 0, 3, 2, 3, 4}}};
    using Navigation = CityNavigation;
    require(Navigation::edgeWaypoint(mesh, {0, 0}, {1, 0}, {10, 20, 30}) ==
                std::array<float, 3>{12, 21, 36},
            "Retail shared-edge midpoint, origin, or clearance changed.");
    require(!Navigation::edgeWaypoint(mesh, {0, 0}, {1, 3}, {}) &&
            !Navigation::edgeWaypoint(mesh, {0, 1}, {1, 0}, {}),
            "An invalid or single-vertex transition produced a waypoint.");
}

static void checkNavigationRestrictions()
{
    CityMesh mesh;
    mesh.positions = {{0, 0, 0}, {4, 2, 6}, {0, 2, 0}, {5, 5, 0}};
    mesh.buffers = {{"", {0, 1, 2}}, {"", {1, 0, 3}}};
    CityNavigation navigation;
    navigation.buffers.resize(2);
    for (auto& buffer : navigation.buffers) {
        for (auto& lane : buffer.neighbors) lane = {UINT16_MAX};
        buffer.roadFlags = {0};
    }
    navigation.buffers[0].neighbors[0][0] = 0;
    navigation.buffers[0].neighbors[3][0] = 1;
    using Mode = CityNavigation::RouteMode;
    using Material = CityNavigation::Material;
    navigation.materialBuffers.fill(UINT32_MAX);
    navigation.materialBuffers[static_cast<size_t>(Material::road)] = 1;
    require(navigation.findRoute(mesh, {0, 0}, {1, 0}, Mode::vehicle).size() == 2 &&
            navigation.findRoute(mesh, {0, 0}, {1, 0}, Mode::pedestrian).empty(),
            "Navigation ignored retail material restrictions.");
    for (const auto flags : {1, 4}) {
        navigation.buffers[0].roadFlags[0] = static_cast<uint8_t>(flags);
        require(navigation.findRoute(mesh, {0, 0}, {1, 0}, Mode::vehicle).empty() &&
                navigation.findRoute(mesh, {0, 0}, {1, 0}, Mode::unrestricted).size() == 2,
                "Navigation ignored a retail wrong-way edge code.");
    }
}

static void checkNavigationScoring()
{
    CityMesh mesh;
    mesh.positions = {{0, 0, 0}, {10, 0, 0}, {5, 0, 0}};
    mesh.buffers = {{"", {0, 0, 0, 1, 1, 1, 2, 2, 2}}};
    CityNavigation navigation;
    auto& buffer = navigation.buffers.emplace_back();
    for (auto& lane : buffer.neighbors) lane.assign(3, UINT16_MAX);
    buffer.roadFlags.assign(3, 0);
    buffer.neighbors[0] = {3, UINT16_MAX, 3};
    buffer.neighbors[1][0] = 6;
    buffer.neighbors[3] = {0, 0, 0};
    buffer.neighbors[4][0] = 0;
    using Mode = CityNavigation::RouteMode;
    const auto direct = navigation.findRoute(mesh, {0, 0}, {0, 3});
    const auto retail = navigation.findRoute(mesh, {0, 0}, {0, 3}, Mode::vehicle,
                                             std::array<float, 3>{10, 0, 0});
    require(direct.size() == 2 && retail.size() == 3 && retail[1].indexOffset == 6,
            "Retail squared route scoring did not prefer two shorter steps.");

    mesh.positions = {{0, 0, 0}, {10, 0, 0}, {5, 0.25f, 0}, {5, -0.25f, 0}};
    mesh.buffers[0].indices.insert(mesh.buffers[0].indices.end(), {3, 3, 3});
    for (auto& lane : buffer.neighbors) lane.resize(4, UINT16_MAX);
    buffer.roadFlags.resize(4);
    buffer.neighbors[0] = {9, UINT16_MAX, 3, 3};
    buffer.neighbors[1][0] = 6;
    buffer.neighbors[3] = {0, 0, 0, 0};
    const auto tied = navigation.findRoute(mesh, {0, 0}, {0, 3}, Mode::vehicle,
                                           std::array<float, 3>{10, 0.1f, 0});
    require(tied.size() == 3 && tied[1].indexOffset == 9,
            "Retail frontier did not truncate the first candidate's fractional score.");
}

static void checkStackedNavigationGround()
{
    CityMesh mesh;
    mesh.positions = {{0, 0, 0}, {10, 0, 0}, {0, 10, 0},
                      {0, 0, 40}, {10, 0, 40}, {0, 10, 40}};
    mesh.buffers = {{"", {0, 1, 2}}, {"", {3, 4, 5}}};
    CityNavigation navigation;
    navigation.buffers.resize(2);
    navigation.buffers[0].overlaps = {{{1, 0}}};
    navigation.buffers[1].overlaps = {{{0, 0}}};
    const auto upper = navigation.sampleGround(mesh, {0, 0}, {2, 2, 38}, 4, 50);
    const auto lower = navigation.sampleGround(mesh, {1, 0}, {2, 2, 2}, 4, 50);
    require(upper && upper->height == 40 && upper->triangle.buffer == 1 &&
            lower && lower->height == 0 && lower->triangle.buffer == 0,
            "Stacked navigation ground picked the wrong authored layer.");
    require(!navigation.sampleGround(mesh, {0, 0}, {2, 2, 20}, 10, 10) &&
            !navigation.sampleGround(mesh, {0, 0}, {8, 8, 1}, 50, 50),
            "Navigation ground accepted a height or position outside the query.");
}

static void checkIndexedGround()
{
    CityTerrain terrain;
    terrain.ground = {
        CityTriangle{{{-600, -600, 0}, {600, -600, 0}, {-600, 600, 0}}},
        CityTriangle{{{-600, -600, 40}, {600, -600, 40}, {-600, 600, 40}}}
    };
    const auto reference = terrain;
    terrain.indexGround();
    const auto preferred = terrain.groundContact({-550, -550, 20}, 1);
    require(preferred && preferred->triangle == 1 && preferred->height == 40,
            "The active triangle did not win an equal-height-distance tie.");
    for (const float x : {-601.f, -600.f, -512.f, -0.01f, 0.f, 512.f, 600.f, 601.f}) {
        for (const float y : {-601.f, -600.f, -512.f, -0.01f, 0.f, 512.f, 600.f, 601.f}) {
            for (const float z : {1.f, 20.f, 39.f})
                require(terrain.groundHeight({x, y, z}) == reference.groundHeight({x, y, z}),
                        "Indexed ground changed a height, tie, or cell boundary.");
        }
    }
}

static void checkGroundLinks()
{
    CityTerrain terrain;
    terrain.ground = {
        CityTriangle{{{50, 50, 0}, {60, 50, 0}, {50, 60, 0}}},
        CityTriangle{{{0, 0, 0}, {10, 0, 0}, {0, 10, 0}}},
        CityTriangle{{{0, 0, 40}, {10, 0, 40}, {0, 10, 40}}}
    };
    terrain.groundNavigation = {{0, 0}, {0, 3}, {0, 6}};
    CityNavigation navigation;
    auto& buffer = navigation.buffers.emplace_back();
    buffer.roadFlags.resize(3);
    buffer.overlaps = {{{0, 6}}, {}, {}};
    for (auto& lane : buffer.neighbors) lane.assign(3, UINT16_MAX);
    terrain.indexGround(&navigation);
    const auto overlap = terrain.groundContact({2, 2, 20}, 0);
    const auto closest = terrain.groundContact({2, 2, 1}, 0);
    require(overlap && overlap->triangle == 2 && closest && closest->triangle == 1,
            "Authored overlaps did not break ties or overrode a closer ground contact.");
    buffer.overlaps.front().clear();
    buffer.neighbors[0][0] = 6;
    buffer.neighbors[3][0] = 0;
    terrain.indexGround(&navigation);
    const auto edge = terrain.groundContact({2, 2, 20}, 0);
    require(edge && edge->triangle == 2, "Authored edge neighbor did not break a ground contact tie.");
}

static void checkIndexedWalls()
{
    CityTerrain terrain;
    terrain.walls = {
        CityTriangle{{{-600, 0, 0}, {600, 0, 0}, {600, 0, 100}}},
        CityTriangle{{{-10, 10, 0}, {10, 10, 0}, {10, 10, 100}}},
        CityTriangle{{{2000, 2000, 0}, {2200, 2000, 0}, {2200, 2000, 100}}}
    };
    terrain.indexWalls();
    require(terrain.wallCandidates(-600, -20, 600, 20) == std::vector<size_t>{0, 1},
            "Wall grid omitted, duplicated, or reordered candidates.");
    require(terrain.wallCandidates(-1600, -1600, -1400, -1400) == std::vector<size_t>{} &&
            !terrain.wallCandidates(-1000000, -1000000, 1000000, 1000000),
            "Wall grid did not distinguish empty cells from a linear fallback.");
}

static void checkStaticPlane()
{
    CityAssets city;
    auto& scene = city.districts.emplace_back().scene;
    CityMesh mesh;
    mesh.name = "wall";
    mesh.positions = {{0, 0, 0}, {0, 100, 0}, {0, 100, 200}, {0, 0, 200}};
    mesh.buffers = {{"", {0, 1, 2, 0, 2, 3}}};
    scene.meshes.push_back(mesh);
    CityNode visual{};
    visual.parent = -1;
    visual.rotation = {0, 0, 0, 1};
    visual.scale = {1, 1, 1};
    visual.geometry = {{"", "wall", {}}};
    scene.nodes.push_back(visual);
    auto proxy = visual;
    proxy.parent = 0;
    proxy.name = "Plane01-node";
    proxy.translation = {100, 200, 0};
    scene.nodes.push_back(proxy);
    const auto draw = prepareCityView(city, CityOpening{});
    require(draw.districtSurfaceCount == 1 && draw.terrain.walls.size() == 2 &&
            draw.terrain.walls.front()[1] == std::array<float, 3>{100, 300, 0},
            "A static Plane proxy was drawn or lost its world-space collision quad.");

    auto model = scene;
    city.geometryLibraries.emplace("building", std::move(model));
    visual.translation = {400, 300, 0};
    visual.geometry = {{"building", "wall", {}}, {"building", "wall", {}}};
    scene.nodes = {visual};
    const auto instance = prepareCityView(city, CityOpening{});
    require(instance.districtSurfaceCount == 2 && instance.terrain.walls.size() == 2 &&
            instance.terrain.walls.front()[1] == std::array<float, 3>{500, 600, 0},
            "External model Plane was duplicated or lost its composed placement.");
    city.geometryLibraries.at("building").meshes.front().positions =
        {{0, 0, 0}, {100, 0, 0}, {100, 100, 0}, {0, 100, 0}};
    const auto planar = prepareCityView(city, CityOpening{});
    require(planar.terrain.walls.size() == 8 &&
            planar.terrain.walls.front()[0] == std::array<float, 3>{500, 500, -100},
            "Horizontal Plane did not create exactly four perimeter walls.");
}

static void checkJumpRamp()
{
    CityOpening world;
    world.checkpoint.emplace();
    world.vehicleActor = 0;
    CityActor car{};
    car.position = {0, 40, 0};
    world.actors.push_back(car);
    auto terrain = flatTerrain();
    const CityTriangle west{{{-200, 0, 0}, {200, 0, 0}, {200, -200, 100}}};
    const CityTriangle east{{{-200, 0, 0}, {200, -200, 100}, {-200, -200, 100}}};
    terrain.jumpRamps.push_back({west, east});
    CityPlayback driver;
    driver.player = CityPlayerPose{{0, 140, 0}, 0};
    driver.timeAfterCheckpointMs = 0;
    driver.drive(world, terrain, {}, 20, true);
    require(driver.vehicle && driver.vehicle->occupied, "Could not initialize the ramp fixture vehicle.");
    bool climbed = false;
    bool launched = false;
    for (int i = 0; i < 150; ++i) {
        driver.drive(world, terrain, {0, 1}, 20, false);
        const auto& pose = *driver.vehicle;
        if (pose.onRamp) require(driver.visitedJumpRamps == 1, "Ramp contact did not set its achievement bit.");
        climbed |= pose.onRamp && pose.position[2] >= 50;
        launched |= pose.airborne && pose.position[2] >= 100;
        if (pose.airborne)
            require(pose.jumpDistanceCm > 0 && driver.longestJumpMeters == 0,
                    "Jump distance was missing or recorded before landing.");
        if (launched && !pose.airborne) break;
    }
    const auto& landed = *driver.vehicle;
    require(climbed && launched && !landed.airborne && !landed.onRamp && landed.verticalSpeed == 0 &&
            landed.position[2] == 0 && landed.position[1] < -200,
            "Vehicle did not climb, leave, and land beyond the ramp.");
    require(landed.jumpDistanceCm == 0 && driver.longestJumpMeters > 0,
            "Landing did not record and reset the accumulated jump distance.");
}

static void checkDistantProjection()
{
    CityOpening world;
    const CityCamera shot{{0, 0, 0}, {0, 1, 0}, 8000, 60, 1};
    world.cameras.push_back({shot, shot, 0, 0});
    std::optional<CityCameraMatrix> distant;
    const auto main = cityCameraMatrix(world, 1.5f, 0, nullptr, nullptr, {}, {}, nullptr, nullptr, &distant);
    require(distant && (*distant)[0] == main[0] && (*distant)[1] == main[1] && (*distant)[3] == main[3],
            "Distant projection changed the camera pose or field of view.");
    const auto project = [&](float y) { return ((*distant)[2][1] * y + (*distant)[2][3]) / y; };
    require(project(7999) < 0 && std::abs(project(8000)) < 0.0001f &&
            std::abs(project(20000) - 1) < 0.0001f && project(20001) > 1,
            "Distant projection did not span the main far plane to 200 m.");
    world.cameras.front().first.farPlane = world.cameras.front().second.farPlane = 20000;
    cityCameraMatrix(world, 1.5f, 0, nullptr, nullptr, {}, {}, nullptr, nullptr, &distant);
    require(!distant, "An empty distant clip interval created a projection.");
}

static void checkBillboardPose()
{
    CityVertex vertex{{12, 20, 34}, {}, {1, 1, 1, 1}, {0, -1, 0}};
    const CityBillboard billboard{0, {10, 20, 30}, {1, 0}, {vertex}};
    CityCameraMatrix camera{};
    camera[0] = {0, 3, 0, 0};
    poseCityBillboard(billboard, camera, &vertex);
    require(vertex.position == std::array<float, 3>{10, 22, 34} &&
            vertex.normal == std::array<float, 3>{1, 0, 0},
            "Billboard did not rotate toward the horizontal camera basis.");
}

static void checkLodPlacement()
{
    CityMesh nearMesh;
    nearMesh.name = "near";
    nearMesh.positions = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    nearMesh.buffers = {{"", {0, 1, 2}}};
    auto farMesh = nearMesh;
    farMesh.name = "far";
    CityNode first{};
    first.parent = -1;
    first.translation = {10, 20, 30};
    first.rotation = {0, 0, 0, 1};
    first.scale = {2, 3, 4};
    first.geometry = {{"", "near", {}}};
    first.userProperties = "lod=0;";
    auto second = first;
    second.geometry = {{"", "far", {}}};
    second.userProperties = "lod=10; lodmax=50;";
    CityScene model;
    model.meshes = {nearMesh, farMesh};
    model.nodes = {first, second};

    auto placed = first;
    placed.translation = {100, 200, 300};
    placed.geometry = {{"model", "near", {}}};
    placed.userProperties = "lodmax=20; billboard=1;";
    CityAssets city;
    city.districts.emplace_back().scene.nodes = {placed};
    city.geometryLibraries.emplace("model", std::move(model));
    city.usableLodModels.insert("model");
    const auto draw = prepareCityView(city, CityOpening{});
    require(draw.surfaces.size() == 2 && draw.surfaces[1].lodAlternative &&
            draw.vertices[0].position == draw.vertices[3].position &&
            draw.vertices[1].position == draw.vertices[4].position,
            "LOD switch applied the base model transform twice.");
    const auto& bounds = draw.surfaces[1].lodLimit;
    require(bounds && bounds->visible({2100, 200, 300}) && !bounds->visible({2101, 200, 300}),
            "Instance LOD maximum was lost on the alternate.");
    require(draw.billboards.size() == 2 && draw.billboards[0].origin == draw.billboards[1].origin,
            "LOD alternate lost the placed billboard pivot.");
    auto vertices = draw.vertices;
    CityCameraMatrix camera{};
    camera[0] = {0, 1, 0, 0};
    for (const auto& billboard : draw.billboards)
        poseCityBillboard(billboard, camera, vertices.data());
    require(vertices[1].position == std::array<float, 3>{100, 204, 300} &&
            vertices[4].position == vertices[1].position &&
            draw.surfaces[1].bounds && draw.surfaces[1].bounds->maximum[1] >= vertices[4].position[1],
            "LOD alternate lost billboard rotation or its conservative bounds.");
}

static void checkNodeProperties()
{
    CityNode node{};
    node.userProperties = " lod=0 lodmax = 70;\r\nbillboard=1; label=wood bench; no-value";
    const CityLodLimit limit{{100, 200, 300}, 7000.f * 7000.f};
    require(limit.visible({100, 200, 7300}) && !limit.visible({100, 200, 7301}),
            "Retail LOD maximum did not include the exact 3D cutoff.");
    CityScene model;
    model.nodes.resize(3);
    model.nodes[0].userProperties = "lod=50; lodmax=70;";
    model.nodes[1].userProperties = "lod=0;";
    model.nodes[2].userProperties = "lod=30;";
    const auto lod = readCityLodGroup(model);
    require(lod.select(0) == 1 && lod.select(3000.f * 3000) == 1 &&
            lod.select(3001.f * 3001) == 2 && lod.select(5000.f * 5000) == 2 &&
            lod.select(7000.f * 7000) == 0 && !lod.select(7001.f * 7001),
            "Retail LOD group distance selection changed.");
    require(node.property("lod") == "0" && node.property("lodmax") == "70" &&
            node.property("billboard") == "1" && node.property("label") == "wood bench" && !node.property("no-value") && !node.property("bill"),
            "BRES node property parsing ignored a key boundary.");
}

static void checkCityFrustum()
{
    const CityCameraMatrix identity{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
    const auto frustum = cityFrustum(identity);
    require(cityBoundsVisible({{-2, -2, -2}, {2, 2, 2}}, frustum),
            "Frustum rejected a box enclosing the view.");
    require(cityBoundsVisible({{1, -0.5f, 0}, {2, 0.5f, 1}}, frustum),
            "Frustum rejected a box touching the edge.");
    require(!cityBoundsVisible({{-0.5f, -0.5f, -2}, {0.5f, 0.5f, -0.1f}}, frustum) &&
            !cityBoundsVisible({{-0.5f, -0.5f, 1.1f}, {0.5f, 0.5f, 2}}, frustum) &&
            !cityBoundsVisible({{1.1f, -0.5f, 0}, {2, 0.5f, 1}}, frustum),
            "Frustum accepted a box outside the Vulkan clip planes.");
}

int wmain(int argc, wchar_t** argv)
{
    if (argc > 2) {
        std::cerr << "Usage: city_collision_test [extracted game data directory]\n";
        return 2;
    }
    try {
        checkCityFrustum();
        checkNodeProperties();
        checkBillboardPose();
        checkDistantProjection();
        checkStaticPlane();
        checkJumpRamp();
        checkLodPlacement();
        checkNavigationWaypoint();
        checkNavigationRestrictions();
        checkNavigationScoring();
        checkStackedNavigationGround();
        checkIndexedGround();
        checkGroundLinks();
        checkIndexedWalls();
        checkSpawnRules();
        walkAndSlide();
        driveAndReverse();
        simulateParticles();
        verifyScriptExecution();
        verifyScriptChapterCommands();
        verifyWaitKilled();
        verifyWaitForLock();
        verifyScriptWait();
        verifyScriptCheckpoints();
        verifyScriptEscape();
        verifyScriptAudio();
        verifyScriptConditions();
        verifyMissionCompletion();
        verifyScriptReset();
        verifyScriptTick();
        verifyCinematicCommands();
        verifyScriptCameras();
        verifyScriptFades();
        verifyScriptActors();
        verifyMissionStart();
        verifyScriptDialogueAndWeaponHud();
        verifyWaitStolen();
        verifyDoorSounds();
        verifyPayphonePursuit();
        verifyExplosionCameraShake();
        verifyScriptedCameraHandoff();
        verifyStayWithin();
        verifyMissionConditions();
        verifyMissionConditionOrder();
        verifyVehicleCamera();
        if (argc == 2) poseRetailDoor(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
