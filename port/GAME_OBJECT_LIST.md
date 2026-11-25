# Canonical intrusive game-object lists

This component ports the original GameObjectList algorithms and the manager's
three-list selection/count component. It does not implement the full manager,
metatype factory, pools, object loader or global singleton. Every required entity
and factory operation is abstract; no default update/name/category/death/create
or empty teardown adapter is supplied.

`RetailGameObjectListEntity` is an orthogonal interface on the same whole
canonical object that implements `RetailTargetEntity`. Its one private hook
stores previous/next/list pointers plus a native membership guard. It is not a
second entity, target snapshot, name registry, order vector or control block.
Non-const `levelCategory()` can override both interfaces with one genuine loaded
category implementation. It must not substitute a raw factory/metatype tag.

The original LevelObject Load preselects objectList before actual insertion.
`preselectList` preserves that state separately from linked membership; append
overwrites the preselected destination. Already linked/stale hooks, wrong-list
removal and invalid insertion anchors reject instead of corrupting two lists.
There is no automatic detach/reparent workaround. Plain remove clears all hook
links and returns the previously captured next. Removing an entity does not
destroy/refilter its body or alter category counts.

## Iteration and context

Original UpdateGameObjects0x314c29 captures next BEFORE update. Self-removal
therefore does not skip the captured successor; a tail appended during the last
update is not visited in that pass. Its skip flag suppresses calls, not walking.
ForEach0x314c55 rereads current.next AFTER its callback, so self-removal stops
the pass and a newly appended tail can be visited. GetNext0x314c7d likewise
rereads a rejected candidate's next after its condition. These paths do not use
a copied enumeration order or an animation/physics update substitute.

GetFirst(condition)0x314cb5 rereads the actual head after its first condition.
A successful condition returns that current head, which can differ from its
argument after mutation. A rejected first condition starts GetNext from the
current head, potentially skipping a newly substituted head. Native rejects
the source-invalid final-head NULL dereference rather than inventing a match.
All referenced objects that source iteration will reread/capture must remain
alive for that operation. Removing without destruction is supported; destroying
a live-reread current/captured-next through another owner is outside the valid
callback domain. The canonical owner must enforce that lifetime precondition.

Conditions receive the current integer value on each call. Null context means
zero. ForEachIf0x314cf9 calls its first GetFirst with NULL context, then uses a
local initialized zero for callbacks and subsequent GetNext. Flags ignored by
the original methods stay ignored, not interpreted as new filters.

Original FillList0x314d4d passes a genuinely UNINITIALIZED stack slot to both
search calls. Native `fillList` therefore requires an explicit initial context,
with no default. This is a deliberate bounded replacement for indeterminate
state, never a claim that original context was zero. Reference execution seeds
the exact original slot with0x13579bdf and observes it on every condition.
Capacity is tested after finding each match, before writing/incrementing; an
additional match beyond capacity returns false with the filled count. Output
may be NULL for counting; otherwise the caller owns capacity output slots.
Negative native capacity rejects. No output array is allocated internally.

## Destruction and canonical lifetime

RemoveAndDestroy0x314b65 unlinks first and then calls required canonical teardown.
Manager RemoveAndDestroy0x315f05 first reads the fresh loaded category and
decrements its count, then reads the current list and performs that removal.
The NULL manager branch returns NULL without changing counts. A later failure
does not imply rollback of a preceding valid decrement.

Clear0x314bb9 captures next, destroys the STILL-LINKED current entity, advances
to captured next, and only zeroes endpoints after the pass. The original base
LevelObject destructor does not unlink. Native preserves this teardown position
but rejects reentrant query/mutation/walking of that clearing list: its source
head/previous pointers can already refer to destroyed objects. The current
entity's `objectList()` and `linked()` identity remain observable to real body
and metadata cleanup. No destroyed node is dereferenced. The supplied lifetime
owner must outlive lists, finish teardown without throwing, and keep all captured
successors valid until processed. List destruction invokes clear with the same
requirements; violations are not swallowed or converted to empty completion.

`destroy` must perform genuine teardown at that call. Merely dropping a shared
handle while an active target lease defers physical destruction is insufficient.
The real owner must preflight/reject unsafe active-lease destruction BEFORE list
mutation. This remains a concrete full-manager integration requirement; the
list module adds no competing alive cache, observer registry or fake invalidation.
Its tests use canonical uniquely owned objects and actual Box2D teardown, not
a claim that the eventual shared target/entity destruction boundary is closed.

## Manager selection, names and respawn

Manager Add0x31502d and GetFirst0x314ff5 use selector0->list0,1->list1,other->list2.
getObjectList0x314f25 instead uses0->list0,2->list2,other->list1. These are
deliberately different. Name search0x315069 tries list0, then2, then1. Explicit
clearLists uses0,2,1 order; embedded list destruction uses reverse member order
1,2,0. It does not delete-this, reset the global manager or free metatype data.
The actual17 counts initialize zero. Add does not increment them; dedicated
methods do. Negative counts are retained; invalid categories and signed overflow
are checked. Manager-destruction service access rejects while lists are torn down.

Name condition0x314e4d reads one current name pointer, accepts NULL as no match,
and uses exact case-sensitive strcmp. This is separate from the metatype
catalogue's prefix lookup. Native rejects embedded-NUL queries and snapshots
the query value so destroying an entity whose name supplied the input cannot
leave a dangling query. The passed manager is the fixed genuine manager for the
serial operation; replacing the global manager inside callbacks is not claimed.

When respawn is requested, a found category3 entity is queried for death. If
dead, metatype is read, manager RemoveAndDestroy runs, required factory creates
the actual replacement, its real SetName endpoint runs, then Add selects0.
The category is read again by removal after metatype lookup; no stale category
cache is introduced. Other categories do not query death. A NULL replacement
raises a checked error after prior source-ordered effects, without fabricating
an entity or rolling back. Real creation/pooling and canonical setter behavior
remain required owner implementations.

## Evidence and validation

Ignored original probes execute real list/link/condition/update/selector/name
and SetName/count bodies. They cover16 mutation/context/teardown identity traces,
five selector pairs and two complete respawn paths, including a required-observer
category change during metatype lookup. The reference destructing/factory
endpoints are typed callback captures, not complete retail game-object teardown.
Comparison projects those exact endpoint orders and avoids treating stale Clear
anchor traversal as a valid native operation. Separate native tests genuinely
delete canonical objects; active Box2D contact-removal and shape-destruction
callbacks observe unlinked versus still-linked state before object free. Plain
remove/append produces no fabricated refilter or body destruction.

Additional native checks cover full-width pointer identity/count65537,
membership/preselection, name/death/null/partial-failure paths, Clear reentrancy,
capacity/counting/context and manager clear/destructor order. No manual gameplay
is required and no ARM/reference tooling is linked into the native component.
