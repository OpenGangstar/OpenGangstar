# Original height search and local cliff bodies

OG-014, investigated against native baseline f5723dd and the pinned original ARM
library. This document extends ACTOR_COLLISION.md with implementation-level data
and control-flow evidence. It does not claim that native height search has been
ported or that every exceptional original branch has been reconstructed.

Only this document is changed. No gameplay, ARM edits or native controller edits
were performed. Addresses retain the Thumb bit used by config/functions.tsv.

## Native implementation update: OG-015

The ordinary no-jump backend is implemented in retail_height.h/.cpp and its
native subsystem test. It preserves original buffer/indexOffset identity,
mutable lane-7 generations, query/cache/reseed ordering and exact local cliff
strip construction with real native Box2D ownership. The investigation below
remains the source map; separate jump and point/info helpers are not implicitly
implemented by this component.

Independent integration builds and all eight CTests pass in Release and Debug.
A research-only execution of the original ARM height core matches all 12,998
supplied positive-area centroid result bits and selected identities. Its edge
update call is intercepted, so this oracle does not establish cliff-body parity.
Separate native actual-body/contact/proxy tests validate that backend.

Original world bounds remain +/-2000 m. Supplied geometry extending beyond
them produces 254 explicit selected-triangle refresh errors, with 16,330 actual
proxy instances in accepted cases. All 17,085 generated proxy instances build
in a separate test-only wider world. The native guard queries the actual world
using a staged nonvirtual InRange forwarding API and the native polygon's
OBB-based AABB; no production bounds, solver or assertions are changed.

Capacity beyond twelve strips and out-of-range polygons fail before replacing
old bodies. Query/marker/cache state can already have changed; these are not
resumable transactions. Reset or reinitialize from known state. Native metadata
outlives DestroyBody callbacks. One shared terrain context per world and borrowed
object lifetimes are caller invariants. Arbitrary boxes differing from decoded
vertex extrema, full actor Init, jumps, steering/controller binding and the
original out-of-world scene policy remain incomplete.

## Functions and evidence

| Original symbol or function | Address | Evidence used |
| --- | --- | --- |
| PhysicAttributes::LoadArrays(char*) | 0x00319ed5 | ARM allocation, read order and vector insertion |
| PhysicAttributes::SegmentsIntersect(a,b,c,d) | 0x003196fd | ARM degeneracy, determinant and inclusive parameters |
| PhysicInfo::Reset(world) | 0x00319cd5 | Pending dependency; lifetime boundary |
| PhysicAttributes::SetHeightAttributes(point,info) | 0x0031ad31 | ARM overlap traversal and recursive retargeting |
| PhysicAttributes::SetHeightAttributes(node,point,info) | 0x0031b3e9 | ARM scene/buffer/triangle traversal and state commit |
| PhysicAttributes::AddCollisionShapes(slot,key) | 0x0031e285 | ARM face, edge, width, filter and allocation rules |
| PhysicAttributes::UpdateEdgesCollisions(slot) | 0x0031e99d | ARM local-body destruction and ordered key enumeration |
| PhysicAttributes::GetHeight(slot,point,bool,bool) | 0x0031eb99 | ARM edge traversal, visit markers, overlap and result branches |
| PhysicAttributes::GetHeight(point,slot,detailed) | 0x0031ff49 | Complete wrapper control flow, also documented in ACTOR_COLLISION.md |
| PhysicAttributes::SetCurrentJumpShape(shape) | 0x0031ccf1 | ARM quad-to-world conversion and jump state |
| PhysicAttributes::GetJumpHeight(point,slot) | 0x0031a6fd | ARM bounds, inclusion, interpolation and state changes |

Matched C++ in src/game/characters/PhysicAttributes.cpp supplies GetTriCenter,
GetPathPos, CreateNeighborTris, SetPhysicInfo, GetHeightFrom, SetIsOnGround,
ResetPhysicInfo and destruction. Pending functions above have assembly bodies in
asm/characters. Existing nonmatching files did not supply a replacement height
search implementation. ELF GOT targets establish s_neighbors, s_overlapping,
s_cliffVertexKeyMap, s_cliffTriMap, s_heightmap and vector3::normalize/equals.

## Native inputs and original identities

The executable native inputs already exist as the decoded CityMesh heightmap,
CityNavigation buffers, materialBuffers, neighbor lanes, cliffVertexKeys,
cliffTriangles and ordered overlaps, plus the selected heightmap node's absolute
translation. Keep vertex order, buffer order, index offsets and overlap order.
No disk schema modification is needed for this investigation.

The original pair is (buffer, indexOffset), both unsigned 16-bit values. An
indexOffset selects the first of three consecutive mesh indices; it is not a
triangle ordinal or a byte address. The ordinal is indexOffset/3. Original mesh
buffer positions share the heightmap vertex identities. Transforming a triangle
adds the node's absolute position to each vertex. The inspected paths do not
apply its complete rotation/scale matrix. Replacing that addition with a generic
world matrix could change the original result.

Each s_neighbors[buffer] is eight pointers to uint16 arrays, one entry per
triangle. Lanes 0/1/2 contain neighbor index offsets for edges AB/BC/CA; lanes
3/4/5 contain their buffer IDs. 0xffff marks absent links. Lane 7 is a mutable
word whose low two bits are an authored classification and upper 14 bits are a
visit generation. Lane 6 is loaded separately but unused by the inspected height
walk. It is not safe to merge lane 7 with the separate byte-per-triangle array
called roadFlags in native code.

LoadArrays allocates these eight arrays and reads them buffer by buffer, then
reads the separate byte arrays. It reads the eight material-buffer indices,
two counted uint16 cliff maps, and per-triangle overlap lists. Each overlap
record is the original array<uint16> layout: data +0, allocated +4, used +8,
then flags. Each input pair is inserted as two successive words, buffer then
indexOffset. Therefore used counts words, not pairs or bytes. A native vector of
TriangleRef is a useful representation only if ordering and phase-specific loop
bounds are preserved.

PhysicInfo's observed ARM fields are:

| Offset | Meaning established here |
| --- | --- |
| +0 | Selected buffer ID, uint16 |
| +4 | Selected heightmap scene-node pointer |
| +8 | Selected triangle index offset, uint16 |
| +12/+24/+36 | World-space A/B/C, float3 |
| +48 | Normalized triangle normal, float3 |
| +60 | Triangle/state changed marker for the query |
| +64 | Cached previous query position, float3 |
| +76/+78/+80 | Additional cached identities transferred by GetHeightFrom; detailed meaning unresolved |
| +88 | Twelve owned local cliff-body pointers |
| +136/+137 | onGround / wasOnGround |

Use native types and named fields, never these ARM offsets as a native 64-bit
layout. Shared visitation state belongs to the terrain/query context, while the
selected triangle, cached point and cliff bodies belong to the actor's slot.

## Core height walk

For discussion, call the first bool C (the crossing guard) and the second bool D
(the argument called detailed by the public wrapper). These are semantic labels
for observed branches, not recovered source parameter names. The public wrapper
calls core GetHeight(slot, point, true, detailed).

On entry, the core snapshots A/B/C-derived XY differences, and updates the
current triangle's lane-7 word to (oldWord & 3) | s_currentUsed. The wrapper
increments the shared uint16 s_currentUsed by four before starting the core.
The candidate checks reject classification 1 or 2 and reject
(word & 0xfffc) == s_currentUsed. Classification 0 or 3 can be traversed.
The core does not repeat that classification rejection for an already-selected
seed before its inside-triangle calculation. Seed initialization is a separate
contract. No marker-array clearing at uint16 wrap was found in these functions;
wrap behavior and any external reset require a dedicated compatibility test.

Compute signed XY side values in AB, BC, CA order. For ordinary counterclockwise
vertices, sideAB is cross(B-A, point-A); the other edges are cyclic equivalents.
The edge-outside test is strictly <0, so points on an edge continue as inside for
that edge. The three large blocks start at 0x0031ec9e, 0x0031f23a and
0x0031f79a. They are ordered decisions, not a breadth-first search or a minimum-Z
candidate reduction.

When outside an edge:

1. If C is true, check whether the segment from cached point to new point
   intersects that edge in XY. If it does not, skip that edge's traversal and
   continue with the next edge decision.
2. Try that edge's direct neighbor. Reject 0xffff, classifications 1/2 and an
   already-visited generation. A valid neighbor replaces the selected pair and
   node with the global heightmap, fetches translated A/B/C and normalized
   normal, sets +60, and recursively calls core with the same C and D=false.
   Return that recursive result directly.
3. If no direct neighbor is accepted, inspect the saved source triangle's
   authored overlap list in order, with the specific loop bound described
   below. Unlike the direct-neighbor path, this phase writes the candidate buffer and
   index offset into info before its class/visit rejection (AB writes at
   0x0031ef5c and 0x0031ef72). A rejected candidate can therefore leave those
   identities changed. Accepted candidates fetch translated vertices; do not
   assume the direct-neighbor normal/changed-marker commit was also performed.
   The function can recurse when the cached-to-query segment
   crosses a candidate edge. Otherwise it checks all three signed sides and
   recurses into the first candidate containing the point, with C=false.
4. Exhaustion has a slot-zero branch that calls the node-taking
   SetHeightAttributes on the global heightmap. The adjacent continuation then
   resumes subsequent side decisions. Preserve that state mutation; this is
   not equivalent to returning the closest overlap height.

The direct-neighbor entry and commit for AB are at
0x0031ed0a..0x0031eef0; BC and CA have corresponding duplicated blocks. Normal
construction is the normalized cross product (B-A) x (C-A). Recursion after a
commit passes D=false. The out-of-line crossing-guard blocks are at
0x0031fac8, 0x0031fb0c and 0x0031fb28. Code generated as branches-with-link into
this function is part of the same control flow, not a separate exported helper.

The local overlap phase has a verified narrower bound: it loads used, subtracts
two, initializes word index j=0, and visits while j < used-2, advancing j by two.
For AB see 0x0031ef36..0x0031ef46 and 0x0031f1d2..0x0031f1de; BC and CA duplicate
this pattern. Thus this phase excludes the last stored pair, and skips a
single-pair list. This statement is limited to that phase. The point-taking
SetHeightAttributes helper visits the full used count, and later global reseed
paths can inspect additional triangles. It would be incorrect to label the
last pair globally unreachable or assume that it is a self/sentinel record.

Once all three side decisions fall through, +60 causes UpdateEdgesCollisions.
If +60 is false, onGround=true and wasOnGround=false also cause the update;
otherwise the body update is skipped. These tests occur before interpolation,
including a later interpolation failure. UpdateEdgesCollisions itself returns
without changing bodies while onGround is false.

The remaining math is XY barycentric interpolation. It forms the reciprocal
signed area, computes two weights and the remainder 1-weight0-weight1, tests
all three against the inclusive range [0,1], and combines original vertex Z
values. There is no query-Z proximity ranking or rise/drop cutoff here. There
is no observed 0.001 determinant guard matching cityTriangleHeight. Preserve
single-precision operation order when establishing numerical parity; malformed
or degenerate triangles need an explicit documented policy, not an invented
constant plane or epsilon.

On a failed weight test, slot zero or D=false calls node-taking
SetHeightAttributes(globalHeightmap, point, info), then returns 999999.0. A
nonzero slot with D=true returns the sentinel without that reseed. The reseed
can therefore change selected state while this call still reports failure.
The public wrapper preserves its old cached Z on that failure, as described in
ACTOR_COLLISION.md. Do not immediately manufacture a new successful height after
reseed unless the original caller actually retries.

The uncommon case of an exhausted edge search mutating info through the global
reseed and then continuing with already-computed local side differences needs a
literal control-flow implementation and independent fixture before parity is
claimed. This report establishes the calls and ordering, but does not claim a
simplified high-level rewrite of that mixed old/new-state continuation is safe.

## The two SetHeightAttributes overloads

The node-taking overload is a global seed search. It recognizes the two mesh
node types 0x6d656164 and 0x73656164; other nodes recurse through children in
list order. Recognized nodes first test XY against the node bounding box. The
inspected Z box test uses the midpoint of the box's own min/max Z, rather than
the query's Z. Mesh buffers are visited in ascending index order and triangle
index offsets in steps of three. Low classification 1/2 triangles are skipped.
It accepts the first triangle passing all three nonnegative signed XY side
tests, stores node/pair/translated A/B/C/normalized normal, sets +60 and copies
the supplied query point to the cached position. It then returns. This is not
a nearest-height global search; the query's Z does not rank triangles.

A mesh whose bounding check or triangle scan fails resumes child recursion.
The mesh-node scan, child order and first-match selection are observable in
stacked or coincident surfaces. A native global spatial index can accelerate
candidate retrieval only while reproducing this order and filter contract.

The point-taking overload is a different overlap-assisted search. It starts
from the current selected pair's full overlap list, skips classifications 1/2,
fetches translated candidate vertices, and tests edges AB/BC/CA against cached
movement when an outside-side test is met. A successful crossing changes the
selected node/pair and recursively restarts that same overload. An accepted
triangle stores the geometry/normal and changed marker, then returns its
interpolated height. Exhaustion returns 999999.0. It does not use the same
visited-generation guard or shortened overlap bound as core GetHeight.
Recursion termination on adversarial cycles and exact exceptional interpolation
behavior remain unverified; do not replace this helper by sampleGround.

SegmentsIntersect ignores Z, rejects a segment whose XY endpoints coincide,
and uses exact floating comparisons. With nonzero determinant, both line
parameters must be inclusively between 0 and 1. Its determinant-zero branch
returns true when both numerators are zero without a subsequent projected
interval test. Collinear disjoint segments consequently need an original-quirk
fixture; substituting a generic robust segment library changes that behavior.

## Exact cliff-strip construction

AddCollisionShapes(slot, vertexKey) reads a (start,count) pair from
s_cliffVertexKeyMap[2*vertexKey], then visits the corresponding s_cliffTriMap
entries in stored order. A 0xffff triangle entry is skipped. Faces come from
MATERIAL_TYPE_CLIFF's heightmap buffer. Find the requested key among the three
indices and rotate the face so A is that key, B the next vertex and C the
previous vertex. The original has a fallthrough read after an unsuccessful
three-index search; valid authored maps must therefore be verified to contain
the requested key rather than relying on that malformed-input behavior.

Translate A/B/C by the heightmap node's absolute position and normalize
N=(B-A) x (C-A). Eligibility first requires
N.x*(cachedX-A.x) + N.y*(cachedY-A.y) + N.z*0 >= 0.
Cached Z is deliberately absent. If PhysicAttributes has a supplied body pointer,
N.x*velocityX + N.y*velocityY + N.z*0 must also be <=0. Otherwise skip the face.
A null supplied body does not invent a zero scene height or bypass the remaining
edge checks.

Two incident edges are considered in order: B->A and A->C. For each, skip if the
corresponding edge's neighbor-buffer lane equals the cliff material buffer.
The first uses the rotated A index's lane, the second the previous vertex's
lane. This removes internal cliff-to-cliff edges by authored adjacency; it is
not equivalent to building every triangle edge or deduplicating by position.

For an edge with start S and end E, delta=E-S:

- Skip if any existing local body in any slot has body XY exactly equal to
  S.xy/100. Each slot scan stops at its first null entry. The key is this anchor,
  not both endpoints, triangle identity, orientation or height.
- Form n=normalize((-delta.y, delta.x, 0)). Require
  (delta.x*delta.x + delta.y*delta.y)/10000 > float(0x3bd1b717), whose value is
  approximately 0.0064. The threshold is strict, approximately an 8 cm edge.
- Require dot(N,n)>0 strictly.
- Width w is 10 centimetres when infoCount<=1, and 100 centimetres when
  infoCount>1. This width is independent of the actor's 0.3 m circle radius.
- Body position is S.xy/100, angle zero. The four local XY vertices, in order,
  are [-n*w, delta.xy-n*w, delta.xy, (0,0)] /100.

At 0x0031e830 the function allocates a 28-byte ARM metadata record with owner
and node null, lower/upper Z equal to min/max over all three face vertices,
other two heights zero, and flags +24/+25 false. It creates a mass-zero static
body with that user data. The polygon is non-sensor, density 0, restitution 0,
final friction 1, category 0, mask 0xffff, group PhysicAttributes.index+1.
The early ShapeDef friction default 0.2 is overwritten; use the final value.
Vertices are already in metres and include their body's local coordinates.
Do not apply world translation twice.

The first free local-body pointer receives the created body. If all twelve
entries are occupied, the inspected path still creates the body but has no
store before leaving shape-definition cleanup. No overflow destruction was
found there. Treat capacity exhaustion as a separately reviewed original
lifetime hazard; do not silently describe it as an enforced pre-allocation
12-body cap. A safe native divergence needs to be documented and tested.

UpdateEdgesCollisions returns immediately if slot.onGround is false. Otherwise
it frees metadata, destroys and nulls all twelve slot-owned bodies. It adds
cliff shapes for the current triangle's A/B/C vertex IDs in order, then visits
neighbor edges 0/1/2 in order. For a valid target pair it scans the neighbor's
three vertex IDs and calls AddCollisionShapes for a vertex absent from all
three current-triangle indices. This is an ordered regeneration, not a stable
all-world wall mesh. Destroying bodies can issue contact callbacks; retain
correct ownership and callback lifetime while rebuilding.

## Jump boundary

The filter's SetCurrentJumpShape receives a polygon whose metadata provides
four heights. It transforms its four XY vertices by the body transform, scales
XY to centimetres, pairs them with those heights, stores the quad at
PhysicAttributes +52/+64/+76/+88, and stores the mean of all four points at
+100. It enables +112 and copies metadata flag +25 to +113. The body filter's
strict positive directional gate is what selects this state, not the height
search discovering an arbitrary nearby ramp.

GetJumpHeight first requires squared 3D distance from the stored quad centre
strictly below 2250000 (a 1500 cm radius). On failure it clears enabled +112 and
current +116 and returns 999999.0. It tests all four quad edges in XY with a
nonnegative side rule; an outside edge returns the sentinel without the same
explicit distance-reset branch. The accepted path interpolates the stored
surface and increments +116. The public wrapper clears +116 before slot-zero
queries and selects the jump result only if it exceeds the ordinary successful
height and is not the sentinel. A core miss does not proceed into this jump
selection path. Full nonplanar quad interpolation and all producers of jump
metadata remain outside this alive/on-foot/no-jump first slice.

## Supplied-asset validation

The following checks ran directly against the user's ZIP without extracting or
modifying retail assets:

| Asset | Result |
| --- | --- |
| nav_mesh_arrays.bin | 446284 bytes; SHA256 1a35e730a252496cde98b17559128eebde528d9498c653af4dfc8ce40ba20dc4 |
| santa_monica_heightmap.bdae | 201348 bytes; SHA256 4d82e8e58fe60113d470c6ad8e0483d9ba82c16928d06892c3fa67b83edb7252 |
| Height mesh | One mesh, 7271 positions, stride 12, eight buffers |
| Index counts in buffer order | 6009,5757,10488,15156,2964,2934,183,237 |
| Navigation | 14576 triangles; all buffer index counts match BRES |
| Direct links | 43621 present neighbor links; zero invalid target pairs |
| Vertex indices | Every mesh index is within the 7271-position table |
| Material buffer order | road=1, sidewalk=0, park=5, buildingBase=2, intersection=4, cliff=3, water=6, deathWater=7 |
| Cliff maps | 14542 key words, 15156 live face references, zero sentinels, maximum 14 faces per key; zero bad ranges/offsets or keys missing from referenced faces |
| Lane 7 low bits | 12998 zero, 1104 one, 474 two; no three; high bits initially zero |
| Lane 6 | All 14576 entries zero |
| Overlaps | 10112 empty, 4464 nonempty, maximum 48 pairs; none contains its own source pair |

The asset observations establish concrete inputs, not runtime parity. For
example source (buffer=0,indexOffset=27) has a one-pair overlap [(3,8475)],
so the shortened core phase skips that candidate; the full overlap helper does
not. Source (0,72) has six ordered candidates
[(3,420),(3,474),(3,453),(3,507),(1,1677),(1,1713)]. No self-last interpretation
is supported by these assets. The array loader and phase bounds, rather than
an intended graph interpretation, determine the compatibility behavior.

A real sloped seed is (0,0), vertex IDs [0,1,2], with Z values
[-273.0469970703125,-273.0469970703125,-169.64999389648438]. Its local centroid
is approximately (2133.7467448,21503.9335938,-238.5813293), classification zero.
With explicitly initialized seed state and the correct node translation, a
centroid query provides a numerical interpolation fixture without progression.
It is not evidence that a global seed search must choose that same triangle.

## Smallest faithful implementation and independent expectations

Use a typed per-slot state and a shared mutable terrain generation context over
the already decoded original IDs. Port the core's inside-selected-triangle path,
wrapper cache/sentinel policy and body-regeneration trigger first. Then port
ordered neighbor recursion, overlap phases, explicit reseeding and cliff strips.
No nearest-Z adapter, constant plane, arbitrary wall slide or silent unsupported
branch is an acceptable completion of this boundary.

Independent synthetic fixtures should include:

- A=(0,0,10), B=(100,0,30), C=(0,100,50), point XY=(25,25): expected Z=25 from
  the plane z=10+0.2*x+0.4*y. Different supplied query Z must not change it.
  Edge point B is included; a point just outside AB enters traversal. Reverse
  winding, zero area and marker wrap require explicit separate expectations.
- A square split into two authored triangles: crossing each of AB/BC/CA selects
  the corresponding exact (buffer,indexOffset), preserves flags, sets +60 and
  refreshes local bodies in the documented order. Repeat with missing links,
  low class 1/2, already-visited target and a crossing guard that rejects the
  movement segment. Shared-edge equality is an inside case.
- Ordered overlap lists with zero/one/two pairs and a useful final pair, proving
  the shortened phase versus full helper. Two stacked valid surfaces prove
  selection follows seed and order rather than query-Z closeness.
- Cached Z before a core sentinel; missing selected-node state; slot zero versus
  another slot with D=true/false; changed-marker and onGround transition cases.
  Verify state mutation and returned failure independently after reseeding.
- Cliff A=(100,0,0), B=(0,0,0), C=(100,0,100), requested key A, no internal
  cliff neighbor, cached XY=(50,10): the B->A edge has n=(0,1). A one-slot actor
  produces body position (0,0), local polygon [(0,-0.1),(1,-0.1),(1,0),(0,0)]
  metres and metadata Z=[0,100]. Multi-slot width produces Y=-1. The other
  incident edge has zero XY length and is rejected. A negative cached Y fails
  the face-side gate; positive normal velocity rejects when a supplied body
  exists. No supplied body does not apply that velocity rejection.
- Exact edge-length threshold, dot=0, a cliff-to-cliff neighbor, duplicate anchor
  with different end/height, first-null slot scan, twelve entries full, false
  onGround retaining existing bodies, and contact Remove during destruction.
- Segment intersection at endpoints, degenerate first/second segment, parallel
  noncollinear and collinear disjoint lines. Preserve the documented exact
  original branch behavior rather than mirror a replacement helper's result.
- Jump near/far distance boundary, inside/outside quad, +116 count/reset,
  flag +25 propagation and sentinel selection. Nonplanar interpolation requires
  separate original-derived numeric expectations before that branch is enabled.

The remaining implementation blockers are the mixed old/new-state reseed
continuations, exceptional/degenerate math, marker wrap and external resets,
point-overlap recursion termination, full nonplanar jump interpolation, and the
original full-capacity allocation hazard. The ordinary one-slot/no-jump data,
edge construction and ordered traversal boundaries above are concrete enough
for a reserved native implementation with independently reviewed fixtures.

Validation performed here was read-only ARM/C++/ELF comparison, literal decoding,
complete navigation parsing, BRES height-mesh enumeration, complete cliff-key
incidence/range checks and independent slope/strip fixture arithmetic. No new native runtime fixture, build, matching ARM compilation or
playthrough was run. Whitespace/address/patch checks accompany submission.
