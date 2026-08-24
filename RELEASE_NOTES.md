# Ms. Metaverse Native 1.0.0

The first complete native Windows release of the offline 1995 FMV game flow.

## Highlights

- Native x64 Win32 executable; no emulator, original installer, mounted CD, or
  third-party runtime DLLs required.
- Complete two-disc flow: profiles, Cryo selection, Weights, ORDER, navigation,
  Brains, Looks, Talent, Gong/Penalty, Slots, encounters, tally, winner, replay,
  and credits.
- Original Cinepak, Microsoft Video 1, RLE8, PCM, bitmap, and data assets are
  consumed directly without lossy conversion.
- Original timing, persistent file formats, economy, random/rotation state,
  cutscene boundaries, and judging rules recovered and audited.
- `Alt+Enter` toggles fullscreen; `Ctrl+Alt+F1` sets the balance to $999999.
- Physical-disc/copy-protection checks are intentionally removed.

## Required original data

This release does **not** include copyrighted game assets. Supply a lawfully
obtained copy of both original discs and place their `BMP`, `DAT`, `MOV`, and
`WAV` folders beside the EXE as `assets` (Disc I) and `assets2` (Disc II). Full
extraction instructions are in the included README.

## Downloads

- `ms-metaverse-native-v1.0.0-windows-x64.zip`: application and documentation
- `ffmpeg-8.1.2.tar.xz`: exact corresponding FFmpeg source for the statically
  linked LGPL component
- `ffmpeg-8.1.2.tar.xz.asc`: FFmpeg's detached signature for that source
- `SHA256SUMS.txt`: release checksums

The Windows executable is unsigned, so SmartScreen may show an unrecognized
publisher warning. Verify the published SHA-256 checksums before running it.
