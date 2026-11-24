# Original quaternion and typed animation pose sampling

OG047 ports the original quaternion interpolation and typed translation, Z-only
translation, rotation and scale channel operations. It reuses the existing decoded
channels, clip catalogue, root sampler and animation clock. The native renderer and
preview sampling path are unchanged. This is an animation prerequisite for a real
owning actor scene, not a complete skin renderer or production animator host.

## Source contracts

The reference is libsanangeles.so SHA256
b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a.
Addresses retain the Thumb bit.

| Original symbol | Address | Implemented contract |
| --- | --- | --- |
| quaternion::slerp(quaternion,quaternion,float) | 0x00221e15 | Literal dot/sign/threshold/weight/normalization order |
| quaternion::normalize | 0x001c8d11 | Exact-one shortcut and float sum/sqrt/reciprocal |
| CAnimationTrack::getValue(int,void*,bool) | 0x0028e031 | Shared bracket search, raw exact/last/step branches, clamped weight |
| CAnimationTrack::applyValue(int,void*,bool) | 0x0028dc6d | Corresponding typed direct application |
| CQuaternionEx::getKeyBasedValueEx(two keys) | 0x0028fce9 | Original spherical quaternion interpolation |
| CQuaternionEx::getKeyBasedValueEx(key,prior,factor) | 0x0028fc65 | Same original slerp operation on prior/key values |
| CQuaternion::getKeyBasedValue(exact key) | 0x002920e5 | Raw four-word copy |
| CPositionEx / CPositionZEx / CVector3dEx | 0x0028f691 / 0x0028f625 / 0x002904a5 | Float vector/component interpolation |
| Character::updateAnimation | 0x002f3489 | Existing selected sample clock; optional whole-pose evaluation seam |

## Quaternion arithmetic

retailAnimationSlerp computes the original left-associated float dot product.
A negative dot flips the **first** quaternion and negates the dot. This retains the
original representative's component signs; changing the second quaternion instead
can give the same rotation with different returned words.

The original predicates are angle+1 > 0.05 and 1-angle >= 0.05, in that order.
The spherical branch uses float acos/sin and inverse-sine weights, with no final
normalization. The near-linear branch uses first*(1-t)+second*t and normalizes
that weighted result. It does not use first+(second-first)*t. Raw nonunit inputs
are not normalized at entry. The literal perpendicular/opposite branch is also
retained; finite large components can produce an unordered intermediate dot and
reach it. No identity fallback or dot clamp was added.

The normalizer moved unchanged into the assets animation layer. The existing public
retailSceneNormalizeQuaternion remains a wrapper, preserving its public API and all
scene/yaw/heading/jump callers. This avoids an assets-to-physics dependency or a
second authoritative normalization implementation. Raw authored SNode rotations
remain unnormalized by scene construction.

The native checked input contract rejects nonfinite quaternion source components
and interpolation factors. Arithmetic can still yield the original nonfinite
result, such as normalization of zero; it is not repaired to identity. No Android
libm implementation or arbitrary NaN payload equivalence is claimed.

## Typed channels and bracketing

sampleRetailAnimationChannel consumes the existing CityAnimationChannel and returns
its addressed components. Translation and scale use XYZ, rotation XYZW, Z-only
translation uses returned component0. applyRetailAnimationChannel writes only the
corresponding RetailAnimationPoseTransform field; Z-only translation changes only
local Z. This TRS value has no node identity or scene ownership.

The root sampler delegates to the same typed path, so its bracket and float lerp
behavior are not independently duplicated. Exact keys, a single key, the last key
and step interpolation copy authored words. Before the first key, linear evaluation
still executes interpolation with clamped-zero weight. Consequently an exact
nonunit quaternion key remains nonunit, while a before-first near-linear quaternion
sample may normalize. Replacing every clamped endpoint with a raw key copy would
change original behavior and signed-zero results.

The existing decoder contract remains: finite values, strictly increasing key times
in the supported int32 range, no duplicate timestamps. Interpolation is explicitly
step or original nonzero interpolation, represented by the existing linear enum;
rotation uses slerp in that nonzero case. Unknown native transforms/selectors,
empty channels, duplicate times and nonfinite addressed keys raise errors.
Inherited wide integer difference arithmetic avoids native C++ overflow; parity is
not claimed for original signed-timeline subtraction overflow or malformed inputs.

The caller supplies the channel's actual relative origin and original interpolation
policy. The decoder's clip filter can discard outside boundary neighbors in another
asset. No nearest retained key is claimed equivalent to a missing original bracket.
Unsupported original channel classes such as X/Y-only or quaternion-angle tracks
are not added to the decoder by this patch.

## Actual source coverage

Independent raw enumeration of anim_ingame.bdae found 24 source tracks across 23
UIDs: one translation, one Z-only translation and 22 quaternion tracks. Every sampler
selector is 1. All 24 retain the required start/tail keys in all 213 decoded clips:
5,112 clip/channel pairs, zero missing-boundary or empty pairs. Native tests exercise
20,448 endpoint/interior samples across those pairs. This is a supplied-resource
proof, not a general guarantee for every clipped animation library.

The executable-original comparison uses independent raw ZIP values and signed
source clip starts for all 24 tracks, rather than feeding decoded native values back
as the retail reference. Six samples per track in the original walking interval
produce 144 exact output-bit comparisons per configuration. Separate synthetic
inputs cover step/before-first/exact/between/last/after-last behavior and signed-zero,
nonunit, negative-dot, threshold-neighbor and large-intermediate quaternion cases.

## Whole-pose clock seam

RetailAnimationPoseEvaluator::evaluate(relativeMs,absoluteMs) is an optional genuine
live-pose operation. A real owner applies all bound channels at that selected time
and returns its actual local root translation. It owns no second clock. The existing
advance and RetailActorAnimationBinding::advance accept a trailing defaulted pointer;
existing callers retain the source-channel path and behavior.

When an evaluator is supplied, the live floating cursor advances before callbacks,
as original store0x002f34ce. Ordinary sampling calls it once. A nonlooping end calls
the end-minus-one sample. A looping wrap calls end-minus-one, start, then head in
that order; tail/start callbacks see the previous cache and head sees the newly
stored start cache. Zero advance still evaluates. An unbound/cleared animation
performs no callback. The evaluator's live root result drives displacement, cache
and retained root height, rather than being overwritten by an independent root
sample from the immutable source channel.

The evaluated local root remains sampled when advance returns. The caller consumes
body velocity/wake intents and then visualRootTranslation through the real local
setter, preserving the existing controller boundary. There is no premature root
clear inside the sampler and no empty callback. This is not a complete port of
arbitrary body-side callback/cache ordering; the clock API already represents body
operations as intents. Full actor composition must perform real effects and retain
that documented boundary.

Evaluator callbacks must perform pose evaluation/readback, not replace the active
clock, flags or selected channel while advance is running. Failure propagates. It
is explicitly not transactional: cursor may be advanced and a loop's start cache
may already be stored; rootHeight/final root setter can remain uncommitted. Callers
must not resume after an exception under an assumed rollback. Native tests preserve
these partial-state semantics without pretending that original ARM callbacks throw.

## Verification and remaining work

Release and Debug builds pass the existing clock, selection, binding, actor control,
scene, heading and jump tests, plus the new pose target. Direct original execution
compares 91 quaternion cases and 80 synthetic channel cases per configuration with
zero differences. All 144 raw-source track comparisons also match exactly. Four
focused original Character captures establish selected callback times, visible
cursor/cache state and the final zero-root setter. Native tests cover that ordering,
including disabled motion, nonloop end, ordinary and zero-advance sampling.

The reference tools model AEABI float operations/conversions/comparisons and imported
float acos/sin/sqrt using host math followed by float rounding. Actual original
search, exact-copy, typed interpolation, slerp, normalize and Character control-flow
bodies execute. No animator constructor, Android libm implementation, skin vertex
loop, scene ownership or renderer execution is established by those modeled calls.
All emulator tools, logs and retail bytes remain ignored research artifacts; native
build/runtime has no ARM or Python dependency.

Mutable actor nodes, target ownership/invalidation, bound animator retention,
full skin matrix/vertex processing, visibility dispatch and production actor host
integration remain separate work. OG045's earlier phrase "visible mesh" is qualified:
FindAMesh0x002dc175 does not filter visibility/options; it tests tags, Plane/Jump UID
exclusions and ordered traversal. The skin multiplication helper also remains
separate from shared affine scene multiplication until its literal source contract
is independently verified. This patch changes neither path.
