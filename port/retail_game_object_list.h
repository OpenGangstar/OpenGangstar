#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>

class RetailGameObjectList;
class RetailGameObjectManagerLists;

// Orthogonal interface on the SAME canonical entity as RetailTargetEntity.
// Owns its sole intrusive hook; never a separate registry/snapshot/control block.
class RetailGameObjectListEntity {
public:
    RetailGameObjectListEntity() = default;
    virtual ~RetailGameObjectListEntity() = default;
    RetailGameObjectListEntity(const RetailGameObjectListEntity&) = delete;
    RetailGameObjectListEntity& operator=(const RetailGameObjectListEntity&) = delete;
    virtual void update(std::int32_t elapsed) = 0;
    virtual const char* name() const = 0; // Actual borrowed C-string, NULL allowed.
    virtual std::int32_t levelCategory() = 0; // Loaded s_type, not raw factory type.
    virtual bool isDead() = 0;
    virtual std::int32_t metatype() = 0;
    virtual void setName(std::string_view) = 0;

    RetailGameObjectList* objectList() const noexcept { return hook.list; }
    bool linked() const noexcept { return hook.linked; }
    RetailGameObjectListEntity* previous() const;
    RetailGameObjectListEntity* next() const;
    // Original Load selects objectList before insertion. Native membership is
    // distinct from that preselection, without maintaining another entity list.
    void preselectList(RetailGameObjectList*);
private:
    friend class RetailGameObjectList;
    struct Hook {
        RetailGameObjectListEntity *previous = nullptr, *next = nullptr;
        RetailGameObjectList* list = nullptr;
        bool linked = false; // Checked native membership, not a game alive flag.
    } hook;
};

// Required canonical owner operations, no default implementation. destroy must
// complete actual teardown at this call, not merely drop a shared_ptr and defer
// effects. Owner must preflight active query leases BEFORE list mutation. It
// must keep captured-next/current objects alive where source iteration rereads
// them. Direct external destruction that violates those domains is unsupported.
class RetailGameObjectLifetime {
public:
    virtual ~RetailGameObjectLifetime() = default;
    virtual void destroy(RetailGameObjectListEntity&) noexcept = 0;
    virtual RetailGameObjectListEntity* createFromMetatype(std::int32_t) = 0;
};

class RetailGameObjectList {
public:
    using Entity = RetailGameObjectListEntity;
    using Condition = std::function<bool(Entity&, std::int32_t)>;
    using Function = std::function<void(Entity&, std::int32_t)>;
    explicit RetailGameObjectList(RetailGameObjectLifetime&);
    // Lifetime owner outlives this list; destruction requires all entities are
    // genuinely destroyable. Callbacks must not query/mutate/walk this clearing
    // list. Current entity objectList()/linked() remain observable for teardown.
    ~RetailGameObjectList();
    RetailGameObjectList(const RetailGameObjectList&) = delete;
    RetailGameObjectList& operator=(const RetailGameObjectList&) = delete;
    std::int32_t size() const;
    Entity* first() const;
    void add(Entity&, bool flag);
    void insertAfter(Entity&, Entity& previous);
    Entity* remove(Entity&);
    Entity* removeAndDestroy(Entity&);
    void clear();
    void updateGameObjects(std::int32_t elapsed, bool skip);
    void forEach(const Function&, bool flag) const;
    Entity* next(const Condition&, const Entity&, bool flag, const std::int32_t* context = nullptr) const;
    Entity* first(const Condition&, bool flag, const std::int32_t* context = nullptr) const;
    void forEachIf(const Function&, const Condition&, bool flag) const;
    // Explicit native replacement for the ORIGINAL UNINITIALIZED stack context.
    // output may be NULL for counting; otherwise caller supplies capacity slots.
    bool fillList(const Condition&, std::int32_t capacity, Entity** output,
                  std::int32_t& count, bool flag, std::int32_t initialContext) const;
    Entity* getGameObjectByName(std::string_view, bool flag, bool respawnDead,
                               RetailGameObjectManagerLists&) const;
private:
    friend class RetailGameObjectListEntity;
    void requireUsable() const;
    void requireFresh(const Entity&) const;
    RetailGameObjectLifetime& lifetime;
    Entity *head = nullptr, *tail = nullptr;
    bool clearing = false;
};

// Actual intrusive-list/count component of the manager, not a substitute GOM
// factory, loader, pool or global singleton. Canonical owner supplies lifecycle.
class RetailGameObjectManagerLists {
public:
    using Entity = RetailGameObjectListEntity;
    explicit RetailGameObjectManagerLists(RetailGameObjectLifetime&);
    ~RetailGameObjectManagerLists();
    RetailGameObjectManagerLists(const RetailGameObjectManagerLists&) = delete;
    RetailGameObjectManagerLists& operator=(const RetailGameObjectManagerLists&) = delete;
    void add(Entity&, std::int32_t selector);
    Entity* first(std::int32_t selector) const;
    RetailGameObjectList& getObjectList(std::int32_t selector);
    Entity* remove(Entity&);
    Entity* removeAndDestroy(Entity*); // Original NULL branch returns NULL.
    void addGameObjectOfType(std::int32_t category);
    void removeGameObjectOfType(std::int32_t category);
    std::int32_t typeCount(std::int32_t category) const;
    void clearLists(); // Original explicit order0,2,1; no delete-this/global reset.
    Entity* createFromMetatype(std::int32_t);
    Entity* getGameObjectByName(std::string_view, bool respawnDead);
private:
    void requireUsable() const;
    RetailGameObjectLifetime& lifetime;
    std::array<std::int32_t, 17> counts{};
    RetailGameObjectList list0, list2, list1;
    bool destroying = false;
};
