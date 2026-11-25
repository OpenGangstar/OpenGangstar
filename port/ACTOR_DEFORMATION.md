# Loaded DefaultFactory deformation composition

`RetailActorScene(loadedResource)` constructs the actual ordinary, morph and
skin resource chain. `RetailActorDeformationPipeline::create(scene)` composes
their existing software process owners in dependency order. This is the
DefaultFactory deferred CPU path, independently checked with all 21 supplied
nested vehicle resources and both Player scenes. It is not the separate
CarFactory eager/damage path or a renderer/frame scheduler.

## Identity and canonical storage

The caller supplies one `RetailActorLoadedResource` for a genuine physical
load. Fresh actors reuse that handle. Each factory call still creates fresh
mutable resource, material, cache and palette identities. Ordinary resources
retain the load's exact current position/normal alias; the pipeline never copies
full meshes or substitutes immutable bind-pose streams. Separate physical loads
remain separate even when their immutable source metadata is shared.

Skin's `baseMesh()` is its actual immediate source, which may be ordinary or
morph. Morph owns its actual ordinary base and ordered ordinary targets.
`sourceMaterial` and its box alias the actual base, not a flattened mesh-name
lookup. Each morph has one `RetailMorphState`, retained intrinsically by Scene
and shared with the process; process release or recreation does not reset its
weights/base entry. The state's catalogue alias retains only immutable decoded
source, never the live Scene. Its index is the native decoded morph-catalogue
index, not a claimed raw global SController index.

The typed morph process overload verifies the exact Scene, immediate base,
ordered target resource addresses and canonical state before using the existing
arithmetic core. Skin verifies that same Scene and its exact immediate base.
Loaded ordinary sources must supply their exact canonical stream alias. Equal
UIDs, mesh values, shared geometry metadata and a foreign physical load do not
establish these bindings.

Only actual ordinary dependencies of owned skins/morphs are accepted as actor
process sources and protected against extraction. A movable ordinary shadow
cannot impersonate a dependency even if it shares the same geometry and loaded
arrays. Its own intrinsic stream alias nevertheless survives legitimate shadow
extraction and actor destruction without retaining the old actor. Existing
bound-Pose shadow transfer rules are unchanged.

## Process ownership and lifetime

The pipeline retains Scene and builds one process per actual resource address.
It exposes borrowed references, not escaping process shared pointers. Scene
holds only a weak association with its active pipeline: repeated `create` calls
return that active owner, and dropping it permits later reconstruction using
the same canonical Scene state. There is no Scene/process ownership cycle.

A retained output view has the existing independent allocation/Scene lifetime;
it can survive process release and pipeline destruction. Borrowed process
references cannot. The legacy low-level factories remain available for explicit
controls and require their caller to preserve one active mutable process owner
per resource. The pipeline's weak association is not a registry policing those
independent APIs. Concurrent mutation requires caller synchronization.

## Literal deferred behavior

Each process is prepared through its genuine dependency chain; the pipeline
does not additionally prepare every map entry. This preserves the original
source-first ordering and the distinction between registration and draw steps.

* Disabling skin before its deferred morph allocates leaves skin descriptors
  absent. A later draw computes the morph and arms skin's source-release flag,
  but does not refresh the disabled skin descriptors.
* Repeating the disabled setter copies the source's now-current descriptors.
  Repeating the same boolean is therefore an observable operation.
* Disabling with skin output already present preserves that own output.
* Release honors the pending source obligation first, temporarily enables and
  clears disabled skin output, then restores disabled state.
* Step 2 does not arm the step-1 source-release obligation; releasing skin can
  leave morph output present until explicit morph release.

The existing morph loop preserves its original source-entry-zero quirk and
current target lookup, including live loaded target mutations. No duplicate
kernel, weight policy, decoder, palette or clock is introduced.

## Source evidence and validation

Reference `libsanangeles.so` SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Default morph creation `0x29d27d` supplies defer=true/NULL driver; the distinct
CarFactory path is `0x3092b5`. Scene construction `0x296dc1` performs authored
visual construction and finalizes through `0x293da9`. Geometry/skin creation
allocates fresh resources (`0x29d2a5`, `0x29d061`); loaded descriptors retain
source arrays (`0x2a18e8..0x2a1942`). Existing software operations are documented
in DEFORMATION.md: morph/skin preparation `0x2a58f5`/`0x2b1195`, toggle
`0x2b1111`, release `0x2a536d`/`0x2b1099`, and kernels
`0x2a56e5`/`0x2b1c6d`.

Independent original execution covers all 21 nested factories with two fresh
instances per load. A separate 23-scene probe checks completed factories have
empty ordinary/bound animator lists even where a global animation library is
present. Native actual-asset tests construct all 23 loaded scenes and compare
all immediate dependency, material/box alias, storage and palette identities.
Four scenarios per vehicle compare 378 original descriptor phases and 742,608
used stream component words plus 378 runtime base-entry words bit for bit in
both Release and Debug. The fourth scenario seeds source entry99, mutates a
current loaded target and changes live weights before re-preparing.

Original reference probes execute real factory/finalizer, process preparation,
toggle/release and CPU kernels with an isolated CommonGL software receiver and
native-import hooks. Pointer values are compared as presence/alias relations,
not as matching allocation addresses. This is no GPU/device or allocator
teardown parity claim. Native tests separately cover foreign-load/instance and
target-order rejection, weak pipeline reuse/recreation, retained views and
extracted shadow storage lifetime. Full Release/Debug builds and all 51 CTests
pass at the submission baseline. No gameplay progression was used.

## Supported boundary

The metadata-only Scene constructor retains its conservative morph and global
animation-library rejection. Explicit loaded scenes permit unattached global
definitions but reject authored instance kinds outside 2/3/4, external
references, alternate selectors and genuinely resolved lights. No automatic
animator attachment is inferred from a library's presence. Existing actual
bound-Pose transfer safeguards remain active.

Default loaded Scene and software process composition are implemented here.
CarFactory damage/weight production, general attached animator update, hardware
batch deformation, GPU upload/draw and outer vehicle/actor scheduling are
separate integration work. The caller must supply the genuine loaded handle
and use the existing graph/pose/material owners; this module does not fabricate
those systems or claim a complete vehicle controller.
