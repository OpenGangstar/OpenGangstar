# OpenGangstar

OpenGangstar brings Gangstar game projects together in a desktop launcher.
The launcher uses a game library and a single primary action, following the
[OpenRAC Launcher](https://github.com/OpenRAC/OpenRAC/tree/develop/launcher).

## Set up the checkout

From the OpenGangstar repository root:

```sh
git submodule update --init --recursive
git -C games/gangstar-wch apply ../../games/gangstar-wch-patches/0001-library-build.patch
```

Apply the patch once per clean game checkout. The submodule pins the upstream
West Coast Hustle decompilation, and the patch carries the OpenGangstar build
integration. The moved local checkout already has the patch applied.

## Run the launcher

Run `python launcher` with Python and Tkinter. Select **Gangstar: West Coast
Hustle** and click **Build & verify**. Use **Browse…** to select your own APK,
retail `libsanangeles.so`, or compiler directory. The launcher uses
`games/gangstar-wch/baselib/` and the game's normal toolchain settings by
default.

The launcher runs `tools/build.py --all` and writes verification images
under `games/gangstar-wch/build/matches/`. The patch also preserves the
experimental `--link-object` build. Its compiled-section link currently
fails with duplicate definitions. Producing a playable build is still in progress. See the game's
[build instructions](games/gangstar-wch/docs/BUILDING.md) and
[command-driven build plan](games/gangstar-wch/docs/COMMAND-BUILD-PLAN.md).
