# Changelog

All notable project changes are documented here.

## 1.0.0 - 2026-08-24

- Reimplemented the complete offline Windows game flow as a native x64 Win32
  application, with all 100 recovered game-specific functions accounted for.
- Preserved the original two-disc data split, 640x480 presentation, profile
  records, random/rotation state, judging economy, navigation, slots, tally,
  and authored media timing.
- Added native playback for Cinepak, Microsoft Video 1, RLE8, and PCM media via
  a statically linked minimal FFmpeg 8.1.2 build.
- Fixed cutscene skip teardown, full-window flicker, contestant selection,
  judge-meter reset, Gong/Penalty behavior, and Looks magnifier region cycling.
- Added `Alt+Enter` fullscreen and the `Ctrl+Alt+F1` $999999 accessibility/test
  shortcut.
- Removed physical-disc/copy-protection checks and third-party runtime DLL
  requirements.
- Added reproducible extraction, parity audits, media probes, UI smoke tests,
  public-release packaging, licensing, credits, and provenance documentation.
