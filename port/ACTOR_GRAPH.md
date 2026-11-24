# Global actor and ordinary-shadow ownership

This component implements the bounded original global hierarchy path for
default-factory actors and their authored ordinary shadow subtrees. It owns
real mutable nodes, ordered links and root-edge lifetime references. It does
not implement camera/culling/render submission, Character's material setup,
the full SceneNodeManager pool, or a second animation sampler.

`RetailActorGraph` owns an actual global node with original scene-manager type
0x72676d73. Its child list is the authoritative hierarchy for lookup, visibility,
collection and late OnAnimate. The accompanying typed root-edge ownership map
holds the actual actor scene or moved subtree lifetime, not callbacks or copies
of their geometry. `retain` supplies an explicit extra owning reference for
original grab-style inspection/reparenting. A shadow reference owns only the
moved subtree plus immutable decoded source, never its former ActorScene.

`registerShadow` physically transfers unique node and fresh ordinary mesh
ownership out of the caller-retained or active actor's vectors. Addresses, matrix references,
source indexes and allocation IDs stay unchanged. Owner vectors remain dense
iteration lists; an allocation ID must not be treated as a current vector
position after transfer. Unsupported skinned, externally borrowed,
animator-bound or skin-joint-dependent subtrees reject before transfer. Actual
authored Player shadows are the supported ordinary route. Only the first
generated child gets rendering layer1; the container and later children retain
their original layers.

The shared-actor overload accepts the genuine caller-held construction owner
before its root enters the global hierarchy. It registers only the moved shadow;
it never appends the actor root or retains the actor through the shadow. This
matches PlayerTest::switchContext: Character binding at0x301cea registers shadow
through0x2f2516; actor-root attachment follows at0x301db4..0x301dba. Consequently
the global child order is shadow, then actor. The reference-only overload keeps
its existing already-active-actor precondition. Both use the same extraction and
append implementation; no early actor attachment substitutes for the new path.

Original addChild0x273479 retains before removing the old link, appends the same
node and calls setParent0x273415, then notifies the manager and propagates new
inherited visibility. Local TRS and cached matrices remain stale and unchanged.
The native lifetime reference is retained before removal. Removal0x2731f5
clears only the removed root's parent/manager, drops its actual parent-edge owner
before erasing the child record and notifying hierarchy change. Descendant
manager values and inherited visibility remain unchanged until reattachment.
Cached collection arrays likewise retain their previous borrowed node pointers
until a real collection rebuild; callers must not dereference removed entries.
An absent removal performs no drop or notification. Same-parent add after a
pointer-only setParent preserves the original nested-removal quirk: the newly
appended child record is removed, leaving a parent pointer without membership.
An explicit setParent(null) followed by addChild restores a normal edge.

setParent is literally a parent-pointer operation: it removes the old link,
assigns parent/flag0x80 and changes manager when necessary. It does not append
to the destination children or propagate visibility. Its caller must retain
the node owner and borrowed parent. setSceneManager0x2727c9 derives each node's
manager from its current PARENT; the passed argument is not authoritative.
Supported default nodes use the truly empty onChangedSceneManager0x217311;
camera overrides and other scene classes are outside this component.

The actual manager's hierarchy-dirty state represents original byte980 and
starts true, as its constructor0x21b575 writes at0x21b90a.
collectAllNodes0x219535/0x219a89 rebuilds preorder lists without a visibility
filter, excluding original empty/dummy/container/manager tags from the first
list and adding ordinary-animator nodes to the second. Bound animator retention
does not enter that list or invalidate it. Ordinary animator add/remove notifies
after onBind/onUnbind/list operations in the original order. removeAnimators
0x27409d unbinds and drops each ordinary animator before visiting the next,
retaining the old list count until all records are cleared. It notifies even
when the list was empty. Bound-list removal remains separate.

lateFrame accepts the already-converted original unsigned caller time, rebuilds
dirty hierarchy collections, and calls the actual root.onAnimate exactly once.
The original Application::_Draw0x2d4fe1 dispatches manager update at0x2d5054
with sentinel -123456 and false. update0x219b09 rebuilds dirty collections before
the false-pass global OnAnimate. This API takes the time after that caller
conversion; it does not schedule another actor sampler.
The caller's float timer/sentinel conversion and the separate OnUpdateTime true
pass are not substituted. The existing manually sampled bound pose is not added
to ordinary animator dispatch merely to update global shadow caches.

releaseActor takes the genuine caller-supplied s_nodePoolID byte.0xff removes
the actual global shadow edge before dropping the actor edge. Other byte values
leave the shadow edge retained independently. Visibility/show/despawn and pool
slot policy remain the caller's original operations. If no external actor owner
exists, actor teardown invalidates and frees its remaining pose graph while a
pooled shadow and its immutable source survive. Final shadow removal drops that
subtree's resources at the last real owning reference.

The weak manager-state guard is a native lifetime adaptation for original
borrowed manager pointers. It prevents dereferencing an expired manager while
preserving root-only clearing on ordinary remove. Explicit reference/parent
lifetimes and unsupported reentrant hierarchy mutation are checked native
domains; they are not claims of parity with legacy invalid-pointer behavior.
Explicit destructor invalidation of the remaining graph occurs while all its
records are alive. Destruction skips manager notification only if that native
borrowed manager guard has already expired. Global-root ordinary animators are
invalidated while its children and manager still exist.

Transfer preflight rejects unsupported dependencies before moving records and
reserves both ownership vectors before assigning the detached root. Failure
after a successful transfer but before append finishes destroys that extracted
subtree; it is not a transaction restoring the old actor hierarchy. The code
does not claim legacy allocation-failure parity.

This implements only supported default ordinary-shadow extraction and root
attachment. It does not run complete Character::bindAnimation or install a
complete animation/material/render host. Cameras, generic skinned or animator-
bound subtree transfers, external borrowed parents, the true OnUpdateTime pass,
Timer conversion, full node-pool eviction and rendered-shadow submission remain
separate prerequisites. Explicit caller-held pool references may reuse the same
actor and shadow identities without keeping a dead actor alive via its shadow.

Original source identity, focused original/native graph comparisons, real
Player resource gates and remaining limits are recorded in the ignored task
report. No manual gameplay is needed for those checks.

OG085 additionally compares both completed original nonzero Player factories,
including real onPostLoad, through five pre-attachment/registration/late-frame/
removal phases each:10 snapshots and12,240 exact node-state words plus manager,
child and cached collection order per configuration. Native tests cover caller
cleanup before any actor attachment and prove that the registered shadow and
material survive without retaining the dead actor. Existing original18-phase
graph profiles remain unchanged.
