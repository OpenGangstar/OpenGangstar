#include "retail_game_object_list.h"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void categoryIndex(std::int32_t category) {
    if (category < 0 || category >= 17) throw std::runtime_error("Invalid original manager category index.");
}
std::string queryValue(std::string_view query) {
    if (query.find('\0') != std::string_view::npos) throw std::runtime_error("Embedded NUL in object name query.");
    return std::string(query); // Survives destruction of a matching entity's name.
}
}

RetailGameObjectListEntity* RetailGameObjectListEntity::previous() const {
    if (hook.list) hook.list->requireUsable();
    return hook.previous;
}
RetailGameObjectListEntity* RetailGameObjectListEntity::next() const {
    if (hook.list) hook.list->requireUsable();
    return hook.next;
}
void RetailGameObjectListEntity::preselectList(RetailGameObjectList* list) {
    if (hook.linked || hook.previous || hook.next) throw std::runtime_error("Cannot preselect a linked object list.");
    if (list) list->requireUsable();
    hook.list = list;
}
RetailGameObjectList::RetailGameObjectList(RetailGameObjectLifetime& owner) : lifetime(owner) {}
RetailGameObjectList::~RetailGameObjectList() { clear(); }
void RetailGameObjectList::requireUsable() const {
    if (clearing) throw std::runtime_error("Object list query/mutation during still-linked clear.");
}
void RetailGameObjectList::requireFresh(const Entity& object) const {
    if (object.hook.linked || object.hook.previous || object.hook.next)
        throw std::runtime_error("Original list insertion requires an unlinked clean hook.");
}
std::int32_t RetailGameObjectList::size() const {
    requireUsable(); std::int32_t count = 0;
    for (auto* object = head; object; object = object->hook.next) {
        if (count == std::numeric_limits<std::int32_t>::max()) throw std::runtime_error("Object list count overflow.");
        ++count;
    }
    return count;
}
RetailGameObjectList::Entity* RetailGameObjectList::first() const { requireUsable(); return head; }
void RetailGameObjectList::insertAfter(Entity& object, Entity& previous) {
    requireUsable(); requireFresh(object);
    if (!previous.hook.linked || previous.hook.list != this)
        throw std::runtime_error("Previous object is not in this list.");
    if (previous.hook.next) {
        previous.hook.next->hook.previous = &object;
        object.hook.next = previous.hook.next;
    }
    previous.hook.next = &object; object.hook.previous = &previous;
    if (tail == &previous) tail = &object;
    object.hook.list = this; object.hook.linked = true;
}
void RetailGameObjectList::add(Entity& object, bool) {
    requireUsable(); requireFresh(object);
    if (head) insertAfter(object, *tail);
    else { head = tail = &object; object.hook.list = this; object.hook.linked = true; }
}
RetailGameObjectList::Entity* RetailGameObjectList::remove(Entity& object) {
    requireUsable();
    if (!object.hook.linked || object.hook.list != this) throw std::runtime_error("Object is not a member of this list.");
    auto* next = object.hook.next;
    if (object.hook.previous) object.hook.previous->hook.next = object.hook.next;
    if (head == &object) head = object.hook.next;
    if (object.hook.next) object.hook.next->hook.previous = object.hook.previous;
    if (tail == &object) tail = object.hook.previous;
    object.hook = {};
    return next;
}
RetailGameObjectList::Entity* RetailGameObjectList::removeAndDestroy(Entity& object) {
    auto* next = remove(object);
    lifetime.destroy(object);
    return next;
}
void RetailGameObjectList::clear() {
    requireUsable(); clearing = true;
    auto* object = head;
    while (object) {
        auto* next = object->hook.next;
        lifetime.destroy(*object); // Still linked, unlike removeAndDestroy.
        object = next;
    }
    head = tail = nullptr; clearing = false;
}
void RetailGameObjectList::updateGameObjects(std::int32_t elapsed, bool skip) {
    requireUsable(); auto* object = head;
    while (object) {
        auto* next = object->hook.next;
        if (!skip) object->update(elapsed);
        object = next;
    }
}
void RetailGameObjectList::forEach(const Function& function, bool) const {
    requireUsable(); if (!function) throw std::runtime_error("Missing object iteration function.");
    for (auto* object = head; object; object = object->hook.next) function(*object, 0);
}
RetailGameObjectList::Entity* RetailGameObjectList::next(const Condition& condition, const Entity& object,
                                                       bool, const std::int32_t* context) const {
    requireUsable(); if (!condition) throw std::runtime_error("Missing object iteration condition.");
    // object may have been removed by a callback: its cleared next is genuine.
    for (auto* next = object.hook.next; next; next = next->hook.next)
        if (condition(*next, context ? *context : 0)) return next;
    return nullptr;
}
RetailGameObjectList::Entity* RetailGameObjectList::first(const Condition& condition, bool flag,
                                                        const std::int32_t* context) const {
    requireUsable(); if (!condition) throw std::runtime_error("Missing object iteration condition.");
    if (!head) return nullptr;
    if (condition(*head, context ? *context : 0)) return head;
    if (!head) throw std::runtime_error("Condition removed the final head before original GetNext dereference.");
    return next(condition, *head, flag, context);
}
void RetailGameObjectList::forEachIf(const Function& function, const Condition& condition, bool flag) const {
    requireUsable(); if (!function) throw std::runtime_error("Missing object iteration function.");
    std::int32_t context = 0;
    for (auto* object = first(condition, flag); object; object = next(condition, *object, flag, &context))
        function(*object, context);
}
bool RetailGameObjectList::fillList(const Condition& condition, std::int32_t capacity, Entity** output,
                                   std::int32_t& count, bool flag, std::int32_t initialContext) const {
    requireUsable(); if (capacity < 0) throw std::runtime_error("Negative native object output capacity.");
    count = 0;
    auto context = initialContext;
    for (auto* object = first(condition, flag, &context); object; object = next(condition, *object, flag, &context)) {
        if (count == capacity) return false;
        if (output) output[count] = object;
        ++count;
    }
    return true;
}
RetailGameObjectList::Entity* RetailGameObjectList::getGameObjectByName(std::string_view query, bool flag,
                                                                     bool respawnDead, RetailGameObjectManagerLists& manager) const {
    const auto name = queryValue(query);
    const Condition condition = [&](Entity& object, std::int32_t) {
        const auto* current = object.name(); // One actual ARM field read.
        return current && std::strcmp(name.c_str(), current) == 0;
    };
    auto* object = first(condition, flag);
    if (respawnDead && object && object->levelCategory() == 3 && object->isDead()) {
        const auto type = object->metatype();
        manager.removeAndDestroy(object);
        object = manager.createFromMetatype(type);
        if (!object) throw std::runtime_error("Original respawn factory returned no canonical object.");
        object->setName(name);
        manager.add(*object, 0);
    }
    return object;
}

RetailGameObjectManagerLists::RetailGameObjectManagerLists(RetailGameObjectLifetime& owner)
    : lifetime(owner), list0(owner), list2(owner), list1(owner) {}
RetailGameObjectManagerLists::~RetailGameObjectManagerLists() {
    destroying = true;
    list1.clear(); list2.clear(); list0.clear(); // Original embedded member destruction order.
}
void RetailGameObjectManagerLists::requireUsable() const {
    if (destroying) throw std::runtime_error("Manager list operation during destruction.");
}
void RetailGameObjectManagerLists::add(Entity& object, std::int32_t selector) {
    requireUsable(); (selector == 0 ? list0 : selector == 1 ? list1 : list2).add(object, false);
}
RetailGameObjectManagerLists::Entity* RetailGameObjectManagerLists::first(std::int32_t selector) const {
    requireUsable(); return (selector == 0 ? list0 : selector == 1 ? list1 : list2).first();
}
RetailGameObjectList& RetailGameObjectManagerLists::getObjectList(std::int32_t selector) {
    return selector == 0 ? list0 : selector == 2 ? list2 : list1;
}
RetailGameObjectManagerLists::Entity* RetailGameObjectManagerLists::remove(Entity& object) {
    requireUsable();
    if (!object.objectList()) throw std::runtime_error("Object has no current list.");
    return object.objectList()->remove(object);
}
void RetailGameObjectManagerLists::addGameObjectOfType(std::int32_t category) {
    requireUsable(); categoryIndex(category); auto& count = counts[category];
    if (count == std::numeric_limits<std::int32_t>::max()) throw std::runtime_error("Manager category count overflow.");
    ++count;
}
void RetailGameObjectManagerLists::removeGameObjectOfType(std::int32_t category) {
    requireUsable(); categoryIndex(category); auto& count = counts[category];
    if (count == std::numeric_limits<std::int32_t>::min()) throw std::runtime_error("Manager category count underflow.");
    --count;
}
std::int32_t RetailGameObjectManagerLists::typeCount(std::int32_t category) const {
    requireUsable(); categoryIndex(category); return counts[category];
}
RetailGameObjectManagerLists::Entity* RetailGameObjectManagerLists::removeAndDestroy(Entity* object) {
    requireUsable(); if (!object) return nullptr;
    removeGameObjectOfType(object->levelCategory());
    if (!object->objectList()) throw std::runtime_error("Object has no current list after category decrement.");
    return object->objectList()->removeAndDestroy(*object);
}
void RetailGameObjectManagerLists::clearLists() {
    requireUsable(); list0.clear(); list2.clear(); list1.clear();
}
RetailGameObjectManagerLists::Entity* RetailGameObjectManagerLists::createFromMetatype(std::int32_t type) {
    requireUsable(); return lifetime.createFromMetatype(type);
}
RetailGameObjectManagerLists::Entity* RetailGameObjectManagerLists::getGameObjectByName(std::string_view query,
                                                                                     bool respawnDead) {
    requireUsable(); const auto stable = queryValue(query);
    if (auto* object = list0.getGameObjectByName(stable, true, respawnDead, *this)) return object;
    if (auto* object = list2.getGameObjectByName(stable, true, respawnDead, *this)) return object;
    return list1.getGameObjectByName(stable, true, respawnDead, *this);
}
