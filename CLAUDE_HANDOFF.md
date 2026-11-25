# OpenGangstar handoff to Claude

Prepared 25 November 2026 after the user's request to finish in-flight work and
hand off. The overall goal is paused and **not complete**. Workers have stopped;
do not restart an automatic development loop without the user's direction.

## Start here

Use `C:\Users\Ethan\Documents\GitHub\OpenGangstar`, branch `main`, remote
`https://github.com/OpenGangstar/OpenGangstar.git`. The chat's old
`GangstarDecomp\gangstar-wch` working directory is not the integration repository.

The last verified code integration is `42b2861`, following movement/camera fix
`ce3ca50`. This handoff is a subsequent documentation commit. Inspect current
Git state before making changes. The worker patches described below are already
integrated; do not apply them again.

Read these repository records selectively:

- `port/WORK_QUEUE.json`: task IDs, scopes, dependencies, reservations and evidence.
- `port/STATUS.md`: implementation/validation history. Its legacy encoding is
  not valid UTF-8; append ASCII bytes rather than rewriting the whole file.
- `port/MIGRATION.md`, `port/README.md`: x86-64 build and launcher setup.
- `port/OBJECT_FACTORY_COVERAGE.md`: original factory dispatch and payload tails.
- `port/ACTOR_DEFORMATION.md`, `port/GAME_OBJECT_LIST.md`: final integrated work.
- `port/CHARACTER_TARGET.md`, `port/CHARACTER_RELOAD.md`: canonical component
  interfaces and their required, still-uncomposed production endpoints.

## User's actual objective and constraints

Produce a complete, faithful native **Windows x86-64** Gangstar: West Coast Hustle
port using **Vulkan**, with launcher game selection and one-click **Build** and
**Play**. The later 64-bit requirement supersedes the earlier x86 instruction.
The Android APK, ARM/Android emulation and original Android runtime are not native
runtime dependencies. Modding is not a priority.

Port/account for all available original code and major systems: rendering,
audio, input, physics, AI, scripting, animation, UI, world streaming and saves.
Support every identified relevant asset format. Preserve original game behavior,
binary asset layouts and the separate matching ARM decompilation. Consolidate
actual duplicate implementations without deleting distinct game behavior.

Do not fabricate gameplay logic, silently no-op missing functionality, suppress
errors, or substitute a demo for the intended complete port. A menu, successful
build or an in-game screenshot does not establish completion. Record partial,
implemented, validated, missing and unverified behavior accurately.

**No manual mission progression or playthrough.** Use original-code evidence,
compilation, static checks, automated subsystem tests, complete asset enumeration
and short startup smoke tests. The user can test gameplay themselves. Do not ask
them to play through missions to discover assets or establish completeness.

## Finished before handoff

| Commit | Result |
| --- | --- |
| `efdc8cd` | Character reload guards, captured weapon/type semantics and original animation dispatch. |
| `530c3b1` | Complete ordered metatype catalogue and shared original longest-prefix lookup. |
| `dccc19a` | One common weak Character target and distinct rich Player target transitions. |
| `9877f5a` | Original object factory dispatch/read coverage and remaining implementation boundaries. |
| `ce3ca50` | Reported interactive movement speed, reversed horizontal input and vehicle camera follow bugs. |
| `42b2861` | Loaded DefaultFactory deformation composition and actual intrusive game-object lists. |

The latest user-reported bugs were reproduced before changing code:

- Interactive movement used cinematic `move_walk`, including its slower travel
  speed and animation. It now uses the supplied steady unarmed `move_run` clip188
  and authored mean root travel speed **428.971 cm/s**. Cinematic `move_walk`202
  and its **136.614 cm/s** speed remain separate.
- Positive horizontal input used the opposite of the rendered camera-right
  vector. Movement now agrees with `cityCameraMatrix`'s `cross(forward,+Z)` basis.
  Regression tests cover four headings and all four cardinal input directions.
- Main playback time was permanently clamped at the opening duration. Every
  later vehicle-entry transition remained active and suppressed vehicle camera
  updates. The opening-specific advance overload now continues its existing
  clock into gameplay. Standalone cinematic timelines remain bounded. Automated
  production advance/entry/driving/exit checks cover the100/300 ms handoffs.

Launcher Play already selects the rebuilt
`port/build/x64/Release/gangstar-wch.exe`. Both Build and Play exist; Play builds
if necessary and keeps temporary retail ZIP extraction alive while the game runs.

### Important movement fidelity boundary

OG102 statically confirmed original `PlayerTest::move` at `0x3026b9`:
strength above `0.5 * animationSpeedMultiplier` selects steady run188; the lower
branch selects walk202. Ordinary unarmed steady playback uses rate1.0. From idle,
the original first requests transition179 with flags0x204 before steady run;
bit0x200 suppresses reselection during that transition. Armed/context/target
variants also differ. The current older production playback does not implement
that entire original state machine or exact per-frame root-motion/body pipeline.
The bug fix is not a claim that those remaining controller behaviors are complete.
OG102 is source/disassembly research, not a new original-execution proof.

## Final verification

On the combined final code after `ce3ca50`:

- Full Release and Debug x64 builds pass; **all55 CTests pass per configuration**.
- Game and new component tests are genuine **PE32+ AMD64** (`0x20b`, `0x8664`).
- Fresh original/native deformation comparison: all21 nested vehicle factories,
  **378 phases,742,608 used stream component words and378 runtime base entries**
  match per configuration. Both Player scenes and all23 loaded scenes construct.
- Fresh original factory checks confirm empty ordinary/bound animator lists in
  all23 scenes, even where unattached global animation definitions exist.
- Fresh original/native lists: **16 link/mutation/context/teardown traces,
  five selector pairs and two name/respawn/count traces** match per configuration.
  Native tests also exercise actual Box2D shape/contact destruction callbacks.
- Ten existing actual-asset scene/graph/pose/bounds/material/animation/body/host/
  finalizer executables pass in each configuration,20 runs in total.

Immediately before the final component integration, `ce3ca50` also passed all53
then-registered tests/configuration, complete retail checks and both Vulkan
opening smoke tests with isolated save folders. The55-test final suite includes
the movement/camera regressions. The smoke tests were not repeated after the
component-only integration; no production Vulkan changes were made there.
Final build logs contain zero compiler warnings.

Retail auditing retains **98/98 decoded images, zero opened-image decode
failures and the same111 missing referenced paths**. Audit exit1 for those absent
resources is expected evidence of the gap, not a passing full-asset-completeness
claim. Do not hide it with guessed aliases or fallbacks.

Root evidence is local and ignored under:

- `port/build/reviews/OG-101/`: pre-fix failures, full builds/tests, retail checks,
  Vulkan smoke logs, architecture and original-preservation evidence.
- `port/build/reviews/OG-092/`: final combined build/test logs, native dumps,
  freshly copied/replayed original descriptors, comparisons and legacy runs.
- `port/build/reviews/OG-097/`: fresh original list probe and comparison inputs.
- `port/build/reviews/OG-102/`: source report and deferred099 checkpoint copy.
- Earlier reviewed evidence remains under the corresponding `OG-*` directories.

These are reproducible research artifacts, not redistributable retail assets or
native runtime requirements. Reference scripts sometimes contain fixed output
paths: copy and rewrite into your own ignored directory before executing them.
Unicorn is reference-only; current root probes use the existing private Python
support under worker3's ignored `port/build/og015-tools` directory.

## Build, run and assets

From the repository root in PowerShell:

```powershell
cmake -S port -B port/build/x64 -A x64
cmake --build port/build/x64 --config Release --parallel 4
ctest --test-dir port/build/x64 -C Release --output-on-failure
cmake --build port/build/x64 --config Debug --parallel 4
ctest --test-dir port/build/x64 -C Debug --output-on-failure
python -m launcher
```

Installed tools used for validation: Visual Studio18/2026 C++ x64 toolchain,
CMake and Vulkan SDK1.4.363. The build has pinned native dependencies, including
historical Box2D2.0.1, GameSWF, zlib, libogg/libvorbis and the project's JPEG code.

The user supplied their own West Coast Hustle HD APK and HTC game-data ZIP.
Read the **gitignored** `.launcher-settings.json` for the actual local
`windows_assets`/`assets` paths; do not commit those assets. Native play uses the
ZIP/extracted game data, not the APK. The launcher handles extraction through
`launcher.game_apk.windows_game_data(Path(...))`; its context must outlive game
execution. The earlier BlueStacks `sdcard/gameloft/...` discussion is not the
native Windows asset-loading architecture.

CLI smoke syntax uses a positional extracted asset directory, **not `--assets`**:

```powershell
& .\port\build\x64\Release\gangstar-wch.exe 'C:\extracted\gangstar' --save-dir 'C:\temporary\fresh-saves' --opening-smoke-test
& .\port\build\x64\Release\city_collision_test.exe 'C:\extracted\gangstar'
& .\port\build\x64\Release\retail_actor_deformation_test.exe --assets 'C:\extracted\gangstar'
& .\port\build\x64\Release\retail_game_object_list_test.exe --dump
```

Use fresh temporary saves for smoke tests. Do not overwrite the user's saves.
LLVM is at `C:\Program Files\LLVM\bin`; when disassembling original Thumb code,
use `llvm-objdump --mcpu=arm1136jf-s` so BLX instructions decode correctly.

## Highest-priority unfinished work

The executable still contains older `city_*` playback/presentation approximations.
The substantial source-verified `retail_*` engine components are not yet a complete
canonical Player/game-object/world/VM/renderer runtime. Keep that distinction.

1. **OG040 / OG099: canonical entity and LevelObject lifecycle.** Compose one
   actual object with its real scene/body/name/category/flags/weapon/target and
   intrusive hook. Recover ctor/Load/enable/disable/Update/destructor ordering.
   Use the existing metatype catalogue and decoder, not a second raw parser,
   registry, scene proxy or copied entity state. Original LevelObject flags4 and
   Character flags240 are distinct words. Shared type/pool tables are global,
   not per-entity. Load preselects a list before linking; SetName does not
   automatically invalidate cached metatype. Source-only checkpoint:
   `../OpenGangstar-workers/worker-1/port/build/OG-099-checkpoint.md` and root102 copy.
   No099 production API or implementation was approved.
2. **Close actual destruction ownership.** OG096 uses a weak common target with
   explicit known-NULL versus expired-nonnull state and transient shared leases.
   OG097 requires synchronous genuine teardown at its noexcept destroy endpoint.
   Dropping a manager shared_ptr can defer effects and is insufficient. The
   canonical owner must preflight/reject unsafe active leases before list
   mutation. Do not invent a second alive flag/registry or silently defer teardown.
3. **OG100: real scene pools.** Root recovered17 original policy phases; see
   `port/build/reviews/OG-100/notes.md`, `probe.py`, `policy-results.json` and symbol
   windows. Full name/collision/factory/Graph ownership API remains unfinished;
   no pool product code was added. An original growth defect is proven: increasing
   a minimum after dynamic growth constructs from the old minimum rather than
   old live size, overwriting active identities without teardown. Choose and
   document a checked native adaptation rather than silently reproduce or repair
   invalid ownership. `update` only updates its countdown; do not invent a
   reclamation scheduler. Explicit reclaim uses unsigned ages strictly >60000 ms.
4. **Real outer updates and producers.** Bind original ordered Player/Character/
   NPC/weapon/world/VM operations, actual sound, target acquisition, camera,
   CombatIdle and Lock effects. Existing interfaces require genuine endpoints;
   recording callbacks does not implement those systems. OG005 production
   StopObject waiting remains blocked on this real ordered host.
5. **Production deformation/rendering.** New OG092 composes the genuine default
   deferred software path. Separate CarFactory eager/damage-weight production,
   hardware deformation, current GPU upload/draw and real actor/vehicle frame
   scheduling remain. General attached animator/timeline event dispatch is also
   incomplete. Do not declare DefaultFactory proof to be CarFactory/GPU proof.
6. **Full assets and remaining systems.** Resolve the111 missing referenced
   resources and remaining formats, including WAL/additional DDS coverage.
   Complete original gameplay, AI, scripts, UI, streaming, audio and save behavior
   systematically. Do not discover later assets through a manual playthrough.

## Ownership and fidelity rules already established

- One explicit `RetailActorLoadedResource` represents a physical load. Fresh
  instances share its current source arrays but have fresh mutable resources,
  materials, palettes and morph states. A second physical load is distinct even
  when immutable metadata is shared. No per-actor bind-pose copy fallback.
- Match actual Scene/resource addresses and immediate dependencies, not UID
  equality. Skin may depend on Morph, which depends on ordinary source/targets.
  Keep one canonical `RetailMorphState` and existing arithmetic kernels.
- Scene retains only a weak active-pipeline association. Retained views own their
  required storage/Scene; no reverse ownership cycle. Moved shadows retain their
  own resources/materials/load aliases, not their former actor.
- Ordinary and bound animator lists differ. Unattached global animation
  definitions do not imply an attached animator. Unsupported actual instances
  must still reject; do not bypass guards wholesale.
- Preserve exact float operation/order, cached matrices, dirty flags, byte36/42,
  alias timing, release order and observable repeated setters. Existing original
  comparisons intentionally cover signed zero, NaN and source quirks.
- Intrusive Update snapshots next before callbacks; ForEach rereads next after.
  Clear destroys still-linked objects, unlike unlink-first RemoveAndDestroy.
  Manager list selectors deliberately differ by method. FillList's original
  context is uninitialized; the native API requires an explicit initial context.
- Native PBM support contains a documented allocation/read-count safety repair
  for proven original overruns. Repaired behavior is not claimed as undefined
  original heap parity. Preserve that distinction.

## Original decompilation protection

`games/gangstar-wch` remains pinned to
`08e4fc11d8457ac40c502f3b0b2e966a20247293` from the separate decompilation repo.
All2969 original baseline file sizes/SHA256 matched again after the final
component integration; see `port/build/reviews/handoff-final/verification.json`. Original ELF SHA256 is
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Earlier original-project83 Python checks/repository/progress gates passed; no
fresh full ARM matching rebuild is claimed by the final native integrations.

These pre-existing user changes belong to the user; preserve them:

```text
 M docs/COMMAND-BUILD-PLAN.md
 M tools/build.py
?? config/linker/Aggregate.ld
?? tools/extract_retail_data.py
?? tools/extract_retail_text.py
?? tools/link_retail_image.py
```

The migration snapshot is local at `%TEMP%\opengangstar-migration-baseline.json`.
Do not reset this submodule, change ARM layouts/toolchain flags for x64, or delete
apparently missing long assembly paths. Keep `core.longpaths=true`.

## Worker and workspace state

All three available workers stopped. The requested four-worker configuration
exceeded this runtime's four-agent total limit including the orchestrator; that
capacity blocker is recorded. Reassess Claude's actual capacity rather than
claiming four active workers.

Adjacent isolated worktrees remain at `../OpenGangstar-workers/worker-1`,
`worker-2`, `worker-3`. Worker2/3 retain frozen uncommitted source copies and
private CMake wiring against older baselines; their patches are already in
`42b2861`. Preserve their ignored reference evidence. Worker1 has clean tracked
state and saved099/102 research. No submission is waiting for review or merge.

If the user resumes work, follow `port/WORK_SCHEDULING.md`: dynamic priorities,
latest verified assignment baselines, exclusive writable resources, isolated
worktrees, root-only integration, independently reviewed submissions and a
persistent queue. Do not permanently assign workers by subsystem.

On Windows, verify absolute targets before recursive filesystem operations and
use one shell end-to-end. Never use broad reset/cleanup on these worktrees.
Avoid `copy2` for source/dependency transfers: preserved old timestamps previously
caused false incremental MSBuild passes. Use `write_bytes`, `copyfile` or Git
patches and ensure affected sources actually rebuild.

Suggested first Claude action: inspect `git status`, `git log`, the queue and this
handoff, then obtain the user's next direction. Do not treat these completed
component milestones as evidence that the complete native port is finished.
