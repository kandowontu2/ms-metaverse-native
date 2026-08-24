# Reconstructed behavior and data contracts

## Primary offline flow

1. Log in or use the guest profile; only ordinary named profile records persist.
2. Play `MOV/INTRO/MMINTRO.AVI`.
3. For ordinary/GUEST sessions, open `HALL.DAT` at navigation entry 13. Its Ms.
   Metaverse door returns action 13 and then enters the cryo chamber without a
   replay reset or any additional RNG draws. Choose five of ten contestants.
   The `DEMO` name with password `BUYVV` instead keeps IDs 3, 4, 5, 8, and 10
   and jumps directly to navigation state 1.
4. Weight Looks, Brains, and Talent.
5. Order the host-comment phrases.
6. Navigate the crossroads and pavilion graph.
7. Judge contestant performances, use gong/penalty actions, and manage credits.
8. Earn credits from slots and by tagging wandering Simms.
9. Enter the tally machine, compute the winner, then replay, save, or quit.

The original network feature downloaded contemporary contestants from a service
that is no longer part of the media. The first native target is therefore the
complete on-disc/offline game. Network behavior is isolated behind a future
provider interface rather than contacting obsolete endpoints.

## Text scene files

`DAT/NAV/NAVIGATE.DAT` contains an integer count followed by 31 pairs of a scene
stem and numeric variant. Static analysis at `0x0040c97e` confirms this exact
`count` then `%s %i` parser.

Static analysis of the loader at `0x0040a43f` establishes the exact whitespace-
delimited grammar used by all seven supplied scene files (`BRAINS`, `LOOKS`,
`HALL`, `XR1`, `TALNAV`, `TALLY`, and `XR2`):

1. Background AVI path and background-sprite token (`none` on all seven files).
2. Object/hotspot count.
3. Exactly 12 integers per object: index, flags, three integer pairs, three
   object-link indices, and one auxiliary value.
4. One asset-path token when `(flags & 0x0C00) != 0`.
5. One coordinate pair when `(flags & 0x0040) != 0`.
6. A caller-known number of trailing variant-entry object indices (3 for
   Brains and TALNAV, 2 for Looks and TALLY, 4 for Hall, and 8 for XR1 and
   XR2). Variant `N` starts at the `N`th trailing object index.

The field order and unknown flag bits are preserved losslessly in the native
parser. Names remain intentionally neutral until the corresponding update and
hit-test paths establish their behavior.

`BRAINS.DAT` object 17 is the sole observed short record: its auxiliary integer
is absent and its WAV path immediately follows the third link. The original
loader ignores `fscanf`'s return count, leaves that integer indeterminate, and
then successfully reads the path. The native parser applies a targeted,
deterministic compatibility value of zero for this record shape.

After parsing each object, `0x0040a6ef` clears one consumed-state DWORD at
`this + 0x578 + 4*n`. Every scene load therefore rearms the ordinal prefix for
that DAT, including immediate XR1/XR2/pavilion cross-scene reloads. All 133
supplied objects use ordinal numeric IDs, so every `0x0080` encounter lookup is
covered by that reset. The native loader now performs the same bounded prefix
reset while retaining safe validation for malformed counts.

Construction then opens the background AVI at its first paused frame, plays
the selected object's WAV only when `0x0400` is present and the asset token's
first byte is not `0`, preloads `ZPR.WAV`, and starts the middle range only when
the selected object has `0x0100`. The later arrival handler additionally routes
`0x0800` and `0x2000`; those bits are not initial-entry triggers. Across the 30
real NAVIGATE entries, 29 start on `0x0100`, 13 also dispatch a `0x0400` cue,
and the sole static entry is Hall variant 4. The constructor dispatches that
starting cue/range before its final `srand(timeGetTime())`; native code preserves
that order but omits the intervening removable-CD validator as required by the
offline port.

## Navigation-node behavior

The interaction handler at `0x0040af01` chooses a direction from the pointer's
position around the 640x480 midpoint and the current object's low flag bits.
The transition handler at `0x0040aa61` then plays the matching AVI frame range
and replaces the current object with the linked object:

| Direction | Required flag | Range/link pair in file order |
|---|---:|---:|
| Right | `0x01` | 1 |
| Up | `0x04` | 2 |
| Left | `0x02` | 3 |

The native `SceneNavigator` reproduces this graph behavior and validates every
enabled link and variant entry when a scene is loaded.

The shared MCI range helper at `0x0040a966` does not start a scene soundtrack
when the AVI is merely opened, paused, or sought. Embedded audio starts with
the same bounded `MCIWndPlayTo` frame range and is cut when that range stops,
including Skip Ahead, before the destination WAV is dispatched. Of the seven
DAT-selected navigation backgrounds, only `XRDS1F.AVI` and `XRDS2F.AVI` contain
audio (11,025 Hz mono PCM); `BRAINNAV`, `HALL`, `LKSPVL`, `TALLY`, and
`TLNTMSHP` are silent. The native media state now caches crossroads PCM at
scene load, slices it to each requested inclusive frame range, and keeps
single-frame arrival seeks silent. This prevents the full crossroads track
from incorrectly playing as soon as a paused scene is constructed.

That helper first destroys the two DIBs holding a current `0x0040` positional
overlay. The shared native transition entry now clears the retained N1–N4
overlay for every corresponding call, including delayed Crossroads idle clips
and Slots, then reconstructs it only after a destination settles. No supplied
scene range is descending, so the helper's otherwise supported reverse-MCI
branch is unreachable across all seven DAT files.

The corpus contains 306 unique decodable range endpoints plus one intentional
MCI EOF boundary: XR1 idle object 22 requests `1503..1686`, while
`XRDS1F.AVI` contains 1686 zero-based frames (`0..1685`). MCI accepts its
stream-length position and posts ordinary completion. The native range player
now clamps that sole boundary to frame 1685, clears the active range on decoder
EOF defensively, and clamps delayed embedded-audio clock jumps to the requested
last frame. This preserves the final authored image and prevents the ten-second
idle path from leaving navigation permanently input-blocked.

Although every navigation AVI decodes at 320×240, the executable's DAT points,
slot artwork, cursor split, and MMS trajectories all occupy a 640×480 logical
client. The native compositor therefore stretches the movie to that full
logical canvas before drawing any retained overlay. This keeps N1–N4, slot
symbols, and wandering Simms in the same coordinate system used by the original
paint and input handlers.

XR1 has one additional one-shot path. Object 18 has flags `0x0110`. On its
first automatic departure, `fcn.0040aa61` calls `WaveMixActivate(FALSE)`, plays
the stored middle range (frames 1391–1502), and records a pending audio resume.
After that first arrival, later visits replace the animation with a single
seek to the destination object's middle-range start frame. For object 18's
destination, object 0, that held frame is 0. The native runtime stops its
navigation sound during the first long transition, resumes destination cues
on arrival, and applies the same later-pass held-frame rule. Both latches live
in the navigation popup, not the round document: `fcn.0040a0db` clears them at
`0x0040a1fd`/`0x0040a203`, and the cross-scene wrapper `fcn.0040cb7d`
destroys and reconstructs that popup. Revisiting object 18 without leaving XR1
therefore holds frame 0, while leaving XR1 and later loading it into a new popup
re-arms frames 1391–1502.

The post-transition handler beginning at `0x0040b4df` reads the object pointer
*after* `fcn.0040aa61` replaces it. Its `0x0400` branch at `0x0040b657` and
`0x0800` branch at `0x0040b690` therefore play the newly arrived object's WAV,
not the object being left. Action and cross-scene flags are resolved first;
then the destination cue plays once; then `0x0100` may initiate another
automatic transition. The native resolver now preserves that order with an
explicit one-shot arrival state. A self-link produces another arrival and can
replay its cue, while repainting a settled node cannot. Across both bundled
trees there are 35 real node WAV tokens. `XR2.DAT` object 17 additionally uses
the literal asset token `0`; it is an intentional silent placeholder on an
automatic `0x0500` node and is not treated as a filename.

Initial scene construction is narrower than that common arrival handler.
`0x0040a7fa` plays the selected entry object's asset only when bit `0x0400` is
set; it does not treat an initial `0x0800` object as a cue. It then immediately
resolves a selected `0x0100` automatic node. The native loader keeps this
initial-entry state distinct from post-transition `0x0400|0x0800` arrivals.

Navigation also has a distinct departure channel. Scene setup loads
`WAV/NAV/ZPR.WAV` into WaveMix channel 2 at `0x0040a83b`. Immediately before a
valid user-directed move, `fcn.0040aa61` tests the source flags against `0x0900`
and plays that channel when neither `0x0100` nor `0x0800` is set. Ordinary
directional moves and the `0x4000` slot Up/self activation therefore play the
0.766-second effect; automatic nodes and `0x0800` sound-bearing sources do not.
The native port uses a separate PCM output channel so ZPR can overlap the
original arrival/slot channels instead of replacing them. Both bundled copies
decode identically to 8,448 samples at 11,025 Hz mono.

Automatic routing is a three-bit family, not only `0x0100`. At `0x0040b690`,
`0x0800` first plays its stored WAV and then unconditionally invokes the middle
range/link. The `0x0100` branch at `0x0040b6e8` invokes the same transition
without that cue. Finally, `0x2000` at `0x0040b82e` opened dialog resource 130
for the retired Virtual Vegas custom-sprite service and then also invoked the
middle route. Native navigation now uses the exact combined automatic mask
`0x2900`: it preserves all 60 bundled routing nodes and the `0x0800` cue
ordering, while bypassing only the unavailable FTP dialog and its network
work. The two `0x2000` nodes are Brains object 19 and Hall object 12.

The same post-transition handler processes `0x0020` before destination audio.
Exactly five records carry it, all in `XR1.DAT`: objects 1 and 19–22. It
reactivates the WaveMix session and calls the navigation AVI range helper with
`(25,25)`, leaving the background on frame 25. The native resolver performs
that exact seek on every such arrival; the probe verifies both the node map and
that bundled `XRDS1F.AVI` decodes frame 25.

Those flags also enable a previously missing delayed movie branch in
`fcn.0040aa61`. Once a settled `0x0020` object has remained idle for more than
10,000 ms and no modal navigation work is active, the executable draws a
Microsoft-CRT random value scaled to four entries. The pointer table begins at
popup offset `+0x70`, so the four pointers read at `+0xbc`, `+0xc0`, `+0xc4`,
and `+0xc8` prove that the candidates are XR1 objects 19–22. Their middle AVI
ranges are respectively 870–1079, 1080–1303, 1304–1390, and 1503–1686. A used
initial choice walks forward with wraparound; selecting the fourth distinct
entry clears all four used bits immediately. Playback deactivates navigation
sound without changing the current DAT object. Normal completion or Skip Ahead
then follows the ordinary `0x0020` notify path: reactivate sound, seek frame 25,
process that settled arrival, and store a fresh timestamp, so the next idle
clip waits another ten seconds. This test exists only in timer ID 2's 500 ms
callback and uses a `JBE` early return, so exactly 10,000 ms is still too early;
the clip begins on the first 500 ms callback strictly after the threshold. The
native runtime now preserves that cadence and sequence, and its media probe
locks all four object/range mappings.

The scaled draw itself has a nonstandard but gameplay-visible boundary rule.
The executable multiplies the 0–32767 CRT output by the single-precision
constant `1/32767` (`0x38000100`) and the requested range, then truncates toward
zero through `fcn.00417abc`; it does not divide by 32768. Fixed slot and idle
ranges use the equivalent precomputed float constants. This distinction changes
reachable boundary outputs (for example, CRT value 21845 maps to 2 for a
three-way draw), so the shared native RNG now retains it for idle clips, Simm
selection, host reactions, and Slots.

The loader calls `fcn.0040c552` between initializing that timer state and
seeding the CRT generator. Static analysis identifies it solely as the mounted
disc `VOL.DAT` validator/rejection prompt; it does not populate the four idle
pointers. It remains intentionally absent from the native runtime, which seeds
after loading the packaged scene and never checks an original CD.

The pointer handler at `0x0040af01` does not divide the screen into loose
left/center/right thirds. Its exact priority is:

1. If `y < 240` and flag `0x04` is enabled, select Up regardless of X.
2. Otherwise, if `x < 320` and flag `0x02` is enabled, select Left.
3. Otherwise, if `x > 320` and flag `0x01` is enabled, select Right.
4. The exact point `x == 320` has no horizontal direction.

Native hover and click selection now use those boundaries and flag checks. PE
group-cursor IDs 160 (Up), 163 (Right), and 164 (Left), which the original
constructor loads into the fields consumed by this handler, are rebuilt as
standalone `.cur` files by the resource ripper and embedded in the Win32 port.
The scene loader starts timer ID 2 with a 500 ms interval at `0x0040a7de`.
When the pointer helper records neutral state 4, its timer path alternates
cursor 165 then 161, producing the original two-frame sparkling-hand cursor.
An active notified MCI range makes `0x0040af01` force that neutral state
regardless of pointer position. `0x0040af96` updates the sparkling frame before
its active-MCI early return, so the animation continues during ordinary
navigation transitions and the Slots reel. The native timer uses the same
interval, order, and frame persistence. The
adjacent state-5 branch alternates Simm-face resources 169/170, but the audited
offline navigation instructions contain no writer that can assign state 5;
those two resources remain classified as unreachable rather than guessed into
an unrelated hit test.

Right-click navigation is also an original control path, not a native-port
convenience. `fcn.0040a0db` builds one popup in this exact order: `Talent`
(command 200), `Brains` (201), `Looks` (202), `Slots` (204), `Exit` (205), and
`Skip Ahead` (206). The wrapper suppresses the popup only for `NAVIGATE.DAT`
entries 13 through 16, the four Hall starts. `Skip Ahead` is gray while the
scene is settled, enabled when `fcn.0040aa61` starts a bounded transition, and
gray again after it pauses/seeks that transition and runs normal arrival
handling. The Slots shortcut selects entry 29 (`XR1`, variant 8) from a Disk-I
scene or entry 30 (`XR2`, variant 8) from a Disk-II scene. The native dispatcher
uses those same bundled entries directly, with no disc query or remapping.
The right-click handler also sets the navigation object's `+0x628` latch before
tracking the popup. While that latch remains set, the 500 ms timer refreshes
the idle timestamp instead of starting a delayed `0x0020` range. Dismissing the
menu therefore suppresses ambient navigation movies until the next movement or
Skip request enters `fcn.0040aa61` and clears the latch. The native popup now
retains that otherwise invisible state transition as well.

The same command family establishes the remaining action results. DAT actions
2, 3, 4, and 5 open Talent, Brains, Looks, and tally; Crossroads action 6 and
Hall action 16 exit without a second prompt; Hall action 13 begins a fresh
round at Cryo directly from the post-intro entry-13 Hall, without running the
replay reset or consuming its ten cadet-reroll RNG calls. Hall actions 14 and 15 belonged solely to the retired Blackjack
Turbo and Assault Poker launch loops, so the offline target returns to their
Hall variants without asking for original media. Popup `Exit` and the normal
window/Escape close path use the recovered question exactly: `Are you sure you
want to quit the game now?`

Flag `0x0040` supplies a logical-canvas point for a positional navigation
overlay. The handler formats `BMP/NAV/N<object-index>.BMP`, queries its native
extent through the path represented by `fcn.004115b5`, and the original
imperative GDI implementation saves/restores the covered background through
`fcn.00412121`. All four observed nodes are in `XR1.DAT`: N1 at `(172,110)`,
N2 at `(186,34)`, N3 at `(210,80)`, and N4 at `(148,82)`. Their respective
bitmap sizes are 218x64, 236x140, 42x220, and 356x114. The original does not
leave these overlays continuously visible: timer body
`0x0040b184-0x0040b207` first restores the captured backing, toggles its
selector, then shows Nn on the next 500 ms callback and repeats. The native
retained-mode compositor keeps the settled navigation frame as that backing
and alternates backing/Nn in the same order, invalidating only the exact scaled
overlay rectangle. Automatic or cross-scene nodes do not flash a transient
overlay.

## Slots activation, animation, and result overlay

Slots is not a separate stage entered on arrival. `XR1.DAT` and `XR2.DAT` each
mark object 15 with flags `0x4007`; its Up link points back to object 15. An Up
activation runs the middle/self transition: frames 812–869 in XR1 or 821–878 in
XR2. At `0x0040acb5`, `fcn.0040aa61` starts `WAV/NAV/SLOTSPIN.WAV`, begins that
bounded navigation range, and pumps ordinary window messages until
`timeGetTime() + 0x1194`, exactly 4,500 ms. It then sends the MCI stop command.
The time baseline is captured after the range-play call returns, so media seek
and audio setup do not shorten the reel dwell. The native event timer leaves
painting responsive for the same dwell and continues dispatching left clicks,
right-click popup commands, and window close just like the nested original
loop. The navigation MFC message table at `0x00431598` maps
`WM_LBUTTONDOWN` (`0x0201`) directly to `fcn.0040aa61`. That function tests
the active-MCI field at `+0x118` before it inspects pointer direction, so any
left click during the reel synchronously seeks the range to its end and aborts
the notified play. Popup `Skip Ahead` command 206 enters the same branch.
The resulting notification is consumed by the same nested message pump;
`0x0040b4e7` clears the Slots flag and reveals the result immediately, while the
outer call continues to its original 4,500 ms return boundary. Input is live
again and a second popup shows Skip gray because the active-MCI flag is already
clear. Selecting a pavilion/Slots shortcut while the original reel is still
active reconstructs the scene and cancels the pending result. Without Skip, the
deadline stops both the bounded crossroads frames and their embedded PCM and
reveals the result on the same node.

That click-to-interrupt rule is shared by all navigation playback, not unique
to Slots. A left-button down during a manual or automatic bounded transition,
or during one of the delayed `0x0020` idle ranges, skips to the stored terminal
frame and executes the ordinary completion/arrival path. A settled scene still
uses the pointer-derived Up/Left/Right direction normally.

The result painter beginning at `0x0040b4e7` computes a wager of ten percent of
the current balance capped at $50. The cap is a signed integer comparison of
the stored wager bits against the bits for `50.0f`, not a floating-point
minimum. A positive NaN restored from a legacy profile therefore clamps to $50,
while a negative NaN remains unchanged. Its fixed three-, five-, and fifty-way draws
use precomputed `range/32767.0f` constants before x87 truncation. Two-thirds of
the first 0–32767 random draw forces all three reels to the same one of five
symbols; otherwise each reel is drawn independently. A match awards one wager,
except scaled value 37 out of 50 awards twenty wagers. A miss deducts one wager.
The matching sounds are
`WINBIG.WAV`, `WINDING.WAV`, and `SLOTLOSE.WAV`. A zero balance skips both the
movie and random result, plays `SLOTLOSE.WAV`, and immediately draws
`NO CREDIT`.

The fifteen `BMP/SLOT/S<symbol><reel>.BMP` files are drawn at native size. Reel
dimensions are 88×84, 82×84, and 88×86 at `(190,320)`, `(290,320)`, and
`(388,320)`. The 24-point Arial overlay uses transparent backing: the updated
balance is blue at `(40,20)`, the matched-symbol branch is red at `(450,20)`,
the unmatched branch is red at `(500,20)`, and `NO CREDIT` is red at
`(350,20)`. The side follows match state rather than the amount's sign, which
is observable for a negative legacy balance. Amounts use
the original `$%4.2f` formatting. The result persists until another spin or a
departure from the slot node.

Cross-scene flag behavior is now instruction-level traced as well. Flag
`0x0200` sends the object's auxiliary value to the outer navigation dialog as a
`NAVIGATE.DAT` entry. If `0x1000` is also set, the executor sends message
`0x40c` instead of `0x40b`. The `0x40c` handler at `0x0040cc1b` brightens the
current 256-color palette toward white before loading the next scene. It first
stops the active MCI range and destroys the old popup, then executes exactly
199 passes over all 256 palette entries. Each pass adds ten to channels below
246; the executable's blue- and green-saturation branches accidentally set red
to 255, leaving a recoverable off-white palette before its final white fill.
The native 32-bit compositor preserves that recurrence instead of using a
linear alpha fade. The old synchronous loop contains no clock, so its wall-time
speed was hardware-dependent. It does not test round or pavilion completion.
Exactly three objects carry the combined
`0x1200` flags: `BRAINS.DAT` → entry 10, `XR1.DAT` → entry 9, and `XR2.DAT` →
entry 7. The native runtime selects the same fade-to-white path for those three
nodes and switches all other `0x0200` nodes immediately.

The original outer handlers call `fcn.0040cecb` for navigation entries above
20. That routine is a removable-media swap helper, not completed-pavilion
gating. It checks `VOL.DAT`: entries 21/22 request original volume 2, entries
23/24 request volume 1, and declining the prompt returns 25/26/27/28
respectively. Other extended entries pass through unchanged. The native package
keeps both asset roots available simultaneously, preserves the requested entry
unchanged, and performs no volume, drive, or physical-CD check. The unrelated
entry-14/15 prompts for launching Virtual Vegas Blackjack Turbo or Assault
Poker are likewise intentionally excluded from the native game.

## Cryo control and sponsored-portrait presentation

The constructor at `0x0040521d` places the five highlighted Cryo controls at
`(33,71)`, `(98,80)`, `(53,172)`, `(52,256)`, and `(54,341)` over the static
`BUTTONS.BMP` rail. When the information panel is open, its six highlighted
labels use `(470,239)`, `(488,265)`, `(492,288)`, `(490,313)`, `(491,340)`,
and `(456,364)` over `INFOBK.BMP`. The native port loads all eleven original
bitmaps and uses black-key transparency at those exact logical coordinates.
The constructor first loads `CRYO.BMP` and synchronously exposes that base by
itself; only then does it load INFOBK, BUTTONS, MONEY, the five controls, and
the six labels. Each load failure produces the original independent style-30
warning and construction continues. Since MM.EXE derives a target rectangle
from the loaded highlight dimensions, a failed highlight collapses only its
own target; the native input path now does the same instead of leaving an
invisible button active. The packaged probe locks all fifteen source dimensions.

The recovered entry at `0x00403fe7` is a separate 118-byte host dispatcher,
not the adjacent Cryo painter. It stops `sndPlaySound`, constructs the generic
media runner, invokes mode 6 with channel 1/take 1 for C11 or take 2 for C12,
destroys the runner, and reissues `CRYO.WAV` with flags 9. That final restart is
unconditional, so the native C11 and first-sponsorship C12 continuations restore
the loop after success, synchronous `OUCH.WAV`, allocation failure, or media-open
failure. The painter begins at `0x0040405d` and remains documented separately.

The disassembler failed to split the two mouse bodies after the cleanup thunk
at `0x00405d5c`, but their boundaries and roles are explicit in the code.
`0x00405d67` is mouse-down: it records action IDs 7–11 for the five main
controls or 1–6 for the visible detail labels, invalidates the target, and
forces the pressed bitmap to paint. `0x00405fb7` clears that action on mouse-up
and calls its handler only if the release point remains inside the same
rectangle. A second mouse-down outside every target does not clear an earlier
action; that retained action survives until mouse-up. Every visible detail
release also repaints the information-label region after dispatch or cancel.
There is no Cryo hover-highlight state. The down handler also
contains an original hidden branch: Ctrl-clicking the money rectangle
`(465,24)-(565,50)` adds the IEEE-754 constant at `0x00430a94`, exactly
`$100.00`, and refreshes the display. The native input path preserves all of
these details.

Painter `0x0040405d` formats the candidate cost with `$%d` in green at
`(305,129)` and available credits with `$%.2f` in black at `(465,24)`, both in
transparent background mode. Cryo dialog resource 158 specifies 8-point
MS Sans Serif; native painting uses that normal-weight font and exact TextOut
origins instead of the previous bold Arial/vertically-centered approximation.

After sponsorship, `0x00405121` loads `GIRL%d.BMP` and invalidates a 60×60
portrait slot at `x = 217 + 70 * selected_count`, `y = 412`. Because the count
is incremented first, the five visible positions are 287, 357, 427, 497, and
567. Only the newly selected girl's DIB is loaded at that point; unused
portraits are not preloaded. The original releases its previous portrait DIB
because already-painted pixels survive in the non-erasing window DC. The native
compositor retains one data-only bitmap per filled slot solely to reconstruct
those same pixels after a modern window exposure.

## Profiles and credits

The original top-level initialization body at `0x00401818–0x00401a38` scans
drive letters for a CD-ROM, then repeatedly opens `X:\\vol.dat` before entering
profile selection. This body was split away from its owning `0x00401690` class
function in the generated function inventory. It is intentionally absent from
the native flow: packaged/configured `assets` and `assets2` are the only media
roots, so startup performs no drive scan, volume-file read, or insert-disc
prompt.

The login path at `0x0040291f` and save path at `0x00402edd` establish the
offline profile contract:

- `GUEST` accepts no password and is never saved.
- `DEMO` requires `BUYVV` and is likewise never appended or updated.
- New named profiles begin with `$500.00`.
- A returning stored balance below `$400.00` is raised to `$400.00` at login.
- Names and passwords are entered without a UI limit; MM.DAT matching and
  persistence use their first 10 characters.
- `MM.DAT` consists of 28-byte records: `name[11]`, `password[11]`, two padding
  bytes, and one little-endian IEEE-754 credit float at offset 24.

The native profile store reads and writes this exact layout so an original
offline database can be imported without conversion. MM.EXE performs no
database-wide validation: it scans one 28-byte record at a time, ignores
unrelated malformed credentials, and accepts every 32-bit credit payload. The
native scan now does the same for complete records and safely ignores a damaged
final fragment instead of reproducing the executable's uninitialized-stack
tail read. Matching `0x00402d84`/`0x00402d92`, authenticated credit bytes are
copied verbatim and compared as a signed integer against float bits `0x43c80000`;
only a lower bit pattern is replaced with `$400`. This differs from a
floating-point `max` for sign-bit-set NaNs. Merely loading and later saving
`MM.DAT` therefore cannot normalize unrelated records or force a valid user to
guest because another record is malformed. The alternate Virtual
Vegas client mode stored the same three fields as separate 11/11/4-byte records
in `VVT.DAT`; that network-oriented mode is outside the first offline target.
The original also creates an empty `MM.DAT` before opening the profile dialog
when the file does not exist, including a session that ultimately chooses
GUEST. `DEMO/BUYVV` validation occurs before the ordinary record scan, but does
not bypass it: a matching legacy DEMO record restores and floors its balance;
an unmatched DEMO starts at $500. Both cases remain excluded from final save.
At final save, the ordinary offline path seeks to and replaces only the selected
player's 28-byte record (or appends a new one). Both the lookup and write use
`Left(10)`, while the full submitted edit strings remain active in the running
document. A damaged trailing fragment is preserved and the deterministic safe
equivalent of the original's usual short-read path appends after it. The native
writer follows that targeted behavior rather than normalizing or rewriting
unrelated records.
The orchestrator calls this save once after its game/navigation flow returns.
As in `0x00402edd`, an open/update failure returns silently; there is no
post-window custom error dialog.

The modal login class begins at `0x0040815f` despite appearing later in the
executable than the Talent code. Resource 167 is a borderless, 8-point
MS Sans Serif popup with two uppercase edits. Its initialization centers the 640×480
`BMP/PASSWORD/PASSWORD.BMP` canvas, places resource edit IDs 1002 and 1005 at
`(205,208,208×16)` and `(205,295,208×16)`, and treats the native-size
53×52 `BMP/PASSWORD/OK.BMP` at `(535,378)` as pressed art. Mouse-down inside
sets the held flag and synchronously repaints without mouse capture; mouse-up
clears/repaints it and submits only when the release is also inside. The popup
is centered from the full desktop rectangle rather than the taskbar-reduced
work area. Its resource edits impose no ten-character UI limit: the offline
record scan compares `Left(10)` and persistence truncates to the legacy field
width. The native dialog uses these exact ripped assets, coordinates, and
press/release semantics instead of substitute Guest/Login/Cancel controls.

The caller initializes the name to `GUEST` and loops the same artwork dialog
after its original modal warnings. An empty name reports `Invalid name!` and
resets the name to `GUEST`; a guest password reports the original
`GUEST does't require password.` text; non-guest names require a password;
`DEMO` requires `BUYVV`; an existing mismatch reports `Wrong password!`; and an
unknown valid pair creates a new $500 profile. Returning balances are raised to
$400 by the signed raw-bit rule above. The caller does not inspect the modal
return code at `0x00402b17`, so a dialog creation/modal failure falls through
with the object's retained values—initially `GUEST` and blank. This is not a
user cancellation route: vtable entries `0x00408616`/`0x00408617` make normal
Enter, Escape, and window close inert, and only mouse-up inside the painted OK
target submits the fields.

Required bitmap failures retain the original modal presentation. MM.EXE's
`CWnd::MessageBox` wrapper substitutes string resource `0xe000`
(`Ms. Metaverse`) when its callers pass a null caption; each failed required
load uses style `0x30` and `Can't read bitmap file!`. This now covers both
profile images, Cryo/ORDER/Weight art, the three judging pavilions and their
portraits/controls/meters, and ordinary bitmap-backed scene backgrounds. The
dynamic Brains rating loader preserves its exceptional filename-bearing text,
`Can't read bitmap file 'bmp\Brain\%d.bmp'!`. A failed Looks magnifier level
remains an intentional silent wrap probe; the warning appears only if its
level-one retry also fails. That final failure still hides ROTATE, clears the
old magnifier, advances the selected region's counter, and synchronously
repaints only `(225,64)-(436,377)`, leaving the presentation area blank as the
original does.

Pavilion and host-reaction rotation are install-global state, not fields in a
named profile. `NRFPAV.DAT` is exactly 130 bytes: ten CRLF-terminated four-flag
Brains lines followed by ten five-flag Talent lines. `fcn.0040304e` draws both
variants for each sponsored contestant and immediately rewrites the affected
lines, Brains first and Talent second, before advancing to the next contestant.
The five Brains and then five Talent object constructors each call
`srand(time(0))` before any file open, so the final constructor establishes the
shared CRT stream; native replays all ten seed calls in that same position. It
then reopens only the current contestant's lines rather than retaining a startup
snapshot. When a cycle's final zero is consumed, the line resets while retaining
that choice, preventing a boundary repeat. If an active line has no zero,
`0x004169e7` returns `-1` without consuming `rand`; the caller stores variant
zero after incrementing it. Open/seek/read/write failures never gate the round.
The raw reader also does not validate or stop at CR/LF: it stores each fetched
byte minus ASCII `0`, including wrapped EOF byte `0xcf`, and tests only zero
versus nonzero. The native port now uses the exact `nrfPav.dat` name, valid-file
format, raw selection semantics, timing, and guest behavior. A clean native
install synthesizes the original all-zero installed template on its first
successful line update. Its 180-byte `hnrd.dat`
host-reaction table is reopened for each X selection and written immediately
afterward for guest and named rounds alike. The X selector preserves all raw
32-bit words outside the active mutation instead of canonicalizing the table.
A process exit that made no draw does not create or rewrite either table.
`MM.DAT` profile persistence is suppressed for both `GUEST` and `DEMO`.

## Category weights and tally scoring

The category dialog handlers at `0x00410285` and `0x004104e4` establish the
original weight behavior. Looks, Brains, and Talent each have an integer control
from 0 through 10 and the screen initializes all three to 5. The all-zero
combination is rejected with the modal text `Weight the categories please.`. On confirmation, Looks
and Brains become truncated percentages of the control total; Talent receives
the remainder, so the three stored integer weights always sum to 100. For
example, `1/1/1` becomes `33/33/34`. The zero branch leaves any existing
document weights untouched. On success the executable divides each of Looks
and Brains by the total in the x87 stack, multiplies by exact `100.0f`, truncates
toward zero, writes Looks then Brains then the Talent remainder, destroys the
dialog media, and ends with result zero. The native helper preserves that
operation and mutation order; exhaustive tests cover all reachable 0–10 input
triples.
Each increment/decrement handler also plays the resulting `WAV/TT%d.WAV`
value asynchronously, matching `sndPlaySound(..., 1)`. Value zero restores the
blank gauge from `WEIGHT.BMP`; it deliberately retains the previous gauge
wrapper in memory and merely hides it beneath that base repaint. The next
nonzero update destroys the retained wrapper before loading its replacement,
and dialog teardown also owns it if no later update occurs. Values 1–10 draw
`TH1_%d`, `TH2_%d`, and
`TH3_%d` at `(80,85)`, `(300,85)`, and `(525,85)`. The six DOWN/UP pressed
bitmaps use their constructor-defined rectangles. Their down handlers force the
pressed art to paint before changing the gauge; the changed gauge is likewise
painted before `TT%d.WAV` starts. Only the selected gauge's exact recovered
rectangle is invalidated and synchronously updated. Clicking DOWN at zero or
UP at ten still repaints that boundary gauge and replays `TT0.WAV` or
`TT10.WAV`; all 66 control/value combinations and all 33 category/value gauge
plans are regression-tested. `OK.BMP` appears on
mouse-down at `(269,413)`
in its exact 105×57 rectangle. Moving outside while pressed clears it
permanently; if the press flag survives, `0x004103be` commits on mouse-up
without retesting the release coordinate.

The same rectangle discipline applies to the pressed controls themselves.
Mouse-down repaints only the selected arrow or OK rectangle; mouse-up and
drag-out restore only that same rectangle and force it onscreen before any
commit warning or transition. If malformed state ever leaves both press flags
set, the recovered handler restores the arrow before OK, and native preserves
that ordering. The encompassing MFC class entry `0x0040ffcc` is now
instruction-verified as well. Its painter consumes dirty layers in exact base,
OK, selected-arrow, current-gauge order, clears only the flags it consumes, and
leaves noncurrent gauge flags pending. C91 is composited over the retained
parent surface in native code because the original used a separate modal child.

Weight teardown `0x00410f5b` is also instruction-complete. Both successful OK
(modal result zero) and confirmed Cancel (result one) release the generic host
wrapper, base DIB, OK bitmap, six arrows, and three gauges in the recovered
ownership order. That final group includes a non-null old gauge retained by a
zero-value update. Result zero continues to ORDER; result one takes the
orchestrator's exit path.

The initial 500 ms delay and modal C91 introduction deliberately show no gauge
overlays: `0x00410087` starts all three control values at zero. Constructor
`0x004106e6` nevertheless loads hidden `TH1_1`, `TH2_1`, and `TH3_1` backing
wrappers after `WEIGHT.BMP` and `OK.BMP`, before the six arrows. They remain
owned but invisible throughout C91. Timer body `0x00410152` replaces those same
three wrappers with the value-5 gauges after C91 returns, then stores 5/5/5;
the native continuation now preserves that dialog lifetime rather than
reconstructing or invalidating the whole Weight scene. ORDER has
the analogous staged reveal. Its two-second delay and C51 show `ORDER.BMP` plus
`TH10.BMP`; the ten phrase rows and the initial hand position are loaded after
C51. The row hit rectangles are already live during the delay, so an early row
click can draw `HAND.BMP` before the phrase artwork appears.

ORDER's C51 return is an in-place continuation, not a second construction of
the dialog. Constructor `0x0040fa08` warning-loads `ACCEPT.BMP`, `TH10.BMP`,
and `HAND.BMP` in that exact order; the first painter pass then draws the base.
After C51, `0x0040fc60` loads and synchronously paints COMMENT rows 0 through
9 one at a time, followed by `0x0040fd51` repainting the union of the old and
current 85×75 hand rectangles. The native retained-mode renderer keeps all ten
decoded phrase handles for window exposure, but consumes the original
base/ACCEPT/current-COMMENT/HAND/TH/current-rank dirty layers in recovered
order. Pointer actions therefore repaint their clipped original rectangles
instead of reconstructing the whole ORDER scene.

The tally routine at `0x004031d0` scores each of the five selected contestants:

`score = looks_rating * looks_weight + brains_rating * brains_weight + talent_rating * talent_weight`

The highest score wins. Comparison is strictly greater, so tied scores retain
the earliest contestant. A disqualified contestant is excluded and all three
of its pending-pavilion flags are cleared. When all pavilion judging is
complete, the player receives the accumulated reward if it exceeds $500,
otherwise $500. If any judging remains pending, the award is $27 for every
completed contestant/category pair. `0x004032f6–0x00403344` loads, adds, and
stores the balance separately for each such pair rather than adding a multiplied
total once; the repeated single-precision rounding can differ at a sufficiently
large balance. These calculations, including the original tie, disqualification
mutation, signed stored-float comparison, and per-add rounding behavior, are
implemented and instruction-verified in the native `TallyRound` routine.

The hub's `TALLY.DAT` object 3 action 5 dispatches this tally immediately; there
is no separate keyboard tally command. The navigation command handler also
accepts synthetic command ID 203 and maps it to the same action 5, although its
six-item popup deliberately omits that hidden command. If the computed winner is the judge
cadet, Disk II `WAV/ANNOUNCE/WRONG4.WAV` plays, her sponsorship stake is
removed, its result is clamped with the executable's unsigned stored-float-bit
comparison (including preservation of negative zero), and no winner movie is
shown. Otherwise Disk II
`MOV/TALLY/W%d.AVI` plays synchronously in the round sequence. At $400 or more
the original replay question follows; this threshold is likewise the original
signed stored-float-bit comparison against `0x43c80000`. Accepting preserves the balance, the
document's accumulated sponsorship pool, and the previous weights/comment map
until their new dialogs overwrite them, then starts another round at Cryo. The
executable also rerolls the judge-cadet pair once per cleared contestant (five
pairs, with only the last retained), so the port consumes that same RNG
sequence. It intentionally bypasses the original disc-swap prompt on that
branch. Declining goes directly to Disk II
`MOV/CREDIT/CREDIT.AVI`. Below $400, Disk II `WAV/END2.WAV` plays synchronously
before that same credit movie and exit.

## Comment ordering and host responses

The ORDER handler at `0x0040f653` starts at rank 10 and counts down to rank 1.
Each of the ten phrases may be selected only once. Assigning visual row `R`
stores `9 - R` in the game state's table for the assigned rating. This reversal
is significant because the artwork's top-to-bottom order and the host-comment
file number run in opposite directions. Clicking an already assigned row leaves
the hand on its prior valid row and, like pressing ACCEPT while an assigned row
is selected, shows `This command had been selected,\nplease choose another.` in
the original modal message box.

`ORDER.BMP` is augmented with `TH10.BMP` at `(88,30)`, `HAND.BMP` at
`(215,30)`, and ten `COMMENT/%d.BMP` phrase rows at `(308,40 + 40*row)`.
Although each phrase bitmap is 314×32, `0x0040f35c` constructs its clickable
rectangle as 313×30: `(308,40 + 40*row)-(621,70 + 40*row)`, with the Win32
right and bottom edges excluded.
`ACCEPT.BMP` is a 196×91 pressed-state overlay at `(21,381)`, not permanent
art. Moving outside its rectangle while pressed clears the press permanently;
if the press flag survives until mouse-up, `0x0040f5ca` assigns without another
release-coordinate test. Assigned rank
numbers use the original red, bold 20-pixel Arial text at
`(317,45 + 40*row)`. `TT10.WAV` through `TT1.WAV` are asynchronous. After the
rank-one assignment, ORDER closes immediately without stopping `TT1.WAV`, so
that final phrase may continue over the first navigation frame.

The assignment side effects occur in exact order: mutate the selected row and
decrement the rank, synchronously paint the red number in its 17×30 dirty
rectangle, start `TTn.WAV`, replace/repaint `TH(n-1).BMP`, and only then write
`9-row` to the document's rank-indexed mapping table. Teardown releases the
base/static/final-comment ownership chain before continuing to navigation for
successful rank one, right-double-click result zero, or confirmed Cancel
result one; none of those routes issues `sndPlaySound(NULL)`.

The three Judge-O-Matic handlers at `0x0040715a`, `0x0040937e`, and
`0x004135dd` use that table as follows:

- Rating 0 plays the generic `WAV/TT0.WAV` response.
- Ratings 1 through 10 select the corresponding ORDER-table entry.
- Brains, Looks, and Talent format `B<comment><slot>.WAV`,
  `L<comment><slot>.WAV`, or `T<comment><slot>.WAV` under `WAV/COMMENT`.
- `slot` is the contestant's one-based judging position from 1 through 5, not
  her global contestant ID.
- The response starts asynchronously when the meter band is selected. Brains
  and Talent use meter rectangle `(70,67)-(130,288)`; Looks uses
  `(62,13)-(118,278)`.
- The meters are not gated by an active contestant. On the empty Talent and
  Brains chooser, every band plays the generic `TT0.WAV` through `TT10.WAV`
  and paints its numeric gauge without storing a contestant score. If a wide
  pavilion performance child is open, selecting a band first sends it the
  original MCI stop command; the aborted-play notify destroys that child and
  restores the pavilion loop before the judge response starts. ACCEPT cannot
  be pressed while the performance child is still open. An active
  wide-pavilion zero plays no response.
- Looks uses a separate highlighted-slot field: after its entry event it
  automatically highlights and displays the first unfinished contestant, and
  after ACCEPT or a correct Penalty Box result it automatically advances to
  the next unfinished one. Its meter remains live even when a completed
  pavilion is reopened; a positive value plays the highlighted slot's
  `L<comment><slot>.WAV`, zero is silent, and the value is written to that
  slot immediately rather than waiting for ACCEPT. Automatic activation after
  ACCEPT and a manual portrait switch both reset that numeric value to zero and
  hide the retained rating DIB, so a contestant never inherits the prior
  contestant's visible gauge. Correct Penalty and a zero meter click use the
  same hide-without-release painter behavior.
- ACCEPT never synthesizes or replays a response on its own. If no meter band
  was selected after activating the contestant, the stored zero rating is
  accepted silently before the animated host reaction; the recovered
  `[+0xac] > 0` guard in Brains/Talent is the one-based active-contestant slot,
  not a minimum-rating test.
- ACCEPT, GONG, PENALTY, and Looks ROTATE display their ripped bitmap on
  mouse-down. Looks stores action codes 1/2/9 and acts only when mouse-up stays
  inside that same Penalty/Accept/Rotate rectangle. Talent and Brains instead
  store one shared press flag: their `0x0040741e`/`0x00409658` release handlers
  clear all three controls and select ACCEPT, GONG, or PENALTY solely from the
  mouse-up rectangle, even if a different wide control armed the flag. They
  synchronously expose all three restored controls before dispatching the
  selected action. Looks calls UpdateWindow before ROTATE's release hit test,
  but its base and ROTATE dirty flags have already been consumed; that paint
  therefore leaves the pressed pixels intact. Code 9 is then cleared before the
  hit test, and both a cancelled release and a completed bounded turn jump out
  without ACCEPT's base-restoration epilogue. Because the Talent/Brains painter
  tests that one flag
  for all three controls, an unrelated full-window repaint while it survives
  reveals all three pressed bitmaps; the usual clipped mouse-down repaint still
  exposes only the control under the pointer.
  The Looks down path does not gate those three controls on a nonzero current
  contestant ID and does not capture the mouse. Its release asymmetry is
  literal: ACCEPT eventually clears code 2, ROTATE clears code 9 before its
  release hit test while leaving its pressed surface behind, and PENALTY leaves
  both code 1 and its pressed surface latched until mouse-move exits its rectangle
  or a later control press replaces it. A correct PENALTY branch restores that
  control during its base-dirty forced repaint; WRONG5 does not. Native keeps
  the logical latch separate from the retained pressed surface.
  Talent/Brains accept replaces the current bottom portrait with
  `DIMMED/%d.BMP` before the animated X response starts. Looks instead runs X
  first, then stores the rating and dims the portrait after X returns. A gong
  similarly paints `GONG/%d.BMP` before C61 or contestant advancement. The gong portrait is local to that
  pavilion-dialog visit: leaving and re-entering reconstructs the completed,
  non-disqualified portrait as `DIMMED/%d.BMP`, because the constructor stores
  no persistent gong-art state. `PENALTY/%d.BMP` remains visible across
  pavilions because disqualification is round-document state.
- Category entry initially leaves no contestant active. The five 60×60 portrait
  rectangles begin at `(287,412)` with a 70-pixel stride. This empty-chooser
  lifecycle applies to Talent and Brains: clicking any still-pending portrait
  selects that contestant and only then charges the performance surcharge and
  starts her one selected presentation movie. Clicking another portrait while
  one is active displays the modal `Please judge the previous contestant.`;
  clicking a completed portrait while idle is silent. After a result the screen
  returns to this chooser, so their judging order is player-selected, not
  forced 1→5. That return is in-place: ACCEPT, Gong, and correct Penalty clear
  the active slot and restart the pavilion loop without reconstructing the
  background, actions, five portraits, or current rating wrapper. Their prior
  numeric value and gauge therefore remain visible on the chooser. Selecting
  another portrait resets the number and loads the visible `0.BMP` gauge only
  after its surcharge succeeds;
  NOCASH2 preserves the old value/art, clears the attempted active
  slot, and leaves the synchronous NOCASH2 result silent afterward rather than
  restarting ambience. Looks is different: it automatically highlights the first
  unfinished slot and allows direct switching to any other unfinished portrait
  without that modal; switching clears the target slot's stored Looks score but
  retains the dialog-local numeric rating, matching the original quirk. These
  switches and automatic result advances seek the one dialog-lifetime ROTATE
  child in place rather than reopening its decoder or restarting ambience.
  After the fifth result, the completed chooser remains visible. In Talent, a
  further click anywhere in the five-portrait strip closes the pavilion. In
  Brains, that click first runs `C81` and closes only after its clip and WAV.
  The hub may open an already completed pavilion again: the original outer
  orchestrator always constructs results 2/3/4 without a completion preflight.
  Talent again closes from a strip click; Brains dispatches `C81` again for
  that dialog and then closes; Looks still requires its direct exit target.
  Every pavilion also has the original direct exit target `x < 60, y < 382`,
  which can leave at any time; Looks uses that target rather than an all-results
  portrait-strip branch.
- Each newly constructed pavilion copies its category's five pending values
  from the round document and masks any already-disqualified slot. ACCEPT,
  Gong, and a correct Penalty Box result clear only that dialog-local copy;
  score bytes, cash, and the disqualification flag retain their recovered
  immediate document writes. The pavilion close routine copies all five local
  pending values back to the document before stopping ambience or releasing
  bitmap/movie resources. Native now keeps the same staging boundary, including
  early direct exits and the Brains C81-delayed close.
- Re-entering a completed Looks pavilion still constructs ROTATE.AVI at frame
  zero, clears the current contestant ID, and hides the child after the empty
  pending scan. The child's lifetime is retained: a later active
  `WM_ACTIVATEAPP` shows that frame-zero surface, matching the original quirk.
- All three pavilion painters show the live balance in green at `(465,25)`
  using the shared dialog's 8-point MS Sans Serif. Talent and Brains format it
  as `$%.2f   ` to erase a longer prior value in their partial repaint path;
  Looks formats `$%.2f` after restoring the backing rectangle. Each painter
  also frames all five 60×60 portraits at `(287 + slot*70, 412)` in white.
  Talent/Brains add the active pending portrait's red frame only while their
  `+0x64` dirty latch is armed, then clear that latch at the end of the same
  painter invocation even when no valid active slot was drawn. A later painter
  pass that reaches the portrait strip therefore restores white; native keeps
  this one-shot state instead of reconstructing a persistent red frame.
- Mouse move scans the five stored portrait rectangles. Entering a different
  portrait stores its one-based slot and repaints the shared `(287,388)-(640,410)`
  name strip synchronously; leaving all five clears it the same way. The painter draws the hovered
  contestant's green name at that portrait's x-coordinate and y `388`.
- The normal game-screen pointer is MM.EXE cursor resource 166. All three
  judging mouse-move handlers switch to the right-pointing resource 174 over
  the recovered `x < 60 && y < 382` direct pavilion-exit region; Looks uses
  resource 181 over the whole active `(225,64)-(436,376)` rotate child. The
  two one-pixel front-view gaps suppress magnifier clicks, but do not change
  that child-window cursor. Their `WM_SETCURSOR` routes do not apply a global
  default: Talent at `0x00407db2`, Brains at `0x00409ff0`, and the Looks
  pavilion at `0x0041447d` return handled while preserving the cursor chosen by
  mouse movement; the Looks child at `0x004126cf` explicitly restores 181.
  Looks parent mouse-move sets 166 everywhere else except the lower-right
  `x > 580 && y >= 382` corner, where it performs no cursor write and preserves
  the previously selected cursor.
  Navigation's `0x0040af8e` likewise preserves its directional or alternating
  neutral frame. The native host now consumes `WM_SETCURSOR` with the same
  ownership instead of allowing `DefWindowProc` to overwrite those pointers.
  All of these cursor resources are embedded into the native EXE.

Disk I contains all 100 Brains/Looks response files. Disk II supplies all 50
Talent responses plus 50 contestant/variant performance movies, ten contestant
intros, and ten profile clips under `MOV/TALENT`.

## Host interstitial and score-reaction triggers

The generic media object built at `0x0040d1fd`, loaded by the instruction body
beginning at `0x0040da9f`, and run by `0x0040e5c8` is shared by host clips,
score reactions, and navigation Simms. Host AVIs are transparent sprite layers,
not full-screen replacements. Their upper-left positions come from the ten
little-endian coordinate pairs in `DAT/HOST/HPNT.DAT`:

`(0,0), (160,240), (80,256), (80,256), (304,256), (96,200),`
`(160,240), (160,240), (160,240), (144,120)`.

Generic cursor selection is mode-specific. The dedicated mode-6 `C` loader at
`0x0040dc3a` installs hand resource 166. The shared MMS loader installs hand
166 for mode 4 but crosshair/target resource 162 for ordinary mode 3 at
`0x0040ddd4`; `0x0040e6c7` reapplies that stored cursor on every
`WM_SETCURSOR`. Thus wandering Simms retain the original target while `C` and
`X` host clips retain the hand. Resource 162 is also embedded into the native
EXE, so this correction adds no runtime file or library dependency.

Mode-6 host input has a separate completion rule in the split mouse body at
`0x0040d6ca`: every left click ends the current C clip immediately. The imported
`SPR.dll` color-key-aware hit test controls only whether completion also plays
`WAV/HOST/OUCH.WAV`; a transparent part of the sprite or any background point
still dismisses the clip without OUCH. Mode-4 X reaction records carry `0x0080`
and ignore clicks, while mode-3 Simms retain their own hit/reaction path.

The exact modal C-clip triggers recovered from the callers are:

| Clip | Trigger | Entry delay |
|---|---|---:|
| `C11` | Enter Cryo | 500 ms |
| `C12` | First successful sponsorship of the round | immediate state event |
| `C21` | Enter Talent judging | 500 ms |
| `C31` | Enter Brains judging | 500 ms |
| `C41` | Enter Looks judging | 500 ms |
| `C51` | Enter ORDER/comments | 2,000 ms |
| `C61` | First Gong separately in Talent and Brains | immediate state event |
| `C71` | First correct Penalty Box result separately in Talent and Brains | immediate state event |
| `C81` | Click the portrait strip after all five Brains results are complete (the direct left-edge exit bypasses it) | immediate state event |
| `C91` | Enter category weights | 500 ms |

C21/C31/C41 are document-lifetime one-shots and do not replay after leaving and
re-entering an incomplete pavilion or after choosing to play another round.
The replay reset loop does not restore their three fields, nor does it clear
the accumulated sponsorship/reward field at document offset `+0x404`. C61/C71 are
dialog-local flags: each new Talent or Brains visit rearms its first eligible
Gong and correct-Penalty event. Newly constructed Cryo, Weight, and ORDER
dialogs also replay C11/C91/C51 in the next round.

The one-shot flag suppresses only the host clip, not the dialog's timer. A
Talent/Brains re-entry whose C21/C31 was already consumed still constructs its
bitmap wrappers, waits the original 500 ms timer interval, and only then starts
`TALENT.WAV` or `BRAINS.WAV`; it does not start the loop immediately or request
it twice. A consumed-C41 Looks re-entry waits that same interval before it
constructs ROTATE, auto-selects/seeks the first pending contestant (or retains
frame zero hidden when none remain), and starts `LOOKS.WAV`.

Those entry flags are consumed before the generic runner is called: Talent at
`0x004066ea`, Brains at `0x004088d1`, and Looks at `0x00412968`. The common
motion-backed loader sends `EndDialog(..., 0)` for source-open, media-buffer,
AVI-child, and sprite-construction failures via `0x0040dc6c`; the dedicated
mode-6 C loader uses that same result-zero exit when its movie child cannot be
created. Each caller still executes its ordinary post-modal continuation.
Consequently a failed C21/C31/C41 construction does not re-arm the document
entry and cannot replay on the next pavilion visit. The native delayed-entry
path now consumes its matching state at this same attempt boundary and resumes
inside the existing pavilion without reloading its background, controls,
gauge, or portraits.

Cryo starts looping `CRYO.WAV` before its 500 ms `C11` timer is armed, so the
opening half-second is not silent. Its static Cryo DIBs already belong to the
dialog during that delay. The C11 helper temporarily owns the modal media; when
it returns (including a media-open failure), the same dialog restarts
`CRYO.WAV`, constructs and shows `ROTATE.AVI` at `(258,142)`, then constructs
the hidden `INFO.AVI` child at `(464,84)`. It does not reload the chamber or
predecode contestant media before C11. Talent, Brains, and Looks instead start their
pavilion loops only after the corresponding entry host object returns. ORDER
and Weight have no screen ambience.

During a generic mode-6 `C` clip, the original tests clicks against the opaque
host sprite rather than its rectangular bounds. Every click ends the clip;
only an opaque hit first records the OUCH flag. The shared completion routine
then stops the companion audio and plays `WAV/HOST/OUCH.WAV` synchronously
before the caller resumes. The native renderer follows that immediate,
color-key-aware boundary. Mode-4 `X` reactions do not arm the response.

The C-clip AVI does not supply the display clock. `fcn.0040eb52` asks WinMM for
the companion WAV position in samples and selects
`floor(samples * 10 / 11025) + 1`, matching the executable's recovered
`10/11025` double constant and its one-frame look-ahead. The WAV is fixed at
11,025 Hz mono 8-bit in the original loader and its end is the modal completion
boundary, even when the AVI has a longer silent visual tail. The native runner
now uses the live wave cursor as the same master clock rather than accumulating
nominal AVI frame durations.

Mode-4 X completion is graph-master rather than audio-master. When an MMS
result record completes, `0x0040d2ab` immediately invokes `0x0040e4bd`, which
stops the companion channel and returns the modal result. Four X WAVs outlast
their graphs (`X14`, `X18`, `X211`, and `X212`); X18 is 420 ms longer, so
waiting for those audio tails would produce a visible control-return delay.

Selecting a rating starts its ORDER-selected comment WAV asynchronously.
Accepting the rating then invokes generic mode 4 for an animated `X` host
response and advances only after that response completes. Every pavilion's
ACCEPT caller forwards the current rating, not the contestant ID.
`fcn.0040e8ae` maps values 3 and below (including zero) to X group 3, 4–7 to
group 1, and 8 and above to group 2. The active group sizes are
15, 12, and 11, producing `X11..X115`, `X21..X212`, and `X31..X311`.
`X213` is bundled but outside the executable's group-2 bound; `X312.MMS` is the
empty unused placeholder. Each active response uses an MMS motion graph, AVI
sprite frames, and a companion WAV. The original stores 45 interleaved int32
used flags in the 180-byte `hnrd.dat`; selection does not repeat within a group
until its active set is exhausted. The native port reads and writes this exact
layout beside the other local profile state.

That read is intentionally not a startup validity gate. At `0x0040e9c5` the
original zeroes all 180 bytes, attempts to open/read HNRD, and never tests the
open result or returned byte count. A missing or short table therefore still
selects an X variant from the zero-filled remainder. A successful raw read is
equally permissive: each little-endian word is tested only for equality with
zero. The selector counts zero words in the active group, clears that group if
none remain, calculates `floor(rand * zero_count * 0x38000100)`, and scans
forward through the interleaved table to the selected zero before storing `1`.
Because `0x38000100` is slightly below the mathematical `1/32767`, even CRT
output 32767 selects the last zero rather than reaching the loop's structural
one-past probe. The immediate create/truncate rewrite stores all 45 raw words,
so malformed and inactive words that were not cleared or selected survive
verbatim. The native selection path now reproduces those rules while retaining
a strict canonical parser for audits and diagnostics. The original's empty
installed-root shortcut skips HNRD and draws directly; the packaged native root
is always configured, so that branch is unreachable without a CD or registry
probe.

The motion-backed generic-loader branch has a global random side effect that is
easy to miss when porting these triggers independently. At `0x0040ddc0` it
calls `srand(timeGetTime())`. For an X reaction, `fcn.0040e8ae` runs *after*
that reseed, so the non-repeat variant draw uses the fresh millisecond stream.
For a navigation Simm, the outer handler first calls `fcn.0040c3ed` to choose
D0–D10 with the existing stream, then the loader reseeds before opening the
selected D/T media. Mode-6 C clips take the earlier dedicated branch at
`0x0040db2f` and return without reaching the reseed. The native runtime now
reproduces those three distinct boundaries; each X or Simm object consequently
replaces the shared stream used by later idle clips, host reactions, and Slots,
while C clips leave it untouched.

MMS frame bounds and coordinate bounds are independent. At an AVI terminal the
original either loops the record frame or, in mode 4, holds the terminal-minus-
one frame while motion continues. `X24` runs one coordinate tick past its AVI
end; MCI clamps that seek, so the native player likewise holds the last decoded
bitmap instead of aborting the reaction.

The mode-6 C runner uses the companion WAV as its ordinary completion boundary;
the supplied C AVIs are never shorter than that audio, so their silent visual
tails do not extend the modal. Dispatch first clears the ordinary scene-sound channel, and the owner
restores its pavilion loop after the modal response. During an X mode-4 reaction
the original message pump also permits an underlying Talent/Brains performance
to finish and deliver its MCI notification. The native host voice therefore uses
an independent WinMM PCM channel while that performance continues to advance.

Mode-4 X companion audio is best-effort. The loader calls the void WaveMix
channel-replacement helper and never checks the returned wave handle, then
continues into MMS/AVI construction. A missing or unplayable X WAV therefore
leaves the graph visible and silent rather than cancelling the rating response;
the native loader now retains that failure boundary.

Generic construction failure is a modal result, not an abandoned caller. All C
and X dispatchers destroy the allocated generic wrapper and continue their
normal tail after result zero; the Simm caller additionally tests `result > 10`,
so the same failure awards nothing but still restores ZPR and its return clocks.
The native starters mirror those continuations instead of leaving the owner in
a pending-host or pending-Simm state.

## Disk II Talent and finale media

The Talent handler formats `MOV/TALENT/TI%d.AVI` at `0x00407363` only when the
active slot is the judge cadet and the round's separate clue-pavilion selector
is Talent. Otherwise it formats one `T%d%d.AVI` contestant/rotation performance.
Brains makes the equivalent exclusive choice between `BI%d.AVI` and
`B%d%d.AVI`. These are alternatives, not introduction → performance chains.
Correct Penalty Box results use `TP%d.AVI` and `BP%d.AVI` synchronously before
judging advances. Looks uses the `BMP/LOOK/MAG` family for normal judging and
reserves `LP%d.AVI` for a correct Penalty response. That response temporarily
hides ROTATE and occupies `(225,64)` at 200×320; the normal ROTATE child is
212×312 at the same origin and is shown again afterward. All three response
helpers issue MCI `Play From 0 Wait` inside the pavilion input handler. The
owner therefore cannot process Escape, close, or another pavilion action until
the response returns; the native asynchronous decoder now enforces that same
non-reentrant boundary.

The ordinary Talent/Brains performance child has a separate failure lifetime.
Those callers allocate and store the wrapper object before invoking shared
creator `0x0040802f`, ignore its return, and later test only whether the stored
object pointer is non-null. An AVI open failure therefore leaves an empty child
that still blocks ACCEPT. A meter click tears it down before rating; Gong or
Penalty reports that it interrupted an existing performance and consequently
suppresses that action's first C61/C71 host insert. The native port now tracks
this wrapper lifetime independently from decoder-open state and paints the
failed child black until one of those exact teardown routes runs.

## Looks rotation and magnifier

The normal Looks presentation is not `MAG/111.BMP` and is not an `LP` movie.
`MOV/LOOK/ROTATE.AVI` is a 401-frame, 200×320 reel containing forty frames per
contestant. On activation the original seeks to `(contestant_id - 1) * 40` and
stretches the frame into the child rectangle `(225,64,212,312)`. ROTATE plays
the inclusive block `base..base+19` from front to back, then `base+20..base+39`
from back to front. Each bounded turn is issued with MCI `Wait`, so the same
owner-input block applies until its twentieth frame returns. The native port
uses those same frame ranges and completion boundary. The helper never checks
the MCI command result: even a missing/failed ROTATE child toggles the stable
front/back state after the no-op command, which the native state machine now
preserves.

On initial entry the presentation rectangle stays as the bare `LOOKS.BMP`
background while the delayed `C41` host clip runs. Only after C41 returns does
the dialog construct `ROTATE.AVI` and automatically select the first pending
contestant. If no pending contestant remains, it hides ROTATE and leaves the
area blank. `0x00412d8b` does preload `MAG/111.BMP` (and hidden gauge
`LOOK/1.BMP`) before the timer, but neither is substituted into that rectangle.
Re-entry after C41 has already been consumed follows the same 500 ms timer and
post-host continuation; it does not construct or seek ROTATE immediately.
Blank/zero transitions hide these retained wrappers rather than destroying
them.

The rotate child owns the body hit testing. In its local coordinates the front
view sends magnifier region 1 for `(0,0)-(211,103)`, region 2 for
`(0,104)-(211,208)`, and region 3 for `(0,209)-(211,312)`, with the two original
one-pixel gaps left inactive. The back view maps the whole `(0,0)-(211,312)`
area to region 4. Selecting a region hides the AVI child and paints the selected
211×313 bitmap at `(225,64)`.

Because that hides the child window, the next click in the presentation area
lands on the parent. The parent handler at `0x0041345a` re-shows the same
stopped ROTATE child without reopening or reseeking it, covering the retained
magnifier bitmap. Application activation performs the same retained-child
show operation even after a completed-pavilion entry left it hidden.

Each region owns a counter initialized to 1. Normal filenames concatenate the
contestant ID, region, and counter as `BMP/LOOK/MAG/%d%d%d.BMP`. After a load
the counter increments; attempting a missing level resets that region to 1 and
loads its level-one file. The counters persist while the Looks pavilion advances
between contestants, matching the original window object, but a hub exit
destroys that dialog and a later Looks visit resets all four counters to 1.

When the current contestant is the judge cadet, one region substitutes an `IM`
file. Contestants 1–10 use special regions `4,2,3,2,1,2,4,1,1,2`, producing
`IM14`, `IM22`, `IM33`, `IM42`, `IM51`, `IM62`, `IM74`, `IM81`, `IM91`, and
`IM102`. All ten substitutions and all 132 magnifier bitmaps are validated by
the native asset probe.

The finale path formats `MOV/TALLY/W%d.AVI` at `0x004023d2` using the winning
contestant ID. The low-credit/replay branch references
`MOV/CREDIT/CREDIT.AVI` at `0x00402483`. The sole in-game trigger is object 3's
action 5 in Disk II `TALLY.DAT`; the earlier port-only Enter and `C` shortcuts
have been removed. All ten winner movies and the credit movie are decoded with
their embedded 11,025 Hz mono audio. They reuse the intro's centered
resource-158 modal: its blank canvas is visible for timer ID 1's 100 ms delay
before the AVI child applies zoom 200% and starts. Full-stream probes decode
all 4,970 frames in these twelve 320×240 files. Every PCM stream ends first,
leaving a 0.03161–0.1 second visual tail that the native AVI cadence preserves
before completion. The separate Disk II `END2.WAV` and
`ANNOUNCE/WRONG4.WAV` streams are validated at the same rate and channel count.

## Sponsorship, performance fees, gong, and Penalty Box

Attempting to sponsor a Cryo contestant already present in the five-slot list
shows the original modal warning text, `This girl has be selected,`, with its
second line ` please select another one.` (including the leading space).
Affordability rejection is a different branch and plays
`WAV/ANNOUNCE/NOCASH1.WAV` synchronously.

The Cryo chamber is a fixed 640×480 composition recovered from the original
paint and mouse handlers. `CRYO.BMP` is the base; `BUTTONS.BMP` is drawn at
`(0,0)`, `MONEY.BMP` at `(358,3)`, `ROTATE.AVI` at `(258,142)`, and the optional
`INFOBK.BMP`/`INFO.AVI` panel at `(432,61)`/`(464,84)`. The browse, rotate,
information, and sponsorship controls retain their executable-defined hit
rectangles rather than using broad screen regions. Each contestant owns a
40-frame section of `ROTATE.AVI`; `fcn.00404962` advances through it in
ten-frame quarter-turns, one ROTATE click at a time. After the fourth quarter,
IDs 1–6 wrap to their first ten frames. The executable's unusual ID 7–10
branch instead seeks to frame `first+10` and issues PLAYTO `first+9`; the native
state machine preserves that non-forward wrap before continuing at the second
quarter. The six information labels use the exact per-contestant INFO.AVI frame
pairs stored at `0x004364a8`.

The left/right browse handlers at `0x00404714`/`0x0040483b` stop the ROTATE
child and, when the INFO panel is open, also stop its current embedded audio
before seeking the newly selected contestant's initial frames. The visible
INFO stop/NAME-BIO seek/show sequence occurs before ROTATE is stopped and
sought. `0x004049ec`
toggles INFO by stop/hide on close and seek/show of segment zero on open; it
does not automatically play the full biography segment.

The MM.EXE PE resource tree has no accelerator table. Cryo, Weight, ORDER,
judging, and DAT navigation are driven by their recovered painted mouse targets;
the native port therefore does not expose keyboard aliases for those controls.
Default dialog processing is separate from accelerators and follows each
class's recovered OnOK/OnCancel vtable slots, described below.

The shared AVI child setup at `0x0040802f`, like the recovered scene
constructors, sets the visible caption to exactly `Ms. Metaverse`. Native
scene/node/player/credit debug text is therefore retained only as internal
state and is no longer appended to the caption.

The centered one-shot class's vtable identifies `0x004014d3` as OnOK and the
bare return at `0x0040152d` as OnCancel. Escape/window-close is therefore a
no-op for MMINTRO, winner, and credits. Enter/default OK while MCI is active
stops playback and calls `EndDialog(0)` through `0x0040152e`; while inactive,
the same handler sends Play Notify and marks playback active instead. The
completion helper closes the MCI device, destroys its wrapper, then returns the
supplied result. The intro caller
at `0x00401bd3` exits only on a nonzero result, so Enter while MMINTRO is
playing skips the remaining movie and follows the same ordinary Cryo or
DEMO-navigation continuation as natural notify. During each instance's initial
100 ms timer delay no movie child exists; OnOK merely issues the pending Play
Notify command and leaves the modal open. Winner ignores the result, while the
credit caller exits after either result.

The movie child has its own message map at `0x00430100`. Once the timer has
created that child window, any `WM_LBUTTONDOWN` enters `0x004011d2`, stops active MCI,
clears the shared active flag, and sends private message `0x0405` to its owner.
The owner's `0x004014c5` handler completes the same result-zero path, so a left
click skips MMINTRO, a winner, or credits just like active Enter/OK. A click
during the blank 100 ms owner-only interval remains inert because the owner map
at `0x00430150` has no left-button entry. Timer expiry alone does not change
that routing: if shared constructor `0x0040802f` gets a null `MCIWndCreateA`
result, the wrapper object exists but no child HWND owns the click, so the blank
owner remains mouse-inert.

Initialization at `0x0040133c` sets caption `Ms. Metaverse`, centers the exact
640×480 outer rectangle, invalidates/updates its black surface, and arms timer
1 for 100 ms. The timer handler kills that timer, allocates the shared wrapper,
creates with style `0x0A`, sends MCIWnd zoom 200, sends Play Notify, and marks
the child active in that order. `0x004014b2` completes only for
`MCI_NOTIFY_SUCCESSFUL` (code 1); other notify results remain modal. Native
winner completion also clears the decoded bitmap and forces the now-black
owner to paint before the replay prompt or blocking `END2.WAV`, matching the
original child-destroy-before-`EndDialog` continuation order.

ORDER has a separate hidden exit. Its map at `0x00431f78` binds
`WM_RBUTTONDBLCLK` to `0x0040ff2d`, which calls the ordinary teardown with modal
result zero without showing the quit confirmation. The orchestrator does not
inspect ORDER's result and immediately enters navigation state 1. Any rankings
already assigned remain in the document table; unassigned words retain their
prior values. The modal C51 child disables its ORDER owner, so this route cannot
fire while that host clip owns input.

The vtable at `0x00432088` also makes ORDER OnOK a bare return at
`0x0040f516`; Enter cannot accept or complete the board. Its OnCancel at
`0x0040f517` shows the usual quit confirmation and, on Yes, tears down with
modal result one. The outer call at `0x00402118` never tests that result, so it
still proceeds to navigation state 1 with the partial comment table. Weight's
adjacent `0x004101ec` OnOK is also a no-op, but its caller does test a nonzero
Cancel result and exits the game.

Two other default-command routes differ from the former native behavior. The
painted profile vtable at `0x00431198` points OnOK and OnCancel to the adjacent
bare returns `0x00408616`/`0x00408617`; Enter, Escape, and close do not submit or
dismiss resource 167, and only its mouse-up-inside handler calls the base OnOK.
The navigation hub vtable at `0x00431ab0` instead maps OnOK to `0x0040cb12`,
which stops and destroys the active scene and ends result-zero. Both hub callers
fall through to the common profile-save/application-exit tail for that result,
so Enter exits navigation immediately without the OnCancel confirmation.

The message-map audit also resolves application activation. Cryo
`0x004063ce`, Talent `0x00407dd2`, and Brains `0x0040a010` rearm their painter
flags on both activation edges. On activation only, ORDER `0x0040ff37` reloads
all ten COMMENT bitmaps, Weight `0x00410fd9` recreates all three gauges from the
current integer values, and Looks `0x0041449d` shows both its pavilion and any
existing ROTATE child. These calls can reveal ORDER phrases or Weight gauges
during their normally blank entry delay after an Alt-Tab round trip. Native now
performs the same refreshes. The distinct generic-media handler at `0x0040ef28`
still closes its modal on either activation edge before its owner resumes.

The child constructor at `0x0040145a` passes MCIWnd style `0x0A`; bit `0x08`
is `MCIWNDF_NOERRORDLG`. It then sets the shared active flag unconditionally
after the play request. Thus an unavailable or undecodable intro, winner, or
credit does not show a custom error or auto-resolve: the centered modal stays
blank until Enter/OK invokes its ordinary zero-result completion path. Native
now records actual child creation separately from timer completion, preventing
a click on the blank owner from incorrectly skipping a failed movie.

The six detail handlers at `0x00404c85` through `0x00404eb5` first stop the
current INFO segment, synchronously play `WAV/TT0.WAV` through `TT5.WAV` for
the selected label, and only then start that label's contestant-specific
`INFO.AVI` range with its embedded audio. The native handler now preserves
that ordering rather than starting the AVI range without its spoken prompt.
The six handlers index the one-based contestant into the 0x30-byte record table
at `0x004364a8`; all 60 inclusive pairs are transcribed and all 120 endpoint
frames are decoder-probed. Their `sndPlaySoundA` flags value is zero, so missing
prompts retain default-sound fallback rather than `SND_NODEFAULT` behavior.

A successful sponsorship immediately deducts its cost and paints that
contestant's 60×60 `GIRL%d.BMP` portrait at `(287 + slot*70, 412)`. There is no
separate confirmation step and no undo operation in the original screen. The
cash and new portrait are synchronously visible before first-sponsor `C12`.
`NOCASH1.WAV` and `TT0`–`TT5` use flags zero, retaining the original default
sound fallback; the pavilion ambience uses flags `0x09`, not `SND_NODEFAULT`.
The fifth valid selection immediately advances to category weights, followed by
ORDER; stage-navigation
keys cannot bypass the five-contestant requirement. Duplicate and unaffordable
choices are rejected.

Browse-left and browse-right each select the new contestant, stop/seek any
visible Cryo movie children, then call `sndPlaySound(CRYO.WAV, 0x19)`. The
`ASYNC | LOOP | NOSTOP` combination preserves an announcement that is still
active but restores the Cryo loop after a completed synchronous detail or
no-cash response displaced it. Initial Cryo entry uses `0x09` instead.

The fifth successful sponsorship returns result zero and immediately executes
destructor `0x0040621f`. It closes the retained ROTATE child (`+0x210`) before
the INFO child (`+0x20c`), which also ends any bounded INFO PCM, then stops the
shared legacy channel. It deletes the five control highlights, six detail
highlights, backing/base, BUTTONS, latest sponsored portrait wrapper, INFOBK,
and MONEY before ending the dialog. Only after this destructor returns does
the top-level `0x00401b03` call variant preparation `0x0040304e`; no Cryo movie
or audio remains active during those ten random draws and writes.

`fcn.0040391f` stores the ten names at game-document offsets
`+0x108..+0x1bc` and the matching sponsorship values at `+0xd4..+0xf8`.
The pavilion paint paths index the names as
`document + contestant_id * 0x14 + 0xf4`, so contestant ID 1 is Queen (not
Domina). The exact ID order is:

| ID | Contestant | Sponsorship |
|---:|---|---:|
| 1 | Queen | $100 |
| 2 | Nancy | $250 |
| 3 | Suzi | $1,000 |
| 4 | Conchita | $250 |
| 5 | Jane | $150 |
| 6 | Dee | $50 |
| 7 | Sammy | $50 |
| 8 | Rhonda | $500 |
| 9 | Jackie | $50 |
| 10 | Domina | $150 |

The same constructor clears the five selected IDs, fifteen rating bytes, five
disqualification fields, three category weights, cash, and accumulated reward;
it initializes each of the fifteen pavilion-pending fields to one and writes
the ten constructor-default ORDER mapping words as `9,8,7,6,5,4,3,2,1,0`.
Before `MMINTRO.AVI`, however, top-level `0x00401b03` unconditionally installs
the fixed `3,4,5,8,10` roster and overwrites that table with
`0,1,2,3,4,5,6,7,8,9`. Ordinary Cryo later replaces all five IDs and ORDER
replaces all ten comment words. DEMO skips both screens, so its judging audio
uses the identity table for the full round. The native pre-intro state now
keeps that exact distinction.

Choosing a contestant in Cryo deducts her full sponsorship and adds the same
amount to the round's accumulated reward. The original also looks ahead to the
cheapest remaining choices and rejects a selection that would make completing
a five-person roster unaffordable. The native implementation preserves the
executable's exact ordering and even its zero-based/one-based comparison quirk.

Opening a new Talent or Brains contestant performance costs another 10% of her
sponsorship; replaying the current performance does not. The original permits a
tolerance down to -$0.001 and clamps that tiny negative result to zero. A lower
balance synchronously plays `NOCASH2.WAV`, clears the active-contestant field,
and returns to the pavilion chooser. Looks is a bitmap/ROTATE presentation and
has no corresponding surcharge or `NOCASH2` branch in `fcn.00413361`.

The judge cadet has two independent random fields: a position among the five
sponsored contestants and one clue pavilion (Brains, Talent, or Looks). The
  slot alone controls Penalty correctness; both fields control whether `BI`, `TI`,
or Looks `IM` media reveal the clue. A wrong Penalty Box accusation plays
`WRONG5.WAV` and deducts 10%
of that contestant's sponsorship, then returns without restarting pavilion
ambience. Talent and Brains tear down the active 320×240 performance before
testing the accusation, so it remains blank even on the wrong branch; Looks
keeps its current rotate/magnifier presentation. A correct accusation follows the reward
  path, refunds the full sponsorship, and marks the contestant disqualified.
  The correct branch zeros the contestant's stored category score. Talent and
  Brains deliberately leave the dialog-local numeric rating and current gauge
  untouched; Looks resets its local number and hides the current gauge while
  retaining that wrapper until replacement or dialog teardown.
  The `PENALTY/%d.BMP` portrait (and Looks' hidden meter state) is synchronously
  repainted after `REWARD1.WAV` but before `PENALTY.WAV` and the TP/BP/LP response movie.
  The response movie is an in-handler MCI `Wait`: player input cannot cancel or
  truncate it, and only after it returns may the contestant advance. This is
  intentionally unlike Slots, whose 4.5-second deadline explicitly runs a
  `PeekMessage`/translate/dispatch loop while it waits.
  Looks additionally hides ROTATE and repaints the presentation area to its
  background before `PENALTY.WAV`; the stopped ROTATE surface is restored only
  after LP returns (or immediately after an LP open failure), before the
  automatic selector and `LOOKS.WAV` restart.
Gong is a separate Brains/Talent action: it completes only the current category
with a zero rating. Both handlers first tear down and repaint the active 320x240
performance, then paint the contestant's `GONG/%d.BMP` portrait and force that
update before playing `GONG.WAV` synchronously. The first eligible `C61` host
response therefore does not start until the gong finishes. The performance
teardown returns whether Gong interrupted an active movie; a true return skips
C61. Normal movie-notify completion has already destroyed the child and
restored ambience, so a later Gong can run C61. C71 uses the same captured
condition after the synchronous correct-Penalty response. Looks intentionally
has no Gong action.

The Talent/Brains dispatcher argument tuples are exact: entry uses `(2,6,1)`
or `(3,6,1)`, rating response uses `(0,4,rating)`, Gong uses `(6,6,1)`,
Penalty uses `(7,6,1)`, and the Brains completion insert uses `(8,6,1)`.
Every call stops ordinary sound before constructing the generic wrapper and
restarts the pavilion loop after destroying it, including on construction
failure. The outer Gong/Penalty tail starts that loop a second time. The C81
tail instead enters pavilion close immediately, whose first audio operation
stops the loop that the dispatcher just restored.

A direct mouse-down on wide ACCEPT is disabled while the 320x240 performance
wrapper exists, but the three controls share one press latch. Pressing Gong or
Penalty and dragging the release onto ACCEPT therefore invokes ACCEPT without
destroying that performance. Its mode-4 X overlay pumps the underlying dialog:
the performance may notify before or after X completes. Advancing the accepted
contestant must retain that wrapper until its own notification (or an explicit
meter/Gong/Penalty/close teardown) clears it.

## MMS sprite scripts

The 51 `.MMS` files are binary, little-endian motion/event scripts loaded as one
contiguous buffer. The following header/record offsets are verified from the
32-bit update path; semantic names remain provisional until dynamic traces cover
every flag combination:

| Offset | Width | Observed use |
|---:|---:|---|
| `0x06` | 16-bit | loop/reset frame index |
| `0x08` | 16-bit | exclusive terminal AVI-frame bound |
| `0x0c` | 16-bit | offset of the next record from the buffer base |
| `0x0e` | 16-bit | event/movement flags |
| `0x14` | 16-bit | timer interval, also used to derive update pacing |
| `0x16` | 16-bit | termination/result code |
| `0x18` | 16-bit | initial sprite X |
| `0x1a` | 16-bit | initial sprite Y |
| `0x1c` | 16-bit | sound/event index |
| `0x1e` | 16-bit | sprite Z order |
| `0x24` | 32-bit | packed background-sprite position |

Each state record is 44 bytes. The root is at file offset zero; 16-bit fields at
`0x0A` and `0x0C` link to hit-driven and timer-driven records respectively (zero
means no transition). The native parser walks this record graph, validates every
link, retains the original bytes and unknown fields, and decodes coordinate
streams referenced by records carrying flag `0x0200`. At each tick the original
applies absolute or relative X/Y values, calls `SetSpritePosition`, handles
sound/score flags, and loops or terminates according to the record flags.

The on-disc navigation encounter pairs `DAT/NAV/D0.MMS` through `D10.MMS` with
blue-screen sprite movies `MOV/NAV/T0.AVI` through `T10.AVI`. MMS positions are
top-left anchors in the original 640×480 canvas. The disassembled SPR.dll
`SetSpritePosition` export stores X/Y directly. Rendering uses the natural
half-open bitmap rectangle, but `SpriteHitTest` uses strict comparisons on all
four sides: `(x,x+width) × (y,y+height)`. A pixel exactly on any stored edge is
therefore not a hit. Flag `0x0100` makes a
coordinate trajectory relative to the record's initial position; otherwise its
points are absolute. The terminal value is exclusive: for example, the T1
reaction record uses `1..7` while its AVI contains frames 0 through 6. Scripts
D1–D10 transition from a moving state to a click reaction carrying terminal
code 50. D0 chains five moving/click phases and ends with code 100; an escape
path ends with zero. Those nonzero codes are success sentinels, not literal
dollar awards. The caller pays `(15 - 2 * pacing_factor)%` of the selected
contestant's sponsorship value, or of a fixed $250 base for D0: exactly 5–15
percent depending on the cash-derived factor. The native motion player and
navigation economy now preserve both layers rather than crediting a flat
$50/$100.

Delta-motion escape uses the original asymmetric strict bounds: `x < -200`,
`y < -200`, or `x > 680`. Exact `-200`/`680` values remain active, and there is
no upper-Y cutoff. That omission is observable in downward-moving authored
motion and is preserved rather than normalized to the 640×480 canvas.

That top-left convention is visually significant: D1 first appears at Y=452
with a 320-pixel-high T1 sprite, so only its leading edge enters at the bottom
of the canvas. Treating Y as a bottom anchor shifts every Simm and X host
reaction upward by its full frame height and moves the opaque hit region away
from the rendered pixels. Rendering and color-key hit testing now share the
top-left conversion while retaining SPR.dll's separately regression-locked
strict border exclusion.

The hit handler formats that computed award as white 24-point Arial
`$%4.2f` at logical `(500,20)` immediately when the successful reaction record
is entered, but it does not add the amount to document cash yet. The outer
navigation caller recomputes/commits the same amount only after the modal MMS
reaction finishes. The native renderer now shows the award during the reaction
and retains that delayed accounting boundary.

The generic-media mouse handler beginning after the disassembler-split boundary
at `0x0040d6c2` makes mode 3 a true modal target. Clicking inside the moving
child follows the MMS hit link and its `T#S#.WAV` state sound. Clicking anywhere
outside it plays `WAV/NAV/RICOCHET.WAV` on WaveMix channel 2 and leaves the
encounter running. The native player preserves both outcomes on an independent
PCM effect channel; the 17,406-sample, 11025 Hz mono copies on both discs were
decoded and verified identical. The handler first tests MMS flag `0x0080` and
ignores the click entirely when it is set. All D1–D10 $50 reaction records and
all five D0 $100-chain reaction records carry that flag, so subsequent inside
clicks and outside misses are silent until the authored reaction completes;
the native input path now preserves that modal suppression.

MMS state voices `T#S#.WAV` use WaveMix channel 3 inside the generic object's
session. They can therefore coexist with navigation/departure audio, replace
one another on a state change, and are stopped when `fcn.0040e4bd` closes the
generic object after its motion graph returns. The native runtime now gives
those voices a separate decoded-PCM channel with the same sprite-object
lifetime instead of routing them through the process-wide `sndPlaySound`
channel. Record flag `0x0040` is what starts a state voice; a hit from such a
record first stops channel 3, and the silent `0x0080` reaction records do not
restart `T#S0`. Across all 108 reachable records, the corpus has exactly ten
flag values and fifteen flag/timer/result/sound combinations, now locked by the
asset probe.

Generic media is not paced at a fixed movie frame rate. In `fcn.0040e6f2`, the
current record's signed `timer_interval / 20` supplies the delay count (zero is
promoted to one), and each pacing unit waits for two distinct `timeGetTime`
changes. Mode 4 and mode 6 force a factor of five; a record with flag `0x0080`
forces eight before those mode overrides. The dormant mode-5 B-movie fallback
instead forces ten units for the root MMS record and one for every linked
record. Consequently ordinary zero-timer X motion advances at about 10
ms per update, X13's one 60-timer record at about 30 ms, and the eight
200-timer D2/D3/D4/D5/D7/D8/D9/D10 reward records at about 160 ms. The native
clock reproduces these two-tick products instead of its former 100 ms
placeholder cadence.

Mode 4's timed-record phase uses `fcn.0040eb52`, not that ordinary product.
Its first call only captures `timeGetTime`. Normal passes wait to 100 ms; a
late pass accumulates only the excess beyond 100 ms, and performs catch-up only
when that accumulator is strictly greater than 100. The catch-up count is the
truncated single-precision product with `0.01f`; it advances AVI frame state
without consuming additional MMS coordinate pairs, resets directly to the
record's loop frame if it reaches the terminal frame, and clears the
accumulator. Elapsed values above 100,000 ms are treated as stale/wrapped clock
samples: the timestamp is recaptured but an existing overrun is preserved.
The native host runner now reproduces this distinction under event-loop stalls.

The mode-3 Simm movement factor comes from `fcn.0040b26c`: truncate each of the
five selected sponsorship costs to integers, integer-average them, truncate
the current credit balance, then compute `clamp(5 - credits / average, 0, 5)`.
Zero means no deliberate wait; factors one through five yield approximately
2–10 ms for ordinary zero-timer movement. The reaction-record override remains
eight. The generic class's OnCancel at `0x0040e6e0` sets both completion flags;
its event loop then calls `fcn.0040e4bd` with result zero. Its derived
`WM_ACTIVATEAPP` handler at `0x0040ef28` ignores the `bActive` argument and
separately calls the same result-zero teardown on either activation-change
notification. The caller at `0x0040b374` awards only results greater than ten,
so Escape/window-close or Alt-Tab during either
movement or a hit reaction cancels
the encounter without committing its pending award. It still runs the common
caller tail; right-click and stale menu commands remain owned by the modal
child rather than leaking into the disabled navigation parent.
When the modal object returns, navigation stores a new timestamp before
resuming, so the native completion/failure paths also rearm the ten-second
XR1 idle-animation clock rather than allowing an expired idle timer to fire
immediately. That return tail also restarts `WAV/NAV/ZPR.WAV` on channel 2 on
success, escape, or generic-loader failure; the native path does the same.

The executable does not spawn one continuously merely because the current DAT
tree is XR1 or XR2. Its 500 ms navigation timer waits for strictly more than
1000 ms on a settled object whose
flags include `0x0080`, checks the outer object's per-index consumed array, and
then runs generic media mode 3. An exact 1000 ms callback is rejected, so the
usual phase-aligned appearance is at 1500 ms rather than the native port's old
near-1000 ms 10 ms-pump trigger. The 13 flagged records are Brains 4/6/8, Looks
1/4/7/8, Talent 2/4/6/8/10/12, and no XR1/XR2 records. The lookup is indexed by
the DAT object's numeric ID, but the loader clears the new DAT's entire ordinal
object prefix on every scene load. Repeated IDs across different trees therefore
do not share suppression state; returning to a previously loaded tree rearms
its encounters even within the same navigation popup. Popup reconstruction
after a pavilion also begins with the array clear.

The Simm media picker itself has a different lifetime. On the initial state-1
hub entry, the document's 256-byte table marks D0 and the five sponsored
contestant IDs eligible. `fcn.0040c3ed` samples uniformly from entries equal to
one and increments the chosen byte to two. When no ones remain, every nonzero
entry becomes one again. The replay reset does not clear this table: the next
state-1 entry immediately rearms D0 and the new five IDs, while older nonzero
IDs join the pool again after that pass is exhausted. The native trigger
preserves these table, timer, modal-input, and recycling rules. The original
would scan forever if all 256 bytes were zero, but that state is unreachable in
play because state one always arms D0 before a Simm can be scheduled. The native
loader reports a deterministic failure for that corrupt synthetic state instead.

`DAT/HOST/X312.MMS` is a zero-byte placeholder and is accepted as an empty
script, matching the source media.
