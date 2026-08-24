# Offline native-port completion audit

## Reference and scope

The instruction-level reference is the CD's Windows 95 PE32 build,
`SETUP32/DATA/MM.EXE` (264,704 bytes, SHA-256
`8074816cb59ff84f8ca91e897672c8b8325fcc0b7cf9cbe3d21eb89f17489f9b`).
The VHD supplies the installed Windows 3.1 build, legacy state templates, and
runtime configuration evidence. Its `MM.EXE` is byte-identical to the CD's
`SETUP16` build; it is not silently treated as the same executable.

“Complete” here means the recovered two-disc offline game: profile/login,
intro, Cryo selection, Weight, ORDER, all seven navigation graphs, Simm
encounters, slots, the three judging pavilions, tally/winner/replay, credits,
media timing, painting/input state, and persistence. It deliberately excludes:

- all physical-CD, drive-letter, `VOL.DAT`, volume-label, and insert-disc logic,
  as requested;
- the retired Virtual Vegas network/FTP service, which has no functioning
  backend and is not needed by the offline routes.

## Evidence matrix

| Objective | Evidence | Result |
|---|---|---|
| Reproducible asset extraction | Immutable BIN/CUE/VHD and normalized-ISO hashes in `media-inventory.md`; extraction tools retain ISO, HFS resource forks, FAT16 files, and PE resources | Complete |
| Ripped game data | Package contains 721 Disk-I and 431 Disk-II data files; the two roots remain separate so five conflicting same-path files keep the correct bytes | Complete |
| Function-level behavior | `function-parity.md` accounts for all 1,046 recovered PE32 functions: 100 implemented/instruction-verified, 928 replaced platform/runtime, nine replaced bundled-media/CD routines, nine obsolete-online routines, zero partial or unmapped | Complete for scoped offline target |
| Video triggers | `media-trigger-audit.md` reconciles all 266 installed AVIs: 265 active files have owning triggers and exact boundaries; `MOV/HOST/X11DB.AVI` is proven movie-only/inactive | Complete |
| Navigation/media timing | Package probe checks seven DAT graphs, 306 range endpoints, both embedded-audio backgrounds, all host C/X families, centered movies, Penalty/performance media, and the one legal EOF clamp | Complete |
| UI state and input | Focused native tests cover recovered hit rectangles, pressed/dirty layers, default dialog routes, activation handlers, and state mutation order; the GUI audit drives the actual EXE through profile Cancel/close/Enter, intro Cancel/close/retired-key rejection, intro completion, navigation Enter, and both `Alt+Enter` fullscreen transitions | Complete, no known divergent route |
| Persistence | Regression coverage includes exact 28-byte `MM.DAT`, 130-byte `NRFPAV.DAT`, and 180-byte `HNRD.DAT` semantics, malformed tails, raw float bits, guest/DEMO exclusions, and update timing | Complete |
| No original-CD dependency | Runtime resolves `assets` and `assets2`; package contains zero `VOL.DAT` files and the GUI audit launches without `--assets`/`--assets2` | Complete |
| Self-contained executable | Static FFmpeg, libgcc, and libstdc++ linkage; import check permits only Windows system/API-set DLLs; original cursors and application icon are embedded | Complete |

## Verification commands

```powershell
./scripts/test.ps1
./scripts/package-native.ps1 -SkipBuild
python tools/audit_function_parity.py
```

The final package is `dist/ms-metaverse-native`. It contains exactly one EXE,
no DLL files, no original installer executable, and no `VOL.DAT`. The game data
is intentionally adjacent to the EXE in `assets` and `assets2`; those are data
files, not additional libraries. The final executable is 5,448,871 bytes with
SHA-256
`F0E40AC8193B0FD66C633E3AC5517DAD197D3190731AC62669993D7F8360A7AC`.

No unfinished-source marker, partial parity row, unmapped recovered function,
or active unowned AVI remains in the scoped offline port.
