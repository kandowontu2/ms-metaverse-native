# Media and asset inventory

## Immutable source fingerprints

| Input | Bytes | SHA-256 |
|---|---:|---|
| `METAVERSE1.bin` | 744,746,688 | `132a60b57d98490cbc33ca1dfdeee9049f39601607b5a27de12a3642cbadabf4` |
| `METAVERSE1.cue` | 76 | `881f051f5d77c8844bd3f5661445f93bef74657924582d8ed9614d7dba237f64` |
| `METAVERSE2.bin` | 772,608,480 | `8a51b5246c79ecebd4a36fe34796f6ebe8708c6843e31596f5b6630100a1a2f4` |
| `METAVERSE2.cue` | 76 | `8ee6afe0964c4623d5b6226d29c954a0d486c068eb969f606f84ff19d074cc46` |
| `msmetaverse.vhd` | 35,652,096 | `775d927bb4c290c3f19b812ed079a75e2b224d072c2b942614a483ccb6c6b1fa` |

The CUE describes one `MODE1/2352` track. Normalizing the 316,644 raw sectors
produces a 648,486,912-byte ISO user-data image with SHA-256
`11426ed79d98812a6e2a585383f9347183bc3dab43176d65274ad2920dd1b8cc`.
Disk II has 328,490 raw sectors and normalizes to a 672,747,520-byte ISO image
with SHA-256
`7f2dc0825ddeaae2bc5d9ccd69c376ec4dafb1d57059e9b277a7dd2d2ace64ff`.

The fixed VHD contains a bootable FAT16 partition at LBA 63. The partition has
66,465 sectors, 512-byte sectors, four sectors per cluster, and 498 live files.
It is a Windows 3.1/DOS installation whose game files live in `C:\VVEGAS\MM`.

## Hybrid CD filesystems

The logical CD image contains both ISO 9660 and Apple HFS views:

- ISO 9660: 828 files, including one associated-file resource fork.
- HFS volume `Ms Meta disk 1`: 831 catalog files.
- HFS resource forks: three files, 6,170 resource bytes in total.
- The HFS partition begins at 512-byte block 283 and spans 1,266,283 blocks.

The three resource-bearing HFS files are `AppleShare PDS`, the hidden volume
`Icon\r`, and `BMP:CRYO:INFOBK.BMP`. The extraction pipeline retains MacBinary II,
data-fork, and resource-fork representations.

Disk II contributes 432 ISO files and 436 HFS catalog files. Its HFS volume is
`Ms Meta disk2`; two files carry resource forks. Its PC view totals 660,958,344
bytes: 134 AVI, 164 WAV, 76 BMP, 51 MMS, six DAT, and one `.OLD` file. The AVIs
comprise 84 Cinepak, 40 RLE8, and ten Microsoft Video 1 files.

Across both ISO trees there are 987 unique case-insensitive paths. Of 273 paths
present on both discs, 268 are byte-identical. Five intentionally differ:
`BMP/NAV/N4.BMP`, `VOL.DAT`, `WAV/GONG.WAV`, `WAV/HOST/C21.WAV`, and
`WAV/HOST/C31.WAV`. The native package therefore preserves separate `assets`
and `assets2` roots instead of choosing an arbitrary overlay version.

The shared C61/C71 host-event AVIs exist only on Disk I even though both Talent
and Brains can trigger them; their WAV copies exist on both discs and are
byte-identical. The native dispatcher therefore resolves C61/C71 from the Disk
I root. C21 resolves from Disk II because its two WAV copies are intentionally
different. These distribution invariants are enforced by the packaged probe.

Disk II closes the content gaps from Disk I with 70 `MOV/TALENT` movies, ten
`MOV/TALLY/W*.AVI` winner movies, `MOV/CREDIT/CREDIT.AVI`, all 50 Talent
Judge-O-Matic comments, and the `TALNAV`, `XR2`, and `TALLY` navigation graphs.

## PC asset census

The PC view contains 828 files totaling 640,059,150 bytes after excluding its
generated manifest.

| Type | Count | Notes |
|---|---:|---|
| BMP | 309 | 305 are 8-bit `BI_RGB`; three are 1-bit; one entry is the associated resource fork |
| WAV | 227 | PCM only; predominantly mono 8-bit/11,025 Hz |
| AVI | 132 | 587,500,004 bytes; about 85.7 minutes total |
| MMS | 51 | Binary sprite-motion scripts |
| DAT | 16 | Mostly textual scene/navigation definitions; several save/state templates are binary |
| EXE | 9 | Installers plus 16-bit and 32-bit game builds |
| DLL | 6 | Includes sprite and sound-mixer runtimes |

AVI audio is PCM. The complete video codec set is bounded to:

| Codec | Files | Native decoder name |
|---|---:|---|
| Cinepak (`cvid`) | 72 | `cinepak` |
| 8-bit RLE (`BI_RLE8`) | 50 | `msrle` |
| Microsoft Video 1 (`CRAM`) | 10 | `msvideo1` |

Some AVI sprites have odd dimensions (for example 211×240), so lossy MP4
normalization would change dimensions or chroma-key edges. The native port
therefore decodes the original AVIs directly.

## Embedded executable resources

The 32-bit `MM.EXE` contains 78 PE resources:

- 34 cursors and 23 cursor groups;
- 10 string-table blocks containing 47 non-empty strings;
- four dialogs;
- two bitmaps and two icons;
- one icon group, one menu, and one version resource.

`tools/extract_pe_resources.py` preserves every resource in raw form, converts
standalone bitmaps, and reconstructs grouped cursor and multi-image icon files.
The native EXE embeds all nine reachable gameplay cursors plus original group
icon 178 from these lossless reconstructions.
