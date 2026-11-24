#include "retail_weapon.h"
#include "filesystem.h"
#include "retail_integer.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
  bool failed = false;
  try {
    action();
  } catch (const std::out_of_range &) {
    failed = true;
  }
  require(failed, "Invalid native ammunition bounds were accepted.");
}
CityWeaponMetatype definition(std::int32_t type = 1, std::int32_t ammo = 0,
                              std::int32_t size = 30) {
  return {"loaded", ammo, size, 68, 72, 80, "flash", true, false, 64, 76, type};
}
struct Sound final : RetailWeaponReloadSound {
  RetailWeaponAmmo &ammo;
  std::int32_t &context;
  int calls = 0;
  std::int32_t type = 0, clip = 0, reserve = 0;
  bool mutate = false;
  Sound(RetailWeaponAmmo &a, std::int32_t &c) : ammo(a), context(c) {}
  void playReloadSound(RetailWeapon &weapon) override {
    ++calls;
    type = weapon.weaponType();
    clip = weapon.clip();
    if (context >= 0 && context < 2 && weapon.ammoType() >= 0 &&
        weapon.ammoType() < 7) {
      auto &stored = ammo.counts()[context][weapon.ammoType()];
      reserve = stored;
      if (mutate)
        stored = 456;
    }
    if (mutate)
      weapon.setClip(123);
  }
};
void controls() {
  std::int32_t context = 0;
  RetailWeaponAmmo ammo(context);
  for (const auto &row : ammo.counts())
    for (auto count : row)
      require(count == 0, "Initialized original ammo starts at zero.");
  require(!ammo.unlimited(), "Original manager unlimited policy starts false.");
  ammo.counts()[0][0] = 9;
  ammo.counts()[1][0] = 21;
  require(ammo.getAmmo(0) == 9, "AnyContext reads actual current context.");
  context = 1;
  ammo.setAmmo(0, 7);
  require(ammo.counts()[0][0] == 9 && ammo.getAmmo(0) == 7,
          "Set resolves canonical context anew, without a context cache.");
  context = -8;
  require(ammo.getAmmo(0, 0) == 9,
          "Explicit context bypasses current-context lookup.");
  rejects([&] { ammo.getAmmo(0); });
  rejects([&] { ammo.setAmmo(0, 1); });
  rejects([&] { ammo.getAmmo(-1, 0); });
  rejects([&] { ammo.setAmmo(7, 1, 0); });
  rejects([&] { ammo.getAmmo(0, -2); });
  rejects([&] { ammo.setAmmo(6, 1, 2); });
  context = 0;
  ammo.counts()[0][6] = -3;
  ammo.setAmmo(6, 100);
  require(ammo.counts()[0][6] == -3, "Type6 setter never writes raw store.");
  ammo.setUnlimited(true);
  require(ammo.getAmmo(6) == 1, "Unlimited nonpositive public reserve is one.");
  ammo.setAmmo(0, 1);
  require(ammo.counts()[0][0] == 9,
          "Unlimited setter preserves a higher stored count.");
  ammo.setUnlimited(false);
  ammo.counts()[0][0] = 2;
  RetailWeapon weapon(definition(1, 0, 5));
  require(weapon.clip() == 5 && weapon.range() == 64 && weapon.cone() == 68 &&
              weapon.damage() == 72 && weapon.shootFrequency() == 76 &&
              weapon.dryShootFrequency() == 80 && weapon.isDouble() &&
              !weapon.isTwoHanded(),
          "Loaded scalar getters preserve authoritative values.");
  weapon.setClip(99);
  Sound sound(ammo, context);
  sound.mutate = true;
  weapon.reload(false, ammo, sound);
  require(sound.calls == 1 && sound.clip == 2 && sound.reserve == 0 &&
              weapon.clip() == 123 && ammo.counts()[0][0] == 456,
          "Required sound observes committed clip/ammo; callback mutations "
          "survive.");
  RetailWeapon rocket(definition(7));
  rocket.setClip(99);
  ammo.counts()[0][0] = 0;
  sound.mutate = false;
  sound.calls = 0;
  rocket.reload(false, ammo, sound);
  require(rocket.clip() == 99 && sound.calls == 1,
          "Empty rocket retains previous clip and still plays reload sound.");
  context = 9;
  RetailWeapon direct(definition(1, 99, INT32_MIN));
  direct.reload(true, ammo, sound);
  require(direct.clip() == INT32_MIN && sound.calls == 2,
          "Per-call unlimited bypasses all ammo/context table access.");
  for (std::int32_t type :
       {INT32_MIN, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, INT32_MAX}) {
    auto meta = definition(type);
    for (bool two : {false, true}) {
      meta.twoHanded = two;
      meta.doubleWeapon = !two;
      RetailWeapon value(meta);
      require(
          value.isGun() == ((type >= 1 && type <= 6) || type == 8) &&
              value.isProjectile() == (type == 7) &&
              value.isTwoHanded() == two && value.isDouble() == !two,
          "Original weapon predicates retain their exact type/flag domain.");
      value.setClip(-123);
      value.setClipSize(INT32_MIN);
      require(
          value.clip() == -123 && value.clipSize() == INT32_MIN,
          "Independent clip/size setters do not clamp or implicitly reload.");
    }
  }
  require(
      retailSubtractInt32(INT32_MIN, 1) == INT32_MAX &&
          retailSubtractInt32(INT32_MAX, INT32_MIN) == -1,
      "Shared signed32 subtraction preserves original wrapped bit patterns.");
}
void originalCases() {
  std::int32_t currentContext = 0;
  RetailWeaponAmmo ammo(currentContext);
  std::size_t index = 0;
  for (std::int32_t context : {0, 1})
    for (bool unlimited : {false, true})
      for (std::int32_t type : {0, 5, 6})
        for (std::int32_t stored : {INT32_MIN, -1, 0, 1, 3, 30, INT32_MAX}) {
          currentContext = context;
          ammo.setUnlimited(unlimited);
          for (auto &row : ammo.counts())
            row[type] = stored;
          std::cout << "case {\"index\":" << index++
                    << ",\"kind\":\"get\",\"result\":" << ammo.getAmmo(type)
                    << "}\n";
          for (std::int32_t value : {INT32_MIN, -1, 0, 1, 30, INT32_MAX}) {
            ammo.counts()[context][type] = stored;
            ammo.setAmmo(type, value);
            std::cout << "case {\"index\":" << index++
                      << ",\"kind\":\"set\",\"result\":"
                      << ammo.counts()[context][type] << "}\n";
          }
        }
  for (bool roleUnlimited : {false, true})
    for (bool unlimited : {false, true})
      for (std::int32_t type : {0, 1, 3, 6, 7, 8, 9})
        for (std::int32_t ammoType : {0, 6})
          for (std::int32_t size : {INT32_MIN, -1, 0, 1, 30, INT32_MAX})
            for (std::int32_t stored :
                 {INT32_MIN, -1, 0, 1, 3, 30, INT32_MAX}) {
              currentContext = 1;
              ammo.setUnlimited(unlimited);
              ammo.counts()[1][ammoType] = stored;
              RetailWeapon value(definition(type, ammoType, size));
              value.setClip(99);
              Sound sound(ammo, currentContext);
              value.reload(roleUnlimited, ammo, sound);
              std::cout << "case {\"index\":" << index++
                        << ",\"kind\":\"reload\",\"clip\":" << value.clip()
                        << ",\"ammo\":" << ammo.counts()[1][ammoType]
                        << ",\"soundCalls\":" << sound.calls
                        << ",\"soundType\":" << sound.type
                        << ",\"clipAtSound\":" << sound.clip
                        << ",\"ammoAtSound\":" << sound.reserve << "}\n";
            }
  require(index == 2940, "Original comparison case count differs.");
}
void retail(const char *directory) {
  ResourceFileSystem files(directory);
  const auto assets = loadCityAssets(files);
  require(!assets.weaponMetatypes.empty(),
          "No original weapon definitions loaded.");
  for (const auto &[id, meta] : assets.weaponMetatypes) {
    RetailWeapon value(meta);
    require(value.weaponType() == meta.weaponType &&
                value.ammoType() == meta.ammoType &&
                value.clipSize() == meta.clipSize &&
                value.clip() == meta.clipSize && value.range() == meta.range &&
                value.cone() == meta.cone && value.damage() == meta.damage &&
                value.shootFrequency() == meta.shootFrequency &&
                value.dryShootFrequency() == meta.dryShootFrequency &&
                value.isDouble() == meta.doubleWeapon &&
                value.isTwoHanded() == meta.twoHanded,
            "Loaded native weapon scalar differs from original decoded "
            "definition.");
    std::cout << "weapon " << id << ' ' << value.weaponType() << ' '
              << value.ammoType() << ' ' << value.clipSize() << '\n';
  }
  std::cout << assets.weaponMetatypes.size()
            << " original decoded weapon definitions passed.\n";
}
} // namespace
int main(int argc, char **argv) {
  try {
    controls();
    if (argc > 1) {
      if (std::string(argv[1]) == "--dump")
        originalCases();
      else
        retail(argv[1]);
    }
    std::cout << "Original weapon ammo/reload checks passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
