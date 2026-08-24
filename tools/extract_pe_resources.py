#!/usr/bin/env python3
"""Extract raw and readily convertible resources from a PE executable."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

import pefile


RESOURCE_TYPES = {
    1: "CURSOR",
    2: "BITMAP",
    3: "ICON",
    4: "MENU",
    5: "DIALOG",
    6: "STRING",
    7: "FONTDIR",
    8: "FONT",
    9: "ACCELERATOR",
    10: "RCDATA",
    11: "MESSAGETABLE",
    12: "GROUP_CURSOR",
    14: "GROUP_ICON",
    16: "VERSION",
    24: "MANIFEST",
}


def resource_name(entry: object) -> str:
    name = getattr(entry, "name", None)
    if name is not None:
        return str(name)
    return str(entry.struct.Id)


def safe_name(name: str) -> str:
    translation = str.maketrans({c: "_" for c in '<>:"/\\|?*'})
    cleaned = "".join("_" if ord(c) < 32 else c for c in name).translate(translation)
    return cleaned.rstrip(" .") or "_"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dib_to_bmp(data: bytes) -> bytes:
    if len(data) < 12:
        raise ValueError("DIB is too short")
    header_size = struct.unpack_from("<I", data, 0)[0]
    masks = 0
    if header_size == 12:
        bits = struct.unpack_from("<H", data, 10)[0]
        palette_entries = (1 << bits) if bits <= 8 else 0
        palette_bytes = palette_entries * 3
    elif header_size >= 40 and len(data) >= 40:
        bits = struct.unpack_from("<H", data, 14)[0]
        compression = struct.unpack_from("<I", data, 16)[0]
        colors_used = struct.unpack_from("<I", data, 32)[0]
        palette_entries = colors_used or ((1 << bits) if bits <= 8 else 0)
        palette_bytes = palette_entries * 4
        if header_size == 40 and compression in (3, 6):
            masks = 16 if compression == 6 else 12
    else:
        raise ValueError(f"unsupported DIB header size {header_size}")
    pixel_offset = 14 + header_size + masks + palette_bytes
    return b"BM" + struct.pack("<IHHI", 14 + len(data), 0, 0, pixel_offset) + data


def icon_to_ico(data: bytes) -> bytes:
    if len(data) < 16:
        raise ValueError("icon DIB is too short")
    header_size = struct.unpack_from("<I", data, 0)[0]
    if header_size < 12:
        raise ValueError("invalid icon DIB")
    if header_size == 12:
        width = struct.unpack_from("<H", data, 4)[0]
        height = struct.unpack_from("<H", data, 6)[0] // 2
        planes = struct.unpack_from("<H", data, 8)[0]
        bits = struct.unpack_from("<H", data, 10)[0]
    else:
        width = abs(struct.unpack_from("<i", data, 4)[0])
        height = abs(struct.unpack_from("<i", data, 8)[0]) // 2
        planes = struct.unpack_from("<H", data, 12)[0]
        bits = struct.unpack_from("<H", data, 14)[0]
    width_byte = 0 if width >= 256 else width
    height_byte = 0 if height >= 256 else height
    colors = 0 if bits >= 8 else min(255, 1 << bits)
    header = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack(
        "<BBBBHHII",
        width_byte,
        height_byte,
        colors,
        0,
        planes,
        bits,
        len(data),
        22,
    )
    return header + entry + data


def group_icon_to_ico(group: bytes, icon_images: dict[int, bytes]) -> bytes:
    """Rebuild a multi-image ICO from RT_GROUP_ICON and RT_ICON blobs."""
    if len(group) < 6:
        raise ValueError("group icon is too short")
    reserved, resource_type, count = struct.unpack_from("<HHH", group, 0)
    if reserved != 0 or resource_type != 1 or count == 0:
        raise ValueError("invalid group icon header")
    if len(group) != 6 + count * 14:
        raise ValueError("invalid group icon entry table")

    directory_entries: list[bytes] = []
    images: list[bytes] = []
    image_offset = 6 + count * 16
    for index in range(count):
        (
            width,
            height,
            colors,
            entry_reserved,
            planes,
            bits,
            declared_size,
            image_id,
        ) = struct.unpack_from("<BBBBHHIH", group, 6 + index * 14)
        raw = icon_images.get(image_id)
        if raw is None:
            raise ValueError(f"missing RT_ICON image {image_id}")
        if declared_size != len(raw):
            raise ValueError(f"invalid RT_ICON image {image_id}")
        directory_entries.append(
            struct.pack(
                "<BBBBHHII",
                width,
                height,
                colors,
                entry_reserved,
                planes,
                bits,
                len(raw),
                image_offset,
            )
        )
        images.append(raw)
        image_offset += len(raw)
    return struct.pack("<HHH", 0, 1, count) + b"".join(
        directory_entries + images
    )


def group_cursor_to_cur(
    group: bytes, cursor_images: dict[int, bytes]
) -> bytes:
    """Rebuild a standalone CUR from RT_GROUP_CURSOR and RT_CURSOR blobs."""
    if len(group) < 6:
        raise ValueError("group cursor is too short")
    reserved, resource_type, count = struct.unpack_from("<HHH", group, 0)
    if reserved != 0 or resource_type != 2 or count == 0:
        raise ValueError("invalid group cursor header")
    if len(group) != 6 + count * 14:
        raise ValueError("invalid group cursor entry table")

    directory_entries: list[bytes] = []
    images: list[bytes] = []
    image_offset = 6 + count * 16
    for index in range(count):
        width, stored_height, _planes, bits, declared_size, image_id = (
            struct.unpack_from("<HHHHIH", group, 6 + index * 14)
        )
        raw = cursor_images.get(image_id)
        if raw is None:
            raise ValueError(f"missing RT_CURSOR image {image_id}")
        if len(raw) < 4 or declared_size != len(raw):
            raise ValueError(f"invalid RT_CURSOR image {image_id}")
        hotspot_x, hotspot_y = struct.unpack_from("<HH", raw, 0)
        image = raw[4:]
        height = stored_height // 2
        width_byte = 0 if width >= 256 else width
        height_byte = 0 if height >= 256 else height
        colors = 0 if bits >= 8 else min(255, 1 << bits)
        directory_entries.append(
            struct.pack(
                "<BBBBHHII",
                width_byte,
                height_byte,
                colors,
                0,
                hotspot_x,
                hotspot_y,
                len(image),
                image_offset,
            )
        )
        images.append(image)
        image_offset += len(image)
    return struct.pack("<HHH", 0, 2, count) + b"".join(directory_entries + images)


def decode_string_table(block_id: int, data: bytes) -> dict[int, str]:
    strings: dict[int, str] = {}
    offset = 0
    for index in range(16):
        if offset + 2 > len(data):
            break
        length = struct.unpack_from("<H", data, offset)[0]
        offset += 2
        byte_count = length * 2
        value = data[offset : offset + byte_count].decode("utf-16le", errors="replace")
        offset += byte_count
        if value:
            strings[(block_id - 1) * 16 + index] = value
    return strings


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="PE executable or DLL")
    parser.add_argument("destination", type=Path, help="output directory")
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=True)

    pe = pefile.PE(str(args.source), fast_load=False)
    if not hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
        manifest = {
            "source": str(args.source.resolve()),
            "source_sha256": sha256(args.source.read_bytes()),
            "machine": f"0x{pe.FILE_HEADER.Machine:04x}",
            "resource_count": 0,
            "string_count": 0,
            "resources": [],
        }
        manifest_path = args.destination / "pe-resource-manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"resource_count": 0, "string_count": 0, "manifest": str(manifest_path.resolve())}, indent=2))
        return
    image = pe.get_memory_mapped_image()
    resources: list[dict[str, object]] = []
    strings: dict[int, str] = {}
    cursor_images: dict[tuple[int, int], bytes] = {}
    cursor_groups: list[
        tuple[str, int, bytes, dict[str, object]]
    ] = []
    icon_images: dict[tuple[int, int], bytes] = {}
    icon_groups: list[
        tuple[str, int, bytes, dict[str, object]]
    ] = []

    for type_entry in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        type_id = type_entry.struct.Id if type_entry.name is None else None
        type_name = RESOURCE_TYPES.get(type_id, resource_name(type_entry))
        for name_entry in type_entry.directory.entries:
            item_name = resource_name(name_entry)
            for language_entry in name_entry.directory.entries:
                data_entry = language_entry.data.struct
                data = bytes(
                    image[
                        data_entry.OffsetToData : data_entry.OffsetToData + data_entry.Size
                    ]
                )
                language = language_entry.struct.Id
                relative = Path("raw") / safe_name(type_name) / safe_name(item_name)
                raw_path = args.destination / relative / f"{language}.bin"
                raw_path.parent.mkdir(parents=True, exist_ok=True)
                raw_path.write_bytes(data)
                converted: str | None = None
                if type_id == 2:
                    converted_path = args.destination / "converted" / "BITMAP" / f"{safe_name(item_name)}-{language}.bmp"
                    converted_path.parent.mkdir(parents=True, exist_ok=True)
                    converted_path.write_bytes(dib_to_bmp(data))
                    converted = converted_path.relative_to(args.destination).as_posix()
                elif type_id == 3:
                    converted_path = args.destination / "converted" / "ICON" / f"{safe_name(item_name)}-{language}.ico"
                    converted_path.parent.mkdir(parents=True, exist_ok=True)
                    converted_path.write_bytes(icon_to_ico(data))
                    converted = converted_path.relative_to(args.destination).as_posix()
                elif type_id == 6 and item_name.isdigit():
                    strings.update(decode_string_table(int(item_name), data))
                record: dict[str, object] = {
                    "type": type_name,
                    "type_id": type_id,
                    "name": item_name,
                    "language": language,
                    "rva": data_entry.OffsetToData,
                    "bytes": len(data),
                    "sha256": sha256(data),
                    "raw": raw_path.relative_to(args.destination).as_posix(),
                    "converted": converted,
                }
                resources.append(record)
                if type_id == 1 and item_name.isdigit():
                    cursor_images[(int(item_name), language)] = data
                elif type_id == 12:
                    cursor_groups.append((item_name, language, data, record))
                elif type_id == 3 and item_name.isdigit():
                    icon_images[(int(item_name), language)] = data
                elif type_id == 14:
                    icon_groups.append((item_name, language, data, record))

    for item_name, language, group, record in cursor_groups:
        language_images = {
            image_id: data
            for (image_id, image_language), data in cursor_images.items()
            if image_language == language
        }
        converted_path = (
            args.destination / "converted" / "CURSOR" /
            f"{safe_name(item_name)}-{language}.cur"
        )
        converted_path.parent.mkdir(parents=True, exist_ok=True)
        converted_path.write_bytes(group_cursor_to_cur(group, language_images))
        record["converted"] = converted_path.relative_to(
            args.destination
        ).as_posix()

    for item_name, language, group, record in icon_groups:
        language_images = {
            image_id: data
            for (image_id, image_language), data in icon_images.items()
            if image_language == language
        }
        converted_path = (
            args.destination / "converted" / "ICON_GROUP" /
            f"{safe_name(item_name)}-{language}.ico"
        )
        converted_path.parent.mkdir(parents=True, exist_ok=True)
        converted_path.write_bytes(group_icon_to_ico(group, language_images))
        record["converted"] = converted_path.relative_to(
            args.destination
        ).as_posix()

    if strings:
        (args.destination / "strings.json").write_text(
            json.dumps(strings, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    manifest = {
        "source": str(args.source.resolve()),
        "source_sha256": sha256(args.source.read_bytes()),
        "machine": f"0x{pe.FILE_HEADER.Machine:04x}",
        "resource_count": len(resources),
        "string_count": len(strings),
        "resources": resources,
    }
    manifest_path = args.destination / "pe-resource-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "resource_count": len(resources),
                "string_count": len(strings),
                "manifest": str(manifest_path.resolve()),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
