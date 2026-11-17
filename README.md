# OpenGangstar

OpenGangstar brings Gangstar game projects together in a desktop launcher.
The launcher uses a game library and a one-click build, following the
[OpenRAC Launcher](https://github.com/OpenRAC/OpenRAC/tree/develop/launcher).

The [Windows port build](port/README.md) targets 32-bit x86 and uses Vulkan.

## Set up the checkout

From the OpenGangstar repository root:

```sh
git -c core.longpaths=true submodule update --init --recursive
git -C games/gangstar-wch apply ../../games/gangstar-wch-patches/0001-library-build.patch
```

Apply the patch once per clean game checkout for Android builds. The submodule
pins the upstream West Coast Hustle decompilation. The Windows target prepares
its patched sources during the [port build](port/README.md).

## Run the launcher

Run `python launcher` with Python and Tkinter. Select **Gangstar: West Coast
Hustle** and choose a platform.

### Windows

On Windows, the launcher opens with **Windows x86 (Vulkan)** selected. Click
**Play on Windows** to build the game if needed and launch it. Click **Build for
Windows** to compile it separately. Select **Android** from the platform menu
to access the Android build and play controls.
Play prompts for your retail game data ZIP if no path is set. Use **Browse
folder…** to select extracted game data. The launcher extracts ZIP assets for
the current session and remembers the selected path. See the
[Windows build requirements](port/README.md) and [port status](port/STATUS.md).

### Android

For **Android**, use
**Browse…** to select your own APK,
retail `libsanangeles.so`, or compiler directory. The launcher uses
`games/gangstar-wch/baselib/` and the game's normal toolchain settings by
default. Build inputs are remembered in the local checkout.

The launcher runs `tools/build.py --all --link-object`. It verifies the
configured source units, links eligible compiled sections into the retail
layout, and writes a byte-identical Android `libsanangeles.so` under
`games/gangstar-wch/build/library-link/`. The other sections come from
your local retail library. If you select an APK, the launcher also writes
`gangstar-wch.apk` in the same folder. The APK matches the input archive
byte for byte, preserving its original signatures. Run it with your
game's original assets on a compatible Android device. See the game's
[build instructions](games/gangstar-wch/docs/BUILDING.md) and
[command-driven build plan](games/gangstar-wch/docs/COMMAND-BUILD-PLAN.md).

### Android game data

Select **Game data ZIP (optional)** before building to prepare
`gangstar-wch-data.zip` next to the APK. Extract this ZIP into Android's
shared storage root, typically `/sdcard/`. The prepared archive includes
game assets and leaves saved games and preferences in your original ZIP.

The retail Android installer expects downloaded data under
`/sdcard/gameloft/games/gangstar/`. Version 3.5.0 checks resource sizes
and selects the ATC texture set when Android reports manufacturer `HTC`.
Use the asset set that matches the device profile. On BlueStacks
Nougat 32-bit, the generated APK installed and reached the retail
installer. A non-HTC profile asked for more data with the HTC asset set.
### Play on Android

After building from an APK, connect one compatible Android device or emulator
through ADB and click **Play on Android**. The launcher installs the built APK
and deploys the prepared asset ZIP if the game data folder is absent. It checks
the transferred file list and sizes before moving the files into place. It reuses
an identical installed APK and preserves any existing game data. The status
distinguishes the retail installer from the actual game activity.
Set `ANDROID_SERIAL` before starting the launcher if several devices are listed.
