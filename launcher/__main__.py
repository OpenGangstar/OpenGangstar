"""Run the OpenGangstar desktop launcher with `python launcher`."""

from contextlib import contextmanager
import hashlib
import json
from pathlib import Path, PurePosixPath
from queue import Empty, Queue
from shutil import which
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from zipfile import BadZipFile, ZipFile


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from launcher.android import play
from launcher.game_apk import package_apk, package_game_data, windows_game_data

SETTINGS = ROOT / ".launcher-settings.json"
GAME_ROOT = ROOT / "games" / "gangstar-wch"
WINDOWS_BUILD = ROOT / "port" / "build" / "win32"
WINDOWS_EXE = WINDOWS_BUILD / "Release" / "gangstar-wch.exe"
WINDOWS_PLATFORM = "Windows x86 (Vulkan)"
if not (GAME_ROOT / "tools" / "project.py").is_file():
    raise SystemExit("Initialize the game with git submodule update --init --recursive.")
sys.path.insert(0, str(GAME_ROOT))
from tools.project import BASE_LIB_NAME, BASE_LIB_SHA256


PAGE = "#191919"
RAIL = "#131313"
PANEL = "#24221e"
TEXT = "#e6e6eb"
MUTED = "#b4b4bf"
AMBER = "#f5c040"

# Each library entry maps to a build command relative to the checkout.
GAMES = {
    "Gangstar: West Coast Hustle": ("tools/build.py", "--all", "--link-object"),
}


@contextmanager
def retail_library(source):
    """Yield a local library path, extracting only the matching file from an APK."""
    if not source or Path(source).suffix.lower() != ".apk":
        yield source, None
        return

    with TemporaryDirectory(prefix="opengangstar-") as directory:
        path = Path(directory) / BASE_LIB_NAME
        with ZipFile(source) as apk:
            for entry in apk.infolist():
                parts = PurePosixPath(entry.filename).parts
                if len(parts) != 3 or parts[0] != "lib" or parts[2] != BASE_LIB_NAME:
                    continue
                digest = hashlib.sha256()
                with apk.open(entry) as library, path.open("wb") as output:
                    for chunk in iter(lambda: library.read(1024 * 1024), b""):
                        digest.update(chunk)
                        output.write(chunk)
                if digest.hexdigest() == BASE_LIB_SHA256:
                    break
            else:
                raise ValueError(f"The APK does not contain the supported {BASE_LIB_NAME}.")
        yield str(path), entry.filename


def load_settings():
    try:
        settings = json.loads(SETTINGS.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    if not isinstance(settings, dict):
        return {}
    return {name: value for name, value in settings.items() if isinstance(value, str)}


def save_settings(**updates):
    settings = load_settings()
    settings.update(updates)
    with TemporaryDirectory(prefix=".launcher-settings-", dir=SETTINGS.parent) as temporary:
        pending = Path(temporary) / SETTINGS.name
        pending.write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")
        pending.replace(SETTINGS)


class Launcher:
    def __init__(self, window):
        self.window = window
        self.running = False
        self.operation = "Build"
        self.events = Queue(maxsize=1000)
        settings = load_settings()
        self.retail = tk.StringVar(value=settings.get("retail", ""))
        self.assets = tk.StringVar(value=settings.get("assets", ""))
        self.toolchain = tk.StringVar(value=settings.get("toolchain", ""))
        self.windows_assets = tk.StringVar(value=settings.get("windows_assets", settings.get("assets", "")))
        self.status = tk.StringVar()
        self.title = tk.StringVar()
        platforms = (WINDOWS_PLATFORM, "Android") if sys.platform == "win32" else ("Android",)
        self.platform = tk.StringVar(value=platforms[0])
        window.title("OpenGangstar Launcher")
        window.geometry("1050x700")
        window.minsize(820, 580)
        window.configure(bg=PAGE)
        window.protocol("WM_DELETE_WINDOW", self.close)

        style = ttk.Style(window)
        style.theme_use("clam")
        style.configure("Build.TButton", background=AMBER, foreground=RAIL,
                        font=("Segoe UI", 11, "bold"), padding=(20, 10))
        style.map("Build.TButton", background=[("disabled", PANEL), ("active", "#ffe08f")],
                  foreground=[("disabled", MUTED)])
        style.configure("Secondary.TButton", background=PANEL, foreground=TEXT,
                        font=("Segoe UI", 11, "bold"), padding=(20, 10))
        style.map("Secondary.TButton", background=[("active", "#37342d")],
                  foreground=[("disabled", MUTED)])
        style.configure("Browse.TButton", background=PANEL, foreground=TEXT, padding=(10, 4))
        style.map("Browse.TButton", background=[("active", "#37342d")])
        style.configure("Path.TEntry", fieldbackground=RAIL, foreground=TEXT,
                        insertcolor=TEXT, padding=6)

        header = tk.Frame(window, bg=PAGE)
        header.pack(fill="x", padx=24, pady=16)
        self.label(header, "OpenGangstar", PAGE, 19, AMBER, bold=True).pack(side="left")
        self.label(header, "Launcher", PAGE, 11, MUTED).pack(side="left", padx=12)

        body = tk.Frame(window, bg=PAGE)
        body.pack(fill="both", expand=True)
        library = tk.Frame(body, bg=RAIL, width=250)
        library.pack(side="left", fill="y")
        library.pack_propagate(False)
        self.label(library, "Game library", RAIL, 12, bold=True).pack(
            anchor="w", padx=16, pady=(18, 12))
        self.games = tk.Listbox(library, bg=RAIL, fg=TEXT, selectbackground="#3c3220",
                                selectforeground=AMBER, font=("Segoe UI", 11),
                                highlightthickness=1, highlightbackground=RAIL,
                                highlightcolor=AMBER, borderwidth=0, exportselection=False,
                                activestyle="none")
        self.games.pack(fill="both", expand=True, padx=12)
        for title in GAMES:
            self.games.insert("end", title)
        self.games.selection_set(0)
        self.games.bind("<<ListboxSelect>>", self.select_game)
        self.select_game()

        content = tk.Frame(body, bg=PAGE)
        content.pack(side="left", fill="both", expand=True, padx=24, pady=(0, 20))
        target = tk.Frame(content, bg=PAGE)
        target.pack(fill="x")
        self.label(target, "Platform", PAGE, 10).pack(side="left", padx=(0, 8))
        self.platform_picker = ttk.Combobox(target, values=platforms, textvariable=self.platform,
                                            state="readonly", width=22)
        self.platform_picker.pack(side="left")

        actions = tk.Frame(content, bg=PAGE)
        actions.pack(fill="x", pady=(10, 14))
        self.build_button = ttk.Button(actions, text="Build game", style="Build.TButton",
                                       command=self.start_build)
        self.play_button = ttk.Button(actions, text="Play on Android", style="Build.TButton",
                                      command=self.start_play)
        status = self.label(actions, "", PAGE, 11, MUTED)
        status.configure(textvariable=self.status)
        status.grid(row=1, column=0, columnspan=2, sticky="w", pady=(8, 0))

        self.cover = None
        self.cover_refresh = None
        self.hero = tk.Canvas(content, bg=PANEL, height=155, highlightthickness=0)
        self.hero.pack(fill="x")
        self.hero.bind("<Configure>", self.draw_cover)
        self.title.trace_add("write", self.draw_cover)
        self.retail.trace_add("write", self.schedule_cover)
        self.load_cover()

        inputs = tk.Frame(content, bg=PAGE)
        inputs.pack(fill="x", pady=14)
        inputs.columnconfigure(1, weight=1)
        self.android_inputs = tk.Frame(inputs, bg=PAGE)
        self.android_inputs.grid(row=0, column=0, columnspan=2, sticky="ew")
        self.android_inputs.columnconfigure(1, weight=1)
        self.path_row(self.android_inputs, 0, "Retail APK or library", self.retail, self.pick_retail)
        self.path_row(self.android_inputs, 1, "Game data ZIP (optional)", self.assets, self.pick_assets)
        self.path_row(self.android_inputs, 2, "Toolchain", self.toolchain, self.pick_toolchain)
        self.windows_inputs = tk.Frame(inputs, bg=PAGE)
        self.windows_inputs.grid(row=0, column=0, columnspan=2, sticky="ew")
        self.windows_inputs.columnconfigure(1, weight=1)
        self.path_row(self.windows_inputs, 0, "Retail game data", self.windows_assets,
                      lambda: self.pick_data_zip(self.windows_assets), "Browse ZIP…")
        ttk.Button(self.windows_inputs, text="Browse folder…", command=self.pick_windows_folder,
                   style="Browse.TButton").grid(row=0, column=3, padx=(6, 0))

        self.label(content, "Output", PAGE, 10, MUTED).pack(
            anchor="w", pady=(0, 5))
        console = tk.Frame(content, bg=RAIL)
        console.pack(fill="both", expand=True)
        scrollbar = ttk.Scrollbar(console)
        scrollbar.pack(side="right", fill="y")
        self.log = tk.Text(console, bg=RAIL, fg=TEXT, font=("Consolas", 10), height=6,
                           padx=10, pady=8, wrap="char", borderwidth=0, state="disabled",
                           yscrollcommand=scrollbar.set)
        self.log.pack(fill="both", expand=True)
        scrollbar.configure(command=self.log.yview)
        self.windows_assets.trace_add("write", self.update_windows_play)
        self.platform.trace_add("write", self.update_platform)
        self.update_platform()
        window.after(50, self.poll_build)

    def update_platform(self, *_args):
        windows = self.platform.get() == WINDOWS_PLATFORM
        if windows:
            self.android_inputs.grid_remove()
            self.windows_inputs.grid()
        else:
            self.windows_inputs.grid_remove()
            self.android_inputs.grid()
        self.build_button.configure(text="Build for Windows" if windows else "Build for Android")
        self.build_button.configure(style="Secondary.TButton" if windows else "Build.TButton")
        self.build_button.grid(row=0, column=int(windows), sticky="w", padx=(8 if windows else 0, 0))
        self.play_button.grid(row=0, column=int(not windows), sticky="w", padx=(0 if windows else 8, 0))
        self.play_button.configure(text="Play on Windows" if windows else "Play on Android")
        self.play_button.configure(style="Build.TButton" if windows else "Secondary.TButton")
        if windows:
            self.update_windows_play()
        else:
            self.play_button.state(["!disabled"])
            self.status.set("Ready to build")
        self.draw_cover()

    def schedule_cover(self, *_args):
        if self.cover_refresh is not None:
            self.window.after_cancel(self.cover_refresh)
        self.cover_refresh = self.window.after(250, self.load_cover)

    def load_cover(self):
        self.cover_refresh = None
        self.cover = None
        path = Path(self.retail.get().strip()).expanduser()
        if path.suffix.lower() == ".apk":
            try:
                with ZipFile(path) as apk:
                    entry = apk.getinfo("res/drawable-hdpi/ab_background.png")
                    if entry.file_size <= 2 * 1024 * 1024:
                        image = tk.PhotoImage(master=self.window, data=apk.read(entry))
                        if image.width() == 854 and image.height() == 480:
                            self.cover = image.subsample(2)
            except (BadZipFile, KeyError, OSError, RuntimeError, tk.TclError):
                pass
        self.draw_cover()

    def draw_cover(self, *_args):
        self.hero.delete("all")
        width = self.hero.winfo_width()
        if self.cover is not None:
            title_width = min(350, max(245, width // 2))
            self.hero.create_image(width, 71, image=self.cover, anchor="e")
            self.hero.create_rectangle(0, 0, title_width, 155, fill=PANEL, outline="")
        else:
            title_width = max(245, width)
        platform = "Windows x86 · Vulkan" if self.platform.get() == WINDOWS_PLATFORM else "Android · ARM"
        self.hero.create_text(20, 24, text=platform, fill=AMBER,
                              anchor="nw", font=("Segoe UI", 10))
        self.hero.create_text(20, 52, text=self.title.get(), width=title_width - 40,
                              fill="#ffe08f", anchor="nw", font=("Segoe UI", 22, "bold"))

    @staticmethod
    def label(parent, text, background, size, color=TEXT, bold=False):
        return tk.Label(parent, text=text, bg=background, fg=color,
                        font=("Segoe UI", size, "bold" if bold else "normal"))

    def path_row(self, parent, row, label, value, browse, button_label="Browse…"):
        self.label(parent, label, PAGE, 10).grid(row=row, column=0, sticky="w", padx=(0, 12))
        ttk.Entry(parent, textvariable=value, style="Path.TEntry").grid(
            row=row, column=1, sticky="ew", pady=3)
        ttk.Button(parent, text=button_label, command=browse, style="Browse.TButton").grid(
            row=row, column=2, padx=(6, 0))

    def select_game(self, _event=None):
        if self.games.curselection():
            self.title.set(self.games.get(self.games.curselection()[0]))

    def pick_retail(self):
        path = filedialog.askopenfilename(parent=self.window, title="Select your retail game",
                                          filetypes=[("Android APK or shared library", ("*.apk", "*.so")),
                                                     ("All files", "*")])
        if path:
            self.retail.set(path)

    def pick_assets(self):
        self.pick_data_zip(self.assets)

    def pick_data_zip(self, value):
        path = filedialog.askopenfilename(parent=self.window, title="Select the game data archive",
                                          filetypes=[("ZIP archive", "*.zip")])
        if path:
            value.set(path)

    def pick_windows_folder(self):
        path = filedialog.askdirectory(parent=self.window, title="Select the extracted game data folder")
        if path:
            self.windows_assets.set(path)

    def pick_toolchain(self):
        path = filedialog.askdirectory(parent=self.window, title="Select the toolchain root")
        if path:
            self.toolchain.set(path)

    def start_build(self):
        if self.running:
            return
        if self.platform.get() == WINDOWS_PLATFORM:
            self.start_job("Windows build", "Building for Windows x86…", self.run_windows_build,
                           self.windows_assets.get().strip())
            return
        command = [sys.executable, "-u", *GAMES[self.title.get()]]
        source = self.retail.get().strip()
        data_path = self.assets.get().strip()
        assets = Path(data_path).expanduser().resolve() if data_path else None
        toolchain = self.toolchain.get().strip()
        if source:
            source = str(Path(source).expanduser().resolve())
        if toolchain:
            toolchain = str(Path(toolchain).expanduser().resolve())
            command.extend(("--toolchain", toolchain))

        self.start_job("Build", "Building and checking bytes…", self.run_build,
                       command, source, assets, toolchain)

    def update_windows_play(self, *_args):
        if self.running or self.platform.get() != WINDOWS_PLATFORM:
            return
        self.play_button.state(["!disabled"])
        if not WINDOWS_EXE.is_file():
            self.status.set("Play builds the Windows game on first launch")
        elif not self.windows_assets.get().strip():
            self.status.set("Click Play to select a game data ZIP, or browse for a folder")
        else:
            self.status.set("Ready to play on Windows")

    def start_play(self):
        if self.running:
            return
        if self.platform.get() == WINDOWS_PLATFORM:
            source = self.windows_assets.get().strip()
            if not source:
                self.pick_data_zip(self.windows_assets)
                source = self.windows_assets.get().strip()
                if not source:
                    return
            self.start_job("Windows launch", "Preparing Windows game…", self.run_windows_play, source)
            return
        self.start_job("Launch", "Connecting to Android…", self.run_play)

    def start_job(self, operation, status, target, *args):
        if self.running:
            return
        self.operation = operation
        self.running = True
        self.build_button.state(["disabled"])
        self.play_button.state(["disabled"])
        self.games.configure(state="disabled")
        self.platform_picker.configure(state="disabled")
        self.status.set(status)
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        Thread(target=target, args=args, daemon=True).start()

    def run_play(self):
        try:
            status = play(GAME_ROOT / "build/library-link",
                          lambda line: self.events.put(("output", line + "\n")))
        except Exception as error:
            self.events.put(("output", f"LAUNCH FAILED: {error}\n"))
            self.events.put(("done", 2))
        else:
            self.events.put(("done", status))

    def run_build(self, command, source, assets, toolchain):
        try:
            save_settings(retail=source, assets=str(assets) if assets else "", toolchain=toolchain)
        except OSError as error:
            self.events.put(("output", f"Could not remember build paths: {error}\n"))
        try:
            with retail_library(source) as (library, apk_entry):
                if library:
                    command.extend(("--baselib", library))
                result = self.run_command(command, GAME_ROOT)
                artifacts = GAME_ROOT / "build/library-link"
                if result == 0 and apk_entry:
                    output = artifacts / "gangstar-wch.apk"
                    package_apk(Path(source), apk_entry, artifacts / BASE_LIB_NAME, output)
                    self.events.put(("output", f"OK APK {output} (original signatures preserved)\n"))
                if result == 0 and assets is not None:
                    output = artifacts / "gangstar-wch-data.zip"
                    count = package_game_data(assets, output)
                    self.events.put(("output", f"OK GAME DATA {output} ({count} assets)\n"))
        except Exception as error:
            # A failed input or subprocess must always release the build button.
            self.events.put(("output", f"BUILD FAILED: {error}\n"))
            result = 2
        self.events.put(("done", result))

    def remember_windows_paths(self, source):
        try:
            save_settings(windows_assets=source)
        except OSError as error:
            self.events.put(("output", f"Could not remember Windows game data path: {error}\n"))

    def run_windows_play(self, source):
        self.remember_windows_paths(source)
        try:
            if not source:
                raise ValueError("Select your retail game data ZIP or extracted folder first.")
            with windows_game_data(Path(source).expanduser().resolve()) as assets:
                if not WINDOWS_EXE.is_file():
                    self.events.put(("status", "Building for Windows x86…"))
                    result = self.build_windows_executable()
                    if result != 0:
                        self.events.put(("output", "Windows build failed. See CMake output above.\n"))
                        self.events.put(("done", result))
                        return
                self.events.put(("output", f"Starting {WINDOWS_EXE.name} with data from {source}\n"))
                self.events.put(("status", "Windows game running"))
                status = self.run_command([str(WINDOWS_EXE), str(assets)], WINDOWS_EXE.parent)
        except (OSError, ValueError, BadZipFile, RuntimeError) as error:
            self.events.put(("output", f"LAUNCH FAILED: {error}\n"))
            status = 2
        self.events.put(("done", status))

    def run_windows_build(self, source):
        self.remember_windows_paths(source)
        try:
            result = self.build_windows_executable()
            if result == 0:
                self.events.put(("output", "Select your retail game data and click Play on Windows.\n"))
        except (OSError, ValueError) as error:
            self.events.put(("output", f"BUILD FAILED: {error}\n"))
            result = 2
        self.events.put(("done", result))

    def build_windows_executable(self):
        cmake = which("cmake")
        if cmake is None:
            raise ValueError("Install CMake and add it to PATH. See port/README.md.")
        result = self.run_command([cmake, "-S", "port", "-B", str(WINDOWS_BUILD), "-A", "Win32"], ROOT)
        if result == 0:
            result = self.run_command([cmake, "--build", str(WINDOWS_BUILD), "--config", "Release"], ROOT)
        if result == 0:
            if not WINDOWS_EXE.is_file():
                raise ValueError("CMake did not produce the Windows executable.")
            self.events.put(("output", f"OK WINDOWS {WINDOWS_EXE}\n"))
        return result

    def run_command(self, command, directory):
        with subprocess.Popen(command, cwd=directory, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, errors="replace",
                              creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)) as build:
            for line in build.stdout:
                self.events.put(("output", line))
            return build.wait()

    def poll_build(self):
        lines = []
        for _ in range(200):
            try:
                event, value = self.events.get_nowait()
            except Empty:
                break
            if event == "output":
                lines.append(value)
            elif event == "status":
                self.status.set(value)
            elif event == "done":
                if value == 0 and self.operation == "Windows build":
                    self.status.set("Build complete. Click Play on Windows to launch.")
                elif value == 0 and self.operation == "Windows launch":
                    self.status.set("Windows game closed")
                else:
                    self.status.set(f"{self.operation} passed" if value == 0
                                    else f"{self.operation} failed (exit {value})")
                self.running = False
                self.games.configure(state="normal")
                self.build_button.state(["!disabled"])
                self.play_button.state(["!disabled"])
                self.platform_picker.configure(state="readonly")
        if lines:
            follow = self.log.yview()[1] == 1.0
            self.log.configure(state="normal")
            self.log.insert("end", "".join(lines))
            self.log.configure(state="disabled")
            if follow:
                self.log.see("end")
        self.window.after(50, self.poll_build)

    def close(self):
        if self.running:
            messagebox.showinfo(f"{self.operation} running", "Wait for the current task before closing.",
                                parent=self.window)
            return
        self.window.destroy()


if __name__ == "__main__":
    window = tk.Tk()
    Launcher(window)
    window.mainloop()
