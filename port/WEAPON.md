# Loaded weapon scalars, ammunition and reload

`RetailWeapon` owns the loaded scalar record and the sole mutable clip count
for one weapon. It consumes the authoritative `CityWeaponMetatype`; it does
not parse assets, construct a game object, attach a scene or create projectiles.
The composing canonical weapon must retain this state rather than rebuild a
clip snapshot for each operation.

`RetailWeaponAmmo` owns the initialized ammunition portion of the original
manager: two rows of seven signed32 counts and one unlimited policy, initially
zero/false. It borrows the canonical current Player context word by reference.
That reference must remain valid for its lifetime. It is not the complete
WeaponManager constructor or init, which also creates twenty game objects and
performs HUD effects.

## Scalar state

The loaded weapon type, ammo type, clip size, range, cone, damage, shoot
frequency and dry-shoot frequency remain signed32 values. Initial clip equals
the loaded clip size, including negative values. Clip and clip-size setters
write their separate original fields without clamping or implicitly reloading.
Double/two-handed predicates use the decoded loaded flags. Gun types are
1,2,3,4,5,6,8; projectile type is 7. Other firing flags, locking and readiness
state are outside this component.

## Ammunition and context

Each `getAmmo` and `setAmmo` independently resolves context=-1 from the current
borrowed Player word. Explicit contexts 0/1 bypass that lookup. There is no
cached context or second unlimited-aware reserve store.

`getAmmo` returns 1 only when actual stored count is nonpositive and manager
unlimited is enabled; otherwise it returns the actual signed count. `setAmmo`
skips a write when unlimited is enabled and stored>=requested. Ammo type 6 is
never written by this setter, even when unlimited is disabled. Negative and
extreme values are retained.

`counts()` exposes the sole actual raw store for later original producer and
restore composition. It does not implement those producers or save restoration.
Public get/set reject invalid type/context table accesses explicitly rather
than reproducing original out-of-bounds memory behavior. A valid explicit
context still works when the unused current Player word is invalid.

## Reload and mandatory sound

`reload(roleUnlimited, ammo, sound)` preserves original branch order:

- Per-call unlimited sets clip to clipSize without any ammo/context table
  lookup or write. It is independent of the manager unlimited policy.
- Otherwise it reads the current public reserve with AnyContext. Rocket type 7
  assigns clip 1 and subtracts1 only when reserve>0 or manager unlimited is
  enabled; an empty non-unlimited rocket retains its previous clip.
- Other types fill to clipSize and subtract it when sufficient or manager
  unlimited is enabled. Otherwise they assign clip=reserve and local reserve0.
- It calls `setAmmo` with AnyContext anew. Existing clip is never added back to
  reserve. Type6 and unlimited setter rules remain authoritative.
- Finally it calls the required reload-sound endpoint on the actual live
  weapon, including empty-rocket and per-call-unlimited branches.

`RetailWeaponReloadSound` has no default implementation. A genuine sound owner
must implement it for production composition. Its callback observes committed
clip/ammo state; mutations it makes survive because no deferred commit follows.
This component does not substitute a silent successful host.

Signed subtraction uses `retailSubtractInt32`, preserving the ARM signed32 bit
pattern without native signed overflow. The existing timeline helper and its
comments/arithmetic were moved unchanged into `retail_integer.h`, with only
the prefixed shared name and its three timeline call sites changed. There is
no general arithmetic framework or other timeline behavior change.

## Original evidence and validation

Recovered `Weapon::Load` assigns clip=clipSize and the exact decoded scalar
fields, and loads double/two-handed flags from source data. Original manager
`getAmmo0x331409`, `setAmmo0x33161d` and `Weapon::reload0x330bf9` establish the
rules above. Original scalar getters/setters and predicates are recovered in
the original `Weapon.cpp`; source offsets are evidence only and are not used
as native layouts. Manager constructor0x331285 starts unlimited false; the
separate init ammunition loop allocates/zeros both seven-word rows.

The independent reference replay executes 2,940 original controls: 84 get,
504 set and 2,352 reload cases. They include both contexts, manager/per-call
unlimited roles, type 6, rockets, retained clip and signed32 extremes. Native
Release/Debug snapshots match every output and mandatory sound observation.
Additional original controls confirm fresh AnyContext resolution, sound after
all state commits with surviving callback mutations, and per-call unlimited
bypassing even invalid unused ammo/context fields.

Native checks also cover invalid bounds, scalar setters/predicates and all 11
weapon definitions loaded through the existing asset decoder. Full x64 builds
and CTests include the existing timeline tests after helper extraction.

This is not a full Weapon/manager, firing or actor host. Projectile ownership
and `reloadRocket` object creation, shooting/readiness, lock/achievement/HUD
effects, scene attachment, sound playback composition and inventory/save
restoration remain separate genuine implementation dependencies. No manual
gameplay or mission progression is used for validation.
