"""Inventory resource filenames and container signatures without running the game."""

import argparse
from collections import Counter
from pathlib import Path, PurePosixPath
from zipfile import ZipFile


def classify(header):
    if not header:
        return "empty"
    if header[:4] == b"DDS " and len(header) >= 88 and int.from_bytes(header[80:84], "little") & 4:
        return "DDS/" + header[84:88].decode("ascii", errors="replace").rstrip("\0 ")
    signatures = {
        b"BRES": "BRES",
        b"DDS ": "DDS",
        b"OggS": "Ogg",
        b"\x89PNG": "PNG",
        b"RIFF": "RIFF",
    }
    if header[:4] in signatures:
        return signatures[header[:4]]
    if header[4:8] == b"ftyp":
        return "ISO base media"
    if header[:3] in (b"FWS", b"CWS", b"ZWS"):
        return header[:3].decode("ascii")
    if header.startswith(b"BM"):
        return "BMP"
    if header[:4] in (b"\x00\x01\x00\x00", b"OTTO"):
        return "sfnt"
    return "no recognized signature"


def inventory(source):
    counts = Counter()
    if source.is_dir():
        for path in source.rglob("*"):
            if path.is_file():
                with path.open("rb") as stream:
                    counts[path.suffix.lower(), classify(stream.read(88))] += 1
    else:
        with ZipFile(source) as archive:
            for entry in archive.infolist():
                if not entry.is_dir():
                    with archive.open(entry) as stream:
                        suffix = PurePosixPath(entry.filename).suffix.lower()
                        counts[suffix, classify(stream.read(88))] += 1
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Retail game data directory or ZIP")
    args = parser.parse_args()
    counts = inventory(args.source)
    print(f"{sum(counts.values())} files")
    for (suffix, signature), count in sorted(counts.items()):
        print(f"{suffix or '(none)':12} {signature:24} {count:5}")


if __name__ == "__main__":
    main()
