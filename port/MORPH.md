# Original CPU morph streams

`retailMorphVertexRange` ports the arithmetic in
`CColladaMorphingMesh::morph(unsigned int)` (`0x2a56e5`) and its original
`setWeightedVertex<vector3df>` (`0x2a54bd`, GOT0x1db4) and
`addWeightedVertex<vector3df>` (`0x2a53d9`, GOT0x26d0) helpers. It consumes
the existing decoded `CityMorphController`, genuine current source streams,
decoded target geometry and the caller's actual source-buffer vertex range.
Output reuses `RetailSkinningOutput`; there is no second source decoder,
matrix cache, or parallel stream structure.

## Weight and stream behavior

The runtime target array contains the source first and the authored targets in
source order. Every invocation resets the source weight to one. For method 0 it
subtracts each target weight in order (`((1-w1)-w2)...`). Every nonzero method
keeps source weight one. It does not clamp weights, divide by total weight, or
restrict the method to a guessed two-value enum.

When the source weight is nonzero, the initializer copies/scales the current
source stream. If it is zero, the original skips zero target weights to choose
the first nonzero weight, but still initializes from the SOURCE buffer arrays.
It then accumulates only targets after that selected target. Thus a single
target with weight 1 and method 0 produces the base source, rather than the
target. This behavior was verified by executing the actual original function,
not inferred from the existing vehicle preview's delta formula.

A zero weight reads no geometry reference and writes nothing. Weight 1 uses a
raw copy for the initializer and direct addition for subsequent targets; other
weights use float multiplication before addition. Target order and float
cancellation are retained. The source is initialized in full over the selected
range, followed by its normal stream, then each later target's position and
normal contributions in source order. Normal vectors are not normalized.

Normals initialize only when both source and output arrays exist. A later
target without normals contributes no normal values. With output normals
absent, all normal data is ignored. The original can pass a null adjusted normal
output pointer to an add helper when the source has no normals but a later used
target has them. Native checks this unsafe combination before any output writes;
it does not invent a zero seed. Original float-component helpers read three
floats directly. Used packed normal streams are rejected by the native leaf;
there is no renderer-based conversion assumption.

The explicit current source must identify the controller's actual authored
source. A host composing nested controllers must supply their genuine current
output and identity. This function does not replace nested controller loading
with an undeformed base mesh or cache by a guessed UID.

## Original buffer preparation boundary

`prepareMorphBuffers(bool, IVideoDriver*)` (`0x2a55a5`) builds one
`CMeshBufferProxy` per source mesh buffer. The proxy retains that source buffer's
component arrays and the source mesh box through `setReferences` (`0x2a5321`).
The true-bool branch clears the proxy's position and normal pointers, leaving
their strides and other components retained for later driver preparation. The
false-bool branch requests real driver process buffers with stream mask1 or3
depending on material flags40 mask2, and process type1. Stream masks, process
types and returned flags are distinct namespaces. It does not establish
a new independent morph vertex topology or normalize normals.

`onPrepareBufferForRendering` (`0x2a58f5`) obtains the driver process buffer,
and invokes morph only when the returned flags include bit 2 (value 4). The
existing car subclass (`0x309339`) additionally controls when that preparation
occurs. Native driver/proxy ownership, per-render-stage flags and the live
vehicle damage/animation weight producer remain later integration work. This
CPU function supplies no fake driver callback or vehicle damage policy.

The actual morph range comes from the source's requested mesh buffer, using
`getVertexIndexStart` (`0x2a15dd`) and exclusive `getVertexIndexEnd`
(`0x2a15e9`). Those original accessors sign-extend the authored 16-bit start and
last index and add one to the latter. Target arrays are obtained from target
mesh buffer 0 and offset by the same source range. The decoded meshes retain
the common current source streams. Native callers must provide a valid range;
negative or overflowed original ranges are not silently cast to huge native
indices. All output outside that range stays unchanged.

## Checked native behavior and limits

Native allocations and indexes use native widths. Disk decoding remains in
the existing authoritative asset reader. Structural input errors, nonfinite
authored weights or addressed vectors, short used streams, and unsupported
used normal formats fail before writes. Unused zero-weight targets and the
first selected target geometry in the base-zero branch remain unread. This is
an explicit checked adaptation, not parity with malformed-pointer or NaN-payload
legacy behavior. Arithmetic overflow from finite valid inputs is not clamped.

The original runtime mutates its internal source weight on each call; this
stateless leaf computes the same value locally. It does not claim that the
controller's original target-array lifetime or driver allocation has been
ported. Caller-owned current weights are used directly, without the preview's
rear/front deformation substitutions.

## Source and validation

Original ELF SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Related construction evidence comes from `instanciateMesh` (`0x2a5b4d`),
constructors (`0x2a5cc5`, `0x2a5d9d`) and `CCarFactory::createMorph`
(`0x3092b5`). Runtime source and target weights retain authored target order.

Automatic raw enumeration of all 359 supplied BRES resources identifies
21 local morph controllers, each method 0 with two targets and zero authored
target weights. Their 63 source/target meshes use float32 position and normal
streams, no signed-negative source ranges, with maximum 651 vertices. This is
separate ignored reference inventory; production keeps the existing reader.

Tests cover independent expected outputs for normalized/relative and nonzero
method dispatch, negative and large weights, sequential subtraction, float
cancellation, the base-zero initializer quirk, unread references, signed zeros,
partial and empty ranges, absent normal streams, and checked errors. Supplied
controller tests use their actual authored streams with both authored weights
and clearly identified explicit test weights. The reference oracle executes
the actual original morph loop, weighted helpers and signed range accessors at
nonzero buffer slot 2, with padded stream strides and output guard bytes. Typed
mesh/component accessors and imported float/memcpy operations are intercepted;
driver/proxy allocation, full live factory construction and vehicle policy are
not validated by this arithmetic oracle. No manual gameplay is required.

Release and Debug full builds and all 28 CTests pass. In each configuration,
48 synthetic cases match 1,044 float component bits and 126 supplied-controller cases
match 371,304 component bits against the actual original functions, including
unchanged values outside each range. All 21 controllers and 61,464 processed
vertices are covered. Both configurations' full retail audits are identical
to the prior baseline (359 BRES, 98 images and the same 111 missing texture paths).
Automated menu/opening Vulkan smoke checks also pass. The game does not yet
call this CPU leaf through a real morph driver/owner; these smoke checks prove
the existing game path remains usable, not production morph integration.
