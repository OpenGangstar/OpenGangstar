# Common Character target and rich Player transitions

`RetailCharacterTarget` is the one common borrowed target component for a
canonical Character entity. Its constructor binds that actual owner by
reference and establishes known NULL, as the original Character constructor
does. The owner outlives the component. A Player does not have a second target
field or a source-scene target snapshot.

## Canonical identity and checked lifetime

`RetailTargetEntity` extends the existing target-position interface with live
level classification. It must be implemented by the actual canonical game
entity owned by the eventual object manager. A scene adapter, retained old
position, physics-body user-data proxy or source UID is not that entity.
`getPosition` must resolve the entity's current genuine virtual position
binding, including after scene replacement/pooling. This patch provides no
concrete fake entity, registry or default position/category implementation.

The field contains one weak entity reference and a known-NULL discriminator.
`entity()` and the existing OG086 `currentTarget()` resolve it anew on every call.
Known NULL returns null. Expired nonnull identity throws instead of silently
becoming no-target. A returned lease retains the actual entity and calls its
current position operation; it does not preserve an obsolete scene snapshot.

The eventual canonical Character owns this same component, and the eventual
object manager owns this same entity. The entity manager and disable/cleanup
producers must maintain that association and clear appropriate references at
their original lifecycle points. Weak references expose an incorrect missing
lifecycle transition rather than pretending deletion always clears the target.

`setRaw` implements inherited Character.SetTarget: it stores the borrowed
identity without notifications, camera/combat/Lock effects or old-target
dereference. It can clear an expired nonnull old identity. This differs from
the rich Player operation, which must first resolve/classify its old target
and therefore fails explicitly if that entity has already expired.

## Rich Player operation

`setRetailPlayerTarget` uses that same component. The incoming shared lease is
a value parameter, capturing the original identity before callbacks can
change a caller-owned slot. Notification source is derived from the component's
bound canonical Character owner, not an independently supplied actor/store
pair. There is no same-pointer early return.

The operation performs the following original sequence:

1. Resolve the old target and, if its live level category is5/NPC, notify it
   with targeted=false and the bound Character identity.
2. Store the captured incoming pointer through the raw common store. This
   overwrites any old-notification target replacement, as in the original.
3. For nonnull incoming, write byte 354=1 and byte 355=0, preserving byte 353.
   Classify the captured incoming entity live and notify it when category 5.
   With force=true, freshly look up Camera and enter mode 1. Then call the
   required CombatIdle operation on the bound Character.
4. For null incoming, write byte 354=0 and byte 353=0, preserving byte 355. Look
   up Camera and read its mode. Enter mode0 if it is 1, or when force is true
   and the current canonical flags240 mask 0xfe is clear. If entering, perform
   a fresh Camera lookup; do not reuse the earlier owner implicitly.
5. Look up the real Lock owner first. Only afterward resolve the CURRENT common
   target and pass it to that Lock's real setTarget operation. Notifications,
   camera, CombatIdle and even Lock lookup can have changed the common field.

Camera entry receives the actual bound Character, literal 0.3f and the original
false trailing flag. Required Camera, NPC, CombatIdle and Lock interfaces have
no defaults. They must implement the genuine game operations, including
actual AI, camera, combat and billboard/parent effects; simply recording a
pointer is not a complete production host.

Flags240 is borrowed by reference and read at its actual late decision point.
`RetailPlayerTargetBytes` retains the three opaque bytes as optional raw values.
Unknown constructor values stay unknown until an evidenced write; they are
never guessed zero. A null branch preserves unknown 355, while a nonnull branch
preserves unknown 353. Callback byte/flag writes remain live and survive unless
the original sequence explicitly overwrites those fields.

## Leases and original ownership boundary

The original raw/rich stores contain no direct entity grab/drop. Native fields
remain weak. Old/new/final entities receive transient serial leases for safe
required operations; these are an explicit checked-lifetime adaptation, not
original strong field ownership or exact reference-count parity. The old lease
ends after its classification/notification. Captured incoming lasts through
the operation; final Lock input is freshly leased after Lock lookup. Releasing
all genuine owners during callbacks can leave an explicitly expired common
field after the serial lease ends. It never silently becomes known NULL.

An active lease can delay physical C++ reclamation after manager ownership is
released. Dropping a manager shared pointer is therefore not proof that the
original destruction effects have executed at the right time. Actual GOM
destruction/invalidation and active-lease policy remain the canonical owner's
responsibility, outside this component; no extra alive registry is invented.

Unknown indirect ownership inside full NPC/camera/combat/Lock implementations
is not inferred. Their eventual implementations must preserve genuine behavior
without introducing a strong target field or a second Character target cache.

## Source evidence and tests

Inherited raw setter `Character::SetTarget0x2f147d` stores Character+260. Player
getter `_GetTarget0x300db9` loads wrapper+264, the same field because its
Character base begins at wrapper+4. Character construction initializes it NULL;
Player construction separately calls the rich operation for its side effects.

Rich `_SetTarget0x301261` captures incoming in r6 before callbacks, classifies
old/new targets through the actual live s_type table and performs the sequence
above. At `0x3012c8`, it looks up Lock; only `0x3012d0` then reloads the current
field. The null Camera lookups are `0x301318` and conditionally `0x301332`.
These read/lookup boundaries must not be folded into a snapshot argument.

Independent original execution covers 371 rich traces, including distinct
old/new and same NPC targets, null/non-null branches, force/mode/flag masks,
target changes at old/new notification, camera, combat, Lock lookup/set, and
byte/flag mutations. Twelve raw setter/getter cases include clearing a dangling
old pointer without dereferencing it. Native Release/Debug compare exact
required-operation traces, current Lock target, final bytes and flags.

Native-only checked-lifetime tests distinguish known NULL from expiry, prove
the field does not strongly retain targets, verify actual-entity fresh position
leases, preserve unknown constructor bytes, capture incoming before caller-slot
mutation and retain it only for the serial operation. Those adaptations do not
claim original dangling-memory/undefined behavior equivalence.

This completes the bounded common state and rich transition given genuine
required hosts. It does not complete the canonical entity/GOM owner, target
acquisition or lifecycle producers, NPC AI, camera, CombatIdle animation or
Lock rendering implementation. No existing OG086 facing/scene behavior, scene
graph, pose, clock or source decoder is changed. No manual gameplay is used.
