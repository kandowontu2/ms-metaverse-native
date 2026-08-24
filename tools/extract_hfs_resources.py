#!/usr/bin/env python3
"""Inventory a hybrid CD's HFS catalog and export every non-empty resource fork.

This wrapper uses the read-only hfsutils commands inside WSL. Each resource-bearing
file is first copied as MacBinary II, then split into its data and resource forks.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path, PurePosixPath


def run_wsl(*args: str) -> str:
    result = subprocess.run(
        ["wsl", "-e", *args],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return result.stdout


def wsl_path(path: Path) -> str:
    return run_wsl("wslpath", "-a", "-u", str(path.resolve())).strip()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def decode_hls_escape(text: str) -> str:
    output: list[str] = []
    index = 0
    escapes = {
        "a": "\a",
        "b": "\b",
        "f": "\f",
        "n": "\n",
        "r": "\r",
        "t": "\t",
        "v": "\v",
    }
    while index < len(text):
        if text[index] != "\\":
            output.append(text[index])
            index += 1
            continue
        index += 1
        if index == len(text):
            output.append("\\")
            break
        if text[index] in "01234567":
            end = index
            while end < len(text) and end < index + 3 and text[end] in "01234567":
                end += 1
            output.append(chr(int(text[index:end], 8)))
            index = end
            continue
        output.append(escapes.get(text[index], text[index]))
        index += 1
    return "".join(output)


def safe_component(name: str) -> str:
    translation = str.maketrans({c: "_" for c in '<>:"/\\|?*'})
    cleaned = "".join("_" if ord(c) < 32 else c for c in name).translate(translation)
    cleaned = cleaned.rstrip(" .")
    return cleaned or "_"


def parse_catalog(listing: str) -> list[dict[str, object]]:
    current_directory = PurePosixPath()
    entries: list[dict[str, object]] = []
    for line in listing.splitlines():
        if line.startswith(":") and line.endswith(":"):
            raw_parts = line[1:-1].split(":")
            current_directory = PurePosixPath(
                *(decode_hls_escape(part) for part in raw_parts if part)
            )
            continue

        fields = line.split(maxsplit=8)
        if len(fields) < 9:
            continue
        kind = fields[1]
        if not kind.lower().startswith("f"):
            continue
        name = decode_hls_escape(fields[8])
        type_creator = fields[2]
        file_type, creator = (
            type_creator.split("/", 1) if "/" in type_creator else ("", "")
        )
        relative = current_directory / name
        entries.append(
            {
                "catalog_id": int(fields[0]),
                "flags": kind,
                "file_type": file_type,
                "creator": creator,
                "resource_bytes": int(fields[3]),
                "data_bytes": int(fields[4]),
                "path": relative.as_posix(),
                "hfs_path": ":" + ":".join(relative.parts),
            }
        )
    return entries


def split_macbinary(data: bytes) -> tuple[bytes, bytes]:
    if len(data) < 128 or data[0] != 0 or not 1 <= data[1] <= 63:
        raise ValueError("invalid MacBinary header")
    data_size = struct.unpack_from(">I", data, 83)[0]
    resource_size = struct.unpack_from(">I", data, 87)[0]
    data_start = 128
    resource_start = data_start + ((data_size + 127) // 128) * 128
    if resource_start + resource_size > len(data):
        raise ValueError("MacBinary fork lengths exceed the file")
    return (
        data[data_start : data_start + data_size],
        data[resource_start : resource_start + resource_size],
    )


def export_resources(
    entries: list[dict[str, object]], destination: Path
) -> list[dict[str, object]]:
    exported: list[dict[str, object]] = []
    destination.mkdir(parents=True, exist_ok=True)
    wsl_destination = wsl_path(destination)

    for entry in entries:
        if not entry["resource_bytes"]:
            continue
        relative = PurePosixPath(str(entry["path"]))
        safe_parts = [safe_component(part) for part in relative.parts]
        base_relative = Path(*safe_parts)
        macbinary_path = destination / "macbinary" / Path(f"{base_relative}.macbin")
        data_path = destination / "forks" / Path(f"{base_relative}.data")
        resource_path = destination / "forks" / Path(f"{base_relative}.rsrc")
        macbinary_path.parent.mkdir(parents=True, exist_ok=True)
        data_path.parent.mkdir(parents=True, exist_ok=True)
        resource_path.parent.mkdir(parents=True, exist_ok=True)

        macbinary_relative = macbinary_path.relative_to(destination).as_posix()
        target = f"{wsl_destination}/{macbinary_relative}"
        run_wsl("hcopy", "-m", str(entry["hfs_path"]), target)
        macbinary = macbinary_path.read_bytes()
        data_fork, resource_fork = split_macbinary(macbinary)
        if len(data_fork) != entry["data_bytes"]:
            raise ValueError(f"data fork size mismatch for {entry['path']}")
        if len(resource_fork) != entry["resource_bytes"]:
            raise ValueError(f"resource fork size mismatch for {entry['path']}")
        data_path.write_bytes(data_fork)
        resource_path.write_bytes(resource_fork)
        exported.append(
            {
                "path": entry["path"],
                "macbinary": macbinary_path.relative_to(destination).as_posix(),
                "macbinary_sha256": sha256_bytes(macbinary),
                "data_fork": data_path.relative_to(destination).as_posix(),
                "data_bytes": len(data_fork),
                "data_sha256": sha256_bytes(data_fork),
                "resource_fork": resource_path.relative_to(destination).as_posix(),
                "resource_bytes": len(resource_fork),
                "resource_sha256": sha256_bytes(resource_fork),
            }
        )
    return exported


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="hybrid ISO image")
    parser.add_argument("destination", type=Path, help="output directory")
    args = parser.parse_args()

    source_wsl = wsl_path(args.source)
    mount_output = run_wsl("hmount", source_wsl)
    volume_output = run_wsl("hvol")
    listing = run_wsl("hls", "-a", "-l", "-i", "-R", "-b")
    entries = parse_catalog(listing)
    exports = export_resources(entries, args.destination)

    manifest = {
        "format": "Apple HFS catalog and MacBinary II resource-fork export",
        "source": str(args.source.resolve()),
        "source_bytes": args.source.stat().st_size,
        "source_sha256": sha256_file(args.source),
        "hmount": mount_output.strip(),
        "volume": volume_output.strip(),
        "catalog_file_count": len(entries),
        "resource_file_count": len(exports),
        "catalog": entries,
        "exports": exports,
    }
    manifest_path = args.destination / "hfs-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "catalog_file_count": len(entries),
                "resource_file_count": len(exports),
                "manifest": str(manifest_path.resolve()),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
