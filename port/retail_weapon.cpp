#include "retail_weapon.h"
#include "retail_integer.h"

#include <stdexcept>

std::size_t RetailWeaponAmmo::checkedContext(std::int32_t context) const {
  if (context == anyContext)
    context = playerContext;
  if (context < 0 || context >= 2)
    throw std::out_of_range("Invalid original ammunition context.");
  return static_cast<std::size_t>(context);
}

std::size_t RetailWeaponAmmo::checkedType(std::int32_t type) {
  if (type < 0 || type >= 7)
    throw std::out_of_range("Invalid original ammunition type.");
  return static_cast<std::size_t>(type);
}

std::int32_t RetailWeaponAmmo::getAmmo(std::int32_t type,
                                       std::int32_t context) const {
  const auto slot = checkedContext(context);
  const auto stored = ammunition[slot][checkedType(type)];
  return stored <= 0 && unlimitedPolicy ? 1 : stored;
}

void RetailWeaponAmmo::setAmmo(std::int32_t type, std::int32_t count,
                               std::int32_t context) {
  const auto slot = checkedContext(context);
  auto &stored = ammunition[slot][checkedType(type)];
  if (unlimitedPolicy && stored >= count)
    return;
  if (type != 6)
    stored = count;
}

RetailWeapon::RetailWeapon(const CityWeaponMetatype &definition)
    : values(definition), currentClip(definition.clipSize) {}

bool RetailWeapon::isGun() const {
  switch (weaponType()) {
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 8:
    return true;
  default:
    return false;
  }
}

void RetailWeapon::reload(bool roleUnlimited, RetailWeaponAmmo &ammo,
                          RetailWeaponReloadSound &sound) {
  if (roleUnlimited) {
    currentClip = clipSize();
  } else {
    auto remaining = ammo.getAmmo(ammoType());
    if (isProjectile()) {
      if (remaining > 0 || ammo.unlimited()) {
        currentClip = 1;
        remaining = retailSubtractInt32(remaining, 1);
      }
    } else if (remaining >= clipSize() || ammo.unlimited()) {
      currentClip = clipSize();
      remaining = retailSubtractInt32(remaining, clipSize());
    } else {
      currentClip = remaining;
      remaining = 0;
    }
    ammo.setAmmo(ammoType(), remaining);
  }
  sound.playReloadSound(*this);
}
