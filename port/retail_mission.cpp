#include "retail_mission.h"
#include "retail_save.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace {
uint32_t scriptWord(const CityScriptCommand& command)
{
    if (command.payload.size() != 4) throw std::runtime_error("Invalid retail script word payload.");
    uint32_t value = 0;
    for (const auto byte : command.payload) value = (value << 8) | byte;
    return value;
}

int32_t signedScriptWord(const CityScriptCommand& command)
{
    const auto bits = scriptWord(command);
    int32_t value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

bool applyCinematicCommand(const CityScriptCommand& command, const RetailMissionHooks& mission, bool skip)
{
    if (!command.payload.empty()) throw std::runtime_error("Invalid retail cinematic command payload.");
    using Phase = RetailCinematicPhase;
    const bool startup = command.opcode >= 17;
    const bool entering = command.opcode == 15 || command.opcode == 17;
    const auto ready = startup ? Phase::startup : Phase::mission;
    const auto current = mission.cinematicPhase();
    if (entering) {
        if (current == Phase::gameplay) {
            const auto transition = startup ? Phase::enteringStartup : Phase::enteringMission;
            mission.setCinematicPhase(skip ? ready : transition, skip);
        }
        return mission.cinematicPhase() == ready;
    }
    if (current == ready)
        mission.setCinematicPhase(startup ? Phase::leavingStartup : Phase::leavingMission, skip);
    return mission.cinematicPhase() == Phase::gameplay;
}
}

bool retailEscapeReached(const RetailEscapePositions& positions, float distanceCm)
{
    const float x = positions.player[0] - positions.target[0];
    const float y = positions.player[1] - positions.target[1];
    const float z = positions.player[2] - positions.target[2];
    return std::sqrt(x * x + y * y + z * z) >= distanceCm;
}

void RetailWantedState::setLevel(int32_t value)
{
    const auto next = std::clamp(value, 0, 5);
    if (next > stars) phase = Phase::start;
    else if (next == 0) phase = Phase::inactive;
    stars = next;
}

void RetailWantedState::setPursuers(unsigned count)
{
    if (count > pursuers && stars > 0) phase = Phase::start;
    pursuers = count;
}

bool RetailWantedState::update(double deltaMs)
{
    // WantedLevelManager arms the lost-contact timer on a separate tick.
    if (stars == 0 || pursuers) return false;
    switch (phase) {
    case Phase::inactive:
        break;
    case Phase::start:
        phase = Phase::waiting;
        phaseMs = 0;
        break;
    case Phase::waiting:
        if (phaseMs >= 10000) {
            phase = Phase::blinking;
            phaseMs = 0;
        } else phaseMs += std::max(0., deltaMs);
        break;
    case Phase::blinking:
        if (phaseMs >= 5000) {
            --stars;
            phase = stars ? Phase::start : Phase::inactive;
            return true;
        }
        phaseMs += std::max(0., deltaMs);
        break;
    }
    return false;
}

RetailScriptExecution::RetailScriptExecution(CityScriptProgram program)
    : program(std::move(program)), scripts(this->program.size())
{
}

void RetailScriptExecution::releaseCheckpoint(Script& script, const RetailTravelHooks* travel)
{
    if (!script.checkpoint) return;
    if (!travel || !travel->setActive) throw std::logic_error("Active script checkpoint requires a world binding.");
    travel->setActive(script.checkpoint->object, false);
    script.checkpoint.reset();
}

void RetailScriptExecution::freezeRunningScripts(uint32_t except, int32_t playerContext, const RetailTravelHooks* travel)
{
    for (uint32_t id = 0; id < scripts.size(); ++id) {
        auto& script = scripts[id];
        if (id == except || script.state != RetailScriptState::active || script.context != playerContext) continue;
        releaseCheckpoint(script, travel);
        script.state = RetailScriptState::frozen;
    }
}

void RetailScriptExecution::reset(int32_t playerContext, bool restoringSave,
                                  std::optional<uint32_t> startScript, const RetailTravelHooks* travel)
{
    if (startScript && *startScript >= scripts.size()) throw std::out_of_range("Invalid startup script index.");
    fadeRemaining = {};
    for (uint32_t id = 0; id < scripts.size(); ++id) {
        auto& script = scripts[id];
        releaseCheckpoint(script, travel);
        if (script.trackedVehicle) {
            if (!travel || !travel->trackVehicle) throw std::logic_error("Tracked vehicle requires a world binding.");
            travel->trackVehicle(*script.trackedVehicle, false);
        }
        script = {};
        const auto& commands = program[id];
        // ScriptManager::resetGame activates opcode-4 roots only on an ordinary new game.
        if (startScript == id || (!startScript && !restoringSave && !commands.empty() && commands.front().opcode == 4))
            unlock(id, -1, playerContext);
    }
}

void RetailScriptExecution::unlock(uint32_t id, int32_t context, int32_t playerContext)
{
    auto& script = scripts.at(id);
    script.context = context == -1 ? playerContext : context;
    script.state = script.context == playerContext ? RetailScriptState::active : RetailScriptState::frozen;
}

void RetailScriptExecution::unlockFrozenScripts(int32_t playerContext)
{
    for (auto& script : scripts) {
        if (script.state == RetailScriptState::frozen && script.context == playerContext)
            script.state = RetailScriptState::active;
    }
}

void RetailScriptExecution::restoreUnlocks(const std::vector<SaveBlock>& blocks, int32_t resetContext)
{
    // SaveGame applies script records to an already reset world.
    auto context = resetContext;
    for (const auto& block : blocks) {
        if (block.id == 12) context = std::get<int32_t>(block.value);
        if (block.id != 0 && block.id != 15) continue;
        const auto record = block.id == 0 ? SavePair{std::get<int32_t>(block.value), -1}
                                         : std::get<SavePair>(block.value);
        if (record.first >= 0 && static_cast<size_t>(record.first) < scripts.size())
            unlock(static_cast<uint32_t>(record.first), record.second, context);
    }
}

void RetailScriptExecution::tick(int32_t elapsedMs, int32_t playerContext,
    const std::function<bool(const CityScriptCommand&, int32_t)>& dispatch,
    const std::function<void()>& refreshEvents, const std::function<void()>& saveGame, bool skipBlockingCommands,
    const RetailTravelHooks* travel, const std::function<void(const CityAudioCue&)>& playAudio,
    const RetailMissionHooks* mission)
{
    // ScriptManager::update checks the live state at each index, including changes made earlier in this tick.
    for (uint32_t id = 0; id < scripts.size(); ++id)
        execute(id, elapsedMs, playerContext, dispatch, refreshEvents, saveGame,
                skipBlockingCommands, travel, playAudio, mission);
}

void RetailScriptExecution::execute(uint32_t id, int32_t elapsedMs, int32_t playerContext,
    const std::function<bool(const CityScriptCommand&, int32_t)>& dispatch,
    const std::function<void()>& refreshEvents, const std::function<void()>& saveGame, bool skipBlockingCommands,
    const RetailTravelHooks* travel, const std::function<void(const CityAudioCue&)>& playAudio,
    const RetailMissionHooks* mission)
{
    auto& script = scripts.at(id);
    if (script.state != RetailScriptState::active) return;
    const auto& commands = program.at(id);
    while (script.state == RetailScriptState::active && script.next < commands.size()) {
        const auto& command = commands[script.next];
        if (command.opcode >= 1 && command.opcode <= 3) {
            const auto target = scriptWord(command);
            auto& linked = scripts.at(target);
            if (command.opcode == 1) {
                for (auto ancestor = std::optional<uint32_t>(id); ancestor; ancestor = scripts[*ancestor].parent)
                    if (*ancestor == target) throw std::runtime_error("Recursive retail script call.");
                if (linked.parent) throw std::runtime_error("Retail subscript already has a parent.");
                linked.parent = id;
                script.state = RetailScriptState::waitingForChild;
            }
            if (command.opcode == 2) {
                releaseCheckpoint(linked, travel);
                linked.state = RetailScriptState::locked;
            } else {
                unlock(target, -1, playerContext);
            }
        } else if (command.opcode == 4) {
            if (!command.payload.empty()) throw std::runtime_error("Invalid startup marker payload.");
        } else if (command.opcode == 57) {
            const auto duration = signedScriptWord(command);
            // Retail Wait consumes the current tick on entry and advances only below zero.
            const auto remaining = script.waitRemainingMs.value_or(duration) - std::max(0, elapsedMs);
            if (!skipBlockingCommands && remaining >= 0) {
                script.waitRemainingMs = remaining;
                break;
            }
            script.waitRemainingMs.reset();
        } else if (travel && travel->setActive && travel->reached &&
                   (command.opcode == 64 || command.opcode == 105)) {
            if (!skipBlockingCommands && !script.checkpoint) {
                auto checkpoint = decodeCityScriptCheckpoint(command);
                travel->setActive(checkpoint.object, true);
                script.checkpoint = std::move(checkpoint);
            }
            if (!skipBlockingCommands && !travel->reached(*script.checkpoint)) break;
            releaseCheckpoint(script, travel);
        } else if (playAudio && command.opcode >= 60 && command.opcode <= 63) {
            const auto cue = decodeCityScriptAudio(command);
            if (!skipBlockingCommands || cue.action == CityAudioAction::stop) playAudio(cue);
        } else if (command.opcode == 101 && travel && travel->escapePositions) {
            const auto escape = decodeCityWaitEscape(command);
            if (!skipBlockingCommands) {
                const auto positions = travel->escapePositions(escape.target);
                if (!positions || !retailEscapeReached(*positions, static_cast<float>(escape.distanceCm))) break;
            }
        } else if (command.opcode == 89 && travel && travel->occupiedVehicle && travel->trackVehicle) {
            const auto target = decodeCityWaitStolen(command);
            if (!skipBlockingCommands && travel->occupiedVehicle() != target) {
                if (!script.trackedVehicle) {
                    travel->trackVehicle(target, true);
                    script.trackedVehicle = target;
                }
                break;
            }
            if (script.trackedVehicle) {
                travel->trackVehicle(*script.trackedVehicle, false);
                script.trackedVehicle.reset();
            }
        } else if (command.opcode == 90 && mission && mission->wantedLevel) {
            const auto target = signedScriptWord(command);
            if (!skipBlockingCommands && mission->wantedLevel() != target) break;
        } else if (command.opcode == 66 && mission && mission->showObjective) {
            mission->showObjective(decodeCityMissionObjective(command));
        } else if (command.opcode == 65 && mission && mission->missionState && mission->startMission) {
            const auto missionId = scriptWord(command);
            const auto state = mission->missionState();
            if (state == 0) {
                mission->startMission(missionId, skipBlockingCommands ? 0 : -1);
                freezeRunningScripts(id, playerContext, travel);
            }
            // MissionStart blocks during the title sequence. State 3 or 4 permits script progress.
            if (state != 3 && state != 4 && !skipBlockingCommands) break;
        } else if (command.opcode == 67 && mission && mission->finishMission) {
            if (!command.payload.empty()) throw std::runtime_error("Invalid MissionOver payload.");
            mission->finishMission();
            unlockFrozenScripts(playerContext);
        } else if (command.opcode >= 15 && command.opcode <= 18 && mission &&
                   mission->cinematicPhase && mission->setCinematicPhase) {
            if (!applyCinematicCommand(command, *mission, skipBlockingCommands)) break;
        } else if ((command.opcode == 12 || command.opcode == 13) && mission && mission->selectCamera) {
            auto camera = decodeCityScriptCamera(command);
            if (skipBlockingCommands && camera.scene) camera.transitionMs = 0;
            mission->selectCamera(camera);
        } else if ((command.opcode == 41 || command.opcode == 42) && mission && mission->setFadeOpacity) {
            const auto fade = decodeCityScriptFade(command);
            auto& remaining = fadeRemaining[fade.fadeIn ? 0 : 1];
            if (!fade.durationMs || skipBlockingCommands || (remaining && *remaining <= 0)) {
                remaining.reset();
                mission->setFadeOpacity(fade.fadeIn ? 0 : 255);
            } else {
                // Retail initializes the duration and opacity on entry and starts counting on the next tick.
                if (remaining) *remaining -= std::max(0, elapsedMs);
                else remaining = fade.durationMs;
                const auto alpha = std::clamp<int64_t>(*remaining * 255 / fade.durationMs, 0, 255);
                mission->setFadeOpacity(static_cast<uint8_t>(fade.fadeIn ? alpha : 255 - alpha));
                break;
            }
        } else if (command.opcode == 43 && mission && mission->selectCinematicAnimation) {
            mission->selectCinematicAnimation(signedScriptWord(command));
        } else if ((command.opcode == 21 || command.opcode == 22 || command.opcode == 24) &&
                   mission && mission->placeActor) {
            mission->placeActor(decodeCityScriptActorPlacement(command));
        } else if ((command.opcode == 6 || command.opcode == 7) && mission && mission->animateActor) {
            mission->animateActor(decodeCityScriptActorAnimation(command));
        } else if (command.opcode == 56 && mission && mission->showDialogue &&
                   mission->dialogueComplete && mission->clearDialogue) {
            if (!skipBlockingCommands) {
                if (!script.dialogueShown) {
                    mission->showDialogue(decodeCityScriptDialogue(command));
                    script.dialogueShown = true;
                    break;
                }
                if (!mission->dialogueComplete()) break;
            }
            mission->clearDialogue();
            script.dialogueShown = false;
        } else if (command.opcode == 80 && mission && mission->limitHudWeapon) {
            mission->limitHudWeapon(signedScriptWord(command));
        } else if (command.opcode == 81 && mission && mission->limitHudWeapon && mission->weaponManagerLimit) {
            if (!command.payload.empty()) throw std::runtime_error("Invalid weapon HUD unlock payload.");
            if (mission->weaponManagerLimit() != -1) mission->limitHudWeapon(-1);
        } else if (command.opcode == 47 && mission && mission->tutorialsEnabled && mission->showTutorial) {
            const auto tutorial = decodeCityScriptTutorial(command);
            if (mission->tutorialsEnabled()) mission->showTutorial(tutorial);
        } else if (command.opcode == 48 && mission && mission->hideTutorial) {
            if (!command.payload.empty()) throw std::runtime_error("Invalid retail HideTutorial payload.");
            mission->hideTutorial();
        } else if (command.opcode == 112 && mission && mission->tutorialsEnabled && mission->blockingTutorial) {
            const auto tutorial = decodeCityScriptTutorial(command);
            if (mission->tutorialsEnabled() && !skipBlockingCommands && !mission->blockingTutorial(tutorial)) break;
        } else if ((command.opcode == 76 || command.opcode == 77) && mission && mission->setShopUnlocked) {
            mission->setShopUnlocked(decodeCityShopLock(command), command.opcode == 77);
        } else if (command.opcode == 23 && mission && mission->disableActor) {
            // Retail sets the pending-disable bit in normal playback and calls disable on skip.
            mission->disableActor(decodeCityDeferredDisable(command), skipBlockingCommands);
        } else if (command.opcode == 29 && mission && mission->actorAttack) {
            mission->actorAttack(decodeCityActorAttack(command));
        } else if ((command.opcode == 44 || command.opcode == 45) && mission && mission->setFightSide) {
            mission->setFightSide(decodeCityFightSideActor(command), command.opcode == 44);
        } else if (command.opcode == 46 && mission && mission->clearActorAttacks) {
            if (!command.payload.empty()) throw std::runtime_error("Invalid retail clear actor attacks payload.");
            mission->clearActorAttacks();
        } else if (command.opcode == 85 && mission && mission->tagKillTarget) {
            auto target = decodeCityKillTarget(command);
            if (std::find(script.killTargets.begin(), script.killTargets.end(), target) == script.killTargets.end()) {
                mission->tagKillTarget(target);
                script.killTargets.push_back(std::move(target));
            }
        } else if (command.opcode == 91 && mission && mission->actorKilled && mission->clearKilledTarget) {
            if (!command.payload.empty()) throw std::runtime_error("Invalid retail wait for kill targets payload.");
            if (!skipBlockingCommands && std::any_of(script.killTargets.begin(), script.killTargets.end(),
                    [&](const auto& target) { return !mission->actorKilled(target); })) break;
            for (const auto& target : script.killTargets) mission->clearKilledTarget(target);
            script.killTargets.clear();
        } else if (command.opcode == 87 && mission && mission->actorKilled && mission->clearKilledTarget) {
            const auto target = decodeCityWaitKilled(command);
            if (!skipBlockingCommands && !mission->actorKilled(target)) break;
            mission->clearKilledTarget(target);
        } else if (command.opcode == 108 && mission && mission->actorLocked &&
                   mission->actorKilled && mission->markLockTarget) {
            const auto target = decodeCityWaitForLock(command);
            const bool waiting = !skipBlockingCommands && !mission->actorLocked(target) &&
                                 !mission->actorKilled(target);
            mission->markLockTarget(target, waiting);
            if (waiting) break;
        } else if (command.opcode == 99 && mission && mission->actorGetOut) {
            mission->actorGetOut(decodeCityActorGetOut(command));
        } else if (command.opcode == 106 && mission && mission->explodeVehicle) {
            mission->explodeVehicle(decodeCityExplodeVehicle(command));
        } else if (command.opcode == 111 && mission && mission->tutorialsEnabled && mission->blinkHud) {
            const auto target = signedScriptWord(command);
            if (mission->tutorialsEnabled()) mission->blinkHud(target);
        } else if (command.opcode == 113 && mission && mission->recordChapterEnd) {
            if (!command.payload.empty()) throw std::runtime_error("Invalid retail chapter-end payload.");
            mission->recordChapterEnd();
        } else if (!(mission && mission->conditions && mission->conditions->apply(command)) &&
                   !dispatch(command, elapsedMs)) {
            break;
        }
        ++script.next;
        refreshEvents();
    }
    if (script.state != RetailScriptState::active || script.next != commands.size()) return;
    const auto parent = script.parent;
    script = {};
    if (parent) unlock(*parent, -1, playerContext);
    else saveGame();
}

RetailMissionConditionResult RetailBeQuick::evaluate(int32_t elapsedMs, bool cinematic)
{
    if (remaining <= 0) return {false, std::nullopt};
    if (cinematic) return {true, std::nullopt};
    const auto visible = remaining;
    remaining -= std::max(0, elapsedMs);
    return {true, visible};
}

RetailMissionConditionResult RetailPhotographQuickly::evaluate(int32_t elapsedMs, bool targetPhotographed)
{
    if (!photographed && targetPhotographed) {
        photographed = true;
        remaining = 0;
        return {true, std::nullopt, true};
    }
    if (remaining <= 0) return {photographed, std::nullopt};
    const auto visible = remaining;
    remaining -= std::max(0, elapsedMs);
    return {true, visible};
}

bool retailProtectionPasses(RetailProtectionTarget kind, bool dead, bool exploded)
{
    return !dead || (kind == RetailProtectionTarget::vehicle && !exploded);
}

std::string formatRetailMissionTimer(int32_t durationMs)
{
    const auto ms = std::max(0, durationMs);
    char text[24];
    std::snprintf(text, sizeof text, "%02d:%02d:%d", ms / 60000, (ms / 1000) % 60, (ms % 1000) / 100);
    return text;
}

RetailStayWithin::RetailStayWithin(int32_t distanceCm, int32_t warningTextId)
    : radiusSquared(int64_t(distanceCm / 100) * (distanceCm / 100)), textId(warningTextId)
{
}

bool RetailStayWithin::evaluate(const std::array<float, 3>& position, const std::array<float, 3>& target,
                                 int32_t elapsedMs)
{
    const float x = position[0] - target[0], y = position[1] - target[1], z = position[2] - target[2];
    // Retail truncates the squared distance in metres before testing the threshold.
    outside = std::floor((x * x + y * y + z * z) / 10000.f) > radiusSquared;
    timer = outside ? std::max(0, timer - std::max(0, elapsedMs)) : graceMs;
    return timer > 0;
}

std::optional<int32_t> RetailStayWithin::warningText() const
{
    return outside ? std::optional{textId} : std::nullopt;
}

std::optional<float> RetailStayBetween::gauge(const std::array<float, 3>& position,
                                              const std::array<float, 3>& target) const
{
    const auto distance = std::hypot(position[0] - target[0], position[1] - target[1], position[2] - target[2]);
    if (targetRemoved || distance < minimumCm || distance > maximumCm) return std::nullopt;
    return (distance - minimumCm) / (maximumCm - minimumCm);
}

bool RetailMissionConditions::add(RetailMissionCondition condition, std::string target, int32_t failureCause)
{
    constexpr size_t capacity = 8;
    if (conditions.size() == capacity) return false;
    if (!std::holds_alternative<RetailBeQuick>(condition) && target.empty())
        throw std::invalid_argument("Retail mission condition requires a target.");
    conditions.push_back({std::move(condition), std::move(target), failureCause});
    return true;
}

bool RetailMissionConditions::add(const CityMissionConditionCommand& command)
{
    using Type = CityMissionConditionType;
    switch (command.type) {
    case Type::protectNpc: return add(RetailProtectionTarget::character, command.target, command.argument);
    case Type::protectCar: return add(RetailProtectionTarget::vehicle, command.target, command.argument);
    case Type::stayWithin: return add(RetailStayWithin(command.argument, command.warningText), command.target);
    case Type::stayBetween: return add(RetailStayBetween{command.argument, command.minimumDistanceCm}, command.target);
    case Type::beQuick: return add(RetailBeQuick(command.argument));
    case Type::photographQuickly: return add(RetailPhotographQuickly(command.argument), command.target);
    }
    throw std::invalid_argument("Unsupported retail mission condition command.");
}

bool RetailMissionConditions::apply(const CityScriptCommand& command)
{
    if (command.opcode == 68) {
        if (!command.payload.empty()) throw std::runtime_error("Invalid MissionClearConditions payload.");
        clear();
        return true;
    }
    const auto condition = decodeCityMissionCondition(command);
    if (!condition) return false;
    add(*condition);
    return true;
}

void RetailMissionConditions::targetRemoved(std::string_view name)
{
    for (auto& entry : conditions) {
        if (auto* distance = std::get_if<RetailStayBetween>(&entry.condition); distance && entry.target == name)
            distance->targetRemoved = true;
    }
}

RetailMissionConditionUpdate RetailMissionConditions::update(
    int32_t elapsedMs, bool cinematic, const std::array<float, 3>& playerPosition,
    const std::function<std::optional<RetailMissionTargetState>(std::string_view)>& findTarget)
{
    RetailMissionConditionUpdate result;
    for (auto& entry : conditions) {
        bool passes;
        if (auto* timer = std::get_if<RetailBeQuick>(&entry.condition)) {
            const auto state = timer->evaluate(elapsedMs, cinematic);
            result.timerMs = state.timerMs;
            passes = state.passes;
        } else if (const auto* distance = std::get_if<RetailStayBetween>(&entry.condition)) {
            const auto target = findTarget(entry.target);
            result.spyGauge = target && !target->dead ? distance->gauge(playerPosition, target->position) : std::nullopt;
            passes = result.spyGauge.has_value();
        } else {
            const auto target = findTarget(entry.target);
            if (!target) throw std::runtime_error("Missing retail mission condition target " + entry.target);
            if (auto* photo = std::get_if<RetailPhotographQuickly>(&entry.condition)) {
                const auto state = photo->evaluate(elapsedMs, target->photographed);
                result.timerMs = state.timerMs;
                result.clearPhotoTargets |= state.clearPhotoTargets;
                passes = state.passes;
            } else if (auto* distance = std::get_if<RetailStayWithin>(&entry.condition)) {
                passes = distance->evaluate(playerPosition, target->position, elapsedMs);
                result.warningText = distance->warningText();
                result.timerMs = result.warningText ? std::optional{distance->remainingMs()} : std::nullopt;
            } else {
                const auto kind = std::get<RetailProtectionTarget>(entry.condition);
                passes = retailProtectionPasses(kind, target->dead, target->exploded);
            }
        }
        if (!passes) {
            result.failureCause = entry.failureCause;
            result.timerMs.reset();
            result.warningText.reset();
            result.spyGauge.reset();
            clear();
            break;
        }
    }
    return result;
}
