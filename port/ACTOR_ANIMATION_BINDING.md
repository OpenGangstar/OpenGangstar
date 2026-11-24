# Original actor animation catalog and binding

OG036 adds indexed original animation selection/root binding around the verified `retail_animation` clock and sampler. It consumes authoritative decoded source metadata from `city_assets`; there is no second BRES metadata reader, animation clock, skeleton sampler, average movement speed or hardcoded walk/run clip replacement. It supplies a required host contract for actual runtime animator/root/shadow/mesh operations. A source CityNode index is never presented as an actual live node pointer.

## Original asset and ID evidence

PlayerTest::switchContext 0x00301a75 constructs `./char_pthug.bdae` in context 0 at 0x00301cba..0x00301ccc; context 1 uses `./char_pthug_jail.bdae`. It passes exact UID `Bip01-node` to Character::bindAnimation at 0x00301ce0..0x00301cea. The bind function's literal resource is `./anim_ingame.bdae`. CTimelineController::getClipStart 0x002b2855/getClipEnd 0x002b2865 index directly into12-byte source clip records; getClipLength 0x002b2875 subtracts signed end minus start. getClipIndex 0x002b2a21 scans in original array order with case-sensitive strcmp and returns the FIRST equal name or-1. Names do not replace numeric identity, and duplicate names must not collapse records.

The shared decoder preserves `CityAnimationSource{index,startMs,endMs}` on each clip and exposes an exact-index catalog loader. Existing script-facing case-insensitive lookup, including crackingHands, remains a separate existing decoder policy. The actor catalog requires actual source metadata/order and validates duration. Its exact-name lookup retains first-match semantics. All native identities/indices preserve host pointer width; serialized source indices remain fixed-width values.

The supplied anim_ingame library contains 213 clips and213 casefold-distinct names. Actual source examples are:

| ID | Exact source name | Signed start/end milliseconds |
| --- | --- | --- |
| 67 | combat_gun_rifle_hi_reload2 | 77366..78233 |
| 164 | move_hurt_to_idle | 214033..215300 |
| 165 | move_idle | 215466..217466 |
| 188 | move_run | 241167..241899 |
| 201 | move_turn | 249567..250233 |
| 202 | move_walk | 250399..251332 |
| 212 | move_walk_to_idle | 258500..259066 |

These observations validate available resources; runtime interval/root resolution uses decoded fields, not this table. Command-specific getSpecificAnimation posture/weapon mapping is owned by the actor controller and is not duplicated here.

## Binding and lifetime

Character::bindAnimation 0x002f23fd performs this order:

1. Construct a fresh animator from `./anim_ingame.bdae` with the original DefaultFactory (GOT + 0x4940 resolves to 0x004b6460). A null explicit actor argument uses the Character's actual scene instance.
2. If Character::s_timelineController is null, retain this animator's timeline. Initialize Character::s_fistsCombatSequence to literal IDs {144,94,92,93,95,90,91} at 0x002f252c..0x002f2560. Later bindings preserve the existing first timeline and sequence.
3. Call animator setTimelineCtrl(NULL), onBind(actual actor subtree), then drop the local animator reference. Binding ownership remains with the real scene; detaching the timeline is not an inferred visibility or pause flag.
4. Resolve the caller's exact root UID by getSceneNodeFromUID, and separately resolve `shadow-node`. That original lookup 0x00273021 is self-first, ordered depth-first, exact strcmp.
5. Set the FIRST shadow child's rendering layer to 1. Vtable+236 is setRenderingLayer, not setVisible. Apply literal shadow color/type pairs(0xff000000,3),(0xffffffff,0),(0xffffffff,1),(0xffffffff,2).
6. Apply literal material flags(0x400,false),(0x200,false),(4,true),(16,true), register the actual shadow through the original scene-manager registration boundary, then find the actual actor mesh with GS3DStuff::FindAMesh.

The native scene host exposes each required operation. It must implement genuine allocation/timeline retention/binding/refcount release, live UID lookup and shadow/mesh effects. Empty hooks or a source-only identity are not a supported game host. The catalog is shared application data corresponding to the first retained timeline, not a new independent timeline per actor. Host/model/catalog/nodes/mesh/live clock/flags all outlive the binding. Model topology and source mapping remain fixed for that binding. Every actor's binding instance and actual runtime pointers remain distinct even when actors share one loaded model resource.

The binding copies decoded clips only to resolve their channel node indices against the retained model's source UID order using the existing shared bindCityAnimationTracks. It separately retains the required actual actor/root/shadow/mesh identities from the host. Missing actual identity/model-root/mesh/shadow is an explicit unsupported binding. Some checks can occur after host setup effects, so construction failure is not a rollback transaction. The original may tolerate a null animation root until updateAnimation returns its early completed result, or dereference missing shadow data; those invalid/missing-scene behaviors are not substituted with fabricated native nodes.

## Clock, initial cache and live rocket reload

The existing RetailAnimationClock is the shared live actor clock. Original Character construction 0x002f3231 zeroes cache XYZ at 0x002f3298..0x002f329c; later bindAnimation does NOT reset it. Clear selection and accepted/repeated changes likewise preserve the cache and retained root height. The binding neither pre-samples the new root into the cache nor selects an initial idle on its own. Original Player context setup selects ID 165 later, after other scene/body setup; that caller ordering remains controller/host work. Spawn/reuse must preserve the verified lifecycle, not assume every binding creates a fresh actor cache.

The binding and pure selector share guardRetailAnimationSelection, which evaluates only the verified guards. Clear/deferred outcomes apply their original mutations; accepted leaves the live clock/flags unchanged and does not validate an interval or compute a rate. On an accepted transition away from 67, the required live weapon query is performed; type 7 invokes reloadRocket against the OLD LIVE actor before accepted fields are committed. The source invokes the live weapon getter again for reload; the host's reload operation must resolve the real current weapon.

The original does not rerun guards after reload. The callback may have changed the current animation, blocker flags, cached root/height or speed multiplier. The accepted commit helper is factored from the verified selector; it runs no guards/reload, reads the CURRENT multiplier, retains CURRENT cache/root height, and applies the requested accepted fields. Existing unrelated flag effects survive and request164 applies 0x80000000. A blindly assigned pre-reload preview, a guard rerun on callback-mutated state, or reload after commit changes original observable behavior. The pure selector's existing reload-intent API remains compatible; the new binding consumes its real host operation internally and returns only selection status to avoid a second reload.

Accepted rate computation and validation occur after that callback. With old multiplier 2 and finite request speed FLT_MAX, an early accepted preview would overflow; the actual reload can change the multiplier to 1, making the original accepted rate finite FLT_MAX. Guard evaluation therefore completes before accepted rate calculation or validation. Regression fixtures compare this case against actual original execution. If the actual post-reload multiplier still overflows, the existing native finite-rate check raises after the callback.

## Sampled root contract

The exact runtime root identity is exposed separately from its source model index. rootChannel requires the actual indexed clip's unique bound translation channel. Every supplied clip has that Bip01-node channel with exact clip-begin key and tail coverage. There are 3295 keys in the original full translation track and3295 in its rotation track; clip-filtered translation tracks contain3160 retained keys across the213 intervals. All supplied anim_ingame samplers, including both root tracks, have original selector 1, so the default linear sampler is evidenced for this entire fixed-library slice. Other explicitly supplied selector choices remain the caller's metadata responsibility. Sparse/nonincreasing/duplicate-time or multiple root-translation-writer cases remain explicit native unsupported boundaries rather than guessed interpolation/writer precedence.

advance delegates the verified original root-clock/sampler primitive, using the actual decoded interval and bound root track. It returns the same sampled displacement, cache/rootHeight and visual-root-zero/body velocity intent consumed by the actor controller. Zero elapsed samples and updates the cache but does not write body velocity. There is no average displacement or extra actor translation applied here. The caller supplies the genuine absolute actor matrix; matrix translation is excluded by the verified primitive.

This advance remains a ROOT prerequisite. Full animator channel application, skin deformation, intermediate pose/event side effects, final pose/root reset application and body/callback ordering are not proved by returning a root step. The actor controller must consume its exact live clock's result, perform WakeUp/body velocity and required actual-root translation work at the original boundaries, and provide real scene/pose operations. No full skin, renderer, Character::Update or production actor-instance adapter parity is claimed. Callback mutation of live selection state is supported as tested; callbacks must not throw, reenter or invalidate borrowed catalog/model/instance storage.

## Validation and limitations

Synthetic native fixtures cover exact-case/duplicate-name indexed identity, signed source intervals, missing metadata/identity errors, required setup order/first timeline reuse, source/runtime identity separation, cache preservation and first sample, repeated phase/clear, and the live reload callback that changes animation/blocker/multiplier/cache/rootHeight/flags. Supplied tests enumerate all 213 roots for both normal and jail player models, total 1278 beginning/interior/tail samples. Their runtime identities are explicit constructed host fixtures, not a claim that the actual game factory/skin instance was instantiated. Original execution compares native 639 normal-player samples against the actual global CAnimationTrack, not another clipped native sampler, and checks all 213 original interval/length/name getters.

Guard-only revision fixtures additionally cover deferred ID mutation without accepted-rate computation, preserved deferred state on rejection, old-multiplier overflow corrected by live reload, and genuine post-reload overflow rejection. The original setAnimation probe verifies the finite FLT_MAX result bit for bit after multiplier 2 becomes 1 inside the real reload call.

Original bind probes execute the real function, real UID recursion/strcmp, IReferenceCounted::drop and shadow-child setRenderingLayer. External construction/animator/material/registration operations are intercepted to inspect exact arguments/order, so this is dispatch/refcount/cache evidence, not a claim those external subsystems were ported. Both first/subsequent timeline cases preserve root cache and produce the original four output identities. A separate original setAnimation probe mutates live actor state during the real reload call and verifies continuation without guard rerun. All probes require an actual return PC; no timed-out call counts as success.

Release/Debug build/test/oracle details are recorded in the submission report. Imported invalid IDs/metadata and malformed channels raise explicit native errors instead of reproducing invalid ARM memory reads. Existing decoder key bracketing/filtering limits remain unchanged; supplied roots satisfy the verified domain. Animation events, non-player/nested external library bindings, unsupported track transformations, dynamic hierarchy replacement and full native animator/skin/render host implementations remain separate work. No gameplay progression or manual playthrough is used as validation.
