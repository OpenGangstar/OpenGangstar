# Character weapon reload and animation dispatch

`checkRetailCharacterWeaponReload` implements the original Character decision
against the actual `RetailWeapon`/`RetailWeaponAmmo` owners. It owns no clip,
ammo, category, NPC state or animation clock. The required live host supplies
the genuine virtual weapon lookup, current level classification, conditional
NPC word 428 read and virtual animation operation. There are no default reload
or animation callbacks.

The returned weapon must stay alive for this serial call even if a callback
replaces the equipped pointer. The caller supplies the canonical ammo owner
and real reload-sound implementation from the existing weapon component.

## Decision and read order

The original virtual `getWeapon` is called exactly once. A null result returns
immediately. Weapon type is captured from that returned object before the
clip guard. Type 0 or clip>0 returns before any category/ammo reads.

Only then is the live level category read. Category 8 selects the Player branch:
reload occurs only when the canonical manager's public AnyContext ammo count
is positive, and calls the actual weapon's `reload(false)`. Every other
category calls the actual `reload(true)`, bypassing reserve access according
to the existing weapon leaf. Manager unlimited policy and per-role unlimited
remain different original behaviors. Signed/negative clips are not clamped.

After actual reload and its mandatory sound return, animation dispatch uses
the type captured before reload. It does not fetch a newly equipped weapon or
overwrite callback effects.

## Animation selection

`setRetailCharacterReloadAnimation` implements the complete original switch:

| Captured weapon type | Animation |
| --- | --- |
|1 |56 only for live category 5 and live NPC word 428==3; otherwise51 |
|2,6 |60 |
|3 |76 |
|4 |75 |
|5 |51 |
|7 |67 |
|8 |68 |
|Other, including0/negative |No call |

Every request has speed 1, loop=true, rootMotion=false and
directionalMotion=false. Category is reread only for type 1, after reload/sound;
NPC state is read only if that late category equals 5. Other types and unknown
values do not make speculative category/NPC reads. Level category means the
live `LevelObject::s_type[getObjectType()]` classification, not metatype IDs,
physics mask bits or source-scene tags. Category 5 is NPC, category 8 Player.
Word428 retains its observed name; this component does not invent broader AI
state semantics for it.

The required animation host must perform the genuine virtual Character
operation using the actor's retained existing binding/clock and preserving any
specialized behavior. This component merely supplies the exact existing
`RetailAnimationRequest`. It does not bind an animator or provide a fake
successful host. No state commit follows the animation callback, so its effects
also survive.

## Original evidence

`Character::checkForWeaponReload` is at `0x2f207d` and
`Character::setReloadWeaponAnimation` at `0x2f1f9d`. The actual recovered
`Character.cpp` definitions agree with their original instructions.

At `0x2f2084..0x2f2088`, the weapon is obtained through vtable+216. The original
retains that pointer in r5, calls the type getter and retains type in r7 before
the clip guard at `0x2f20a4..0x2f20aa`. Category is read afterward from the live
table at `0x2f20b2..0x2f20b8`. The non-Player call reloads with true at
`0x2f20bc..0x2f20c0`; the Player branch calls public AnyContext ammo and then
reloads with false at `0x2f20ce..0x2f20ee`. Both pass retained r7 to animation
dispatch afterward.

Type 1's late category read is at `0x2f1fb4..0x2f1fc2`; the conditional NPC
word 428 read is at `0x2f1fc4..0x2f1fcc`. Animation dispatch uses actual vtable+160
with float1 and the original three boolean arguments. Original instructions
reject unknown signed values before indexing the jump table.

## Validation and boundaries

Independent original execution compares 4,408 complete native/ARM traces and
final states per configuration: actual virtual weapon lookup, actual category
table/NPC memory reads, required sound endpoint, exact animation ID/float bits/
boolean arguments, clip/ammo and final category/NPC/equipped identities. Cases
include null/type 0/positive guards, signed clip extremes, protected ammo type 6,
manager versus role unlimited, negative/unknown types and callback mutations.

Reload sound can replace the equipped weapon and change category/NPC state;
the captured old weapon/type still drives dispatch while the late NPC selector
sees the changed live values. A getter callback mutation and a final animation
callback mutation are also checked for surviving effects.

A separate original-only probe changes the actual captured weapon's type
during sound and proves that dispatch still uses the old captured type.
The native loaded-type API intentionally remains read-only: native tests use
equipped-weapon replacement and source inspection of the captured local,
without private-layout bypass or a test-only mutation seam. No unsupported
native actual-type-mutation fixture is claimed.

This completes the bounded Character functions given genuine required hosts.
Canonical actor/weapon lifetime and Player context composition, the live NPC
role, real sound playback, actual animation binding/pose effects and the wider
Character update remain separate integration work. No Scene/Graph/Pose,
weapon scalar or clock/store implementation was changed, and no manual
gameplay was used.
