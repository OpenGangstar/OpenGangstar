# Original actor movement and the native controller boundary

OG-010, investigated against native baseline 4d849da and the pinned retail ARM
reference. This is an implementation prerequisite, not a completed movement port.
It refines the blocking-command dependencies in CINEMATIC_SCHEDULER.md.

Original movement is driven by sampled animation-root displacement and a physics
body. Script waits do not own its clock or determine when it has finished. A
distance divided by average clip speed is insufficient for the native controller.

## Evidence map

Addresses retain the Thumb bit used by config/functions.tsv. Matched means the
original Android catalogue reports a match; it does not mean native coverage or
an independently repeated matching build in this investigation.

| Original function | Address | Reference status |
| --- | --- | --- |
| Character::setDestination(irr::core::vector3d<float> const&, int) | 0x002f2105 | Matched C++ |
| Character::walkToLocation(irr::core::vector3d<float> const&) | 0x002f400d | Matched C++ |
| Character::runToLocation(irr::core::vector3d<float> const&) | 0x002f3fa5 | Matched C++ |
| Character::Stop() | 0x002f1d3d | Matched C++ |
| Character::CheckDestination() | 0x002f2135 | Matched C++ |
| Character::turnToward(irr::core::vector3d<float> const&) | 0x002f3e99 | Matched C++ |
| Character::setAnimation(int, float, bool, bool, bool) | 0x002f1d91 | Pending body; ARM inspected |
| Character::updateAnimation(float) | 0x002f3489 | Pending body; ARM inspected |
| Character::updatePosition(irr::core::quaternion) | 0x002f42cd | Pending body; ARM inspected |
| Character::initPhysic() | 0x002f4465 | Pending body; ARM inspected for dependencies |
| Character::bindAnimation(char const*, Character::AnimationInfo&, irr::scene::ISceneNode*) | 0x002f23fd | Pending body; binding inspected |
| Character::getSpecificAnimation(int) | 0x002f1a31 | Pending body; dependency identified |
| PlayerTest::walkToLocation(irr::core::vector3d<float> const&) | 0x00301535 | Pending body; ARM inspected |
| PlayerTest::runToLocation(irr::core::vector3d<float> const&) | 0x003014ed | Pending body; ARM inspected |
| PlayerTest::Update() | 0x00304ec9 | Pending body; relevant branches inspected |
| PlayerTest::updateAnim(float) | 0x00303fe9 | Pending body; ARM inspected |
| PlayerTest::Stop() | 0x00301a3d | Pending body; ARM inspected |
| PlayerTest::UpdateSceneNode() | 0x00302685 | Pending body; ARM inspected |
| NPC::walkToLocation(irr::core::vector3d<float> const&) | 0x002fd7c5 | Matched C++ |
| NPC::runToLocation(irr::core::vector3d<float> const&) | 0x002fd789 | Matched C++ |
| NPC::Stop() | 0x002fd821 | Pending body; ARM inspected |
| NPC::Update(int) | 0x002ffe0d | Pending body; movement branches inspected |
| GS3DStuff::update(StateMachine*) | 0x002e078d | Pending body; update order inspected |
| ScriptManager::moveObject(LevelObject*, LevelObject*) | 0x0033f0f9 | Pending body; ARM inspected |
| PlayerTest::teleportTo(LevelObject const*) | 0x00304895 | Pending body; reset/placement branches inspected |
| LevelObject::canGoToPoint(irr::core::vector3d<float> const&, irr::core::vector3d<float>&, irr::core::vector3d<float>&) | 0x00317fb5 | Unresolved native dependency |
| PhysicAttributes::GetHeight(irr::core::vector3d<float> const&, int, bool) | 0x0031ff49 | Unresolved native dependency |

## Destination, animation, and completion state

setDestination copies a point and stores the square of the supplied integer
limit. It does not move the actor or select an animation. The PlayerTest walk/run
wrappers reject a dead player, set flag 0x8000, set the limit to 100 world units,
and call the corresponding Character method. World positions are centimetres:
Character::initPhysic divides XY by 100 when creating the body; updatePosition
multiplies body XY by 100 when updating the scene node.

Character walk rejects death, replaces only the low movement-flag byte with 2,
faces the destination, and requests specific animation 202 with speed 1, looping
and root motion enabled. Run replaces that byte with 4 and requests animation
188; its base method additionally rejects the down state. The player wrapper
sets its destination/0x8000 before that additional run guard. Do not flatten these
two levels into an invented uniform guard.

The selected animation must go through getSpecificAnimation. IDs 202 and 188
are requests, not proof that every weapon/context/actor always uses the same
hardcoded clip. setAnimation checks interruption and cinematic state. A request
for the current animation returns without resetting its phase. Reissuing a walk
to another destination therefore must not automatically restart the walk cycle.
Its accepted-animation path stores clip start/end/length, speed, loop/root-motion
options and a zero frame cursor. It does not establish a general policy of
clearing every movement flag or root-position cache.

For the scripted player branch, PlayerTest::Update at 0x00305104..0x0030519a
checks 0x8000, faces the adjusted destination, and compares the horizontal squared
distance to the original destination against the stored squared limit. The
comparison is strictly less than: exactly 100 cm does not finish a default move.
The assembly's Z term subtracts destination Z from itself; this branch does not
require matching the destination height. Completion at 0x00305656 clears 0x8000,
invokes PlayerTest::Stop, then calls startTurn with 8.0. It does not snap to the
destination. This decision occurs in the actor update, before this frame's
physics step and later script execution.

Character::Stop preserves upper flags, replaces the low byte with 1, and requests
specific animation 212 with root motion disabled. LevelObject::Stop is an
original empty base method. PlayerTest::Stop adds zeroing of body linear and
angular velocity and clears its target; the movement-completion branch, rather
than this wrapper itself, clears 0x8000. Consequently, the VM's 0xfe readiness
mask must query the real movement word, not a new boolean based on elapsed time.

NPC movement has additional state at original offset 356. Walking sets bit 0,
running sets bit 1, and their matched helpers clear selected other bits including
0x4000; NPC::Stop clears the low two NPC bits after Character::Stop. The relevant
NPC::Update arrival branch at 0x00300504..0x0030056a also compares horizontal
distance squared strictly against the stored limit and clears those bits, but
subsequent path, vehicle, target and AI branches differ from the player. This
investigation did not reconstruct the whole NPC update. NPC/Character vtables
inherit LevelObject::GetSpeed() == 0. That original accessor, not actual body
velocity or NPC path duration, is what StopObject queries for categories 2/3.

## Root motion and frame ordering

Character::bindAnimation constructs the animator from ./anim_ingame.bdae and
binds the root UID supplied by the actor setup. NPC::setupSceneNode at
0x002fd8bd supplies Bip01-node; the retail string was verified in the ELF.
Do not extrapolate a complete actor binding contract from a guessed node index.
The animator slot used by updateAnimation resolves to the scene-node animation
interface; the inspected CSceneNodeAnimatorSet vtable points its animateNode
entry to 0x002b0209.

The ordinary walk/run root-motion path in updateAnimation has these observable
steps:

1. Advance the floating cursor by the supplied elapsed value times the accepted
   animation rate. Character's initial speed multiplier is 1. Clip length is
   end minus start, verified in CTimelineController::getClipLength at 0x002b2875.
2. Evaluate the animator at clip start plus the truncated integer cursor modulo
   clip length. Root evaluation therefore does not simply sample an unrestricted
   floating scene-playback clock.
3. Subtract the cached previous root position from the sampled root position.
   When a wrap/end is detected, first evaluate start + length - 1 and accumulate
   the tail. For a looping clip, evaluate its start, update the root cache, and
   then accumulate the new head. Merely subtracting samples on opposite sides of
   a wrap produces the wrong displacement.
4. If positive animation advance and the root-motion option permit it, transform
   the local displacement using the actor node's absolute matrix and write body
   linear velocity. The ordinary branch uses literal displacement factor 0.001
   and denominator elapsed * 0.0001, equivalent to ten times transformed root
   displacement divided by elapsed in milliseconds. Preserve the original
   matrix and scale conventions; replacing them with an assumed yaw is unproven.
5. Update the cached sampled root position, retain the sampled root Z separately,
   and set the animated root node's local translation to zero. Translation must
   not be applied to both the body and the rendered skeleton.

The separate option at original byte 150 uses the sampled displacement magnitude
with stored direction components instead of the full local-displacement transform. It is not enabled
by the basic walk/run calls inspected here. Treat it as a distinct unresolved
controller branch rather than silently substituting a constant speed.

isAnimationLastFrame at 0x002f15bd uses integer cursors: a nonlooping clip ends
when the advanced integer cursor reaches its length; loop wrap detection compares
old/new remainders. It is not an arbitrary-multiple-wrap counter. Preserve and
test the exact full-cycle/large-delta behavior before changing that assumption.
The initial root cache and interruption transitions also need comparison against
the original binding path; do not reset caches on every destination command.

GS3DStuff::update establishes the relevant outer order:

1. Read the application/device timer and form the elapsed value from its previous
   value. SceneNodeManager::update at 0x00321421 updates a pool/countdown field;
   that call alone is not evidence of advancing every animation.
2. Update objects from GameObjectManager list 1, then call PlayerTest::Update.
   NPC::Update clears its body's velocities before evaluating its animation;
   PlayerTest::updateAnim invokes Character::updateAnimation when bound and valid.
3. Add the elapsed time, capped at 250 ms for this physics accumulator, and run
   b2World::Step at 0x00400c7d using 1/60 second and 10 iterations. Vehicle physics
   updates surround each step. The animation timer and this capped accumulator
   must not be confused with the native walk helper's unrelated 50 ms clamp.
4. Synchronize the player scene node through UpdateSceneNode/updatePosition;
   vehicle/passenger branches have their own paths. The normal updatePosition
   path copies body XY back into the object while preserving world Z, applies
   rotation, and updates visual/shadow transforms. It does not integrate a walk
   toward a destination.
5. Call ScriptManager::update at 0x0033f511, then update the camera and remaining
   presentation systems. A movement command dispatched here affects the next
   actor update; a waiting VM can observe completed actor flags this frame.

Held dialogue must therefore block command progress without stopping the actor
update/physics clock. Original collision resolution can prevent arrival forever;
an estimated clip duration is not permission to clear the movement flags.

## Placement and paths are separate operations

MoveActorTo, opcode 24, dispatches through ScriptManager::moveObject. Its
non-player branch copies the marker's rotation and position and reinitializes
PhysicAttributes. It does not call Stop or universally clear destination/animation
flags. Character::SetPosition separately synchronizes the body transform and
refilters its shapes. NPC transform placement must not automatically discard an
older destination merely because the native static timeline gives it precedence.

The player branch uses teleportTo and ResetPlayerTest, with separate vehicle
handling. That reset is not interchangeable with an ordinary NPC transform copy.
Tests involving placement superseding old movement must specify which original
operation and actor category is involved. A StopObject followed by placement must
still execute in VM order in the same tick once the real predicate is ready.

Destination commands take a position snapshot at dispatch. Moving the destination
actor later does not turn that stored point into a tracking reference. For Script
36's two actor-relative walk commands, neither command immediately integrates a
position, so each command can snapshot the other actor's then-current position.

NPC::setPathToFollow resets its current waypoint to -1 and disables looping.
getNextPoint detects a changed path, resets the active waypoint, and obtains its
next authored point; goToNextPoint has repeat and exhaustion behavior. Those
matched helpers and the original NPC/AI update must drive path progression.
They are not a guarantee that linear interpolation through every vertex preserves
AI, collision, stop, target or loop behavior.

## Reusable native facilities and unresolved dependencies

| Existing native facility | Appropriate reuse | Limitation |
| --- | --- | --- |
| readClip, CityAnimationClip/Channel | Retail key/clip metadata and actor-node binding | Verify sampler and boundary semantics for the original integer-frame root evaluation |
| sample/applyAnimationChannel and node transforms | Sampling/math building blocks | Quaternion/transform fidelity is not proven by the current rendering path |
| CityTerrain ground/cliff indices and groundContact | Authored collision geometry and focused query fixtures | Not a replacement for original Box2D bodies, shape filters, contacts or destination adjustment |
| CityPlayback player/actor poses | A destination for post-physics presentation state | Not the owner of original completion flags, per-actor animation phase or body state |
| RetailScriptExecution/objectMotion hooks | Ordered commands and real completion queries | Must be bound to controller state; do not synthesize original flags from scene duration |
| CityCinematicWalk, bindMotions and CityPlayback::walk | Existing approximate presentation and input behavior | Endpoint-average speed, linear travel, wall rejection/sliding and a held scene clock are not faithful root-motion/physics integration |

Native body construction, collision filters/contact behavior, destination
adjustment through LevelObject::canGoToPoint, PhysicAttributes height selection,
animation binding/interruption and the complete NPC AI update remain dependencies.
Physics.h in the matching project is a partial ARM call/layout view, not a native
x86-64 object definition. OG-011 investigates the original physics dependency;
do not instantiate those ARM layout views or use empty base virtual methods as
native physics implementations.

The smallest defensible OG-008 implementation slice is the alive, on-foot,
direct-player walk/run controller: typed destination/flag state, original clip
selection/phase, sampled root velocity, and a real body backend advanced outside
the VM. First validate its free-space movement and exact readiness edges against
the original algorithm. Integrate the real collision/destination/height dependency
before claiming a production-faithful controller. Other branches must remain
explicitly unresolved; a no-op collision adapter is not completion. This slice
does not require another cinematic timestamp scheduler or an immediate rewrite
of every mission.

## Acceptance and remaining proof

- Advance movement/root animation while a zero-duration dialogue holds the VM.
  The command clock may wait; the body clock must not freeze with it.
- Check horizontal distances just below, exactly at, and just above 100 cm, with
  differing Z. Only the strict inside case completes the observed player branch.
- Complete through the actor update, zero player velocities through Stop, and
  let StopObject release a same-tick placement without cancelling movement early.
- Reissue a destination while the same walk animation is active and preserve its
  phase. Resolve actor-relative destination points at dispatch, not every frame.
- Compare integer-frame samples, one wrap, exact full-cycle elapsed, nonlooping
  end, interruption, zero advance, and initial-root-cache behavior. Verify that
  root translation is not added twice.
- Check cardinal directions, actor rotation/scale, body metre/world centimetre
  conversion, fixed-step accumulation and post-physics synchronization.
- Keep an obstructed actor outside the completion radius blocked; verify real
  body/filter and destination-adjustment behavior before accepting that case.
- Preserve player teleport reset separately from non-player placement retaining
  destination state. Verify dead/down guards and upper flag preservation.
- Preserve the original NPC zero-speed StopObject result even when the body is
  moving. Test NPC paths/arrival separately; do not replace this predicate with a
  new duration or distance test.

This investigation inspected the recovered C++, original ARM function branches,
ELF strings/vtables/constants, and the current native movement/sampling code. It
decoded the 1/60, 250 ms, 100 cm, 0.001 and 0.0001 constants and checked clip-length
semantics against the binary. No gameplay, runtime controller validation, new
matching comparison, build, or code change was performed. Interruption branches,
complete path/AI behavior, normalized-direction root motion, height/collision
parity and player reset details need their own bounded implementations and tests.
