"""Run the OpenGangstar desktop launcher with `python launcher`."""

from contextlib import contextmanager
import hashlib
from pathlib import Path, PurePosixPath
from queue import Empty, Queue
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parent.parent
GAME_ROOT = ROOT / "games" / "gangstar-wch"
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
    "Gangstar: West Coast Hustle": ("tools/build.py", "--all"),
}


@contextmanager
def retail_library(source):
    """Yield a local library path, extracting only the matching file from an APK."""
    if not source or Path(source).suffix.lower() != ".apk":
        yield source
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
        yield str(path)


class Launcher:
    def __init__(self, window):
        self.window = window
        self.running = False
        self.events = Queue(maxsize=1000)
        self.retail = tk.StringVar()
        self.toolchain = tk.StringVar()
        self.status = tk.StringVar(value="Ready to build")
        self.title = tk.StringVar()
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
        hero = tk.Frame(content, bg=PANEL)
        hero.pack(fill="x")
        self.label(hero, "Android · ARM", PANEL, 10, AMBER).pack(
            anchor="w", padx=18, pady=(16, 0))
        title = self.label(hero, "", PANEL, 23, "#ffe08f", bold=True)
        title.configure(textvariable=self.title)
        title.pack(anchor="w", padx=18, pady=(1, 14))

        inputs = tk.Frame(content, bg=PAGE)
        inputs.pack(fill="x", pady=(14, 0))
        inputs.columnconfigure(1, weight=1)
        self.path_row(inputs, 0, "Retail APK or library", self.retail, self.pick_retail)
        self.path_row(inputs, 1, "Toolchain", self.toolchain, self.pick_toolchain)
        self.label(content, "Leave paths blank to use the build tool’s defaults.", PAGE, 10, MUTED).pack(
            anchor="w", pady=(4, 0))

        actions = tk.Frame(content, bg=PAGE)
        actions.pack(fill="x", pady=14)
        self.build_button = ttk.Button(actions, text="Build & verify", style="Build.TButton",
                                       command=self.start_build)
        self.build_button.pack(side="left")
        status = self.label(actions, "", PAGE, 11, MUTED)
        status.configure(textvariable=self.status)
        status.pack(side="left", padx=12)
        self.label(content, "Build output · Verification images in build/matches/", PAGE, 10, MUTED).pack(
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
        window.after(50, self.poll_build)

    @staticmethod
    def label(parent, text, background, size, color=TEXT, bold=False):
        return tk.Label(parent, text=text, bg=background, fg=color,
                        font=("Segoe UI", size, "bold" if bold else "normal"))

    def path_row(self, parent, row, label, value, browse):
        self.label(parent, label, PAGE, 10).grid(row=row, column=0, sticky="w", padx=(0, 12))
        ttk.Entry(parent, textvariable=value, style="Path.TEntry").grid(
            row=row, column=1, sticky="ew", pady=3)
        ttk.Button(parent, text="Browse…", command=browse, style="Browse.TButton").grid(
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

    def pick_toolchain(self):
        path = filedialog.askdirectory(parent=self.window, title="Select the toolchain root")
        if path:
            self.toolchain.set(path)

    def start_build(self):
        if self.running:
            return
        command = [sys.executable, "-u", *GAMES[self.title.get()]]
        source = self.retail.get().strip()
        toolchain = self.toolchain.get().strip()
        if source:
            source = str(Path(source).expanduser().resolve())
        if toolchain:
            command.extend(("--toolchain", str(Path(toolchain).expanduser().resolve())))

        self.running = True
        self.build_button.state(["disabled"])
        self.games.configure(state="disabled")
        self.status.set("Building and checking bytes…")
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        Thread(target=self.run_build, args=(command, source), daemon=True).start()

    def run_build(self, command, source):
        try:
            with retail_library(source) as library:
                if library:
                    command.extend(("--baselib", library))
                with subprocess.Popen(command, cwd=GAME_ROOT, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, text=True, errors="replace",
                                      creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)) as build:
                    for line in build.stdout:
                        self.events.put(("output", line))
                    result = build.wait()
        except Exception as error:
            # A failed input or subprocess must always release the build button.
            self.events.put(("output", f"BUILD FAILED: {error}\n"))
            result = 2
        self.events.put(("done", result))

    def poll_build(self):
        lines = []
        for _ in range(200):
            try:
                event, value = self.events.get_nowait()
            except Empty:
                break
            if event == "output":
                lines.append(value)
            else:
                self.status.set("Build passed" if value == 0 else f"Build failed (exit {value})")
                self.running = False
                self.games.configure(state="normal")
                self.build_button.state(["!disabled"])
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
            messagebox.showinfo("Build running", "Wait for the build to finish before closing.",
                                parent=self.window)
            return
        self.window.destroy()


if __name__ == "__main__":
    window = tk.Tk()
    Launcher(window)
    window.mainloop()
