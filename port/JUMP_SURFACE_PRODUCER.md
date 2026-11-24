# Original authored jump-surface producer

OG029 against verified native baseline330d2de. This reconstruction starts from
original source/assembly/ELF and raw supplied BRES data. Existing approximate
CityTerrain.jumpRamps are not evidence, inputs or fallbacks for this producer.

## Original functions and data

| Original symbol | Thumb address | Role |
| --- | --- | --- |
| GS3DStuff::CreateStaticBodies(node,ownerNode,placementNode,node) | 0x002ddf79 | Authored Jump/Plane qualification, definitions and metadata |
| GS3DStuff::AddXRef(ISceneNode*) | 0x002deebd | External library instantiation and distinct owner/placement binding |
| GS3DStuff::FindAMesh(ISceneNode*) | 0x002dc175 | First original visible mesh, excluding Plane/Jump UIDs |
| GS3DStuff::updateVisibility(unsigned) | 0x002dd7e1 | Lazy native body activation and deactivation |
| CollisionBodyDef::CollisionBodyDef() | 0x002db83d | Definition/body arrays, active flags, references |
| CollisionBodyDef::~CollisionBodyDef() | 0x002db305 | Shared definition/metadata lifetime |
| ISceneNode::getRelativeTransformation() const | 0x002738b1 | Raw quaternion, original scale comparison and translation |
| CMatrix4<float>::mult34(matrix,out) const | 0x00272c0d | Original affine hierarchy composition |
| CMatrix4<float>::getRotationDegrees() const | 0x00244dd1 | Body yaw extraction, including gimbal branch |
| quaternion::getMatrix() const | 0x001cdb51 | Actual returning-matrix delegate used by scene relative transforms |
| quaternion::getMatrix_transposed(matrix&) const | 0x001c7f4d | Raw authored quaternion to scene matrix, no normalization substitute |
| quaternion::set(float,float,float) | 0x002185c9 | Original Euler quaternion, double trig then float normalization |
| quaternion::normalize() | 0x001c8d11 | Exact-one shortcut and float reciprocal sqrt scaling |
| quaternion::operator*(vector3 const&) const | 0x00251d99 | Original inverse-orientation vector product |

The field contract depends on the original generated node, not matching names in
one asset. CColladaDatabase::constructNode0x0029692d uses a CSceneNode container
and a generated mesh child for the default unoptimized scene path. It creates the
mesh through factory vslot44 at0x00296b5e, attaches it through addChild at
0x00296c70..76, and applies SNode position/quaternion/scale to the container through
virtual setters at0x002969c0..f4. The source SNode is not assigned to that mesh child.
The optional OptimizeSceneNodes branch is not a new native scene-manager mode here;
full original scene construction/refcounts and its other factory branches remain
outside this binder.

| Decoded input | Original field and consumer |
| --- | --- |
| CityNode.name | SNode UID string at +0; CSceneNode::getUID0x002aee29 |
| Display name, not used for qualification | SNode string at +4; setName in constructor |
| CityNode.translation/rotation/scale | SNode +12/+24/+40; direct original setters |
| CityMesh.name | SGeometry UID at +0; CColladaMesh C1 stores it at mesh+4 at0x002a11b8..ba |
| Generated geometry-child UID | CColladaMeshSceneNode::getUID0x002a26e9, NULL SNode fallback to mesh+4 |

CColladaFactory::createMeshNode0x0029d3b1 calls mesh C1 at0x002a3079 with
SNode=NULL, position(0,0,0), quaternion(0,0,0,1), scale(1,1,1). Its getUID therefore
uses CityMesh.name, not the enclosing SNode UID or display name. The collector's
Jump and FindAMesh Plane/Jump qualifications use that exact mesh field. A synthetic
fixture deliberately makes a visible child's container UID contain Jump and makes
the actual Jump child's container UID contain no Jump. This prevents the two
supplied libraries' coincidentally matching Jump-node/Jump-mesh strings from
standing in for the general contract. No filename alias is manufactured.

CSceneNode C1 at0x002aee4d stores the SNode pointer at node+364, passes SNode+24
unchanged to setRotation0x00272a05, and that setter copies all four floats to
node+184 without normalization/conjugation. CGameFactory::createNode0x002dbf59
forwards the same SNode in its non-billboard branch. The geometry child still has
identity local quaternion: ISceneNode::updateAbsolutePosition0x00273141 composes
its parent's absolute matrix with its own relative matrix. getAbsolutePosition
0x00273b3d reads the absolute matrix translation at node+84/+88/+92. Body yaw and
position thus use the generated child's inherited absolute transform; they do not
use its identity local rotation or substitute a different container's transform.

Keep container hierarchy matrices for composing authored child SNodes, then execute
one additional literal mult34(containerAbsolute,identityLocal) for each generated
mesh child, separately for current Jump and first-visible placement. Identity is
not an operation-elimination permission: the original additions normalize some
negative-zero elements. The first submission omitted that final operation and its
initial fixtures did not expose the difference. OG032's finite-TRS search found
signed-zero matrix differences and body-yaw differences; the revised collector
retains the actual generated-child multiplication without changing the renderer.

The raw-scene regression serializes and reloads two BRES files: external root
T(100,200,300), Q(0,1,0,0), S(1,1,1); visible container T(2,3,4) and Jump container
T(9,8,7), both Qidentity/S(-1,1,1); generated geometry children have identity TRS.
The original final matrices have +0 at M1 for both children and body yaw +0.
The omitted multiplication produced -0 at those positions. Independently expected
current translation is (91,208,293), placement translation (98,203,296), common
linear diagonal (1,1,-1), and all off-diagonal zeros are +0. Real body GetAngle
preserves +0, ID231 advances to232, and the verified native snapshot height at the
produced surface centre is236cm. This distinguishes container and actual child
matrices even though the polygon and height values otherwise agree.

CreateStaticBodies identifies case-sensitive UID substring Jump. It accepts only
the first mesh buffer when its vertex count is four. This check is independent of
triangle count/index order. Non-four-vertex buffers do not allocate a definition or
consume an ID. The original also hides/removes helper scene nodes in its mutable
scene graph; the native catalog binds immutable decoded geometry and records source
identities, not a complete scene-graph removal implementation.

## Exact producer contract

Current Jump node and placement/visible node are distinct original inputs. Body XY
is current-node absolute XY/100. Body angle is current-node absolute-matrix Z rotation
in degrees, converted to float and multiplied by original0.01745329252f.

The rotation helper uses double -asin(M2), cos, strict abs(cos)>1e-8, a float inverse
cosine before multiplying M1/M0, double atan2/RADTODEG57.29577951308232, and negative
angle normalization by360. Its gimbal branch uses atan2(-M4,M5). Replacing this with
a direct unnormalized atan2 radians call changes finite operation ordering.

Raw vertices are cyclically reordered from the first vertex with x<0 and y<50.
Both comparisons are strict. If none qualifies, index4 wraps to0 through `&3`.
Each selected vertex is multiplied by the placement matrix linear3x3, excluding
translation. Its world metadata height is placementMatrix[14]+transformed.z.
Placement XY translation is not added to polygon-local vertices.

The polygon-local XY then uses original quaternion.set(0,0,bodyAngle), including
float normalization unless length squared is exactly1, and the original negative
cross-orientation quaternion-vector product. Results divide by100. Body rotation
is not simply deleted algebraically: original float/trig/normalization operations
and their rounding are retained.

Metadata owner/node pointers are both NULL. Heights are the four ordered vertex Z
values; byte24 is true. Byte25 is now directly identified as a producer ID, rather
than merely guessed from a flag name. ELF0x004b6a8c is the function-local static ID
symbol `_ZZN9GS3DStuff18CreateStaticBodiesEPN3irr5scene10ISceneNodeES3_S3_S3_E2ID`.
The original loads its old byte, increments/stores with uint8 wrap, then copies the
old byte to metadata. First ID0 and wrapped ID0 remain valid enabled jump surfaces.
Meaning beyond this producer identity and further gameplay consumers is not inferred.

BodyDef retains zero mass/angle damping data and allowSleep=true, with sleeping,
fixedRotation and bullet false. Polygon has four vertices, friction0.2, density1,
restitution0, non-sensor, category1, maskffff and group0. updateVisibility activates
with CreateBody then CreateShape, without SetMassFromShapes. Consequently the body
is static/mass0 even though its polygon density is1. This differs from actor-circle
creation; do not automatically reuse the actor mass setup.

## Scene-data binding

All required data is already decoded as CityScene nodes/geometry references and
CityMesh raw positions. No disk schema or existing asset header is changed. The
binder follows external geometry references in scene/node/reference order, resolves
the exact source library, finds the first visible local mesh excluding Plane/Jump,
and separately retains the Jump-node and visible-mesh absolute matrices.

AddXRef copies source instance position and rotation to its new library root, not
its scale. Authored library-node transforms still apply their raw quaternion, scale
and translation. Original getRelativeTransformation uses the returning quaternion
getMatrix delegate, which calls getMatrix_transposed. The different output-argument
getMatrix helper at0x24ec71 has the opposite off-diagonal signs and is not substituted.
Independent original-relative/mult34 validation catches that distinction. Original scale.equals(1,1,1) uses tolerance1e-6 with its inclusive
upper/lower float tests; this is not a generic geometric epsilon or quaternion
normalization. Affine composition retains original three-product addition order.

The supplied jump libraries have a visible sibling mesh translation of
2.6681199073791504cm along local X while the Jump node's local transform is identity.
That visible-node offset affects the placement matrix and, under pitch, the metadata
heights. It must not be silently collapsed into the current Jump-node matrix.

The bound source and scene-owner identities include instance/reference/library/node
indices; sources also include the generated geometry index. Scene definition owner
is separate from metadata's NULL LevelObject owner.
One catalog corresponds to one original CollisionBodyDef scene owner; the host
partitions collected sources by that identity. A shared process-lifetime
RetailJumpSurfaceSequence provides IDs across catalogs/worlds/reloads. This caller
invariant is explicit; creating another independent sequence would duplicate IDs.
The type is noncopyable but does not install a broad process-global registry.

Nested instance-parent, nested jump-library, LOD and billboard factory paths that
need additional original hierarchy/owner behavior report unsupported binding. The
explicit source API can accept correctly supplied original matrices for other host
bindings; no nearest shape, node-name alias or approximate ramp is substituted.
The binder does not claim to implement the full mutable AddXRef/LOD/scene manager.

## Native activation and ownership

Definitions and typed metadata remain stable across activation/deactivation. Native
CreateBody receives the definition's own metadata pointer; CreateShape uses the
original polygon and does not compute body mass from density. Activation is
idempotent for its existing world. It must be deactivated before changing worlds.
Appending new definitions while active is rejected explicitly.

The borrowed world outlives the active catalog. Calls are serial and outside
world.Step; callbacks do not throw. Deactivation keeps metadata alive through native
DestroyBody callbacks and clears each freed body entry immediately, so a later body
callback cannot observe an earlier freed pointer. Metadata/definitions are retained
for reactivation; the destructor deactivates before freeing them. These are native
ownership safeguards around the original definition lifetime.

Before activation, temporary real native polygons compute their actual OBB AABBs;
the authoritative world.InRange predicate checks them. Unsupported out-of-world
geometry reports a visible error before any body insertion. No enlarged shipping
world or silent null proxy is used. Original polygon validity assertions remain;
malformed nonconvex/degenerate inputs are not claimed a new supported format.

## Supplied-resource evidence

Direct automatic enumeration examined all359 BRES scene files in the user ZIP.
Exactly two authored Jump mesh definitions exist: jump_00_01.bdae and
jump_01_01.bdae. Both have four raw vertices and first-buffer indices[0,1,2,2,3,0].
The two high-end vertex heights differ:303.25cm and138.2169952392578cm.

The current WorldDescriptor high-detail resources reference eleven instances:
Downtown2, SantaMonica4, Suburbs5. Its selected low-detail paths end in _low.bdae
and contain no jump references. Separately, the supplied santa_monica_lite.bdae
resource contains four additional scene references; it is not selected as one of
those manifest low-detail paths. They are fifteen serialized scene bindings tested
separately, not fifteen distinct jumps that should all be active in production. Both provided libraries
use the simple proven root external-reference binding; no manual progression is
needed to enumerate these resources.

Raw source SHA256: jump_00_01.bdae
edbc27533a3c68cc3d09d3dc947931cabc17bf913f2b0b1d5bc0a077a3fde6db;
jump_01_01.bdae
9d7aa2dfb4a389ea18f1643b5cacb7876adffd27b57a357a7af5f200a6c30796.

## Validation scope

Ignored og029_oracle.py executes original CreateStaticBodies and original rotation,
quaternion Euler/normalization/vector and matrix helpers. Scene virtuals are supplied
as explicit array-backed original inputs. Allocations, definition-array insertion,
scene hide/remove/parent queries, memcpy, strstr and AEABI scalar/double math/trig
are intercepted. Those boundaries are recorded, not represented as proof of a real
scene manager, original Android libm environment or native body lifetime. Retail ELF
bytes, emulator packages and generated disassembly remain ignored research artifacts,
absent from native builds and runtime. Independent actual Box2D/filter/snapshot and
resource tests supply executable native evidence separately.

Two additional reference-only captures separate supplied inputs from the asset-to-
node bridge. og029_scene_bridge.py executes the real CSceneNode constructor and
TRS setters/getUID, and the real mesh factory call argument flow/getUID. It
intercepts only the CEmptySceneNode base constructor/name storage and the mesh
allocation/complex constructor; it records the NULL SNode and all three identity
TRS arguments. It does not execute the whole database/refcount lifecycle.

og029_child_bridge.py executes actual updateAbsolutePosition, relative, mult34,
getAbsolutePosition and CreateStaticBodies with identity-local generated children
under distinct nonidentity root/container TRS. Parent links and empty child lists
are supplied explicitly, absolute-matrix getters return the actual object matrix,
and the producer's raw mesh/array/allocator/removal hooks remain. Native current
and placement matrix bits, body XY/yaw, polygon/height bits and ID match. The local
child quaternion remains identity while body yaw is nonzero. Independent raw-ZIP
parsing plus the original relative/mult34 helpers also matches all fifteen supplied
serialized bindings. Neither capture is a claim to execute the full scene manager.

Release and Debug full builds and all fifteen CTests pass. Each configuration
validates fifteen actual static bodies/proxies, verified filter dispatch and jump
snapshots, native destruction callbacks/allocation balance, and the original-world
range rejection. Twenty-three synthetic supplied-input producer cases and fifteen
retail cases have zero original-producer differences; the separate nonidentity
child-context cases (nonidentity and adversarial signed-zero) also match exactly. Release/Debug matrix/definition float-bit
dumps are identical. Both configurations pass ordinary terrain retail validation
and automated Vulkan startup/opening smoke checks. The existing retail audit keeps
exit1 with the same111 missing referenced paths as the preceding OG025 audit,
98/98 decoded images and zero opened-image decode failures. This new producer is
not yet bound into production visibility or the actor controller: startup smoke
therefore validates continued native application operation, not runtime execution
of this component. All tested executables are PE AMD64(0x8664).

Exact build/test/oracle results and remaining production bindings accompany the final
ignored OG-029-report.md submission. Original static Plane bodies, mutable scene
helper removal, octree visibility-query scheduling, full factory branches and actor/
scene integration remain separate work; this catalog implements no empty substitute
for them and makes no gameplay-completion claim.
