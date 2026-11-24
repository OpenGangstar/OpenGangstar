# Original skinned-mesh bounding boxes

The native bounds core follows `CColladaSkinnedMesh::computeBoundingBox()`
(`0x2b2259`) and consumes original authored per-joint boxes plus the current
live joint absolute matrices. It does not compute vertex extrema and does not
use the final skin palette, inverse binds, or bind-shape matrix as bounds inputs.

The original prepares its skeleton matrix cache before every explicit compute,
even when the shared dirty flag is already clear. Its ordinary `getBoundingBox`
(`0x2b23b9`) computes only when that same dirty flag is set. CPU skinning and
matrix-cache preparation also clear this flag. Therefore preparing the skin
palette before asking for a box can leave an older cached box visible. A second
bounds-dirty flag would change this behavior. `CSkinnedMeshSceneNode`'s
`getTransformedBoundingBox` (`0x2b2565`) delegates to the mesh box through the
ordinary scene-node getter (`0x2a2685`); it applies no additional node transform.

## Data and arithmetic

The original skin source stores its box count at offset 108 and its ordered
24-byte box table at offset 112. These are fixed original disk widths, not
native pointer offsets. Each box contains min XYZ followed by max XYZ. Unknown
metadata and a known empty table have different meanings. The native optional
table rejects unknown metadata. An explicitly empty table selects the original
joint-translation branch.

The original cache entries contain a pointer to a live absolute matrix. Bounds
use those pointed-to matrices, not the final matrices at the front of the cache
entries. The native core borrows the actual matrices for the call; their owner
must keep them alive and perform the required scene updates first.

`CMatrix4::transformBox` (`0x2b1325`, resolved original GOT entry 0x32e4)
first checks its stored identity hint. A true hint leaves the entire source box
untouched, including inverted ordering and signed zeros. This hint must be
provided from the genuine matrix state, never inferred by comparing its floats.
Otherwise the helper transforms only the min and max corners with three
left-associated float affine dot products, then swaps each reversed min/max
axis. It does not transform all eight corners. This original quirk is retained
for rotations and shear.

The first transformed box initializes the result directly. Later boxes use
`aabbox3d::addInternalBox` (`0x20eaad`, GOT 0x3dd0), which considers the
candidate maximum point before the minimum point and uses strict greater/less
comparisons. Ties preserve the previous signed-zero bits. Without an authored
box table the first joint's raw translation initializes both extremes, followed
by ordered strict point expansion using later joint translations.

The original truncates its cache entry count to eight bits for the bounds loop,
but reads the first entry even when the truncated count is zero. The native
core preserves this for nonempty inputs: 255 visits 255 entries, 256 visits
only the first, 257 visits only the first, and 258 visits the first two. Extra
entries outside that effective range are not inspected by the core.

## Checked adaptations

The original first-entry read with a genuinely empty cache is unsafe. Native
empty input fails explicitly. Missing metadata, a short nonempty authored
table, missing addressed live matrices, and nonfinite addressed source values
also fail explicitly. There is no raw-mesh or zero-box recovery path. Unused
matrix components and entries beyond the truncated range are not validated;
the identity-hint branch does not read matrix float components. Finite input
arithmetic is not clamped, so its original overflow behavior remains visible.

## Source and validation scope

The original ELF used for address and execution comparisons has SHA-256
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Relevant lifecycle entries also include `prepareSkeletonMtxCache` (`0x2b1bb9`),
`prepareSkeletonMtxPtrCache` (`0x2b1b09`), `attach` (`0x2b1ba1`),
`onAnimate` (`0x2b0de9`), and `instanciateMesh` (`0x2b1231`).

The ignored raw-resource inventory enumerates all 83 supplied local kind-zero
skin controllers. All 83 contain finite authored boxes with a table length
equal to their joint count; the largest has 22 joints. None have an empty table
or an inverted source box. Both supplied Player models have 22 boxes. These
counts establish the supplied data contract, without assuming other resources
cannot contain the original empty-table branch.

Independent synthetic checks cover two-corner behavior, negative scales,
identity hints, inverted boxes, strict signed-zero union, translation-only
bounds, truncated counts, and checked malformed inputs. Reference execution
runs the original bounds and real skeleton-preparation routines with explicitly
prebound live joint pointers. It also exercises lazy versus forced preparation
and the actual transformed scene-node getter chain. Imported float arithmetic,
comparisons, and memcpy are modeled by the reference harness; this is not a
whole actor factory, renderer, Android libm, or malformed-input parity claim.

The supported default actor factory rebuilds relative transforms through
`ISceneNode::getRelativeTransformation` (`0x2738b1`), which clears the matrix
hint at `0x27397e`. Original `mult34` (`0x272c0d`) propagates actual input
hints or writes false after multiplication. Independent execution of the actual
default scene/root/skin-node constructors, update and attach found all 36 live
node hints false in each supplied Player model, including all 22 joints. The
original node constructor sets dirty flags 0x38 at 0x273d32 and calls update at
0x273d64, which builds/copies a false-hint relative matrix. Supported owner
operations preserve that invariant and expose no raw matrix/hint setter.
`absoluteIdentityHint()` represents that proved construction/update domain,
never numeric identity. The live probe isolates CPU geometry/material loading
with zero buffers and initializes typed skin-resource fields from original
source data; it does not claim complete original resource/skin allocation.

## Live owner and decoder integration

The existing authoritative `readActorSkins` reader checks the full 116-byte skin
record and retains `CitySkin::jointBoundsCm` in original table order. Missing
hand-built metadata remains `nullopt`; a serialized zero count becomes a known
empty vector. The reader checks spans and finite scalars without repairing
inverted source boxes or introducing another BRES parser.

Every real `RetailActorSceneMesh` factory construction owns one cached box
seeded from its required immutable raw geometry box. Repeated UIDs still own
separate mutable resources and boxes. `boundingBox()` is lazy; explicit
`computeBoundingBox()` unconditionally invokes the same private
`prepareSkinMatrices()` used by lazy `skinMatrices()`. There is one shared
dirty flag, no independent bounds-dirty state or duplicate palette routine.
Bounds read genuine live joint absolute pointers/hints. Node `boundingBox()`
delegates to its actual mesh; the skin `transformedBoundingBox()` returns that
same cached box without an extra node transform. Default containers have a
fixed empty-node cube rather than aggregation. OG078 implements ordinary and
container transformed caches using the distinct original transformBoxEx;
see ACTOR_BOUNDS.md. Custom/global-manager-specific box domains remain explicit.

An unknown metadata getter always fails, including after palette preparation;
it never exposes a raw seed as a fabricated dynamic result. Explicit compute
retains prepare-before-core ordering: a checked failure can leave a refreshed
palette, cleared dirty flag and prior box. That failure is not a resumable
transaction. Repeated unknown metadata getters continue to fail. Proper known
tables retain the original raw-seed and palette-first stale-box behavior.

The actual owner starts enabled and exposes `skinningEnabled()` and
`setSkinningEnabled(bool)`. The original setter overwrites shared dirty with
`!oldEnabled && newEnabled`, including repeated calls that clear an already
dirty state without refreshing the palette. Disabled preparation returns without
refreshing the palette or clearing dirty. Explicit bounds still use live joint
pointers; the lazy getter subsequently clears dirty itself. `cachedBoundingBox()`
borrows the same stable box storage without invoking any lazy getter, matching
the proxy's direct skin+64 reference. The separate process owner must perform
the original output-byte36-dependent stream restoration/clear after the setter;
this cache operation does not pretend to own proxy streams.

After the OG054 empty-influence-row correction, native metadata enumeration
returns all 83 supplied controllers, with 5,676 authored box float bits matching
the independent raw inventory, including both `car_limo.bdae` skins. Each
`CitySkin::sourceUid` also preserves the original SSkin+0 fragment before the
existing `meshName` traversal resolves bind-pose geometry. The 21 skin-through-
morph identities remain distinguishable from direct geometry sources.

Both supplied Player owners are tested through ten original scene/cache phases
each: raw seed, lazy compute, clean stale state, forced refresh, palette-first
state, nonuniform/negative scale and nonunit quaternion, option1 frame, and
transformed getter identity. Actual original scene construction/update/attach,
cache preparation and bounds/getters match 14,200 float component bits per
configuration, including live joint matrices and final palettes. The standalone
core also matches 108 bits across 18 original cases. No gameplay progression,
renderer integration, process-buffer toggle integration or whole-game completion
is claimed.

OG077 additionally compares both actual Player owners across 19 enabled/cache/box
phases each: 27,056 exact state and component words per configuration. Repeated
enable/disable calls, disabled preparation, explicit versus lazy bounds, live
frame dirtying and stale palettes match actual original execution. Full Release
and Debug builds pass all 40 integration tests. All 83 raw source fragments and
21 through-morph distinctions match the authoritative reader output.
