# Original height-scene loading and native binding

OG035 supplies the static default-factory scene prerequisite for the already
implemented height controller. It constructs an owning, stable native node graph
from the authoritative decoded CityScene, retains the original global mesh identity,
and exposes genuine raw bounds and loaded absolute translations. It does not add
a nearest-Z query, renderer transform rewrite, actor animation graph or gameplay loop.

## Original load and construction chain

| Original function or instruction | Address | Observed contract |
| --- | --- | --- |
| GS3DStuff::load(int,int&) | 0x002dfb15 | Height resource load stage |
| Height constructScene call | 0x002dff0e | Literal ./santa_monica_heightmap.bdae, DefaultFactory |
| CreateNeighborTris call | 0x002dff16 | Select first recognized height mesh before LoadArrays |
| CColladaDatabase::constructScene(path,factory) | 0x00297ddd | Resource manager load, database wrapper, scene construction |
| CResFileManager::get(path,bool) | 0x002aea69 | Resource filename identity retained |
| CFileSystem / CCustomFileSystem::getAbsolutePath | 0x001d37d9 / 0x002d6885 | In this fork both assign the input string unchanged |
| CResFile::CResFile(path,file,bool) | 0x002ad4b9 | Filename string at +16 used by mesh runtime type |
| constructScene() / constructVisualScene() | 0x00296dc1 / 0x00296cf1 | Root, active visual containers, ordered SNodes |
| DefaultFactory::createScene / root C1 | 0x0029d215 / 0x0029d135 | Genuine identity root scene |
| CRootSceneNode::onPostLoad | 0x00293da9 | Recursive absolute update, bounds, skin/camera/particle attachment |
| constructNode(SNode*,root) | 0x0029692d | Instance children precede authored SNode children |
| DefaultFactory::createNode / CSceneNode C1 | 0x0029d25d / 0x002aee4d | Raw SNode TRS, distinct UID/display fields |
| DefaultFactory::createMeshNode / mesh-node C1 | 0x0029d3b1 / 0x002a3079 | NULL SNode, identity local TRS |
| constructGeometry(SGeometry*,root) | 0x002924b5 | Geometry kind0 constructs mesh; other kinds return NULL |
| getLight / light lookup / constructLight | 0x00296879 / 0x00296845 / 0x00293ec5 | Exact local name, NULL lookup creates no light |
| updateAbsolutePosition / relative / mult34 | 0x00273141 / 0x002738b1 / 0x00272c0d | Original hierarchy and literal float operation order |
| PhysicAttributes::CreateNeighborTris | 0x0031cc69 | Ordered traversal retains first recognized mesh |

The height load factory pointer is GOT+0x4940, the original DefaultFactory at
0x004b6460. This is not CGameFactory. SNode+76 selects the alternate billboard
factory and is explicitly unsupported here. The separate CGameFactory user-property
billboard test is not applied to this default height path. Native regression coverage
retains that distinction. The default OptimizeSceneNodes value is false; alternate
optimized construction modes are not silently supplied by this component.

The original post-load absolute update happens before CreateNeighborTris. The
supplied static resource has no animation/controller/camera/particle records that
would require the subsequent attachment branches. No root transform mutation occurs
between this construction and the height-selection call. Identity root placement is
established by actual constructors and this caller chain, not guessed from global zero.

## Decoded data and actual runtime node identities

The existing asset reader decodes raw SMesh+20's six bbox floats once into trailing
CityMesh.rawBoundsCm. Original CColladaMesh construction copies those exact words
to mesh+44 at0x002a10b6..0x002a10cc; its getBoundingBox0x002a0e75 returns that box.
The mesh-node bbox getter0x002a2685 delegates to the mesh. No vertex-extrema or
transformed-AABB replacement is made. Missing genuine box metadata is an error.

The reader also retains active visual-scene preorder ranges, SNode+76, ordered
instance records, exact local light UIDs from SCollada+44's24-byte SLight records,
and the original geometry-kind1 spline rejection. These are native decoded fields;
no second production BRES parser or raw ARM pointer layout is introduced.

| Actual runtime node | Tag | Original getter/vtable evidence |
| --- | --- | --- |
| Root scene | 0x72656164 | getType0x0029c319, ISceneNode thunk0x0029c304 |
| Visual or ordinary SNode container | 0x6e656164 | CSceneNode::getType0x002aed7d |
| Default collada mesh | 0x6d656164 | CColladaMeshSceneNode::getType0x002a27b1 |
| Special-path empty mesh type | 0x79746d65 | Same getter, database filename predicate |
| Collada light | 0x7468676c | Actual inherited CLightSceneNode::getType0x00258c3d |

The mesh getter checks case-sensitive database-path substrings car_light,
light_park, light_street, stage_light and sign. Any match returns the empty tag;
otherwise it returns the ordinary mesh tag. It applies only to the ordinary mesh
class: the skinned scene-node getter at0x002b255d instead returns fixed0x73656164.
The jump collector rejects unported node/controller/modular-skin factories before
any library-wide ordinary filename skip, including sign-marked controller inputs.
It does not inspect the mesh UID, SNode UID, raw metatype or LevelObject::s_type category. Vtable slots in the
original ELF confirm these exact class getters. Raw game metadata namespaces
remain separate.

Generated geometry children use the mesh UID because their SNode pointer is NULL.
A source container's UID at SNode+0 and display name at+4 are different fields.
Position/quaternion/scale at+12/+24/+40 pass unchanged through the original setters.
Geometry children have identity local TRS but still execute literal
mult34(containerAbsolute,identity). The extra additions change signed-zero bits.
Both current/placement jump transforms and this scene graph retain that operation.

## Owning native API and query contract

RetailHeightSceneBinding owns CityScene by value and independently allocated native
node records. Decoded meshes and child-view identities remain stable for its lifetime;
copy/move is disabled. root(), globalHeightmap(), source() and absoluteTransform(view)
expose read-only state. The binding outlives every terrain/controller borrowing it.
Construct for a fresh world's global selection and retain it; creating another
binding is not an implementation of original global unload/reference-count behavior.

loadOriginalHeightScene loads the exact original resource through loadCityScene.
A genuine globalHeightmap() can construct RetailHeightTerrain directly. Navigation
and the actual Box2D world remain separately owned host inputs. Static source
meshes use their raw bbox and actual inherited absolute translation. The height
backend retains its original translation-only vertex query, including its raw-box
XY gate; this binder does not rotate vertices into a different query system.

Construction preserves root -> visual -> SNode -> generated instance child order,
including nested SNodes. CreateNeighborTris retains the first recognized mesh even
while traversing later siblings. That global choice differs from the height helper's
last-successful-sibling state overwrite when explicitly started at a larger subtree.
Native tests exercise both identities without conflating them. The actual helper's
null selected-node cache behavior is unchanged; binding a graph does not implicitly
initialize an actor or invent a height callback.

Geometry-kind1 spline instances and missing light references follow proven original
NULL-construction branches. Valid lights retain their genuine nonmesh type and
ordered place in the graph; no physics object or constant-height function is added.
Bounds in the borrowed height view are meaningful for its mesh nodes; nonmesh
records take the original tag-gated child traversal and do not query a fabricated box.

## Shared original quaternion operations

The same module now exposes retailSceneYawQuaternion(float) and
retailSceneNormalizeQuaternion(array<float,4>) for the existing jump producer and
the separate heading implementation. These lift the already verified Euler
set(0,0,z) at0x002185c9 and normalize at0x001c8d11 without a general quaternion
rewrite. Euler trig/products remain double before float components. Normalization
keeps the left-associated float sum of squares, exact length!=1 branch, sqrtf,
float reciprocal and component multiplications. Exact-unit signed zeros survive.
A zero quaternion keeps the original arithmetic's nonfinite result rather than an
invented identity fallback; no NaN payload or arbitrary Android-libm parity claim
is made. Eighteen finite direct original comparisons supplement all jump fixtures.

retailSceneRelative still consumes the raw authored quaternion without calling
this normalizer. The helpers are explicit operations and do not refresh an actor's
cached absolute matrix or implement facing/heading state. Those caller ordering
contracts belong to the separate actor controller.

## Supplied resources and native validation

The height resource SHA256 is
4d82e8e58fe60113d470c6ad8e0483d9ba82c16928d06892c3fa67b83edb7252.
It constructs141 nodes: root, one visual container,138 SNode containers and one
ground mesh. Five spline references return NULL. The ambient-environment-light
reference has no matching SLight record (the table is empty), so it also returns
NULL; no142nd light node is manufactured.

The selected ground_height01-mesh has7271 vertices and eight buffers. The raw box
is(-39520.19921875,-31623,-3246.300048828125) to
(224697,96336.6015625,4889.919921875). The actual loaded absolute translation is
(-34649.3984375,-40383.80078125,745.8629760742188)cm, established by complete
hierarchy construction/update evidence. It is not substituted with a zero global
placement or inferred solely from the source local translation.

All12,998 eligible source centroids pass native terrain queries with that real
placement. Native enumeration creates17,086 actual cliff body instances, at most12
per slot, with no range/capacity/frozen failures in the original world bounds and
complete teardown. This is native enumeration evidence, not a claim to have executed
a full original cliff-body oracle. The old test that explicitly supplies zero
translation still reports its unchanged16,330 bodies and254 range errors (or17,085
proxies in its test-only expanded world). Those input contracts are different.
Original AddCollisionShapes adds global translation before its subtractions/normal/
shape gates, so float ties may also differ; no geometry normalization was added.

Unit fixtures cover raw bbox distinct from extrema, raw-box rejection despite
translated vertex containment, nonidentity hierarchy and exact signed zeros,
light/mesh ordering, stable owning storage, first global versus later helper
selection, exact type predicates and explicit unsupported/missing metadata.
The authoritative animation-catalogue prerequisite was split and integrated as
OG039; its already merged hunks/test are excluded from this scene submission.

## Original executable proof and limits

Reference-only og035_scene_oracle.py independently reads the supplied raw resource,
executes actual original recursive update, class type getters, getChildren and
CreateNeighborTris over genuine linked child lists, and compares all141 native
matrix/type/bbox/global-identity records exactly. It executes the real geometry
rejection and empty light lookup, the constructor's six-word bbox-copy window,
seven mesh filename-type cases, and16 actual SetHeightAttributes scene queries
using supplied navigation classifications. State/pair/vertices/normal/cache bits
match. No empty-children callback substitutes for traversal.

og035_ctor_probe.py executes complete original default root, source-node, mesh-node
and base constructors, constructNode, actual factory dispatch and addChild/update.
A neutral zero-model filename reproduces the exact signed-zero geometry-child
matrix and retained global identity. The same actual child with road_sign as its
database path is unrecognized and CreateStaticBodies skips its Jump UID with no
allocation. Its CPU geometry is a supplied zero-buffer mesh/getter fixture, so the
material loop and texture preparation are not claimed covered.

Imports for allocation/free, memset/memcpy/strstr/strcmp and scalar/double math are
modeled explicitly. The full-graph oracle supplies raw TRS/default identity object
records and mesh getters; the separate complete-constructor oracle establishes
those inputs. Whole resource-cache file I/O, materials, root aggregate bbox work,
skin/camera/particle attachment and Android allocator/destructor lifetime are not
emulated. Native owning graph/terrain/body lifetime is tested separately. All ELF
bytes, emulator packages and generated ARM views remain ignored research artifacts;
the native build/runtime has no ARM or Python dependency.

The original29/32 direct producer/child probes remain bounded mathematical input
proofs where a mesh type was explicitly supplied. Their former signed-model fixture
name contained sign, which would select the original empty runtime tag. The raw
regression now uses neutral zero-model/zero-root names, retains all signed-zero
assertions, and adds genuine original negative-type coverage. Neither the renderer
nor skeleton transform path was globally changed.

Alternate billboard/optimized factories, node/controller/external instance factories,
animated scene updates, streaming/unload and live actor/skin graph binding remain
explicit prerequisites. Unsupported branches throw; no relevant mesh is silently
dropped or replaced with an approximate terrain, wall-slide or constant plane.
Existing native Windows smoke checks validate compatibility, not production use of
this new binding or completion of the full game port. Final configuration/gate results
and patch hashes accompany the ignored OG-035-report.md submission.
