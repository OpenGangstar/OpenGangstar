# Original animation clock and sampled root displacement

OG-018 adds a native animation prerequisite against baseline `141928a`. It does
not replace the production movement controller, physics synchronization, renderer
animation path, or cinematic command scheduler. See `ACTOR_MOVEMENT.md` for those
dependencies. No gameplay progression or manual playthrough is required or used.

The reference is the pinned retail `libsanangeles.so`, SHA-256
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Function addresses below retain the Thumb bit; instruction addresses do not.

## Original evidence and implemented behavior

| Function | Address | Relevant behavior |
| --- | --- | --- |
| `Character::setAnimation` | `0x002f1d91` | Guards selection, stores clip interval/rate/options, clears the cursor on accepted changes. |
| `Character::isAnimationLastFrame` | `0x002f15bd` | Uses truncated integer cursors and strict remainder comparison. |
| `Character::updateAnimation` | `0x002f3489` | Advances its own clock, samples root translation, updates the cache, writes body velocity when enabled, and removes visual root translation. |
| `Character::bindAnimation` | `0x002f23fd` | Constructs the animator, binds the supplied root UID, and does not initialize the previous-root cache. |
| `Character::canAnimationBeInterrupted` | `0x002f148d` | Protects IDs 1 through 23, 86, 164 and 175. |
| `Character::Character` | `0x002f3231` | Initializes root cache XYZ to zero at `0x002f3298..29c`. |
| `CTimelineController::getClipLength` | `0x002b2875` | Returns end minus start. |
| `CAnimationTrack::applyValue(int,void*,bool)` | `0x0028dc6d` | Searches integer key times, selects an exact key directly, otherwise clamps its interpolation weight. |
| `CAnimationTrack::getValue(int,void*,bool)` | `0x0028e031` | Has the corresponding value-sampling search and exact-key path. |
| `CPositionEx::getKeyBasedValueEx` | `0x0028f691` | Interpolates position through the original vector lerp. |
| `core::lerp<vector3d<float>>` | `0x00243dfd` | Evaluates `first + (second - first) * weight` using float operations. |

`RetailAnimationClock` contains native typed state and no ARM layout views.
`selectRetailAnimation` preserves the original selection order:

- Movement flag `0x800` rejects the request before clearing or repeated-ID checks.
- ID `-1` clears clip interval, cursor, rate and options. It preserves the root
  cache, retained height and deferred ID.
- A request for the current ID returns without changing phase, speed, options,
  cache, or deferred animation.
- An unfinished protected nonlooping animation rejects an interruptible
  request, or defers another protected request by ID. An active cinematic permits
  the transition. The original base `setNextAnimation` ignores the other fields.
- Accepted changes clear the deferred ID and floating cursor, store the supplied
  original clip start/end, multiply requested speed by the integer speed
  multiplier, and retain the root cache and height.
- Accepted transitions away from ID 67 with weapon type 7 return a
  `reloadRocket` intent. The controller must perform that weapon operation.
  Selecting ID 164 sets the literal flag `0x80000000`, verified at
  `0x002f1f18`; this must not be confused with `0x8000`.

The original rocket reload runs before committing accepted clock state. Returning
an intent does not establish complete callback-order parity. A future host must
perform that mandatory operation with the original live actor state before the
accepted clock commit, preserving any live flag/cache effects. Selection can be
previewed on copied state, but the host must then commit only the fields justified
by the original order; it must not blindly overwrite the live actor or flags from
a preview, or mutate the live clock and perform the reload afterward. Matched
`reloadRocket` has no direct Character clock/flag access, but indirect object-load
effects remain unverified. This requirement belongs to future host integration.

The default native clock represents the cleared state after the original spawn
selection of ID `-1`. This is not a claim that every constructor field was already
initialized before `OnSpawn` in the original game.

## Clock, boundaries, and cache

`advanceRetailAnimation` advances `cursor += elapsed * rate` with float storage.
Sampling uses the truncated integer cursor modulo the clip length, then adds the
original start time. It does not round or sample an unrestricted floating frame.
An unbound root or cleared animation returns the original completed result and
does not advance the cursor.

For a nonlooping clip, last-frame detection is `int(cursor + advance) >= length`.
For a loop, it is strictly `int(oldCursor) % length > int(newCursor) % length`.
An exact whole-cycle delta therefore does not count a wrap. A large delta may
detect one tail/head transition or none, depending on the two remainders; it does
not multiply displacement by the number of elapsed cycles. This preserves the
original behavior, including its large-delta limitation.

When last-frame detection fires, the original first samples `start + length - 1`
and adds the difference from the previous root cache. A looping clip then samples
the start, replaces the cache with that start position, and adds the new head.
A nonlooping clip holds the end-minus-one sample while its floating cursor keeps
advancing. Each segment is scaled by the original `0.001f` before summation.

The first tail difference is assigned directly at `0x002f36e4/36f0/3700`, whereas
the ordinary path initializes positive zero at `0x002f34d8..de` before adding its
sampled difference. A loop retains its assigned tail while adding the head.
These operations are not interchangeable for signed zero. With an identity actor
matrix in the matrix-driven branch, a nonlooping all-axis negative-zero end sample
minus a positive-zero cache produces negative-zero body velocity, while ordinary
sampling's initial positive-zero addition produces positive zero. Native regression
tests check both paths and the loop tail/head
path by sign bits, not approximate numeric equality.

The first sample subtracts the constructor's zero cache. Neither binding nor an
accepted animation change establishes a general cache reset. Zero advance still
samples, updates root cache/height, and strips visual root translation; it does
not write body velocity. Root-motion-disabled animation also continues to sample
and update those values.

## Root sampler and transform contract

The shared primitive consumes the already decoded `CityAnimationChannel` for the
bound root's local translation. It does not implement a second skeleton or
quaternion sampler. The caller resolves the actor's supplied root UID; it must
not assume a node index. NPC setup's verified UID is `Bip01-node`.

Original search at `0x0028dc9c..ae` selects the greatest key time not exceeding the
query. Equality at `0x0028dcb2..b4` takes a raw-key copy through
`0x0028dcc0..cc`. The native primitive also copies exact keys directly, preserving
signed zero and avoiding cancellation from an unnecessary lerp at weight zero or
one. Queries before the first key use the original clamped-zero lerp; the final
key is held. Selector zero holds the previous key; the evidenced nonzero linear
selector uses float lerp.

Native decoded channels reject duplicate/nonincreasing times, so this primitive
explicitly retains that input contract. The original search itself selects the
last equal key for duplicate exact times; supporting such source data requires a
coordinated decoder change. It is not silently accepted with different behavior.

For the supplied `anim_ingame.bdae`, independent enumeration found 213 clips and
one `Bip01-node` translation track with 3,295 keys and interpolation selector 1.
All 213 root intervals retain the keys needed at their boundaries. Existing clip
decoding can therefore be reused for this root catalog. This result does not
prove that clipped channels from another asset retain outside boundary neighbors
or the same interpolation selector; callers must establish those properties.

For ordinary root motion the original transforms scaled local displacement with
the actor's absolute matrix, using elements 0/4/8 for X and 1/5/9 for Y, then
divides by `elapsed * 0.0001f`. Matrix translation is excluded. This corresponds
to the original centimetre/body-metre velocity convention without adding the
actor position to a displacement. The body wake intent tests local XY before
the matrix transform, as the original does; root Z can still contribute to
transformed XY. The separate directional option uses the sampled displacement
magnitude and the supplied original stored direction. It never substitutes an
average clip speed.

The result's absent `bodyVelocity` means preserve existing velocity. A present
zero velocity means write zero. The result also returns zero visual root
translation and retains sampled root Z in the clock. The caller must apply the
zero translation to the animated root and consume body/weapon intents. Applying
the sampled root translation again in the rendered skeleton would move it twice.

## Validation and limits

Release and Debug x64 harnesses use `/fp:strict` and the isolated worktree's
matching configuration libraries. Native tests cover interruption/defer/reset,
phase retention, first cache, exact and multiple-cycle deltas, nonloop boundaries,
zero/fractional advance, STEP/linear sampling, exact-key signed zero, duplicate
rejection, matrix scale/rotation/translation exclusion, directional motion, body
absence, local body-wake intent, and explicit timeline-domain failures.

Both configurations validate all 213 retail roots and 444 boundary/profile/clock
samples. Independent raw-track constants cover IDs 188 (`move_run`), 202
(`move_walk`) and 212 (`move_walk_to_idle`) at offsets 0, 1, 100 and end-minus-one.
The walk/run IDs are fixtures, not a replacement for `getSpecificAnimation`.

An ignored local ARM execution harness runs the original `updateAnimation` and
`isAnimationLastFrame` instructions across 560 cases. Last-frame results, cursor,
cache XYZ, retained height, body-write intent, XY velocity and wake intent match
native outputs bit for bit. Explicit hooks provide scene samples, matrix/body
objects, and IEEE soft-float helpers; this does not execute the original renderer,
physics world or complete scene animator. A further 72 cases execute original
get/apply-value searches and Position/vector-lerp bodies, comparing exact keys,
clamps, STEP/linear intervals, signed zero and large cancellation bit for bit.
The emulator and retail image are validation-only and are not project runtime
dependencies or committed assets.

An additional 840 cases execute the original `setAnimation`, interruption
predicate and base deferred-ID setter. Selection return/state, flags, rate,
options, cursor, deferred ID, cache/height retention and reload intent match the
native implementation bit for bit. Hooks provide timeline metadata, cinematic
state and weapon operations; they do not port those external systems. This
comparison caught and corrected an initially inverted protected-request branch
before submission.

The signed-zero regression is additionally compared to the original ARM for the
ordinary, nonloop-tail and loop-tail/head paths. Release and Debug each match all
three velocity sign-bit pairs: ordinary positive zero, then negative zero for
both tail cases. The earlier submission failed the two tail cases because it
added the first difference to positive zero. The revision assigns that tail
directly and retains addition only where the original adds.

Reverse/negative-rate playback, negative elapsed/cursor values, nonfinite clocks,
int32 conversion overflow and velocity-denominator underflow are explicitly
unsupported in this prerequisite instead of being silently clamped. Full actor
binding, clip selection by weapon/context, material/skeleton animation,
`switchAnimation`, weapon intent consumption, body collision/height handling,
physics stepping, player reset, and production controller integration remain
separate work. No movement-readiness or complete native movement claim follows
from these clock tests.
