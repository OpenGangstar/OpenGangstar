# Original jump-surface height and state

OG-020, native baseline80705a2. This component implements the original snapshot,
jump-height query and composition with the ordinary terrain wrapper. It does not
implement authored jump-body creation, actor steering, vehicle airborne dynamics,
vertical impulses, animation or the production game-loop binding. Existing approximate
CityTerrain jumpRamps are not inputs to this component or evidence of its parity.

## Original evidence

The original matched/nonmatching source was checked first. These pending functions
were traced in the pinned original ELF and existing assembly records, using the
original Box2D2.0.1 body/shape access contract to identify geometry fields.

| Original function | Thumb address | Implemented boundary |
| --- | --- | --- |
| PhysicAttributes::SetCurrentJumpShape(b2Shape*) | 0x0031ccf1 | Copy transformed quad and metadata, centre, enable and raw flag |
| PhysicAttributes::GetJumpHeight(vector3 const&,int) | 0x0031a6fd | Strict 3D radius, quad inclusion, ABC interpolation and live counter |
| PhysicAttributes::GetHeight(vector3 const&,int,bool) | 0x0031ff49 | Successful ordinary-core jump selection and exact cache/state order |
| PhysicAttributes::PhysicAttributes(int,b2Body*) | 0x0031b29d | Zero quad/centre, disabled state and current counter initialization |
| ContactFilter::ShouldCollide(b2Shape*,b2Shape*) | 0x0031dc7d | Existing native positive-direction callback dispatch, tested with real bodies |

The constructor zeros five three-float vectors at original offsets52..108,
disables byte112 and zeros signed state116. Byte113 is not initialized in that
constructor. The native opaque byte starts at zero for deterministic safe storage;
its pre-snapshot value has no effect on the implemented query paths. SetCurrentJumpShape
copies metadata byte25 to byte113 without boolean conversion. Its original semantic
meaning and additional consumers remain unproven; it is named opaqueJumpFlag.

## Snapshot and lifetime

SetCurrentJumpShape reads four local polygon vertices in their original stored order.
Each XY is transformed by the body's current rotation matrix and position, then
multiplied by100 to centimetres. The multiplication/addition order is preserved.
Each Z comes directly from the corresponding metadata heightsCm element; it is not
rotated or translated by a guessed three-dimensional scene transform.

The centre XY uses left-associated A+B+C+D followed by multiplication by0.25.
Centre Z adds D.z to ((A.z+B.z)+C.z), then multiplies by0.25. The surface becomes
enabled and copies the opaque metadata byte. Selecting another shape does not reset
the live currentJumpState. Neither a shape/body pointer nor a metadata pointer is
retained. Later shape motion, metadata mutation or destruction does not change this
snapshot; selecting it again explicitly refreshes it.

RetailJumpController borrows one live RetailPhysicsOwnerState. Its constructor
represents original PhysicAttributes construction and initializes that owner's
currentJumpState to zero. It must be constructed once with the actor lifetime;
attaching the existing instance to RetailHeightController does not reset the counter.
There is no duplicated cached count. The owner must outlive the jump component, and
queries/snapshot updates are serial with the actor and physics update.

The native snapshot requires an actual four-vertex Box2D polygon and live typed
RetailPhysicsBodyData. Nonpolygons, another vertex count and absent metadata throw
explicit unsupported-binding errors before altering the old snapshot. The original
routine assumed those inputs and read four entries unconditionally. This validation
is an explicit native safety adaptation, not claimed malformed-input parity.

## GetJumpHeight contract

The direct original routine does not check enabled, and its slot argument is unused.
The native method preserves both facts. Calling it directly on the constructor's
zero quad can produce NaN and increment the counter. The ordinary wrapper checks
enabled before calling it, so ordinary construction does not expose that path.

First compute ((dx*dx+dy*dy)+dz*dz) relative to the stored four-point centre in
centimetres. It must be strictly below2250000, the squared1500cm radius. Equality,
farther distance and an unordered NaN comparison clear enabled and currentJumpState,
then return999999. Geometry and opaque flag remain unchanged.

Next test all four successive quad edges in XY. A strictly negative signed side
returns999999 without disabling or clearing the counter. Equality is included.
Consequently a near point outside the quad differs observably from a far point.
No epsilon, nearest height, ramp impulse or direction heuristic is added here.

For an accepted point, the routine computes the plane from A/B/C only. D participates
in the four-edge inclusion and centre, but D.z does not influence that plane. This is
also true for nonplanar quads and points near D; it is not bilinear interpolation or
a split chosen from query position. The denominator, reciprocal, two weights and
height expression preserve original single-precision operation order. There is no
additional determinant or weight-range guard after quad inclusion. Degenerate
floating behavior is retained rather than replaced with a constant plane.

Each accepted direct query increments currentJumpState once, even if the resulting
height is NaN or999999, and even if a caller subsequently retains a higher ordinary
height. Native increment copies the32-bit representation through uint32_t to preserve
original wrapping without signed C++ overflow. It does not infer airborne time or
animation state from that count.

Independent simple fixture: local metre polygon(-1,-1),(1,-1),(1,1),(-1,1), identity
body transform, metadata Z[10,50,130,900]. Centre is(0,0,272.5)cm. Its ABC plane is
z=70+0.2*x+0.4*y: centre height70, point(25,-25) height65, D XY(-100,100) height90
rather than D's stored900. All three are accepted with a nearby query Z.

## Actual terrain-wrapper composition

RetailHeightController accepts an optional pointer to the actor's existing typed
RetailJumpController. A null pointer means ordinary-only behavior. A host dispatching
jump surfaces from its contact filter must pass that same actor's live jump component
to its terrain controller. This is an explicit binding obligation; the existing
RetailContactFilter already rejects a required jump callback with no owner/handler.
There is no unconditional no-op callback or production claim.

The ordinary core and all its markers, selected geometry, body ownership and reseeds
remain unchanged. The wrapper's original order is:

1. Initialize sentinel cache from query, clear changed, and require selected terrain
   state before incrementing the shared generation and executing the ordinary core.
2. A core sentinel returns the current cache immediately. Reseeding inside that core
   may have changed the cache; neither reset nor jump evaluation occurs on this path.
3. After core success, copy query XYZ into cache. Only if jump is enabled, clear the
   live counter for slot zero immediately before evaluating GetJumpHeight. Nonzero
   slots accumulate the existing count. Disabled jumps do not clear it.
4. Select the jump height only if ordinary<jump and jump!=999999. Store the selected
   Z in the cache. Nonwinning/equal jump heights still have the original query's
   counter side effects. A far jump query can disable the snapshot while the wrapper
   correctly returns its successful ordinary terrain height.

This successful-core boundary matters: a previous cached jump height returned after
an ordinary miss is not a new successful ordinary result. The implementation calls
composition inside the original success branch instead of wrapping the public cached
height return value. An independently checked reseed fixture returns queryZ400 after
a core sentinel, retaining counter17 even though a separate jump query there would
return70 and increment it.

## Native and original validation

The automated tests use real native Box2D bodies, polygons, transforms, metadata and
world.Step. A real RetailContactFilter callback dispatches the actual owner identity
to setCurrentShape, then the same owner's count is observed by jump-height queries.
The component copies state during filtering without creating or destroying a body
inside the locked physics step. Body/filter and metadata lifetime requirements remain
explicit; malformed binding exceptions are not a supported step recovery boundary.

Fixtures cover identity/rotated/translated snapshots, independent ABC plane values,
nonplanar D behavior, all boundary vertices, near outside-edge failure, exact radius
and nextafter-inside distance, Z-dependent proximity, unordered radius, disabled direct
query, original zero-quad NaN, signed counter wrap, opaque byte0xa5, selection without
count reset, metadata/transform mutation, destruction after snapshot, rejected native
shape bindings, slot0/nonzero reset ordering, nonwinning/equal/sentinel jump heights,
unselected terrain, unchanged/changed cache after core misses, disabled composition
and total Box2D backing-allocation cleanup.

Ignored research harness port/build/og020_oracle.py executes original ELF machine code
for SetCurrentJumpShape and GetJumpHeight on135 deterministic cases: five actual
native rotations, three height sets and nine query variants. It compares exact
transformed quad/centre/result bits, copied raw flag, enabled state and signed live
counter. All135 match. Twelve composition cases execute the complete original ordinary
height core, original wrapper and original jump routine, comparing returned bits,
cache XYZ bits, changed marker, generation and jump state. All12 match. One additional
original reseed/core-sentinel fixture independently confirms the native expected400
cache result and unchanged17 counter. These are bounded fixtures, not a universal
behavioral equivalence claim.

Oracle interception is explicit: the prior OG015 array-backed mesh/node getters and
AEABI add/subtract/multiply/divide/comparison/unsigned-division/sqrt hooks remain.
The wrapper's synthetic single-node scene additionally hooks getChildren0x272a85 to
an empty list. UpdateEdgesCollisions0x31e99d records invocation only. Native cliff
creation/lifetime is separately tested by retail_height; the jump oracle does not
prove those intercepted operations or Android's full floating environment. Emulator
packages, ELF bytes, generated ARM views and scripts remain ignored research files;
the shipped implementation is ordinary native C++ and real x64 Box2D.

Build/test results and artifact hashes are recorded in the submitted ignored
OG-020-report.md and OG-020.patch. No matching-project edits, manual gameplay or
mission progression are used for validation.

## Remaining work

Authored jump-body producers still need their original vertex ordering, metadata
heights and opaque flag sources bound to decoded scene data. Existing approximate
rendering ramps do not establish that contract. The host must dispatch every relevant
owner to the correct existing jump and terrain components. Complete actor/controller
clock, jump-related animation, vehicle airborne systems, collision-listener side
effects, proximity helper IsPositionOnJump0x31a67d and other consumers of raw byte113
remain separate unimplemented branches. This component adds no guessed implementation
for them. Terrain's existing explicit12-body-capacity and original-world bounds
limitations remain unchanged.
