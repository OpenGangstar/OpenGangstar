# PC audit and x86-64 migration

Completed on 2026-11-24 against OpenGangstar baseline bb3b826.

## Audit scope and decisions

The audit indexed and hashed all 3,041 tracked files, including 2,969 files in
the ARM decompilation submodule. It examined the launcher, native source and
headers, CMake targets and staged source patches, linked dependencies,
platform interfaces, asset readers, save serialization, and tests. Searches
covered pointer casts, allocation sizes, structure-size assertions, assembly,
architecture flags, repeated implementations, and source ownership.

- Consolidated identical WaveOut error checks from the movie and game audio
  implementations into windows_audio.h. Both callers retain their operation
  text and exception behavior. Removed their redundant Windows includes.
- Removed base/dlmalloc.c from the GameSWF build. USE_DL_MALLOC is unset and
  GameSWF's allocation macros use the C runtime. A complete relink validates
  that the allocator object is unnecessary.
- Kept format-specific readers. BRES, big-endian script records, little-endian
  constants and saves, sprite records, and UTF-8 tables have different framing,
  bounds, and error behavior.
- Kept the individual mission tracks and original matching implementations.
  Similar game code is insufficient evidence of interchangeable behavior.
- Existing VorbisStream, ResourceFileSystem, localized text loading, mission
  execution, and vehicle binding remain the authoritative shared facilities.
  No additional engine layer or gameplay implementation was introduced.

## Architecture changes

- CMake requires eight-byte pointers and the MSVC x64 architecture. Configure
  with -A x64. The launcher now builds and launches port/build/x64/Release/
  gangstar-wch.exe and labels the platform Windows x86-64 (Vulkan).
- Startup asserts the native pointer width. Vulkan diagnostics identify x86-64.
- The Windows-only StateMachine source patch uses size_t for pointer
  differences, allocation counts, and copy indices.
- GameSWF's two debug assertion sites use __debugbreak() instead of x86 inline
  assembly. Assertions remain enabled in Debug.
- Pointer truncation diagnostics C4302, C4311, and C4312 are errors for the
  native game, assets library, and GameSWF targets.
- Asset and save fields retain their fixed widths and endianness. Win32 API
  names remain appropriate on Windows x64. Existing LONG_PTR window storage,
  typed callbacks, bounded decoder buffer sizes, and explicit serialization
  avoid native structure-layout dependence.

The old port/build/win32 directory and its caches were removed after baseline
comparison. The x64 build was configured in a fresh directory and rebuilt with
--clean-first. Dependencies were compiled from the existing pinned sources.

## Validation

- Clean x64 Release build and x64 Debug build succeeded.
- Clean Release compiler/linker output contains no warnings or errors.
- All five registered tests passed in both Release and Debug.
- Retail collision/vehicle integration and muted audio/radio integration passed.
- Native startup and Vulkan rendering passed. Existing automated checkpoint
  smoke checks for Scripts 19, 20, 32, 33, 36, and 37 passed with isolated
  saves. The supplied checkpoint bytes remained unchanged.
- The launcher's actual Build and Play buttons passed on Windows with retail
  ZIP input, including platform switching and native startup.
- The game and all seven test/audit executables have PE32+ optional headers
  (0x20b) and AMD64 machine headers (0x8664).
- All seven generated Release static libraries contain AMD64 objects,
  including zlib, GameSWF, Ogg, Vorbis, Vorbisfile, and Vorbisenc.
- A running game process loaded 114 modules. Every loaded PE image, including
  the Vulkan loader, GPU driver, and Windows media components, was AMD64.
- SHA-256 comparisons found no changes in any of the 2,969 tracked ARM files.
  The submodule pin remains 08e4fc11d8457ac40c502f3b0b2e966a20247293.
  Existing local submodule edits remain present. All 83 ARM tooling tests pass.
  A new complete ARM byte-matching build was not performed.

## Remaining limitations

The supplied retail asset set still fails the comprehensive asset audit.
The x86 baseline and x64 audit outputs are identical. All 359 BRES scenes,
98 listed images, 274 indexed sounds, 79 text/metadata tables, and 14 constant
tables decode. The audit reports 111 distinct unavailable referenced texture
paths and 560 of 730 resolved BRES image references. These existing coverage
gaps remain documented in STATUS.md.

The game port remains incomplete. Mission and subsystem limitations in
STATUS.md continue to apply, including remaining mission implementations,
approximate targeting and collision behavior, passenger handling, and
unsupported rendering behavior. This migration proves native x64 execution
and preservation of the exercised behavior, with validation through automated
checks and short smoke runs.
