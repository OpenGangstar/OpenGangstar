#include "retail_character_target.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Event = std::vector<std::int64_t>;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
template <class F> void expired(F action) {
  bool failed = false;
  try {
    action();
  } catch (const std::runtime_error &) {
    failed = true;
  }
  require(failed, "Expired nonnull target silently became NULL.");
}
struct Entity : RetailTargetEntity {
  int id, category;
  RetailActorHeadingPoint position{};
  Entity(int i, int c) : id(i), category(c) {}
  std::int32_t levelCategory() override { return category; }
  RetailActorHeadingPoint getPosition() override { return position; }
};
struct Character : Entity {
  RetailCharacterTarget target{*this};
  std::uint32_t flags = 0;
  RetailPlayerTargetBytes bytes;
  Character() : Entity(99, 8) {}
};
int identity(const std::shared_ptr<RetailTargetEntity> &entity) {
  return entity ? static_cast<const Entity &>(*entity).id : -1;
}
std::int64_t byte(const std::optional<std::uint8_t> &value) {
  return value ? *value : -1;
}
struct Fixture final : RetailPlayerTargetHost,
                       RetailPlayerTargetCamera,
                       RetailPlayerTargetLock {
  Character player;
  std::shared_ptr<Entity> entities[4] = {
      std::make_shared<Entity>(0, 5), std::make_shared<Entity>(1, 3),
      std::make_shared<Entity>(2, 2), std::make_shared<Entity>(3, 5)};
  std::int32_t cameraMode = 0;
  int mutation = 0;
  std::shared_ptr<RetailTargetEntity> callerIncoming;
  std::vector<Event> events;
  int current() const { return identity(player.target.entity()); }
  void replace() { player.target.setRaw(entities[2]); }
  void notifyNpcTargeted(RetailTargetEntity &value, bool targeted,
                         RetailTargetEntity &source) override {
    events.push_back({0, static_cast<Entity &>(value).id, targeted,
                      &source == &player, current(), byte(player.bytes.byte353),
                      byte(player.bytes.byte354), byte(player.bytes.byte355)});
    if (mutation == 1 && !targeted)
      replace();
    if (mutation == 2 && targeted)
      replace();
    if (mutation == 3 && targeted)
      player.target.setRaw({});
    if (mutation == 4 && !targeted) {
      entities[1]->category = 5;
      player.bytes = {0xb0, 0xb1, 0xb2};
    }
    if (mutation == 5 && targeted)
      player.bytes = {0xc0, 0xc1, 0xc2};
    if (mutation == 11 && !targeted)
      callerIncoming = entities[2];
    if (mutation == 12 && !targeted) {
      callerIncoming.reset();
      entities[1].reset();
    }
  }
  RetailPlayerTargetCamera &camera() override {
    events.push_back({1, current()});
    if (mutation == 6)
      player.flags = 2;
    return *this;
  }
  std::int32_t mode() override { return cameraMode; }
  void enterMode(std::int32_t value, RetailTargetEntity &source,
                 float transition, bool flag) override {
    std::uint32_t bits;
    std::memcpy(&bits, &transition, sizeof bits);
    events.push_back({2, value, &source == &player, bits, flag});
    if (mutation == 7)
      replace();
  }
  void combatIdle(RetailTargetEntity &source) override {
    events.push_back({3, &source == &player, current()});
    if (mutation == 8)
      replace();
  }
  RetailPlayerTargetLock &lock() override {
    events.push_back({4, current()});
    if (mutation == 9)
      replace();
    return *this;
  }
  void setTarget(const std::shared_ptr<RetailTargetEntity> &value) override {
    events.push_back({5, identity(value), current()});
    if (mutation == 10)
      player.target.setRaw(entities[1]);
  }
  void rich(std::shared_ptr<RetailTargetEntity> incoming, bool force = false) {
    setRetailPlayerTarget(player.target, std::move(incoming), force,
                          player.flags, player.bytes, *this);
  }
};
void controls() {
  Fixture f;
  require(f.player.target.knownNull() && !f.player.target.currentTarget(),
          "Character constructor common target is known NULL.");
  f.player.target.setRaw(f.entities[0]);
  auto lease = f.player.target.currentTarget();
  require(lease.get() == f.entities[0].get(),
          "Lookup leases the actual canonical entity.");
  f.entities[0]->position = {7, 8, 9};
  require(
      lease->getPosition() == RetailActorHeadingPoint{7, 8, 9},
      "A lease calls current entity position, never a scene snapshot proxy.");
  f.entities[0].reset();
  require(f.player.target.entity().get() == lease.get(),
          "Serial lease retains genuine entity.");
  lease.reset();
  require(!f.player.target.knownNull(),
          "Expired identity remains distinct from known NULL.");
  expired([&] { f.player.target.currentTarget(); });
  expired([&] { f.rich({}); });
  f.player.target.setRaw({});
  require(f.player.target.knownNull() && !f.player.target.entity() &&
              f.events.empty(),
          "Raw clear never dereferences expired old target or invokes rich "
          "effects.");

  Fixture fresh;
  fresh.player.target.setRaw(fresh.entities[0]);
  const auto firstLookup = fresh.player.target.currentTarget();
  fresh.player.target.setRaw(fresh.entities[1]);
  const auto secondLookup = fresh.player.target.currentTarget();
  fresh.player.target.setRaw({});
  require(firstLookup.get() == fresh.entities[0].get() &&
              secondLookup.get() == fresh.entities[1].get() &&
              !fresh.player.target.currentTarget(),
          "Every lookup resolves current common identity anew, retaining only "
          "each serial lease.");

  Fixture unknown;
  unknown.rich({});
  require(unknown.player.bytes.byte353 == 0 &&
              unknown.player.bytes.byte354 == 0 &&
              !unknown.player.bytes.byte355,
          "NULL source stores preserve unknown constructor byte355.");
  unknown.player.bytes = {};
  unknown.rich(unknown.entities[1]);
  require(!unknown.player.bytes.byte353 && unknown.player.bytes.byte354 == 1 &&
              unknown.player.bytes.byte355 == 0,
          "Nonnull source stores preserve unknown constructor byte353.");
  Fixture lateFlags;
  lateFlags.mutation = 6;
  lateFlags.rich({}, true);
  require(lateFlags.player.flags == 2 &&
              lateFlags.events ==
                  std::vector<Event>{{1, -1}, {4, -1}, {5, -1, -1}},
          "Camera lookup flag mutation is read before the late null-camera "
          "force test.");

  Fixture same;
  same.player.target.setRaw(same.entities[0]);
  same.rich(same.entities[0]);
  require(same.events.size() == 5 && same.events[0][0] == 0 &&
              same.events[0][2] == 0 && same.events[1][0] == 0 &&
              same.events[1][2] == 1,
          "Same-target assignment still unnotifies and renotifies NPC.");
  Fixture captured;
  captured.player.target.setRaw(captured.entities[0]);
  captured.callerIncoming = captured.entities[1];
  captured.mutation = 11;
  setRetailPlayerTarget(captured.player.target, captured.callerIncoming, false,
                        captured.player.flags, captured.player.bytes, captured);
  require(captured.current() == 1 && identity(captured.callerIncoming) == 2,
          "Incoming identity is captured before old notification changes "
          "caller slot.");
  Fixture retained;
  retained.player.target.setRaw(retained.entities[0]);
  retained.callerIncoming = retained.entities[1];
  std::weak_ptr<Entity> lifetime = retained.entities[1];
  retained.mutation = 12;
  setRetailPlayerTarget(retained.player.target, retained.callerIncoming, false,
                        retained.player.flags, retained.player.bytes, retained);
  require(lifetime.expired() && !retained.player.target.knownNull(),
          "Incoming serial lease lasts through callbacks but never becomes a "
          "strong field.");
  expired([&] { retained.player.target.entity(); });
  retained.player.target.setRaw({});
}
void dump(std::size_t index, const Fixture &f) {
  std::cout << "case {\"index\":" << index << ",\"events\":[";
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
  std::cout << "],\"target\":" << f.current() << ",\"bytes\":["
            << byte(f.player.bytes.byte353) << ','
            << byte(f.player.bytes.byte354) << ','
            << byte(f.player.bytes.byte355)
            << "],\"finalFlags\":" << f.player.flags << "}\n";
}
void originalCases() {
  std::size_t index = 0;
  auto run = [&](int oldKind, int newKind, bool force, int mode,
                 std::uint32_t flags, int mutation) {
    Fixture f;
    f.player.bytes = {0xa5, 0x5a, 0xc3};
    f.player.flags = flags;
    f.cameraMode = mode;
    f.mutation = mutation;
    const auto old =
        oldKind == 0 ? std::shared_ptr<Entity>{} : f.entities[oldKind - 1];
    const auto incoming = newKind == 0   ? std::shared_ptr<Entity>{}
                          : newKind == 3 ? old
                                         : f.entities[newKind - 1];
    f.player.target.setRaw(old);
    f.rich(incoming, force);
    dump(index++, f);
  };
  for (int old : {0, 1, 2})
    for (int incoming : {0, 1, 2, 3})
      for (bool force : {false, true})
        for (int mode : {0, 1, 2})
          for (std::uint32_t flags : {0u, 1u, 2u, 0xfeu, 0x100u})
            run(old, incoming, force, mode, flags, 0);
  for (int mutation = 1; mutation <= 10; ++mutation)
    run(mutation == 1 || mutation == 4 ? 1 : 0,
        mutation == 6   ? 0
        : mutation == 4 ? 2
                        : 1,
        mutation == 6 || mutation == 7, 0, 0, mutation);
  run(1, 4, true, 0, 0,
      0); // Distinct old/new NPC owners, not only same-pointer NPC.
  require(index == 371, "Original rich target case count differs.");
}
} // namespace
int main(int argc, char **argv) {
  try {
    controls();
    if (argc > 1 && std::string(argv[1]) == "--dump")
      originalCases();
    std::cout << "Canonical common Character target checks passed.\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
