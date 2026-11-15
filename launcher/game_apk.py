"""Package a byte-identical rebuilt native library without invalidating signatures."""

from filecmp import cmp
from pathlib import Path, PurePosixPath
import re
from shutil import copyfile, copyfileobj
from struct import Struct
from tempfile import TemporaryDirectory
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile
import zlib


LOCAL_HEADER = Struct("<4s22xHH")


def package_apk(source: Path, entry_name: str, library: Path, output: Path) -> None:
    rebuilt = library.read_bytes()
    with ZipFile(source) as archive:
        entries = [entry for entry in archive.infolist() if entry.filename == entry_name]
        if len(entries) != 1:
            raise ValueError("The APK's game library entry is missing or ambiguous.")
        entry, = entries
        if archive.read(entry) != rebuilt:
            raise ValueError("The rebuilt library differs from the APK entry and would need re-signing.")

    with TemporaryDirectory(prefix="package-", dir=output.parent) as temporary:
        pending = Path(temporary) / output.name
        copyfile(source, pending)
        with pending.open("r+b") as package:
            package.seek(entry.header_offset)
            signature, name_size, extra_size = LOCAL_HEADER.unpack(package.read(LOCAL_HEADER.size))
            if signature != b"PK\x03\x04":
                raise ValueError("The APK has an invalid local ZIP header.")
            data_offset = entry.header_offset + LOCAL_HEADER.size + name_size + extra_size
            package.seek(data_offset)
            original = package.read(entry.compress_size)
            encoded = encode_entry(rebuilt, original, entry.compress_type)
            package.seek(data_offset)
            package.write(encoded)

        if not cmp(source, pending, shallow=False):
            raise ValueError("The rebuilt APK differs from retail and would need re-signing.")
        with ZipFile(pending) as archive:
            if archive.read(entry_name) != rebuilt:
                raise ValueError("The packaged native library differs from the rebuilt library.")
        pending.replace(output)


def encode_entry(rebuilt: bytes, original: bytes, compression: int) -> bytes:
    if compression == ZIP_STORED:
        if rebuilt != original:
            raise ValueError("The stored APK library differs from the rebuilt library.")
        return rebuilt
    if compression != ZIP_DEFLATED:
        raise ValueError("The APK library uses an unsupported ZIP compression method.")

    for level in (6, 9, 1, 2, 3, 4, 5, 7, 8, 0):
        encoder = zlib.compressobj(level, wbits=-zlib.MAX_WBITS)
        data = encoder.compress(rebuilt) + encoder.flush()
        if data == original:
            return data

    # A different ZIP implementation can encode the same library differently.
    # Retain its original stream so signatures over the archive remain valid.
    if zlib.decompress(original, wbits=-zlib.MAX_WBITS) != rebuilt:
        raise ValueError("The compressed APK library differs from the rebuilt library.")
    return original


def package_game_data(source: Path, output: Path) -> int:
    """Place locally supplied assets under Android's game-data folder."""
    prefix = "gameloft/games/gangstar"
    with TemporaryDirectory(prefix="assets-", dir=output.parent) as temporary:
        pending = Path(temporary) / output.name
        written = set()
        with ZipFile(source) as archive, ZipFile(pending, "w", ZIP_DEFLATED) as result:
            for entry in archive.infolist():
                if entry.is_dir():
                    continue
                parts = PurePosixPath(entry.filename).parts
                if parts[:3] == ("gameloft", "games", "gangstar"):
                    parts = parts[3:]
                elif parts[:1] == ("gangstar",):
                    parts = parts[1:]
                else:
                    continue
                if not parts or not all(re.fullmatch(r"[A-Za-z0-9_.-]+", part)
                                        and part not in (".", "..") for part in parts):
                    raise ValueError(f"Invalid game data path: {entry.filename}")
                if PurePosixPath(parts[-1]).suffix.lower() in (".sav", ".preferences"):
                    continue
                target = "/".join((prefix, *parts))
                if target.casefold() in written:
                    raise ValueError(f"Duplicate game data file: {entry.filename}")
                with archive.open(entry) as data, result.open(target, "w") as packaged:
                    copyfileobj(data, packaged)
                written.add(target.casefold())
        if f"{prefix}/version_pycst.bin" not in written:
            raise ValueError("The ZIP does not contain the West Coast Hustle game data folder.")
        pending.replace(output)
    return len(written)
