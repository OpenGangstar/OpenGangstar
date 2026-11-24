# Direct player controller boundary

OG-008 ports the direct Player walk/run command and scripted-arrival slice using
the native actor body, custom metadata, steering policy and sampled root-motion
intent. It does not replace the launcher's city preview movement or claim a
complete Player::Update. Production actor instance/catalog/scene bindings remain
required; the interfaces below make those dependencies mandatory.

## Original evidence

The reference is the pinned Android libsanangeles.so (SHA256
b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a).
Addresses retain the original Thumb bit.

| Original function/slice | Address | Native behavior |
| --- | --- | --- |
| Player walkToLocation/runToLocation | 0x301535 / 0x3014ed | Live death guard; OR0x8000; copy destination and integer10000 limit before base guard |
| Character walkToLocation/runToLocation | 0x2f400d / 0x2f3fa5 | Second live death guard, run-only down guard; replace low byte2/4, turn, select specific202/188 |
| Character getSpecificAnimation | 0x2f1a31 | Complete nine-request mapping; original context word and live weapon operations, original-1 sentinel for other requests |
| Character Stop / Player Stop | 0x2f1d3d / 0x301a3d | Low byte1, specific212, zero real body linear/angular velocity, then clear target |
| Character startTurn | 0x2f1781 | Strict positive saved direction length, write speed/turning then request201 |
| Character CheckDestination | 0x2f2135 | Real three-output steering; reachable uses original destination, blocked uses adjusted point |
| Player cinematic bypass | 0x305050 | Copy destination without rewriting reachability when cinematic and bit0x1000 clear |
| Player scripted destination | 0x305104..0x30519c,0x305656 | Reload before live0x8000 test; turn adjusted destination; set original root-motion byte; strict distance; clear0x8000, Stop, startTurn8 |
| Character updateAnimation | 0x2f3489, application0x2f35d2..0x2f368a | Wake before body velocity, retained rootZ and animated-root local zero |

Complete specific-animation mapping (`specificAnimation`):

| Request | Context1 | Context2 | Context0 one-handed | Context0 two-handed | Context0 type7 |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | 0 | 0 | 205 | 208 |
| 165 | 36 | 42 | 165 | 166 | 169 |
| 179 | 179 | 179 | 179 | 167 | 170 |
| 180 | 38 | 43 | 180 | 168 | 171 |
| 188 | 39 | 44 | 188 | 189 | 192 |
| 198 | 198 | 198 | 198 | 190 | 193 |
| 200 | 200 | 200 | 200 | 191 | 194 |
| 202 | 40 | 48 | 202 | 203 | 206 |
| 212 | 41 | 49 | 212 | 204 | 207 |

Other requests return-1 before reading context. Other context words return-1
without reading weapon state. Context1/2 do not access weapon state. Context0
tests type7 before querying the current weapon's two-handed state, preserving
live access order. The context word is not a
factory tag, LevelObject::s_type classification or physics categoryBits.

Arrival is horizontal for finite destinationZ: the original third subtraction
is destinationZ-destinationZ, not actorZ-destinationZ. This expression is retained,
so an infinite/NaN destination produces NaN and fails the strict comparison.
Exactly100cm does not finish. No teleport or duration-based completion exists.
Player +153 is Character +149, the original root-motion option; this is shared
RetailAnimationClock::rootMotion, not a fabricated arrival boolean. Stop itself
preserves0x8000; only the completion slice clears it.

## Ownership and integration

RetailDirectPlayerController borrows one world, genuine initialized player body,
live owner, command/animation state and required host. All outlive it. The owner
must be the genuine player identity used by the custom filter and body creation;
the constructor verifies identity, native body metadata/world and original player
shape category. Native pointers remain full-width; there are no ARM layout casts.

RetailDirectPlayerHost supplies real death/down state, context/weapon operations,
scene position/turning, animation selection, reload, target clearing and animated
root translation. Every operation is required. The host animation binding shares
the exact state.animation and state.movementFlags; it must retain phase/cache,
perform original interruption logic, and invoke reloadRocket before accepted
clock fields are committed. The pure clock's deferred intent alone is insufficient
for that ordering. An actual vehicle is explicitly outside this on-foot slice,
not represented as absent. Exceptions are fatal invalid-binding boundaries; a
world that throws through a contact callback cannot safely resume.

The application selects the original outer Player::Update branch. It can invoke
checkDestination(inCinematic), then the scripted-destination slice at the original
point after intervening game-state checks. This component deliberately does not
collapse those into a guessed full Update. In particular checkWeaponReload can
change the live flags before their test, and target/animation/turn callbacks see
mutations in original order. Scripted completion is not an on-foot dead/down guard.

After the real bound animation advance, applyAnimationStep consumes its evaluated
result, preserving actual velocity when no write intent exists, applying wake and
velocity to the native body, synchronizing owner.rootZCm from the shared clock,
then calling the mandatory animated-root scene binding. This must precede the
single shared RetailWorldUpdate::advance. Actual actor scene synchronization
follows physics; scripts follow that. This component owns no world accumulator,
elapsed-time conversion or Step loop. Device milliseconds must come from the
application's common integer timer epoch, with modulo subtraction retained.

World/filter/contact/height/jump lifetimes remain those of their verified native
components. Supply the same borrowed height/jump/owner state to body construction
and real callbacks. The controller does not install a substitute empty listener,
invent cliff bodies, initialize a terrain record from an empty scene, or reset
currentJumpState from a movement duration.

## Validation

The native test uses actual native player bodies, live custom filter metadata,
real steering queries and the verified selection/sampler primitive. Independent
fixtures cover upper-word retention, both death guards, run-down wrapper mutation,
phase/cache preservation, rejected animation selection, context/weapon mappings,
velocity-before-target order, exact/interior arrival, unrelated actorZ, nonfinite
destinationZ, reload-mutated flags, turn201/8, NaN direction, genuine identity and
explicit vehicle rejection. A synthetic nonuniform root with scale/rotation and
large matrix translation drives real native velocity/rootZ/root-local-zero; the
shared world clock then moves the actual body without a controller Step loop.
Synthetic intervals are test data; they are not a production animation catalog.

The ignored research oracle executes original instructions for60 mapping cases,
eight real Player/Character command cases and five scripted-arrival slices.
It verifies original return sentinels and expected flags/arguments/order. The
arrival harness starts at the isolated original branch and stops at0x30519c;
it does not execute the whole Player update. Scene turns, weapon operations,
animation selection and target operations are explicit captured boundaries.
These are independent behavioral fixtures, not a full native/original game run.

## Remaining production prerequisites

- Genuine loaded player actor instance and retained source-to-runtime root/skin
  identity, absolute matrix, scene transform and raw bbox updates; source CityNode
  indices alone are not live scene identities.
- Original full animation catalog/interval/root binding and reload-before-commit
  (OG-036); this controller owns only its three getSpecificAnimation requests.
- Loaded height scene/factory/absolute-transform adapter (OG-035), real jump
  surfaces and local terrain initialization at original call sites.
- Complete Player Update gates and ordering, turn progression, switchAnim,
  ordinary/jump height-Z update, quaternion/shadow/scene sync, target/combat and
  input policy. applyAnimationStep does not silently consume switchAnim intent.
- Live game contact host for actual NPC/vehicle/rocket/damage/path operations and
  real vehicle collection for the shared world. Present actors cannot be hidden
  behind empty hosts. NPC/vehicle/AI movement is not ported by this slice.
- Bind native movement state to VM actorMove/StopObject with real command order
  and independent actor advancement; replace compiled cinematic approximations
  only after the above bindings are genuine.

The launcher build/play actions remain available, but the existing preview walk
code is not reclassified as this controller. No gameplay or mission progression
was used to validate this work.

OG081 expands the selection leaf to its complete original request domain.
Release and Debug each compare10,896 actual original request/context/weapon
combinations, including signed extremes, with exact results and context-memory/
weapon access traces. Native dumps match byte-for-byte across configurations;
all41 integration tests pass. This does not broaden the outer Player update
or invent weapon, target or animation-scene hosts.
