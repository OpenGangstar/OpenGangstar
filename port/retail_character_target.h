#pragma once

#include "retail_actor_target.h"

#include <cstdint>
#include <memory>
#include <optional>

// The actual canonical game entity owned by the eventual object manager, not
// an adapter retaining an old scene/position snapshot after pooling.
class RetailTargetEntity : public RetailActorTargetPosition {
public:
  virtual std::int32_t levelCategory() = 0;
};

// One common borrowed Character target. The canonical Character owns this
// component; its bound owner outlives it. No Player-specific second target.
class RetailCharacterTarget final : public RetailActorTargetLookup {
public:
  explicit RetailCharacterTarget(RetailTargetEntity &owner) : source(owner) {}
  RetailCharacterTarget(const RetailCharacterTarget &) = delete;
  RetailCharacterTarget &operator=(const RetailCharacterTarget &) = delete;
  void setRaw(const std::shared_ptr<RetailTargetEntity> &);
  std::shared_ptr<RetailTargetEntity> entity() const;
  std::shared_ptr<RetailActorTargetPosition> currentTarget() override;
  RetailTargetEntity &owner() const { return source; }
  bool knownNull() const { return !hasIdentity; }

private:
  RetailTargetEntity &source;
  std::weak_ptr<RetailTargetEntity> borrowed;
  bool hasIdentity = false; // Character constructor establishes known NULL.
};

struct RetailPlayerTargetBytes {
  // Unknown constructor values stay unknown until an evidenced source write.
  std::optional<std::uint8_t> byte353, byte354, byte355;
};
class RetailPlayerTargetCamera {
public:
  virtual ~RetailPlayerTargetCamera() = default;
  virtual std::int32_t mode() = 0;
  virtual void enterMode(std::int32_t, RetailTargetEntity &source,
                         float transition, bool opaqueFlag) = 0;
};
class RetailPlayerTargetLock {
public:
  virtual ~RetailPlayerTargetLock() = default;
  virtual void setTarget(const std::shared_ptr<RetailTargetEntity> &) = 0;
};
class RetailPlayerTargetHost {
public:
  virtual ~RetailPlayerTargetHost() = default;
  virtual void notifyNpcTargeted(RetailTargetEntity &, bool targeted,
                                 RetailTargetEntity &source) = 0;
  // Each call resolves the genuine current singleton/owner, with its effects.
  virtual RetailPlayerTargetCamera &camera() = 0;
  virtual void combatIdle(RetailTargetEntity &source) = 0;
  virtual RetailPlayerTargetLock &lock() = 0;
};

// The value parameter captures incoming identity before any callback. Leases
// protect actual entities only during this serial operation, never as fields.
void setRetailPlayerTarget(RetailCharacterTarget &,
                           std::shared_ptr<RetailTargetEntity> incoming,
                           bool force, std::uint32_t &canonicalFlags240,
                           RetailPlayerTargetBytes &, RetailPlayerTargetHost &);
