#pragma once

#include "retail_animation.h"
#include "retail_weapon.h"

// Required live canonical Character operations. No default category, equipped
// weapon snapshot or animation implementation is supplied. A returned weapon
// must outlive the serial call even if reload callbacks replace the equipped
// pointer. setAnimation must use the actual retained animation binding/clock.
class RetailCharacterReloadHost {
public:
  virtual ~RetailCharacterReloadHost() = default;
  virtual RetailWeapon *getWeapon() = 0;
  virtual std::int32_t levelCategory() = 0;
  // Original NPC word428, read only for type1/live level category5.
  virtual std::int32_t npcCategory428() = 0;
  virtual void setAnimation(const RetailAnimationRequest &) = 0;
};

void setRetailCharacterReloadAnimation(std::int32_t weaponType,
                                       RetailCharacterReloadHost &);
void checkRetailCharacterWeaponReload(RetailCharacterReloadHost &,
                                      RetailWeaponAmmo &,
                                      RetailWeaponReloadSound &);
