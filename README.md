# Ms. Metaverse Native

Version 1.0 is a clean-room, native x64 Windows reimplementation of the offline
1995 FMV game _Ms. Metaverse_. It preserves the original two-disc game flow and
media without emulation, installation, physical-CD checks, or non-system
runtime DLLs.

> **Original game data is not included.** You must supply a lawfully obtained
> copy of both original discs. This repository and its releases contain no
> original video, audio, bitmap, data, installer, disc image, icon, or cursor
> content.

The source written for this port is MIT-licensed. FFmpeg retains its LGPL
license. The original game and all of its content remain the property of their
respective rights holders. This is an unofficial preservation project and is
not endorsed by or affiliated with the original creators or rights holders.

## Install a release

1. Download and extract `ms-metaverse-native-v1.0.0-windows-x64.zip` from the
   [GitHub Releases page](https://github.com/kandowontu/ms-metaverse-native/releases/latest).
2. Extract Disc I and Disc II with the commands below.
3. Put Disc I's `BMP`, `DAT`, `MOV`, and `WAV` folders in `assets` beside the
   EXE. Put the same four folders from Disc II in `assets2`.
4. Run `ms_metaverse_native.exe`.

The EXE contains its media decoders and compiler runtime. It does not require
FFmpeg DLLs, Visual C++ redistributables, the original installer, a mounted CD,
or administrator access.

See [CREDITS.md](CREDITS.md), [CHANGELOG.md](CHANGELOG.md), and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution and release
details.

## Preservation workspace

This repository also inventories the original media, extracts its data
reproducibly, documents the recovered executable behavior, and builds the
native replacement.

The original BIN/CUE and optional VHD are treated as read-only inputs. Generated images,
extracted files, hashes, and analysis reports live under ignored workspace
directories so they cannot be mistaken for source code.

## Source media

Expected inputs:

- `METAVERSE1.bin`: raw Mode-1/2352 CD track
- `METAVERSE1.cue`: track layout
- `METAVERSE2.bin`: raw Mode-1/2352 second CD track
- `METAVERSE2.cue`: second-disc track layout
- `msmetaverse.vhd`: fixed VHD containing a FAT16 installation

## Initial extraction

```powershell
python tools/extract_mode1.py <METAVERSE1.bin> work/METAVERSE1.iso
python tools/extract_iso9660.py work/METAVERSE1.iso extracted/iso
python tools/extract_hfs_resources.py work/METAVERSE1.iso extracted/hfs
python tools/extract_mode1.py <METAVERSE2.bin> work/METAVERSE2.iso
python tools/extract_iso9660.py work/METAVERSE2.iso extracted/iso2
python tools/extract_hfs_resources.py work/METAVERSE2.iso extracted/hfs2
python tools/extract_fat16_vhd.py <msmetaverse.vhd> extracted/vhd
python tools/inventory_assets.py extracted/iso artifacts/inventory
python tools/inventory_assets.py extracted/iso2 artifacts/inventory2
python tools/extract_pe_resources.py extracted/iso/SETUP32/DATA/MM.EXE artifacts/pe/MM
```

Both tools write JSON manifests containing source and output hashes. They do not
modify the input images.

`extract_iso9660.py` keeps ISO associated files (used by this hybrid CD for
Macintosh resource forks) in a sibling `__associated__` directory so a fork and
its ordinary data file cannot overwrite one another.

The HFS resource-fork exporter requires `hfsutils` inside WSL (on Ubuntu:
`sudo apt-get install hfsutils`). It inventories the entire HFS catalog and
exports every non-empty resource fork as MacBinary II plus split `.data` and
`.rsrc` files.

## Native Windows foundation

The x64 Win32 target renders the original 8-bit bitmaps and plays PCM WAV files
directly. Its media layer streams the original Cinepak, Microsoft Video 1, and
RLE8 AVI frames through a minimal FFmpeg 8.1.2 build linked statically into the
EXE, decodes embedded AVI audio to native `waveOut` PCM playback, and loops both
without converting the assets. This avoids generation loss, retains the
odd-sized sprite frames, and requires no FFmpeg or compiler-runtime DLLs.

The executable currently provides a playable native offline flow through the
intro, the ordinary-profile entry-13 Hall, Cryo sponsorship, category weights,
the original Comments/ORDER board,
Brains, Looks, and Talent performances, all seven recovered navigation graphs,
slots, both Crossroads scenes, winner/tally movies, the credit movie, and tally.
The intro, winner, and credit movies retain their shared centered modal's blank
100 ms pre-play timer; pavilion and Penalty movies use their separate
immediate-start handlers. The shared player doubles each 320×240 movie to the
original 640×480 surface. After the centered movie child appears, a left click
uses its original stop/result-zero skip path; winner teardown clears that movie
before the replay prompt or finale sound, so its last frame cannot leak behind
the next step.
Profile selection runs before `MMINTRO.AVI`, matching the recovered top-level
controller rather than prompting after the opening movie. Profile selection
now uses the original 640×480 `PASSWORD.BMP`, exact two edit rectangles, and
painted `OK.BMP` press/release target in a borderless popup. Both fields retain
the original uppercase styles. Its vtable makes default Enter, Escape, and
window-close inert; only releasing the painted OK target submits the fields.

Cryo's C11 entry and first-sponsorship C12 use the recovered shared host
boundary: legacy audio stops, the mode-6 host clip runs modally, its wrapper is
destroyed, and `CRYO.WAV` restarts even when host media cannot open. C11 then
constructs ROTATE followed by hidden INFO in the retained Cryo dialog.
The original modal validation loop is
restored for guest, demo, returning, and newly created profiles. The original
`GUEST` and `DEMO` sessions remain non-persistent; only an authenticated or new
ordinary named profile is written to `MM.DAT`.
Legacy record loading is deliberately tolerant like MM.EXE: unrelated malformed
records and arbitrary IEEE-754 credit payloads do not reject the whole file,
and an authenticated return balance uses the recovered signed raw-bit `$400`
floor rather than a floating-point maximum.
After the intro, an ordinary or guest session now opens `HALL.DAT` at entry 13
and reaches first-round Cryo only through its Ms. Metaverse action. That branch
does not run the later replay reset or consume its extra judge-cadet RNG draws.
Before the intro, the original top orchestrator installs IDs `3,4,5,8,10`, raw
weights `5,5,5`, and an ascending host-comment table for every profile. Ordinary
play replaces them through Cryo, Weight, and ORDER; DEMO retains them, so its
Judge-O-Matic reactions now select the correct Disc II comment audio.
Navigation scenes obtain their video paths and object counts from the original
DAT files and seek/play the recovered transition frame ranges. Crossroads AVI
audio is likewise range-gated: a loaded/paused scene and a single-frame seek
remain silent, while bounded playback starts and stops the matching PCM span.
The three `0x1200` cross-scene nodes stop that range before reproducing the
original 199-pass palette-brightening recurrence; physical-CD remapping remains
disabled because both asset trees are packaged together.
Flagged
navigation nodes composite the original blue-screen `T0`–`T10` AVI sprites at
the SPR runtime's top-left coordinates, cumulative relative paths, and state transitions
from `D0`–`D10.MMS`,
including the recovered one-second trigger, voice clips, strict four-edge DLL
hit test, and 5–15%-of-sponsorship awards represented by the MMS $50/$100
success sentinels. Misses play the original `RICOCHET.WAV` without dismissing the modal
encounter. Judging uses the original performance movies,
custom host-comment ordering, sponsorship/surcharge economy, Gong and Penalty
Box rules, and exact tally behavior. Talent/Brains affordability and all three
wrong-accusation debits retain MM.EXE's single unrounded x87 multiply-add,
including its `-0.001f` tolerance and stored-float negative clamp. The C host interstitials and all 38 active
motion-scripted X score reactions play at their traced state transitions before
the flow advances. Meter selection speaks the ordered comment asynchronously;
pressed judging controls commit on release, and accepted/gonged portraits change
before their host response is shown. Every C-clip click ends that clip; an
opaque sprite hit additionally plays the original synchronous `OUCH.WAV` from
the completion routine. Named profiles persist in the legacy
28-byte format; host reaction rotation persists in the original 180-byte
`hnrd.dat` layout. The install-global Brains/Talent rotation state uses the
exact 130-byte `nrfPav.dat` format. Both rotation files update immediately for
guest, DEMO, and ordinary named rounds; profile records are omitted for both
GUEST and DEMO.
`nrfPav.dat` is reopened when each round's media variants are prepared and its
two affected lines are updated in place per contestant; `hnrd.dat` is reopened
for every score reaction, matching the install-global timing instead of using a
process-start snapshot.
Cryo uses all eleven original highlighted control/detail bitmaps and places the
five sponsored contestant portraits at the traced 60×60 bottom-strip slots.
Those highlights are pressed states, not hover effects: an action runs only
when mouse-up remains inside the same recovered rectangle. The original hidden
Ctrl-click on the money panel also grants exactly $100.
C11 runs after the original 500 ms delay inside that retained Cryo dialog;
ROTATE and hidden INFO are constructed only after it returns. Browse ordering,
all forty quarter-turns and ten wrap cases, all 60 INFO ranges, per-selection
portrait loading, and every reachable sponsorship cash boundary are tested.
On the fifth selection, ROTATE closes before INFO, active INFO audio stops, the
legacy Cryo channel and owned artwork are released, and only then are the ten
Pavilion variants drawn and written.
The post-Cryo sequence matches MM.EXE: category Weight first, then ORDER.
Weight starts at 5/5/5 and uses the exact gauges, arrows, asynchronous spoken
values, and release-to-confirm OK state. ORDER uses its exact thermometer, hand,
phrase rows, red rank labels, and release-to-assign ACCEPT state.
Judging pavilions wait for a click on any unfinished bottom portrait before
charging or loading media, then return to that chooser after each result; they
no longer force contestant order 1→5. Talent/Brains keep that chooser and its
current rating gauge wrapper alive in place through ACCEPT, Gong, correct
Penalty, and NOCASH2. A successful next surcharge resets the numeric value and
loads the visible `0.BMP` gauge. Looks likewise clears its visible gauge and
numeric value whenever a different contestant becomes active.
All three pavilion painters also restore the original live green balance at
`(465,25)` and white portrait frames. Talent/Brains add the active pending
slot's red frame only on the one painter pass armed by selection, and the exact
name-strip repaint shows the green contestant name while a bottom portrait is
hovered.
Talent/Brains preserve the original shared press flag: mouse-up on any of their
three action rectangles dispatches that release target after a press began on
one of them. Looks retains its separate per-button release code.
All three pavilion visits keep their 500 ms entry callback after C21/C31/C41 has
already been consumed. In Looks this means ROTATE construction, automatic
contestant selection/seek, and `LOOKS.WAV` remain delayed on re-entry too.

Navigation audio is destination-driven: each DAT node WAV plays once after
arrival, before any automatic follow-on hop. `XR1.DAT` positional nodes render
the original N1-N4 overlay bitmaps at their stored 640x480 coordinates, and
directional hover uses the embedded original Up/Right/Left cursor resources.
Neutral areas alternate the ripped 165/161 sparkling-hand frames every 500 ms.
XR1 also preserves its one-time sound-muted long transition and its later
held-frame shortcut; the five `0x0020` arrivals restore the original frame-25
background state before playing their destination cue.
The 320×240 navigation movies are stretched into the original 640×480 logical
canvas before DAT overlays and MMS sprites are drawn, so visual and mouse
coordinates now share the executable's coordinate space.
Valid manual departures also play the original `ZPR.WAV` effect on an
independent audio channel. The recovered `0x0900` mask suppresses it for
automatic and `0x0800` sound-bearing source nodes; slot Up/self retains it.
Right-click now opens the original six-item navigation popup in its recovered
order. Pavilion shortcuts dispatch directly, Slots selects the original XR1/XR2
variant-8 entry for the current asset tree, Exit uses the original confirmation,
and Skip Ahead is enabled only while a navigation range is active. During a
Slots wait, using Skip aborts the notified range and reveals the result
immediately; a second popup correctly shows Skip gray. Hall action
13 starts the game, actions 6/16 exit, and the retired external-game actions
return to the Hall without any physical-media prompt.
If a transition finishes while its already-open popup is still tracking, the
formerly enabled Skip command preserves the original stale-menu quirk and
forces that node's middle DAT transition.

Slots now runs as the original in-place navigation-node action rather than a
standalone approximation. Up on XR1/XR2 object 15 plays its exact self-range and
`SLOTSPIN.WAV` for 4,500 ms measured after range playback has started, then draws the three native-size ripped symbols at
their recovered coordinates, updates the balance, and plays the matching win or
loss sound. Zero credit immediately draws `NO CREDIT` without starting the reel
movie. Its recovered wait loop continues pumping queued Windows messages; this
is deliberately distinct from the blocking pavilion MCI-Wait calls. No original
disc is queried for any of these assets.

The packaged executable is self-contained: its import table contains only
Windows system DLLs. The build and packaging scripts fail if a third-party DLL
dependency reappears, and the package contains no runtime DLL files or original
installer executables. Game data remains in the ordinary data-only `assets` and
`assets2` directories (`BMP`, `DAT`, `MOV`, and `WAV`). The runtime
resolves those directories beside the EXE before considering the caller's
working directory, so Explorer and shortcut launches remain portable. It
performs no physical-CD, drive, volume-label, or copy-protection check.

The two disc roots remain separate because five shared paths have intentionally
different bytes on each original volume. The app resolves Disk-I scenes from
`assets` and Talent/Crossroads-II/tally content from `assets2`, preserving those
disc-specific variants instead of flattening them into a lossy merged tree.
The [AVI trigger audit](analysis/media-trigger-audit.md) accounts for all 266
installed movies and their playback boundaries. `MOV/HOST/X11DB.AVI` is the
sole movie-only inactive artifact; it has no script, audio, or executable
reference and is intentionally not dispatched.

```powershell
./scripts/build-native.ps1
./build/native/ms_metaverse_native.exe --assets ./extracted/iso --assets2 ./extracted/iso2
```

The required minimal static codec archives and headers are stored under
`third_party/ffmpeg-static`; no separately installed FFmpeg SDK is used.
`scripts/verify-self-contained.ps1` inspects the PE import table after every
native build and again during packaging. FFmpeg's LGPL notices are included in
that directory and copied into packaged builds. Exact build provenance is in
`third_party/ffmpeg-static/PROVENANCE.md`, and the corresponding upstream source
archive accompanies public binary releases.

Original gameplay cursors and the application icon are optional local fidelity
resources. They are never committed or published. With a lawfully owned
`MM.EXE`, run `./scripts/prepare-original-resources.ps1`; ordinary local builds
then embed the reconstructed resources. Public builds use Windows fallbacks.

Main controls:

- Press `Alt+Enter` to toggle borderless fullscreen on the current monitor.
  The original 640×480 canvas remains aspect-correct with black letterboxing,
  and mouse/cursor hit testing is translated back into original coordinates.
- Press `Ctrl+Alt+F1` to set the current player's live balance to exactly
  `$999999.00`. Named-player balances retain that value through the ordinary
  profile save on exit; Guest sessions remain intentionally non-persistent.
- Intro advances when its movie ends; after the initial 100 ms delay, a left
  click on it uses the original centered-movie skip path. The same applies to
  winner and credit movies. Pavilions and Crossroads use the original clickable
  DAT links; there is no generic stage-skip control.
- Cryo uses its five original on-screen browse/rotate/info/sponsor controls and
  all six information labels. The fifth valid sponsorship advances
  automatically; selections cannot be undone or bypassed. Each information label
  plays its original `TT0`–`TT5` spoken prompt before the matching
  contestant-specific `INFO.AVI` segment. The 60 authored segment ranges are
  taken directly from MM.EXE and every start/end frame is package-validated.
- Weights and ORDER use their painted arrow/ACCEPT/OK controls with original
  mouse-down and mouse-up-inside commit behavior. The original ORDER dialog also
  continues into navigation on a right double-click, retaining a partial
  ranking table if the board was not finished. Its confirmed Escape/close path
  has the same surprising continuation despite asking whether to quit the game.
- Navigation: after a one-second dwell on an encounter-enabled node, click the
  wandering Simm to tag it and earn the scripted reward. A miss plays the
  original ricochet effect and leaves the Simm active. Exact sprite-border
  pixels are misses, matching SPR.dll before its color-key test. The original target
  crosshair remains active for this mode-3 encounter; host introductions and
  score reactions retain the hand cursor.
- Judging uses the painted Judge-O-Matic, ACCEPT, GONG, PENALTY, and Looks
  ROTATE controls. Click the Looks contestant's head, torso, legs, or back to
  cycle the exact magnifier images. The left 60-pixel exit region returns to
  navigation whenever no synchronous Looks turn or Penalty response is active.
  Those authored movies cannot be truncated with Escape, close, or another
  pavilion click. After the fifth Talent/Brains result, the completed
  portrait strip is an additional close path; Brains plays C81 there first.
  Completed pavilions can be revisited as in the original; a completed Brains
  strip click plays C81 for that visit before returning to navigation.
- Slots: move Up at the slot-machine node. Its reel movie runs in place for the
  original 4.5 seconds, then leaves the exact symbols and result visible on that
  same node until you spin again or depart. Skip Ahead ends the range and shows
  that result immediately; any left click during the active reel follows the
  same original stop/notify path. Left clicks likewise skip other active
  navigation ranges before running their normal arrival handling and repainting
  the resolved destination immediately.
- Tally: Disk II `TALLY.DAT` object 3 action 5 is the sole trigger. It calculates
  automatically, plays the winner/judge-cadet finale media in order, and
  presents the original replay decision at the $400 threshold. Below $400,
  Disk II `END2.WAV` precedes the credit movie; a No answer at or above $400
  goes directly to credits. Replay returns to Cryo without an original-CD check.

MM.EXE contains no accelerator resource. Accordingly, aside from the explicit
native `Alt+Enter` display toggle and `Ctrl+Alt+F1` credit cheat, the game exposes no port-only
arrow/Enter/Space/R/I/G/X/P shortcuts that bypass those painted control paths.
Default dialog commands still follow the original
vtables: Enter exits an active navigation hub without confirmation, Escape asks
to quit most gameplay dialogs, profile and centered movies ignore Cancel, and
ORDER's confirmed Cancel continues into navigation.
Alt-Tab/reactivation also follows the recovered handlers: active ORDER and
Weight artwork is reloaded, and an existing Looks rotation child is shown again.

Run extraction/unit checks, the native build, static dependency inspection,
complete two-disc media/trigger probe, and real Win32 UI-route audit with:

```powershell
./scripts/test.ps1
```
