# Original actor collision and terrain contract

OG-012, investigated against native baseline 670e056 and the pinned retail ARM
reference. This is a controller prerequisite and a bounded source investigation,
not a completed collision port. No gameplay was performed. The corrected
category interpretation also applies to ACTOR_MOVEMENT.md and the cinematic VM.

## Evidence and naming

Addresses retain the Thumb bit used by config/functions.tsv. Matched means the
original catalogue marks recovered C++ as matching; this investigation did not
repeat an ARM matching build. Pending functions were inspected in their original
assembly and, where necessary, through ELF constants and GOT symbol targets.

| Original function | Address | Evidence inspected |
| --- | --- | --- |
| Character::initPhysic() | 0x002f4465 | Pending ARM, body/shape creation and metadata |
| Character::CheckDestination() | 0x002f2135 | Matched C++, output ownership |
| NPC::walkToLocation / runToLocation | 0x002fd7c5 / 0x002fd789 | Matched C++, movement masks |
| NPC::Stop() | 0x002fd821 | Pending ARM, low-bit clearing |
| NPC::Update(int) | 0x002ffe0d | Pending ARM, ordinary arrival continuation |
| LevelObject::canGoToPoint(point, hit, adjusted) | 0x00317fb5 | Pending ARM, queries, filters, output and adjustment branches |
| LevelObject::canGoToPoint(point) | 0x00318be5 | Separate pending overload; not interchangeable without review |
| PhysicInfo::Reset(b2World*) | 0x00319cd5 | Pending dependency |
| PhysicAttributes::ResetPhysicInfo() | 0x00319d65 | Matched C++ |
| PhysicInfo::PhysicInfo() | 0x00319dd1 | Matched C++ |
| ContactListener::Remove | 0x0031a5ed | Matched C++ |
| PhysicAttributes::GetJumpHeight(point, slot) | 0x0031a6fd | Pending dependency |
| PhysicAttributes::InitWorld() | 0x0031a951 | Matched C++ |
| PhysicAttributes::SetHeightAttributes(point, info) | 0x0031ad31 | Pending dependency |
| PhysicAttributes::PhysicAttributes(int, body) | 0x0031b29d | Pending ARM, slot allocation and pool ownership |
| PhysicAttributes::SetHeightAttributes(node, point, info) | 0x0031b3e9 | Pending dependency |
| PhysicAttributes::IntersectSegmentWithHeightMap(info, plane, start, end, hit, normal) | 0x0031b9a9 | Pending dependency |
| PhysicAttributes::CreateNeighborTris(node, id) | 0x0031cc69 | Matched C++, neighbour data dependency |
| PhysicAttributes::SetCurrentJumpShape(shape) | 0x0031ccf1 | Called by custom filter; stateful dependency |
| ContactListener::Persist | 0x0031d7a1 | Pending ARM, NPC contact retargeting |
| ContactFilter::ShouldCollide | 0x0031dc7d | Pending ARM, custom gate and vertical intervals |
| PhysicAttributes::AddCollisionShapes(slot, key) | 0x0031e285 | Pending ARM, cliff maps and local shape construction |
| PhysicAttributes::UpdateEdgesCollisions(slot) | 0x0031e99d | Pending ARM, local body destruction/recreation |
| PhysicAttributes::SetPhysicInfo(info, slot) | 0x0031eb15 | Matched C++, state copy and edge update |
| PhysicAttributes::GetHeight(slot, point, bool, bool) | 0x0031eb99 | Pending ARM, triangle/neighbor search; incomplete reconstruction |
| PhysicAttributes::GetHeight(point, slot, detailed) | 0x0031ff49 | Pending ARM, complete wrapper control flow |
| PhysicAttributes::GetHeightFrom(source, point, slot) | 0x00320041 | Matched C++, state transfer |
| PhysicAttributes::Init(region, surface, point) | 0x003200f9 | Pending ARM, initial height state and edge update |
| ContactListener::Add | 0x00320351 | Pending ARM, category dispatch and NPC/static branch |

The source files are games/gangstar-wch/src/game/characters/PhysicAttributes.cpp,
characters/Character.cpp, ai/NPC.cpp and world/LevelObject.cpp, with corresponding
asm/characters, asm/ai and asm/world files. Physics.h and PhysicsBodyDef.h are
partial Android ABI views, not usable native 64-bit Box2D class definitions.

LevelObject::Load(GameObjectBasic const&, factory) assigns objectType from
GetMetatype(). On the first load for that index, it assigns s_type[objectType]
from data.type and the pool size from data.poolSize. s_type is a 256-element
runtime classification table, initialized to -1 by the recovered setup code.
getObjectType() returns the byte at original LevelObject offset 36. Thus raw
metatypes, native factory record tags, and the resulting s_type category are
separate values. Do not globally rename numeric factory tags from these results.
The ELF's initial static table contents are not a classification of live objects.

Category 5 is NPC: Persist compares s_type to 5 at 0x0031d7ee and calls
NPC::getPathToFollow at 0x0031d806. Vehicle::notifyHitNPCofDespawn also uses 5
before accessing Character::lastHitVehicle. Category 3 is Vehicle: Add compares
3 at 0x003203d2 and enters its vehicle sensor branch at 0x003206a8; the matched
GameObjectList update also tests 3 for vehicle state. Category 8 shares the
character query branch with 5, and Add routes its category-8 branch through the
player's adjusted base pointer. Filter and character initialization additionally
recognize the player by PlayerTest::GetPlayer(), adjusted by four ARM bytes for
its LevelObject base. Native code must use real object identity, not reproduce
that pointer adjustment. Category 2 remains unnamed in this investigation.

For NPC StopObject, category 5 requires flags356 bit 0x4000 to be set. The
matched walk/run helpers clear it and set low bit 0/1 respectively. NPC::Stop
calls Character::Stop and clears only the two NPC movement bits; it does not set
0x4000. In NPC::Update, the destination-arrival branch at
0x00300504..0x0030056a compares horizontal squared distance strictly against the
stored squared limit and clears those bits. Its ordinary continuation tests
0x2000 at 0x00300674 and sets 0x4000 at 0x00300848 when that vehicle-related branch
is absent. Path/target/vehicle continuations still have additional behavior.
Inherited LevelObject::GetSpeed()==0 does not make an NPC script wait immediately
ready: categories 2/3, rather than 5, use that accessor in StopObject.

## World, actor body and owned terrain bodies

InitWorld constructs Box2D with bounds [-2000,2000] metres on each XY axis, zero
gravity and sleeping enabled. It enables position correction, disables continuous
physics, installs the game's ContactFilter and ContactListener, and creates the
shared ground body. Stock Box2D defaults alone do not implement these callbacks.
The independently investigated dependency is Box2D 2.0.1; upstream provenance and
solver parity belong to the separate physics dependency investigation.

Character::initPhysic returns immediately when the actor body already exists.
Otherwise it creates a one-slot PhysicAttributes object with supplied body NULL
when attributes are absent, then creates the body. The new body starts with
zero velocity; BodyDef angle, damping and explicit mass data are zero.
Position is scene XY divided by 100.
The final actor BodyDef disables sleeping: the initial true default at
0x002f44b6 is overwritten at 0x002f4502 before CreateBody. Fixed rotation, bullet
and initial sleeping flags are false. World-level sleep permission and actor
sleep permission must not be conflated. The circle has local centre (0,0),
radius 0.5 metres, density 1, friction 0.3, restitution 0, and is not a sensor.
CreateShape is followed by SetMassFromShapes; do not leave a mass-zero static
body or replace it with a kinematic transform.

Correction verified during OG-025 on 2026-11-25: the earlier 0.3-radius and
0.2-friction inference misidentified stack fields. Original initPhysic stores
the 0.5 literal (0x3f000000 at 0x2f461c) to CircleDef radius at sp+40 in
0x2f4552; 0.3 (0x3e99999a at 0x2f4624) goes to friction at sp+12 in
0x2f4562. Five independent original ARM argument captures cover player/NPC,
existing/new attributes and existing-body early return. Native shape definitions
must use their real fields, not these ARM offsets. Generic smaller-circle
collision fixtures are not actor-definition evidence.

The player category bits are 2; other characters receive 4. The mask is 0xffff,
and signed group is PhysicAttributes.index + 1. Character saves those filter
values at original offsets 50/52/54. Physics categories are another namespace:
category bits 4 do not mean LevelObject s_type category 4. PhysicAttributes
reserves an index from a 50-entry pool. Its constructor also stores its supplied
body pointer and slot count, allocates 140-byte ARM PhysicInfo records, clears
jump-enabled state at +112 and current jump state at +116. Allocation failure or
pool exhaustion behavior is not reconstructed here.

The body user data is an owned 28-byte ARM record. Character creation stores the
LevelObject owner at +0, its skin scene node at +4, zeros four floats at
+8/+12/+16/+20, and clears bytes +24/+25. The checked-in PhysicsBodyData header
only describes the first 16 bytes. A native record must explicitly represent all
observed fields and use native pointers; copying those offsets into a 64-bit
class would corrupt it. The first pair of floats supplies vertical bounds in
ordinary static records. Flag +24 selects the four-height/jump-surface branches;
flag +25 remains unnamed here.

Terrain collision is not simply every rendered wall triangle converted once.
PhysicAttributes::Init initializes the region/surface state and calls
UpdateEdgesCollisions. SetPhysicInfo copies the selected buffer/triangle state,
four cached vectors, marker and cached position, then updates edge collisions.
GetHeightFrom transfers the source slot's region/triangle/vectors/cache fields
when that source has valid state, then calls GetHeight with detailed=true.
ResetPhysicInfo calls each slot's Reset and sets both ground-state bytes true
only for slot zero. SetIsOnGround copies onGround to wasOnGround before storing
the new value; these are distinct pieces of state.

UpdateEdgesCollisions first rejects a slot whose onGround byte at +136 is false.
Otherwise it deletes user data and destroys/nulls each of that slot's 12 owned
bodies. It invokes AddCollisionShapes for the current triangle's three vertex
keys, then inspects the triangle-neighbor table and adds keys from eligible
neighbor triangles, excluding already-present current-triangle vertices. Original
buffer IDs and index offsets matter; a flattened native triangle number alone
cannot identify this state. The matched destructor similarly frees local body
metadata and bodies, then releases its pool index.

AddCollisionShapes uses s_cliffVertexKeyMap and s_cliffTriMap as well as
s_heightmap. Their GOT targets were resolved from the ELF, not named from a
visual guess. The function filters triangles, compares selected geometry with
slot position and body velocity, avoids duplicates across slot body arrays,
and builds local static four-vertex polygon shapes. The inspected construction
at 0x0031e830 creates ownerless/node-less 28-byte metadata with Z min/max at
+8/+12, flags false, and creates a static body. The final polygon filter at
0x0031e908..0x0031e91e has category 0, mask 0xffff and group index+1. Therefore
custom filtering can associate this terrain body with its owning actor even
though metadata.owner is null. Exact edge eligibility, width choices, all
neighbor transitions and other jump-shape producers remain required ports; this
investigation has not supplied a complete AddCollisionShapes implementation.

## Custom contact filtering

ShouldCollide has no call to the stock default filter. Its first gate is:

- Reject either shape when its category, group and mask are all zero.
- If at least one category is zero and both signed groups are equal, continue
  to metadata/height tests without the two mask intersections.
- Otherwise require both categoryB & maskA and categoryA & maskB to be nonzero.

There is no generic rule here that equal positive groups force every pair to
collide, or that equal negative groups force every pair apart. In particular,
a local category-zero cliff polygon reaches its matching actor through the
special group rule; equal group IDs for two nonzero categories do not bypass
masks. Preserve this distinction from the upstream Box2D default implementation.

After that gate, absent body user data on either side returns true. This is an
explicit original fallback, not authorization to omit metadata from all bodies.
With metadata on both sides, each side produces a vertical interval. For a
node-backed NPC or the explicitly recognized player, base Z is GetPosition().Z
minus Character's root Z at +156. Its interval is [base-25, base+150] centimetres.
Other node-backed objects use the node bounding box's min/max Z. Vehicle branches
also consult PhysicAttributes state +116 and can suppress a collision against
node-less geometry while that state is positive; those branches are not a
complete vehicle contract.

For node-less metadata with flag +24 set, min/max are taken across all four
height floats. For ordinary metadata, +8/+12 provide the bounds; additional
category and coordinate branches occur at 0x0031dffa..0x0031e094. When either
shape category is zero, this ordinary branch uses stored lower/upper bounds.
When both categories are nonzero, the inspected node-less side keeps its stored
lower bound only if its body XY is strictly inside X=(-450,-405), Y=(-560,-420)
metres; otherwise the lower bound becomes -999999. The upper bound remains the
stored +12 value. The second-shape branch applies the same conditions. The
meaning of that authored rectangle is unresolved, but its numeric tests are
observed behavior and must not disappear into a universal wall-height policy.

The normal overlap decision is strictly lowA < highB and highA > lowB. Exactly
touching vertical endpoints do not collide. Afterwards flag +24 can cause a
state-changing directional gate: dot the other body's XY linear velocity with
this body's transform column at +20/+24. A strictly positive result calls the
other owner's PhysicAttributes::SetCurrentJumpShape with this shape and returns
false. There is a symmetric second-shape branch. This is not merely a pure
height predicate, and it is not equivalent to unconditional wall sliding.

## Contact callbacks and their side effects

The original base b2ContactListener Add/Persist/Remove functions at
0x003196e9/0x003196ed/0x003196f1 are genuinely empty matched methods.
ContactListener::Result is also genuinely empty. The installed derived Add,
Persist and Remove are game implementations; substituting those base no-ops
would discard required behavior.

Remove reads both owners, ignores the pair when either owner's s_type is 12,
and otherwise decrements the owner vehicle's sensorContactCount for a sensor
against a non-sensor. Its exact code relies on sensor ownership established by
the constructors: it does not independently verify category 3 in this method.
Inventing a broader safe-looking cast policy is not the recovered contract.

Add includes projectile/explosion handling, vehicle sensor counts/horns,
vehicle collision responses, damage and event branches. For the actor/static
portion, it recognizes an unpathed NPC whose state at +428 is 0, 1 or 2. It can
rewrite Character's destination with limit 100 and reissue base run motion if
NPC flag 4 is set. The explicit category-8 player branch does not route through
that NPC retargeting branch. These guarded returns do not make Add globally
empty; active world categories must be dispatched or explicitly unsupported.

Persist returns when both owners are present or both absent. With exactly one
owner it only proceeds for category-5 NPC, state +428 in {0,1,2}, and no
NPC::getPathToFollow. At +296/+300/+304 it compares the cached vector to zero
using vector3::equals with epsilon 0x358637bd (approximately 0.000001). A zero
cache receives the contact normal from +24/+28 and Z=0, then returns.
A subsequent qualifying contact accumulates those normal components and computes a
new destination as current position + 500 * accumulated vector, with a
100-centimetre destination limit. If NPC flag 4 is set, it calls
Character::runToLocation on the stored destination. Both contact shape orders
have this behavior. These fields are collision response state, not a second
animation root sample or a time-based scripted movement duration. The contact
layout is verified against Box2D 2.0.1 b2Contact.h: shape1, shape2, position,
velocity, normal, separation, friction, restitution, id. Thus normal is at ARM
+24/+28 and velocity at +16/+20. b2ContactResult has a different layout. Native
code must use fields/accessors rather than copy either ARM layout.

## Height wrapper and destination adjustment

GetHeight(point, slot, detailed) is stateful. PhysicInfo.cached XYZ is at ARM
+64/+68/+72; 999999.0 is the exact uninitialized/no-result sentinel. The wrapper:

1. Initializes the cache from the query position if cached Z is the sentinel.
2. Clears the per-query marker byte at slot +60.
3. Returns cached Z without a core query if slot +4 is zero.
4. Otherwise advances a shared 16-bit query marker by four, then calls
   GetHeight(slot, point, true, detailed).
5. If that returns the sentinel, preserves the previous cached Z.
6. On success copies query XYZ, optionally evaluates GetJumpHeight when the
   PhysicAttributes +112 flag is set, chooses a greater non-sentinel jump
   height, and stores/returns the selected Z. Slot zero clears +116 before
   that jump query.

Thus a miss does not become zero ground, and selecting the nearest triangle by
absolute Z difference is not this contract. The 0x0031eb99 core reads the slot's
selected region/triangle, triangle vectors and neighbor tables. Reconstructing
its complete neighbor/edge search, detailed-mode side effects and transitions is
still necessary. The wrapper alone cannot supply a faithful terrain adapter.

The three-output canGoToPoint computes an XY Box2D AABB enclosing current and
requested positions, in metres. Its margin is 0.5 m for categories 5/8, 1.5 m
for category 3, and zero for the other inspected path. Query margin and circle
radius are separate parameters; the actor circle also has radius 0.5 m.
Query requests at most 100 shapes and uses
a zeroed 100-pointer buffer. It normalizes the full requested-minus-current 3D
vector and builds two oppositely offset XY segments from the resulting XY
components. A native 2D normalization would differ when destination Z differs.

The query's filtering is separate from ShouldCollide. Category-zero candidate
shapes require group == actor PhysicAttributes.index+1 and nonzero mask;
sensors, actor-owned shapes and body metadata with a null owner are skipped.
The category-zero/group/mask branch rejoins before those owner tests, so the
ownerless local cliff bodies described above are also excluded from this query;
they still participate in physical contacts through ShouldCollide. This
distinguishes steering obstruction queries from actual terrain collision. It calls each retained shape's TestSegment
with that body's transform and maximum fraction 1. Node-backed candidates have
vertical bounding checks, while node-less height intervals have equal-height
and differing-height branches. The earliest accepted fraction replaces the
previous one only under a strict less-than test. Shape-order/tie and the
100-candidate cap must therefore be preserved or explicitly assessed.

The first output is current + fraction*(requested-current), including Z.
The boolean result is fraction == 1 exactly. When obstructed, the third output
is a separate adjusted destination, calculated from the selected shape's
normal/vertices and metadata. Polygon-specific adjustment at
0x00318618..0x00318824, differing-height branches, and vehicle-relative branches
are substantial; constants include 50/75 cm as well as the query margin.
The entire adjustment formula and all degenerate cases are not yet recovered.
Replacing it with the first hit point or an arbitrary slide is not justified.

Character::CheckDestination owns the meaning of these outputs. On true it marks
destinationReachable and copies the original destination to adjustedDestination;
on false it marks unreachable and copies the third output. It does not change
the original destination. The actor faces/steers using adjustedDestination while
completion compares the original destination. No-hit third-output contents need
not be consumed by this caller. VM blocking cannot own this update: independent
actor updates and the subsequent physics step must continue during dialogue.

## Native reuse and smallest correct implementation boundary

The current cityTriangleHeight geometry calculation, decoded height triangles,
triangle adjacency and spatial candidate indexes are reusable data/query
building blocks, subject to preserving original buffer/triangle identity and
checking candidate coverage. They have not been proven to implement the core
GetHeight selection contract. CityTerrain::groundContact chooses the closest
absolute height within caller rise/drop bounds and optional preferred neighbors;
it lacks the original cache, markers, jump and local-body transitions.
CityTerrain::rampContact chooses the highest eligible ramp independently.
crossesWall slices triangle faces at max(startZ,endZ)+55 and uses distance and
crossing tests with caller radius. Those are current native approximations,
not original custom filtering, body collision response, or canGoToPoint.

A faithful first direct-destination controller can be restricted to an alive,
on-foot player/NPC without authored path, combat, vehicle interaction or jump
state, in explicitly supported original terrain. That scope still requires:

1. The real Box2D body/circle, original world settings and correctly owned
   native metadata; original filter and the reachable callback branches.
2. Original selected-triangle height state, initialization, local edge bodies
   and transitions. Port core GetHeight and AddCollisionShapes for the accepted
   terrain cases before claiming retail terrain parity.
3. Both offset segment queries and the selected blocker adjustment for accepted
   shapes. Unsupported blocker/terrain cases must be reported and prevent a
   parity claim, not silently pass through a no-op adapter.
4. The animation-root motion and update order in ACTOR_MOVEMENT.md, with real
   body integration, post-step position synchronization, original destination
   tolerance and NPC/player readiness words.

A synthetic flat, unobstructed fixture is a useful first independently tested
slice, with explicit initialized height state and actual empty-world queries.
It is not evidence that retail movement or collision is complete. The next
necessary bounded ports are core height/triangle state and cliff-body creation,
then polygon destination adjustment, followed by the actor-contact branches.
Vehicles, vehicle entry, bullets/rockets, combat damage, NPC paths/AI, jumps,
unverified rectangle-area behavior and unclassified factory categories remain
outside this first slice. Their absence must stay visible in status and dispatch.

## Acceptance cases and validation performed

These are required implementation acceptance cases, not tests executed here:

- Body initialization: player/other category bits 2/4; radius 0.5, friction 0.3, mass from
  density, friction/restitution, metre conversion, correct non-sensor group,
  disabled actor sleeping and
  independent actor pool allocation. Destroy/reset must release local metadata.
- Filter: all-zero rejection in either order; matching category-zero group with
  mask nonzero; mismatching group; both nonzero categories with equal groups but
  excluded masks; absent metadata fallback; exact vertical touching versus
  positive overlap; root Z subtraction; all four jump heights and both positive
  and nonpositive velocity-dot gates with the expected jump-state side effect.
- Terrain: two elevations at one XY, triangle-edge and neighbor transitions,
  uninitialized cache, slot without +4 state, core sentinel after valid cache,
  enabled/disabled jump query, slot-zero jump reset, onGround false edge update,
  and destruction/recreation of local cliff bodies. Validate region/triangle
  identities against supplied assets without requiring progression.
- Query: clear path returns true and exact destination hit; either offset ray
  blocked while the centre ray is clear; sensors/self ignored; actor-specific
  category-zero group/mask cases; equal fractions, fraction exactly one,
  >100 broad-phase candidates and destination with different Z. Blocker-specific
  adjusted destination needs independently derived expected positions, not
  expectations copied from an approximate native slide implementation.
- Contacts: NPC/static Add versus Persist, both shape orders, initial zero
  response cache versus subsequent accumulation, excluded +428 states, authored
  path suppression, flag-4 run reissue, player/static negative retarget case,
  and vehicle sensor add/remove balance in a separately supported vehicle slice.
- Controller: held VM dialogue does not freeze root motion/body stepping; an
  obstruction outside the strict arrival radius does not mark complete; exactly
  100 cm is not arrival; NPC walk/run clears 0x4000, ordinary arrival sets it,
  and NPC::Stop alone does not invent it. Preserve player teleport reset versus
  NPC placement retaining destination state.

Executed validation was read-only comparison of recovered C++, original ARM
branches, ELF/GOT names and literal values, plus inspection of current native
terrain queries. No new native implementation, compilation, automated runtime
fixture, matching ARM build, asset mutation or manual progression was performed.
In particular, this report does not claim the large pending core height,
collision-shape and adjusted-destination functions have been fully reconstructed.
