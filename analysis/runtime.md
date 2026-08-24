# Original runtime analysis

## Executable variants

The installed VHD copy is the Windows 3.1 build:

- `MM.EXE`, 189,072 bytes, NE/Windows 3.10;
- SHA-256 `be49ba7c8a4d74edbc5845e6a99d538b369d8182c2b17819fc5495652fb3ee1a`;
- byte-identical to `SETUP16/DATA/MM.EXE` on the CD.

The preferred behavioral reference is the Windows 95 build:

- `SETUP32/DATA/MM.EXE`, 264,704 bytes, PE32/i386 GUI;
- SHA-256 `8074816cb59ff84f8ca91e897672c8b8325fcc0b7cf9cbe3d21eb89f17489f9b`;
- link timestamp 1995-11-07 00:33:57;
- 1,046 functions identified by static analysis.

The 32-bit executable uses MFC-style document/window infrastructure and imports
Win32 APIs from `USER32`, `GDI32`, `KERNEL32`, `WINMM`, `MSVFW32`, `AVIFIL32`,
`WSOCK32`, `ADVAPI32`, `COMDLG32`, `SHELL32`, and `WINSPOOL`.

## Bundled runtime libraries

`SPR.DLL` is the game-specific sprite runtime. Its public API is:

- `CapBackground`
- `DeleteSpriteList`
- `LoadAVISprite`
- `LoadBackground`
- `LoadSprite`
- `SetSpritePosition`
- `SetSpriteZOrder`
- `SpriteCleanUp`
- `SpriteFromHdc`
- `SpriteHitTest`
- `UpdatePositions`

It decodes AVI frames through Video for Windows and composites them with GDI.

`WAVMIX32.DLL` is Microsoft’s 1993 real-time WaveMix library. The game uses its
session/channel/open/play/pump calls for overlapping PCM effects. The native port
does not need to reproduce its ABI; it needs to preserve channel overlap and
timing behavior.

## Installation and disc assumptions

The 32-bit build queries:

- registry key `SOFTWARE\Virtual Vegas\Ms Metaverse\1.0\MM`;
- string value `mmpath`;
- a CD-ROM drive containing `VOL.DAT`;
- an install-local `WAVEMIX.INI` and state files such as `TLST.DAT`.

The Windows 3.1 install uses `WINDOWS\VVEGAS.INI` with
`mmpath=C:\VVEGAS\MM`. The native port replaces both registry and CD-drive
discovery with explicit per-disc asset-root resolvers (`--assets` /
`MS_METAVERSE_ASSETS` and `--assets2` / `MS_METAVERSE_ASSETS2`). Keeping the
volumes distinct also preserves the five shared paths whose contents differ by
disc.

## Static-analysis artifacts

Generated, ignored analysis output under `artifacts/disassembly` includes:

- full Intel-syntax disassembly and PE headers for `MM.EXE`, `SPR.DLL`, and
  `WAVMIX32.DLL`;
- import, function, and call-graph JSON;
- address-tagged strings;
- focused pseudo-code for startup, install discovery, cryo, brains, navigation,
  weights, and sprite update paths.
