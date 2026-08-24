#!/usr/bin/env python3
"""Extract a FAT16 partition from a fixed VHD without mounting the image."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from typing import BinaryIO, Iterator


SECTOR_SIZE = 512
FAT16_PARTITION_TYPES = {0x04, 0x06, 0x0E}


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def dos_datetime(date_word: int, time_word: int) -> datetime | None:
    if date_word == 0:
        return None
    year = 1980 + ((date_word >> 9) & 0x7F)
    month = (date_word >> 5) & 0x0F
    day = date_word & 0x1F
    hour = (time_word >> 11) & 0x1F
    minute = (time_word >> 5) & 0x3F
    second = (time_word & 0x1F) * 2
    try:
        return datetime(year, month, day, hour, minute, second)
    except ValueError:
        return None


def decode_short_name(entry: bytes) -> str:
    stem = entry[0:8].decode("cp437", errors="replace").rstrip()
    suffix = entry[8:11].decode("cp437", errors="replace").rstrip()
    if entry[0] == 0x05:
        stem = "\xE5" + stem[1:]
    return f"{stem}.{suffix}" if suffix else stem


def decode_lfn_piece(entry: bytes) -> str:
    raw = entry[1:11] + entry[14:26] + entry[28:32]
    chars: list[str] = []
    for offset in range(0, len(raw), 2):
        value = u16(raw, offset)
        if value in (0x0000, 0xFFFF):
            break
        chars.append(chr(value))
    return "".join(chars)


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


@dataclass(frozen=True)
class DirectoryEntry:
    name: str
    attributes: int
    cluster: int
    size: int
    modified: datetime | None

    @property
    def is_directory(self) -> bool:
        return bool(self.attributes & 0x10)


class Fat16:
    def __init__(self, image: BinaryIO, partition_lba: int):
        self.image = image
        self.partition_lba = partition_lba
        self.partition_offset = partition_lba * SECTOR_SIZE
        boot = self.read_at(0, SECTOR_SIZE)
        if boot[510:512] != b"\x55\xaa":
            raise ValueError("FAT boot sector signature is missing")

        self.bytes_per_sector = u16(boot, 11)
        self.sectors_per_cluster = boot[13]
        self.reserved_sectors = u16(boot, 14)
        self.fat_count = boot[16]
        self.root_entry_count = u16(boot, 17)
        self.total_sectors = u16(boot, 19) or u32(boot, 32)
        self.sectors_per_fat = u16(boot, 22)
        self.volume_label = boot[43:54].decode("ascii", errors="replace").rstrip()
        self.fs_type = boot[54:62].decode("ascii", errors="replace").rstrip()

        if self.bytes_per_sector != SECTOR_SIZE:
            raise ValueError(f"unsupported sector size: {self.bytes_per_sector}")
        if self.fs_type != "FAT16":
            raise ValueError(f"expected FAT16, found {self.fs_type!r}")

        self.root_dir_sectors = (
            self.root_entry_count * 32 + self.bytes_per_sector - 1
        ) // self.bytes_per_sector
        self.fat_sector = self.reserved_sectors
        self.root_sector = self.fat_sector + self.fat_count * self.sectors_per_fat
        self.data_sector = self.root_sector + self.root_dir_sectors
        self.cluster_size = self.sectors_per_cluster * self.bytes_per_sector
        self.fat = self.read_at(
            self.fat_sector * self.bytes_per_sector,
            self.sectors_per_fat * self.bytes_per_sector,
        )

    def read_at(self, relative_offset: int, size: int) -> bytes:
        self.image.seek(self.partition_offset + relative_offset)
        data = self.image.read(size)
        if len(data) != size:
            raise ValueError("unexpected end of VHD")
        return data

    def cluster_offset(self, cluster: int) -> int:
        if cluster < 2:
            raise ValueError(f"invalid data cluster {cluster}")
        sector = self.data_sector + (cluster - 2) * self.sectors_per_cluster
        return sector * self.bytes_per_sector

    def cluster_chain(self, first_cluster: int) -> Iterator[int]:
        cluster = first_cluster
        visited: set[int] = set()
        while 2 <= cluster < 0xFFF8:
            if cluster in visited:
                raise ValueError(f"FAT cycle at cluster {cluster}")
            visited.add(cluster)
            yield cluster
            fat_offset = cluster * 2
            if fat_offset + 2 > len(self.fat):
                raise ValueError(f"cluster {cluster} exceeds FAT")
            cluster = u16(self.fat, fat_offset)
        if cluster < 0xFFF8 and cluster not in (0x0000, 0xFFF7):
            raise ValueError(f"invalid FAT chain terminator 0x{cluster:04x}")

    def read_chain(self, first_cluster: int, size: int | None = None) -> bytes:
        chunks = [
            self.read_at(self.cluster_offset(cluster), self.cluster_size)
            for cluster in self.cluster_chain(first_cluster)
        ]
        data = b"".join(chunks)
        return data if size is None else data[:size]

    def directory_bytes(self, cluster: int | None) -> bytes:
        if cluster is None:
            return self.read_at(
                self.root_sector * self.bytes_per_sector,
                self.root_entry_count * 32,
            )
        return self.read_chain(cluster)

    def entries(self, cluster: int | None) -> Iterator[DirectoryEntry]:
        data = self.directory_bytes(cluster)
        lfn_parts: dict[int, str] = {}
        for offset in range(0, len(data), 32):
            entry = data[offset : offset + 32]
            if len(entry) < 32 or entry[0] == 0x00:
                break
            if entry[0] == 0xE5:
                lfn_parts.clear()
                continue

            attributes = entry[11]
            if attributes == 0x0F:
                sequence = entry[0] & 0x1F
                lfn_parts[sequence] = decode_lfn_piece(entry)
                continue

            if attributes & 0x08:
                lfn_parts.clear()
                continue

            long_name = "".join(lfn_parts[i] for i in sorted(lfn_parts))
            lfn_parts.clear()
            name = long_name or decode_short_name(entry)
            modified = dos_datetime(u16(entry, 24), u16(entry, 22))
            yield DirectoryEntry(
                name=name,
                attributes=attributes,
                cluster=u16(entry, 26),
                size=u32(entry, 28),
                modified=modified,
            )


def partition_table(image: BinaryIO) -> list[dict[str, int]]:
    image.seek(0)
    mbr = image.read(SECTOR_SIZE)
    if len(mbr) != SECTOR_SIZE or mbr[510:512] != b"\x55\xaa":
        raise ValueError("valid MBR signature not found")
    partitions = []
    for index in range(4):
        offset = 446 + index * 16
        partitions.append(
            {
                "index": index + 1,
                "bootable": mbr[offset] == 0x80,
                "type": mbr[offset + 4],
                "start_lba": u32(mbr, offset + 8),
                "sectors": u32(mbr, offset + 12),
            }
        )
    return partitions


def verify_fixed_vhd(path: Path) -> dict[str, object]:
    with path.open("rb") as stream:
        stream.seek(-SECTOR_SIZE, os.SEEK_END)
        footer = stream.read(SECTOR_SIZE)
    if footer[:8] != b"conectix":
        raise ValueError("fixed VHD footer not found")
    disk_type = struct.unpack_from(">I", footer, 60)[0]
    current_size = struct.unpack_from(">Q", footer, 48)[0]
    if disk_type != 2:
        raise ValueError(f"VHD disk type {disk_type} is not fixed")
    if path.stat().st_size != current_size + SECTOR_SIZE:
        raise ValueError("VHD size does not agree with its footer")
    return {"disk_type": "fixed", "virtual_bytes": current_size}


def extract_tree(fs: Fat16, destination: Path) -> list[dict[str, object]]:
    files: list[dict[str, object]] = []
    visited_directories: set[int] = set()

    def visit(cluster: int | None, relative: PurePosixPath) -> None:
        if cluster is not None:
            if cluster in visited_directories:
                raise ValueError(f"directory cycle at cluster {cluster}")
            visited_directories.add(cluster)

        for entry in fs.entries(cluster):
            if entry.name in (".", ".."):
                continue
            component = safe_component(entry.name)
            item_relative = relative / component
            output_path = destination.joinpath(*item_relative.parts)
            if entry.is_directory:
                output_path.mkdir(parents=True, exist_ok=True)
                visit(entry.cluster, item_relative)
                continue

            data = fs.read_chain(entry.cluster, entry.size) if entry.size else b""
            output_path.parent.mkdir(parents=True, exist_ok=True)
            output_path.write_bytes(data)
            if entry.modified is not None:
                timestamp = entry.modified.timestamp()
                os.utime(output_path, (timestamp, timestamp))
            files.append(
                {
                    "path": item_relative.as_posix(),
                    "bytes": entry.size,
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "attributes": f"0x{entry.attributes:02x}",
                    "first_cluster": entry.cluster,
                    "modified": entry.modified.isoformat() if entry.modified else None,
                }
            )

    destination.mkdir(parents=True, exist_ok=True)
    visit(None, PurePosixPath())
    return files


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="fixed VHD input")
    parser.add_argument("destination", type=Path, help="output directory")
    parser.add_argument(
        "--manifest",
        type=Path,
        help="JSON manifest path (default: <destination>/vhd-manifest.json)",
    )
    args = parser.parse_args()

    vhd_info = verify_fixed_vhd(args.source)
    with args.source.open("rb") as image:
        partitions = partition_table(image)
        partition = next(
            (item for item in partitions if item["type"] in FAT16_PARTITION_TYPES),
            None,
        )
        if partition is None:
            raise ValueError("no FAT16 partition found")
        fs = Fat16(image, partition["start_lba"])
        files = extract_tree(fs, args.destination)

    manifest = {
        "format": "fixed VHD / MBR / FAT16",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "source": str(args.source.resolve()),
        "source_bytes": args.source.stat().st_size,
        "source_sha256": sha256_file(args.source),
        "vhd": vhd_info,
        "partitions": partitions,
        "filesystem": {
            "type": fs.fs_type,
            "volume_label": fs.volume_label,
            "bytes_per_sector": fs.bytes_per_sector,
            "sectors_per_cluster": fs.sectors_per_cluster,
            "cluster_bytes": fs.cluster_size,
        },
        "file_count": len(files),
        "files": files,
    }
    manifest_path = args.manifest or args.destination / "vhd-manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "source_sha256": manifest["source_sha256"],
                "file_count": len(files),
                "manifest": str(manifest_path.resolve()),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
