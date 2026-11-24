# Native fixed-step world update

OG034 implements the physics leaf of `GS3DStuff::update(StateMachine*)`, original0x002e078d, on the caller's genuine shared native Box2D2.0.1 world. It does not implement the complete original world update, construct a parallel world, replace contact callbacks, or infer missing vehicle behavior. The application owns one `RetailWorldStepState` and one `RetailWorldUpdate` binding for that shared world. Bodies, filters and listeners installed by the existing terrain/actor/contact components participate in the real Step.

## Original evidence

The original library SHA256 is `b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`. The checked-in disassembly is `games/gangstar-wch/asm/rendering/_ZN9GS3DStuff6updateEP12StateMachine.s`; application construction is `asm/app/_ZN11ApplicationC1Ev.s`.

| Original addresses | Native operation |
| --- | --- |
| 0x2d5220..0x2d5226,0x2d5260 | Application accumulator+96 and signed step counter+100 initialize to zero |
| 0x2e07da..0x2e07f4 | Read current device timer milliseconds and form a32-bit subtraction from GS3DStuff::lastTime |
| 0x2e08a8..0x2e08c4,0x2e0b58 | Interpret subtraction's signed bits; negative becomes0, values above249 become250 |
| 0x2e08c4..0x2e08dc | Convert signed clamp to float, DIVIDE by float1000, add to accumulator, reset counter to0 |
| 0x2e08de..0x2e0910 | Update every current eligible vehicle once with the original UNCLAMPED elapsed bits |
| 0x2e0910..0x2e091a | Enter when accumulator >= exact float0x3c888889 |
| 0x2e0920..0x2e0952 | Live indexed vehicle UpdatePhysic(float0x3c888889) pass |
| 0x2e0954..0x2e095a | Genuine shared b2World::Step(float0x3c888889,10) |
| 0x2e095e..0x2e098c | Live indexed Vehicle::stepPhysic() pass |
| 0x2e098e..0x2e0998 | Subtract the same binary32 step from the CURRENT accumulator after all post callbacks |
| 0x2e099a..0x2e09ac | Counter i2f, float-add1, f2iz, then store; this is a per-frame step count |
| 0x2e09ae..0x2e09b6 | Repeat inclusive >= comparison with the subtracted float value |
| 0x2e0a82..0x2e0a8c | Commit current timer to lastTime later, after additional original frame systems |

The literal pool at0x2e0b00/04/08 contains float1000,0x3c888889 and float1. Do not substitute double arithmetic, a multiplied reciprocal, floor(total/dt), a strict comparison or a reconstructed total-minus-stepCount formula. From zero, original50ms takes TWO steps and leaves0x3c888887; original250ms takes FIFTEEN and leaves0x32400000. The near-threshold residue is observable across later frames. The original signed clamp is distinct from the raw elapsed argument used by vehicles and other systems;0x80000000 and0xffffffff contribute zero physics time but remain those bit patterns in Update(int).

## Clock and host integration contract

The existing Windows runtime already reads `std::chrono::steady_clock` in `vulkan.cpp`. There is no existing native shared-world accumulator to reuse; the animation clock represents another timeline and must not become this clock. The new physics leaf accepts raw uint32 elapsed milliseconds and creates no additional wall-clock timer or competing last-tick state. The actor host should obtain integer milliseconds from its existing single clock epoch, subtract uint32 current-minus-previous modulo2^32 and pass that value here. Truncating every floating frame delta independently loses fractional time and does not reproduce an integer device clock. A wrapped previous0xfffffff8/current9 yields17ms and one step.

Original GS3DStuff::wakeup/resume reset lastTime from the device timer; they do not clear application accumulator/counter. The host must preserve this shared state across pause/resume and reset only its appropriate timestamp. This leaf does not own/commit lastTime: the complete original update commits it at0x2e0a8c, outside the leaf. Frame work before/after this boundary must use the host's single captured elapsed/timestamp, rather than advance animation or physics with separately sampled clocks.

Original actor updates, including Character animation/body velocity, precede the leaf. Original player/actor scene synchronization, camera/trails and other world work follow it. Those are mandatory future controller/host operations, not empty substitutes installed here. A direct on-foot host can compose its genuine body/height/animation components before/after this leaf, but that does not establish complete GS3DStuff::update integration.

`RetailWorldVehicles` is a required host interface. It represents the actual eligible vehicle vector prepared by the preceding original object/despawn pass, preserving order and identities. It must resolve the current indexed vehicle on each operation and implement actual Update(int), UpdatePhysic(float) and stepPhysic() behavior. It must not map present but unported vehicles to an empty vector or no-op callbacks. A genuinely empty eligible collection is valid and still steps the shared world. Native pointers/indices retain their full width; no ARM pointer truncation or copied vector snapshot is used.

The original reloads vector begin/end after EVERY callback and reads it again between phases. Appends may join the current pass. Removing the current entry advances the numeric index and can skip the shifted successor. A contact during the real Step can replace the set seen by the post pass. The native implementation deliberately reproduces those behaviors. During frame callbacks the updated accumulator and counter0 are already visible; during each complete pre/Step/post cycle the preceding count remains visible. Only then are subtraction and increment committed.

The world/state/host records and bodies must have valid shared lifetimes. Queries are serial. Callbacks must not throw, reenter this leaf, destroy the bound world/state or write the step state. Original indirect callback mutation of application accumulator/counter is not established by these tests and is outside this native host contract. Normal native code returns a finite remainder smaller than one step. Nonfinite imported state is rejected before callbacks. Finite extreme state where float subtraction cannot decrease the value produces an explicit error after the first genuine Step/post pass, rather than silently hanging or inventing a time/counter. These are native unsupported-input boundaries; they are not original NaN/stack/nontermination parity claims. After a callback/physics error, partially updated physics/state is not a resumable transaction.

## Validation

The native test contains33 literal original-execution bit fixtures covering0/1/16/17/33/50/249/250/251/1000ms, signed extremes, step-predecessor/exact/successor values and negative zero. It observes all vehicle arguments, order and state visibility. A deterministic1024-frame sequence checks persistent remainder range; the separate original execution oracle compares each resulting accumulator/counter exactly. Original fixture capture uses the original0x2e08a8..0x2e09b8 instruction window, including the remote0x2e0b58 signed-clamp branch, and intercepts external AEABI, vehicle and Box2D calls. Step interception verifies the exact world identity, dt bits and10 iterations. The end-PC is required; exhausting an instruction budget is not counted as success.

Independent native physics fixtures step a real shared world with colliding dynamic bodies, gravity, damping and angular velocity and compare bitwise with direct Box2D Step(dt,10) calls. Another real contact listener mutates the eligible vector during Step; the post pass must see the replacement before counter commit. Native allocation accounting returns to baseline. Additional original probes confirm append/removal during frame/pre/post passes and replacement during Step. Native explicit nonfinite/nonprogress error fixtures document the bounded safety behavior. No source-string test, manual gameplay or mission completion is used as evidence.

The full native Windows Release/Debug builds and CTest gates are recorded in the OG034 submission report. The independent oracle proves this timing/callback dispatch slice, not original vehicle simulation, Android event machinery, every indirect callback side effect, complete actor lifecycle or production game-loop integration. Real clip or asset enumeration is not relevant to this scalar physics leaf; animation asset validation remains with its original component.
