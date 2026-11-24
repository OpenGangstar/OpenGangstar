# Borrowed current vertex streams

OG070 adds `RetailVertexStreams` and overloads of the existing CPU morph and skin
functions. The view borrows a source validation name, full-index position and
normal vectors, and the actual current normal format. It owns and retains
nothing. Name storage and both vector objects must remain alive during each call;
the caller must prevent concurrent mutation or storage replacement during a call.
Copying the view copies borrowed identities, not vertex data.

The old `CityMesh` entry points remain available and delegate immediately to the
view overloads. There is one arithmetic/preflight implementation per operation.
No decoder, immutable geometry, topology, indices, UVs, colours, or material data
are copied to pass a current deformation result to the next operation. For
example, the view can reference `RetailSkinningOutput::positions` and `normals`
from the preceding morph directly. Mutating those vectors between calls is
visible through the same view.

## Original contract retained

The reference is `libsanangeles.so`, SHA256
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Addresses include the Thumb bit.

* `CColladaMorphingMesh::morph` at `0x002a56e5` selects the current source buffer
  and uses its current component arrays. Weighted helper calls retain their
  original multiply/copy/add order. The exact-zero base-weight branch still uses
  the source arrays with the first nonzero target's weight, skips that target's
  geometry, and then adds subsequent targets. This source-initializer quirk has
  not been replaced with a target copy.
* `CColladaSkinnedMesh::skin` at `0x002b1c6d` reads its current source component
  arrays and the supplied final skeleton cache. Its raw normal transform,
  influence order, positive/negative-zero skip, exact-one shortcut and zero-total
  division are unchanged.
* Original range getters `0x002a15dd` and `0x002a15e9` select the source buffer's
  signed 16-bit first/end range. This API continues to expose checked native
  `[first,end)` indices into full-index storage. It does not reinterpret a compact
  used-range process span as a full source vector.

An empty **current** normal vector means absent. Neither overload looks up
immutable base normals or another controller's normals. Existing normal-capacity,
float-format, source-name, full skin-vertex count and final-cache count checks
remain unchanged, as do transactional malformed-used-input checks before output
writes. Unused output values remain untouched. Existing NaN output classification
is preserved; Android NaN payload identity is not promised. See [SKINNING.md](SKINNING.md)
and [MORPH.md](MORPH.md) for the complete bounded arithmetic contracts.

## Validation

`retail_vertex_streams_test` exercises 21 dynamic morph-to-skin chains using
views constructed once before three successive updates. It checks actual source,
intermediate, destination and borrowed-name addresses; used-range guards;
normalized/relative and negative weights; both signs of skipped zero; the literal
base-zero source initializer; absent current normals; and zero-total skin output.
Independent controls reject identity/count/format/nonfinite errors without
partial writes and compare the preserved mesh entry point with the view overload.

An ignored reference harness executes original morph `0x002a56e5`, exports its
actual destination addresses, then supplies those same padded arrays directly to
original skin `0x002b1c6d`. All 21 chains match native results: 468 intermediate
and 504 final component bits, including finite and infinity values, with zero
differences. The original range getters and weighted helpers execute; only typed
mesh/buffer component accessors and imported floating/memory operations are
hooked. The morph output is not repacked between original operations.

The existing 55 synthetic plus six retail skin cases and 48 synthetic plus 126
retail morph cases retain identical before/after dump hashes in Release and Debug.
Original execution also checks those inputs against the supplied raw resources.
These are CPU-core proofs with supplied final matrices, not allocator/driver or
complete animation-controller integration proofs.

## Remaining integration

The caller still resolves genuine resource/controller identity, maintains final
skeleton-cache freshness, and owns current/output storage. A stream name is the
existing validation identity, not a new runtime resource UID or chain resolver.
This change does not add the original driver/process allocator, compact used-span
adaptation, nested resource factory, weight animator, renderer upload, or full
retained actor pipeline. Those responsibilities remain separate; the borrowed
seam removes the need to fabricate or copy a `CityMesh` to connect their CPU
operations.
