# Native image loading

The native loader dispatches by file signature. It supports BMP, PNG, JPEG, TGA,
PSD and retail ATC DDS mip chains. The automated retail asset audit enumerates
`.bmp`, `.tga`, `.psd`, `.png`, `.dds`, `.jpg` and `.jpeg`; no gameplay progression
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
