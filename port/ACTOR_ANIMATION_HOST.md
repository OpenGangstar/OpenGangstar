# Concrete Character animation scene host

`RetailActorAnimationHost` implements the existing
`RetailActorAnimationSceneHost` with the genuine retained scene, plain pose,
global graph, mutable timeline and prepared material owners. It creates no
actor clock, sampler, clipped-track copy, parallel binding or renderer actor.
The canonical actor owner constructs the host before the existing binding:

```cpp
RetailActorAnimationHost host(actorScene, graph, animationGlobals);
RetailActorAnimationBinding binding(animationGlobals.catalog(), host,
                                    playerState.animation, playerState.movementFlags);
```

The graph and globals outlive the host; the host retains its explicit actor
reference. The binding borrows that host's model and the globals' catalogue.
Weapon selection, controller/body operations and clock state remain their
existing real bindings. For manual animation evaluation, temporarily retain
`host.pose()` and pass that same object to `binding.advance(..., pose.get())`.
Apply the returned body/root-strip operations in the original caller order;
this scene host does not consume them or advance a second clock.

## Actual factory and bind operations

The matching ELF SHA256 is
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
The original const/default animator factory at `0x297c51`/`0x29d395` creates a
fresh plain animator plus a real mutable timeline. Character `bindAnimation`
at `0x2f23fd` then performs this order, preserved by the existing binding:

1. Construct the full source-order pose from `./anim_ingame.bdae`. Its sole
   actual optional timeline member receives a fresh `RetailTimeline`, backed
   by an alias of the canonical immutable catalogue owner.
2. If no application-scoped shared timeline exists, retain that exact attached
   object and initialize fists indices `{144,94,92,93,95,90,91}`. Later factory
   timelines do not replace it or reset current fists state.
3. Clear the pose's actual timeline field before binding. The first object
   survives through global ownership; later attached objects die on detach.
4. `RetailActorPose::bind` invokes real onBind/forceBind on the actual actor
   root. All 24 source tracks bind to 23 unique Player nodes. Typed local
   references/masks and node-held ownership are the existing genuine pose path.
5. Drop the local factory reference. The host keeps a weak pose handle;
   permanent retention is supplied by the 23 actual node lists. A call to
   `pose()` is an explicit temporary grab, including while factory-local.
6. Resolve the actual root and shadow by live UID. Set the actual first shadow
   child's rendering layer, then mutate the genuine prepared material owners:
   black selector3, white selectors0/1/2, flags `0x400=false`, `0x200=false`,
   `4=true`, `16=true`.
7. Reparent that same already-bound shadow to the global graph, preserving all
   target, matrix, node, mesh and material addresses. FindAMesh then traverses
   the remaining actor and returns the actual skin node. It does not return
   an immutable model index or accidentally select the moved shadow mesh.

Outer Player setup attaches the actor root later (`0x301db4`–`0x301dba`), after
Character binding at `0x301cea`. The host never adds that actor edge early.
The graph's resulting order is shadow then actor. There is no ordinary-frame
animator registration or extra skeleton evaluation.

`RetailActorAnimationGlobals` owns the immutable indexed catalogue and full
source library plus the actual shared mutable timeline and fists state. Its
resource constructor reuses existing readers; callers may instead supply their
canonical shared owners. `clearSharedTimeline()` models
`cleanTimelineController` at `0x2f14c1`: release the shared timeline, preserve
fists and leave detached poses alive. A later first binding initializes fists
again. Neither a loaded catalogue nor Character's detached clock is substituted
for the mutable timeline.

## Bound shadow ownership

The legacy graph APIs keep their original explicit rejection of bound-subtree
transfer. A new overload accepts an actual `RetailActorPose&`, checks its weak
scene owner and root identity, and requires the actual shadow in its target
slots. Extraction permits only that same pose in moved bound lists; unrelated
bound owners, ordinary frame animators, skin/joint dependencies and external
records remain rejected before ownership changes.

Transfer does not call onUnbind, copy targets or alter masks. The independently
retained subtree owns its actual nodes/resources and immutable source; its node
list retains the pose. The pose keeps only a weak actor owner. Dropping the
actor therefore invalidates its remaining targets before freeing records,
leaving only the shadow target and one pose reference alive. Evaluation after
that owner expires fails before dereferencing the stored root. Dropping the
shadow then unbinds that last target before releasing the pose. Conversely,
shadow-first destruction clears only its UID slots while the actor remains
sampleable. No shadow edge secretly retains a dead actor.

## Timeline attachment boundary

The real optional member implements original `setTimelineCtrl`/getter ownership
at `0x288249`, `0x288285` and `0x288289`. Replacement drops the old field before
assigning the caller's explicitly retained incoming reference. The host has no
second session timeline field.

Original `animateNode` at `0x2af0a1` only forwards its supplied time to apply;
it does not inspect the timeline. Existing apply/animateNode arithmetic remains
unchanged. The separate general attached-time/event path at `0x2af435` and base
`0x2b3301` is unported and unexposed here; it is not replaced by a no-op.
Character detaches before binding and manually evaluates its pose, so that
general event path is outside this host's operation.

## Validation and limits

Public-operation tests cover actual attach/first-retain/detach/local-drop,
subsequent timeline destruction, weak pose ownership, invalid/foreign/extra
bound owners, independent actor/shadow destruction, materials, masks and
canonical clock composition. Both actual Player graphs match whole original
bind execution: 48 typed target references, 72 nodes and 1,872 exact local TRS
and cached matrix words, plus masks, lists, layer, prepared scalar materials
and parent/child relations. Original probes also prove 23->1->destruction pose
retention and global timeline cleanup while both poses remain callable.

The omitted original post-load box phase found by this comparison is supplied
in the scene constructor and documented in `ACTOR_FINALIZER.md`.
The original reference's application/device/manager getter returns a genuine
root interface fixture; its global parent identity is compared as that explicit
boundary, not by the fixture's resource-derived UID. Every actor/node UID,
target and local hierarchy comparison remains exact. Imported allocation,
file-manager and soft-float/CRT boundaries remain research-only; no interpreter
is linked into the native runtime.

The default Player/Bip01 path is supported. Fresh host construction is required
for another complete binding session; general NPC roots, live model switching,
node-pool policy, weapon behavior, attached event dispatch, renderer submission
and full outer Player Update are separate work. Invalid setup/failure after
binding or ownership transfer is not a rollback-safe game session. Release
consumers before their explicitly borrowed owners; do not destroy an owner
during dispatch. No gameplay progression is used as validation.
