# Original mutable material scalar state

`RetailMaterialState` ports the original material mutation leaves without copying
the ARM pointer-containing `SMaterial` layout into native objects. All source
colors, flags and dirty masks retain fixed 32-bit values. Texture ownership,
texture matrices, source-material preparation and owning scene traversal remain
separate responsibilities.

Original reference ELF SHA256:
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.

## Source contracts

The scalar defaults match the actual `SMaterial` constructor at `0x20e7ed`.
`SMaterial::setFlag` at `0x20fb1d` treats a supplied flag containing bit4
specially: it changes word0 bit0 (numeric mask1), rather than flags40 mask4.
That branch is exclusive even for a combined flag mask. Other flags compare
whether any requested bit is already set; changed masks below bit5 dirty mask1,
while higher masks dirty mask4. Public flag2 changes stored flags40 mask2.

The internally derived flags40 mask4 tracks color-material state. Its value is
true if stored lighting mask2 is clear, public vertex colors are enabled, or
current and ambient packed colors are equal. A change of this internal state
dirties mask1. Changing word0 alone does not automatically dirty mask1. One
shared native helper preserves the repeated original derivation in flag,
current-color and ambient-color mutations.

The recognized-mesh leaf of `GS3DStuff::SetColor` at `0x2ded05` changes only
material0: selectors0/1/2/3 address current84/ambient48/specular56/emissive52.
Current color changes dirty mask8; the other color changes dirty mask4. Only
current and ambient changes recompute tracking. Equal values and unsupported
selectors return without mutation. `GS3DStuff::SetAlpha` at `0x2df34d` changes
only selector0's current alpha byte, with the same current-color mutation rules.
The native uint32 input explicitly narrows alpha to the original unsigned-char
argument domain. It reuses the current-color mutation instead of duplicating
tracking and dirty-mask logic.

The original scene helpers recognize ordinary and skin mesh node tags, update
their first material and return; other node types recurse through children.
This scalar object does not pretend to perform that traversal or own any node.

## Primary color boundary

When a renderer requests color attribute4, original `setupArrays` at `0x2c72fd`
uses the primary stream exactly when one exists and either stored lighting
mask2 is clear or word0 mask1 is set. Otherwise it calls `glColor4ub` with the
four literal current-color bytes. `usesVertexColors` implements that predicate;
`retailMaterialRgba` converts those byte0/1/2/3 values to canonical float RGBA.
The current color is a producer alternative, not an additional multiplication
over a selected vertex stream. No lighting equation is introduced here.

If the caller does not request attribute4, original setupArrays preserves the
actual current color instead. This API requires the requested-color domain and
does not invent an absent-request fallback. Secondary vertex colors are separate.

## Validation and remaining integration

Root executed the actual original constructor, setFlag, SetColor, SetAlpha and
setupArrays against the independently emitted native input/output corpus in both
Release and Debug: 6,720 mutations, 47,040 exact state words and 13,440 GL primary
selection checks per configuration. Ordinary and skin tags alternate. Every
unrepresented material byte remains unchanged. The corpus includes public and
stored flag namespaces, mixed/high/zero masks, repeated changes, color equality,
pre-existing dirty masks, all four color selectors, unsupported selectors and
unsigned-char alpha narrowing. Non-gray RGBA conversion has a focused fixture.

The ignored original driver captures actual GLES arguments and supplies only
typed node material0/type dispatch plus imported GLES entry points. It does not
prove scene recursion, an original graphics device, or hardware pixel parity.
The native focused test is `retail_material_test`; `--oracle-dump` emits its
original-comparison corpus. No retail data is embedded in the test.

The existing city/actor renderer has not yet been switched to this state. Its
primary stream ordering, prepared constants and unsupported normal-lighting
approximation remain explicitly pending OG059's rendering correction. Genuine
per-instance material ownership, all-material flag recursion and material0 color
recursion must be integrated into the retained scene host before that host is
complete. This change supplies their shared scalar implementation, not empty
host callbacks or a declaration that the game port is complete.
