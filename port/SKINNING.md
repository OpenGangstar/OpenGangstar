# Original CPU skinning

OG051 ports the CPU vertex loop from CColladaSkinnedMesh::skin(unsigned int) at
0x002b1c6d. It consumes decoded typed geometry and a caller-owned, current final
skeleton cache. It does not implement a second matrix cache, morph controller,
renderer, actor scene or BRES reader.

The reference libsanangeles.so SHA256 is
b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a.
Addresses below retain the Thumb bit.

## Native contract and ownership

retailSkinVertexRange takes CityMesh, CitySkin, a borrowed vector of16-float final
joint matrices, an explicit [firstVertex,endVertex) range and caller-owned
RetailSkinningOutput position/normal vectors. It retains no pointer and allocates
no output storage. Matrix order must be the CitySkin joint order. The actual actor
owner must refresh that cache first and keep source/cache/output alive through the
call. Missing joint nodes must be resolved or rejected by that owner; this helper
does not fabricate joints or substitute an identity matrix.

Input positions/normals are the actual current source stream, including any
preceding original morph operation. Passing undeformed base geometry for a live
morph controller would be a caller error. The final cache already includes the
original joint absolute/inverse-bind/bind-shape work; applying those transforms
again here would deform twice. No additional actor/world transform or unit
conversion is appended by this function. Renderer matrix/lighting state remains a
separate original contract.

Output position capacity covers endVertex. If both normal streams exist, they must
be complete float streams. If either is absent, the original position-only branch
runs and any supplied output normal values remain unchanged. Output outside the
range is untouched. This API exposes the original buffer range explicitly rather
than guessing it from indices or silently processing an entire unrelated mesh.

## Original body and range selection

| Original function or region | Address | Evidence |
| --- | --- | --- |
| CColladaSkinnedMesh::skin | 0x002b1c6d | Current input/output buffer selection, two stream branches and influence loop |
| prepareSkeletonMtxCache call | 0x002b224a | Original refresh when dirty; native cache freshness belongs to the actual owner |
| prepareSkinBuffers | 0x002b1659 | Per-buffer influence offset accounts for vertices before its start |
| CColladaMeshBuffer::getVertexIndexStart | 0x002a15dd | Signed source16-bit first vertex |
| CColladaMeshBuffer::getVertexIndexEnd | 0x002a15e9 | Signed source16-bit last vertex plus one |
| CMeshBufferProxy range forwarding | 0x002a2045 / 0x002a2055 | Forwards those exact range identities |
| Point/normal calculations | 0x002b1d82..0x002b1f6e | Literal float products, sums and source influence order |
| Position-only calculations | 0x002b20bc..0x002b21ae | Corresponding point and weight accumulation |
| Total-weight division | 0x002b1f8a..0x002b204a / 0x002b21c4..0x002b2248 | Exact-one shortcut; one reciprocal for all addressed components |

The native range is checked against actual typed storage. The implementation uses
size_t for allocation/index/capacity arithmetic and uint16_t existing joint IDs;
there are no serialized pointers, ARM struct overlays or pointer-width casts.
Malformed negative/overflowed original ranges are rejected instead of reproduced
as unchecked memory reads.

## Literal skin arithmetic

For each vertex, position and normal accumulators and total weight start at positive
zero. Influences retain their authored order. Both positive and negative zero
weights are skipped **before reading their joint matrix**. A zero-weight invalid
joint therefore does not become a bounds error. Nonzero weights use the original
three-term left-associated matrix dot product. Position then adds matrix translation;
normal uses only the same raw3x3 linear part.

Each transformed vector is multiplied by the influence weight and added to its
accumulator. The weight is added to the total in that same source order. The core
does not reorder influences, use double accumulation, clamp weights or repair the
first influence into a direct copy. Those changes can alter signed zeros and
cancellation.

If total is exactly1, the accumulators are stored directly. Otherwise one float
reciprocal1/total multiplies every position component and every present normal
component. This is total-weight division, not Euclidean normal normalization.
There is no inverse-transpose normal transform. Nonuniform scale and original
normal length remain visible in the output.

Zero influences and all-zero totals retain original IEEE arithmetic: reciprocal
infinity and zero-times-infinity can produce NaN; cancelled weights can produce
infinities. No bind-pose or nearest-joint fallback was invented. NaN classification
is validated, without claiming Android/x64 NaN payload equality.

## Source normal format boundary

The loop reads float pointers directly. CColladaMeshBuffer construction at
0x002a1902..0x002a1944 stores the declared normal type and raw pointer; its final
interleaved stride setup at0x002a1a1a..0x002a1a48 retains the actual source stride.
CMeshBufferProxy::setReferences0x002a5321 copies these declarations without an
unpacking conversion. The float normal accessor0x001c7f15 accepts type6 and rejects
type0, but this CPU loop does not call it; it uses direct float reads.

Focused original execution confirms both packed type0 and float type6 declarations
retain their original pointers/strides through those constructor windows and the
actual proxy copy. The checked native API explicitly rejects packed-normal output
instead of reproducing unsafe float reinterpretation or borrowing a renderer's
normal conversion. Position-only operation can still be requested explicitly.

Independent raw enumeration of all359 supplied BRES files finds83 local kind0
skin controllers, including21 sources reached through local morph controllers.
All83 base-mesh buffer position and normal declarations are type6 float32. There
are no packed/absent/external/unresolved/cyclic cases in this inventory, and no
parse errors. Thus the identified supplied skins require no packed conversion.
The Player meshes have405 and396 vertices and direct float normal streams.

## Checked native adaptations

Structural validation runs before writing output. It checks source mesh identity,
per-vertex influence count, cache count, range/capacities, original byte-sized
influence-count domain, finite addressed source vectors/weights and finite used
matrix components. Nonzero invalid joint references fail; skipped zero weights do
not touch that cache entry. Unused homogeneous matrix components are not read or
validated as though they participated in this affine projection.

Errors propagate without partial output writes. This checked-input behavior is a
native safety adaptation to the original unchecked memory access, not claimed
malformed-input parity. Finite negative/nonunit weights remain supported by the
literal loop. Arithmetic nonfinite results are not converted to valid geometry.
Input/output allocation, actor callback ownership and matrix-cache dirty state
remain caller responsibilities.

## Supplied zero-influence data and decoder limitation

The83 skins contain27,434 source vertices, at most6 influences per vertex. There
are301 totals different from exactly1;205 are nonzero and require the original
reciprocal, including50 vertices in the normal Player and52 in the jail Player.

The remaining96 zero-count/zero-total vertices are all in car_limo.bdae controller
car_body-mesh-morpher-skin, with5 joints and564 vertices. Their index ranges are
[408,456) and[495,543), within the actual buffer[0,564) and referenced by its index
array. They are not unreferenced padding. Actual original CPU loop execution on
these raw ranges yields576 NaN position/normal component classifications, with
untouched output exact. The morph constructor/render path is not executed by this
probe; a zero-influence result is independent of the supplied matrix/position.

The existing authoritative readActorSkins decoder rejects zero influences before
this core can run. This patch does not change that separate decoder contract or
claim full native processing of all83 skins. The broader vehicle/decoder boundary
must be accounted for explicitly; silently injecting bind-pose weights would hide
an original data/behavior discrepancy. Native Player validation is unaffected.

## Verification and limits

Release and Debug exercise independent expected point/normal geometry, raw normal
length, nonunit and near-one totals, source-order cancellation, zero/negative-zero
weights, missing used caches, zero influences, signed zeros, nonzero/empty ranges,
absent streams and checked malformed capacities. Actual original skin execution
compares55 synthetic range/stream cases,801 finite/infinity components and21 NaN
classifications per configuration, with zero differences.

Two supplied Player meshes are decoded through the existing public actor loader
using an explicit test-only loader request. Its numeric category requests skin
decoding; it is not evidence of the Player's runtime object category/factory.
Three explicit final-cache fixtures per model cover2,363 processed vertices and
partial ranges;14,418 output/untouched components match original execution exactly.
These matrices are caller inputs, not an invented animation or duplicate skeleton
cache. Genuine live-owner integration can use the separately verified actor scene
cache; it remains a later integration gate.

The reference executes actual skin0x002b1c6d and signed-short range getters with
nonzero buffer slot2, independent raw retail values and padded source/output float
strides. Guard bytes remain unchanged. Hooks provide only typed mesh/buffer stream
accessors and AEABI float operations. Dirty-cache generation, morph/allocator/
renderer/actor ownership are not established by that loop oracle. All reference
bytes/tools/logs remain ignored research artifacts; native code uses no ARM or
Python runtime.

Full native build/test/smoke/audit results accompany the ignored OG-051-report.md.
They validate compatibility, not production rendering through this new backend or
completion of the whole Windows port.
