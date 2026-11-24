# Retained original plain actor animator

`RetailActorPose` consumes the immutable full source library and the genuine
`RetailActorScene` owner. It implements the original default Player plain
animator and `RetailAnimationPoseEvaluator`; it is not AnimatorSet, a second
clock, a renderer pose approximation or a complete Character scene host.

Create a shared pose with `RetailActorPose::create(library)`, then call
`bind(sharedScene)`. The actual target nodes retain that same pose through the
scene's typed animation references. The pose holds a weak scene owner, so there
is no scene->node->pose->scene ownership cycle. Retaining the pose after scene
destruction cannot make its old pointers usable: evaluation locks the genuine
owner before any root or pose dereference and fails explicitly if it expired.
This weak owner is a native lifetime adaptation, not an original ARM layout.
Cross-root rebinding requires both contexts to remain live during the transfer.
After the current owner expires, create a fresh pose rather than rebinding an
expired root identity that could alias recycled native memory. That boundary is
an explicit error; no stale Bip01 pointer or false same-root success is allowed.

## Binding and callback semantics

Original onBind0x2b3bc1 compares the root field before doing anything. Binding
the same root is a no-op, including after a target unbind. Binding another root
stores that root and force-binds every source slot in order. It does not remove
old node registrations. `forceBind()` explicitly repairs slots against the
current live root and does not reset key-index scratch.

The original forceBind0x2b3615 resolves the source target UID, not a skin-joint
scope or decoded node index. Missing target nodes stay NULL in the original
source-order vector. Position/PositionZ bind the actual local position and mask4;
Quaternion binds the actual local quaternion and mask2; Scale binds the actual
local scale and mask1. Repeated tracks on a node retain only one bound-list
reference. No ordinary animator list entry is added by this class.

The typed mask assignments are independently proved by original getters plus
setIsAnimated0x2728a5, and required the separate OG067 owner correction. Raw bit4
is animated position, so the original relative-cache low-mask0x1b correctly takes
its translation-only branch for that bit; animated scale1 rebuilds the basis.
No direct flag write or pose-side workaround replaces the owner contract.

Original onUnbind0x2b348d calls forceUnbind0x2b3441, matching the callback node's
UID against every source target UID. It clears matching slots without clearing
the root field. This also applies after rebinding: an old scene's callback can
clear matching targets in the new scene. The native implementation preserves
that surprising behavior. It never silently unregisters the old scene or repairs
the cleared slots on a same-root bind.

`boundRoot()` exposes the original retained identity field; it is a borrowed
identity, not permission to dereference it after owner expiration. Partial
target unbinding does not destroy the live Bip01 node. `evaluate` reads that
genuine current local position after applying all remaining bound tracks.

## Exact sampling and application

The original addAnimationTrack0x2af67d starts with zero length, converts each
signed first/last source time to float, subtracts in float, compares with the
float-converted current integer length and converts the selected result back to
integer. For example16777217..16777219 produces period4, not2. The supplied
24-track library produces259066. Nonpositive/zero length and conversion beyond
the supported signed-int32 range are explicit errors rather than a fake period.

This component is the detached timeline path used by Character after
setTimelineCtrl(NULL). Every apply accepts the original unsigned time and uses
unsigned modulo by that library period. It does not normalize by selected clip
length or independently advance time. Mode1 suppresses interpolation; other raw
mode values allow it, subject to the track's selector0 suppression.

`retail_animation` shares its existing bracket search and typed interpolation
with the new full signed-source sampler. Equal times choose the rightmost key;
before-first scratch clamps to0. Original int32 subtraction wraps before float
conversion in the full-source path. Existing clipped-channel validation and
arithmetic remain unchanged. Both paths use the same original quaternion slerp
and exact-key raw-copy implementation; no second quaternion algorithm is added.

The native integer key-index records start at0. Source cache metadata nonzero
selects first-record scratch for every non-NULL target, even when slot0 itself
has no target. Otherwise each slot has its own scratch. Search writes the
selected index but never uses an old index as another track's bracket.

Plain apply's weight exactly1 path performs unweighted typed application.
Position writes XYZ, PositionZ writes only Z through the position base,
Quaternion copies/interpolates four words, and Scale writes XYZ. Exact quaternion
keys remain unnormalized. For these four supported classes the original
nonunit-weight applyKeyBasedValue methods are literally empty; search/scratch
still occurs but target words are unchanged. This includes NaN weight's unequal
comparison branch. Weighted **getValue**, which has different current-pose
blending behavior, is not substituted for this original apply operation.

Actual CSceneNodeAnimator::animateNode0x2af0a1 forwards its supplied time to
virtual applyAnimationValues. The override preserves that forwarding, while
Character's manual evaluation path still adds no ordinary list registration.

Raw pose writes rely on the original persistent animation masks. They neither
call local setters nor refresh cached absolute matrices. `evaluate(relative,
absolute)` applies using the absolute source time, then reads the true live
Bip01 position. It does not add the selected clip origin a second time.

The existing Character clock retains its callback/body ordering: tail/start/head
samples see the already advanced clock, body velocity/wake intents are consumed
by the caller, and the caller finally applies visualRootTranslation to strip the
actual root. Pose application does not strip the root early or consume body
intents. A subsequent full-pose evaluation overwrites the stripped root normally.

## Validation boundaries

Tests cover typed references/retention, missing slots, common/per-slot scratch,
same-root/no-op/explicit repair, old callbacks after rebind, owner destruction,
creator release, source selectors, mode/weight branches, unsigned wrapping,
signed duplicate/overflow sampling, float-derived period and real clock/root
composition. Asset-backed tests evaluate both actual Player graphs across655
profiles each: global boundaries, every213 clip's begin/tail/midpoint and
mode/weight cases. Whole local TRS, untouched lanes, flags and scratch are
compared to actual original execution, with repeated actual root stripping.

Original evidence, immutable ELF/asset hashes, Release/Debug results and ignored
probe boundaries are recorded in the submission report. Original imports and
resolved runtime source records are modeled only in research tools; there is no
ARM/Python runtime dependency. Android libm final-bit parity, original allocator
teardown, complete resource/timeline/event I/O, generic AnimatorSet/Blender,
non-root factory binding, renderer/CPU skinning and the remaining full scene
host/controller composition are not claimed. No gameplay progression is used.
