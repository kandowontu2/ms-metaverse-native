#!/usr/bin/env python3
"""Extract an ISO 9660 image, preserving Macintosh associated-file forks."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path, PurePosixPath
from typing import BinaryIO, Iterator


LOGICAL_BLOCK_SIZE = 2048
ASSOCIATED_DIRECTORY = "__associated__"


def le16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def le32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def safe_component(name: str) -> str:
    translation = str.maketrans({c: "_" for c in '<>:"/\\|?*'})
    cleaned = name.translate(translation).rstrip(" .")
    if cleaned in ("", ".", ".."):
        return "_"
    reserved = {
        "CON",
        "PRN",
        "AUX",
        "NUL",
        *(f"COM{i}" for i in range(1, 10)),
        *(f"LPT{i}" for i in range(1, 10)),
    }
    if cleaned.split(".", 1)[0].upper() in reserved:
        cleaned = f"_{cleaned}"
    return cleaned


def recording_time(raw: bytes) -> datetime | None:
    if len(raw) != 7 or raw[1] == 0 or raw[2] == 0:
        return None
    signed_offset = raw[6] if raw[6] < 128 else raw[6] - 256
    zone = timezone(timedelta(minutes=signed_offset * 15))
    try:
        return datetime(
            1900 + raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], tzinfo=zone
        )
    except ValueError:
        return None


@dataclass(frozen=True)
class IsoEntry:
    name: str
    extent: int
    size: int
    flags: int
    unit_size: int
    gap_size: int
    recorded: datetime | None

    @property
    def is_directory(self) -> bool:
        return bool(self.flags & 0x02)

    @property
    def is_associated(self) -> bool:
        return bool(self.flags & 0x04)

    @property
    def is_multi_extent(self) -> bool:
        return bool(self.flags & 0x80)


class Iso9660:
    def __init__(self, image: BinaryIO, image_size: int):
        self.image = image
        self.image_size = image_size
        self.primary = self._find_primary_descriptor()
        self.logical_block_size = le16(self.primary, 128)
        if self.logical_block_size != LOGICAL_BLOCK_SIZE:
            raise ValueError(
                f"unsupported ISO logical block size: {self.logical_block_size}"
            )
        self.volume_blocks = le32(self.primary, 80)
        self.system_id = self.primary[8:40].decode("ascii", errors="replace").rstrip()
        self.volume_id = self.primary[40:72].decode("ascii", errors="replace").rstrip()
        self.root_record = self._parse_record(
            self.primary[156 : 156 + self.primary[156]]
        )

    def _find_primary_descriptor(self) -> bytes:
        for block in range(16, 256):
            self.image.seek(block * LOGICAL_BLOCK_SIZE)
            descriptor = self.image.read(LOGICAL_BLOCK_SIZE)
            if len(descriptor) != LOGICAL_BLOCK_SIZE:
                break
            if descriptor[1:6] != b"CD001":
                raise ValueError(f"invalid ISO volume descriptor at block {block}")
            if descriptor[0] == 1:
                return descriptor
            if descriptor[0] == 255:
                break
        raise ValueError("ISO 9660 primary volume descriptor not found")

    def _parse_record(self, record: bytes) -> IsoEntry:
        if len(record) < 34 or len(record) != record[0]:
            raise ValueError("malformed ISO directory record")
        name_length = record[32]
        raw_name = record[33 : 33 + name_length]
        if raw_name == b"\x00":
            name = "."
        elif raw_name == b"\x01":
            name = ".."
        else:
            name = raw_name.decode("ascii", errors="replace")
            if ";" in name:
                name = name.rsplit(";", 1)[0]
        return IsoEntry(
            name=name,
            extent=le32(record, 2),
            size=le32(record, 10),
            flags=record[25],
            unit_size=record[26],
            gap_size=record[27],
            recorded=recording_time(record[18:25]),
        )

    def read_extent(self, entry: IsoEntry) -> bytes:
        if entry.unit_size or entry.gap_size:
            raise ValueError(f"interleaved file is unsupported: {entry.name}")
        start = entry.extent * self.logical_block_size
        end = start + entry.size
        if end > self.image_size:
            raise ValueError(
                f"extent for {entry.name} ends at {end}, past image size {self.image_size}"
            )
        self.image.seek(start)
        data = self.image.read(entry.size)
        if len(data) != entry.size:
            raise ValueError(f"short read for {entry.name}")
        return data

    def entries(self, directory: IsoEntry) -> Iterator[IsoEntry]:
        data = self.read_extent(directory)
        offset = 0
        while offset < len(data):
            record_length = data[offset]
            if record_length == 0:
                offset = ((offset // self.logical_block_size) + 1) * self.logical_block_size
                continue
            record = data[offset : offset + record_length]
            yield self._parse_record(record)
            offset += record_length


def extract_tree(fs: Iso9660, destination: Path) -> list[dict[str, object]]:
    files: list[dict[str, object]] = []
    visited_directories: set[int] = set()

    def visit(directory: IsoEntry, relative: PurePosixPath) -> None:
        if directory.extent in visited_directories:
            raise ValueError(f"directory cycle at ISO extent {directory.extent}")
        visited_directories.add(directory.extent)

        pending_multi: dict[tuple[str, bool], bytearray] = {}
        pending_meta: dict[tuple[str, bool], IsoEntry] = {}

        for entry in fs.entries(directory):
            if entry.name in (".", ".."):
                continue
            component = safe_component(entry.name)
            if entry.is_directory:
                if entry.is_associated:
                    raise ValueError(f"associated directory is unsupported: {entry.name}")
                output_path = destination.joinpath(*relative.parts, component)
                output_path.mkdir(parents=True, exist_ok=True)
                visit(entry, relative / component)
                continue

            key = (component, entry.is_associated)
            data = fs.read_extent(entry)
            if key in pending_multi:
                pending_multi[key].extend(data)
            else:
                pending_multi[key] = bytearray(data)
                pending_meta[key] = entry
            if entry.is_multi_extent:
                continue

            complete = bytes(pending_multi.pop(key))
            first_entry = pending_meta.pop(key)
            output_relative = relative
            if entry.is_associated:
                output_relative = relative / ASSOCIATED_DIRECTORY
            output_relative /= component
            output_path = destination.joinpath(*output_relative.parts)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_bytes(complete)
            if entry.recorded is not None:
                timestamp = entry.recorded.timestamp()
                os.utime(output_path, (timestamp, timestamp))
            files.append(
                {
                    "path": output_relative.as_posix(),
                    "iso_name": entry.name,
                    "bytes": len(complete),
                    "sha256": hashlib.sha256(complete).hexdigest(),
                    "extent": first_entry.extent,
                    "flags": f"0x{entry.flags:02x}",
                    "associated": entry.is_associated,
                    "recorded": entry.recorded.isoformat() if entry.recorded else None,
                }
            )

        if pending_multi:
            names = ", ".join(name for name, _associated in pending_multi)
            raise ValueError(f"unterminated multi-extent files: {names}")

    destination.mkdir(parents=True, exist_ok=True)
    visit(fs.root_record, PurePosixPath())
    return files


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="2048-byte/sector ISO image")
    parser.add_argument("destination", type=Path, help="output directory")
    parser.add_argument(
        "--manifest",
        type=Path,
        help="JSON manifest path (default: <destination>/iso-manifest.json)",
    )
    args = parser.parse_args()

    image_size = args.source.stat().st_size
    with args.source.open("rb") as image:
        fs = Iso9660(image, image_size)
        files = extract_tree(fs, args.destination)

    manifest = {
        "format": "ISO 9660",
        "source": str(args.source.resolve()),
        "source_bytes": image_size,
        "source_sha256": sha256_file(args.source),
        "system_id": fs.system_id,
        "volume_id": fs.volume_id,
        "logical_block_size": fs.logical_block_size,
        "volume_blocks": fs.volume_blocks,
        "file_count": len(files),
        "associated_file_count": sum(bool(item["associated"]) for item in files),
        "files": files,
    }
    manifest_path = args.manifest or args.destination / "iso-manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "source_sha256": manifest["source_sha256"],
                "file_count": manifest["file_count"],
                "associated_file_count": manifest["associated_file_count"],
                "manifest": str(manifest_path.resolve()),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
