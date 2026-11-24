# Native retail contact listener policy

OG-021 reconstructs the installed game ContactListener Add, Persist and Remove
policies using real native Box2D callbacks and RetailPhysicsBodyData. Result
is genuinely empty in the original. Production actor ownership and the game's
AI, damage, animation, controls, audio, rocket and event bindings remain required.
A passing policy test does not mean those game systems are implemented.

## Evidence and identity contract

Original Thumb entries are Add 0x00320351 (0x950 bytes), Persist 0x0031d7a1
(0x25c), Remove 0x0031a5ed (matched C++), and the matched empty Result method.
Both complete pending ARM bodies and their literal pools were inspected before
implementation. Remove is in the original characters/PhysicAttributes.cpp.
No matching ARM file, layout, status table or binary was modified.

Shape owners come from existing native body metadata, with missing metadata
equivalent to no owner as in these original callbacks. Role identities are
bijective with RetailPhysicsBodyData.owner. Live owner.levelCategory is
LevelObject::s_type: 3 Vehicle, 5 NPC, 8 the player-class branch, 12 Rocket.
Actual player identity and current vehicle are separately supplied genuine
owner pointers. Category 8 alone does not identify the active player.
GetMetatype()==38 is a different namespace and must never become category 38.

RetailContactHost is an abstract mandatory game interface. Every role accessor,
getter and game operation is pure virtual, with no optional or empty default.
Roles and getters must describe actual live game state; operations must perform
their genuine named side effects. A missing role/binding must fail explicitly.
The fixture host in tests observes dispatch and mutates only fixture state; it
is not a production AI/damage implementation.

Native role records represent observed values, not original byte layouts:

| Record | Original evidence represented |
| --- | --- |
| NPC | Signed state+428, real path presence, flags+356 and cached XYZ+296/+300/+304. |
| Vehicle | sensorContactCount+784, turnSoundPlaying byte+792, flags+344, driver+696, and the word+924 set to5000 on a live-vehicle rocket contact. No broader meaning for that word is claimed. |
| Rocket | Launcher owner+60 and explosion flag byte+92, also written by matched Rocket::explode. |
| Character | lastHitVehicle+196; the category-8 guard is the complete word at PlayerTest primary-base+372, whose broader meaning remains pending. |

Original primary/secondary base pointer adjustments select real game roles.
Native code uses actual owner identity and host roles; it does not subtract four
bytes or reinterpret ARM structures. Position/front/right/dead/dying/speed/
health/metatype/destination getters are live game bindings. Do not precompute a
single snapshot that hides earlier callback side effects.

## Add: complete dispatch order

1. Check the first Rocket, then the second Rocket. With an owned other body,
   launcher equality or an other sensor returns immediately. If not already
   exploded, a live category-3 vehicle gets contact word924=5000 and
   explode(false); all other eligible contacts call explode(true). Return after
   exploding. An already exploded rocket falls through to later branches.
   See 0x320384..0x3203b8, 0x320612..0x32068c and 0x320c12..0x320c50.
2. First vehicle-owned sensor wins; otherwise check the second vehicle sensor.
   Two sensors return without a count change. A sensor against a non-sensor
   increments its live count, then can play the horn only when turnSoundPlaying
   is false, flags344 bit0x800 is set, vehicle is not dead, and the other owner
   is the genuine player or the player's current vehicle. Return after this
   branch. Other sensor ownership does not create a new global sensor exclusion.
   See 0x3203b8..0x3203fc and 0x3206a8..0x320756.
3. With both owners present, category5/8 supplies a character and category3 a
   vehicle; original first/second evaluation order is retained. Driver equality
   with the genuine player is separate from the category-8 guard word. Require
   vehicle.GetSpeed()>18, no previous lastHitVehicle, !Character::isDying(),
   !isDead() and a zero player guard word before a hit can proceed. The direct
   isDying call was identified from GOT offset0x107c as original0x002f15a1,
   not interpreted as an unrelated global game predicate.
4. Hit amount is (sqrtf(velocity.x²+velocity.y²)/1000)*3600, in that operation
   order. It must also be strictly greater than18. The two speed tests are
   distinct; negative vehicle speed is not replaced by its absolute value.
5. Choose the strongest projection, starting with max(-v dot front,0): code1
   for a positive initial negative-front projection, otherwise code0. A strictly
   larger -v dot right chooses3; a larger +v dot front chooses0; a larger
   +v dot right chooses2. Equal candidates preserve the earlier choice. These
   are maximum comparisons, not a minimum-distance rule. Preserve each 3D
   dot's zero-Z term and the original getter order.
6. Genuine player: lock controls. Other character: virtual SetTarget(vehicle).
   Then store lastHitVehicle and call hitByCar(direction,amount,vehicle). If the
   vehicle's driver was the genuine player, victim is not the genuine player,
   and the victim's live post-hit health<=0, invoke player.onObjectKilled(victim).
   See 0x32040a..0x32047e, 0x320758..0x320784, 0x3208da..0x32096c,
   0x3209ec..0x320be4 and 0x320c5e..0x320c72.
7. A first-owner vehicle receives collision(normal*dot(-velocity,normal),
   other live category or-1). A second-owner vehicle receives the corresponding
   positive-velocity projection. When both are vehicles, after the first
   collision and before the second: if first metatype38 touches the player's
   current vehicle, or second metatype38 touches it in the opposite order,
   raise EvPlayer type22/code4. Evaluate live metatype/current-vehicle getters
   only along those original branches. Eligibility retains the fact that the
   first vehicle collision was dispatched (original r1=1 at0x3204f2), even if
   that callback changes the first owner's live category. The second category
   gate and collision's other-category argument still use their live values.
   See 0x32047e..0x320500,
   0x32078a..0x3207ec, 0x32096e..0x3209ce and 0x320bfe..0x320c10.
8. With exactly one owner, a category-5 NPC in state0/1/2 and without a path
   is retargeted. Tangent=(normal.y,-normal.x,0). If its full dot with GetFront
   is strictly negative, negate it. Set destination=current+500*tangent, limit
   100cm. Original Z adds signed zero directly. If the live NPC flags356 bit4
   is set afterward, reissue qualified Character::runToLocation on its stored
   destination. There is no category-8 redirect and no blanket sensor skip.
   Both shape orders use the supplied contact normal without an invented flip.
   See 0x320500..0x3205de, 0x3207ee..0x3208d8 and 0x320c52/0x320c74.

## Persist, Remove and jump dependency

Persist returns when both owners are present or both absent. With exactly one
owner, only category5/state0/1/2/no-path qualifies. It compares the entire
cached XYZ to zero using inclusive original epsilon0x358637bd (1e-6f), verified
in vector3d<float>::equals0x00209da5. A zero cache receives contact.normal XY
and Z=0, then returns without retargeting. Otherwise accumulate normal XY,
add zero to cached Z, set destination=current+500*cache with limit100, then
reissue base run iff the live flag4 is set. Normal is the actual native
b2ContactPoint.normal field, not its velocity. Its original ARM offsets24/28
are evidence only; no native offset cast is used.

Remove first rejects either category12 owner. Otherwise a sensor against a
non-sensor with an owner decrements that owner's vehicle sensor count. It relies
on genuine constructor-established sensor ownership and does not add a new
category3 gate. The mandatory vehicle-role binding must enforce that ownership
invariant; a guessed cast or saturating decrement is not substituted.

The original base Add/Persist/Remove methods are empty, but the installed
derived methods above are not. Only derived Result has an empty implementation.
Jump-shape processing belongs to the custom filter's original
SetCurrentJumpShape0x0031ccf1 dependency (OG020). This listener neither fakes that
operation nor infers jump flags from contact duration. The filter/height/jump
host and listener must share the same genuine owner and live state in production.

## Validation and integration limits

Actual native Box2D callback records, bodies and fixtures pass x64 Release and
Debug tests with strict FP and Debug assertions. Tests cover both shape orders,
rocket/self/sensor/exploded precedence, live vehicle word write and effect
selection, sensor counts/horn guards/player identities, exact Remove behavior,
NPC tangent/path/state/run guards, Persist zero/epsilon/accumulation,
four hit directions and ties, strict18/negative-speed/death/dying/player guards,
live post-hit health, metatype/category changes, retained first-vehicle dispatch,
collision projections/event ordering,
and explicit missing role errors. Real world.Step contacts invoke the installed
Add/Persist policy; real sensor separation invokes Remove and balances count.

An ignored research-only oracle executes the actual original ARM callbacks with
controlled native-game observations and contact records. It verifies the return
sentinel, both-order NPC state/retarget, rocket word/explosion, sensor/horn/remove,
all four hit directions/tie in both orders, player lock, strict hit amount and
vehicle collision/event order. Its game operations are explicit observation
hooks; it does not emulate a complete game or replace the native geometry tests.
Unicorn and all original binary/research artifacts remain ignored build files.
An additional original case changes the first owner's category3 to5 inside
its collision callback: collisionA(other3), EvPlayer22/4, collisionB(other5)
still occurs. The native regression fails against the frozen initial submission
and passes with the retained dispatch flag in both configurations.

No CMake or shared-header change is part of this patch. An ignored standalone
harness builds the new policy/test against each existing native Box2D library;
root will wire opengangstar_physics/CTest when ownership allows. Production role
records, actual host methods, independent actor/world updates and complete
terrain/controller/AI/damage/event bindings remain incomplete. Missing mandatory
bindings are fatal errors: the old b2World cannot safely resume after an exception
escapes Step. Negative binding tests call the listener directly, outside Step.
No manual gameplay or progression was used.
