# Original height attributes and scene initialization

OG030, baseline330d2de. The original scene and point helper boundaries are implemented
in retail_height.h/.cpp, with isolated native and original-backed tests. The two
overloads remain distinct; neither is a nearest-Z query. Complete production scene
loading and actor initialization remain explicit host prerequisites.

## Original entry points and wrapper boundary

| Function | Thumb address | Responsibility |
| --- | --- | --- |
| SetHeightAttributes(point,PhysicInfo*) | 0x0031ad31 | Full ordered overlap traversal, recursive pair retargeting and extrapolated plane height |
| SetHeightAttributes(node,point,PhysicInfo&) | 0x0031b3e9 | Ordered scene-node/buffer/triangle selection and state/cache commit |
| GetHeight(slot,point,crossing,detailed) | 0x0031eb99 | Existing ordinary core; calls scene helper at exhausted-edge/result failure sites |
| GetHeight(point,slot,detailed) | 0x0031ff49 | Cache/generation/core/jump wrapper; no implicit scene selection for an absent selected node |

The wrapper first replaces a sentinel cached Z with the complete query point and
clears changed. With a null selected node it returns the cache immediately. A
nonempty global scene does not change that branch. Core scene reseeds are separate
calls at0x31f236,0x31f796,0x31f8b0 and0x31fe64; native firstSeed now follows the
configured global heightmap's real ordered child view. No lazy initialization is
added to the absent-node wrapper branch.

## Node overload: ordered scene selection

Recognized mesh node types are exactly0x6d656164 and0x73656164. Unrecognized nodes
skip mesh processing and visit their children. Recognized nodes use the node's raw
bounding box: query X/Y must be inclusively within its XY bounds. The Z check uses
(box.minZ+box.maxZ)*0.5 against those same bounds; query Z is not used. A translated
triangle can therefore contain a query whose raw-box test rejects it. An overflowing
finite midpoint or unordered comparison follows the literal failed-box branch.

After the box gate, scan buffers in ascending index order and each buffer's index
offsets0,3,6,... . Reject lane7 classifications1 and2 only; upper visit-generation
bits do not exclude this global selection. Fetch A/B/C in stored order and add that
node's absolute translation to every coordinate. No full rotation/scale matrix is
applied by this helper. Require all three signed XY sides not to be negative.
The first accepted triangle in this node commits selected node, buffer/indexOffset,
translated geometry, normalized normal, changed=true and the complete query cache.
It returns without visiting that node's children. It does not refresh cliff bodies.

When this node's type, box or complete mesh scan fails, recurse through children in
the original linked-list order. The parent continues with every sibling after a
recursive call: a later matching sibling overwrites an earlier match. A later miss
does not clear an earlier match. This differs from a globally first-match tree walk.
With no successful match, the incoming info fields remain unchanged.

The original calls getChildren0x272a85 on each loop condition; that function returns
the actual child-list field at node+240. Native data must preserve ordered hierarchy
and stable identities, without embedding those ARM offsets or inventing an empty list.

## Point overload: full overlaps and recursive retargeting

Read the full overlap list for the info record's current (buffer,indexOffset) pair.
The stored used count is in uint16 words and advances by two. The final pair is
included, unlike the existing ordinary core's shortened local-overlap phase. Empty
or classification-exhausted lists return999999 without a fabricated height.
Only lane7 low classifications1/2 reject candidates; no visit generation is used.

Candidates are fetched from the global heightmap mesh and its absolute translation,
even when the incoming info's selected node is a different scene node. For each
negative signed side AB,BC,CA, test the original cached-to-query segment against that
edge using original SegmentsIntersect. A successful crossing changes only selected
node/pair to the global candidate, then recursively restarts this same point overload.
The new pair's own complete overlap list is read. A recursive miss can leave that
new identity alongside the preceding geometry/cache. No implicit cache refresh occurs.

When a negative-side segment test fails, processing continues to the next edge;
it does not reject that candidate. Thus a candidate outside query XY can still be
accepted when none of those segment tests crosses. Acceptance commits pair/node,
geometry, normalized normal and changed=true, then returns the original ABC plane
expression. There are no barycentric-range guards and query Z does not rank a plane.
The info cache remains unchanged on acceptance as well as on recursive transitions.

The original recursive helper has no generation/cycle guard. A separate research
probe records three identical recursive entry states on a self-overlap outside-edge
crossing and explicitly stops execution; it is not counted as a successful returned
fixture. The native implementation records each entered pair during this call and
throws when a pair repeats. With fixed query/cache, immutable mesh/navigation and a
node view read-only throughout the query, this repeats the same original helper
state. There is no arbitrary iteration cap or fabricated height fallback. Earlier
identity retargets may already be visible; reset/reinitialize from known state before
another query. This is an explicit safety adaptation, not original termination parity.

## Mixed node sources inside the ordinary core

Multi-node selection also exposes the core's literal geometry sources. On entry to
an outside-edge search it captures the current selected node's absolute translation.
A direct neighbor then changes selected identity to the global heightmap, fetches
the global mesh vertices, and adds that previously captured translation. It does
not replace the captured translation with the new global node's translation.
The local overlap phase retains the selected node and fetches that node's mesh,
again using the edge search's captured translation. Classification rejection can
already have changed the pair without changing the node or old geometry.

Global scene reseeding can change node, geometry and cache during an exhausted
edge search while scalar interpolation differences still belong to the preceding
triangle. Both the existing mixed-state behavior and these mesh/translation sources
must remain explicit; a generic fetch-from-current-node rewrite changes results.
Original executable fixtures confirm a direct neighbor returning215 from global
vertices plus an old node Z translation200 even though the new global node's own
translation is50. A complete wrapper/core child-reseed fixture selects a flatZ200
last sibling but returns215 from the retained original scalar differences.

## Data and ownership prerequisites

Existing CityMesh supplies vertex/buffer/index order, and CityNavigation supplies
global classifications, neighbors and full ordered overlap pairs. Preserve selected
node identity separately from the global canonical heightmap used by overlap and
cliff paths. Child meshes must be valid against the shared navigation index namespace;
runtime-node type, raw bbox and absolute translation must be genuine host data.
Mesh/navigation storage and all referenced native scene records must outlive the
terrain context and any controller retaining their identities. No raw ARM node/array
pointers become native metadata. Original CreateNeighborTris retains the first
recognized global heightmap node; subsequent helper info assignments do not grab
individual selected nodes. Native borrowed records replace that ARM reference-count
mechanism with an explicit owning-host lifetime requirement.

CityScene retains source bytes, ordered parent relationships and local transform
data. The current decoder does not expose the raw SMesh bbox or runtime node type.
Those values must not be guessed from generic nodes. A typed scene adapter can
require genuine supplied fields; a later decoded-scene binding remains explicit
until loader/runtime-type and transform composition are proved.

## Native API and read timing

RetailHeightSceneNode contains only the actual type tag, CityMesh pointer, raw bbox,
absolute translation and ordered child pointers. Global scene topology, type and mesh
identities stay fixed after context validation. Bounds and absolute translations
are live host values across calls; the host may update them between serial queries.
The complete view is read-only during a query, including callbacks from cliff refresh.
The context never freezes supplied node bounds or translations at construction.

The existing mesh-only RetailHeightTerrain constructor preserves its bounded legacy
single-node contract: a native recognized mesh view with supplied translation and
vertex-extrema box. A second constructor accepts the genuine global heightmap view;
it validates mesh/navigation compatibility, missing metadata and cycles before use.
Validation cannot prove external pointer lifetime. Recognized child meshes may have
fewer buffers/indices than the canonical mesh when their classification entries
exist; an encountered unavailable triangle is an explicit unsupported binding, never
silently skipped. Raw box comparisons retain their own unordered/overflow behavior.
Original malformed native-memory reads are not reproduced.

RetailHeightState now retains a full-width selectedNode identity alongside its
existing selected flag and geometry. initializeSelected preserves the existing
explicit pair path and uses the current global node translation. New methods are:

- initializeFromScene(slot,point): execute the node helper from the global heightmap.
- initializeFromNode(slot,node,point): validate and execute an explicitly supplied
  start subtree, including nonmesh containers; any retained selected record is borrowed.
- setHeightAttributesFromPoint(slot,point): execute the separate full-overlap helper
  and return its plane/sentinel result while preserving the original cache behavior.

Scene selection reads each tested node's current raw box, then its absolute
translation per buffer scan. Point-overlap selection captures the current global
translation on each original recursive-entry iteration. The ordinary core captures
the current selected node translation at entry to each outside-edge search, before
changing a direct-neighbor identity. Cliff generation continues to read canonical
global geometry and its current translation. These sources are not interchangeable.
The node helper alone does not call UpdateEdgesCollisions. A grounded native fixture
with eligible geometry proves it creates no body until explicit edge refresh, which
then creates the actual Box2D cliff. No placeholder body/scene/owner callbacks exist.

The supplied santa_monica_heightmap.bdae is201348 bytes, SHA256
4d82e8e58fe60113d470c6ad8e0483d9ba82c16928d06892c3fa67b83edb7252.
Direct read-only ZIP inspection finds one height mesh,7271 vertices,eight buffers.
Its serialized SMesh+20 bbox equals vertex extrema bit-for-bit:
(-39520.19921875,-31623,-3246.300048828125) to
(224697,96336.6015625,4889.919921875). The source scenes contain138 top-level nodes,
no nested child records, and one ground_height01 mesh instance. That source node's
local translation is(-34649.3984375,-40383.80078125,745.8629760742188), identity
rotation/unit scale. These source observations alone do not prove its final loaded
absolute position; production scene initialization remains the host's responsibility.

## Original executable research

Ignored OG-030/initialization_oracle.py executes19 bounded original
fixtures with actual getChildren code and real nonempty linked child lists. It
captures last-successful-sibling overwrite, parent-success child skipping, unrecognized
containers, raw box versus translation, class1/2 filtering, absent-node wrapper
cache-only behavior, final overlap inclusion, accepted outside-plane extrapolation
and recursive retarget/cache retention. The recursive fixture verifies two actual
point-helper entries, and a destination-empty case verifies partial identity with
old geometry/cache after failure. Additional complete-core fixtures verify the
mixed node-source, live-property and nonempty-child reseed behavior described above.
Every returned call verifies the expected return sentinel. AEABI and synthetic
mesh/node getters are intercepted; no empty-children
hook is used. This does not prove complete scene loading, cliff refresh or Android's
full floating-point environment. No emulator is a native runtime dependency.

The full supplied asset corpus additionally executes all14576 point records against
original SetHeightAttributes(point,info):4464 accepted, including498 accepted final
pairs, with zero result/selected-pair/normal/cache-bit differences. Sixteen queries
using the source height node's explicitly supplied placement and actual serialized
box compare the complete original scene helper, also with zero differences. Release
and Debug native dumps agree across all14592 records. The original mesh node's real
empty child field is represented for those asset cases; separate nonempty hierarchical
fixtures exercise actual getChildren code without an empty-list hook.

Existing ordinary checks remain12998 eligible centroids and16330 actual accepted
cliff proxy instances with254 explicit original-world range errors, and17085 actual
proxies in the test-only expanded world. These tests enumerate assets without
gameplay progression. Exact configuration/gate commands and hashes accompany the
ignored OG-030-report.md submission.

Full native x64 builds and all13 CTests pass in Release and Debug, including the
existing ordinary terrain, jump, contact/filter, steering and Box2D fixtures.
Both Vulkan menu smoke tests exit0; they do not instantiate the new production
scene bindings or establish gameplay parity. No manual missions or playthrough
are used.

## Remaining production boundaries

The host still must provide the actual loaded global heightmap identity, runtime
node tags, raw bbox and absolute positions, and retain the decoded mesh/navigation
and node storage. Generic CityScene local transforms are not silently promoted to
the final loaded hierarchy. Dynamic mesh/topology replacement requires a new valid
context; only the documented live bbox/translation fields may change between calls.
This slice does not port GS3DStuff scene construction, full Character initialization,
scene ownership/reference counting, vehicle transfer or the actor/game-loop binding.
The existing on-foot body, jump and terrain components remain independently tested
prerequisites. Existing local-cliff capacity and original world bounds stay unchanged.
