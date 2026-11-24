# Original actor post-load bounding-box phase

The native scene constructor now performs the omitted box phase from genuine
`CRootSceneNode::onPostLoad` at `0x293da9`:

1. Recursively update actual absolute transforms.
2. Call `CSceneNode::computeBoundingBox` at `0x2aef15` on the actual root and its
   separate private root-box storage.
3. Continue existing genuine skin/joint attachment.

This phase is observable even when the resulting aggregate does not change:
it calls current ordinary-node transformed-box getters and consumes their
`0x200` dirty state. Before the original finalizer, both Player shadow mesh
nodes have flags `0x2c0`; afterward they have `0x40`. Skin nodes are skipped by
this traversal and remain `0x240`; actual subsequent skin attachment marks the
skin cache dirty and supplies its real joint matrix pointers.

`RetailActorScene::postLoadBoundingBox()` exposes the separate retained cache
corresponding to original root-object+480. The original constructor initializes
it to `[-1,-1,-1]..[1,1,1]`. It is not the public ISceneNode local-box object:
actual original pointer identities differ. The public root/container getter
remains the proved CEmptySceneNode fixed cube. No new aggregate replaces it.

## Literal helper, including return behavior

`RetailActorSceneNode::computeBoundingBox(output)` follows original ordered
children and exact tags, independently of visibility:

- Ordinary mesh `0x6d656164`: invoke the actual transformed getter, which uses
  the existing cached absolute matrix and translation-first `transformBoxEx`.
  If `found` is false, add the box to the caller's current output. If true,
  copy the box over output. This branch **does not set found**.
- Container `0x6e656164`: recurse with a local cube `[-1,1]`. Only a true child
  return propagates that output, copying and setting found if previously false
  or merging if already true.
- Skin `0x73656164`, root tags and all other types are ignored by this helper.

The original's direct-mesh branch never setting found is preserved. Actual
direct shadow queries return false while modifying output. A nested-only root
query returns false and leaves arbitrary supplied output unchanged, despite
querying ordinary descendants. Both completed Player private root boxes remain
the constructor cube. The native implementation retains the full literal
branch structure; it does not substitute a constant return, nearest geometry,
fixed dirty-bit clearing or an intended aggregate algorithm.

The helper does not update absolute transforms or force clean boxes to refresh.
After a local position write, a clean transformed cache remains visible until
the original absolute update marks it dirty. Repeated queries still traverse
the same children and call their getters, preserving current cache semantics.

## Merge order and exceptional values

Original `aabbox3d::addInternalBox` at `0x20eaad` adds the input MAX point first:
capture all three components, compare each `>` against output max, then each
`<` against output min. It then reads the input MIN point and repeats. The
native helper follows that exact order, including when input/output alias.

Strict comparisons preserve existing signed zeros on equality and do not
replace NaN endpoints through unordered comparisons. They also account for
reversed input endpoints without a guessed repair pass. Capturing both input
endpoints early would be wrong for an aliased inverted box: original MAX
updates MIN before the subsequent MIN read, producing a different result.
No `fmin`, `fmax`, second transform implementation or eager geometry fallback
is used.

The reference tests use controlled mutable cache values to isolate this merge
operation. Actual native node getters remain in use. Original counterparts
execute the real helper and union instructions with explicitly typed list,
type-getter and current-box records; these cases are not claimed as file-factory
or general nonfinite-transform validation. Separate real Player cases execute
the full original factory/finalizer and actual transformed getters.

## Validation and domain

The matching original ELF SHA256 is
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Ten original helper cases cover empty/direct/multiple children, recursion and
ignored tags, repeated calls, NaN payload retention, infinities, signed zeros,
inverted endpoints and input/output aliasing. Native tests retain exact word
expectations and compare against those executable source results. Two actual
Player resources additionally match six initial/stale-local/updated-absolute
cache phases and separate private/public root storage proofs.

The source constructor's existing default-node/material/skin domain is
preserved. General camera/particle attachment, embedded animators, unsupported
alternate factories and morph-backed scene resources are not newly implemented
by this phase. Existing setup guards remain. This completes the missing box
phase for supported actors, not every CRootSceneNode subsystem. No gameplay
or manual mission exploration is required.
