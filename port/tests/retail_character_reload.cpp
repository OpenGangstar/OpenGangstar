#include "retail_character_reload.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Event = std::vector<std::int64_t>;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
CityWeaponMetatype definition(std::int32_t type, std::int32_t ammoType) {
  return {"fixture", ammoType, 5, 0, 0, 0, {}, false, false, 0, 0, type};
}
struct Fixture final : RetailCharacterReloadHost, RetailWeaponReloadSound {
  std::int32_t context = 1;
  RetailWeaponAmmo ammo{context};
  RetailWeapon first, second;
  RetailWeapon *equipped;
  std::int32_t category, npc;
  int mutation;
  std::vector<Event> events;
  Fixture(std::int32_t type, std::int32_t clip, std::int32_t cat,
          std::int32_t state, std::int32_t reserve, bool unlimited,
          int profile = 0, bool missing = false, std::int32_t ammoType = 0)
      : first(definition(type, ammoType)), second(definition(7, ammoType)),
        equipped(missing ? nullptr : &first), category(cat), npc(state),
        mutation(profile) {
    first.setClip(clip);
    second.setClip(77);
    ammo.counts()[1][ammoType] = reserve;
    ammo.setUnlimited(unlimited);
  }
  int identity(const RetailWeapon *value) const {
    return !value ? -1 : value == &first ? 0 : 1;
  }
  RetailWeapon *getWeapon() override {
    auto *result = equipped;
    events.push_back({0, identity(result)});
    if (mutation == 7) {
      equipped = &second;
      category = 5;
      npc = 3;
    }
    return result;
  }
  std::int32_t levelCategory() override {
    events.push_back({1, category});
    return category;
  }
  std::int32_t npcCategory428() override {
    require(category == 5,
            "NPC role read outside original live category gate.");
    events.push_back({2, npc});
    return npc;
  }
  void setAnimation(const RetailAnimationRequest &request) override {
    std::uint32_t speed;
    std::memcpy(&speed, &request.speed, sizeof speed);
    events.push_back({4, request.animation, speed, request.loop,
                      request.rootMotion, request.directionalMotion});
    if (mutation == 4) {
      equipped = &first;
      category = 9;
      npc = 8;
    }
  }
  void playReloadSound(RetailWeapon &weapon) override {
    events.push_back({3, identity(&weapon), weapon.weaponType(), weapon.clip(),
                      ammo.counts()[1][weapon.ammoType()]});
    if (mutation >= 1 && mutation <= 6) {
      equipped = &second;
      category = mutation == 2 ? 8 : 5;
      npc = mutation == 3 ? 0 : 3;
    }
  }
  void check() { checkRetailCharacterWeaponReload(*this, ammo, *this); }
};
void controls() {
  Fixture missing(1, 0, 8, 3, 0, false, 0, true);
  missing.context = 9;
  missing.check();
  require(missing.events == std::vector<Event>{{0, -1}},
          "Null weapon returns before live reads.");
  Fixture fists(0, INT32_MIN, 8, 3, 0, false);
  fists.context = 9;
  fists.check();
  require(fists.events == std::vector<Event>{{0, 0}},
          "Type0 returns before category/ammo reads.");
  Fixture loaded(1, 1, 8, 3, 0, false);
  loaded.context = 9;
  loaded.check();
  require(loaded.events == std::vector<Event>{{0, 0}},
          "Positive clip returns before category reads.");
  Fixture empty(1, -1, 8, 3, 0, false);
  empty.check();
  require(empty.first.clip() == -1 &&
              empty.events == std::vector<Event>{{0, 0}, {1, 8}},
          "Player depleted reserve skips reload and animation.");
  Fixture managerUnlimited(1, 0, 8, 3, -1, true);
  managerUnlimited.check();
  require(managerUnlimited.first.clip() == 5 &&
              managerUnlimited.ammo.counts()[1][0] == -1,
          "Player public unlimited reserve allows actual policy-aware reload.");
  Fixture replaced(1, 0, 8, 0, 3, false, 1);
  replaced.check();
  require(replaced.events == std::vector<Event>{{0, 0},
                                                {1, 8},
                                                {3, 0, 1, 3, 0},
                                                {1, 5},
                                                {2, 3},
                                                {4, 56, 0x3f800000, 1, 0, 0}} &&
              replaced.equipped == &replaced.second,
          "Captured old weapon/type and late live NPC category survive "
          "equipped replacement.");
  Fixture animationMutation(1, 0, 8, 0, 3, false, 4);
  animationMutation.check();
  require(animationMutation.category == 9 && animationMutation.npc == 8 &&
              animationMutation.equipped == &animationMutation.first,
          "Animation callback mutations survive final return.");
  Fixture dispatch(1, 0, 0, 3, 3, false);
  setRetailCharacterReloadAnimation(2, dispatch);
  require(dispatch.events == std::vector<Event>{{4, 60, 0x3f800000, 1, 0, 0}},
          "Non-type1 dispatch does not read category or NPC state.");
  dispatch.events.clear();
  setRetailCharacterReloadAnimation(INT32_MIN, dispatch);
  require(dispatch.events.empty(),
          "Unknown signed type performs no animation or extra reads.");
}
void dump(std::size_t index, const char *kind, const Fixture &f) {
  std::cout << "case {\"index\":" << index << ",\"kind\":\"" << kind
            << "\",\"events\":[";
  for (std::size_t i = 0; i < f.events.size(); ++i) {
    if (i)
      std::cout << ',';
    std::cout << '[';
    for (std::size_t j = 0; j < f.events[i].size(); ++j) {
      if (j)
        std::cout << ',';
      std::cout << f.events[i][j];
    }
    std::cout << ']';
  }
  std::cout << "],\"clip\":" << f.first.clip()
            << ",\"ammo\":" << f.ammo.counts()[1][f.first.ammoType()]
            << ",\"category\":" << f.category << ",\"npc\":" << f.npc
            << ",\"equipped\":" << f.identity(f.equipped)
            << ",\"actualType\":" << f.first.weaponType() << "}\n";
}
void originalCases() {
  std::size_t index = 0;
  for (std::int32_t cat : {0, 5, 8})
    for (std::int32_t npc : {0, 3})
      for (std::int32_t clip : {INT32_MIN, -1, 0, 1, INT32_MAX})
        for (std::int32_t type : {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, INT32_MAX})
          for (std::int32_t reserve : {-1, 0, 3})
            for (bool unlimited : {false, true})
              for (std::int32_t ammoType : {0, 6}) {
                Fixture f(type, clip, cat, npc, reserve, unlimited, 0, false,
                          ammoType);
                f.check();
                dump(index++, "check", f);
              }
  for (std::int32_t cat : {0, 5, 8}) {
    Fixture f(1, 0, cat, 3, 3, false, 0, true);
    f.check();
    dump(index++, "check", f);
  }
  for (int mutation = 1; mutation <= 7; ++mutation) {
    Fixture f(mutation == 5   ? 7
              : mutation == 6 ? 9
                              : 1,
              0,
              mutation == 2                    ? 5
              : mutation == 3 || mutation == 7 ? 0
                                               : 8,
              0, 3, false, mutation);
    f.check();
    dump(index++, "check", f);
  }
  for (std::int32_t cat : {0, 5, 8})
    for (std::int32_t npc : {0, 3})
      for (std::int32_t type :
           {INT32_MIN, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, INT32_MAX}) {
        Fixture f(type, 0, cat, npc, 3, false);
        setRetailCharacterReloadAnimation(type, f);
        dump(index++, "direct", f);
      }
  require(index == 4408, "Original Character dispatch case count differs.");
}
} // namespace
int main(int argc, char **argv) {
  try {
    controls();
    if (argc > 1 && std::string(argv[1]) == "--dump")
      originalCases();
    std::cout << "Original Character weapon reload checks passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
