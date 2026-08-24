#!/usr/bin/env python3
"""Convert a raw 2352-byte Mode-1 CD track to a 2048-byte/sector ISO image."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


RAW_SECTOR_SIZE = 2352
USER_DATA_OFFSET = 16
USER_DATA_SIZE = 2048
SECTORS_PER_CHUNK = 512
SYNC = b"\x00" + (b"\xff" * 10) + b"\x00"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def convert(source: Path, destination: Path) -> dict[str, object]:
    source_size = source.stat().st_size
    if source_size % RAW_SECTOR_SIZE:
        raise ValueError(
            f"{source} is not an integral number of {RAW_SECTOR_SIZE}-byte sectors"
        )

    sector_count = source_size // RAW_SECTOR_SIZE
    raw_digest = hashlib.sha256()
    iso_digest = hashlib.sha256()
    destination.parent.mkdir(parents=True, exist_ok=True)

    sector_number = 0
    with source.open("rb") as src, destination.open("wb") as dst:
        while raw := src.read(RAW_SECTOR_SIZE * SECTORS_PER_CHUNK):
            if len(raw) % RAW_SECTOR_SIZE:
                raise ValueError("short sector at end of input")
            raw_digest.update(raw)
            view = memoryview(raw)
            cooked = bytearray((len(raw) // RAW_SECTOR_SIZE) * USER_DATA_SIZE)

            for local_index in range(len(raw) // RAW_SECTOR_SIZE):
                raw_start = local_index * RAW_SECTOR_SIZE
                header = view[raw_start : raw_start + USER_DATA_OFFSET]
                if header[:12] != SYNC:
                    raise ValueError(f"invalid CD sync at sector {sector_number}")
                if header[15] != 1:
                    raise ValueError(
                        f"sector {sector_number} is mode {header[15]}, expected Mode 1"
                    )

                cooked_start = local_index * USER_DATA_SIZE
                cooked[cooked_start : cooked_start + USER_DATA_SIZE] = view[
                    raw_start
                    + USER_DATA_OFFSET : raw_start
                    + USER_DATA_OFFSET
                    + USER_DATA_SIZE
                ]
                sector_number += 1

            dst.write(cooked)
            iso_digest.update(cooked)

    return {
        "format": "CD-ROM Mode 1/2352 to 2048-byte user-data image",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "source": str(source.resolve()),
        "source_bytes": source_size,
        "source_sha256": raw_digest.hexdigest(),
        "sector_count": sector_count,
        "output": str(destination.resolve()),
        "output_bytes": destination.stat().st_size,
        "output_sha256": iso_digest.hexdigest(),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="raw Mode-1/2352 BIN track")
    parser.add_argument("destination", type=Path, help="output 2048-byte ISO image")
    parser.add_argument(
        "--manifest",
        type=Path,
        help="JSON manifest path (default: <destination>.manifest.json)",
    )
    args = parser.parse_args()

    manifest = convert(args.source, args.destination)
    manifest_path = args.manifest or Path(f"{args.destination}.manifest.json")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))

if __name__ == "__main__":
    main()
