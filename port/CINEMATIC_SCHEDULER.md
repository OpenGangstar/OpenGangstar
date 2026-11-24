# Ordered cinematic execution prerequisites

Production cinematics currently compile original commands into separate timed
collections. This representation cannot faithfully implement every blocking
command. OG-005 identified a concrete failure in retail Script 33:

| Command index | Command |
| --- | --- |
| 42 | ActorWalkTo(Player, Marker_Pedro_C3M1_02C) |
| 43 | Wait(700 ms) |
| 44 | ActorPlayLoop for Latisha |
| 45 | Wait(300 ms) |
| 46 | Dialogue with duration zero, held until input |
| 47 | Wait(400 ms) |
| 48 | StopObject(Player) |
| 49 | MoveActorTo(Marker_Pedro_C3M1_02A, Player) |

Commands 48 and 49 share the nominal timestamp 10,700 ms when only authored
fade and Wait durations are summed from the full script. `readCinematic` omits
command 48. The later placement can therefore supersede the earlier walk without
waiting for its original completion predicate. Timestamp equality does not retain
which commands executed before a blocking command.

`CityPlayback::advance` also leaves its single clock unchanged while dialogue is
held. Actor movement and animation use that clock, so they freeze during the
dialogue. The original scene and game objects advance independently of script
blocking: `GS3DStuff::update` at 0x002e078d updates the scene manager and then
objects, while `PlayerTest::UpdateSceneNode` at 0x00302685 advances position through
`Character::updatePosition` at 0x002f42cd.

Adding a fixed movement-duration delay would count held dialogue as zero movement
time and can wait longer than the original game. Shifting only later timestamps
misses same-time commands; shifting every equal-time cue alters commands that
preceded the wait. StopObject must not cancel movement or relocate the actor.
Script 36 also rebinds actor-relative movement from live positions, making
load-time delay estimates unsuitable for those paths.

The implementation sequence is:

1. Preserve original movement command decoding and ordered VM dispatch (OG-007).
   Opcodes 25/26 are ActorWalkTo/ActorRunTo; 34/35 are ActorRunPath/ActorWalkPath.
   Only skipped destination commands relocate through ScriptManager::moveObject.
   Path commands still initialize the original NPC path/AI behavior on skip.
2. Port actor movement and completion state with a world clock that advances while
   the VM blocks (OG-008). Placement, destinations, paths, animation/root movement,
   and completion state belong to the actor controller. Do not fabricate original
   flags from a compiled scene duration.
3. Bind the original command VM to cinematic actors, placement, camera, dialogue,
   fade, and audio. Preserve command order and query real actor completion through
   the existing objectMotion hook, rather than adding another static scheduler.

The StopObject predicates already implemented in the shared VM come from
`Script::executeCommand` at 0x00339cb9, opcode-5 branch 0x0033b6be. Player/category-1
movement flags masked by 0xfe must be zero; categories 2/3 require virtual
GetSpeed() to equal zero; category 5 requires flag 0x4000 to be set. Original NPC
and Character vtables inherit the zero-speed accessor. Do not substitute a wait
for their compiled path duration.

Acceptance must cover movement during held dialogue, a stop followed by a
same-tick placement, actor-specific placement behavior, consecutive moves with a
stop, original NPC zero-speed behavior, vehicle readiness and skip, and dynamic
actor-relative destinations. Camera, fade, audio, and animation timing also need
original-state evidence. This dependency investigation made no production change;
the compiled cinematic defect remains unresolved. No manual progression was used.
