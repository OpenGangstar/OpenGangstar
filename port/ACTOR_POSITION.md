# Original post-body actor scene readback

`updateRetailActorPosition` ports Character::updatePosition at `0x2f42cd` using
genuine native scene nodes, an optional real `b2Body`, retained floor state and
the same actor's live height slot. Quaternion input is by value, preserving the
original copy before any scene work.

When a body exists, the function reads its native transform position through
Box2D's real GetPosition API, multiplies XY by100f, and retains the actor's
cached worldZ. It commits those values with node setPosition and
updateAbsolutePosition(false), matching qualified LevelObject::SetPosition at
`0x316861`. It does not call Character's separate body teleport operation.

It then always assigns the copied quaternion, reads cached world position again
and assigns that value as LOCAL position, without another absolute update.
The next cached-position value supplies approved `updateRetailActorShadow`,
which uses retained floor+3 and the actual height changed/normal state. The
floor module and this module share one original LevelObject cached-position
value getter; no duplicate position conversion or quaternion math was added.

Nonidentity parent behavior remains literal. For parent translation(10,20,30),
initial actor local(1,2,3), cached absolute(11,22,33) and body(1.25,-2.5) metres,
the first placement produces cached absolute(135,-230,63). The final LOCAL
position becomes that same cached value. No inverse parent transform or extra
refresh is introduced. Shadow absolute cache remains stale for the later real
global frame.

## Player post-world reset and gate

`finishRetailPlayerPhysics` ports the bounded outer continuation at
`0x2e09b8..0x2e09e4`. It clears player linear XY even when the existing world
accumulator took no steps. Native Box2D2.0.1 SetLinearVelocity is exactly a member
assignment: it does not wake, move or refilter the body. Angular velocity,
sleep state and transform remain unchanged.

It returns false only when movementFlags mask0x400 is set and actual current
animation is -1 or162, matching the real Player isDown dispatch at `0x2f1581`.
The caller reads live state after world callbacks. A true result requires actual
Player UpdateSceneNode processing: real target lookup/facing side effects first,
then copied current rotation and the Character readback above. The original
vehicles step and script update follow this stage. This helper does not pretend
the target is absent or supply a second physics accumulator/Step loop.

## Validation and limits

Original ELF SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.

Root independently replayed the original whole Character function for four
body/no-body and identity/nonidentity parent cases, plus six actual original
post-world linear-reset/down-gate continuations. Native Release/Debug match
54 coordinate/velocity words and six gate decisions. Original probes observe
the qualified base placement and setter/getter order, check stop/return PCs,
and reject any Character teleport/SetXForm/refilter entry.

Native tests use actual Box2D bodies/shapes and a real collision-filter observer.
They verify no movement, velocity reset or refilter during scene readback;
unchanged shadow cached-matrix bytes; retained old shadow rotation; no wake or
angular change during the player reset; a reset with zero physics steps; and
readback after a genuine native Step using the retained pre-step floor. Approved
OG065 separately proves all shadow normal/quaternion branches. These tests do
not invoke manual gameplay or terrain queries after stepping.

Original ARM body words appear only in ignored reference probes. Production
code uses native Box2D APIs and typed fixed32 flags/animation state, with no ARM
object overlays. Imported reference memory/arithmetic, typed actor/body inputs
and intercepted scene-dispatch boundary are explicit. Full original allocator,
Android runtime and entire outer Player function are not claimed.

The production host still must provide real global shadow ownership, target/
facing/vehicle/death branches, live collision metadata, the single shared world
advance, subsequent vehicles/scripts/camera and late scene/render scheduling.
It must preserve original metadata observation timing rather than silently
using stale box/position snapshots. No extra height query, teleport/refilter,
fake scene host or renderer completion is introduced by this component.
