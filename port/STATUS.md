# Windows x86-64 port checklist

Completion requires native implementations for the original systems and support for
all identified assets. Validate with builds, source review, automated subsystem
tests, resource enumeration, and short startup smoke tests. Do not use manual
mission playthroughs for discovery or verification.

## Code coverage

- [x] Resolve the Debug startup breakpoint in GameSWF's borrowed memory reader.
  A symbolized stack identified a read through membuf's writable accessor.
  Reads now select a const accessor; writable-buffer checks remain active.
  A focused borrowed/owned-buffer regression test reproduced 0x80000003 before
  the fix and passes afterward. Ten CTests and the automatic Vulkan opening
  smoke pass in both Release and Debug.
- [ ] Account for every original translation unit and symbol in the game's
  `config/functions.tsv`. It records 17,703 symbols across 622 source labels,
  including 15,698 marked matched for the Android build. The native build
  currently compiles four recovered translation units alongside the Windows
  port. The submodule contains 184 C and C++ translation units under
  `src/game`. Android matching and Windows coverage are separate measures.
- [x] Build and link the Win32 Release executable with Vulkan.
- [ ] Port the original renderer and resource manager. The current Vulkan
  renderer supports startup presentation, selected BRES meshes, textured city
  surfaces, alpha cutouts, and CPU skinning. Lighting is approximate. Shaders,
  effects, decals, and other retail material modes need coverage.
- [x] Preserve initial material selection and native rendering for modes
  0/13/14/15/25, including layer-0 overrides and particle preparation.
  Transparent geometry shares node-origin/layer ordering after opaque draws.
  Eleven actual GPU pixel checks verify blend factors, fractional alpha,
  strict 0.5 cutoff, depth writes and equal-depth LEQUAL. All seven Release
  CTests, retail fixture, opening and six checkpoint smokes passed.
  OG-027 also implements texture stages for modes 3/12/17, indexed sampler/image
  bindings, static texture matrices, raw-normal reflection texgen and persistent
  stage alpha state. Lighting/fog, custom GL_DECAL selection, animated materials
  and complete child transforms remain open; see REMAINING_MATERIALS.md.
  Exact-distance native material ordering deliberately avoids original
  pointer hashes and non-strict signed wrapped comparison. See MATERIAL_MODES.md.
- [x] Decode and preserve BRES node user properties through the retail
  property record. The audit found 2,092 tagged nodes, including 592
  billboards and 2,081 LOD tags. Vulkan applies authored `lodmax` cutoffs
  to tagged district surfaces using the node origin and the current camera
  eye. The test checks the inclusive 3D cutoff. Tagged district billboard
  geometry rotates around the vertical axis using the horizontal camera
  basis, with conservative frustum bounds. Offline checks verify the
  geometry and normals rotate together. The external-model LOD selector
  decodes authored thresholds, keeps the nearer level at a threshold, and
  hides beyond the group maximum. Tests cover a three-level retail bench
  and exact boundaries. The dealer sign fixture verifies the retail
  whitespace-separated `lod=60 lodmax=80` assignments. Vulkan switches eligible district instances
  whose base mesh matches the zero-distance level and whose root-level
  alternatives have available authored textures. The retail test prepares
  665 alternate draw surfaces. Alternatives use their pose relative to the
  base level and preserve the tighter instance cutoff. A fixture verifies
  placement with translation and nonuniform scale. Billboard LOD alternatives
  share the placed node's pivot and retain conservative rotated bounds.
  Base collision stays tied to the original
  district geometry. Other LOD layouts, missing textures, pixel parity,
  and node-specific physics remain open.
- [x] Apply the retail `trans_alphach_ref` material to district surfaces
  bound to `props_alpha.tga` and `props_alpha_01.tga`. The original GLES
  renderer uses a strict greater-than comparison against 0.5. The retail
  fixture verified the cutoff on 1,545 draw surfaces. Synthetic production GPU
  checks cover cutoff, surviving alpha and depth; complete retail pixel parity
  remains unverified.
- [x] Cull static district surfaces against the Vulkan camera frustum. The
  original city loader selects box culling. The port computes world-space
  bounds per draw surface and keeps boxes that touch the clip planes. Tests
  cover enclosing, touching, and outside boxes, and the opening smoke passes.
  District streaming and matching the original scene-node bounds remain open.
- [ ] Port animation, skinning, morph targets, and scene controllers for all
  models. The current implementation handles selected character clips and
  skin controllers. The payphone checkpoint arrow uses the retail rigid
  node animation with Z translation, quaternion rotation, and vector scale.
  Vehicle morphs and interactions remain incomplete.
- [x] Build the pinned historical Box2D 2.0.1 dependency for native x86-64:
  all 31 engine translation units, verified retail 1024-proxy/8192-pair tuning,
  native allocator/arena alignment, strict floating point, and retained Debug
  checks. Independent Release and Debug physics tests cover allocation cleanup,
  stepping, contacts, filters, capacity, continuous collision and a joint.
  All six Release CTests and the native opening smoke pass. Both physics
  archives contain only AMD64 objects. See PHYSICS_DEPENDENCY.md for provenance,
  source-comparison limits and adaptations. Production actor/vehicle bodies,
  custom game callbacks and complete original engine parity remain unresolved.
- [x] Reconstruct the observed original ContactFilter::ShouldCollide branches
  with real native Box2D bodies and typed metadata: original masks/groups, live
  s_type ownership, root/bbox/four-height intervals, strict rectangle boundaries,
  vehicle jump gate and directional jump dispatch. Seven Release CTests and
  both Debug physics tests pass; the native Vulkan startup smoke passes.
  Missing mandatory jump-state handling is a fatal host-binding error, not a
  no-op. Production filter installation, live body ownership, SetCurrentJumpShape
  state changes, contact listeners and production height/cliff binding remain pending.
- [x] Implement the ordinary no-jump terrain-height and owned local-cliff backend
  in retail_height.h/.cpp. Eight Release and eight Debug CTests pass. All
  12,998 supplied positive-area triangle centroid height bits and selected
  identities match an independent execution of the original ARM core.
  Native tests exercise actual bodies, proxies, contacts, metadata lifetime,
  markers, overlaps, mixed reseeds, cache mutation and fifty-slot ownership.
  At original +/-2000 m bounds, 254 selected-triangle refreshes explicitly
  reject out-of-range colliders; 16,330 body/proxy instances remain supported.
  A separate test-only wider world validates 17,085 generated proxy instances.
  The shipping world is not enlarged. More than twelve strips also fails
  explicitly; errors preserve old bodies but may leave changed query state.
  Jump surfaces, arbitrary node boxes, separate overlap helpers and production
  actor/world binding remain incomplete. See HEIGHT_SEARCH.md.
- [x] Implement both original destination obstruction-query overloads with
  actual native Box2D shapes and shared body metadata. Query cap/order, category
  margins, full 3D normalization, separate height filters, first/second-ray
  behavior, polygon corner extension, vehicle-circle adjustment and retained
  rejected fractions/scratch follow original evidence. Nine Release and nine
  Debug CTests pass; nine original ARM oracle fixtures independently pass.
  Missing native metadata, insufficient/unmatched polygon geometry and original
  zero-margin degenerate queries fail explicitly. This component does not own
  actor movement or establish production controller binding. See STEERING.md.
- [x] Implement original animation selection, integer clock, sampled root
  displacement and translation-key interpolation as a native component.
  All 213 supplied roots and 444 profile checks pass in both configurations.
  Independent original ARM comparisons pass 840 selection, 560 clock and
  72 sampler cases. A separate signed-zero regression fails before correction
  and passes after preserving initial-tail assignment versus later addition.
  Returned body/weapon intents still require original host ordering; reload
  must precede accepted clock commit. Production actor/skeleton binding and
  duplicate-time/reverse-playback domains remain incomplete. See ROOT_MOTION.md.
- [x] Implement the original contact-listener Add/Persist/Remove policy with
  real native Box2D callbacks and mandatory live game bindings. Rocket/sensor,
  character/vehicle, NPC retarget, damage-direction and event ordering have
  native fixtures and 21 original ARM oracle cases. The callback-category
  mutation regression preserves dispatch history and live later arguments.
  Production AI, weapon, damage, audio and event hosts remain unbound.
  See CONTACT_LISTENER.md.
- [x] Implement jump-surface value snapshots, original quad inclusion/ABC plane,
  live counter and successful-core-only terrain-wrapper selection. Native
  lifetime/filter tests and independent original 148-case comparisons pass in
  Release and Debug. Additional peer fixtures cover 300 cases per configuration;
  NaN payload parity is not claimed. Authored jump-body creation, owner routing,
  IsPositionOnJump and complete actor/vehicle jump behavior remain incomplete.
  See JUMP_PHYSICS.md.
- [ ] Port physics, including collision, vehicle dynamics, rigid bodies,
  and camera obstruction. Current movement uses the retail height mesh and
  approximate collision against vertical scene triangles. The vehicle wall
  test derives its footprint from the retail mesh and approximates it with
  three circles. Wall contact estimates normal impact speed from the swept
  bumper and applies the game's collision threshold and damage multiplier
  from the vehicle record. The model's four wheel meshes roll with accepted travel, and
  the front wheels steer with the current input. The vehicle camera reads
  its rear setback, eye height, and target offsets from the retail car
  record. The occupied camera retains its previous eye and applies the retail
  setback bounds and steering rules against the port's vehicle pose and speed.
  CamDefault uses its script-specified blend duration to switch from the
  scripted view to the on-foot camera. The on-foot camera uses the retail
  initialization offsets and preserves the follow heading while walking.
  Windows right-drag ports yaw and pitch, including the retail sensitivity
  difference between walking and driving and the on-foot pitch limits.
  A stationary vehicle resets its camera over 300 ms after a double
  right-click. Tap-to-target and runtime terrain-based adjustments remain
  open.
  Normal vehicle entry and exit interpolate the camera eye and target over
  the retail 100 ms and 300 ms durations. Vehicle explosions shake the eye
  and target together with the retail one-second decay and a camera-local
  random stream. Camera obstruction and other camera
  modes remain open, along with original suspension, rigid-body shapes,
  steering and handling.
- [ ] Port the game audio manager, radio, positional audio, and
  dialogue playback. A native streaming decoder provides 16-bit PCM for
  retail Ogg Vorbis resources. Scripted priority cues stop radio and mute
  the retail restricted effects in newly queued audio buffers. The supplied
  Android build's listener callback is a no-op. The Windows port reads
  effects and radio volumes from the retail preferences file in the save
  directory. The preferences writer updates those two fields while
  preserving other blocks. Options > Sound uses the retail menu clips for
  both sliders and mute controls. The menu loops retail music cue 35 and
  samples effect 225 when the effects slider is released. Volume changes
  reach newly queued PCM buffers. Res.array drives music and effects routing
  through the original m_ name prefix.
  The WaveOut backend supports per-voice gain
  and pitch through linear PCM resampling. Changes affect newly queued
  buffers. The Lincoln's V6 uses its retail gear thresholds and 1,000–5,000
  RPM sample bank. WaveOut plays the starter and blends the loops using the
  retail V6 RPM filter and sine-shaped load transition. An uninterrupted
  transition uses the retail four updates between endpoint samples. Windows
  uses its own sample-window weights and can reverse a transition mid-fade.
  The retail V6 profile disables the separate rumble layer. Clutch, limiter,
  and other engine types remain open. WaveOut plays the opening script's decoded
  one-shot and priority cues by retail sound ID. Windows Media Foundation
  handles intro media. The Lincoln starts at the station stored in its retail
  vehicle record. Radio playback advances as tracks finish and station changes
  play the original tuning cue. The selector uses a dedicated random state
  with the retail algorithm. Global random-state sharing, the retail radio
  HUD, saved station persistence, and full vehicle coverage remain open.
- [ ] Port all input modes and control mapping. XInput supports analog movement,
  driving triggers, vehicle interaction, camera orbit and stationary recenter,
  cutscene skip, and radio stepping
  while the game window has focus. The current Win32 path also supports
  mouse, keyboard movement and vehicle interaction, and touch emulation.
  Windows keyboard and controller menu navigation use retail SWF focus and
  release events. Sound sliders accept ten-point adjustments and reuse
  the retail preview and Windows preferences writer. The smoke test
  opens Options and Sound, adjusts volume in memory, and starts New Game.
- [ ] Port the AI, spawn systems, state transitions, and pedestrian and vehicle
  behavior. Only the portable state-machine base currently builds.
- [ ] Port the script interpreter and mission systems. The native reader
  indexes all retail script records and validates chapter startup, PlayScript,
  LockScript, and UnlockScript references. A native execution path retains every command
  payload and supports context-gated activation, PlayScript child suspension
  and resumption, UnlockScript, command retry, event refresh, and a callback
  for saving at root completion. Wait uses the retail strict-negative timer
  boundary and blocking-command skip. LockScript changes the target state
  while retaining its command index and pending wait. WaitCheckpoint and
  WaitCheckpointCar bind a named object through the world interface, pass
  the required vehicle to the completion check, and release their marker
  on completion or LockScript. The live world and minimap still need to
  implement that interface. Synthetic tests exercise these transitions.
  WaitStolen waits for exact named vehicle occupancy and requests vehicle
  tracking while blocked. The Windows opening reads the highlight icon
  from `Highlights.array` and displays it at the Lincoln until entry.
  Vulkan draws the authored `target_car.bdae` model as an animated child of
  the vehicle pose using the translucent world-marker pipeline. The offline
  test verifies draw geometry, live attachment, animation looping, and
  visibility around entry. The opening smoke presents a fixed fixture and
  checks Vulkan draw submission before activation, while active, and after
  entry. Pixel-level appearance and retail material parity remain unverified. The
  native asset audit loads both entries in `Highlights.array`, including the
  thug marker, and validates all four surface bindings against the decoded
  texture set. The thug marker still needs a runtime caller.
  The interpreter and opening timeline share the retail sound command
  decoder. The Win32 mixer stops ambient IDs 25 and 14 before a scripted
  priority loop. A skip suppresses playback opcodes while preserving stops.
  StopObject now has a strict decoder and an explicit shared-executor motion
  query. Its original player/category-1 flag mask, category-2/3 exact zero-speed
  comparison, and category-5 completion-bit polarity are preserved. Tests cover
  blocking, resume order, skip, missing bindings, malformed records, and floating
  edge cases. All 151 retail StopObject records across 41 scripts validate.
  This command waits; it does not cancel movement.
  Category 5 is NPC (flags356 bit 0x4000), not vehicle; category 3 uses the
  vehicle speed predicate. The prior inference that inherited NPC GetSpeed()==0
  makes NPC waits pass was incorrect. The numeric predicate did not change;
  production NPC completion-state binding remains unresolved.
  Original actor movement commands now share a typed decoder across catalog
  validation, compiled motion parsing and the VM. Opcodes 25/26 dispatch walk/run
  destinations; 34/35 dispatch run/walk paths. Skipped destinations reuse the
  existing moveTo placement hook, while skipped paths still dispatch. Tests
  verify order around StopObject, no replay while blocked, missing-host fallback,
  skip differences and malformed payloads. All 473 retail movement records
  validate. Production actorMove binding and independent actor/world advancement
  remain unresolved; this is command support, not a completed movement system. Production compiled cinematic
  timelines still ignore opcode 5 and do not yet bind the motion query.
  The live game still uses its opening timeline. Runtime dispatch for the
  remaining opcodes and behavioral parity with the retail scheduler remain
  open. The asset audit decodes the mission catalog
  and checks MissionStart commands against it. The native StayWithin
  condition preserves the retail distance rounding and ten-second grace
  timer. The native BeQuick, PhotographQuickly, and protection conditions
  preserve the recovered countdown and failure rules. The condition
  manager accepts up to eight entries, evaluates in registration order,
  and clears them on the first failure. The offline dispatch path registers
  decoded StayWithin, countdown, photography, NPC protection, and vehicle
  protection commands against placed district objects. Opening script 17
  registers its ProtectCar condition at the authored time and clears it
  at the scripted departure before activating the payphone checkpoint.
  The opening uses WaitEscape's inclusive 3D distance threshold and the
  checkpoint command's strict inside-radius comparison. The interpreter
  shares the WaitEscape distance check and can query target and player
  positions or advance on a blocking-command skip. General object activation
  and live interpreter dispatch remain open.
  MissionObjective shares its decoder with the opening HUD. The payphone
  marker uses the chapter text selected after WaitEscape.
  Enable, Disable, MoveTo, SetAnim, and SetAnim2 use decoders shared
  by the interpreter and the opening timeline. Native dispatch sends actor
  placement and animation requests to the world host. Live world mutation
  through the interpreter remains open.
  Dialog shares its decoder with the opening captions. The interpreter
  sends text, speaker, and duration to its host, blocks until dismissal,
  and clears captions on skip. Opcodes 106, 111, and 113 dispatch a named
  vehicle explosion, a HUD blink gated by tutorial settings, and chapter
  completion. Offline checks verify the stat 24 increment and dispatch
  during cinematic skip. These commands still need live mission bindings.
  ShowTutorial and HideTutorial dispatch decoded title and body IDs through
  the interpreter. The opening timeline shares the ShowTutorial decoder.
  The live timeline still controls tutorial visibility. After WaitEscape, it
  displays the localized retail Mini-Map tutorial title and mission objective.
  The retail tutorial body describes touch controls and still needs adaptation.
  ActorGetOut (99), issued for Player at the payphone checkpoint, validates
  the named actor and dispatches an exit request through the interpreter.
  The live opening validates that command after WaitCheckpoint and requests
  the animated Windows vehicle exit without an input press. The Windows exit
  waits for grounded clearance and bypasses the manual speed limit.
  The general mission interpreter still needs a live actor host.
  The payphone cinematic's four camera commands and five fades resolve from
  retail script and district assets on a separate timeline. It includes fixed
  waits, two localized dialogue cues with manual dismissal, and the priority
  sound loop's start and stop. The playback clock pauses on each dialogue cue,
  resumes on dismissal, and dispatches audio once per cue. The live scene
  starts after the scripted exit animation and uses the retail camera, fades,
  portraits, captions, and audio cues. The player uses both retail staging
  markers and resumes gameplay at the final marker. Juan and Loco use retail
  meshes, staging cues, and resolved animation clips. The player performs
  the scripted hand gesture and returns to the retail idle loop. Juan walks
  to the retail WalkTo marker using straight-line interpolation and a duration
  from the walk clip's root speed. General NPC pathfinding remains open.
  The return to the follow camera uses the retail 250 ms gameplay fade.
  At the handoff, Loco moves to the retail fight marker and displays the
  scripted hand-combat stance in gameplay. The localized fight objective
  appears after the reveal. The Combat tutorial text loads from retail data
  and still needs Windows prompts and visibility handling.
  The interpreter implements the following WaitKilled command, including
  waiting for the named actor and clearing its target markers on completion
  or skip.
  WaitForLock marks its named actor while waiting and clears the marker
  after lock, actor death, or skip. The payphone handoff runs the retail
  lock command with a minimap marker. Q or gamepad X toggles lock on the
  scripted actor. General target selection and world highlighting remain open.
  ActorAttack dispatches the retail attacker and target after lock-on. The
  live payphone binding turns Loco toward the player and advances his retail
  walk animation over grounded, unobstructed steps. The port currently uses
  straight-line pursuit and the 150 cm Fists weapon range from metatypes.gmap.
  The native loader decodes retail weapon metatype fields. Loco alternates
  the retail right and left punch clips when the on-foot target is in range
  and unobstructed. The port records Fists damage once at the midpoint of
  each clip, rechecking range and obstruction at contact. The fight uses
  PlayerTest's retail starting health of 300. The HUD uses the retail
  health gauge with a blinking fill below 150. At zero health movement stops.
  The retail collision query, defeat and respawn flow remain open.
  F or gamepad Y plays the player's
  punch. Locked, unblocked hits reduce Loco's provisional 100 health.
  The live WaitKilled ends the objective at zero health. Loco stays where
  he fell, using the retail fall-to-dead and dead-idle clips. Opcode 23
  distinguishes pending disable during playback from immediate disable
  on skip. Windows approximates pending cleanup using the camera frustum
  after the fall completes. The original death clip selection remains open.
  The post-fight scene plays two
  camera commands, two fades, seven localized dialogue cues, and the audio
  loop through script 84 and its two child scripts. Juan and the player staging markers
  and seven animation cues are bound to their retail models and rendered
  during the scene. The follow camera returns under the scripted fade,
  and the authored audio stop runs when the fade ends. The subsequent
  unlock of script 18 activates its retail WaitCheckpoint and minimap marker.
  CheckpointMission_LC also displays its animated retail world marker.
  Reaching it clears both markers, plays the retail 350 ms fade and displays
  mission 1's localized title and sprite animation. Script 18's first cinematic
  plays after that title and returns to the follow camera. It uses
  its cameras, fades, localized dialogue, staging markers, and audio through
  the shared script 84 handoff. Player, Juan, and LC animations bind to
  their retail models, and the Hummer model loads at its authored marker.
  After the cinematic the Hummer stays at its marker and the retail vehicle
  objective appears with a minimap icon. The Hummer's retail drivetrain,
  suspension, health, camera and door data load through the common vehicle
  path and drive in isolation. Its render geometry has separate wheel
  ranges and collision dimensions, with pose transforms checked in isolation.
  After the cinematic Windows switches its controllable vehicle to the
  Hummer. Entry clears the vehicle objective. The Lincoln remains parked,
  and Hummer lights and damage particles remain open. The retail WaitStolen,
  500 ms wait, and Hummer-only pier checkpoint drive the minimap, animated
  retail world marker, and scripted exit at the destination. Opcode 112
  displays the Radio Station tutorial after the wait and pauses driving
  until dismissal. The Windows prompt uses keyboard and gamepad controls.
  ProtectCar activates after the tutorial and fails the route if the Hummer
  explodes. The Windows retry restores the Hummer route and player staging
  rather than replaying the earlier mission. Retail retry cinematics, tutorial artwork and settings,
  remain open. The scripted Hummer exit starts the pier cinematic with
  retail camera, fade, dialogue, audio and Matadore animation cues.
  Matadore runs to the retail destination marker with the retail run clip.
  Windows interpolates that move directly at the clip's root speed.
  The pier handoff unlocks the Hummer-only drive to the weapon shop, with
  its localized objective, animated marker, minimap icon, and scripted
  exit at arrival. The interlude then plays the retail shop cameras,
  dialogue, fades, and audio, with the Hummer relocated to its shop
  marker. The player, Juan, and Matadore walk toward their authored
  destinations using retail clips. Windows uses straight-line interpolation
  and preserves the player's position when control returns. Original steering,
  shop interaction and the following route remain open. The shop scene
  enables the retail approach arrow and minimap marker until its
  WaitCheckpoint is satisfied. The live UnlockShop command enables the
  retail yellow entrance marker and shows an adapted Shops tutorial.
  At the entrance, press E or gamepad A on foot to clear the tutorial and
  open a Windows inventory panel. It pauses gameplay and shows all eight
  retail items with their prices and localized labels. Use Up and Down to
  select health or the vest. Enter or gamepad A opens confirmation. Health
  costs $100 and restores full health. The $200 vest refills armor to 200, which
  absorbs damage before health unless the hit bypasses armor. Windows clamps
  depleted armor to zero. Escape or gamepad B closes the panel.
  The entrance stays active for repeat visits. The Windows confirmation and
  error prompts use English. On foot, positive armor draws the three retail
  HUD layers and clips the fill from the left. Windows right-aligns the
  gauge in its 480-pixel HUD coordinate space. The cash balance uses retail
  HUD sprites and language-specific grouping during gameplay and in the
  Windows shop panel. The original cash visibility controls, shop layout,
  and weapon purchases remain open.
  After the first shop visit, script 18 tracks the Hummer's live position,
  waits for the player to reenter it, then marks the retail drive checkpoint
  at LC. Arrival fades out and waits for the scripted exit before starting
  LC's closing cameras, dialogue, audio, gestures, and actor movements.
  Walking cues use straight-line motion. The Hummer is hidden at its retail
  disable cue and removed from live playback when the scene ends. MissionOver
  awards the retail $100 once, updates progress, and presents the localized
  completion objective, reward animation, and audio. Windows then activates
  script 25's LC marker and saves the handoff. Crossing the marker now
  presents the retail fade, title, introductory cinematic, and first
  localized drive objective. The cinematic uses straight-line walking.
  The Hummer spawns at its authored position. The live script waits for
  entry, registers ProtectCar, and activates the drive checkpoint. Arrival
  requests the scripted exit, then starts the authored 300 ms fade and
  plays the Loco cinematic with its cameras, cast, dialogue, and animation
  cues. The handoff removes the Hummer and shows the localized objective.
  Both Loco actors follow their authored GMAP waypoints. The Windows
  cinematic samples the segments using clip-derived speed and projects the
  actors onto the terrain. The Hummer follows its retail drive and speed
  cues toward Marker_C2M2car01 and disappears at the authored disable cue.
  Windows samples a straight drive with terrain height and wheel roll.
  Retail AI steering remains open. The next objective begins a tailing
  sequence. The shared script runner now decodes StayBetween, computes
  the normalized spy gauge from 3D distance, and fails outside the
  authored 200�2000 cm range or on loss of the target. Windows now moves
  both Locos along the authored paths and draws the retail spy gauge.
  Automated Continue smoke verifies the handoff, live movement, and
  failure when the target falls out of range. Movement is sampled
  from the script timeline and animation speeds. The 250 ms tailing
  fade clears mission conditions, then the confrontation plays its retail
  cameras, dialogue, actor movements and animations. The player returns
  to the follow camera with the next localized objective. Automated
  Continue smoke covers the drive checkpoint, live tailing, closing fade,
  and the confrontation handoff. Script 25 then activates the retail
  walking checkpoints C2M203 and C2M202 in order, animates their world
  markers, and tracks the active marker on the minimap. Automated
  Continue smoke verifies the order and that both markers clear. The
  second checkpoint now starts the retail phone cutscene, including
  fades, camera cuts, animation, dialogue, and priority audio cues.
  It returns control at the player's location and activates C2M204.
  C2M204 now starts the retail Loco fight introduction. Windows uses its
  fades, staging markers, camera, gesture, and split dialogue timing, then
  restores the player at the last authored marker and shows the localized
  fight objective. Both Locos now pursue the player and use the existing
  native melee damage model, which approximates contact at the midpoint
  of each punch clip. The retail script checks both 4,000 cm escape
  distances in order and defers each opponent's removal until it leaves
  the camera frustum. Automated Continue smoke checks both opponents'
  attacks and escape gates. MissionOver awards the retail $200 once,
  updates mission and cash statistics, and saves the script 26 handoff.
  The LC checkpoint starts script 26's retail title and opening scene,
  which stages the Hummer and displays the first localized objective.
  Automated Continue smoke covers the live payout and separately
  restores a script 26 checkpoint. The script 26 guard now spawns at
  its authored marker with its retail health and a minimap icon. The
  player can lock on and punch the guard or either script 25 opponent.
  Windows uses the existing melee approximation, with the guard idle
  until damaged and retaliating afterward. The guard defeat animation
  and retail 500 ms wait run before the player exit command. Automated
  smoke covers damage, defeat, and that wait. After any scripted Hummer
  exit, the Cooper scene runs its authored camera and dialogue, then
  returns to the follow camera with the next localized objective. The
  Cooper spawns independently of the Hummer, rolls toward the retail
  marker at 60 km/h, and holds its last pose. Windows samples that drive
  in a straight line and defers the guard's removal until it leaves view.
  At the handoff, Loco takes control of the Cooper and pursues the
  player. Windows uses direct steering with terrain and wall checks,
  and evaluates the retail 200 m escape gate against the live car position.
  The gate awards $300 once and queues the vehicles for removal offscreen.
  Automated smoke checks both sides of the distance threshold and the payout.
  Completing the Cooper escape saves script 27 and activates the LC
  checkpoint. Continue restores that checkpoint. Script 27 plays its
  retail title and opening, protects the Hummer, and waits for entry
  before activating C2M501. Reaching C2M501 requests the scripted exit.
  Automated smoke checks the transition from the payout and a separate
  saved script 27 fixture. After the C2M501 exit, the retail 300 ms fade
  leads into the confrontation. Windows renders the authored camera,
  dialogue, staging and gestures. The three walks use animation-derived
  straight-line motion. The cutscene queues the Hummer for offscreen
  removal and returns control with the fight objective. The three Locos
  spawn with their authored health and minimap tags. The native fight
  uses lock-on melee and retaliation after damage. The script clears
  the tags only after all three opponents are defeated, queues offscreen
  removal, and honors the 3-second wait. Automated checks cover the
  multi-target gate and delay. The closing cutscene then plays the
  authored LC dialogue, camera, gestures, and fades. MissionOver awards
  $400 once and saves the script 28 handoff. Continue restores the LC
  checkpoint, which now starts script 28's retail title, opening scene,
  and first localized objective. Automated smoke covers the live payout
  and a separate script 28 save fixture. The basketball checkpoint now
  animates in the world and appears on the minimap. Reaching it on foot
  or in the player's active vehicle starts the authored court scene after
  any required exit. The scene blends back to the follow camera over
  1,000 ms and displays the court fight objective. The three guards stay
  active with retail health. Script 28 runs the timed tutorial and
  minimap tags, then waits for every guard to fall. Combat uses native
  lock-on melee and retaliation after damage. After defeat, the retail
  fade hides the tutorial and the closing scene plays its camera moves,
  dialogue, and music cues. MissionOver awards $500 once and updates
  mission stats. The automated script 28 fixture checks the closing scene,
  tutorial cleanup, payout, and mission count through Vulkan. The closing
  reveal unlocks and saves script 29 at the LC checkpoint. A script 29 save
  fixture verifies Continue, the retail title, and the opening camera,
  actor animation, music, and dialogue sequence. Script 29 shows the
  localized car objective and its approach marker in the city and on the
  minimap. Reaching it stages the protected car and four guards at their
  authored positions. Entry starts the authored ambush cameras, actor
  placement, running and weapon poses, dialogue, and fades. The follow
  camera returns to the occupied car. Five enemies are staged at their
  cinematic endpoints and tagged on the minimap with the combat objective.
  The escape sequence waits for the authored distance from each enemy
  and the final fixed marker. It then fades to black, exits the player
  from the car, and plays the two-line repair briefing. The repair
  objective marks the drive checkpoint in the city and on the minimap.
  Only the mission car satisfies the checkpoint. Arrival exits the
  player and plays the retail cameras, dialogue, reveal, and localized
  tutorial prompt. Automated checks cover the distance gate, both
  scripted exits, the car requirement, and the tutorial prompt. The
  tutorial marks the shop approach and unlocks the shop as the player
  reaches it. The native confirmation panel quotes the retail
  damage-based price, debits cash once, restores vehicle health and
  deformation, and records the retail repair stats. The fixture
  confirms payment through Win32 input and checks the resulting cash,
  vehicle state, and stats. Leaving the shop activates the delivery
  objective and marker for the required car. Arrival clears mission
  conditions and plays the retail closing cameras, actors, and dialogue.
  MissionOver removes the delivered car, credits the reward once, and
  updates the mission count. The smoke fixture covers the delivery and
  payout. The finale saves the main mission handoff, and Continue
  restores the new marker at El Toro rather than the earlier LC marker.
  Script 30 plays its retail title and opening cutscene there, then
  shows the localized objective and destination in the city and minimap.
  The arrival plays the confrontation with its authored camera, actor
  motion, dialogue, and music. Two opponents enter the melee fight from
  their cinematic endpoints. Defeating both and passing the authored
  delay starts Latisha's interlude. It moves the first two opponents
  back to their authored marks, adds the third, and returns to the
  follow camera. All three are tagged and must fall before the next
  delay. The second fight hands off to the retail Cooper departure
  scene and seats the player in the protected Cooper. The drive objective
  marks the destination in the city and on the minimap. The destination
  requires the Cooper and triggers the player's exit, then fades to
  the delivery finale. The finale clears car protection, plays the
  retail scenes and dialogue, removes the Cooper, and awards the
  mission payout once. Completion saves the script 31 handoff at El Toro.
  Continue restores the new entrance, title, and van introduction.
  Automated fixtures verify the Cooper gate, payout, and restored entrance.
  Script 31 stages the playable van, applies the retail protection
  condition, and waits for entry. After the authored seven-second wait,
  the player exits for El Toro's briefing. The drive objective and
  marker then require the van at the destination. The smoke fixture
  enters the van through Win32 input and checks the wait and vehicle gate.
  Arrival exits the player and plays the retail meeting with Juan and the
  Yakuza contact. The script then waits for reentry and the authored
  100 ms delay before El Toro's next line. That briefing activates the
  second van checkpoint in the city and on the minimap. Arrival exits
  the player and plays Juan's scene with the two Loco actors and their
  authored movement. Automated fixtures check both van checkpoints and
  the cinematic transitions. The protection encounter registers Juan
  and the contact, walks the two attackers to their authored marks,
  and waits eight seconds before directing both at Juan. Native shots
  use the retail Beretta's damage, range, and fire rate with a wall
  check. The fixture checks damage, both kill tags, and the completion
  delay. It simulates defeating the attackers. Full NPC weapon
  presentation, ammunition, targeting and gunfire AI, and player
  ranged combat remain open, so encounter balance is unverified.
  The completed protection gate plays Juan's second handshake scene.
  The retail entry cue seats the player in the van during the fade,
  and the handoff removes the defeated attackers, restores van
  protection, and starts El Toro's next briefing. The fixture verifies
  the seated player, actor cleanup, and briefing timing. The third van
  checkpoint plays Juan's arrival scene, then runs the authored six-second
  wait and police reveal. The automated fixture checks the checkpoint,
  the wait boundary, both police dialogue lines, the two-star HUD, and van
  reentry. The next objective waits for zero stars. The retail escape timer
  has isolated coverage. The wanted HUD plays the authored star and empty
  frames while no police are pursuing the player. Ambient police within
  the retail alert radius and with a clear line of sight hold the timer.
  Losing contact starts the escape countdown. Officers favor nearby
  sidewalk triangles toward the player and police cars steer toward the
  player at road junctions. The native contact check and local waypoint
  selection approximate the retail event-driven pursuit controllers.
  The Script 31 fixture holds two stars during police contact, then
  clears both stars and releases the script gate after contact is lost.
  The port shows the escape confirmation for the authored two-second
  wait, then plays the debrief after the player exit animation.
  The fourth objective requires the van at CheckpointDrive_C2M903.
  The fixture checks the delay, debrief, and vehicle-only checkpoint.
  The fourth arrival forces the van exit, applies the 250 ms entrance
  fade, and plays the retail cameras, dialogue, actor walks, gestures,
  and priority audio cues. The final objective stages the van at its
  authored marker and waits for reentry. The closing sequence clears
  the van protection, plays the final dialogue and fades, removes the
  van, and applies the retail reward once. The Script 19 unlock restores
  through Continue and displays the authored El Toro checkpoint.
  The El Toro checkpoint starts Script 19 with its 200 ms fade,
  retail title, and opening cinematic. The port stages and protects
  aPimpCarF_00, waits for entry, and activates Checkpoint_C2M1001 on
  the HUD and minimap. The first arrival fades for 300 ms, waits
  200 ms, and exits the player before playing the retail cameras,
  dialogue, gestures, and two walks. The scene stages both opponents
  at their final positions and shows the fight objective. The first
  fight tags the second opponent after 400 ms and waits for both
  defeats. After the authored 800 ms wait, the port fades into the
  dialogue interlude and removes both actors. The interlude stages
  Loco_02 through Loco_04, fades in over 200 ms, and waits for all
  three defeats before the one-second cleanup wait and debrief.
  The second debrief stages and tags four Loco guards. After all
  four defeats and the one-second cleanup wait, Checkpoint_C2M101
  appears in the world and minimap. The automated fixture verifies
  the guard gate and return trigger. The return checkpoint runs the
  500 ms fade and 250 ms wait, clears protection, removes the mission
  car, and plays the retail closing. MissionOver pays once, and the
  Script 20 unlock saves and restores the El Toro checkpoint.
  Script 20 now opens at El Toro with the retail title, camera moves,
  gestures, dialogue, and audio. It stages the mission car, requires
  entry, and tracks Checkpoint_C2M1101 in the world and on the minimap.
  The first drive checkpoint now fades into the retail arrival cinematic,
  including the four approach paths, camera cuts, dialogue, and gun-draw
  animations. The first shootout loads the NPC pistol from the retail data,
  protects Juan, LC, and El Toro, and waits for all four tagged Locos before
  its timed handoff into the second encounter cinematic. The cutscene
  restages the retail opponents, then the second wave carries ally health
  forward and clears protection after all four targets are defeated.
  The following cutscene stages El Toro's death and the four Locos'
  reinforcements. The retreat runs the retail attack delay and sequential
  100 m escape checks. Windows NPCs use nearest-enemy targeting in the
  first two fights and fire from their scripted positions during the
  retreat. Script 20 closes with the retail dialogue and unlocks Script 32
  without a payout. The Windows save restores the objective and the
  CheckpointWalk_C3M0_01 marker. Entering that marker runs Script 32's
  opening fade, Latisha and Juan cutscene, and chapter-end counter.
  The nested MissionOver command pays Script 20's reward, and the Windows
  save restores the Script 33 mission marker at CheckpointMission_C3M1.
  Entering that marker plays Script 33's title and opening cutscene,
  then requires the Lincoln at CheckpointDrive_C3M01_START. Reaching it
  fades into the race introduction, stages the Lincoln and rival car,
  and starts the race on the retail course. The player follows the
  ordered world and minimap checkpoints while the rival follows its
  authored path. The Windows port uses constant-speed rival movement
  and checks the finish order. Winning plays the retail Latisha cutscene,
  pays the race reward, and saves both Script 36 and 43 unlocks. Both
  markers appear in the world, with the nearer one on the minimap.
  The Script 36 marker plays its retail title and Latisha cutscene. The
  next objective sends the player to Checkpoint_C3M2_01 and plays the
  arrival scene. The first fight protects Juan, activates two unarmed
  attackers after the retail delay, and waits for both targets. Windows
  melee actors choose the nearest opponent. After the fight, Juan and
  the player start the debrief from their live positions. The next
  objective tracks the retail pickup and waits for its destruction.
  The reinforcement cutscene leads into a second fight that keeps Juan's
  remaining health and protection. The two Locos fight unarmed, and
  YakuzaA_00 uses the retail shotgun stats. The closing cutscene
  pays the mission reward and saves the Script 37 unlock alongside
  Script 43. Their gameplay, rival collision, and passenger ejection
  remain pending.
  The chapter-three scripted actor
  cues, retail pursuit and target selection, and the fourth van arrival
  look cue also remain pending.
  Other open work includes passenger seating, the parallel script 21
  race, retail payment presentation and audio, unaffordable repair
  feedback, and scripted look cues.
  Sparse high-seat animation channels hold their nearest
  in-range key at clip boundaries.
  Script 17's MissionOver credits the opening mission's $250 from
  Missions.gmap once and updates earned cash, mission count, and progress
  stats. Successful shop purchases update cash spent. The completion
  presentation uses the retail localized objective text, reward animation,
  and priority audio cue. The audio mixer starts the retail ambient loop
  selected by the player context. The opening route uses street ambience.
  The completion objective uses retail opening and closing banner artwork,
  with a three-second hold. The reward uses the original BIGB cash glyphs
  at the retail position. Windows draws objective text with its caption renderer.
  Live prison context transitions,
  general persistence, the remainder of script 18,
  and Juan's dynamic movement remain open.
  Opcodes 80 and 81 dispatch the retail weapon
  HUD restriction and release. Live dialogue and weapon HUD hosts remain open.
  The opening timeline draws the speaker portrait selected by Dialog from
  `cinematics_char.bsprite`, using its authored sprite placement. Windows
  reserves caption space beside the portrait. Opening dialogue and the Skip
  label use bundled fonts, with Unifont for Japanese, and captions honor
  authored line breaks. The opening smoke test rendered all six languages.
  Inline highlights use the retail `#FFFF33` color between the opening and
  closing text controls. The smoke test checks both highlighted and ordinary
  pixels. Exact caption placement and later portrait transitions remain
  unverified.
  The interpreter handles MissionOver by notifying the mission host before
  waking scripts frozen for the current player context. The save loader can
  replay script activation records and player context switches in file order
  into a reset interpreter. The live mission host and Continue remain open.
  MissionStart requests the retail title sequence, freezes other active
  scripts in the player's context, releases their bound checkpoints, and
  waits for the mission host to reach state 3 or 4. The skip path requests
  a zero-duration title.
  The Windows handoff executes MissionStart and presents mission 0's retail
  chapter animation and localized title before the opening timeline.
  The renderer combines the one-second mission reveal with the script's
  separate fade, using the retail integer alpha calculation. The opening
  title uses the bundled GROBOLDpro face and the bundled Unifont face for
  Japanese. The smoke test rendered the title and reveal in all six
  supported languages and verified every title glyph against its face.
  The original title metrics and placement remain unverified.
  The reset path rewinds commands, releases bound checkpoints, and activates
  opcode-4 startup scripts for a new game or a single explicitly selected
  script. All six chapter startup IDs are available from retail data.
  The interpreter's tick evaluates live script states in ascending index
  order. Activating a higher-index script can run it in the same tick.
  Native opcodes 15–18 request mission and startup cinematic transitions
  and wait for the host's corresponding phase. CamSet and CamDefault
  share the opening timeline's camera decoder. During a skip, CamSet
  selects the scene camera with a zero-duration transition. Offline
  validation executes all 15 commands of retail script 94 through the
  activation of script 17, including camera, fade opacity, sound, and
  cinematic animation requests. The fade path preserves entry tick
  initialization and integer alpha. Windows New Game now renders startup
  cameras and fades and plays startup audio from the interpreter. On the
  script 17 handoff it switches to the existing mission timeline. The startup
  adapter advances entry and exit phases before script dispatch on the next
  frame, as the retail manager does. Esc requests the startup skip and
  bypasses the exit transition while preserving audio stop commands.
  Startup opcode
  43 shows and clears the retail cinematic bars. The startup BARS_UP and
  BARS_DOWN animations both use the same single frame in the supplied
  archive. The startup Skip control uses the retail artwork and localized
  text. Windows anchors it at the top right. Other cinematic animations, control locking, and
  live save capture remain open.
  The interpreter shares the condition decoder and handles
  MissionClearConditions. The protection check reads
  the Lincoln's placed or live health and fails once the live vehicle's
  five-second destruction timer sets the explosion flag. Other collision
  sources, general live script dispatch, and later mission execution remain
  open. The Lincoln stops, darkens, and plays sound 110 on explosion. The
  wreck tint is applied in the Vulkan material shader. The damage state
  switches to smoke below 30 percent health and to fire at zero. The fire
  loop stops when the vehicle explodes. Smoke and fire use the Lincoln's
  model anchor, and the explosion starts at the vehicle position. Vulkan
  draws the three effects as depth-tested textured billboards. Spatial mixing
  and area damage remain open.
- [ ] Port the remaining UI, HUD, menu actions, localization, and mission UI.
  The GameSWF main menu supports the localized retail Exit confirmation.
  Options > Language cycles through the six original packs in retail order,
  updates the menu, and saves the selection without changing other
  preference blocks. The command-line language overrides the saved pack
  when starting the game.
  Current support also includes the startup and main-menu paths, the
  opening dialogue in six languages, and a simplified objective arrow.
  The minimap loader decodes the city and prison bitmaps and 18 HUD frames
  with their original anchors. Projection uses the recovered world bounds
  from `Map::setMap`, and zoom follows the retail speed thresholds. The
  opening city HUD draws the retail bezel, map, and player marker. Its map
  follows the player or occupied vehicle and rotates with the camera. The
  walking and payphone objectives use the retail checkpoint marker and its
  four-step `ICON_CHECKPOINT` loop, with
  off-screen positions clamped along their bearing to the viewport edge.
  Original marker clipping details, general object tracking,
  switching to the prison map at runtime, and in-game visual verification
  remain open.
  Windows-specific control prompts are currently in English.
  Opening mission failure pauses the scene, hides the minimap and objective
  markers, and plays the retail FAILED sprite sequence and sound cue. The
  localized retail title appears as the animation finishes. The Windows
  title uses native font rasterization and approximate placement. After the
  animation and the one-second input lock, the localized prompt blinks
  and acknowledgement restarts the port's opening mission at its authored
  handoff. The port replays script 17's opening sequence on retry and
  retains its failure count in memory. Retail recovery script dispatch,
  use of the count by the Services tutorial, and reason text for other
  conditions remain open.
- [ ] Port world streaming, district activation, and collision layers. The
  Vulkan loads high-detail scene geometry from all nine descriptors in
  retail order and frustum-culls their draw surfaces. The supplied
  low-detail scenes have a separate render pass within the frame, using the
  retail clip range from the main far plane to 200 m. Vulkan clears depth
  before the near city pass. Tests verify the clip interval and separate draw
  range. The startup smoke test submitted up to five low-detail surfaces per
  frame. GS3DStuff loads one world heightmap, named
  `santa_monica_heightmap.bdae`, with the shared navigation tables.
  All 3,133 spawn points across the nine districts resolve against that mesh.
  Direct and externally instanced Plane proxies feed the native wall solver.
  Steep quads retain their triangles. The native solver approximates the
  retail XY body with perimeter walls extending 100 cm above and below
  the authored edges. Retail data produced 11,662 proxy triangles.
  Tests cover composed placement, duplicate references, and quad boundaries.
  Authored Jump quads from placed models provide vehicle ramp heights and
  stay out of the draw mesh. The native chassis retains its exit slope
  velocity, integrates gravity, and lands on the world heightmap. Wheel
  suspension samples authored ramp heights before launch and ignores surface
  contacts while airborne. Isolated tests cover suspension tilt, climb,
  launch, and landing. Landing updates the session longest-jump stat in whole
  meters from accumulated 3D travel. Contact with any of the 11 supplied
  ramps sets a distinct bit in retail stat 41. IDs follow native district
  and node traversal. Exact retail bit ordering, jump impulse, achievement
  persistence, nested proxies, and Box2D parity remain open.
  Low-detail material parity, live spawning, and
  district activation beyond the opening remain open.
- [ ] Port save application, game-state capture, settings, and persistence UI.
  The Windows port writes an opening-mission checkpoint to `OpenGangstar.sav`
  at the script 17 and script 18 handoffs. Continue restores the position,
  cash, armor, achievement stats, and the corresponding LC entrance. The
  checkpoint uses the retail block codec and a separate filename to preserve
  imported `Gangstar2.sav` files. Arbitrary retail saves and subsequent
  Windows checkpoints remain open.
  The main menu reads achievement progress from the Windows checkpoint when
  available and falls back to the imported retail save. The automated smoke
  test selects Continue from the main menu with an isolated checkpoint in a
  fresh process. Language switching updates labels on the live menu.
  An isolated test still crashes when recreating GameSWF after destroying
  its last player in the same process.
  The native save codec reads the retail little-endian blocks, decodes known
  fields, and preserves unknown payloads when serializing. Windows can replace
  files in a separate save directory while the retail resource filesystem
  remains read-only. The save loader reconstructs player state, inventory,
  tutorial flags, and progression records in isolation. The Windows writer
  serializes a supplied state snapshot and replaces the save atomically.
  Achievement capture writes all stat IDs and completed IDs in retail order.
  Live driving updates retail stats 14 and 17 for peak whole km/h and
  kilometers driven. Wall impacts on the occupied car update retail stat 18
  with the most health lost in a single hit. The distance accumulator resets at each kilometer,
  matching PlayerTest::addDistanceDriven.
  An offline round trip verifies jump stats and completion flags through the
  save codec.
  It emits context switches around armor records because the retail reader
  applies armor to the current context. Capturing and applying the live
  game state and enabling Continue remain open.
- [ ] Port achievement tracking and UI. The native reader loads
  `Achievements.gmap`, validates rule and stat references, and implements
  the retail comparison operators. Native achievement state latches successful
  rules, queues completion notices, recalculates progress on completion, and
  replays save records in order without displaying old notices. The main
  menu loads this state from the separate Windows save directory and keeps
  the saved progress value. Game event wiring, UI, and the meaning of the
  resource trailer remain open.

## Asset coverage

Run the inventory on the prepared data ZIP or an extracted data directory.

```powershell
python port/asset_inventory.py C:\path\to\gangstar-wch-data.zip
```

- [x] Enumerate the supplied retail archive. It contains 1,146 files.
  The scan identified 359 BRES scenes, 74 DDS/ATCA textures, 22 BMPs, two
  compressed SWFs, one ISO base media file, three sfnt fonts, two PNGs,
  249 Ogg streams, and 25 empty Ogg slots.
- [x] Run the native asset audit on the supplied archive. All 359 BRES
  relocation tables passed, all 359 files opened with the current scene
  decoder, and all 96 BMP/DDS base levels decoded. Truncated BRES and DDS
  inputs failed with the expected diagnostics.
- [x] Resolve external geometry and material bindings decoded from scene
  nodes. All 5,420 decoded references resolved against the supplied archive.
- [ ] Account for unresolved image dependencies. The audit resolved 560 of
  730 BRES image entries and 553 of 723 decoded material texture
  dependencies. The 170 unresolved image entries reference 111 distinct
  texture names absent from both the supplied data ZIP and the APK. Determine
  how the retail resource manager resolves these before declaring full coverage. The
  audit opens BRES texture paths directly and detects the format from file
  contents. In the supplied archive, 111 distinct paths could not be opened
  and every referenced texture that opened decoded successfully. The
  catalog audit resolves 4,054 of 4,212 textured mesh-buffer bindings after
  applying instance material overrides. The 158 unresolved bindings use 100
  distinct texture names. This count covers decoded mesh instances in every
  supplied BRES scene, including scenes the game may load only as libraries.
  The remaining 11 absent paths have only image/material table references,
  with no decoded catalog mesh-buffer binding; their runtime reachability
  remains unverified. All 111 absent basenames are missing from both supplied
  archives. The audit reports this distinction without changing its dependency
  failure status or dropping unresolved references. The native x64 audit was
  rebuilt and rerun against the ZIP; its existing diagnostics remain unchanged.
- [x] Resolve original texture-record/sampler/image-list/image indices rather
  than flattening image catalogues. The777diffuse records across776effects
  select only1prepared secondary slot, which resolves. Hospital selects one
  texture, props_alpha_02; props_alpha_01 is an unused catalogue entry. Earlier
  1404/1409secondary-binding claims were incorrect.
  The retail alpha-reference renderer disables later texture stages and
  uses stage-zero texture alpha for the 0.5 cutoff. However, original Collada
  construction selects alpha-channel blending (type 14) for the hospital base
  material, not alpha-reference (type 15); its layer-0 props_alpha_02 texture
  does not match the game override for props_alpha/props_alpha_01. The prior
  claim that this base material inherently selects alpha-reference was incorrect.
  Native material modes now distinguish blending from cutoff. Original
  initial catalog selection is 1178 solid, 169 alpha-channel, and 19 additive
  material records before game/instance overrides. See MATERIAL_MODES.md for
  selector precedence, fixups, exact blend behavior, and remaining caveats.
  Correct native mode propagation/blending, other multi-texture modes, dynamic
  material updates, and pixel parity remain open.
- [ ] Validate every district texture binding using the same material resolver
  as the Vulkan renderer. The supplied archive covers 2,908 of 2,910 textured
  mesh buffers across the 13 district and low-detail scenes. The only missing
  district texture is `light_parking.tga`. Hollywood uses it on two
  `light_park_00.bdae` instances. The separate `light_parking_00.bdae` model
  uses the available `props_00.tga` and has different geometry. Vulkan displays the
  authored material diffuse color if the referenced image is absent and
  logs the missing path. The asset audit still reports unresolved images.
- [x] Decode the retail packed BGRA color-0 stream and apply its RGB tint
  in the Vulkan lit shader. The audit decoded 169,337 colors and the
  Hollywood parking-light fixture verifies channel order. Vertex alpha
  material modes and pixel parity remain open.
- [x] Preserve the second packed BGRA vertex color stream. The audit decoded
  19,637 secondary colors across 51 meshes. A factory fixture checks
  that the secondary stream retains its distinct RGB channels. Retail
  shader selection for this stream remains unverified.
- [x] Decode the auxiliary BRES texture coordinate stream bound by
  CColladaMeshBuffer. The retail audit decoded 15,626 coordinate pairs across
  55 meshes. The suburbs fixture checks the distinct packed values against
  the authored scale and bias. Shader use of this stream remains open.
- [x] Decode authored BRES normals stored as normalized signed bytes or
  32-bit floats. The supplied archive contains 234,052 normals across 1,875
  meshes. Vulkan uses the authored normals for lit surfaces, with face normals
  as the fallback. The CPU applies node transforms, marker animation, vehicle
  and wheel rotation, and weighted joint transforms to the normals. The retail
  test verifies that the marker normal rotates with the car. Matching the
  original lighting equation and visual output remains open.
- [ ] Validate all BRES scenes, spline geometry, animation, effects, and
  controllers against native readers. The current scene audit covers the
  fields the renderer consumes. The six retail particle scenes expose emitter
  shape, limits, lifetime, size, and speed settings, plus ten animated emitter
  parameters. The standalone particle simulation uses animated birth rate,
  size and speed variance, box, sphere, and cylinder emission, the retail random generator,
  single-precision birth accumulation,
  lifetime and size envelopes. All six supplied retail effects use boxes.
  Synthetic tests cover the sphere and cylinder spawn volumes. The Lincoln effects use the authored
  direction and angular variation. The emitter's scene instance and parent
  transforms place its spawn volume and rotate its motion into world space.
  The explosion follows its authored vertical animation and uses the sole
  image listed in its scene. Vulkan uses the original source-alpha additive
  blend for this effect. The opening smoke submits an isolated explosion
  fixture without advancing the mission. The renderer samples retail diffuse RGB
  and alpha tracks per particle using its age and emitter timeline mapping.
  The retail Collada PSpinModel initialization and application functions are
  no-ops for this particle type. The offline audit simulates each supplied
  emitter, checks finite state and color, and verifies particle expiry.
  Other animated material properties, other effect placement paths, and
  visual verification remain incomplete.
- [x] Decode both PNG resources in the supplied archive, `huds.png` and
  `icon.png`, through Windows Imaging Component. A native test verifies
  BGRA channels, straight alpha, row order, and rejection of a truncated PNG.
- [x] Decode version 1 RGB PSD sprite atlases with raw and PackBits 8-bit
  composite channels through the original sprite lookup order. Synthetic
  fixtures verify alpha, channel order, section offsets, and row overrun
  rejection. The supplied ZIP contains no PSD files.
- [x] Apply the original city texture LOD bias of -3 in Vulkan, bounded by
  the active device limit. Textures shared with other scene surfaces retain
  the district sampler, matching the original texture-object update. The
  retail ZIP has single-level textures, so the smoke verifies sampler setup
  and descriptor use. Mip selection on multi-level assets remains unverified.
- [ ] Validate texture coverage across the other retail asset sets.
  The supplied archive's 22 BMPs, 74 single-level DDS/ATCA textures, and two
  PNGs all decode. The native decoder handles ATC RGB, ATCA explicit alpha,
  and ATCI interpolated alpha. Vulkan uploads the authored mip levels for city
  textures. Offline fixtures check block stride, both ATCI alpha endpoint orders,
  odd dimensions, level order, and truncation. ATC RGB and ATCI are verified
  with synthetic fixtures. Multi-level GPU output remains unverified.
  The original renderer also has a TGA loader. The native texture reader
  decodes raw and run-length encoded TGA pixels, including indexed and
  grayscale images. Synthetic fixtures cover both origin axes, alpha, an
  indexed palette with a nonzero base, and malformed run lengths. The
  supplied archive uses ATCA DDS containers for its `.tga` files.
- [x] Port the original BMP loader variants: BI_RGB 1/4/8/16/24/32-bit,
  RLE4, and RLE8. Monochrome ignores its unused palette as the original does;
  indexed palettes retain opaque 555 quantization, and 16-bit pixels preserve
  A1R5G5B5 alpha. OG-027 corrects five-bit expansion to original bit replication
  (v<<3)|(v>>2), proven by both original RGBA32 converters for all65536words;
  the prior nearest-UNORM assumption was incorrect. Existing24/32-bit conversion
  and keepBitmapAlpha remain unchanged. All five integration CTests and six
  independently generated pixel fixtures pass; the x64 Vulkan smoke passes.
  New variants are validated synthetically, not by the supplied retail files.
  RLE4 packet padding/pointer defects are deliberately corrected; skipped output
  uses palette index zero instead of uninitialized memory, and malformed streams
  fail explicitly. These adaptations do not claim legacy malformed-input parity.
  The full retail audit output remains unchanged, including 98 decoded images
  and the existing missing-resource failure.
- [x] Decode the supplied gameplay Ogg audio. All 249 nonempty streams
  decoded through the native reader and matched their declared PCM frame
  counts, totaling 74,515,577 frames. The audit also recognized 25 empty
  audio slots and rejected a truncated stream.
- [x] Read `RadioStation.gmap` and `RadioAds.array`. All 43 song,
  advertisement, and jingle references across five radio stations and their
  shared advertisement list resolve to nonempty retail streams.
- [x] Decode type 9 spawn-point records using the original district
  spawn-point pass layout. The supplied archive has 3,133 spawn points
  with 45,923 payload values across three object types. The loader binds all
  points to 19 groups using the original longest case-insensitive metatype
  prefix, retains the first authored candidate list for each group, and
  supports modulo selection from that list. Offline checks cover every point.
  The native selection rules also implement the wanted-level police cap,
  odd-draw substitutions, and retry and respawn delay ranges. The spawn
  timer acquires reusable slots from the retail pool: one per distinct
  civilian or vehicle metatype and three per police metatype, including
  the reserved alternate officer. Allocation and release update the
  global spawned police count. The Vulkan gameplay update uses those
  slots to pose and draw the original models and release distant entities.
  Fixtures exercise all four recovered spawn classes. Eligibility checks the
  retail class and total population caps, inner and outer sphere distances,
  and the 300 cm vertical clearance. Vehicle spawn points reject active
  non-checkpoint objects within the retail 1,000 cm horizontal radius. The
  moving-vehicle check uses the top two road flag bits to choose a speed
  and compares relative velocity along the line to the actor. Fixtures
  cover the road speed and spawn heading. The native spawn scheduler
  combines the visibility query, camera-derived centers, per-point timers,
  and authored road flags with the actor and population snapshot. An
  accepted spawn updates the class count and clearance for later points
  in the same pass. A rejected spawn restores the pool slot. Ambient
  spawning starts with payphone gameplay. Entities use the authored spawn
  rotation and terrain height. Spawned pedestrians loop the retail idle
  clip through the same skeletal deformation as scripted actors. Archive
  tests check pose changes and loop boundaries for every pedestrian slot.
  Ambient pedestrians take adjacent sidewalk edges and advance using the
  retail walk clip and its root speed. Each step checks ground and walls.
  Moving ambient cars select directed road and intersection edges,
  use the encoded road speed, and stop at static walls using their
  measured footprints. Parked vehicles stay in place. The full retail
  destination search and steering remain pending. Moving traffic holds
  its waypoint when a swept footprint reaches a vehicle or pedestrian
  on the same level, and resumes once clear. This currently models
  blocking with circles. Retail obstacle avoidance, collision response,
  arrest, police weapons, and remaining despawn rules are pending. All 491 Santa Monica spawn
  points resolve to authored heightmap triangles by buffer and index offset.
  The loader reads the eight neighbor arrays per buffer and the 14,576
  road flag bytes from `nav_mesh_arrays.bin`. All 154 moving spawn points
  resolve to flag bytes with two distinct speed codes. The reader also
  decodes the eight material buffer IDs, both cliff maps, and all 27,472
  overlap links, validating the links against the heightmap. Runtime
  pathfinding and terrain transitions remain open. The loader now pairs
  the retail edge-offset and buffer arrays into 43,621 validated directed
  links and recognizes 107 boundary sentinels. The retail waypoint rule
  uses the shared edge midpoint and adds 3 cm of vertical clearance. All
  43,621 edge links yielded a waypoint in the offline check. A native A*
  query now finds directed routes using triangle-center distances. The retail
  test found an 18-triangle route between authored spawn points and verified
  every transition's waypoint. The port now applies retail destination
  material restrictions for sidewalk and vehicle routes and checks both
  wrong-way edge fields for vehicles. Pedestrian and vehicle modes sum
  squared triangle-center distances and add a squared target-distance
  estimate, as in the retail scoring. Fixtures verify both filters and
  distinguish this score from linear distance. The pedestrian and vehicle
  frontier scans candidates in insertion order and truncates each new best
  score, matching the recovered retail selection routine. A fixture covers
  fractional scores. Full route parity, cliff transitions, and runtime AI
  integration remain open. The native rules compute camera-relative inner and
  outer centers using the retail offsets, actor height, and the forward lead
  for vehicle groups at the retail speed threshold. Tests exercise both sides
  of that threshold. The native countdown waits until the update after
  expiry, retries blocked points, and consumes random draws in retail order.
  A fixture verifies both countdown paths. All 45 candidate and police
  override metatype IDs bind to their retail entity type, base transforms,
  model path, and pool size. The archive audit resolves every bound model
  to a decoded BRES scene. The gameplay loop still needs
  to drive the timers, update centers, query vehicles, and create entities.
- [x] Query ground height from a seed navigation triangle and its authored
  overlap links, preserving the selected triangle. The stacked-layer fixture
  checks both layers and height limits. Walking and driving use a 512 cm
  grid over static terrain. Boundary, negative-coordinate, and stacked-layer
  tests agree with the linear search. Walking, driving, and vehicle exit
  retain the selected contact triangle, which now has a validated retail
  navigation reference. The previous triangle, its edge neighbors, and its
  overlap links are sampled before the grid and win equal-distance ties.
  A closer valid grid candidate can still win. Fixtures cover both cases.
- [x] Resolve retail cliff face ranges by heightmap vertex. The loader
  validates the paired start/count table and triangle offsets, and the
  retail test resolves all 15,156 links across 3,850 vertices. Walking and
  driving check all 5,052 distinct authored cliff faces using the Windows
  wall-intersection routine, with visual wall geometry still used for other
  obstacles. Wall queries use the same 512 cm grid as ground queries and
  preserve collision order when merging cells. The original per-body Box2D
  strip construction has a bounded native backend; production actor binding remains open.
- [x] Load both retail vehicle light models and resolve the Lincoln's
  authored light mounts. Both mount transforms follow the vehicle's
  translation and yaw. The native loader uses the sole image entry in each
  light scene as the texture while preserving the authored diffuse tint.
  The retail fixture verifies mesh UVs, resolved materials, atlas decoding,
  and transformed mounts. The Windows driving path uses the retail
  `Vehicle::UpdatePhysic` visibility rules. With the
  brake held, forward motion selects `car_light_00.bdae`, reverse selects
  `car_light_01.bdae`, and a stop retains the selected pair. Releasing the
  brake clears both pairs. Rear wall impacts accumulate the retail deformation
  scalar and prevent pair changes while braking once it reaches 0.35. The
  opening smoke checks two Vulkan additive draws per selected pair and zero
  draws at reset and on the wreck. Both effects use the retail six-key,
  166 ms opacity track. The native driving update restarts the selected
  timeline on qualifying motion and advances a retained pair while braking.
  Offline checks cover opacity interpolation, timeline restart, and release.
  Each mount follows the vertical offset of its corresponding rear wheel.
  The suspension integrates retail spring and damper coefficients, gravity,
  and travel bounds using the native terrain contacts. The retail Lincoln
  fixture checks two suspension ticks and both mount associations on a slope.
  Wheel pivots use the authored parent nodes. Chassis pitch and roll use the
  plane through the front/rear and left/right contact midpoint directions.
  The body, door skin, and attachments share that pose. This uses the native
  terrain solver. Original chassis force coupling, terrain contact parity,
  and frame scheduling remain open.
- [x] Decode vehicle gear profile index and apply the four retail shift tables.
  The Lincoln uses profile 1. The offline fixture checks its 45 km/h
  threshold, single-step shifts, and reverse.
- [x] Decode authored forward force, mass, and top speed. Forward throttle
  interpolates retail gear power across each shift interval, holds the final
  gear multiplier, and caps speed at the authored limit. The kinematic solver
  approximates rear-wheel force at the chassis. Reverse uses the authored
  reverse force with the same approximation. Opposite input scales that
  force to brake to zero before applying drive in the new direction. Forward speed uses the
  retail health multiplier, twice the health fraction from 15% through
  50%, with a minimum factor of 0.3. The native chassis coasts down to the
  reduced limit. Original wheel-body coupling, brake dynamics, and
  state-dependent speed overrides remain open. The reverse speed limit is
  still a fixed native approximation.
- [x] Decode all 21 placed vehicle records across the nine district object
  maps and all 44 vehicle templates in `metatypes.gmap`. The loader retains
  the recovered numeric words and animation strings, including fields whose
  gameplay semantics remain unknown. All default stations, model paths, and
  door animation asset paths resolve. The opening Lincoln uses station 2.
- [x] Decode BRES morph controller source, method, target mesh indices, and
  default weights. The supplied asset archive has 21 car morph controllers
  with two targets each. The Lincoln fixture verifies that the ordered rear
  and front target meshes each differ from the 492-vertex base geometry.
  Front and rear wall impacts drive both targets in the native draw path.
  The renderer blends body positions and normals in bind pose before skinning
  during door animation. The retail fixture checks full rear and half front
  weights and verifies that rear damage remains visible in a skinned pose.
  The Vulkan smoke submits a wreck with both morph weights set.
  The native audit decoded 55 clips from all four referenced door animation
  libraries, including idle clips that start at −33 ms. The audit matched
  840 of 1,280 track instances by node name across 44 model-library pairs.
  Every pair had at least one matching track. The opening Lincoln loads
  both bound door libraries. The Windows vehicle path samples the left and
  right opening, closing, and exit clips and updates the skinned model.
  The skin reader follows morph controller references to their source meshes.
  Door transitions play retail sound 122 at opening or exit and sound 113
  at the end of the close or exit clip. The opening scene uses the original
  left and right player animations for opening the door, entering, closing
  from inside, and exiting. It holds the door open during entry and locks
  movement while the sequence plays. Seat placement is estimated from entry
  root motion, and door clip timing is synchronized to the player clips.
  The seated idle clip loops while the driver follows the moving vehicle.
  The vehicle interior surfaces draw during door transitions and hide when
  the doors are closed. Vulkan culls exterior backfaces while the doors
  are closed and draws the exterior two-sided during door transitions.
  Original seat placement, timing, close-sound suppression,
  morph deformation, unmatched tracks, and other vehicle parameters remain open.
- [x] Read all 14 retail `*_pycst.bin` tables. The native audit decoded
  3,696 entries, including the two untagged script string aliases, and
  rejected a truncated table. Win32 startup reads the full version table
  and reported asset version 3.3.2 for the supplied archive. Logo timing
  uses the retail game loop rate. Achievement progress uses the mission
  count and progression weight from the gameplay table when present.
  Other runtime consumers remain open.
- [ ] Validate all SWF, font, sprite, localization, and glyph assets. Current
  consumers use selected SWF, font, sprite, and string resources. The common
  sprite reader decodes image dimensions, modules, frame parts, animation
  references, and names. The startup logo uses its frame compositor, which
  preserves nested draw order and horizontal and vertical flips. The
  shared atlas loader follows the retail extension order. The offline CPU
  compositor renders clipped, alpha-blended modules from the decoded atlas.
  The opening dialogue overlay uses the retail cinematic bars. Their
  original entrance and exit timing, other mission and HUD integration,
  palette semantics, and additional transform flags remain open. The audit
  accepted all 79 language, character, and type tables across 11 groups and
  rejected malformed UTF-8.
- [ ] Document and validate all map, array, configuration, and miscellaneous
  binary formats. Current readers cover selected city, script, and startup
  resources.

## Validation

- [x] Win32 Release compilation and link.
- [x] Startup smoke test with the retail logo, first video frame, and
  GameSWF menu rendered through Vulkan.
- [x] The muted `--opening-smoke-test` presents the retail city and startup
  bars through Vulkan, clicks Skip through Win32, and exits after presenting
  the first dialogue portrait in the mission cutscene before player control.
- [x] Main-menu smoke checks for all six retail languages. The Japanese
  menu renders with the bundled Unifont fallback. A separate loader check
  resolved the opening dialogue to six distinct retail translations without
  executing the script.
- [x] Header-based archive inventory.
- [x] Isolated movement-collision test. It covers stopping at a wall,
  sliding along it, passing above lower geometry, and reversing away from
  a wall in a vehicle.
- [x] Isolated muted WaveOut test for concurrent voices, per-ID stop, and
  empty slots. The opening script was separately loaded and its six audio
  cues verified without executing the script.
- [x] Isolated radio tests for the retail category schedule, queue refills,
  per-station queues, and the consecutive-jingle handling rule.
- [x] Muted radio output test for default station, tuning cue completion,
  station wrapping, stop, and in-session station retention. The native
  vehicle loader independently resolved the Lincoln to station index 2.
- [x] Save codec tests for weapon field order, typed blocks, opaque payload
  preservation, and truncation. The supplied retail save's 102 blocks
  round-tripped byte for byte from a read-only temporary copy.
- [x] Save filesystem tests for write and replacement, recovery from a locked
  destination, save-path restrictions, and retail resource isolation.
- [x] Save state tests for ordered player context changes, per-context armor
  and inventory, and progression records. The supplied retail save also passed
  the isolated state loader.
- [x] Checked the supplied save's stat and achievement indices against the
  retail catalog of 27 achievements, 29 rules, and 50 stats. Isolated tests
  exercise all six comparison operators.
- [x] Tested multi-rule completion, latching after a stat drops, progress,
  reset, and notification suppression during ordered save restoration.
- [x] Vulkan startup smoke test restored two achievements from a temporary
  copy of the supplied save and presented the retail main menu.
- [x] Verified startup rejects a zero game loop frame rate and isolated
  achievement progress uses a custom mission count and progression weight.
- [x] Sampled six door poses on the retail Lincoln offline. Each pose moved
  194–197 of its 873 vertices and preserved the rest within 1 cm.
- [x] Tested that door sounds fire once at each transition and verified
  muted WaveOut playback of both original sound resources.
- [x] Played the same retail sound at 0.5× and 2× through muted WaveOut.
  The faster instance finished while the slower instance was still active.
- [x] Sampled the opening, entry, close, and exit player clips on both sides
  of the retail Lincoln offline and checked the resulting skin positions.
- [x] Verified the Lincoln selects the low seated animation and that the
  skinned player moves with the vehicle across an idle loop.
- [x] Used the retail Lincoln's measured footprint in the isolated wall
  contact and reverse tests.
- [x] Verified all four Lincoln wheel meshes roll, only the fronts steer,
  and travel stops accumulating when stationary and reverses when backing up.
- [x] Audited 106 retail scripts containing 12,450 commands across 93 opcode
  values. Checked 333 PlayScript and UnlockScript links and all six chapter
  startup entries. The reader rejected truncation and an out-of-range link.
- [x] Decoded all 66 mission definitions and resolved their eight sprite
  files and all 62 MissionStart references. The audit rejected truncated
  mission data and an out-of-range mission ID.
- [x] Decoded all 17 retail sprite sheets, covering 557 frames and 172
  animations. The audit rejected a truncated chapter sheet. The Vulkan
  startup smoke test drew the logo through the common reader.
- [x] Composed every retail sprite frame and animation frame into 3,711
  module draws. The audit rejected a cyclic nested frame, and the startup
  smoke test presented the composed logo through Vulkan.
- [x] Resolved and decoded all 17 matching atlases, checked every module
  rectangle, and rendered a nonempty sample from each sheet. Verified the
  HUD PNG fallback and the missing-atlas diagnostic.
- [x] Rendered the retail cinematic `BARS` frame offline at 480 × 320 and
  verified both bars paint while the viewport center stays transparent.
  In-game rendering of the overlay remains unverified.
- [x] Sampled the retail payphone checkpoint arrow offline. Its original
  clip moved 120 vertices and returned to its starting pose at the loop
  boundary. Visual output in gameplay remains unverified.
- [x] Decoded the 1,024 × 1,024 minimap and rasterized all 18 matching HUD
  frames. Verified zoom clamps to 1.5 and 2.5 at 30 and 100 km/h and
  interpolates to 2.0 at 65 km/h.
- [x] Decoded the 512 × 512 prison minimap and checked corner and center
  projections for both maps, including the retail Y-axis inversion.
- [x] Checked camera-relative viewport sampling, speed-based zoom, and
  preservation of the player marker's alpha channel offline.
- [x] Verified the checkpoint marker's projection is the inverse of map
  sampling and that edge clamping preserves its bearing.
- [x] Verified the checkpoint marker's retail 1–2–3–2 frame sequence,
  two-tick frame durations, and eight-tick loop boundary.
- [x] Decoded five StayWithin commands and tested three-dimensional
  distance, warning state, timer reset on return, and failure at zero.
- [x] Tested mission countdown formatting, cinematic pause, photo success
  latching, and the separate NPC and vehicle protection failure rules.
- [x] Checked script 17's protection registration, the Lincoln's initial
  health and damage flag, and failure at the destruction timer boundary
  using the retail archive and isolated damage inputs.
- [x] Rasterized the five retail FAILED frames and verified their durations
  and final artwork against the supplied archive.
- [x] Tested condition registration capacity, first-failure ordering,
  named target lookup, timer and warning output, and cleanup after failure.
- [x] Resolved all five StayWithin commands against retail district
  placements, including the prison marker and a shared vehicle. Evaluated
  both in-range and out-of-range positions without running a mission.
- [x] Decoded and evaluated 75 mission condition commands offline, including
  65 references to placed district objects. Verified that ProtectNPC reads
  its failure cause before the target name in the supplied script map.
- [x] Verified the retail Lincoln interior is hidden at rest and while
  driving, and visible during door transitions.
- [x] Verified the Lincoln selects exterior backface culling only when
  closed and checked the roof winding offline. GPU output for this switch
  remains unverified.
- [ ] Repeat signature and loader validation across retail asset variants.
- [ ] Add and run repeatable native tests for each remaining subsystem.
- [ ] Document deviations and unverified behavior as each subsystem lands.

Run the native subsystem tests from the repository root after building:

```powershell
ctest --test-dir port/build/x64 -C Release --output-on-failure
```

Audit extracted retail resources with the same native scene and texture
readers used by the game. It also decodes every Ogg stream and checks the
PCM frame count. The audit reports unresolved reference paths and
returns a failure status for missing mesh, material, or decoded material
texture dependencies:

```powershell
& .\port\build\x64\Release\retail_asset_audit.exe 'C:\path\to\gameloft\games\gangstar'
```

When `cityparts.array` is present, the audit also loads all listed
districts, applies instance material bindings, and checks each textured
mesh buffer against the decoded textures.

Run the muted audio output test with extracted retail data and a Windows
audio output device:

```powershell
& .\port\build\x64\Release\retail_audio_test.exe 'C:\path\to\gameloft\games\gangstar'
```

Check an existing retail save without applying it in the game:

Place its matching `Achievements.gmap` next to the save to also check the
saved achievement and stat indices against the catalog.

```powershell
& .\port\build\x64\Release\retail_save_test.exe 'C:\path\to\Gangstar2.sav'
```

## OG-025: native on-foot actor body backend

Implemented original body/circle/mass/filter creation, body placement/refilter and saved-filter operations using the shared Box2D world and terrain pool. Original circle radius is 0.5m, friction 0.3. Metadata survives destruction callbacks; the body pointer is cleared before owned-height teardown. Root reproduced the former stale-pointer regression in Release and Debug, then verified the correction, all14 CTests in both configurations, original initialization/placement/cleanup captures, and AMD64 PE32+ body tests. See ACTOR_BODY.md. Full scene, controller, vehicle and production actor binding remain incomplete.

## OG-030: original height initialization

Implemented distinct point-overlap and ordered scene-node initialization helpers, including live raw bounds/translations, last-successful-sibling selection, full overlap traversal, recursive partial-state behavior and core mixed node/mesh sources. Root Release/Debug builds and combined15CTest gates pass. Original executable comparisons cover19returning synthetic fixtures plus a separately stopped cycle probe and all14576point/16scene asset records, with zero differences. Release/Debug corpus bits agree. Existing12998ordinary heights and16330actual cliff proxies/254explicit original-world range errors remain verified; test-only expanded world17085proxies. See HEIGHT_INITIALIZATION.md. Production loaded-scene identities/types/raw boxes/transforms and actor-host composition remain prerequisites.

## OG-027: original material texture stages

Verified texture-stage support for modes3/12/17, indexed sampler bindings, both static texture matrices, raw-normal reflection texgen, persistent driver alpha and mode17screen blending. Root rebuilt the committed slice without pending authored-jump CMake wiring:14CTest pass in Release and Debug. Combined candidate validation also passed15tests;31actual GPU RGBA readbacks, retail fixtures and opening smokes pass in both configurations. Original probes verify state transitions, indexed fixups, matrix coefficients and all65536packed BMP words. All98supplied images decode and the exact111missing-path set remains. See REMAINING_MATERIALS.md. Full primary lighting/tracked material color, dynamic materials, full scene/skin transforms and other listed boundaries remain incomplete.

## OG-029: authored jump-surface producer

Implemented the evidenced AddXRef Jump binding, source geometry/heights/transforms and process-lifetime wrapping ID, with real static Box2D body/proxy activation and callback-safe metadata. Two authored mesh resources produce 11 selected high-detail instances; four separate lite-scene instances are additional resource validation. Root validated all 15 serialized bindings, 23 synthetic producer cases and actual original scene-child transforms. Peer review found an omitted identity-child multiplication affecting signed-zero matrix/body-yaw bits; root reproduced the failure in both configurations, then verified the correction and exact original comparison. Full Release/Debug builds, all 15 CTests and opening smokes pass. See JUMP_SURFACE_PRODUCER.md. Production visibility scheduling, full mutable scene factories, Plane colliders and actor integration remain incomplete.

## OG-034: original fixed-step world physics

Implemented original signed elapsed clamp, binary32 accumulator and inclusive fixed-step loop on the actual shared native Box2D world. Mandatory live vehicle frame/pre/post operations preserve current indexed collection semantics, including contact-driven replacement before the post pass. Root verified 33 original boundary cases, all callback observations and 1,024 sequential frames in both configurations. Full Release/Debug builds and all 16 CTests pass; native corpus bytes agree and test executables are AMD64 PE32+. See WORLD_UPDATE.md. The host still owns its single timer, timestamp commit, actor update/scene synchronization and genuine vehicle simulation; complete production world integration remains unfinished.

## OG-008/039: direct-player controller and indexed animation catalogue

Implemented original direct-player walk/run guards and flags, context/weapon animation selection, steering, strict scripted arrival and sampled-root application to the real native body. Root verified 73 original mapping/command/arrival cases and actual shared-world displacement. The authoritative BRES reader now preserves indexed signed animation ranges and offers a source-order catalogue, retaining duplicate/exact-case identities and the existing script casefold lookup. Focused metadata fixtures and all 18 CTests pass in both configurations. Both retail fixtures, 31 GPU readbacks and opening smokes pass; the expected 111 missing texture references remain. See ACTOR_CONTROLLER.md. These components do not replace presentation movement yet: genuine actor pose/root/skin identity, original animation setup and the outer player/world/VM host remain required (OG035/036/040).


## OG-035: original loaded height-scene binding

The owning static default-factory binding preserves raw mesh bounds, actual loaded translations, ordered native node identities, factory/type predicates and first recognized global height selection. Root Release/Debug builds and all 20 CTests pass. Independent original execution matches 141 nodes, 16 query states and 18 shared quaternion cases; actual constructors/factories and all jump producer/binding regressions pass. Root enumerated 12,998 actual loaded-placement centroids and 17,086 native cliff body instances, maximum 12, without range, capacity or frozen-body failures; every body/proxy was released. These actual loaded-placement results are separate from the earlier zero-translation height fixture's 16,330 bodies and 254 range failures. Full original cliff-body corpus parity is not newly claimed. Shared yaw/normalization helpers replace the identical jump implementation; raw ordinary scene quaternions remain unnormalized. See SCENE_HEIGHT_BINDING.md. Dynamic actor/controller/skin factories remain unsupported here.

## OG-036: indexed actor animation binding

The native binding preserves exact source-order clips, actor/root identities, first/shared timeline lifecycle, fists sequence and live weapon reload ordering. Root reproduced and corrected the initial accepted-rate-before-reload regression; the revised shared guard performs no accepted mutation before the live reload. Independent original lifecycle and actual 213-clip/639-sample-per-model checks pass for normal and jail models. Root reran 560 clock, 840 selection and 72 sampler cases plus the duplicate-time boundary in Release and Debug. Root diagnosed stale MSBuild objects from preserved copy timestamps, refreshed affected inputs, verified the real archive symbols and rebuilt all gates. See ACTOR_ANIMATION_BINDING.md. The required production pose, skin, material/shadow effects and retained actor scene host remain incomplete; callbacks alone do not establish gameplay integration.

## OG-037: original IJG 6b JPEG decoding

JPEG uses the pinned unmodified IJG 6b codec with the original scalar settings and non-fancy upsampling. Root original-code comparisons cover 15 fixtures, seven accepted images and 199 RGB pixels, exact warnings and five matching fatal codes in both configurations. Two truncated-input EOF cases use explicitly bounded native errors instead of the original unsafe source refill behavior. The complete 145-file upstream release and supplied Windows configuration were independently checked; all 47 C objects are AMD64 per configuration, and full upstream notices accompany the game. JPEG shares the existing texture routing; .jpg and .jpeg audit enumeration passes 2/2 synthetic images. All 98 supplied retail images still decode and the exact 111 missing texture-reference set remains. PNG and other existing decoders remain enabled. See IMAGE_FORMATS.md. PCX, PPM, WAL, other DDS variants and listed image/material boundaries remain unresolved.

Combined integration validation: all 20 CTests pass in Release and Debug, both opening smokes pass with temporary save directories, and all 31 Vulkan material pixel checks pass in each configuration. Game and selected subsystem executables are AMD64 PE32+. No gameplay progression was performed. Original ARM preservation was independently checked against all 2,969 baseline file hashes; 83 original Python tests and repository/progress checks pass. No full ARM matching rebuild is claimed.


## OG-043: original actor heading

Point-facing and incremental direction turning preserve original scalar/quaternion conventions, signed-zero behavior, strict thresholds and live getter/setter order. They reuse the existing shared EulerZ and quaternion normalization helpers. The setter marks local rotation dirty without refreshing the cached absolute matrix; final Front reads retain that original behavior. Root complete Release/Debug builds and all 21 CTests pass. Each configuration matches all 465 actual original cases bitwise, including booleans, dirty bits, stale matrices and getter traces; separate original reference/copy mutation probes pass. New test executables are AMD64 PE32+. See ACTOR_HEADING.md. A genuine retained production scene/pose adapter remains required; these bounded operations do not complete player or NPC updates.


## OG-044: original NPC paths

Owned path coordinates retain original fixed-width scalar identity and direct signed XYZ-to-float conversion, without applying common object placement. NPC path selection, loop/repeat, current-waypoint timing, live flags and required common/location/idle/random dispatch preserve original order. Root compared all 5,000 transition/state/callback cases and 92 load/access fixtures with actual original execution in both configurations: 1,371 exact points, including all 84 supplied paths and 1,364 retail waypoints. See NPC_PATH.md. The production world-object catalogue, genuine live NPC dispatch and full arrival/AI update remain prerequisites; no simulated arrival was introduced.

## OG-048: original PCX decoding

PCX now uses the original accepted 8-bit indexed and literal 24-bit paths, RLE, raw row/plane arithmetic and palette quantization through A1R5G5B5. It reuses the existing verified bitmap upload expansion. Root actual original loader/converter comparisons match 278 pixels in Release and Debug, including all 256 palette entries and the original unusual multi-plane behavior. Bounded input checks replace original out-of-range reads/writes; zero-length runs still consume input and continue. A regression fixture preserves TGA with a ten-byte comment. Automatic PCX/JPEG audit enumeration passes 3/3 generated images. No PCX exists in the supplied retail inventory; PPM, WAL and additional DDS variants remain unresolved. See IMAGE_FORMATS.md.

Combined NPC/PCX validation: complete Release/Debug builds and all 23 CTests pass; both retail fixtures, 31 Vulkan material checks and temporary-save opening smokes pass. All 98 supplied images decode and the exact unresolved-reference diagnostics, including 111 missing texture paths, remain unchanged. New subsystem executables are AMD64 PE32+. Original ARM code and source-layout contracts were not modified. Full native actor pose/skin/AI and production world integration remain incomplete.


## OG-046: original random navigation destinations

The real native height-controller slot now retains the original random-edge cursor independently of selected height triangles and visitation words. Random pedestrian/vehicle selection preserves original material costs, wrong-way gates, lane counts, shared RNG consumption and asymmetric fallback scan; no nearest-triangle replacement or artificial attempt cap was introduced. Root full Release/Debug builds and all 24 CTests pass. Actual original execution agrees on all 6,045 cases per configuration (5,888 retail and 157 synthetic), including exact point bits, all cursor fields and RNG count; 781 nonpadding original instructions execute. Root corpora match the frozen submission and new test executables are AMD64 PE32+. See RANDOM_POSITION.md. The shared application lrand48 stream, production NPC/vehicle bindings and full AI/traffic remain required; OG050 is implementing the actual RNG dependency.


## OG-047: original quaternion and typed pose evaluation

Typed translation, translation-Z, scale and quaternion tracks now share original checked key bracketing. Quaternion interpolation preserves the original first-quaternion sign flip, float slerp branches and near-linear-only normalization. The existing scene normalization API delegates to one unchanged lower-layer implementation. An optional genuine pose evaluator consumes the existing clock sample sequence, with advanced live cursor visible before callbacks; actual body/wake and final root setter remain caller operations in original order. No second clock or preview renderer rewrite was introduced. See ANIMATION_POSE.md.

Root complete Release/Debug builds and all 25 CTests pass. Actual original comparisons per configuration cover 91 quaternion cases, 80 typed synthetic cases, 144 raw retail samples, four callback/cache captures, 560 clock cases, 840 selections, 72 root sampler cases, 18 shared scene math cases, 465 heading cases and all retained jump producer/binding regressions. All 5,112 source clip/channel pairs retain brackets, and 20,448 native typed samples pass per configuration. Both retail fixtures, 31 Vulkan material checks and temporary-save opening smokes pass; 98 supplied images decode and the exact 111 missing texture paths remain. New pose tests are AMD64 PE32+. Genuine retained actor node binding, complete CPU skin and production controller/renderer use remain incomplete; OG049/051 implement those prerequisites.


## OG-050: concrete shared original random stream

RetailRandom48 implements the historical application-wide lrand48/srand48 state with defined unsigned arithmetic, original default seed and exact 32-bit seed argument bits. Copying is disabled so consumers share one serial owner. Root independently fetched and hashed the pinned historical Bionic C files, compiled them unmodified with original integer-width assertions and compared 76,800 outputs/full 48-bit states per configuration. Every value matches; full Release/Debug builds and all 26 CTests pass. Actual original startup/timer execution verifies 24 real-time boundary seeds and original store-before-seed order. Independent ELF-aware ARMv6 disassembly confirms 58 lrand48 sites and one srand48 site. Native/reference objects are AMD64 COFF and test/reference binaries AMD64 PE32+. Real two-controller navigation, interleaved consumption and fallback use one concrete stream. See RANDOM48.md. Production startup must still seed that shared instance from the original real-time timer contract and bind every consumer in original order; complete AI/traffic is not implied.

Latest original preservation check: all 2,969 tracked original file sizes/SHA256 remain unchanged from the pre-migration baseline, including pre-existing local edits. All 83 original Python tests, repository checks and progress checks pass. No full ARM matching rebuild is claimed.


## OG-051: original CPU skin vertex/normal core

The native ranged skin backend preserves source influence order, zero-weight skips, original affine point/raw-normal operations and exact total-weight division. It consumes actual current source streams and final skeleton cache matrices; it does not invent a cache refresh, morph result or posed actor. Zero-count/zero-total rows retain original nonfinite arithmetic without bind-pose fallback. Root full Release/Debug builds and all 27 CTests pass. Actual original comparisons per configuration cover 55 synthetic cases (801 finite/infinity matches, 21 NaN classifications) and six Player data/cache fixtures (14,418 exact components). New test executables are AMD64 PE32+. See SKINNING.md.

Independent raw inventory accounts for all 359 BRES files and 83 local skins, including 21 morph chains; every identified skin position/normal source is float32. No packed-normal conversion is needed by these supplied skins. Existing readActorSkins still rejects 96 zero-influence car_limo vertices. Root original execution confirms those rows produce 576 NaN components with 6,192 untouched finite components preserved; OG054 tracks faithful decoder support. Current-cache ownership, full morph behavior, dynamic bounds, buffer dirty lifecycle and production actor/renderer integration remain incomplete.


## OG-052: original cross-slot height transfer

GetHeightFrom copies only the original selected-node/triangle, A/B/C vertices, random edge/index/buffer and cached point, then delegates to the existing detailed height query. It preserves target normal, changed/ground flags, lane ordinal, intersection marker and native body ownership until that real query changes them. The caller point remains a live const reference through both copy and neighbor transitions; original alias behavior can return 100 where an external snapshot returns 77. See HEIGHT_FROM.md.

Root complete Release/Debug builds and all 28 CTests pass. Actual original execution matches all 13,062 records per configuration (64 synthetic plus 12,998 eligible retail queries; all 14,576 source triangles enumerated), including complete slot state, node identity, result bits, visitation and generation. All 90 wrapper instructions execute. Real body-retention/delegated-destruction tests pass; new executables are AMD64 PE32+ and corpora match the frozen submission.

The retained actor owner remains under revision: independent review found unsupported per-UID mutable resource sharing across repeated geometry/controller instances. Its first submission is not merged. Original CPU skin and height primitives are implemented, but complete actor ownership, dynamic bounds, animator/morph application and production world/renderer composition remain pending.


## OG-049: corrected retained mutable actor scene

The DefaultFactory Player owner now retains genuine live root/source/generated ordinary and skinned nodes, exact UID/scope identities, mutable TRS/cached transforms, separate animator lists, visibility/buffer bookkeeping and actual joint pointers/final skin cache. Independent review caught unsupported per-UID sharing of mutable resources. Root reproduced the old failure, then verified fresh geometry/skin/base resources per original factory construction and persistent repeated-instance isolation regressions. Immutable decoded sources remain shared.

Root complete Release/Debug builds and all 29 CTests pass. Actual original comparisons cover 800 node snapshots, 44 primary final joint matrices, two-root identity/cache isolation, 21 bind-identity boundaries, four persistent animation masks, list retention/dispatch and both visibility overrides. The corrected oracle constructs fresh original resources instead of supplying a per-UID cache. Original fresh-factory probes execute geometry/skin/base allocation and construction with explicitly bounded research CPU-buffer interception. Both retail fixtures, 31 Vulkan material checks and temporary-save opening smokes pass; 98 images decode and the exact 111 missing texture paths remain. New tests are AMD64 PE32+. See ACTOR_SCENE.md.

Dynamic skinned bounds, actual full global animator application, material/layer/color state, shadow registry, CPU process buffers and production body/world/renderer composition remain separate prerequisites. The old raw-only bound is not labeled as a dynamic skin bound. The inherited zero-influence limo decoder restriction remains tracked by OG054.


## OG058 / OG060: CPU morph core and complete animation source

Root independently verified both native configurations: full builds and 31 CTests pass. The full source library retains all 24 tracks, 73,176 signed-time keys and source metadata; every record matches raw BRES, and 240 actual original animator construction/application checks pass per configuration. Existing 213 clipped animations and both Player root consumers remain valid. CPU morph matches the original function and weighted helpers for 48 synthetic plus 126 retail cases, 372,348 component bits per configuration, covering all 21 supplied controllers and 61,464 evaluated vertices. The original zero-base-weight initializer quirk is preserved.

All checked executables are PE32+ AMD64. Asset audit remains 98 decoded images and 111 missing texture paths; both Vulkan opening smokes pass with isolated saves. Morph driver allocation/lifetime and live weight producers, retained native full-pose evaluation and complete scene/game hosts remain pending. No manual gameplay or original ARM source changes.


## OG063: original mutable material scalar state

Implemented native fixed32 material color/flag/dirty state and exact requested-primary selector. Public flag4 changes word0 mask1, not stored flags40 mask4. Shared tracking derivation and current-color mutation eliminate repeated native logic across flag/color/alpha leaves. Root actual-original comparisons pass for 6,720 mutations, 47,040 exact state words and 13,440 GL primary selections per configuration, including unchanged material bytes. Full Release/Debug builds and 32 CTests pass; the new test executable is PE32+ AMD64.

Per-instance scene material ownership, original traversal and OG059 loader/shader correction remain pending. Existing renderer has not been switched by this scalar-state change. No original ARM edits or manual gameplay.


## OG061 / OG065: original shadow lifecycle evidence and local projection

Root independently replayed all 54 original scoped shadow graph/bind/projection/lifecycle probes. Implemented local projection from actual actor worldXY, Character floor160+3cm and live PhysicInfo.changed/normal. Shared existing exact vector/from-angle-axis/from-to math with heading; no second quaternion implementation. Root72 original full updatePosition comparisons/config match504 position/quaternion words and preserve stale caches;465 original heading regressions pass. Full Release/Debug builds and33 CTests pass; new executable PE32+ AMD64.

Global shadow reparent/retention, pool-aware teardown/visibility, late world frame and genuine material/render ownership remain pending. The new local projection does not implement an empty registration host. Separately, OG062 full-pose checks discovered wrong typed position/scale masks in049; OG067 corrects them before pose integration. No manual gameplay.


## OG067: correct typed retained animation masks

Whole-pose original comparisons exposed an earlier049 mapping error. Corrected position/PositionZ to persistent mask4, quaternion remains2, scale to1. Position4 takes the existing translation-only branch; scale1 rebuilds the basis. Root actual full forceBind/getter probes prove each mapping; new native regression fails old code in both configurations and passes the fix. Typed64 matrix bits/config,800 original owner snapshots and44 live joint caches plus retention regressions match. Full Release/Debug builds and33CTests pass. No setter-bit substitution, clock change, original ARM edit or masked failure.


## OG056 / OG062: real retained full actor pose

Implemented actual detached plain animator over the full24-track source and genuine retained scene targets. Typed masks4/2/1, source-order NULL slots, persistent bound retention, same-root no-op/forced repair, UID-based old callbacks after rebind, unsigned time/library period, shared/per-slot scratch and direct animateNode forwarding preserve original behavior. Existing clock consumes true live Bip01 position without early root stripping or an extra frame clock. Weighted apply for these four source track classes is the verified original no-op after scratch evaluation, distinct from weighted getters. Weak owner context prevents native ownership cycles and explicitly rejects expired reuse.

Root original R/D comparisons:288 signed/duplicate/wrapped sampler cases,47,160 complete nodeTRS/flag records and31,440 track applications/scratch entries per configuration across both Player models,655 profiles/model and all213 clip boundaries/midpoints. Identical native dump SHA2568d315643e4c3c1b1eae04bba447e38c85953600df7fb1625ccea2afe90275846. Existing213root/444clock and bothPlayer639sample tests pass; original91slerp+80synthetic+144retail old-pose regressions remain exact. Full native builds and34CTests/config pass; checked executables PE32+AMD64.

Full scene/material/render/body/world composition, generic AnimatorSet/blender/timeline/event behavior and source domains outside the documented plain path remain incomplete. No full-game completion, original Android libm identity or manual gameplay claimed.


## OG055: live original skin bounds and authoritative joint boxes

Decoded original full116-byte skin record/count108/table112 into optional ordered joint boxes. Each fresh retained mesh owns one raw-seeded box and shares its existing dirty flag with the actual skeleton cache. Explicit compute forces real preparation even clean; palette-first can expose an older box. Bounds read live joint absolute pointers/hints, original min/max-two-corner transform, strict union and uint8 loop; transformed skin getter aliases without extra transform. Unknown metadata always rejects.

Root both builds:18core cases/108bits and20 actual factory/scene/cache snapshots/14,200bits exact; native81of83metadata tables/5,640bits match independently enumerated raw359BRES/83tables. Existing car_limo zero-row rejection withholds both interior-mesh-skin and car_body-mesh-morpher-skin;054 follows. Combined062 complete native pose dump remains byte-identical to original-verified hash. Full Release/Debug builds and35CTests pass; new test PE32+AMD64. Default-enabled owner only; original disabled-cache behavior and remaining container/non-skin aggregate getters are documented. No original ARM edits or manual gameplay.


## OG054: all original skin influence rows load

Authoritative reader now preserves authored empty influence rows, consuming no joint/weight pairs. Nonempty totals/references and trailing-pair checks remain. Root old decoder fails new serialized regression; corrected full Release/Debug builds and36CTests pass. Independent raw359BRES comparison matches all83skins/27,434vertices, controller indices, every empty/nonempty row, joint index and weight bit. The96 limo rows remain empty and produce original576 NaN component classifications through the existing CPU core; actual original two-range execution also matches6,192 finite/infinity components. No bind-pose weights or NaN-payload identity claim.

All83 authored joint-box tables now match5,676 native component bits, including both previously withheld limo skins. Acceptance does not claim full vehicle/process-buffer/render ownership. Original ARM files unchanged; new test PE32+AMD64; no manual gameplay.


## OG069 / OG071: true pre-physics floor state and commit

Root independently replayed13 original floor/post-body/post-world source probes. Implemented named retained Character floor160 separate from animation rootHeight156. Genuine cached actor world-position values are read twice, the same actor height controller queried at slot0/detailed=false, floor stored, live root height added, then local position committed with nonrecursive absolute update. Preserved unselected wrapper cache, stale cached-vs-local and nonidentity parent behavior. No extra terrain selection, inverse-parent correction, clock, body teleport or refilter.

Root both configs match5 actual original continuations/60floor-local-normal-cache-flag words and4 real decoded Player/terrain commits, preserving descendant cache bytes. Full Release/Debug builds and37CTests pass; newexe PE32+AMD64. Full original outer branch/metadata/world/target/shadow/render integration remains pending; post-world readback must use retained pre-step floor and avoid a second height query. No manual gameplay.
