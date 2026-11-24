#pragma once

#include "city_assets.h"

#include <array>
#include <cstdint>

class RetailWeapon;
class RetailWeaponReloadSound {
public:
  virtual ~RetailWeaponReloadSound() = default;
  virtual void playReloadSound(RetailWeapon &) = 0;
};

// Initialized ammunition portion of the original manager, borrowing the live
// canonical Player context word. This is not a complete WeaponManager.
class RetailWeaponAmmo {
public:
  using Counts = std::array<std::array<std::int32_t, 7>, 2>;
  static constexpr std::int32_t anyContext = -1;
  explicit RetailWeaponAmmo(std::int32_t &currentPlayerContext)
      : playerContext(currentPlayerContext) {}
  RetailWeaponAmmo(const RetailWeaponAmmo &) = delete;
  RetailWeaponAmmo &operator=(const RetailWeaponAmmo &) = delete;

  std::int32_t getAmmo(std::int32_t type,
                       std::int32_t context = anyContext) const;
  void setAmmo(std::int32_t type, std::int32_t count,
               std::int32_t context = anyContext);
  bool unlimited() const { return unlimitedPolicy; }
  void setUnlimited(bool value) { unlimitedPolicy = value; }
  // The sole actual store for later original producer/restore composition.
  // Exposing it does not implement those producers or save restoration.
  Counts &counts() { return ammunition; }
  const Counts &counts() const { return ammunition; }

private:
  std::size_t checkedContext(std::int32_t) const;
  static std::size_t checkedType(std::int32_t);
  std::int32_t &playerContext;
  Counts ammunition{};
  bool unlimitedPolicy = false;
};

// Sole loaded scalar/clip state, not a full game Weapon or projectile owner.
class RetailWeapon {
public:
  explicit RetailWeapon(const CityWeaponMetatype &);
  RetailWeapon(const RetailWeapon &) = delete;
  RetailWeapon &operator=(const RetailWeapon &) = delete;

  std::int32_t weaponType() const { return values.weaponType; }
  std::int32_t ammoType() const { return values.ammoType; }
  std::int32_t clipSize() const { return values.clipSize; }
  std::int32_t clip() const { return currentClip; }
  std::int32_t range() const { return values.range; }
  std::int32_t cone() const { return values.cone; }
  std::int32_t damage() const { return values.damage; }
  std::int32_t shootFrequency() const { return values.shootFrequency; }
  std::int32_t dryShootFrequency() const { return values.dryShootFrequency; }
  bool isDouble() const { return values.doubleWeapon; }
  bool isTwoHanded() const { return values.twoHanded; }
  bool isGun() const;
  bool isProjectile() const { return weaponType() == 7; }
  void setClip(std::int32_t value) { currentClip = value; }
  void setClipSize(std::int32_t value) { values.clipSize = value; }
  void reload(bool roleUnlimited, RetailWeaponAmmo &,
              RetailWeaponReloadSound &);

private:
  CityWeaponMetatype values;
  std::int32_t currentClip;
};
