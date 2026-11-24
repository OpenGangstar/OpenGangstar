# Native port worker scheduling

The orchestrator maintains `WORK_QUEUE.json` as the assignment and reservation
registry. Each task records its baseline, implementation scope, symbols, exclusive
writable resources, priority, complexity, dependencies, assignee, validation, and
status. Workers are reusable execution slots, not permanent subsystem owners.

The requested configuration is four workers and one orchestrator. The current
runtime permits four agents in total, so only three workers can run alongside the
orchestrator. The fourth requested slot remains explicitly blocked by that limit.
Do not claim that four workers are running or create overlapping assignments to
fill capacity. Reassess the limit when resuming in another runtime.

Workers use separate Git worktrees from the latest verified integration baseline.
Only the orchestrator commits, integrates, pushes, or creates pull requests. The
original matching decompilation and retail assets are read-only references for
these native-port assignments. Never commit retail binaries, assets, or tools.

Reserve writable resources before assigning work. Shared headers, structures,
interfaces, and build configuration have exclusive ownership. Workers must request
additional reservations before extending their scope. The orchestrator must not
edit a reserved resource while its worker owns it.

Use the task states AVAILABLE, RESERVED, IN_PROGRESS, BLOCKED, AWAITING_REVIEW,
NEEDS_REVISION, VERIFIED, and MERGED. A worker reports blockers immediately and
submits its task identifier, modified functions and files, original-game evidence,
decisions, build and test results, limitations, and a complete reviewable patch.
Unsupported behavior remains documented rather than replaced by fabricated logic
or silent no-ops.

Review submissions as they arrive, without waiting for a batch. Check intervening
integration changes, original behavior, x86-64 and Vulkan compatibility, duplicate
implementations, and regressions. Return failed verification as NEEDS_REVISION.
Mark VERIFIED only after independent checks pass. Mark MERGED and release its
reservations only after integration and relevant regression checks. Then assign
the highest-priority independent available task from the new verified baseline.

Keep completed records and dependency evidence so another session can resume
without repeating work. A successful build or an individual milestone does not
complete the native-port goal. Validation uses automated checks and short startup
smoke tests, never manual mission progression.
