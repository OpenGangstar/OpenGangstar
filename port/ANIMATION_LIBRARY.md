# Complete source actor animation library

`loadCityAnimationLibrary` exposes the original, unclipped source tracks through
the existing BRES reader. `readCityAnimationLibrary` accepts an already decoded
`CityScene` byte owner. Returned data owns its strings/keys and has no pointer
into the temporary file resource. There is no second BRES parser or clock.

The supplied `anim_ingame.bdae` contains **24 tracks, 73,176 keys and 23 target
UIDs**: one Position, one PositionZ and 22 Quaternion tracks. Every track starts
at source time0 and ends at259066, but their internal key counts/times differ.
No clip reconstruction, target deduplication, key resampling or merging is used.

## Source contract

`CityAnimationLibraryTrack` retains:

- `sourceIndex`: original top-level SAnimation array position, independently of
  its UID or target.
- `sourceUid`: SAnimation+0 string; `target`: SChannel+4 target UID. The two
  identities are not interchangeable, and duplicate strings do not merge tracks.
- `transform`: the existing typed translation/translationZ/rotation/scale lane,
  corresponding to actual source properties1/4/5/10. PositionZ's scalar value is
  stored in value[0]; original application writes it to Z of the bound position.
- `interpolationSelector`: the exact sampler word, without renaming numeric
  values as an assumed interpolation enum. Original selector0 suppresses
  interpolation; other selectors permit it subject to animator mode. All24
  supplied selectors are1.
- `inputSourceIndex` and `outputSourceIndex`: the actual selected entries in the
  source array, rather than an assumption that inputs/outputs are always0/1.
- `keys`: all signed int32 source milliseconds and original finite float words,
  including negative times, signed zero and equal adjacent times. Unused native
  value lanes are zero initialized. No clip offset or duration is applied.

`commonCacheMetadata` retains the exact SCollada+12 word (5 in this source).
Actual DefaultFactory plain animator construction uses its nonzero condition
for shared integer key-index scratch. This is separate from interpolation and
does not establish that tracks have common key arrays or common bracket indices.

No period is calculated in the decoder. Original `addAnimationTrack`0x2af67d
converts first/last signed times to float, subtracts in float and updates the
runtime integer length. The runtime must preserve that conversion and detached
unsigned-time modulo behavior. The decoder's endpoints do not replace it with
an integer-duration shortcut or a selected clip's length.

## Shared reader and preserved consumers

`readAnimationChannel` and `readAnimationSamples` are the single internal channel
and indexed-sampler record readers used by both the full source and clipped
paths. The existing 213-entry indexed catalogue, case-insensitive script lookup,
model binding and particle clipping keep their original behavior. In particular
clipped keys remain uint32 relative milliseconds and the clipped reader retains
its strict increasing-key check. The new signed-key type changes no existing
channel aggregate layout or sampler/clock API.

The complete reader's bounded domain is one sampler/channel per top-level
animation, no nested animation records, signed-time source type4, float-value
source type6 and properties1/4/5/10. It validates spans, source indices, component
counts and finite values through the existing checked reader. Empty tracks,
decreasing times and unsupported property/record shapes fail explicitly instead
of silently removing source records. Equal times are retained; this data seam
does not claim that an existing native clipped sampler handles duplicates.

## Original validation

Original ELF SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Source/order and plain-animator evidence: database constructAnimator0x297c51,
constructAnimation0x297495, DefaultFactory createAnimator0x29d395,
addAnimationTrack0x2af67d, applyAnimationValues0x2af335 and detached timeline
setter0x288249. OG056 establishes this actual Player path separately from
AnimatorSet/Blender behavior.

`animation_library_test` covers full signed/duplicate time preservation, all four
typed lanes, raw selectors/source identities, indexed sources, malformed inputs
and a clipped particle/casefold compatibility fixture. Its optional arguments
are an asset directory and binary dump destination. Actual-source tests validate
the24/73176/23 inventory and retain213 catalogue entries and original indexed
intervals.

Ignored `port/build/OG-060-reference.py` compares every key/time/value word and
metadata field with the raw BRES independently. It then executes actual original
typed track construction for every native-decoded record and the actual default
plain animator. It verifies source order/target URI, default weight/mode,
period259066, detached unsigned wrapping, all untouched value lanes, exact-key
bits and first-slot common scratch even with a NULL first target. Ten time
profiles produce240 actual apply/direct-unweighted-get comparisons per build.

The reference supplies resolved runtime SAnimation/sampler pointers, native
destination buffers and allocation/memory/AEABI/scalar math imports. Actual
original track factories, animator constructors, add-track, binding-slot lookup
and application execute. This is not original resource-file I/O, a shipped ARM
dependency, Android libm bit-equivalence or a full native pose implementation.
Release/Debug full build/regression details are in the submission report.

The retained live scene binder, pose application, controller/body order and
renderer/CPU skinning still require their actual native implementations. This
library supplies the immutable source data those systems need; it adds no empty
host, dummy animator or guessed playback behavior. No manual gameplay is used.
