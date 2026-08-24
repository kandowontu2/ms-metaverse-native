#!/usr/bin/env python3
"""Hash and identify extracted Ms. Metaverse assets."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
from collections import Counter
from pathlib import Path
from typing import Iterator


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def fourcc(data: bytes) -> str:
    if data == b"\x00\x00\x00\x00":
        return "BI_RGB"
    return data.decode("ascii", errors="replace").rstrip("\x00 ")


def bitmap_compression(data: bytes) -> str:
    value = u32(data, 0)
    names = {
        0: "BI_RGB",
        1: "BI_RLE8",
        2: "BI_RLE4",
        3: "BI_BITFIELDS",
        4: "BI_JPEG",
        5: "BI_PNG",
        6: "BI_ALPHABITFIELDS",
    }
    return names.get(value, fourcc(data))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def riff_chunks(data: bytes, start: int, end: int) -> Iterator[tuple[bytes, int, int]]:
    offset = start
    while offset + 8 <= end and offset + 8 <= len(data):
        chunk_id = data[offset : offset + 4]
        size = u32(data, offset + 4)
        body = offset + 8
        body_end = min(body + size, end, len(data))
        yield chunk_id, body, body_end
        next_offset = body + size + (size & 1)
        if next_offset <= offset:
            break
        offset = next_offset


def find_riff_chunk(data: bytes, chunk_id: bytes) -> tuple[int, int] | None:
    for current_id, start, end in riff_chunks(data, 12, len(data)):
        if current_id == chunk_id:
            return start, end
    return None


def parse_bmp(path: Path) -> dict[str, object]:
    with path.open("rb") as stream:
        data = stream.read(128)
    if len(data) < 30 or data[:2] != b"BM":
        return {"kind": "bmp", "valid": False}
    dib_size = u32(data, 14)
    if dib_size == 12 and len(data) >= 26:
        width = u16(data, 18)
        height = u16(data, 20)
        bits = u16(data, 24)
        compression = "BI_RGB"
    elif dib_size >= 40 and len(data) >= 34:
        width = i32(data, 18)
        height = abs(i32(data, 22))
        bits = u16(data, 28)
        compression = bitmap_compression(data[30:34])
    else:
        return {"kind": "bmp", "valid": False, "dib_header_bytes": dib_size}
    return {
        "kind": "bmp",
        "valid": True,
        "width": width,
        "height": height,
        "bits_per_pixel": bits,
        "compression": compression,
    }


def parse_wav(path: Path) -> dict[str, object]:
    with path.open("rb") as stream:
        data = stream.read(min(path.stat().st_size, 1024 * 1024))
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return {"kind": "wav", "valid": False}
    fmt = find_riff_chunk(data, b"fmt ")
    payload = find_riff_chunk(data, b"data")
    if fmt is None or fmt[1] - fmt[0] < 16:
        return {"kind": "wav", "valid": False}
    start, _end = fmt
    format_tag = u16(data, start)
    channels = u16(data, start + 2)
    sample_rate = u32(data, start + 4)
    byte_rate = u32(data, start + 8)
    bits = u16(data, start + 14)
    data_bytes = (payload[1] - payload[0]) if payload else None
    duration = (data_bytes / byte_rate) if data_bytes is not None and byte_rate else None
    return {
        "kind": "wav",
        "valid": True,
        "format_tag": f"0x{format_tag:04x}",
        "channels": channels,
        "sample_rate": sample_rate,
        "bits_per_sample": bits,
        "data_bytes": data_bytes,
        "duration_seconds": round(duration, 6) if duration is not None else None,
    }


def parse_avi(path: Path) -> dict[str, object]:
    with path.open("rb") as stream:
        data = stream.read(min(path.stat().st_size, 4 * 1024 * 1024))
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"AVI ":
        return {"kind": "avi", "valid": False}

    main: dict[str, object] = {"kind": "avi", "valid": True, "streams": []}
    header_list: tuple[int, int] | None = None
    for chunk_id, start, end in riff_chunks(data, 12, len(data)):
        if chunk_id == b"LIST" and end - start >= 4 and data[start : start + 4] == b"hdrl":
            header_list = (start + 4, end)
            break
    if header_list is None:
        main["valid"] = False
        return main

    for chunk_id, start, end in riff_chunks(data, *header_list):
        if chunk_id == b"avih" and end - start >= 40:
            microseconds = u32(data, start)
            total_frames = u32(data, start + 16)
            main.update(
                {
                    "width": u32(data, start + 32),
                    "height": u32(data, start + 36),
                    "frame_count": total_frames,
                    "microseconds_per_frame": microseconds,
                    "duration_seconds": round(total_frames * microseconds / 1_000_000, 6),
                }
            )
            continue
        if chunk_id != b"LIST" or end - start < 4 or data[start : start + 4] != b"strl":
            continue

        stream_header: bytes | None = None
        stream_format: bytes | None = None
        for sub_id, sub_start, sub_end in riff_chunks(data, start + 4, end):
            if sub_id == b"strh":
                stream_header = data[sub_start:sub_end]
            elif sub_id == b"strf":
                stream_format = data[sub_start:sub_end]
        if stream_header is None or len(stream_header) < 48:
            continue

        stream_type = fourcc(stream_header[0:4])
        handler = fourcc(stream_header[4:8])
        scale = u32(stream_header, 20)
        rate = u32(stream_header, 24)
        length = u32(stream_header, 32)
        stream: dict[str, object] = {
            "type": stream_type,
            "handler": handler,
            "scale": scale,
            "rate": rate,
            "length": length,
            "duration_seconds": round(length * scale / rate, 6) if rate else None,
        }
        if stream_type == "vids" and stream_format and len(stream_format) >= 20:
            stream.update(
                {
                    "width": i32(stream_format, 4),
                    "height": abs(i32(stream_format, 8)),
                    "bits_per_pixel": u16(stream_format, 14),
                    "compression": bitmap_compression(stream_format[16:20]),
                }
            )
        elif stream_type == "auds" and stream_format and len(stream_format) >= 16:
            stream.update(
                {
                    "format_tag": f"0x{u16(stream_format, 0):04x}",
                    "channels": u16(stream_format, 2),
                    "sample_rate": u32(stream_format, 4),
                    "bits_per_sample": u16(stream_format, 14),
                }
            )
        main["streams"].append(stream)
    return main


def metadata(path: Path) -> dict[str, object]:
    suffix = path.suffix.lower()
    if suffix == ".bmp":
        return parse_bmp(path)
    if suffix == ".wav":
        return parse_wav(path)
    if suffix == ".avi":
        return parse_avi(path)
    return {"kind": suffix.removeprefix(".") or "no-extension"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="extracted asset tree")
    parser.add_argument("destination", type=Path, help="output manifest directory")
    args = parser.parse_args()
    args.destination.mkdir(parents=True, exist_ok=True)

    items: list[dict[str, object]] = []
    for path in sorted(item for item in args.source.rglob("*") if item.is_file()):
        if path.name.endswith("-manifest.json"):
            continue
        relative = path.relative_to(args.source).as_posix()
        item = {
            "path": relative,
            "bytes": path.stat().st_size,
            "sha256": sha256_file(path),
            **metadata(path),
        }
        items.append(item)

    extensions = Counter(Path(str(item["path"])).suffix.lower() or "<none>" for item in items)
    video_codecs: Counter[str] = Counter()
    audio_formats: Counter[str] = Counter()
    bitmap_formats: Counter[str] = Counter()
    for item in items:
        if item["kind"] == "avi":
            for stream in item.get("streams", []):
                if stream["type"] == "vids":
                    video_codecs[str(stream.get("compression") or stream.get("handler"))] += 1
                elif stream["type"] == "auds":
                    audio_formats[str(stream.get("format_tag"))] += 1
        elif item["kind"] == "wav":
            audio_formats[str(item.get("format_tag"))] += 1
        elif item["kind"] == "bmp":
            bitmap_formats[
                f"{item.get('bits_per_pixel')}bpp/{item.get('compression')}"
            ] += 1

    summary = {
        "file_count": len(items),
        "total_bytes": sum(int(item["bytes"]) for item in items),
        "extensions": dict(sorted(extensions.items())),
        "video_codecs": dict(sorted(video_codecs.items())),
        "audio_formats": dict(sorted(audio_formats.items())),
        "bitmap_formats": dict(sorted(bitmap_formats.items())),
    }
    manifest = {
        "source": str(args.source.resolve()),
        "summary": summary,
        "files": items,
    }
    json_path = args.destination / "asset-inventory.json"
    csv_path = args.destination / "asset-inventory.csv"
    json_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=[
                "path",
                "bytes",
                "sha256",
                "kind",
                "width",
                "height",
                "duration_seconds",
                "bits_per_pixel",
                "compression",
                "format_tag",
                "channels",
                "sample_rate",
            ],
            extrasaction="ignore",
        )
        writer.writeheader()
        writer.writerows(items)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
