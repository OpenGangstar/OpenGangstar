# Live actor collision reads

OG075 adds a live path to the existing native contact filter and actor body. The
constant-input numerical adapter remains available for fixtures. A live filter
cannot consume a snapshot node box, and the numerical constructor rejects live
metadata. Neither path silently substitutes the other.

## Concrete binding and ownership

`RetailPhysicsOwnerReads` supplies canonical owner identity, virtual
`GetPosition` Z and the stored root height. `RetailPhysicsNodeReads` supplies
canonical owner identity and the genuine transformed-node box. All operations are
required; there are no default callbacks. Separate owner/node bindings preserve
the original nullable metadata node independently of the owner.

`RetailActorCollisionBinding` implements both using a retained
`RetailActorScene`, an explicit actual actor node, an explicit actual
`Character::getSkinMesh()` node, and a borrowed existing `RetailAnimationClock`.
It validates instance membership, actor-subtree membership, skin runtime type and
known authored joint-box metadata during setup. It does not select a node by
source index, UID, first mesh, or actor-root assumption. Ordinary/container
transformed getters are rejected by this concrete binding until their separate
implementation is available.

The binding reads actor cached absolute Z without refreshing a transform, reads
the actual clock's stored `rootHeight`, and calls the skin node's existing
`transformedBoundingBox()`. It creates no clock, counter, sampler, box cache or
dirty flag. `levelCategory` and `currentJumpState` remain canonical live fields
of the existing owner record; the latter is already shared with the jump
controller. Position/root snapshot fields are not read by the live path.

`RetailActorPhysics::initializeLive` requires the matching canonical owner and
retains both bindings in native body metadata after original shape creation and
mass calculation. Initialization performs no box or palette read. Existing-body
return still precedes validation of replacement inputs. Body metadata keeps the
scene and its actual nodes alive through `DestroyBody` contact callbacks, then
releases them. The owner and borrowed clock must outlive every body retaining the
binding. `synchronizeSkinNodeBounds` rejects live bodies. Detaching body user data
retains the original absent-metadata filter fallback; it does not transfer or
delete the backend's owned record.

## Original evaluation order

Reference `libsanangeles.so` SHA256
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Addresses include the Thumb bit when naming functions.

* `ContactFilter::ShouldCollide` at `0x31dc7d` applies the shape mask/group gate,
  then captures both body metadata pointers before invoking any getter. A missing
  pointer returns true without scene operations.
* A node-backed category-3 vehicle tests the canonical signed jump state first.
  Positive state against a node-less body returns false before player lookup.
  Category-5 NPC bypasses player lookup. Other eligible branches invoke the live
  genuine-player lookup separately for each side.
* Character intervals call owner virtual `GetPosition` before reading stored
  Character `+156`: `(positionZ-rootHeight)-25`, then `(lower+150)+25`. Matched
  `LevelObject::GetPosition` (`0x31684d`) calls cached scene absolute-position
  getter `0x273b3d`. The root height is the retained animation sample written at
  `0x2f3678`, before root stripping at `0x2f368a`; it is neither the current Bip01
  local Z nor floor `+160`.
* Other node-backed branches call virtual slot 52,
  **getTransformedBoundingBox**, and copy its Z interval immediately. The first
  interval is captured before the second side's live dispatch. The second
  node-less branch re-reads shape categories after the first getter.
* Jump flags, transform column 2 and the other body's velocity are read after
  interval evaluation. A positive directional gate still requires the genuine
  `SetCurrentJumpShape` handler and returns false, independently of overlap.

The actual skin transformed getter `0x2b2565` delegates through mesh-node local
getter `0x2a2685` to lazy skin box getter `0x2b23b9`. That skin-specific alias adds
no node transform. Dirty boxes invoke the existing cache/bounds work; a palette
read can clear the same dirty flag while leaving an older box. NPC/player
intervals bypass this getter and preserve its dirty state. There is no eager
frame/step copy, forced bounds refresh, or second bounds-dirty flag.

Ordinary/base getter `0x272b05` is a different operation, using
`transformBoxEx` (`0x243cad`) and its own cached box/flag `0x200`, without an
absolute-position refresh. It is not the skin two-corner `transformBox` helper.
This binding does not approximate that currently separate domain.

## Steering candidates

Both original `canGoToPoint` overloads also call transformed slot 52: point
`0x318d98..0x318da8`, detailed `0x318284..0x31829c`. They retain the candidate
metadata pointer captured before `TestSegment`, then read node presence and call
the getter only after a hit, before height/scratch work. Native steering now
supports the same live node binding at those points. Existing equal-height,
fraction carry, first-ray selection, strict fraction and scratch quirks remain.

The query actor itself still follows `RetailSteeringActor`'s existing supplied
current-point contract. The original repeatedly calls its actor position getter;
this API requires those values remain identical during a query. It does not
claim support for an arbitrary actor-position callback changing while candidate
getters execute. The concrete candidate binding changes palette/box cache state,
not the query actor's cached transform. No steering header or actor-position
interface was broadened.

## Validation

Release and Debug tests use genuine native Box2D bodies and retained scene nodes.
They cover live/canonical identity, late player/category/node/shape reads,
stored-root-height mutation after position, masks, detached metadata, invalid
binding errors, and both shape orders. A real `world.Step` produces contact Add;
body destruction Remove observes complete retained metadata and a callable scene
binding. Releasing external scene/binding references keeps them alive until body
destruction and then releases them without a cycle. The listener in that test is
a lifecycle observer, not a substitute game ContactListener implementation.

All 34 OG073 original timing cases have matching native results and relevant
virtual-call order: 18 controlled policy cases plus 16 phases using both actual
Player graphs. Dirty state and 3,540 box/palette float components match exactly.
Controlled ordinary getter values exercise filter policy only; they do not claim
a native ordinary-node implementation. The concrete binding rejects those nodes.

Both steering overloads additionally match 24 original Player scene/cache phases,
including hit rejection and miss bypass: 7,160 box/palette components plus all
144 hit/adjusted output components. Native executes real circle segment tests;
the original harness supplies the independently matched hit fraction/miss and
executes actual scene/getter/cache operations. Original query allocation/order
and complete Box2D execution are outside that particular oracle. Legacy steering
geometry, fraction/scratch and binding tests remain enabled.

The ignored research report provides exact scripts, hashes, commands and build
results. Getter exceptions during legacy Box2D stepping are fatal binding errors;
the world is not a supported recovery boundary. Bind correct supported resources
before stepping. This slice supplies a real retained filter/body/steering binding;
the complete production actor factory, world host, vehicle transfer, AI and game
contact-listener installation remain separate integration work.
