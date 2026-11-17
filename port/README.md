# West Coast Hustle on x86 Windows

The Win32 target builds a native executable from port code and portable C++
from the West Coast Hustle submodule. The executable checks the game data
header and presents the Gameloft logo, intro video, and retail splash in a
native Win32 window using a Vulkan swapchain. Click the splash to play the
retail fade into the menu artwork. The primary button clips respond to the
mouse. New Game loads the retail city assets and renders the opening Santa
Monica camera moves and fades in Vulkan. The remaining intro script actions,
lighting, and gameplay still need porting. See the [port checklist](STATUS.md)
for coverage across the original code and assets.

Install Git, CMake 3.24 or newer, Visual Studio with the Desktop development
with C++ workload, and the Vulkan SDK headers and `glslc`. Running the executable requires a GPU
driver that exposes 32-bit Vulkan and Windows Media Foundation for the intro
decoder. The build uses the Windows Vulkan loader
at runtime, so it can use headers from an SDK that ships only x64 libraries.

Initialize the game checkout once. In the launcher,
choose **Windows x86 (Vulkan)**, select your retail game data ZIP or extracted
folder, and click **Play on Windows**. Play builds the executable if needed.
Use **Build for Windows** to compile separately. The commands below
show the setup, command-line build, and run from the OpenGangstar repository root.

```powershell
git -c core.longpaths=true submodule update --init --recursive
cmake -S port -B port/build/win32 -A Win32
cmake --build port/build/win32 --config Release
& .\port\build\win32\Release\gangstar-wch.exe 'C:\path\to\gameloft\games\gangstar'
```

Pass the extracted data directory containing `Version_pycst.bin`, `system_pycst.bin`,
`gameloft_logo_01.bsprite`, `gameloft_logo_01.bmp`, `intro.mp4`, `newmenu.swf`, and
the four bitmaps exported by that SWF.
Append `--smoke-test` to present the logo, first video frame, and splash
without waiting through the intro.
Use `--opening-smoke-test` to also load New Game, render the startup
cinematic with muted audio, and check the first dialogue portrait. It then
submits fixed vehicle objective and explosion scenes to Vulkan and exits without enabling
player control.
Click Skip or press Esc during the New Game startup cinematic to move to
the first mission's opening.
Use `--language english`, `french`, `german`, `italian`, `japanese`, or
`spanish` to select the retail menu and dialogue language. English is the
default. Keep `fontall.ttf` in the resource directory for Japanese menu glyphs.
Windows uses `%LOCALAPPDATA%\OpenGangstar\Saves` for writable save files.
Use `--save-dir <folder>` to override it with a directory outside the retail
game data. Completing the opening mission writes `OpenGangstar.sav` there.
**Continue** resumes from that handoff. Later checkpoints and resuming
arbitrary retail saves are still pending.
The port builds without the Android APK or ARM toolchain.
CMake applies the Win32 compatibility patches to copies of the state-machine
and touch sources in the build directory.
CMake downloads and verifies pinned zlib, GameSWF, libogg, and libvorbis
source archives on the first build. The Win32 build uses static libraries.

To inspect a texture from the HTC asset set, append
`--texture 00906_exp.tga`. These files contain DDS images with ATC explicit
alpha compression. The port decodes the base mip level and displays its
transparency over a checkerboard. The same option opens uncompressed retail
bitmaps such as `gameloft_logo_01.bmp` and PNG assets using Windows Imaging
Component.

## Original startup

The Android JNI bridge calls `appInit` and `appRun`. The recovered startup
path creates an Irrlicht device, then calls `Application::InitWin32` to set up
the resource filesystem, touch input, and accelerometer before calling
`Application::Init`. The frame path ticks Irrlicht's timer before calling
`Application::Update`. The Windows port can replace the JNI bridge, but it
still needs those application methods and an Irrlicht graphics implementation
that routes drawing to Vulkan.

The startup logo uses frame zero from the original sprite atlas at the position
specified by `Application::LogoDraw`. The port uploads the atlas to a Vulkan
image and blits its sprite regions onto the swapchain image in draw order.
The recovered `StateMachine` dispatches the startup frame's update and draw
callbacks. The Win32 timer approximates the original 100-frame hold using the
game loop frame rate defined in `system_pycst.bin`.
On Windows the state stack uses native C++ allocation in place of the
Android STLport dependency.

Media Foundation decodes the original MPEG-4 video and AAC audio. Win32
plays PCM audio through the default output. Vulkan presents letterboxed video
frames against the audio playback clock. If the audio output is unavailable,
video uses the Win32 timer.

The startup path follows the `widgetSplash` placement in `newmenu.swf` to
its `show` and `hide` frames and resolves the bitmap shapes through the
movie's exports. The port rounds placement translations to the nearest pixel
on the 480 × 320 stage. The fade uses the SWF frame rate and opacity values.

After the fade, the single-threaded
[GameSWF core](https://github.com/killerdevildog/gameswf/tree/6385d75c9c02c0e4059ce8629854406c3fa9063d)
loads `newmenu.swf` and advances `widgetMainMenu`. The Win32 target excludes
the upstream SDL and OpenGL backends. The port shows the main-menu widget
and hides the other screens placed on the same SWF timeline. GameSWF binds
the four external retail bitmaps by their exported character IDs and preserves
their alpha channels.
The GameSWF adapter rasterizes menu shapes, bitmap fills, and masks on the CPU.
The SWF's HTML text fields can resolve the GROBOLDpro face from the retail
`font02.ttf` resource when it is present. With `menu.english` present, the main
buttons use the original English labels. Continue is disabled until the port
can apply saved game state. Windows rasterizes the glyphs, and Vulkan uploads and
presents the resulting frame.

New Game follows the asset references in `cityparts.array` and each
`WorldDescriptor`. The current Windows loader keeps the object maps and
decodes the position and index streams in the full- and low-detail BRES scenes.
It reads the visual scene hierarchy, node transforms, and geometry references.
With the supplied data it loads nine districts and thirteen scenes with
151 district meshes and resolves the building and prop references from
174 external geometry libraries. The city renderer transforms each mesh using
its scene node hierarchy and draws its indexed triangles with a Vulkan depth
attachment. It resolves diffuse materials through the BRES effect and image
tables, decodes packed UV coordinates, and samples the retail textures.
Alpha-tested surfaces retain their cutouts.

The first entry in `ScriptChapterStartup.array` selects the opening script in
`allscripts.gmap`. The port reads its `CamSet`, `Wait`, and fade commands, then
follows `UnlockScript` into the next script. Camera records come from
`santa_monica_gameplay.gmap`. The view interpolates between their positions and
targets and applies the retail fade durations. The linked script's `Enable`
and `MoveTo` commands place Juan, Pedro, and the Lincoln at their retail map
markers. The renderer follows each model's skin or morph controller back to
its bind-pose mesh. The opening conversation's `SetAnim2` commands play
gestures and idle loops on Juan and Pedro using `anim_ingame.bdae`. Both
actors use the retail idle loop from the moment they enter the scene.
The port blends the retail joint transforms on the CPU and sends the
skinned vertex ranges to Vulkan. When the selected chapter table is installed,
the `Dialog` commands display its subtitle text at the bottom of the scene.
Click inside the window to dismiss each opening subtitle and continue.
Actor animations keep playing while the script waits for the click.
After the conversation, retail subscript 84 fades out, hides Juan, resets
Pedro to idle, and fades into a third-person view. Use WASD or the arrow
keys to walk. The camera follows Pedro at a fixed angle, and movement
follows the retail Santa Monica height mesh at the speed of the retail
`move_walk` cycle. Walk into the retail checkpoint marker to complete the
movement tutorial. Approach the Lincoln and press E to enter. Use W/S to
accelerate, brake, or reverse, and A/D to steer. The arrow keys also work.
Right-drag to rotate or tilt the camera on foot or in a vehicle.
Double right-click while stopped to recenter the vehicle camera.
Use `[` and `]` to change radio stations. Slow down and press E to exit.
An XInput controller can use the left stick for movement and steering,
right and left triggers to accelerate and brake or reverse, A to enter
or exit a vehicle, B to skip a cutscene, and D-pad left or right to change
radio stations. Use the right stick to orbit the camera. Click it to
recenter the camera while stopped in a vehicle. In the retail menus, use
the D-pad or arrow keys to move focus, A or Enter to select, and B or Esc
to go back. On the sound page, use Up or Down to choose a slider and
Left or Right to adjust it in ten-point steps. Mouse dragging allows finer
adjustments.
Driving uses simplified handling on the
retail height mesh. After leaving the parking lot, follow the on-screen
arrow to the retail payphone checkpoint. Reaching it clears the marker.
Movement checks for walls using steep triangles from the retail level
geometry. The opening script can play and stop its retail Vorbis cues.
The payphone cutscene, vehicle animations, complete collision and physics,
decals, and the remaining audio system still need porting.

The Windows resource filesystem implements the recovered `FileSystemBase`
and `IFileStream` interfaces. Startup reads the version header and textures
through that filesystem using names relative to the extracted data folder.
The resource filesystem is read-only.

Win32 mouse presses, drags, releases, and capture loss feed the recovered
`TouchScreenBase` interface in landscape coordinates. The splash consumes
a new press after the intro finishes. After the fade, GameSWF receives pointer
events in stage coordinates and uses the retail button frame labels for focus,
selection, and release.
