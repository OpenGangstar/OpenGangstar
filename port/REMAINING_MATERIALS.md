# Remaining original material modes

This is the OG027 reconstruction and implementation contract for built-in material
3 (`EMT_LIGHTMAP`), 12 (`EMT_REFLECTION_2_LAYER`), and 17
(`EMT_TRANSPARENT_REFLECTION_2_LAYER`). It does not assign the custom
`CCustomMaterialRenderer_VERTEX_COLOR_ALPHA` renderer to any source record.
The supplied catalogue has no selected occurrences of these three modes: the
359 BRES files contain 1,366 material records, selected as 1,178 solid, 19
additive, and 169 alpha-channel materials. Synthetic fixtures are therefore
required; they are not retail occurrences.

## Source and equations

Evidence is the pinned original `libsanangeles.so`, SHA-256
`b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Addresses below are original Thumb function addresses; instruction addresses
can be even. Native BRES fields remain fixed 32-bit offsets and indices.

Let `P` be the primary color entering texturing, `T0` and `T1` the two texture
samples, and `S0` the stage-zero result. Each fixed-function stage clamps its
result to [0,1]. These equations describe the texture operation, not a claim
that the existing native primary-lighting approximation is complete.

| Mode | Stage zero | Stage one RGB | Stage one alpha | Framebuffer operation |
| --- | --- | --- | --- | --- |
| 3 | REPLACE | S0.rgb * T1.rgb | S0.a * S0.a | Opaque |
| 12 | MODULATE | S0.rgb + T1.rgb | Inherited combiner sources, below | Opaque |
| 17 | MODULATE | S0.rgb + T1.rgb | Inherited combiner sources, below | ONE / ONE_MINUS_SRC_COLOR |

`CCommonGLMaterialRenderer_LIGHTMAP::onSetMaterial`, `0x002cd151`, binds
texture 1 then 0. Modes 3-6 select stage-zero `GL_REPLACE` at `0x002cd39c`;
7-9 select MODULATE. The mode-3 stage-one path sets COMBINE, RGB MODULATE,
SRC0_RGB=PREVIOUS, SRC1_RGB=TEXTURE, and RGB_SCALE=1. Crucially,
`0x002cd2f8`, `0x002cd324`, and `0x002cd352` set COMBINE_ALPHA=MODULATE and
**both** alpha sources to PREVIOUS. Lightmap texture alpha does not participate.
Mode 3 requests vertex attribute mask `0x35` at `0x002cb1d1`.

The reflection onSet methods at `0x002ccdf5` (12) and `0x002ccc31` (17)
bind stages 1 and 0, then select stage-one COMBINE with RGB ADD,
SRC0_RGB=PREVIOUS and SRC1_RGB=TEXTURE. Neither method assigns
COMBINE_ALPHA, SRC0_ALPHA, or SRC1_ALPHA. Mode 17 additionally calls
`glBlendFunc(1, 0x301)` at `0x002ccd76` and enables GL_BLEND; this is
ONE / ONE_MINUS_SRC_COLOR, including the corresponding alpha factors.
Its framebuffer equation is componentwise `source + destination*(1-source)`.
Do not replace it with ordinary source-alpha blending. Both reflection modes
request attribute mask `0x37` (`0x002cb1dd`, `0x002cb1e5`); mode 17's
`isTransparent`, `0x002cb1e1`, returns true.

The texture-stage equations and RGB-versus-RGBA distinctions follow
[Khronos OpenGL ES 1.1, section 3.7.12, Tables 3.15-3.19](https://registry.khronos.org/OpenGL/specs/es/1.1/es_full_spec_1.1.pdf).
REPLACE takes RGB from the texture; its alpha is texture alpha for RGBA and
incoming alpha for RGB. MODULATE multiplies incoming and texture components;
a base-RGB texture supplies alpha 1 to COMBINE. This distinction must be
retained independently of native BGRA8 storage or whether all decoded alpha
bytes happen to be opaque.

## Persistent alpha transition state

Fresh stage-one COMBINE alpha is MODULATE with sources TEXTURE and PREVIOUS.
This agrees with original `SShadowState::STexEnv` construction at `0x002c44c1`:
its source constants are `0x1702` and `0x8578`. Reflection initially computes
`S0.a*T1.a`. Once a lightmap renderer has been bound, both sources become
PREVIOUS, so reflection computes `S0.a*S0.a` instead.

LIGHTMAP unset `0x002cc3d9` resets RGB scale and texture environment modes;
it does not reset alpha sources. Reflection unsets `0x002cc269` and
`0x002cc195` restore the environment mode and clear software texgen. They
also leave alpha sources untouched. `setBasicRenderStates`, `0x002c6661`,
contains no TexEnv call. `setTexture`, `0x002c6df5`, texture `bind`,
`0x002c5271`, and `updateParameters`, `0x002ce9dd`, do not reset the combiner.
`setRenderStates3DMode`, `0x002c7ae1`, calls the previous renderer's unset on
a type change and calls onSet with the reset flag, but does not reset the
texture environment itself. Both reflection methods' reset-true paths still
leave alpha sources untouched. `beginScene`, `0x002c5481`, allocates a dynamic
batch and does not reset TexEnv state. This is driver lifetime state, not
per-material, per-frame, or per-command-buffer state.

An ignored independent Unicorn harness executes the actual original STexEnv
constructor, renderer onSet/unset methods, and reset-true branches. GL entry
points are recorded, while setTexture and basic states are explicitly stubbed
after inspecting their bodies. Fresh 12/17 preserve TEXTURE/PREVIOUS;
3 -> 12 -> 17 and an unchanged-type reset preserve PREVIOUS/PREVIOUS.
The native typed state record preserves that distinction and persist
through ordinary one-stage draws and frame boundaries. Native CityRender is
recreated on session reset, so the record belongs to its Vulkan context, whose
lifetime corresponds to the original driver. Offscreen verification must save
and restore that context state so diagnostic draws do not alter game behavior. Other unsupported
renderers that might change this state are outside this bounded contract.

## Coordinates and resource selection

Reflection onSet sets driver texgen bit 1 and stage-one mode 2 at
`0x002ccf20` onward / the corresponding mode-17 branch. `softTexGen`,
`0x002c8a7d`, mode 2 invokes `computeSimpleSphereMapTexCoords`,
`0x002b8e85`. Mode 1 invokes the different full sphere-map helper; it must
not be substituted. The mode-2 call supplies float32 source normals, stride,
a view*world matrix, the driver's orientation, and **false** for normalization.
The normal is multiplied by the matrix's linear 3x3, without translation,
inverse transpose, or normalization. Coordinates are produced per vertex
before rasterization and then interpolated.

For transformed normal `(x,y,z)`, the original orientation branches produce:

| Orientation | u | v |
| --- | --- | --- |
| 0 | 0.5 + 0.5*x | 0.5 + 0.5*y |
| 1 | 0.5 + 0.5*y | 0.5 - 0.5*x |
| 2 | 0.5 + 0.5*x | 0.5 - 0.5*y |
| 3 | 0.5 + 0.5*y | 0.5 + 0.5*x |

The original constants at `0x002b90b8`/`0x002b90c4` are +0.5/-0.5.
`softTexGen` multiplies driver matrices at +0x20c (view) and +0x250 (world)
through `CMatrix4::mult34`, `0x00272c0d`; `setTransform`, `0x002c7689`,
stores matrices with a 68-byte stride and the recovered enum is view=0,
world=1, projection=2. `GS3DStuff::load` explicitly sets driver orientation
0 at `0x002e0438`; `SetCurrentOrientation`, `0x002dc289`, can subsequently
change it. The existing native lighting normal is inverse-transpose and
normalized, so it is unsuitable as the reflection coordinate input.

`CColladaMeshBuffer` construction at `0x002a17b1` binds buffer byte 16's UV
stream into stage-zero descriptor fields +0x80/+0x84 and buffer byte 19's
stream into stage-one fields +0xa4/+0xa8 (`0x002a198a` through `0x002a1a18`).
The decoder already retains the second stream as `auxiliaryTexCoords` but
CityVertex does not carry it. Mode 3 requires authored UV1. Missing/non-float
normal streams trigger original soft-texgen validation; they must not be
replaced by a guessed normalized vector. Missing UV1/current-coordinate
state after array draws is not a demonstrated substitute for authored UV1.

`CMaterial::prepareMaterial`, `0x002a0905`, binds source material +40's image
to stage 1 at `0x002a09cc` and source +36 to stage 0 at `0x002a09dc`.
The reflection branch dereferences source+36 unconditionally; a reflected source
with absent +36 is outside the original valid domain and is rejected
rather than supplied an effect-texture fallback. Its late lightmap branch selects
mode 3 and replaces stage 1 from the first
image reachable through effect+12 at `0x002a0cca` through `0x002a0ce2`.
The post-load fixup at `0x002ae57e` through `0x002ae5c2` first converts
sampler words (count+72/table+76) into references to image-list entries, then
image-list words (count+80/table+84) into SImage references. The lightmap
vector is count/data at effect+12; `0x002ae72c` through `0x002ae754` fixes
its 28-byte texture records' first word from sampler index into a sampler
reference. Thus native raw decoding follows first record -> sampler index
-> image-list index -> global image index. The diffuse texture vector at
+20 uses the same 28-byte records and fixup at `0x002ae702` through
`0x002ae72a`. Existing flattening of the image list directly does not preserve
this indexed selection and must be replaced with the actual chain. Lightmap
selection unconditionally uses its first texture record; an empty or invalid
chain is rejected rather than substituted with an arbitrary effect image.

## Static texture matrices

The diffuse texture-record loop additionally builds per-stage texture matrices
at `0x002a0d28` through `0x002a0df6`. It passes record rotation(+16), a zero
center, translation(+8,+12), and scale(+20,+24) to
`CMatrix4::buildTextureTransform`, `0x0028f1c5`. For zero center the result is
`u'=sx*cos(r)*u-sx*sin(r)*v+tx`,
`v'=sy*sin(r)*u+sy*cos(r)*v+ty`, with radians and output-axis scaling.
`applyMatricesChanges`, `0x002c81d5`, repacks the original matrix translation
from elements 8/9 into the GL column at `0x002c84d4` through `0x002c8506`.
The transform therefore acts after reflection coordinate generation as well
as on authored UVs. Late material +36/+40 and lightmap image bindings do not
clear the previously prepared per-stage matrices. In particular the late
lightmap image replacement does not build a matrix from the lightmap record.
Static transforms belong in this implementation slice; animated updates and
the distinct render-target vertical-flip branch remain separate boundaries.

The supplied catalogue has 776 diffuse texture effects and 777 texture records,
including one effect with two records. All but sfx_lock record 0 have literal
identity fields; that record has two tiny subnormal translation values. This
raw observation does not establish a visible original nonidentity selection.
All 730 image records have local-image type 0; external-library image lookup
(the prepareMaterial branch at `0x002a0db0`) has no supplied fixture.

## Texture formats, depth, and implementation boundary

`CNullDriver` initializes options to 0x12 at `0x002c1a8a`. Executing original
`getBestColorFormat`, `0x002ce031`, under that default maps input image formats
0/1/2/3 to format 3 (RGBA), preserving format 4. With option32 also enabled,
0/1/2/3 map to format 2 (RGB). Compressed ATC upload bypasses that conversion:
ATC-space is RGB, ATCA/ATCI are RGBA as established in MATERIAL_MODES.md.
Native metadata should describe the original upload base format under the
supported default option contract, not infer it from pixel values. Arbitrary
runtime option changes and alpha-only texture uploads require separate
explicit support; they are not silently equivalent to RGBA.

Modes 3/12 remain opaque and use the existing opaque depth write and LEQUAL
contract. Mode 17 must enter the existing node-based transparent queue and
inherit default option64=false's disabled transparent depth writes. The
original depth predicate and native ordering limitations are recorded in
MATERIAL_MODES.md. No centroid sorting or mode-dependent alpha multiplier
is justified. Full original lighting, animated material transforms, and
original address-dependent exact material tie ordering remain existing gaps.

The native implementation supplies indexed material resources, authored UV1,
raw model-linear reflection normals, camera view axes, two sampled texture
stages, persistent typed alpha state on Vulkan, and the mode-17 screen-blend
pipeline. The existing primary lighting approximation is preserved. The
original missing/non-float-normal path logs and skips the generated-UV
override; native rendering therefore retains authored UV1 in that case.
New two-stage modes reject missing required authored UV streams instead of
inventing the original context's current texture-coordinate state, which is
not guaranteed after array draws. Float normal texgen does not require UV1.
Native Windows uses orientation 0, matching the observed game load setup;
other device orientations have been reconstructed above but no native device
rotation controller is claimed.

## Native implementation

The implementation changes city_assets, city_view, Vulkan, the city shaders,
the image decoder metadata, and their existing tests. Original fixed32-bit
BRES layouts remain offsets and indices; native structures contain native
strings/vectors and have no ARM pointer-size dependency.

Both static affine texture matrices live in an immutable per-binding uniform
buffer. Shared mesh vertices and animation ranges retain their UV streams.
The affine matrix is applied to interpolated coordinates before sampling;
reflection coordinates are generated per vertex first. Per-stage affine
transforms commute with perspective-correct interpolation in exact arithmetic;
native GPU floating-point rounding is not claimed bit-identical to original
ARM software float operations. The uniform buffer has the device's required
offset alignment. Descriptor sets bind both sampled stages and a matrix block;
one-stage descriptors use the real identity block. One 128-byte push range
contains camera64, material32, and two view-normal rows32. It stays within
Vulkan's guaranteed minimum and is visible to both shader stages.

Inherited alpha state advances at actual draw emission after transparent
sorting. It is stored on Vulkan rather than CityRender and survives command
buffers, frames, and native session resets. Offscreen diagnostics save and
restore it. New stage bindings distinguish absent or failed image loads from
neutral fallback storage; a disabled texture stage passes its incoming color.
Modes 3/12/17 use the existing node-origin/layer/cameraOffset ordering and the
existing documented strict material tie policy. No triangle-centroid sorting
was added.

## BMP default conversion correction

The original BMP indexed/16-bit loader produces A1R5G5B5, but default driver
options 0x12 do not upload that image directly as packed16. `reloadTexture`
reaches `getImageData` at `0x002cefa6`. That function calls
`getBestColorFormat` at `0x002cee78`, selecting RGBA32 format3. Its equal-size
branch constructs `CImage(format3,source)` at `0x002ceec8`; constructor
`0x002bbcf5` invokes `Blit(eBlitter3)` at `0x002bbd6a`. Blit's dest3/source0
branch `0x002bab14` resolves its function pointer exactly to
`executeBlit_TextureCopy_16_to_32`, `0x002ba665`. The resized branch reaches
`copyToScaling` and `convert_viaFormat` (`0x002bb064`), then converter
`0x002b994d`.

Executing both original 16-to-32 routines for all 65,536 packed words yields
identical bit replication, `(v<<3)|(v>>2)`, for each five-bit channel. 21,632
packed words differ from nearest UNORM expansion. Native BMP decoding now
uses the proven default conversion, including palette colors, and the tests
check every packed word against the original 32-entry channel ramp. TGA/ATC
already used bit replication. Supplied BMPs are 24/32-bit and retain their
existing decoded pixels. Other runtime option masks, including direct packed
uploads, remain outside the default-options contract.

## Original primary-color selection

The supported original CommonGL path disables GL_LIGHTING in both reset and
changed lighting-requested-flag branches (`setBasicRenderStates`, 0x2c6661,
actual disables at0x2c6734/0x2c6950). Material/light uploads do not establish
an extra directional-normal lighting equation. Original `setupArrays`
(0x2c72fd, selection0x2c757e..0x2c75f4) chooses authored primary RGBA when a
primary stream exists and either stored flags40 mask2 is false or material
word0 mask1 is true. Otherwise it sends the literal bytes of current color84
to glColor4ub. Public material flag4 changes word0 mask1; internal flags40
mask4 is color-material tracking and does not independently select a stream.
All supported built-in material paths request primary attribute4.

`CityMaterial` now retains the verified seven-field `RetailMaterialState`
instead of a separate diffuse word. Preparation preserves the original current
color, ambient, emissive, specular, flags, vertex-color control and dirty88:
color-valued diffuse scales its alpha by effect+56 with float multiplication
then integer truncation; texture-valued diffuse retains current white. A
source+36 layer-zero override copies source+32 literally. Additive selection
and that same literal color copy follow reflection/ordinary-alpha selection;
lightmap follows additive and skips the ordinary ambient/emissive assignment.
Effect+68 suppresses source512/1024 flags after their initial assignment.
Actual original preparation for all1366 supplied records matches all seven
native fields, including nonwhite ambient/specular and dirty state. No filename
exceptions or unconditional alpha multipliers are used.

The earlier OG059 research prose incorrectly described reflection as bypassing
additive. Literal0x2a0a34 sends a present reflection directly to0x2a0a3c;
ordinary alpha selection also ends there. Eight independent original conflict
executions confirm additive follows either branch, then lightmap overrides it.
The existing native mode selector already preserved that precedence.

Primary vertex decoding now preserves byte0/1/2/3 as float RGBA, matching the
original glColorPointer input and Vulkan float vertex format end to end.
Secondary-color decoding and its distinct existing BGRA representation remain
unchanged; no current production consumer or new original use is inferred.
All1877 supplied ordinary mesh loaders compare directly to raw primary and
secondary bytes:169337 primary rows across473 meshes and19637 secondary rows.
These are source rows, not buffer repetitions or visible draw counts.

Resolved world surfaces retain the prepared scalar state and actual primary
stream presence. The production fragment constants choose that state's literal
current RGBA OR the authored stream using `usesVertexColors`; they do not
multiply both. The world shader no longer invents normal-based brightness.
The existing explicit native brightness setting remains an application option.
Independent UI/particle primary producers retain their established constants,
alpha and descriptor paths. Reflection UV generation still uses the original
raw normal path; removing primary lighting does not remove texgen normals.

The two car-light materials demonstrate why selection matters: prepared current
color is0x00ffffff, but their default authored primary streams are white with
alpha255, and the original selector uses those streams. Badge/collectible glow
have no primary stream and use prepared0xff959595. The particle materials keep
the separately proved emitter/animation color producer; corrected source
constants are not multiplied over those particle colors.

This is the prepared-state and resolved-surface producer, not a complete live
actor material host. Original game setup/recursive material0 overrides and
shared mutable actor-material identity must eventually operate on the genuine
scene owner; this change adds no model-name override or synthetic hierarchy.
The scalar mutation leaves remain authoritative in MATERIAL_STATE.md.

## Validation and remaining work

Original executable probes cover fresh/reflection/lightmap/unset/reset alpha
state, indexed resource fixup, all four simple-sphere orientation signs,
static texture matrix coefficients, the default color-format mapping, and
both exhaustive 16-to-32 conversion paths. They run only as ignored research
artifacts and add no emulator or original binary dependency to the port.

The GPU smoke uses production shaders and pipelines with RGBA readback. Its
48 checks retain the earlier 11 material cases, the new texture equations,
RGB/RGBA/disabled-stage alpha, authored UV1, both static texture matrices,
nonunit reflection normals, view rotation, missing-normal fallback, fresh and
inherited reflection alpha, an intervening one-stage draw, state across command
buffers, mode 17 RGB/alpha screen blending, and depth writes. A separate UI
case uses a nonuniform texture through the shared production one-stage
descriptor writer and real identity uniform block, real overlay default
mode 0/diffuseAlpha 1/stageState 0 constants, and the caption pipeline. This checks
legacy UI sampling and fractional alpha directly rather than relying only on
opening smoke presentation. Seventeen additional production-selector readbacks
check non-gray stream and current RGB/alpha, stream absence, public flag4,
stored flags2/internal4, opposite normals, modes13/14/15/25 alpha behavior, and the literal wreck current/stream choice.

Retail enumeration still selects no modes 3/12/17. The former 1,409 secondary
"bindings" flattened whole image lists. The actual indexed diffuse vectors
contain 777 records across 776 effects; only suburbs_low effect 1 has two
records, both selecting low_city_02. Audit now reports 1/1 prepared indexed
secondary material slots and separately reports zero records selecting a
two-stage renderer with an image. This does not assert that mode 0 draws use
the prepared second slot. Hospital has one texture record selecting
props_alpha_02; props_alpha_01 remains in its image catalogue but is not a
selected secondary binding. Tests validate the actual placed hospital material
resolution as mode 14 and its selected single texture.

Remaining boundaries are explicit: arbitrary external lighting state, dynamic
material animation and its matrix-cache transitions, render-target vertical
flips, arbitrary runtime texture-option changes, nonzero device orientation,
and current-coordinate fallbacks without authored streams are not completed
here. External-library image binding requires a live root-scene host and is
rejected explicitly when selected. All 730 supplied image records are local.
Reflection reuses the current native posed skin normal, removes its existing
inverse-transpose world step, and applies the original raw model-linear step;
the original skin-normal deformation algorithm and post-skin component type
have not been validated by this material work. Existing quaternion/node and
camera transform implementations are preserved; a separate original scene
factory/rotation investigation is still in progress. The synthetic tests do
not establish complete original skinning or scene-transform parity. Opaque
emission retains existing native grouping; complete original opaque pass
ordering was not reconstructed here. The inherited-state implementation is
verified for the actual emitted draw order, not a claim that every original
scene produces the same order.

The actual original fixup block was also executed with a deliberately permuted
sampler list [2,0], image list [2,0,1], diffuse records [0,1], and lightmap first
record 1. The original resolved diffuse images [1,2] and lightmap image 2. This
is an independent indexed-identity oracle for the actual-loader fixture.
An original simple-sphere helper execution independently checked all four
orientation signs with an identity matrix, a nonunit normal, and nonzero
matrix translation.

Final verification: complete x64 Release and Debug builds and 36/36 CTests
passed in both configurations. Supplied retail geometry/door/material fixtures
passed in both, and 48 actual GPU RGBA readbacks plus automated opening smoke
passed in both. The full asset audit exits 1 in both configurations because the
supplied archive still lacks 111 distinct texture references; primary bindings
remain 4,058/4,216 and district bindings 2,908/2,910. All 98 supplied images still
decode. No manual gameplay, original ARM edits, commits, or pushes were used.
