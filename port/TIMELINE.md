# Original mutable timeline

`RetailTimeline` implements the original `CTimelineController`, independently of
Character's animation clock and the full-pose sampler. Each original factory
construction needs its own mutable timeline. The immutable clip vector may be
shared, including an aliasing `shared_ptr` into a retained
`RetailActorAnimationCatalog::clips()`. No parser, sampler, actor clock or
animator attachment is added here.

The vector must remain immutable, retain authoritative `CityAnimationSource`
records, and preserve source order. Exact case-sensitive lookup returns the
first matching name. `durationMs` and clipped channels are not timeline bounds.
Zero-span, negative and reversed source ranges are accepted; this component
does not inherit the actor catalogue's positive-duration requirement.

## Original evidence

The matching `libsanangeles.so` SHA256 is
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Thumb function addresses:

| Operation | Address | Preserved behavior |
| --- | --- | --- |
| Constructor | `0x2b2a5d` | Integer time and floating current/start/span zero; scale one; loop, completion and previous-anchor flag false; no clip library. |
| `init` | `0x2b2979` | Changes only integer start/end; does not refresh cached floats or reset time/flags. |
| `setRange` | `0x2b2981` | Without a library, assigns integer bounds and floating start/span; with a library ignores arguments. Both jump to the stored integer start. |
| `jumpTo` | `0x2b292d` | Assigns integer/float current, clears completion and previous-anchor flag, preserves stored prior input. |
| Indexed `setClip` | `0x2b29c1` | Clears completion, selects source metadata, assigns bounds/cached floats/current; preserves the previous-input anchor and flag. |
| Named `setClip` | `0x2b2951` | Exact source-order lookup; missing name returns `-1` without changing state. |
| Count/index/name | `0x2b2849`, `0x2b2a21`, `0x2b28c5` | Actual indexed library data, without case folding. |
| Start/end/length | `0x2b2855`, `0x2b2865`, `0x2b2875` | Original signed endpoints; signed32 bit-pattern subtraction for length. |
| Current clip getters | `0x2b2895`–`0x2b28d5` | Selected source record, independently of integer bounds changed by `init`. |
| Loop/scale | `0x2b2839`–`0x2b2845` | Direct state access; setting scale does not prevalidate or advance. |
| `update` | `0x2b2ad9` | Signed input, literal float arithmetic and callback-before-final-integer-store ordering. |

The actual default factory `0x29d395`, reached by const database
`constructAnimator` at `0x297c51`, creates the original plain animator plus a
fresh timeline, attaches the original 213-entry library, and selects clip zero.
The library-backed native constructor performs that timeline composition.
Character later detaches the timeline before binding its plain animator; its
first shared timeline retention is a separate scene-host integration task.

## Arithmetic and callbacks

Inputs and endpoints convert from signed32 to float, then divide by `1000.f`.
The first update adds zero and anchors the input; subsequent updates subtract
the previous float input, multiply by the live scale, and add to float current.
The prior input is stored before checking completion. For example inputs
`12345,12445,12545` produce integer times `0,99,199`, not `0,100,200`.

Crossing is strictly `endSeconds < currentSeconds`. Equality does not complete.
There is no lower clamp. A one-shot clamps to the end and notifies once. A loop
uses `startSeconds + fmodf(currentSeconds-endSeconds, spanSeconds)`; the
zero-span branch uses zero remainder and still notifies on each crossing. The
span is a signed32 subtraction **before** float conversion, including wrapped
bit patterns. Negative spans are not reordered. Build this source with strict
floating-point rules, as the physics target does.

A callback receives the actual timeline and borrowed full-width userdata. NULL
is valid. The callback runs after the float current/completion/input anchor have
changed but before the final integer time store; it can read the old integer
time. Legal callback calls to `jumpTo`, `setRange`, `setClip`, loop, scale and
callback replacement remain live. The final conversion rereads float current;
no preview state is copied back. Clip changes preserve the input anchor, while
jump/range changes clear it. The next frame therefore depends on which actual
operation the callback performed.

## Native lifetime and checked domains

The raw original constructor leaves integer range, previous-input storage and
userdata unspecified. Native storage is initialized, but `update` and
library-backed operations reject unconfigured/missing backing. Configuring
`init` alone intentionally keeps the original zero cached float state.
Unused previous-input storage cannot affect a first update because its flag is
false. Invalid indices, missing source records, embedded-NUL names, and an
empty library-backed factory are explicit setup errors rather than unchecked
ARM memory accesses.

The callback and userdata must remain valid through dispatch; destruction of
the timeline while its update is active is forbidden. Recursive `update` is
explicitly rejected before changing the nested frame. Setters are allowed.
An exception escaping a callback, or an invalid final floating conversion, is
a failed host operation after possible state changes; no rollback or safe
resumption guarantee is made. Finite in-range float milliseconds truncate
toward zero. NaN/infinite/out-of-signed32 final values fail explicitly rather
than invoking a native C++ conversion outside its domain. Checks occur after
the original clamp and callback: an infinite intermediate repaired by the
nonloop clamp is valid.

The retained backing may outlive the catalogue handle without copying clips.
Each timeline owns only that backing and its own state. Callback userdata is
borrowed and creates no implicit ownership cycle. There are no raw ARM layouts,
narrow pointer casts or source resource reference-count overlays.

## Validation and integration boundary

The test uses public operations and subsequent updates, not raw-state access or
test friends. Persistent expectations were captured from actual original ARM
instructions, including reversed ranges, float rounding, equality, negative
time/scale, zero-span manual and library ranges, init-only cached floats,
wrapped length, completion counts and live callback changes. It also checks
invalid domains, callback failure/reentry and alias-owner retention.

`retail_timeline_test --assets <resource-directory>` reads the existing decoded
catalogue and emits all 213 indexed/name/start/end/length results plus public
state traces. Research-only original execution compares those records with
actual original getters and independent raw BRES records. The original factory
probe executes real animation post-load fixups and const/default factory
construction; file manager/allocation/soft-float math remain explicit host
interception boundaries. The reference interpreter is not a runtime dependency.

This implements the mutable controller. It does not attach a timeline to the
retained native plain animator, replace Character's detached clock, install
the shared first-timeline owner, or complete the concrete Character animation
scene host. Those operations must preserve the independently proved original
construct/retain/detach/bind/drop order.
