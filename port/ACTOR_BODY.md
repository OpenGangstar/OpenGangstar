# Original on-foot actor body backend

OG-025, initial native baseline 141928a; cleanup revision verified against 330d2de.
The bounded RetailActorPhysics backend creates and
owns the actor body and metadata, uses the verified shared terrain-controller pool,
and implements the original body placement/refilter and saved-filter operations.
It is not a complete Character, scene hierarchy, movement controller or production
binding. It does not consume unintegrated jump, animation or contact-listener APIs.

## Original source and corrected circle settings

Existing matched source was consulted before reconstruction. initPhysic remains
pending and was traced against the original ELF/assembly. The placement and collision
helpers already have matched C++ definitions in Character.cpp.

| Original symbol | Thumb address | Native boundary |
| --- | --- | --- |
| Character::initPhysic() | 0x002f4465 | Existing-body early return; on-foot attributes/body/circle/mass/metadata creation |
| Character::SetPosition(vector3 const&) | 0x002f2c61 | Body SetXForm and every-shape refilter leaf only |
| LevelObject::SetPosition(vector3 const&) | 0x00316861 | Scene commit prerequisite, explicitly remains caller responsibility |
| Character::removeCollisions() | 0x002f2b69 | Zero active filter fields and refilter |
| Character::resetCollisions() | 0x002f3091 | Restore original saved filter on every shape and refilter |
| Character::cleanPhysic() | 0x002f2fe1 | Body/metadata/attribute cleanup ownership; safe callback lifetime adaptation |
| PhysicAttributes::PhysicAttributes(int,b2Body*) | 0x0031b29d | One-slot construction with supplied body NULL when absent |
| b2Body::SetXForm(vec2 const&,float) | 0x003f0cd9 | Real native body transform/freeze behavior |
| b2World::Refilter(b2Shape*) | 0x003ffd25 | Real native broad-phase refilter |

The original circle radius is **0.5 metres**, with **friction 0.3**, density 1,
restitution 0 and sensor false. Earlier ACTOR_COLLISION.md text inferred radius 0.3
and friction 0.2; the independent creation-argument capture disproves that inference.
In initPhysic,0x2f4552 writes0x3f000000 to sp+40, the CircleDef radius.0x2f4562 writes
0x3e99999a to sp+12, the friction field. It does not overwrite radius. Original ARM
CreateShape argument capture confirms radius 0.5 and friction 0.30000001192092896,
with local centre(0,0). Native tests assert the actual circle values and analytic
mass pi*0.5^2 and inertia0.5*mass*0.5^2. This report uses the corrected settings.

## Creation order and identities

Original initPhysic returns immediately when Character's body is already present.
It does not inspect new position, metadata or role inputs on that path. When
attributes are absent it constructs one-slot PhysicAttributes with supplied body NULL,
then creates the actor body. An existing vehicle branch copies vehicle PhysicInfo;
that branch is outside this explicitly on-foot backend.

The native backend constructor establishes the original attribute boundary. It
exclusively borrows an optional existing RetailHeightController, or constructs and
owns a one-slot controller before any actor body, with suppliedBody=nullptr. Fresh
owned attributes zero the live owner's currentJumpState, matching the original
attribute constructor. Borrowing existing attributes does not reset that counter.
This does not initialize an unintegrated entire jump snapshot.

initialize performs the idempotent body portion. BodyDef position is the supplied
original GetPosition XY divided by100; angle, mass data and both dampings are zero.
All four final flags are false: allowSleep, isSleeping, fixedRotation and isBullet.
Do not confuse world-level sleeping permission with the actor's disabled sleeping.
CreateShape is followed by SetMassFromShapes, so the actor is a dynamic mass-bearing
body, not a static circle or a kinematic placement substitute.

Explicit native owner identity determines player role: the player circle category
bits are2, all other characters4. This is independent of LevelObject.s_type
classification. Mask is0xffff, signed group is that actor's actual height pool group
(index+1). The three values are saved at native creation, corresponding to original
Character offsets 50/52/54. Disabling or moving the body does not overwrite them.
No independently manufactured group counter or duplicate terrain controller is used.

The actor body is created after attributes. It never becomes the height controller's
constructor-supplied body merely because it was created later. A borrowed height
controller retains its own original supplied pointer. Actual local-cliff fixtures
prove both cases through the original velocity gate, with real native bodies.

## Metadata and lifetime

After shape creation and mass computation, the original allocates a28-byte ARM
metadata record, stores owner and skin-node pointers, zeros four height floats and
clears bytes 24/25. Native code uses the existing typed RetailPhysicsBodyData with
full-width owner pointer, optional raw skin-node bbox Z, zero heights and false
jumpSurface. No ARM offsets are reused for a native object. The verified OG020
trailing defaulted opaqueJumpFlag initializes to zero for this ordinary actor body,
matching original byte 25; it is not interpreted as a new gameplay flag here.

A present node bbox is supplied exactly as the skin node reports it, without guessed
translation or root-height conversion. An absent skin node stays absent. Existing
RetailContactFilter reads the live owner positionZ/rootZ where required. The caller
must synchronize those live values from the real scene; this body leaf does not
pretend to update a scene node. synchronizeSkinNodeBounds adapts actual bbox changes
to the existing value-based native metadata representation, without inventing a new
refilter time. The caller updates it before an original refilter site.

World, live owner and shared terrain outlive the backend. A borrowed controller
belongs to that same world/terrain, is exclusive to this actor and outlives its body;
these are explicit caller invariants, not inferred from pointer values. Owned
attributes are released after the actor body. A borrowed controller remains owned by
its caller, which must release it after the body backend. Native body mutation,
creation/destruction and refiltering occur outside world.Step with nonthrowing
callbacks. No default empty height/jump/scene handler is installed.

The original cleanPhysic frees metadata and nulls userdata before DestroyBody.
Native destruction deliberately retains complete metadata during DestroyBody/contact
Remove callbacks, then frees it, then releases owned height/local-cliff bodies. This
safe lifetime adaptation is tested with actual contacts and documented rather than
claimed byte-identical unsafe cleanup. After DestroyBody returns, the actor body
pointer is immediately cleared before metadata and owned-height teardown. Original
cleanPhysic stores NULL at 0x2f3012 before its attributes destructor call at 0x2f301c.
A real nonthrowing b2DestructionListener regression proves that the actor body and
metadata remain present during its own shape callback, but the later owned-cliff
shape callback observes a NULL actor body. No freed pointer is dereferenced by the
test. RAII destruction is this backend's cleanup boundary; the host must end/reset its backend when Character cleanPhysic is required.

## Placement and saved filters

Full Character::SetPosition first calls LevelObject::SetPosition, which sets scene
local position and calls updateAbsolutePosition(false) when a node exists. This
scene operation and live owner synchronization remain caller responsibilities.
setBodyPositionAfterSceneUpdate implements only the following original body leaf:
SetXForm(requested XY/100,current body angle), then Refilter every shape, including
zero-mask shapes. It does not substitute absolute scene XY for the requested
argument. It does not explicitly zero velocities, reset height/cache/markers, cancel
a destination or reset jump state. Valid placement preserves velocity and angle.

The return value of SetXForm is ignored, as original. A finite out-of-world placement
updates the transform, freezes the body, zeros linear/angular velocity and removes
proxies through actual Box2D behavior. Later placement of that frozen body does not
move or revive it. The leaf still refilters its shapes; null proxies are safely
ignored by original RefilterProxy. No generic teleport fallback or manual unfreeze
is added.

removeCollisions visits every shape but changes only nonzero-mask shapes: category,
mask and group all become zero, followed by refilter. Already zero-mask shapes keep
other filter fields. resetCollisions restores all three saved fields to every shape
and refilters each one. Native tests use multiple real shapes, a recording test
filter, and real original vertical collision filtering to verify those distinctions.

## Explicit initial-geometry safety boundary

Historical CreateShape outside the world asserts in Debug and can silently retain
no proxy in Release. Before actor CreateShape, a temporary actual native circle is
constructed through the protected native factory, its ComputeAABB is evaluated at
the real body's transform, then the actual world.InRange predicate is consulted.
There is no guessed circle bound or caller-duplicated world box. Outside initial
geometry and nonfinite initial XY report explicit errors. Any partial new body is
destroyed; no metadata/body/proxy is leaked. Attributes/pool identity remain reserved
for an explicit retry. Existing-body early return still ignores invalid new inputs.

This is a creation-only safety adaptation, not an alteration of original finite
out-of-world SetPosition behavior. Other invalid/nonfinite placement calls are not
claimed to be a newly supported recovery case. Existing terrain capacity/world
bounds limits and pool invariants remain unchanged.

## Validation and reference capture

Actual native tests cover player/other roles, group identity, circle radius/friction,
analytic mass/inertia, zero damping, permitted rotation, disabled sleeping, raw node
bounds/owner pointers, absent skin, idempotence, unchanged terrain/cache/generation,
velocity/angle placement, all-shape refilter, active-mask disable/restore, Z-only
owner and node-bbox refilter effects, contact recreation, metadata in Remove,
constructor-supplied body distinction, owned/borrowed height lifetime, pool 50
exhaustion/reuse, initial-range failure/retry, frozen placement and full backing
allocation cleanup. No manually played progression is involved.

Ignored og025_oracle.py executes original initPhysic for five cases: player/other,
new/existing attributes, plus the existing-body early return. It captures actual
original body/circle arguments, saved filters, metadata bytes and creation order.
Separate original Character placement/remove/reset execution records correct
requested-coordinate/angle arguments, scene-before-body order, both shape refilters,
mask-zero exclusion and saved-filter restoration.

The oracle intercepts allocations, PhysicAttributes construction, array-backed
GetPosition, skin-node lookup, player lookup, body/shape creation, mass computation
and temporary CircleDef destruction. Placement capture intercepts the scene commit,
SetXForm and Refilter only to record calls. AEABI math hooks are inherited from
OG015. Those interceptions do not validate actual Box2D mass/contact/lifetime or
scene behavior; independent native tests validate the real bodies, contacts and
cleanup. Emulator packages, ELF bytes, captures and generated ARM views stay ignored
research artifacts and are absent from the native build/runtime.

## Remaining production boundaries

Vehicle PhysicInfo transfer, full scene graph commit, owner/bbox synchronization,
actor spawn/clean wiring, root motion/steering/animation clock and production movement
integration remain separate tasks. This backend installs no collision-listener
substitute and makes no full-controller/gameplay completion claim. Invalid borrowed
world/terrain/ownership bindings remain caller errors. Exact validation results,
commands and patch hash accompany the ignored OG-025-report.md submission.
