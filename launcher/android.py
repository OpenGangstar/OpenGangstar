"""Install and start the built West Coast Hustle APK on an ADB device."""

import hashlib
import os
import re
from pathlib import Path, PurePosixPath
from shutil import which
import subprocess
from tempfile import TemporaryDirectory
from time import monotonic, sleep
from uuid import uuid4
from zipfile import ZipFile


PACKAGE = "com.gameloft.android.GAND.GloftGANG"
DATA_PARENT = "/sdcard/gameloft/games"
DATA = f"{DATA_PARENT}/gangstar"


def play(artifacts, log):
    apk = artifacts / "gangstar-wch.apk"
    if not apk.is_file():
        raise ValueError("Build the game from your retail APK first.")
    adb = which("adb")
    if adb is None and os.name == "nt":
        bundled = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "BlueStacks_nxt/HD-Adb.exe"
        if bundled.is_file():
            adb = str(bundled)
    if adb is None:
        raise ValueError("Install Android platform-tools or BlueStacks to play.")

    def run(*args, timeout=25):
        result = subprocess.run([adb, *args], capture_output=True, text=True, timeout=timeout,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if result.returncode:
            detail = (result.stderr or result.stdout).strip() or f"exit {result.returncode}"
            raise ValueError(f"ADB {' '.join(args)}: {detail}")
        return result.stdout.strip() or result.stderr.strip()

    devices = [line.split()[0] for line in run("devices").splitlines() if line.endswith("\tdevice")]
    serial = os.environ.get("ANDROID_SERIAL")
    if not serial and len(devices) == 1:
        serial = devices[0]
    if serial not in devices:
        raise ValueError("Connect one ADB device, or set ANDROID_SERIAL to the device to use.")

    def device(*args, timeout=25):
        return run("-s", serial, *args, timeout=timeout)

    data_exists = device("shell", f"if [ -d {DATA} ]; then echo present; fi")
    data_zip = artifacts / "gangstar-wch-data.zip"
    if not data_exists and not data_zip.is_file():
        raise ValueError("Build with a Game data ZIP, or copy the game data to the device first.")
    packages = device("shell", "pm", "list", "packages", PACKAGE).splitlines()
    if f"package:{PACKAGE}" in packages:
        path = device("shell", "pm", "path", PACKAGE).removeprefix("package:")
        if not re.fullmatch(r"/data/app/[A-Za-z0-9_=.~/+-]+\.apk", path):
            raise ValueError("The installed game does not have a single APK.")
        actual = device("shell", "sha256sum", path).partition(" ")[0]
        if actual != hashlib.sha256(apk.read_bytes()).hexdigest():
            raise ValueError("The connected device already has a different APK for this game.")
        log("Using the matching APK already installed on the device.")
    else:
        result = device("install", str(apk), timeout=120)
        installed = device("shell", "pm", "list", "packages", PACKAGE).splitlines()
        if f"package:{PACKAGE}" not in installed:
            raise ValueError(result or "ADB did not install the game APK.")
        log(result or "Installed the built APK.")
    if data_exists:
        log(f"Using existing game data at {DATA}.")
    else:
        transfer_data(device, data_zip)
        log(f"Installed game data at {DATA}.")
    result = device("shell", "am", "start", "-W", "-n", f"{PACKAGE}/.GameInstaller")
    if "Status: ok" not in result:
        raise ValueError(result)
    log(result)
    return wait_for_activity(device, log)


def wait_for_activity(device, log):
    deadline = monotonic() + 8
    activity = None
    while monotonic() < deadline:
        state = device("shell", "dumpsys", "activity", "activities")
        match = re.search(
            rf"(?:mResumedActivity|topResumedActivity):[^\n]*{re.escape(PACKAGE)}/(?:{re.escape(PACKAGE)})?\.([A-Za-z0-9_]+)",
            state,
        )
        activity = match.group(1) if match else None
        if activity in ("Gangstar", "MyVideoView"):
            log(f"Android opened the game activity: {activity}.")
            return "Game opened"
        sleep(0.5)
    if activity != "GameInstaller":
        raise ValueError("The game is no longer in the foreground. Check the device for an error.")

    manufacturer = device("shell", "getprop", "ro.product.manufacturer")
    texture = device("shell", f"dd if={DATA}/00906_exp.tga bs=1 skip=84 count=4 2>/dev/null || true")
    if texture.startswith("ATC") and manufacturer != "HTC":
        log(f"The retail installer is open. This device reports {manufacturer or 'an unknown manufacturer'}, "
            "but the installed textures use the HTC/ATC format.")
    else:
        log("The retail installer is open. Android has not reached the game activity.")
    return "Installer opened"


def transfer_data(device, source):
    staging = f"{DATA_PARENT}/.opengangstar-{uuid4().hex}"
    with TemporaryDirectory(prefix="opengangstar-android-") as temporary, ZipFile(source) as archive:
        root = Path(temporary).resolve()
        data = root / DATA.lstrip("/").removeprefix("sdcard/")
        expected = {}
        for entry in archive.infolist():
            path = root.joinpath(*PurePosixPath(entry.filename).parts).resolve()
            if not path.is_relative_to(data):
                raise ValueError(f"Unexpected path in built game data: {entry.filename}")
            archive.extract(entry, root)
            if not entry.is_dir():
                expected[f"{staging}/{path.relative_to(data).as_posix()}"] = entry.file_size
        if not (data / "Version_pycst.bin").is_file():
            raise ValueError("The built game data archive is incomplete.")
        device("shell", "mkdir", "-p", DATA_PARENT)
        try:
            device("push", str(data), staging, timeout=240)
            listing = device("shell", f"find {staging} -type f -exec stat -c '%s|%n' {{}} +",
                             timeout=60)
            observed = {}
            for line in listing.splitlines():
                size, separator, name = line.partition("|")
                if separator and size.isdecimal():
                    observed[name] = int(size)
            if observed != expected:
                failed = sum(observed.get(name) != size for name, size in expected.items())
                extra = len(observed.keys() - expected.keys())
                raise ValueError(f"Incomplete ADB game data transfer ({failed} missing or truncated, "
                                 f"{extra} unexpected). Reconnect the device and retry.")
            result = device("shell", f"test ! -e {DATA} && mv {staging} {DATA} && echo published")
            if result != "published":
                raise ValueError("Could not publish the game data. Check the device storage.")
        except (ValueError, subprocess.TimeoutExpired):
            try:
                device("shell", f"if [ -d {staging} ]; then rm -r {staging}; fi")
            except (ValueError, subprocess.TimeoutExpired):
                pass
            raise
