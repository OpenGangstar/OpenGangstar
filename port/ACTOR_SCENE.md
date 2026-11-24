# Owned mutable DefaultFactory actor scene

`RetailActorScene` implements the bounded live graph/cache prerequisite for the
original Player models. Decoded source nodes are immutable data. Every root,
container, generated mesh node, mesh resource, joint pointer array and mutable
matrix cache has a separate stable native owner/identity. This is not a complete
`RetailActorAnimationSceneHost`, renderer, Character controller or scene engine.

## Source and factory bridge

PlayerTest::switchContext at 0x301a75 constructs `./char_pthug.bdae` or
`./char_pthug_jail.bdae` with **DefaultFactory** at 0x301cc8, using GOT+0x4940.
Character::bindAnimation at 0x2f23fd also uses DefaultFactory for its separate
animation library. Generic LevelObject/CGameFactory callers are a different path.

The shared `RetailActorSceneSource` owns `CityScene` and existing decoded skins.
`loadRetailActorSceneSource` reuses `loadCityScene` and the public
`loadCityActorSkins` wrapper over the existing authoritative skin reader. There
is no second production BRES parser. Trailing decoder metadata retains exact
SController+4 UID/source index, joint scope strings and SNode+4 display name.
Existing `CityNode::name` remains SNode+0 UID; `jointName` remains SNode+8 scope.

Both supplied models have 32 authored SNodes, one ordinary shadow instance and
one skin instance with 22 joints. Each produces **36 live nodes**: root, active
visual container, 32 source containers, skin child and shadow child. Their light
reference has no matching local SLight (table count zero), so original lookup
returns NULL and creates no light. A light reference is not evidence of a live
light node. The controller UIDs are `pthug-mesh-skin` and
`pthug_jail-mesh-skin`, distinct from their base geometry UIDs.

Factory/identity evidence:

| Original operation | Address and preserved behavior |
| --- | --- |
| Default createScene/root constructor | 0x29d215 / 0x29d135, independent identity root |
| constructVisualScene/constructNode | 0x296cf1 / 0x29692d, ordered active visuals, generated instances before authored children |
| Default createNode/CSceneNode | 0x29d25d / 0x2aee4d, unchanged source TRS and UID/scope/display distinctions |
| createMeshNode/createSkinNode | 0x29d3b1 / 0x29d4a5, NULL-SNode generated identity children |
| ordinary/skinned node tags | 0x2a27b1 / 0x2b255d; ordinary filename predicate versus fixed skinned tag |
| skin controller runtime UID | CColladaSkinnedMesh 0x2b17e9 copies SController+4 |
| UID/scope lookup | 0x273021 / 0x2735b9, self-first exact strcmp then ordered depth-first, first duplicate wins |

Root tag is 0x72656164, source/visual container 0x6e656164, ordinary mesh
0x6d656164 (or filename-special empty tag 0x79746d65), skin 0x73656164. Runtime
tags are not source instance kinds or game-object categories. Root UID is the
resource filename. Visual containers have empty UID/scope and their display name.
Generated skin UID is its controller UID; ordinary UID is its geometry UID.
Lookups are case-sensitive and retain C-string first-NUL behavior.

Each ordinary factory construction owns a separate mutable geometry resource.
Each skin construction owns a separate skin resource and ordinary base resource,
including repeated instances of the same controller or different controllers
using the same geometry. Only the decoded geometry/controller data is shared;
buffer state, matrix cache and dirty state never use a UID resource cache.
DefaultFactory createGeometry0x29d2a5 and createSkin0x29d061 allocate on every
call; constructGeometry0x2924b5 and constructSkin0x2924d5 do not look up an
existing runtime resource. A second live root also has separate mutable
resources/pointers. Source indices never replace these live identities, and
native pointers are never narrowed into 32-bit integers.

## Local TRS and cached transforms

Position/rotation/scale setters copy values and set flags 0x20/0x10/0x08,
respectively (0x2736b9, 0x272a05, 0x272ae5). They do not refresh any absolute
matrix. `absolute()` returns the actual cached matrix, including stale values.
`updateAbsolutePosition(false)` does not update ancestors or descendants.

The relative cache follows literal 0x2738b1: inspect low mask 0x3f; if mask0x1b
is nonzero rebuild the original quaternion/scale/TRS matrix; otherwise replace
only cached translation. Clear only 0x38 afterward. Persistent animation masks
1/2/4 are separate from setter bits. In particular raw animated-scale mask4 alone
takes the original translation-only branch and retains the old basis. Replacing
it with a setScale call would change behavior.

Absolute update 0x273141 recomputes if parent's flag0x40 or own mask0xbf is set,
using the parent's current cached matrix and the current relative cache. It sets
0x240 and clears0x80. Recursive=true then visits children in order. Native code
reuses verified `retailSceneRelative`/`retailSceneMultiply`; no renderer skeleton
sampler, normalization, epsilon for dirty detection or implicit ancestor refresh
is substituted. The returning quaternion matrix convention differs from the
output-argument engine overload and is preserved by the shared helper.

Actual default constructors initialize the cached matrices with false identity
hints, including numerically identity matrices, and the exposed setter/update
path retains that hint behavior. Literal generated identity-child multiplication
is preserved, including signed-zero additions. This claim does not extend to
unported external renderer/box operations that can change original matrix hints.

## Frame, visibility and animator ownership

Local visibility, inherited visibility and effective visibility are distinct.
Base setVisible0x27305d and notifyVisibilityChanged0x2730d1 propagate effective
changes in child order while preserving each child's local choice. A locally
visible child remains effectively hidden under a hidden parent.

Mesh overrides **both** operations (0x2a2771, 0x2a2b8d). When their incoming bool
is false they write buffer byte9=0, including repeated explicit hides and
inherited false notifications. Buffer bookkeeping is owned per live resource,
not written into shared decoded CityMesh. Full ordinary/ref/proxy constructor
scans show byte8 initialized but no byte9 initialization; native byte9 begins
explicitly unknown (`optional`) until a renderer establishes it or a hide sets
zero. This does not pretend that process buffers or rendering have been ported.

Base OnAnimate0x273335 returns for an invisible node or option1. Otherwise it
dispatches ordinary animators in live list order, updates this node(false), visits
children in order, then clears flag0x40. Appending an ordinary animator during a
callback can execute it in that same frame. Erasing an active animator list or
recursive same-node dispatch is outside the supported safe iterator domain and
is rejected explicitly; target/TRS/visibility/option mutations remain live.
Removing independent bound-list retention during ordinary dispatch is supported
and invalidates the affected targets before dropping that retention.

Ordinary and skin mesh node overrides (0x2a2749, 0x2b25cd) gate effective visibility,
call base OnAnimate, then call the underlying mesh even if base returned for
option1. The plain CColladaMesh vtable resolves to IColladaMesh::onAnimate0x2b2e0d,
literally `bx lr`. Skin onAnimate0x2b0de9 calls that base then sets its real matrix
cache dirty. No empty invented host is used for this proved empty operation.

Ordinary `addAnimator`0x274111 retains each entry (duplicates allowed), then calls
onBind. Bound `addBindedAnimator`0x273511 deduplicates and retains once per node,
and does not add frame dispatch. The typed animation TRS accessors retain in the
bound list and set persistent mask1/2/4. Both single removal APIs call onUnbind
before dropping retention (0x2732c5/0x273265). Masks are not silently cleared.

The animator interface requires real animate/onBind/onUnbind operations. It is a
lifetime/dispatch seam, not an implemented full Collada animator. Native shared
ownership implements list retention; it does not expose original allocator or
reference-count object layouts. Targets are borrowed references into this scene.
Owner teardown invalidates retained targets while **every** node/matrix still
exists, then releases resources and nodes. Callback target invalidation is
mandatory and noexcept. Callbacks must not mutate lists during unbinding. All
body/contact/renderer borrowers must release their node pointers before owner
destruction. This safe owner policy does not claim exact ARM unload order.

## Actual skin joint pointers and matrix cache

Default root postLoad updates the graph before attachSkin. Original attach
0x2b1ba1/prepareSkeletonMtxPtrCache0x2b1b09 resolves each original joint by **scope**
from the actual root, storing a pointer to that live node's cached absolute
matrix. Native caches own stable joint node/pointer arrays. Missing joints reject
construction instead of retaining original NULL pointers for later dereference.

prepareSkeletonMtxCache0x2b1bb9 computes jointAbsolute * inverseBind, followed by
bindShape only when the original constructor's isIdentity result is false, then
clears dirty. GOT+0x468c is collada wrapper0x2b12e9; GOT+0x498 resolves its literal
rowMatrixProduct34 at0x2b0e15. Its affine arithmetic matches the shared multiply
helper; it is not a justification to use a guessed full4x4/renderer product.
Bind identity uses original isIdentity0x243bb5: near-one diagonal comparisons
and off-diagonal abs<=1e-6, not exact byte equality. Boundary cases are compared
against execution of that original function. Default skin-enabled state is proved
by actual IColladaSkinnedMesh base constructor0x2a3521 (+37=1).

Updating a joint absolute matrix alone does not dirty a previously computed skin
cache. A visible skin frame marks dirty; explicit `skinMatrices()` refresh then
reads current cached joint matrices. It never refreshes those nodes implicitly.
Per-instance dirty state and matrix values do not leak between repeated
instances in one root or between shared-source roots.

Original FindAMesh0x2dc175 updates each visited node(false), accepts the two mesh
tags except case-sensitive `Plane`/`Jump` UID substrings, and otherwise recurses
in order. **It has no visibility predicate.** Its earlier research description as
a visible-mesh search was too broad; native code follows the actual instructions.

## Validation and current boundaries

Original ELF SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.

Native tests cover both supplied models, every22-joint mapping, two live roots
sharing immutable geometry, exact UID/scope/display distinctions, duplicate/case/
NUL lookup, stale/nonrecursive cache semantics, signed-zero/nonunit TRS, inherited
visibility, option1 skin behavior, matrix staleness/refresh, list retention and
safe target invalidation. Independent original execution compares 720 supplied
and80 synthetic node snapshots,44 final skin matrices, the second instances'
separate pointers/unchanged caches,21 bind-identity cutoff cases and4 raw
animation-mask cases. Actual original list/buffer/constructor controls verify
retention callbacks, same-frame append and both hide overrides. Persistent
repeated-instance regressions cover ordinary geometry, the same skin controller,
different controllers sharing a base geometry, independent buffer state and
separate lazy cache refresh against the same genuine live joint. A direct
original geometry factory probe independently verifies fresh resources from the
same database/root/source; the node oracle retains separate intercepted resource
instances instead of imposing a per-UID cache.

Reference tooling supplies zero-buffer CPU geometry for constructor/material
isolation and explicit raw source records/import/math/allocation boundaries. It
executes actual default root/container/ordinary-mesh-node/skinned-mesh-node
constructors, addChild, getters, setters, lookup, update/frame traversal,
attachment and matrix-cache bodies. Every ordinary resource in the revised node
oracle is constructed by the original geometry factory/full zero-buffer mesh
constructor. Skin resource records in that oracle are supplied separately per
construction, with their actual original methods. A separate factory probe
executes repeated actual skin allocations, constructor/base-geometry creation
and root registration, intercepting only the CPU skin-buffer preparation stage.
It establishes separate skin/base identities but does not validate that omitted
buffer stage. Neither probe executes the full resource/material pipeline,
original allocator teardown, CPU vertex/normal skinning or rendering. These
boundaries are explicit in ignored `port/build/OG-049-revised-*.py` and logs;
no ARM or Python runtime dependency ships with this owner.

Supported construction is the local unoptimized DefaultFactory path. Alternate
SNode factories, external mesh factories, node/modular/morph/particle/force
instances, morph resources, embedded scene animators and genuinely resolved
lights are explicit unsupported errors. Proven ignored kinds and missing local
light/spline NULL results remain ignored. No dummy live light or morph is added.
The authoritative skin decoder retains its existing strict input domain; for
example it rejects zero-influence vertices in the supplied car_limo skin. This
Player prerequisite is not a claim that all83 skin controllers decode.

Current source geometry exposes the genuine raw bind-pose box. Original dynamic
skinned computeBoundingBox, CPU skin buffers, renderer upload/material/layer/color
effects, shadow registry, manager attachment, pool/streaming/context switching and
full actor pose/controller composition remain separate work. There is deliberately
no fabricated dynamic skinned-box getter or complete animation scene host. The
existing clock/catalogue/verified pose work must compose through these real live
references and original body/frame ordering; this owner does not duplicate them.

Release/Debug builds, full CTest and retail audit results are recorded in the
submission report. No manual gameplay or mission progression was used.
