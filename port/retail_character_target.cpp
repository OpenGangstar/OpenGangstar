#include "retail_character_target.h"

#include <stdexcept>

void RetailCharacterTarget::setRaw(
    const std::shared_ptr<RetailTargetEntity> &value) {
  if (value)
    borrowed = value;
  else
    borrowed.reset();
  hasIdentity = bool(value);
}

std::shared_ptr<RetailTargetEntity> RetailCharacterTarget::entity() const {
  if (!hasIdentity)
    return {};
  auto lease = borrowed.lock();
  if (!lease)
    throw std::runtime_error("Expired nonnull canonical Character target.");
  return lease;
}

std::shared_ptr<RetailActorTargetPosition>
RetailCharacterTarget::currentTarget() {
  return entity();
}

void setRetailPlayerTarget(RetailCharacterTarget &targets,
                           std::shared_ptr<RetailTargetEntity> incoming,
                           bool force, std::uint32_t &flags,
                           RetailPlayerTargetBytes &bytes,
                           RetailPlayerTargetHost &host) {
  {
    const auto old = targets.entity();
    if (old && old->levelCategory() == 5)
      host.notifyNpcTargeted(*old, false, targets.owner());
  }
  targets.setRaw(incoming);
  if (incoming) {
    bytes.byte354 = 1;
    bytes.byte355 = 0;
    if (incoming->levelCategory() == 5)
      host.notifyNpcTargeted(*incoming, true, targets.owner());
    if (force)
      host.camera().enterMode(1, targets.owner(), .3f, false);
    host.combatIdle(targets.owner());
  } else {
    bytes.byte354 = 0;
    bytes.byte353 = 0;
    if (host.camera().mode() == 1 || (force && !(flags & 0xfe)))
      host.camera().enterMode(0, targets.owner(), .3f, false);
  }
  auto &lock = host.lock();
  const auto current = targets.entity();
  lock.setTarget(current);
}
