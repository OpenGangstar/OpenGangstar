# Original material selection and native renderer gaps

OG-004 investigated baseline `feb6317` on 2026-11-24. The reference library has
SHA-256 `b322b642d365a34c48e140234b7ef4efd965d1e81acd2b95d9034e2f4386ea8a`.
Symbol addresses in the catalogue include the Thumb bit; individual instruction
addresses below do not. This report changes no runtime implementation or original
decompilation files.

The custom `CCustomMaterialRenderer_VERTEX_COLOR_ALPHA` is constructed and
registered, but a concrete material that selects it has not been established.
Registering an arbitrary native material enum or selecting it whenever vertex
alpha is present would therefore be unsupported. A separate, directly evidenced
gap is more actionable: ordinary Collada materials select the original
`trans_alphach` and `trans_add` renderers, while the native scene decoder currently
reduces material selection to a diffuse texture/color and an alpha-test flag.

## Native implementation update: OG-009

Verified on 2026-11-25. The initial investigation below describes the earlier
baseline; its proposed follow-up is now implemented for modes 0, 13, 14, 15 and
25. BRES material resolution uses the serialized effect index directly, retains
duplicate effect-name identities, validates indices and applies source image
+36 as the later layer-0 override even when an effect texture is already bound.
Actual-loader fixtures exercise conflicting images, absent URLs and bad indices.

Vulkan shares material pipelines across city geometry, actors, markers, lights
and particles. Modes 13/14/25 retain LEQUAL depth testing, disable depth writes
under the evidenced default option and use SRC_ALPHA with ONE or
ONE_MINUS_SRC_ALPHA for both RGB and alpha. Modes 14/15 use texture alpha alone;
0/13/25 multiply texture, primary and material alpha. Mode 15 discards alpha
less than or equal to 0.5 and preserves surviving fractional alpha and depth
writes. Particle preparation converts 14/16/17/26 to 25, preserving 13/25.

Transparent draw entries share per-mesh-child origins and sort after opaque
geometry, by descending rendering layer and squared node-origin distance plus
camera offset. Shadow nodes retain the game layer override. Exact-distance ties
use a strict native material/resource order: original pointer hashes and signed
wrapped subtraction cannot safely define native C++ ordering. The native path
does not sort triangles. Some unskinned attachments and wheel child origins
still lack complete original transform propagation.

Original evidence added by the implementation includes alpha-reference setup
at 0x002cd4a9, particle preparation at 0x002ac8f5, mode-25 setup at 0x002cbf29,
basic depth state at 0x002c6661, transparent entry construction at 0x00217331 and
ordering at 0x0029f005. The absent Allow_ZWrite_On_Transparent attribute and
driver default 0x12 support disabling transparent depth writes; arbitrary
indirect attribute writes remain unexcluded. Source-image override at
0x002a0972 follows the effect-texture loop returning at 0x002a0e02.

Independent integration validation: all seven Release CTests passed, actual
retail fixture passed, and automated opening plus checkpoint 19/20/32/33/36/37
smokes passed without changing their temporary saves. Eleven actual offscreen
GPU RGBA readbacks cover blending, alpha, cutoff, depth writes and equal-depth
LEQUAL through production pipelines and shaders. The executable is AMD64 PE32+.

All 359 retail scenes and 1,366 material records decode: 1,178 solid, 19 additive
and 169 alpha-channel. Source-image bindings resolve four additional car-light
mesh buffers. The audit still fails on the same 111 missing texture paths and
two district bindings to missing light_parking.tga; all 98 supplied images decode.
This is not full visual parity. Modes 3/12/17 are identified but explicitly
unsupported, custom GL_DECAL selection remains unknown, and lighting, fog,
animated materials, arbitrary option changes and complete node transforms need
further implementation.

## Custom renderer registration and unresolved selection

`Application::Init(irr::IrrlichtDevice*)`, at `0x002d5591`, constructs the custom
renderer in the sequence at `0x002d5742` through `0x002d5798`. Its driver is stored
at object offset 4, its final vtable is installed at `0x002d5784` through
`0x002d5794`, and its pointer is written to `Application::s_materialRenderer`.
The global is at `0x004b67bc`; its GOT slot is `0x004b1538`, offset `0x14f0`
from GOT base `0x004b0048`. This object is a renderer, not a material-type index.

`Application::GetMaterialRenderer()`, at `0x002d3c49`, simply returns that pointer.
The identified direct caller is `GS3DStuff::load(int, int&)`, at `0x002dfb15`.
Its stage-0 branch calls the getter at `0x002dfbf4` and passes the result to the
driver's `addMaterialRenderer(renderer, nullptr)` slot at `0x002dfbfe`. The
returned index is discarded; the function then returns the next load stage, 1.

`COpenGLESDriver::createMaterialRenderers()`, at `0x002d00e1`, installs 27 built-in
entries: 14 calls to `CNullDriver::addAndDropMaterialRenderer()` and 13 calls to
the driver's registration slot. Two additional indirect calls release shared
renderer references; they are not registrations. The built-in name table is at
`0x0047a9c8`, with indices 0 through 26.

`CNullDriver::addMaterialRenderer(...)`, at `0x002c28cd`, appends the renderer
and returns the resulting array size minus one at `0x002c2936` through
`0x002c2954`. The unnamed registration receives a built-in name only while the
previous count is at most 26 (`0x002c296a` through `0x002c297c`). This does not
establish a fixed numeric identity for later custom registrations.

GameSWF has a separate registration path. `render_handler_irrlicht`'s constructor
at `0x003cb7c5` searches for the name `MaterialRenderer_GameSWF`; if absent, it
registers that named renderer at `0x003cb9aa` and stores the returned index at
object offset `0x238`. The custom renderer's unnamed registration must not be
conflated with this named renderer or with an assumed index 27 or 28.

Executable literal references to GOT offset `0x14f0` were found in the getter,
`Application::Init`, and `Application::Quit`. No callable pointer to the getter
was found in allocated data outside symbol metadata. These checks and the
identified caller establish the registration path; they are not a proof that
every possible indirect material-type assignment is absent. No exact custom
selection site, selected retail material, or stable custom enum is claimed.

The custom renderer's established behavior remains:

| Method | Address | Observed behavior |
| --- | --- | --- |
| `isTransparent()` | `0x002d6ab9` | Returns false. |
| `getVertexAttributeMask()` | `0x002d6abd` | Returns `0x15`. |
| `onSetMaterial(...)` | `0x002d6af1` | Binds texture stage 0, applies basic render states, sets `GL_DECAL` (`0x2101`). |
| `onUnsetMaterial()` | `0x002d6bcd` | Restores `GL_MODULATE` (`0x2100`). |

## Collada material fixups and original selection

The on-disk 64-byte `irr::collada::SMaterial` record must not be treated as a
native pointer-containing structure. `CResFileManager::postLoadProcess(...)`, at
`0x002ae0a9`, explicitly resolves its indices in the material loop at
`0x002ae50a` through `0x002ae568`:

| Record offset | On-disk value | Original post-load value |
| --- | --- | --- |
| `0x18` | Effect index | Effect table base plus index times 88 (`0x002ae544` through `0x002ae54e`). |
| `0x24` | Image index or `0xffffffff` | Image table base plus index times 20, or null (`0x002ae516` through `0x002ae520`, `0x002ae550` through `0x002ae558`). |
| `0x28` | Image index or `0xffffffff` | Image table base plus index times 20, or null (`0x002ae522` through `0x002ae536`, `0x002ae55a` through `0x002ae564`). |

The loop does not reinterpret or replace the scalar selectors at `0x1c` and
`0x30`. The subsequent effect loop resolves effect image lists and sampler
references separately. Duplicated image-list entries do not, by themselves,
select the lightmap or reflection renderer.

`CMaterial::CMaterial(...)`, at `0x0029d2c1`, constructs an inline video
`SMaterial` at object offset `0x20` using the original video material constructor,
then invokes `CMaterial::prepareMaterial(...)`, at `0x002a0905`.
The inline video material starts at type 0. Its type word is at outer object
offset `0x24`; do not confuse this with the source record's image slot at `0x24`.

The original built-in names below are read from the library's own name table,
not copied from another Irrlicht version:

| Type | Original name | Selection in `prepareMaterial()` |
| --- | --- | --- |
| 0 | `solid` | Constructor default when no later selector changes it. |
| 3 | `lightmap` | Effect byte `0x08` equals 1; the late branch at `0x002a0a92` through `0x002a0a9a` enters `0x002a0cba` and writes 3 at `0x002a0cc8`. It also binds the first image from that effect parameter to stage 1. |
| 12 | `reflection_2layer` | Source record image pointer `0x28` is non-null and source scalar `0x30` is not 1 (`0x002a09ac` through `0x002a09ca`). |
| 13 | `trans_add` | Source scalar `0x1c` is nonzero (`0x002a0a3c` through `0x002a0a52`). This overrides the earlier reflection/alpha selection. |
| 14 | `trans_alphach` | With no reflection image, effect byte `0x30` equals 1, source scalar `0x30` equals 1, or effect float `0x38` compares unequal to `1.0f` (`0x002a0e1a` through `0x002a0e4c`). |
| 17 | `trans_reflection_2layer` | Source image pointer `0x28` is non-null and source scalar `0x30` equals 1 (`0x002a0e06` through `0x002a0e16`). |

Selection order matters. Reflection or alpha-channel selection precedes the
additive override; the lightmap branch is later still and can replace either.
These are initial construction rules. Repeated preparation and animated material
updates need separate validation rather than assuming a reset to type 0.

`trans_alphach_ref` is type **15**, not type 14. The original game explicitly
assigns 15 to selected placed-city textures through
`GS3DStuff::SetMaterialTypeCondition(...)`, at `0x002dc41d`. Its recovered body
compares only `layers[0].texture`. `GS3DStuff::loadCityPart(...)` and `AddXRef(...)`
apply it to `props_alpha.tga` and `props_alpha_01.tga`. These game-level overrides
must be applied after resolving the actual instance material and its first
texture. Their existence does not justify treating every textured transparent
effect as alpha-reference material.

## Supplied archive results

An offline enumeration covered all 359 BRES files and 1,366 material records.
All material effect indices resolved and agreed with their effect URL names.
Every material image index at `0x24`/`0x28` was either a valid image-table index
or `0xffffffff`. All `0x28` image indices were absent, and all effect bytes at
`0x08` were zero. Thus the supplied catalogue has no initial reflection or
lightmap selections under the branches above.

| Initial mode before game-level overrides | Material records |
| --- | ---: |
| 0, `solid` | 1,178 |
| 13, `trans_add` | 19 |
| 14, `trans_alphach` | 169 |
| 3, 12, 17 | 0 |

These are catalogue record counts, not visible mesh-buffer or draw-call counts.
They do not account for later game overrides, instance substitutions, animation,
or missing referenced images. Of the 169 alpha-channel selections, 168 take the
effect-byte `0x30` condition and one takes the opacity comparison. The latter is
`health_crate.bdae#props_00`, whose effect float `0x38` is zero. A total of 169
effects have byte `0x30` equal to 1, but `p_fire_00.bdae#p_fire_00` is subsequently
overridden to additive mode. Matching aggregate counts therefore do not validate
the old alpha-test interpretation.

The 19 additive records include the checkpoint models, collectible/badge glow,
car light models, `cop_lights.bdae`, `notsoinvisiblewall.bdae`,
`p_explosion_00.bdae`, and `p_fire_00.bdae`.

`hospital_logo_00.bdae#hospital_logo_00_mat` initially selects type 14. Its effect
image list starts with `props_alpha_02.tga` and then `props_alpha_01.tga`.
The second entry does not satisfy the original layer-0 city override. A prior
claim that this base material is inherently alpha-reference is unsupported.
Any placed instance that substitutes another material still requires its own
resolved selection check.

## Original blending and texture base format

`CCommonGLMaterialRenderer_TRANSPARENT_ALPHA_CHANNEL::onSetMaterial(...)`, at
`0x002cd64d`, disables texture stages from 1 upward and binds stage 0. Its texture
combiner modulates texture and previous RGB but replaces output alpha with the
texture alpha. At `0x002cd79a` it calls
`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`, then enables `GL_BLEND`.
This method does not call `glAlphaFunc`. The PLT entries resolve to `glBlendFunc`
at `0x001c4928`, `glEnable` at `0x001c473c`, and `glTexEnvi` at `0x001c488c`.
This is a different rendering operation from the native shader's current
cutoff-only world-material path.

`CCommonGLMaterialRenderer_TRANSPARENT_ADD_COLOR::onSetMaterial(...)`, at
`0x002cbfc5`, selects `GL_MODULATE` and uses
`glBlendFunc(GL_SRC_ALPHA, GL_ONE)`. Neither blending rule should be replaced by
a fabricated generic transparency mode.

For the custom renderer, OpenGL ES 1.1 Table 3.15 defines `GL_DECAL` as follows:
RGB textures replace the incoming RGB; RGBA textures interpolate incoming and
texture RGB by texture alpha. Both retain incoming alpha. The table leaves
alpha-only and luminance variants undefined for this function. The incoming color
is the color entering the texture stage, not automatically an unlit mesh color.
[Khronos OpenGL ES 1.1 specification, section 3.7.12, Table 3.15](https://registry.khronos.org/OpenGL/specs/es/1.1/es_full_spec_1.1.pdf).

The original texture-format evidence is concrete:

| Original input or image format | Original GL format/base format |
| --- | --- |
| DDS `ATC ` | `0x8c92`, ATC RGB |
| DDS `ATCA` | `0x8c93`, ATC RGBA with explicit alpha |
| DDS `ATCI` | `0x87ee`, ATC RGBA with interpolated alpha |
| Video `ECOLOR_FORMAT` 0 or 3 | `GL_RGBA` (`0x1908`) |
| Video `ECOLOR_FORMAT` 1 or 2 | `GL_RGB` (`0x1907`) |
| Video `ECOLOR_FORMAT` 4 | `GL_ALPHA` (`0x1906`) |

`loadATCTexture(...)`, at `0x002ce4c5`, compares the DDS FourCC at
`0x002ce508` through `0x002ce51e` and selects those compressed tokens in the
branches at `0x002ce520`, `0x002ce5d4`, and `0x002ce5e4`. Khronos explicitly maps
the RGB compressed token to base RGB and both alpha tokens to base RGBA.
[Khronos AMD_compressed_ATC_texture extension](https://registry.khronos.org/OpenGL/extensions/AMD/AMD_compressed_ATC_texture.txt).

`CCommonGLTexture::processColorFormat(...)`, at `0x002ce441`, implements the
uncompressed mapping through the jump table at `0x00412ef0`. The supplied
archive's 74 DDS files are all `ATCA`; their original compressed base format is
therefore RGBA. Its other textures are 22 BMPs (20 at 32 bits and two at 24 bits)
and two PNGs (one color type 6, one color type 2).

The original BMP loader constructs image format 2 for 24-bit input and format 3
for 32-bit input (`0x002bcb04`, `0x002bcac8`). Texture creation can subsequently
change the format: `getBestColorFormat(...)`, at `0x002ce031`, honors creation
flags; its flag-32 branch maps image formats 0 to 1 and 3 to 2. Thus a container's
bit depth alone does not prove the final GL base format. The native BGRA pixel
storage is likewise not evidence of the original RGB/RGBA choice.

## Implementation dependencies and validation boundary

A focused follow-up should preserve original initial material mode and selector
fields in the native BRES/material resolver, then propagate the selected mode
through `CitySurface` to Vulkan. Begin with modes 0, 13, 14, and the independently
evidenced game override 15. Reserve `city_assets.h/.cpp`, `city_view.h/.cpp`,
`vulkan.cpp`, the city shaders, and the affected native tests before editing.
Review existing effect-specific marker/light pipelines so their original material
selection is shared rather than implemented a second time. Do not infer mode
from texture alpha values, renderer names, or the mere presence of vertex colors.

Validation should cover additive precedence, the exact type-14 selector, the
layer-0-only type-15 override, the hospital material, the fire additive override,
and the health-crate opacity selector. Check real blend factors, texture-alpha
output, depth-write state, transparent-pass ordering, and alpha-reference state
against their original methods before claiming visual equivalence. Modes 3, 12,
and 17 have identified selection branches but no occurrences in this supplied
catalogue; they need independent format/subsystem fixtures and renderer-state
coverage rather than invented retail examples.

The custom `GL_DECAL` path remains blocked on an identified selection site. Its
known shader equation alone does not justify routing any existing surface to it.

Checks performed were original ELF symbol/GOT inspection, disassembly of the
registration/selection/format paths, strict offline BRES index and selector
enumeration, texture-header inventory, and primary-specification review. Local
evidence is retained only under ignored `port/build/OG-004-*.asm` and
`OG-004-material-inventory.json`; retail binaries or asset contents are not added
to the repository. No build or runtime test was required for this documentation
change, and no manual gameplay was performed. Rendering parity and a complete
search of all indirect or animated selection paths remain unverified.
