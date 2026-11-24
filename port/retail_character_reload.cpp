#include "retail_character_reload.h"

void setRetailCharacterReloadAnimation(std::int32_t type,
                                       RetailCharacterReloadHost &host) {
  std::int32_t animation;
  switch (type) {
  case 1:
    animation =
        host.levelCategory() == 5 && host.npcCategory428() == 3 ? 56 : 51;
    break;
  case 2:
  case 6:
    animation = 60;
    break;
  case 3:
    animation = 76;
    break;
  case 4:
    animation = 75;
    break;
  case 5:
    animation = 51;
    break;
  case 7:
    animation = 67;
    break;
  case 8:
    animation = 68;
    break;
  default:
    return;
  }
  host.setAnimation({animation, 1.f, true, false, false});
}

void checkRetailCharacterWeaponReload(RetailCharacterReloadHost &host,
                                      RetailWeaponAmmo &ammo,
                                      RetailWeaponReloadSound &sound) {
  auto *weapon = host.getWeapon();
  if (!weapon)
    return;
  const auto type = weapon->weaponType();
  if (type == 0 || weapon->clip() > 0)
    return;
  if (host.levelCategory() != 8) {
    weapon->reload(true, ammo, sound);
  } else if (ammo.getAmmo(weapon->ammoType()) > 0) {
    weapon->reload(false, ammo, sound);
  } else {
    return;
  }
  setRetailCharacterReloadAnimation(type, host);
}
