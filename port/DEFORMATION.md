# Owned software deformation buffers

`RetailDeformationMesh` owns the current software process streams between the
verified source decoder, actor palette owner and skin/morph kernels. It does
not implement a GL driver, GPU submission, a resource factory, a damage/weight
animator or a second skin palette. Callers bind the genuine constructed source
and current storage. Standalone sources also require explicit source-buffer
material handles; the actor overload resolves the actual existing materials.

## Sources, proxies and retained views

An ordinary standalone source retains its immutable `CityMesh` metadata and
explicit mutable `RetailSkinningOutput` current storage. These are different
objects: writing original current normals must not mutate an immutable decoded
mesh or silently restore bind-pose normals. Supplying the same current-storage
handle to two sources explicitly shares their subsequent mutations.

The actual actor-source overload retains the genuine `RetailActorScene` and
verifies by actual identity that the source is a base mesh of an owned skin.
Transferable standalone ordinary meshes, including shadows, are rejected before
borrowing anything: retaining the actor alone cannot retain an extracted and
independently destroyed ordinary subtree. A generic movable ordinary-resource
lease remains unimplemented. The supported skin base cannot be extracted by the
current graph's dependency preflight. The source borrows that constructed
mesh's cached box address, without invoking a lazy getter, and aliases each
actual `sourceMaterial(slot)` with the scene's retained lifetime. It creates no
replacement material state and does not use prepared node material instead.
A skin requires the
same actual constructed base resource and scene as its real palette owner,
not merely equal UIDs or an equal decoded `CityMesh` pointer. Two actors can
share decoded geometry while owning distinct constructed resource and material
identities. That does not establish isolated source vertex arrays: fresh runtime
buffers can still borrow shared loaded storage. A production source-stream
producer must supply the actual sharing relationship; this component neither
deep-copies per actor by default nor substitutes immutable decoded streams.

A native graph regression rejects the transferable shadow binding, then performs
real shadow extraction and destruction. A retained accepted skin proxy still
observes its exact original box address and valid current streams; its final
release drops the actor independently of the destroyed shadow.

Morph construction retains its actual source and authoritative decoded
catalogue/controller index. The source's exact geometry object must belong to
that catalogue; a same-named geometry in another resource is rejected. Runtime
target weights are owned separately from immutable catalogue data and change
only when the caller supplies them. There is no guessed vehicle damage policy.

Proxies borrow the real source buffer's topology, original indices, material
handle and all non-deformed metadata. UV0, auxiliary UVs, primary/secondary
colors, normal-format declaration and static source identity are not replaced
by another mesh copy. Morph borrows the source box; skin borrows the real
palette owner's cached skin box storage. The source retains its scene/resource
owners. No bbox query or palette preparation occurs while creating a proxy.

`RetailDeformationView` retains both metadata and the currently referenced
backing allocations. New views after release see absent position/normal
descriptors. An earlier retained view can safely outlive that release or the
process owner. This is memory retention, not an immutable frame snapshot:
original persistent-buffer reuse changes the same values. An asynchronous
renderer must copy or synchronize before preparing that persistent storage
again. No fake completion callback or unsupported Vulkan host is provided.

## Process mask, type and return flags

These are three distinct namespaces. Original stream mask1 requests position,
mask3 requests position plus normal. Preparation chooses from **the embedded
source-buffer material's flags40 mask2**, not the scene node's prepared
root-library material. Original nonzero Player factory executions confirm
those two material objects have distinct identities and states. A proxy shares
the exact source handle, so its next request observes real source mutations.

`RetailProcessBufferType::persistent` has original value1;
`transient` has value0. Constructor eager allocation requests type1 with
allocation allowed. Registration step0 requests type1 without allocation;
nonzero preparation steps request type0. Only step1 records a nested source
release obligation. Unknown masks/types are explicitly outside this software
slice; they are not interpreted as successful fake driver operations.

| Software operation | Original result |
| --- | --- |
| Missing persistent output, allocation disallowed |16, descriptors unchanged |
| Exact persistent mask with output |4, reuse actual allocation |
| Persistent mask mismatch, allocation disallowed |9, descriptors unchanged |
| Type0 while persistent output exists |16, no transient allocation |
| Successful new/reused software output |4 |
| Zero mask or failed allocation |8 |

The owner implements these results using real owned allocations. CPU kernels
run only when returned flags contain4. Allocation failure is checked before
publishing replacement descriptors. The unsupported original batch-backed
return5 path is not emulated.

Original constructor `defer=true` clears only position and normal pointers.
Eager construction allocates and sets mesh byte36 even before CPU evaluation;
the owner preserves this state distinction. An allocation's zero-initialized
unused native entries are not an original computed pose. Genuine registration
and preparation remain necessary before consuming defined output values.
Likewise, an allocated normal stream left unwritten by the original
positions-only branch has no claimed original numeric value. Descriptor presence
alone is not proof that the original branch computed those normals.

## Ranges and checked storage adaptation

The original allocation covers exactly `[first,last+1)`, using signed16 source
range getters. It stores a pointer biased backwards by `first*stride`, allowing
the unchanged original indices to address the compact allocation. Its source
vertexCount is not its allocation count. Mask1 has stride12, mask3 stride24
with normal at position+12. Original normals and other unrequested descriptors
remain borrowed rather than automatically receiving fresh storage.

The existing native kernels use full original indices. The owner allocates
full-index vectors but computes/exposes the original used range. It does not
rebase indices or form C++ pointers before an allocation. Signed-invalid ranges
and current-storage count changes are rejected. Storage outside the used range
is not claimed to be defined original output or rendered as extra vertices.

The original subtracts the pointer bias before testing allocation failure:
failed allocation with nonzero first can appear successful with an invalid
pointer. Native returns8 instead. RAII also releases superseded allocations
instead of copying original invalid-domain leaks. Explicit attempts to free
borrowed/persistent positions as a transient span are rejected before changing
the real palette toggle or releasing a source. No malformed pointer, OOM
undefined behavior or allocator-address parity is claimed.

## Actual kernels and eager normal alias

The owner reuses `RetailVertexStreams`, `retailMorphVertexRange` and
`retailSkinVertexRange`; there is one arithmetic/preflight implementation for
each. Current morph output is passed directly through the shared stream view.
No approximate mesh, alternate skeleton sampler, normal normalization or
second palette is introduced.

Eager mask1 retains a borrowed normal descriptor. When source and output
normals both exist, original CPU processing can write that source normal
storage. The owner temporarily transfers that vector to the kernel's existing
output container, preserving actual in-place input/output aliasing, then
restores vector ownership. There are no callbacks or concurrent accesses
during this transfer. Checked kernel failure restores ownership too. Deferred
mask1 instead has no output normal stream and takes the original positions-only
branch. Original raw normal transforms and repeated in-place effects remain;
there is no bind-pose reset or copied-normal approximation.

## Preparation, palette and release

For a skin, the underlying source prepares first using the same step/slot. Its
flags&4 are recorded as byte42 only for step1. Disabled skin returns the source
flags. Enabled skin requests its own process output and evaluates the real
kernel only for flags&4, setting byte36. Morph performs its actual output
request/kernel without fabricating a source animation update.

`RetailActorSceneMesh` remains the only enabled/dirty/palette/bbox owner. Kernel
evaluation calls its genuine `skinMatrices()`. Process construction and
retained box access use `cachedBoundingBox()` without lazy recomputation. Thus
palette-first preparation can clear the original shared dirty flag while
leaving the previous box visible, exactly as the existing owner specifies.

`setSkinningEnabled` first performs the shared original state transition:
dirty40 becomes `!oldEnabled && newEnabled`. Only when process byte36 is false
does it clear descriptors on enable or restore current source descriptors on
disable. Present output remains retained, including after disable. The shared
palette's disabled preparation, explicit bounds and lazy-getter dirty behavior
remain authoritative; none are reimplemented by this buffer owner.

The composing actor host must retain one active process owner for each actual
skin and route enabled changes through that process owner's setter. Calling the
palette-only setter directly does not update process descriptors. No automatic
factory association or competing process owners for the same live skin are
claimed by this component.

Release honors a pending nested source obligation first, then releases its own
transient allocation and clears position/normal and byte36. The original base
skin release temporarily enables a disabled skin and restores its disabled
state afterward; the owner uses the same shared toggle and descriptor updates.
Other metadata remains borrowed and unchanged. Ordinary source preparation
returns16 and release has no effect, as in the literal original base methods.

Actual original skinned rendering uses an identity world transform for enabled
or already-present CPU output, because its final palette contains joint
absolute transforms. Applying actor absolute a second time would be wrong.
This module returns streams and state; a renderer integration must preserve
that existing original draw-transform contract itself.

## Implemented domain and remaining integration

The supported retained actor path is ordinary source to real DefaultFactory
Player skin. Standalone ordinary source and morph owners are implemented.
Identity, retention and checked source-sharing are explicit, so these owners
can be used without another decoder or global mutable cache keyed by UID.

The current `RetailActorScene` factory rejects resources declaring morph
controllers. A genuine declared morph-to-skin palette/source resource therefore
cannot yet be constructed through that factory. This module rejects foreign
catalogues, geometry-only pretend matches and a matching sourceUid without the
actual supported factory domain. Source-first/release42 logic is retained, but
this is **not** a claim that21 vehicle morph-to-skin factory resources now have
complete production owners. That factory is a separate concrete prerequisite;
the existing070 core-view chain remains independently verified.

Actual runtime material ownership remains separate from the arithmetic owner.
The actor source aliases its genuine embedded buffer states from the verified
actor resource owner. Standalone sources require caller-supplied existing
states. Neither path substitutes node/root-library prepared CMaterial. No full
shared actor material host,
vehicle gate/weight producer, automatic OnAnimate scheduler, hardware batch,
GL driver or Vulkan deformation draw is claimed here.

## Original evidence and validation

Reference ELF SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Relevant original entries: proxy constructor/setReferences0x2a5269/0x2a5321;
prepareMorph/skinBuffers0x2a55a5/0x2b1659; process wrapper0x2a34a1;
CommonGL getProcessBuffer0x2c5d89; software binding0x2c5021;
allocator/stride helpers0x2c4f4d/89/d5,0x2b9511/0x2b9571;
onPrepare morph/skin0x2a58f5/0x2b1195; release0x2a536d/0x2b1099/0x2b3be9;
toggle0x2b1111; skin/morph arithmetic0x2b1c6d/0x2a56e5. See the preserved066
research and its44 root-verified original lifecycle cases.

The native test checks actual source/proxy/material/box addresses, independent
outputs, shared mutable source-normal behavior, flags4/8/9/16, nonzero range,
allocation failure, release retention and safe teardown, actual shared cache
effects and source/actor identity rejection. An independent ignored harness
executes original software allocation, proxy/prepare/toggle/release and both
CPU kernels against56 native phase snapshots, comparing exact used positions,
normals, source-normal bits and flags. It does not replace the CPU operations
with a test host. The two actual Player resources exercise real source/palette
owners and their405/396-vertex native outputs. Test GL/device initialization is
not a production requirement or a claimed GPU parity proof.

The controlled retail fixture explicitly supplies copied input storage for its
kernel comparison. It does not establish the original production source-array
sharing policy or implement that factory producer. Shared-storage controls
separately prove that callers can bind one mutable array to multiple source
owners and observe the original in-place normal effects.
