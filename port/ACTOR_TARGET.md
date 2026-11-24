# Actor target-facing and post-physics scene update

This component implements `PlayerTest::isFacingTarget` at `0x3025cd`,
`PlayerTest::UpdateSceneNode` at `0x302685`, and
`NPC::UpdateSceneNode` at `0x2fd801`. It calls the existing genuine
`updateRetailActorPosition` implementation of `Character::updatePosition`
at `0x2f42cd`. It supplies neither target selection nor an entity manager.

The facing method is a predicate. It never turns the actor or writes rotation.
The Player scene-update method ignores its result. An automatic turn would
change the original behavior.

## Required live bindings

`RetailActorTargetLookup::currentTarget()` must resolve the common Character
target each time it is called. The original field is Character offset 260,
which is Player wrapper offset 264 because its Character base starts at +4.
Original `_GetTarget` at `0x300db9` simply loads that field.

A returned null pointer means an actual known null target. Unimplemented lookup,
an expired nonnull entity identity, and missing position binding must raise an
error. They must not become a fabricated no-target result. The concrete game
entity owner and canonical producers remain required integration work.

A nonnull result is a lease on the selected entity's position endpoint. It must
keep the entity and its current virtual position binding valid during the query.
Holding a previously loaded scene after the entity has been pooled or replaced
does not satisfy this contract. The endpoint returns position by value. A target
getter may have live side effects; its result is snapshotted before the actor
getter runs. No default endpoint or scene-snapshot registry is provided.

The caller binds the complete `RetailActorHeadingHost` and actor node to the
same fixed live entity for each serial operation. Heading position and Front
use the original cached absolute getters; no early matrix refresh is allowed.
Actor, shadow, optional body, retained floor and height state must satisfy the
ownership requirements of `updateRetailActorPosition`. In particular, the
shadow is the real separately owned global shadow, not a replacement node.

## Literal call and arithmetic order

`retailActorIsFacingTarget` performs a fresh target lookup, snapshots target
position, then obtains actor position. It subtracts Y before X and sets direction
Z to positive zero. It uses the shared original vector normalization at
`0x20a2d1`, obtains Front, replaces its Z with positive zero, and normalizes Front
with the same function. The dot product is grouped as
`(frontX * directionX + frontY * directionY) + frontZ * directionZ` and compared
strictly against `.9f`, whose bits are `0x3f666666`. There is no epsilon,
distance threshold, alternate zero-vector direction, or double threshold.

`updateRetailPlayerSceneNode` first obtains a fresh target lease solely for its
nonnull check and releases that temporary before entering the predicate. When
nonnull, the predicate performs the second fresh lookup. The boolean result is
discarded. The wrapper then snapshots all four live quaternion components and
calls `updateRetailActorPosition`. Facing therefore reads the old cached actor
position before the body-position readback. The NPC wrapper only snapshots
rotation and calls the same position implementation; it performs no target call.

The caller applies the existing `finishRetailPlayerPhysics` gate around this
entire Player stage. A rejected stage skips target, facing, rotation and position
work; the existing linear-velocity clearing still occurs. These wrappers do not
step the world, sample animation, run a late scene frame, refilter bodies, cancel
destinations or add a second floor/height query.

## Checked lifetime adaptations and remaining producers

The original target field and both original setters use borrowed pointers.
This leaf uses an explicit query lease instead of reproducing unsafe dangling
dereferences. The outer check does not retain its old target across the second
lookup. A lookup failure or null predicate target raises an error before any
later getter/readback. The original predicate unconditionally dereferences its
target, so returning false for that invalid case would hide a broken binding.
No rollback of side effects already performed by a provider/getter is promised.

Inherited `Character::SetTarget` at `0x2f147d` is a raw store. The richer
`PlayerTest::_SetTarget` at `0x301261` also performs NPC notifications, camera,
combat-idle and Lock operations; it is not interchangeable with that virtual
setter. Acquisition, same-target remove/readd notifications, disable-before-pool
release, and current-target changes during those callbacks still need genuine
entity/manager ownership. `onTargetCancelled` at `0x301569` does not clear the
common target field. This component makes no full target-manager or gameplay
integration claim.

## Validation

The strict-FP test covers cardinal/opposed/zero/vertical/signed-zero inputs and
original vectors producing normalized dots immediately below, equal to, and
above `0x3f666666`. It checks target-before-actor getter order, no rotation
writes, second-lookup replacement/failure, lease release and survival, NPC
absence of target calls, the outer physics gate, and copied quaternion timing.

Real native Box2D bodies and fresh nonzero `char_pthug.bdae` and
`char_pthug_jail.bdae` scene graphs exercise actual cached getters, shadow state,
and body readback. Original ARM execution independently checks the same predicate
and wrapper cases, including a required getter effect and quaternion-copy
observer. The reference harness uses typed game/body fixture records and
documented resource/import isolation; it is not a full game entity manager or
world simulation. Reference artifacts and retail bytes are excluded from the
native build. No manual gameplay is used.
