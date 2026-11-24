# Native image loading

The native loader dispatches by file signature. It supports BMP, PNG, JPEG, PCX,
P1-P6 PBM/PGM/PPM, TGA, PSD and retail ATC DDS mip chains. The automated retail asset audit enumerates
`.bmp`, `.tga`, `.psd`, `.png`, `.dds`, `.jpg`, `.jpeg`, `.pcx`, `.pbm`, `.pgm` and `.ppm`; no gameplay progression
is needed to find or decode those resources.

## JPEG source and behavior

JPEG uses the Independent JPEG Group's original release **6b, 27-Mar-1998**,
matching the retail decoder's embedded version and copyright strings. CMake
fetches the [official IJG archive](https://www.ijg.org/files/jpegsrc.v6b.tar.gz),
pinned to SHA256
`75c3ec241e9996504fe02a9ed4d12f16b74ade713972f3db9e65ce95cd27e35d`.
Existing GameSWF dependencies contain a JPEG integration wrapper, with JPEG
linkage disabled in this project, and do not supply the required codec source.

The fetched library sources are unmodified. Its complete 46-file library list
from `makefile.vc` is built, including compressor, decompressor, progressive,
IDCT and quantization modules. The supplied `jconfig.vc` is copied into the build
tree as `jconfig.h`. The `jmemnobs` backend retains the original `jmemirr` policy:
report the requested memory available, allocate from the host heap, and reject
backing-store requests. Native allocation uses the Windows C runtime instead of
the original Irrlicht allocator. No unsupported format is replaced with pixels
or an empty implementation.

The original `CImageLoaderJPG::loadImage` at `0x2bcf11` explicitly requests
`JCS_RGB`, three output components, and `do_fancy_upsampling=FALSE` at
`0x2bcfc6..0x2bcfd0`. Those options and the release's ordinary integer IDCT are
preserved. Grayscale, RGB and YCbCr decode to top-down RGB24; the native texture
adapter converts that buffer to opaque BGRA storage. The original default driver
conversion uses an RGBA upload, represented by `TextureBaseFormat::rgba`, distinct
from the JPEG source's RGB format. Original CMYK/YCCK-to-RGB requests fail in
`jinit_color_deconverter` at `0x1edd36`; native IJG raises the same error27.

The withdrawn Windows Imaging Component JPEG candidate produced different chroma
interpolation. For the independent 17x11 test image it differed on 186/187 pixels,
with channel differences up to76. The original loader's false upsampling flag is
the proven cause: changing only that flag in the original reference experiment
reproduces the WIC pixels exactly. The native 6b implementation instead matches
the unchanged original's entire187pixel output. PNG retains its existing WIC path.

## Native boundary and malformed input

The C-only `jpeg_decode.c` owns the complete `setjmp`/`longjmp` lifetime. No jump
crosses C++ destructors. Decoder, error, source and output ownership state are on
the heap; modified automatic variables are not read after recovery. All fatal
paths destroy the IJG state and release partially allocated pixels. Successful
RGB ownership transfers to the C++ adapter, whose guard releases it if image
allocation/conversion throws. Nonfatal original decoder messages are formatted
and logged to stderr, with the IJG warning count also available to validation.

Compile-time assertions retain 8-bit samples, 32-bit `INT32`/Windows `long`, and
full native `size_t`/pointer width. The original Windows configuration uses flat
pointers and the historical allocator naturally aligns native pointer-bearing
objects; no ARM layout casts or 32-bit pointer truncation are used.

The native encoded-file limit is64MiB and dimensions are limited to4096 per axis,
consistent with the existing texture loader's supported range. These are explicit
native bounds, not original IJG limits.

The original init/term source callbacks are genuinely empty. Its refill callback
at `0x2bce89`, however, returns TRUE without supplying new bytes, and its positive
skip callback subtracts unchecked. Truncated input can therefore read beyond the
original buffer. The earlier reference harness observed unknown-marker error68
on deterministic zero-filled adjacent memory; that is not proof of a safe or
guaranteed original EOF outcome. Native refill/overlong skip instead raise
`JERR_INPUT_EOF`43. No synthetic EOI marker or missing pixel tail is inserted.
The codec consequently rejects missing EOI even when an APP payload contains
fake `FFD9`, and permits trailing data after a real decoded EOI. An early actual
EOI can still trigger the original nonfatal entropy recovery and warning.

## Validation and limits

Release and Debug texture tests exercise independently authored baseline gray,
YCbCr, Adobe RGB and progressive coefficient fixtures; a colored YCbCr reference;
the nonconstant odd-sized image with real AC/subsampled chroma; CMYK/YCCK failure;
truncation, precision/dimension limits, EOI framing, repeated recovery, BGRA
conversion and the existing PNG/other-format regressions.

An ignored original-instruction harness executes actual retail JPEG header,
entropy, progressive, IDCT, upsampling and color conversion code up to CImage
construction. Native comparisons cover15 fixtures: all199 accepted RGB pixels
across seven images are byte-identical in both configurations, including the
nonfatal early-EOI image and warning count. Five fatal color/precision/dimension
errors agree; two EOF cases use the explicit bounded adaptation, and width4097
uses the native dimension bound. This is fixture-level evidence, not a claim of
exhaustive numerical equivalence for every possible JPEG or an unmodified retail
engine. Arithmetic-coded, hierarchical and lossless processes unsupported by
the historical release remain unsupported. There is no game/emulator runtime
dependency and no manual mission validation.

The supplied retail ZIP contains no JPEG entries. Its98 enumerated images still
decode successfully; the audit retains its existing111 distinct unavailable
texture paths and2908/2910 resolved district bindings. Those unrelated missing
resources remain work to complete. Adding two generated temporary `.jpg`/`.jpeg`
files to an extracted validation copy raises the successful image count to100
without changing the missing-path diagnostics.

## IJG notice and distribution

This software is based in part on the work of the Independent JPEG Group.

The complete unaltered IJG README, including its copyright and no-warranty terms,
is retained from the fetched source and copied to `IJG-README.txt` beside the
Windows game executable after linking. Keep that notice and this attribution
with binary distributions. If distributing IJG source, include its unaltered
README and clearly document changes; this integration changes no upstream source
files and supplies its own C wrapper and build configuration. The release's
`ansi2knr.c` utility is not part of the compiled library.


## PCX source and behavior

The PCX signature probe at 0x002bd2cd recognizes manufacturer byte 0x0a. The
original loadImage at 0x002bd345 accepts only bits-per-pixel 8 and 24. It ignores
version and always executes its RLE loop, including when encoding is zero.
Eight-bit images read the final 768 bytes as an RGB palette without checking a
palette marker, then convert through opaque A1R5G5B5 at 0x002b968d. Native output
uses the already verified original bit-replication upload expansion. Palette
precision loss is intentional original behavior.

The literal 24-bit path uses convert24BitTo24Bit at 0x002b9dad without channel
swapping or vertical inversion. Both paths retain the original decoded size
`height * planes * bytesPerLine * (bits / 8)` and absolute row padding
`abs(bytesPerLine - planes * width * (bits / 8))`. In particular, 8-bit multi-plane
files follow the original indexed path; they are not reinterpreted as planar
RGB. The unusual literal 24-bit layout is not a claim that the original engine
correctly supports every modern PCX encoding. Header origin affects dimensions,
not a placement offset.

RLE runs can cross rows. A zero-length run consumes its value and continues;
encoded bytes with high bits 11 are escaped in the ordinary way. Native checks
reject truncated headers, palettes and runs, allocation overflow, decoded run
overflow and row access outside the allocated buffer. Original unchecked reads
and writes for those malformed inputs are not reproduced. Dimensions remain
limited to 4096 per axis; encoded files and decoded buffers are limited to
64 MiB. Original loadImage can accept a different manufacturer when explicitly
selected by filename and encoding is one; the signature-only native API uses
the original manufacturer probe. When PCX loading fails, the existing TGA
fallback still accepts a TGA whose comment length is ten.

Root Release and Debug tests cover all 256 original palette outputs, padding,
RLE across rows, zero-length runs, ignored header fields, multi-plane indexed
behavior, literal 24-bit rows and malformed-input bounds. Actual original
loadImage and both original color converters were executed for reference;
278 accepted image pixels match each native configuration exactly. Four original
unsupported bit depths are rejected. The reference-only harness supplies file
I/O, allocations and a CImage storage/lock capture; it does not replace the PCX
RLE or color-conversion algorithms. Default A1R5G5B5 upload expansion reuses the
separately verified bitmap conversion contract. No retail PCX is present in the
supplied asset inventory; generated fixtures establish format coverage without
claiming otherwise.

Remaining original loader work includes WAL and DDS variants beyond the
supported retail ATC family. Missing retail texture paths remain asset inventory
findings rather than substituted images.

## P1-P6 source, tokens and rendered channels

Original `CImageLoaderPPM::isALoadableFileFormat` at `0x2bdc91` recognizes
uppercase P1 through P6. Its filename probe at `0x2bdcc1` uses case-sensitive
substring checks for `.ppm`, `.pgm` and `.pbm`; it accepts `image.ppm.backup`,
but not `.pnm` or uppercase `.PPM`. The native public API remains signature-based;
the audit enumerates the three conventional original suffixes.

The loader at `0x2bdf1d` rejects files of 11 bytes or less. Header/sample token
behavior follows the actual `getNextToken0x2bddf9` and
`skipToNextToken0x2bdd6d`: whitespace is space, tab, LF, CR, form feed and vertical
tab. A `#` comment ends at CR/LF or EOF. Inside a token it is removed and the
token continues; `17#comment\n3` becomes `173`. Numeric fields use their initial
decimal digit prefix; a nonnumeric first byte yields zero. Maximum values over
255 reject, but zero/nonnumeric maximum values are accepted. No maximum-value
scaling occurs. ASCII samples retain their low byte, including values above255.
P1 sets a bit only for the literal C-string `1`; other tokens leave it zero.

After the header the original skipper also runs on binary data. Leading pixel
whitespace is discarded, and a leading `#` consumes a comment. This is preserved
rather than replaced with a standards-only single-separator rule. NUL is not
whitespace. The original ASCII remaining-byte preflight is retained: at least
two bytes per P1/P2 pixel, or six bytes per P3 pixel, before token processing.
An otherwise sufficient final undelimited token can therefore fail that check.

P2/P3/P5/P6 create original format3 images. Original loader bytes reach the
actual same-size `CImage` copy through `getImageData0x2cee05` unchanged under
default flags0x12. Actual `getBestColorFormat0x2ce031` chooses3 and
`processColorFormat0x2ce441` selects `GL_RGBA`/`GL_UNSIGNED_BYTE`. Thus byte0 is
rendered red and byte2 blue, irrespective of the separate CPU SColor byte
representation. Native converts those rendered channels to BGRA storage, with
opaque alpha. P1/P4 use original format0 and `convert1BitTo16Bit0x2b95dd`;
their black/white values are opaque0x8000/0xffff and reuse the verified555 upload
expansion. No vertical inversion or sample normalization is introduced.

## PBM allocation repair and checked input

The original P1/P4 branch accidentally reduces its allocation/read count to the
boolean `((pixelCount & 3) + (pixelCount >> 3)) != 0`. That gives only zero or
one byte. Actual memory-access probes show P1 writes beyond it and the original
row converter reads beyond it for larger images; even width4,height1 allocates
zero. Those heap-dependent outputs are not valid pixel references.

Native deliberately repairs only these allocation/read spans. The ASCII packing
span is `ceil(pixelCount/8)`; the converter span is
`height * ceil(width/8)`. P1 allocates the larger span and zeroes it, matching
the original initialization operation on a safe allocation. It still packs
the entire token sequence continuously, LSB first. The original row converter
still consumes each row MSB first and advances past partial row bytes. Thus
odd-width ASCII PBM retains the original packing/converter mismatch; it is not
silently repacked to a standard PBM layout. Unwritten bytes in the expanded
ASCII allocation are explicitly initialized storage, not invented sample tokens.
Missing required tokens reject.

P4 reads the complete converter row span from actual file bytes; short rows
reject. It neither supplies a zero pixel tail nor reuses adjacent heap contents.
Its set bits produce white as in the original converter, including the original
polarity difference from standard PBM. Safe original single-row cases match
unchanged original code. Previously unsafe larger/width4 cases are compared
separately against original execution with only its allocation/memset/read count
repaired. No parity with the original out-of-allocation contents is claimed.

Encoded files and BGRA output are limited to64MiB, dimensions to4096 per axis.
Zero dimensions, decimal uint32 overflow, missing tokens and short binary
payloads reject. These checked native bounds also avoid original wrapped
allocations, empty-token zero tails and unsafe reads; they are explicit malformed
input adaptations. There are no unbounded retry loops or changes to WAL/DDS.

Ignored reference fixtures use original token, packing, converter and actual
CImage/copy/format-selection execution. File I/O includes signed relative seek-1;
system memory and CRT boundaries are explicit shims. PBM guard diagnostics
separate unsafe original cases from unchanged-code pixel comparisons. Native
tests cover every P1-P6 path, comments, all whitespace bytes, binary first-byte
rules, row padding, max values, byte wrapping, rendered RGB, alpha and checked
malformed inputs. Generated `.pbm`/`.pgm`/`.ppm` files also pass through the real
retail audit extension inventory. Reference tooling is not part of the game.

Release and Debug full builds and all47 CTests pass. Across54 reference fixtures,
each native configuration matches1,095 pixels from unmodified original code and
seven original rejections. Eight guarded unsafe PBM cases are separately checked
against the allocation/readcount-repaired original, matching70 pixels. The
actual original CImage/copyTexture path additionally confirms bytes at its
GL texture upload call; GL execution itself is outside that reference boundary.
The supplied ZIP contains no PBM/PGM/PPM files. Its98 images still decode, with
the exact same111 missing texture paths and zero opened-image decode failures.
Adding six temporary P1-P6 files under the three original suffixes raises the
successful audit inventory to104 without changing those missing paths.
