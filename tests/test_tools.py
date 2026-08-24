from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from tools.extract_hfs_resources import (
    decode_hls_escape,
    parse_catalog,
    split_macbinary,
)
from tools.extract_mode1 import SYNC, convert
from tools.extract_pe_resources import (
    dib_to_bmp,
    group_cursor_to_cur,
    group_icon_to_ico,
)
from tools.inventory_assets import bitmap_compression


class Mode1Tests(unittest.TestCase):
    def test_extracts_user_payload_and_hashes_it(self) -> None:
        payload = bytes(index & 0xFF for index in range(2048))
        sector = SYNC + b"\x00\x02\x00" + b"\x01" + payload
        sector += bytes(2352 - len(sector))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "track.bin"
            destination = root / "disc.iso"
            source.write_bytes(sector)
            manifest = convert(source, destination)
            self.assertEqual(destination.read_bytes(), payload)
            self.assertEqual(manifest["sector_count"], 1)
            self.assertEqual(manifest["output_bytes"], 2048)


class HfsTests(unittest.TestCase):
    def test_decodes_hls_backslash_notation(self) -> None:
        self.assertEqual(decode_hls_escape(r"AppleShare\ PDS"), "AppleShare PDS")
        self.assertEqual(decode_hls_escape(r"Icon\r"), "Icon\r")
        self.assertEqual(decode_hls_escape(r"A\072B"), "A:B")

    def test_catalog_parser_tracks_recursive_directory_headers(self) -> None:
        listing = """   1 f  BINA/mdos        2         3 Nov  6  1995 Root\\ File
:BMP:CRYO:
   2 f  .BMP/BABL     2670     72274 Oct 17  1995 INFOBK.BMP
"""
        entries = parse_catalog(listing)
        self.assertEqual(entries[0]["path"], "Root File")
        self.assertEqual(entries[1]["path"], "BMP/CRYO/INFOBK.BMP")
        self.assertEqual(entries[1]["resource_bytes"], 2670)

    def test_splits_macbinary_forks(self) -> None:
        header = bytearray(128)
        header[1] = 4
        header[2:6] = b"TEST"
        struct.pack_into(">I", header, 83, 3)
        struct.pack_into(">I", header, 87, 2)
        blob = bytes(header) + b"abc" + bytes(125) + b"xy" + bytes(126)
        self.assertEqual(split_macbinary(blob), (b"abc", b"xy"))


class AssetMetadataTests(unittest.TestCase):
    def test_bitmap_compression_names(self) -> None:
        self.assertEqual(bitmap_compression(struct.pack("<I", 0)), "BI_RGB")
        self.assertEqual(bitmap_compression(struct.pack("<I", 1)), "BI_RLE8")
        self.assertEqual(bitmap_compression(b"cvid"), "cvid")

    def test_converts_resource_dib_to_bmp(self) -> None:
        dib = struct.pack(
            "<IiiHHIIiiII",
            40,
            1,
            1,
            1,
            24,
            0,
            4,
            0,
            0,
            0,
            0,
        ) + b"\x00\x00\xff\x00"
        bmp = dib_to_bmp(dib)
        self.assertEqual(bmp[:2], b"BM")
        self.assertEqual(struct.unpack_from("<I", bmp, 2)[0], len(bmp))
        self.assertEqual(struct.unpack_from("<I", bmp, 10)[0], 54)

    def test_rebuilds_group_cursor(self) -> None:
        dib = struct.pack(
            "<IiiHHIIiiII", 40, 32, 64, 1, 1, 0, 256, 0, 0, 0, 0
        ) + bytes(256)
        raw_cursor = struct.pack("<HH", 4, 7) + dib
        group = struct.pack("<HHH", 0, 2, 1) + struct.pack(
            "<HHHHIH", 32, 64, 1, 1, len(raw_cursor), 9
        )
        cursor = group_cursor_to_cur(group, {9: raw_cursor})
        self.assertEqual(struct.unpack_from("<HHH", cursor, 0), (0, 2, 1))
        self.assertEqual(cursor[6:10], bytes((32, 32, 2, 0)))
        self.assertEqual(struct.unpack_from("<HH", cursor, 10), (4, 7))
        self.assertEqual(struct.unpack_from("<I", cursor, 18)[0], 22)
        self.assertEqual(cursor[22:], dib)

    def test_rebuilds_multi_image_group_icon(self) -> None:
        first = b"first-icon-image"
        second = b"second-icon"
        group = (
            struct.pack("<HHH", 0, 1, 2)
            + struct.pack(
                "<BBBBHHIH", 32, 32, 16, 0, 1, 4, len(first), 13
            )
            + struct.pack(
                "<BBBBHHIH", 16, 16, 2, 0, 1, 1, len(second), 14
            )
        )
        icon = group_icon_to_ico(group, {13: first, 14: second})
        self.assertEqual(struct.unpack_from("<HHH", icon, 0), (0, 1, 2))
        self.assertEqual(icon[6:10], bytes((32, 32, 16, 0)))
        self.assertEqual(icon[22:26], bytes((16, 16, 2, 0)))
        self.assertEqual(struct.unpack_from("<I", icon, 18)[0], 38)
        self.assertEqual(
            struct.unpack_from("<I", icon, 34)[0], 38 + len(first)
        )
        self.assertEqual(icon[38:], first + second)


if __name__ == "__main__":
    unittest.main()
