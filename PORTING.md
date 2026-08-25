# Native port architecture

## Completed offline target

The repository builds an x64 Win32 executable and a command-line media probe.
The app locates both extracted disc trees, renders original indexed BMPs through
GDI, plays PCM WAV samples, and runs the complete recovered offline flow.
Navigation transitions seek and play the exact inclusive frame ranges stored in
the original DAT graph. The media probe reconciles and decodes the complete
installed AVI corpus, including Cinepak, RLE8, and Microsoft Video 1 at their
original dimensions, and verifies every authored navigation range endpoint.
The codec subset is FFmpeg 8.1.2 built as static archives with only AVI/WAV,
Cinepak, Microsoft Video 1, Microsoft RLE, and PCM enabled. MinGW's GCC and C++
runtimes are also linked statically. The resulting game EXE imports only Windows
system DLLs; `scripts/verify-self-contained.ps1` enforces that property after
build and package operations.

The gameplay library now includes byte-compatible legacy profiles, exact Cryo
contestant-ID/name/cost pairing and affordability checks, performance surcharges
with the original unrounded x87 affordability comparison and stored-float clamp, custom
host-comment ordering, Judge-O-Matic response lookup, Gong and Penalty Box
semantics, pavilion rotation, slots, weighted scoring, completion awards, and
original tie behavior. These modules are covered by native tests and connected
to the Win32 flow. Mouse hit-testing and painting operate in the original fixed
640x480 logical coordinate space.
Cryo entry now retains its original dialog through the 500 ms C11 boundary:
`CRYO.BMP` receives its original base-only synchronous first exposure, then the
other fourteen static wrappers and initial `CRYO.WAV` loop exist before the host
modal runs. The loop restarts and ROTATE followed by hidden INFO are constructed
in place. The shared 118-byte Cryo host dispatcher now has an exact regression
contract for both calls: stop legacy audio, construct/run/destroy generic mode-6
media as channel 1 take 1 (C11) or take 2 (C12), then restart `CRYO.WAV`
unconditionally, including loader failure. Each missing static bitmap warns
independently; a missing pressed
highlight disables only its derived hit target. Browse, INFO, rotation,
sponsorship, portrait, and split mouse handlers are covered across every
contestant, control edge, and reachable cash boundary.
Correct Penalty Box responses now also reproduce the pre-media repaint: Looks
blanks its local gauge, while Talent/Brains retain their current numeric gauge,
alongside the penalty portrait after `REWARD1.WAV`, before `PENALTY.WAV` and
TP/BP/LP.
The TP/BP/LP response and Looks' bounded 20-frame ROTATE helper both originate
as in-handler MCI `Wait` calls. Their owner input is now held until completion,
so Escape, caption close, or another pavilion action cannot truncate or re-enter
them through the native decoder's asynchronous implementation.
Pavilion choices use the original 130-byte `nrfPav.dat` filename and line
layout; both that table and the 180-byte `hnrd.dat` host table are updated at
selection time even in guest rounds, while guest profile records remain
unsaved.
The tables are also read at their original use points: `nrfPav.dat` at round
variant preparation and `hnrd.dat` for each X reaction. Pavilion persistence
uses the recovered Brains-line/Talent-line `r+` update order rather than one
batched end-of-loop rewrite.
NRFPAV is not a startup or round gate. Its ten non-repeat constructors reseed
from `time(0)` before any file I/O; selected lines are then reopened in
contestant order, read as unchecked raw byte-minus-`'0'` values, and rewritten
best-effort. An all-nonzero active line yields variant zero without consuming
another random value, and all open/seek/read/write failures are ignored.
HNRD is not a startup gate: its original selector ignores open/read failure and
uses a pre-zeroed 180-byte table, so missing, short, or non-boolean state still
chooses an X clip. It tests raw little-endian words only for zero, resets an
exhausted active group before drawing, and immediately rewrites all 45 words
without normalizing malformed or inactive values. Selection uses the exact
forward zero scan after `floor(rand * available * 0x38000100)`; even the maximum
CRT output stays below the available count.
The Win32 host now also matches shared dialog resource 158: a centered,
borderless 640×480 `WS_POPUP` outer window with the exact `Ms. Metaverse`
title, instead of the port's former resizable 960×720 overlapped frame.
Startup now preserves the original chronology: the native profile/login dialog
completes before `MMINTRO.AVI`; `--guest` skips only that dialog and still plays
the intro. Before the movie, the orchestrator installs contestant IDs
`3,4,5,8,10`, the identity host-comment table `0,1,2,3,4,5,6,7,8,9`, and raw
weights `5,5,5` for every profile. Ordinary and guest play then enters `HALL.DAT` at navigation entry
13; its Ms. Metaverse action constructs first-round Cryo directly, without the
separate replay-reset loop or its ten RNG calls. The `DEMO` name with its
required `BUYVV` password retains those IDs and the identity comment table,
then enters navigation state 1 without Cryo, Weight, or ORDER. This fixes DEMO
Judge-O-Matic reactions selecting the reversed host-comment audio.
Each Cryo information-label click also follows the original media order:
`TT0.WAV` through `TT5.WAV` plays synchronously before the matching
contestant-specific `INFO.AVI` range and embedded audio. All 60 inclusive
start/end pairs are now sourced from the exact 120-DWORD table at `0x004364a8`;
the packaged media probe seeks all 120 endpoints. The prompt call also preserves
the original flags-zero default-sound fallback instead of suppressing it.
Cryo ROTATE no longer runs a contestant's whole 40-frame block in one click:
the recovered `0x00404962` marker advances in ten-frame quarter-turns and keeps
the original special wrap behavior for contestant IDs 7–10.
All five Cryo controls and six information labels now use their original
yellow pressed bitmaps at the constructor-defined hit rectangles. Recovered
handlers `0x00405d67` and `0x00405fb7` paint on mouse-down and dispatch only on
mouse-up inside that same target. Release synchronously repaints the unpressed
art before a browse, detail prompt, INFO segment, duplicate warning, no-cash
announcement, or sponsorship can begin; the former hover-only port behavior is gone.
The down handler's original Ctrl-click over `(465,24)-(565,50)` also adds
exactly $100. The Cryo painter now uses resource 158's 8-point MS Sans Serif,
transparent `$%d` / `$%.2f` TextOut calls at `(305,129)` / `(465,24)`, and the
five sponsored `GIRL%d.BMP` portraits at the exact bottom-strip positions.
Sponsorship preserves the original prospective-cash look-ahead, including its
zero-based affordability array versus one-based selected-ID comparison. A
successful debit queues the cash repaint and synchronously exposes the new
portrait before first-sponsor C12. The C12 dispatcher silences the Cryo loop
while modal and restores `CRYO.WAV` afterward even if host media startup fails.
After the fifth portrait, Cryo teardown closes ROTATE first, closes INFO and
any embedded PCM second, stops the shared loop, and releases the eleven pressed
DIBs plus its backing/base/control/portrait/background/money ownership in the
recovered order. The outer orchestrator only then generates and persists both
five-entry pavilion variant sets; Weight and ORDER run in their recovered order
before navigation state 1. This prevents an active INFO segment from leaking
across the modal boundary.
The recovered document initializer fixes IDs 1–10 as Queen, Nancy, Suzi,
Conchita, Jane, Dee, Sammy, Rhonda, Jackie, and Domina; the native status and
winner text use that same order instead of the formerly reversed name table.

The pre-intro profile chooser now renders the original 640×480
`BMP/PASSWORD/PASSWORD.BMP` borderless popup. Its uppercase name/password edits use the recovered
`(205,208)` and `(205,295)` 208×16 rectangles, while the native-size 53×52
`OK.BMP` at `(535,378)` follows the original synchronous no-capture press and
mouse-up-inside submission path. It centers against the full desktop and does
not add an edit-length limit; the legacy scan and writer use the first ten
characters while the running document retains the complete submitted text.
Conversion uses the active Windows ANSI code page, matching MM.EXE rather than
adding a printable-ASCII-only branch. The surrounding loop preserves the original GUEST,
DEMO-with-BUYVV-password, missing-input, wrong-password, new-profile, and returning-credit
decisions without introducing any removable-media check. Escape or close remain
in the dialog because profile OnCancel is the bare return at `0x00408617`;
default Enter is likewise ignored by the adjacent OnOK return. Only the painted
OK release submits the last stored fields.
An absent `MM.DAT` is created empty before the dialog. Valid `DEMO`/`BUYVV`
still traverses the ordinary record scan: a stored DEMO balance is restored and
floored, while an unmatched DEMO starts at $500; both remain excluded from save.
The scan no longer rejects the whole database for an unrelated malformed record
or non-finite credit payload. MM.EXE copies an authenticated credit DWORD and
compares it as a signed integer to float bits `0x43c80000`; native activation
now preserves that raw-bit floor, including its NaN edge cases. A dialog
creation/modal failure also falls through to the retained initial GUEST fields,
whereas ordinary Enter/Escape/close remain inert.
Every other selected profile is updated once after the game flow returns. The
targeted save also matches on each credential's first ten characters, replaces
only the first matching 28-byte record, and otherwise appends without rewriting
unrelated bytes. A damaged trailing fragment is preserved before the safe,
deterministic appended record. The
original ignores MM.DAT open/update failure, so the native exit tail likewise
does not display a port-only save-error dialog.

The MMS runtime now composites the original blue-keyed wandering Simm AVIs over
navigation at SPR.dll's top-left X/Y coordinates, applies absolute and
cumulative-relative motion paths, follows hit/timer state
transitions, plays per-state voice samples, and runs mode-4 host reaction frames
at the original timed-record 100 ms cadence. The DLL's hit test adds the
caller's `+16,+16`, rejects points exactly on any of the four sprite borders,
and then compares against the AVI's top-left color key; host and Simm clicks use
those same one-pixel rules. Its `50`/`100` MMS terminal
values are success sentinels; the outer caller awards the recovered 5–15% of
the sponsored contestant's cost (or the fixed $250 D0 base) only after the
modal encounter returns. Generic OnCancel and the separate `WM_ACTIVATEAPP`
handler both finish with result zero, below the caller's award threshold; the
latter ignores `bActive` and handles both activation states exactly as the
original. Escape/window-close or Alt-Tab during movement or a reaction
therefore tears the Simm down, discards any pending award, restarts ZPR, and
resumes navigation; its modal child also blocks the underlying right-click
menu. Encounters begin only after the recovered one-second dwell on a DAT
`0x0080` object and are consumed by the original numeric object-index state;
each cross-scene DAT load clears that new scene's ordinal object prefix, so a
return to the same tree rearms its encounters. They are not continuous
Crossroads ambience. Clicking outside the moving child
plays the original `RICOCHET.WAV` on the shared navigation-effect channel and
keeps mode 3 modal; a hit follows the MMS link and per-state voice channel.
Mode 3 also uses the recovered target cursor resource 162. Generic modes 4 and
6 use hand resource 166; all are compiled into the EXE, and the explicit
`WM_SETCURSOR` dispatch prevents the native class default from overwriting the
original navigation/exit/magnifier cursor owners.
Navigation scene construction also keeps its recovered trigger order: the
initial `0x0400` WAV and `0x0100` background-movie range are dispatched before
the scene's CRT reseed. Initial `0x0800`/`0x2000` objects remain settled until a
later arrival path, and leading-`0` asset tokens are silent. The physical-CD
validator between construction and reseeding is deliberately absent.
All 306 decodable navigation range endpoints are package-probed. XR1's final
idle range uses MCI's legal stream-length stop 1686 for a zero-based 0..1685
AVI; the native player clamps that one EOF boundary, preserves frame 1685, and
completes the route instead of leaving navigation blocked. Audio-clock jumps
are likewise capped at the current DAT range endpoint.
Disk II integration adds its 70 Talent movies, all 50 Talent comments,
the 23-object Talent pavilion, 22-object Crossroads II, 8-object tally machine,
ten winner movies, and credit movie. The roots remain separate at runtime so
the five same-path files that differ between discs retain their proper versions.

The recovered offline target has no known unmapped gameplay function or active
media trigger. The exhaustive recovered-function ledger in
`analysis/function-parity.md` is the authority for that claim; it accounts for
all 1,046 recovered functions with no partial or unmapped rows. The completion
matrix and release evidence are recorded in `analysis/completion-audit.md`.
The companion `analysis/media-trigger-audit.md` reconciles all 266 supplied AVI
files with their recovered call sites and timing boundaries. The sole installed
inactive movie is the movie-only `MOV/HOST/X11DB.AVI` artifact, which has no
MMS, WAV, executable reference, or second-disc counterpart.

The retired Virtual Vegas custom-sprite path is now separated from that offline
backlog. Its Winsock/FTP bootstrap (`0x00414aee`), FTP response state machine
(`0x00414fc2`), active-mode `PORT` setup (`0x00415801`), WSOCK bitmap receiver
(`0x00415315`), `TLST.DAT` completion/cache update (`0x00415b17`), and
`BMP/SOCK/ONLINE1.BMP` transfer painter (`0x0041608b`) are audited as obsolete
online-service functions and intentionally excluded from the native offline
target.
Its two DAT `0x2000` callers are not discarded: Brains object 19 and Hall
object 12 still follow their original middle AVI range/link after the skipped
dialog. Together with plain `0x0100` and sound-bearing `0x0800`, native routing
uses the recovered `0x2900` automatic mask for all 60 bundled nodes.

The current offline target deliberately omits every physical-CD/drive/volume
check. Runtime media comes only from configured or packaged asset directories.
The recovered two-CD `VOL.DAT` insert loop, validator, and prompt/remapper are
explicitly bypassed: extended navigation targets are loaded directly. DAT flag `0x1000` is retained solely
for its real purpose, selecting the original fade-to-white scene transition.
That path stops the active decoded MCI-equivalent range first, then applies the
handler's exact 199-pass `+10` palette recurrence, including its green/blue
saturation writes-to-red quirk. Only the old loop's wall time is platform
paced because the 1995 code used a synchronous, unclocked palette-realization
loop.

The recovered navigation popup is now active on right-click outside the Hall:
Talent, Brains, Looks, Slots, Exit, and Skip Ahead use original command IDs
200/201/202/204/205/206. Slots jumps straight to bundled `NAVIGATE.DAT` entry
29 or 30, and Hall's retired Blackjack/Assault actions return locally instead
of entering their original disc/installation loops. Escape, the window close
button, and popup Exit use the original quit-confirmation text.
Opening that popup also reproduces the original `+0x628` activity latch: if it
is dismissed without a command, the ten-second `0x0020` ambient navigation
movie remains suppressed until the next movement or Skip request. During the
Slots deadline the original nested message pump still permits the popup,
Skip, direct pavilion/Slots commands, window close, and left-button messages.
Skip or any left-button down aborts the notified reel play; its completion is
handled by that same pump and reveals the result immediately. The old call
stack still runs to its 4.5-second boundary,
but input is live again and a second popup shows Skip Ahead gray.
Natural and interrupted range completion now synchronously invalidates and
paints the resolved navigation surface after its arrival handler. This is
required by the native retained compositor: a destination hold-frame seek or
chained DAT load changes the decoded bitmap in memory, but no playback timer
remains to expose it. Without this publish step, the final cutscene frame could
stay painted indefinitely. The Slots result callback uses the same immediate
publish boundary for its symbols and updated balance.
Because `TrackPopupMenu` has its own message loop, an enabled Skip item can also
be selected just after an ordinary range finishes. Command 206 then follows the
original shared-handler quirk: it forces the current DAT object's middle
range/link even when that object has no Up direction flag.

Wandering-Simm and idle-video triggers now run only on the recovered 500 ms
navigation timer. Their comparisons are strict `>1000` and `>10000`, so an
exact-threshold callback waits for the following timer tick; this restores the
authored media timing that the former general 10 ms pump started too early.

Crossroads and pavilion navigation also use the recovered 320/240 mouse split,
DAT direction-bit gating, and original MM.EXE cursor resources 160/163/164.
The PE resource extractor now reconstructs standalone multi-image `.cur` files
from `RT_GROUP_CURSOR` plus `RT_CURSOR` data so these are genuine ripped assets.
Neutral navigation areas alternate original sparkling-hand cursor frames
165/161 on the recovered 500 ms timer; active navigation/Slots MCI forces that
neutral state and the timer keeps animating it, while directional cursor frames
remain steady after playback settles.
MM.EXE also loads cursor resources 169/170, but the only recovered navigation
writes to the state field are 0/1/3/4; state 5 is compared but never assigned.
Those two Simm-face cursors are therefore dead offline assets, not a reachable
native cursor state.

The general game-screen pointer is also restored from MM.EXE resource 166.
Talent, Brains, and Looks switch to the right-pointing pavilion-exit cursor 174
over their recovered `x < 60 && y < 382` direct-close region, while active Looks
rotate-child coordinates `(225,64)-(436,376)` use resource 181. All of these
`.cur` inputs are linked into the EXE as resources and are not runtime files.
Looks otherwise applies hand 166 across the parent except for the exact
`x > 580 && y >= 382` lower-right corner, where its mouse-move body deliberately
makes no `SetCursor` call and preserves the current cursor.
The rotate child's three front hit bands, two one-pixel gaps, whole-back hit,
and stable-view cursor are instruction-verified. A terminal magnifier load
failure now also follows MM.EXE by hiding ROTATE, clearing the old image,
advancing the level, and synchronously repainting only `(225,64)-(436,377)`.
The exact two twenty-frame ROTATE ranges and view toggle are verified for all
ten contestants; a failed unchecked MCI command still changes the logical
front/back state just as MM.EXE does.
All four magnifier levels persist across contestants during one Looks visit,
then reset to 1 when a new Looks dialog is entered from the hub.
Manual Looks portrait switches and post-ACCEPT automatic selection now both
reset the numeric value to zero and erase the prior contestant's gauge. Both
paths seek the same retained ROTATE decoder rather than reopening it.
The Looks constructor now also preloads hidden `MAG/111.BMP` and gauge
`LOOK/1.BMP` in the recovered `0x00412d8b` order. Meter zero, a manual switch,
and correct Penalty hide their retained surfaces instead of releasing them.
After a magnifier hides ROTATE, a click on that exposed parent rectangle
re-shows the stopped child without a decoder reopen or seek.
The three Looks controls are not gated on an active contestant ID, even in the
entry-delay or already-exhausted edge state, and they use no synthetic mouse
capture. Their split release handler clears ACCEPT, clears ROTATE before its
release hit test, but intentionally retains PENALTY's logical action code until
a later drag-out cancellation or another control press. PENALTY's pressed pixels
remain with that latch after an outside or wrong release. ROTATE performs a
forced paint before clearing its code, but because neither the base nor ROTATE
layer is dirty, its pressed pixels survive both an outside release and a full
inside bounded turn. The native compositor tracks these logical and pixel states
separately.
The recovered `0x004129d3` painter now has a tested operation ledger: dirty
LOOK backing, ACCEPT, PENALTY, ROTATE, and MAG layers; unconditional live cash;
dirty rating; unconditional five portraits and white frames; one-shot previous/
current selection frames; then the optional hover name. Its red selection frame
is intentionally not reconstructed after the `+0x70` dirty flag is consumed.
Correct Looks Penalty now blanks the ROTATE rectangle before `PENALTY.WAV` and
LP playback, then restores the stopped retained child only after the response
returns, including the original immediate recovery when LP cannot open.
An already-completed Looks visit still constructs a hidden ROTATE child at
frame zero. The recovered activation handler can re-show that retained surface
even though the entry scan found no contestant; the native host preserves this
quirk without requiring the legacy MCI runtime.
Node WAVs play on arrival after the transition, matching the original pointer
update and post-transition handler, rather than when leaving the source node.
Initial scene construction keeps its narrower `0x0400`-only cue test before an
initial `0x0100` automatic hop; later arrivals accept `0x0400|0x0800`.
The 35 real cue paths are probed in their bundled roots; XR2's one literal `0`
asset token is handled as its intentional silent automatic-node sentinel.
The four `XR1.DAT` `0x0040` nodes also alternate the settled navigation backing
with ripped N1-N4 overlay BMPs at their stored logical-canvas coordinates on
the recovered 500 ms timer. The first callback restores the backing and the
second shows Nn, instead of leaving the overlay continuously visible.
XR1 object 18's `0x0010` path is one-shot: its first traversal silences
navigation sound during frames 1391–1502, then later traversals jump directly
to the destination's held frame. That consumed latch belongs to the current
navigation popup: an in-scene revisit holds the frame, but a cross-scene popup
reconstruction clears the latch and re-arms the authored animation. The five
`0x0020` arrivals (objects 1 and
19–22) restore navigation audio state and seek `XRDS1F.AVI` to frame 25.
Every 320×240 navigation movie is now stretched into that 640×480 logical
canvas before overlays and MMS sprites, correcting the compositor scale as well
as the retained overlay positions.
Scene construction and single-frame seeks keep embedded AVI audio silent, as
the original MCI child did. Only `XRDS1F.AVI` and `XRDS2F.AVI` carry a soundtrack;
their 11,025 Hz mono PCM is started at the requested transition's first frame,
stopped at its bounded last frame or Skip Ahead, and cut before the destination
WAV. Its live sliced-PCM cursor selects the corresponding absolute AVI frame,
so delayed UI ticks seek within the authored range instead of drifting. The
other five DAT-selected backgrounds are probe-locked as silent.
The separate `ZPR.WAV` WaveMix channel is also restored: it plays before valid
manual departures, including slot Up/self, except when source flags contain
`0x0100` or `0x0800`. A dedicated PCM player preserves the original overlap
with other navigation and slot sounds.

Each Cryo INFO range uses the same bounded-media rule after its synchronous TT
prompt: the sliced embedded-PCM cursor selects the contestant/detail AVI frame,
while the exact recovered last-frame cutoff remains authoritative. Silent
ROTATE motion keeps its nominal AVI cadence.

The embedded Slots action is also traced through its painter. XR1/XR2 object 15
activates only through its Up/self link, runs frames 812–869 or 821–878 with
`SLOTSPIN.WAV` for exactly 4,500 ms, then paints the fifteen-source reel result
at native sizes and fixed positions. Wager, match/jackpot selection, result
sounds, blue balance, red signed award/loss, and immediate zero-credit handling
now follow the executable. In particular, all scaled CRT selection uses its
single-precision `1/32767` multiplier and truncation, including the reachable
boundary values that differ from a conventional `/32768` implementation. Slots
also preserves the signed raw-float-bit `$50` wager cap instead of using
`std::min`, plus the original matched/unmatched result-text side independent of
the amount sign. These distinctions retain positive/negative NaN and
negative-balance behavior accepted by the legacy profile loader. Slots
retains its separate recovered `PeekMessage`/translate/dispatch deadline instead
of inheriting the blocking pavilion MCI-Wait policy.
Assets resolve from the active packaged tree only;
there is no original-CD fallback or validation path.

The finale is driven only by Disk II `TALLY.DAT` object 3 action 5. It runs the
recovered weighted tally, handles the judge-cadet `WRONG4.WAV`/stake-removal
branch, and otherwise plays the matching `W1.AVI`–`W10.AVI` with embedded audio.
Incomplete rounds apply each `$27.00` contestant/category award as a separate
single-precision balance update, preserving MM.EXE's observable rounding rather
than replacing the loop with one bulk addition.
At $400 or more it shows the original replay question; Yes preserves the balance
and returns to Cryo, while No goes directly to `CREDIT.AVI`. Below $400,
Disk II-only `END2.WAV` plays synchronously before the credit movie. The earlier
port-only Enter/`C` shortcuts are removed. The original replay disc-swap prompt
is deliberately bypassed, so none of these branches probes physical media.
The shared intro/winner/credit player passes MCIWnd style `0x0A`, including
`MCIWNDF_NOERRORDLG`. An open/decode failure therefore leaves its centered
640×480 modal blank and active, with no error window; Enter/OK follows the same
intro continuation, winner-finale, or credit-exit path as an active skip. Once
the delayed MCI child exists, it applies the recovered 200% zoom to the
320×240 source. Its own left-button handler stops it, sends private message
`0x0405`, and follows that same result-zero boundary. Timer completion
is tracked separately from actual child creation: if `MCIWndCreateA` fails, a
click on the blank owner remains inert because only the missing child handles
left-button input. Completion always closes the media device and clears its
surface before the caller resumes; in particular, winner teardown repaints
black before the replay prompt or synchronous `END2.WAV`.
Penalty responses use a different synchronous helper and still advance after
an open/decode failure. The packaged asset probe prevents these failure paths
in a normal release without adding a codec DLL or disc fallback.
Talent/Brains performance wrappers also preserve the original failed-open
lifetime: the allocated blank child remains active, blocks ACCEPT, and counts
as interrupted when a meter, Gong, or Penalty action tears it down. This state
is independent of the native decoder handle, matching the original pointer
tests at `0x00407c38` and `0x00409e76`.
The one-shot vtable deliberately disables Escape/window-close for intro,
winner, and credit movies. Enter/default OK and a left click on the created MCI
child are skip paths: they stop active playback and return zero. Winner
continues into the finale, credits exit, and MMINTRO's caller treats zero like
natural completion and continues into ordinary entry-13 Hall or DEMO
navigation. Owner clicks during an instance's initial 100 ms pre-play timer do
not close that modal. Other pointer/menu dispatch into the covered scene stays
blocked. Correct Penalty responses are a different synchronous helper class: they
block owner Escape/close/pavilion input until natural completion; decode/open
failure still advances their disqualification flow instead of stranding it.

The Looks pavilion now uses its actual `ROTATE.AVI` presentation rather than a
magnifier bitmap as the initial contestant view. It seeks each contestant's
40-frame block, plays the two recovered 20-frame front/back turns, uses the
original body-region hit bands, cycles per-region close-ups with missing-level
wraparound, and substitutes all ten judge-cadet `IM` images only when the
round's separate clue-pavilion selector identifies Looks. That selector makes
`BI%d` and `TI%d` exclusive Judge Cadet alternatives in Brains and Talent;
normal contestants receive one `B%d%d` or `T%d%d` movie, not a fabricated
intro/performance pair. Normal completion destroys that movie and restores
pavilion ambience at the recovered MCI-notify point.

The shared host-media path now plays all six screen-entry C clips and the four
recovered state-event clips (`C12`, `C61`, `C71`, `C81`) as transparent,
HPNT-positioned overlays with companion WAV audio. C51 uses the recovered
2,000 ms ORDER timer; C11/C21/C31/C41/C91 use 500 ms. Selecting a normal rating
starts its ordered comment asynchronously. Talent/Brains ACCEPT immediately dim
the current portrait before one of the 38 active motion-scripted X host
responses; Looks runs X first and commits/dims only after it returns. Its
recovered dispatcher passes the current rating—not the contestant ID—to mode
4. All three ACCEPT callers therefore select group 3 for scores 0–3, group 1
for scores 4–7, and group 2 for scores 8–10. The C41 path uses
the exact channel/mode/take tuple 4/6/1, stops ambience before the modal helper,
and restores any retained ROTATE child to `(225,64,212,312)` afterward.
Score grouping, top-left SPR positioning, strict active clip bounds, and
the original 180-byte `hnrd.dat` no-repeat format are preserved. Clicking an
opaque host pixel during a C clip records the OUCH flag, ends the clip
immediately, and reproduces the original synchronous `OUCH.WAV` response from
the shared teardown. Transparent/background clicks also end the clip but stay
silent.
C61/C71 also preserve the recovered active-movie branch: interrupting a
still-running Talent/Brains performance suppresses that first host event;
acting after notify completion allows it.
The wide dispatch tuples are regression-locked as C21/C31 `(2|3,6,1)`, X
`(0,4,rating)`, C61 `(6,6,1)`, C71 `(7,6,1)`, and C81 `(8,6,1)`. Their shared
helper always stops then restores ordinary ambience, even after media-open
failure. Gong/Penalty repeat the restore in their outer tail; C81 immediately
stops it in pavilion close. A direct ACCEPT press remains disabled while a
wide performance child exists, but the original shared press latch permits a
Gong/Penalty-to-ACCEPT drag release. That path retains and continues the movie
under X until its independent notify tears it down and repaints exactly
`(208,108)-(528,344)`.
All three pavilion dialogs now stage their five category-pending values locally.
ACCEPT/Gong/correct-Penalty update that modal copy, while score, cash, and
disqualification writes retain their original immediate document timing. The
five pending values commit together at pavilion close before audio or bitmap/
movie teardown, including early direct exits and Brains' post-C81 close.
Their flags are pavilion-dialog-local and rearm on a later visit, whereas the
C21/C31/C41 entry flags remain consumed in round-document state.
The entry flag is cleared before generic media construction, so a missing or
failed host AVI/MMS/WAV still follows the result-zero continuation and cannot
replay C21/C31/C41 on a later pavilion visit.
The Looks entry keeps the performance rectangle blank during C41. Its delayed
callback creates `ROTATE.AVI` only after the host returns, selects the first
pending contestant, or hides ROTATE when none remain; `MAG/111.BMP` is not an
idle chooser even though its hidden wrapper is preloaded. C21/C31/C41
completion resumes inside the already-constructed pavilion and does not reload
its background, controls, gauge, or portraits.
If C41 was already consumed, Looks still waits the same 500 ms timer before
ROTATE construction, automatic selection/seek, and `LOOKS.WAV`; only the modal
host call is omitted.
The generic host runner uses the companion WAV as the mode-6 master clock. It
maps the live 11,025 Hz sample cursor to
`floor(samples * 10 / 11025) + 1`, returns when that audio ends rather than
showing a longer AVI's silent tail, and holds the last visual only if the AVI
ends first. Host speech uses an independent WinMM PCM channel, while dispatch
clears the ordinary scene-sound channel. X mode-4 reactions continue advancing
an underlying Talent/Brains movie, so its completion notify can restore pavilion
ambience even before the host response leaves the screen.
Click, Enter/default-OK, activation-change, failure, and natural-completion
routes all converge on one teardown boundary. Because the native port composites
the original modal child into its owner HWND, that boundary now synchronously
rebuilds the complete retained Weight or ORDER parent before running gauge or
comment continuation; the final decoded host sprite therefore cannot survive a
skip in pixels outside the continuation's smaller dirty rectangles.

The other modal/one-shot AVI families use their embedded PCM cursor as the
shared media clock, matching MCIWnd behavior for the intro, normal
Talent/Brains performances, TP/BP/LP responses, winner movies, and credits.
When the native message loop is delayed it seeks to the authored frame instead
of making the movie and its modal completion drift behind the audio. Authored
silent visual tails still run at AVI cadence after PCM ends—about one second in
TP/BP responses and up to two frames in normal presentations—before the video
completion notification advances gameplay.
`MMINTRO.AVI`, all ten winner movies, and `CREDIT.AVI` share resource 158's
entry boundary: the centered 640×480 modal paints blank, waits 100 ms on timer
ID 1, and only then creates, zooms to 200%, and starts its movie child. The full
asset probe decodes all 4,970 frames across those twelve files; their visual
tails are 0.03161–0.1 seconds after PCM, so the native audio-clocked path
returns to AVI cadence and reaches the same final notify boundary. Pavilion
performance and Penalty movies remain on their distinct immediate-start
handlers.

Weight now precedes ORDER as it does at `0x0040209a`/`0x00402107`. Both screens
use the original bitmap coordinates and pressed-state behavior: Weight starts at
5/5/5 after C91, but its verified constructor owns hidden `TH*_1` backing
wrappers throughout that introduction before replacing them with `TH*_5`.
It blanks zero gauges and commits OK on release. The verified gauge helper
also retains the previous bitmap wrapper at zero while hiding it beneath the
base repaint, then destroys it on the next nonzero replacement. Its teardown
also releases any retained wrapper before either continuing to ORDER or taking
the confirmed-exit path. Arrow/OK press, release, and drag-out synchronously
invalidate their exact original rectangles. The verified stateful painter
still consumes base, OK, selected-arrow, and current-gauge dirty flags in
original order, while the fresh native back buffer reconstructs all retained
layers so a partial repaint cannot replace the rest of the window with black.
ORDER is instruction-verified as a full dialog family too: its constructor
warning-loads ACCEPT, TH10, and HAND in recovered order; C51 completes in place
by synchronously loading each phrase row and moving HAND; its dirty flags retain
their recovered consumption order while each native back buffer restores the
complete board. Assignment paints the red
rank before TTn, replaces THn before committing the reversed document mapping,
and assigns ACCEPT on release.
ORDER's unique right-double-click handler also exits result-zero without a quit
prompt; the caller proceeds into navigation with any partial ranking table. Its
ordinary OnCancel first shows the recovered quit confirmation, but a Yes ends
ORDER with result one and the same caller still proceeds into navigation; the
native port records this original quirk for parity but deliberately corrects
the user-facing route: confirming that prompt now releases ORDER and exits.
The separate right-double-click and completed-ranking routes still release
ORDER-owned media before navigation without stopping the final asynchronous
`TT1.WAV`.
The three judging screens likewise use their exact meter/control rectangles and
ripped ACCEPT/GONG/PENALTY/ROTATE pressed art. Port-only canvas help/status text
has been removed. The native window caption now remains the executable's exact
`Ms. Metaverse`; scene, node, player, credit, and selection diagnostics no
longer leak into player-visible UI.
The original PE contains no accelerator resource, and its recovered game
handlers route through painted mouse targets. Former port-only
arrow/Enter/Space/R/I/G/X/P controls no longer bypass the original hit and
press/release semantics. `Alt+Enter` is the explicit native display shortcut:
it toggles monitor-sized borderless fullscreen, preserves the 4:3
640×480 canvas, maps pointer/cursor work through the letterbox transform, and
restores the exact prior window rectangle. The separate `Ctrl+Alt+F1` convenience
sets the live document balance to exactly `$999999.00`; named profiles persist
it through the existing exit-save path. The remaining Enter/Escape handling is default-dialog
behavior recovered from vtable slots 31/32: navigation Enter exits result-zero
without confirmation; profile and centered-media Cancel are no-ops; most game
dialogs confirm and exit; ORDER confirms and then continues into navigation.
The audited `WM_ACTIVATEAPP` handlers are preserved as well: Cryo/Talent/Brains
rearm painter state on either edge; reactivation reloads ORDER phrases and
Weight gauges and re-shows an existing Looks ROTATE child. Generic media retains
its distinct result-zero completion on either activation edge.
The recovered wide-pavilion release handlers use one shared press flag and then
test ACCEPT, GONG, and PENALTY from the mouse-up point; Looks instead binds its
release to the action code set on mouse-down. All three mouse-move handlers now
also reproduce the green bottom-strip contestant name over the hovered portrait.
Pavilion entry now waits at the original five-portrait chooser: selecting any
unfinished Talent/Brains portrait triggers that contestant's surcharge and
media, while Looks opens directly without a second fee. An unaffordable
Talent/Brains performance plays the original synchronous `NOCASH2.WAV`, clears
the active contestant, and returns to the chooser rather than trapping the
round. That failure retains the chooser's existing rating/gauge and does not
reconstruct its dialog resources. A successful charge resets the numeric value,
installs the category's `0.BMP`, and repaints it visibly. ACCEPT, Gong, and
correct Penalty likewise return in place, preserving
the current numeric gauge while replacing only the completed portrait; wrong
Penalty leaves the active contestant available for continued judging. The surcharge and wrong-accusation balance mutations keep the original
x87 multiply-add as one chain, including the `-0.001f` affordability tolerance
and unsigned stored-bit negative clamp. Each completed result returns to the chooser instead of auto-loading slots 1→5.
The fifth result also leaves that completed chooser open. In Talent, another
portrait-strip click closes it; in Brains, that click plays C81 and returns to
navigation only after its picture and speech finish. All three pavilions also
retain the original `x < 60, y < 382` direct exit target; Looks uses this path.
Their painters now preserve the shared green live-cash origin `(465,25)`, the
wide `$%.2f   ` versus Looks `$%.2f` repaint formats, and the five white
portrait borders. The Talent/Brains red current-slot border is the original
one-paint dirty latch rather than a persistent compositor state; hover updates
invalidate only `(287,388)-(640,410)`.

## Target modules

| Module | Responsibility |
|---|---|
| Asset catalog | Case-insensitive path lookup, manifests, hashes, and source-media validation |
| Renderer | 640×480 logical canvas, indexed artwork, sprite color key, cursor layers, scaling |
| Media | Statically linked minimal FFmpeg video/PCM decode, native PCM mix, monotonic clock, frame/audio synchronization |
| Data | Typed parsers for navigation DAT, scene DAT, host point data, and MMS scripts |
| Gameplay | Profiles, credits, contestant selection, weights, scores, penalty/gong, tally |
| Scenes | Intro, login, cryo, ordering, weights, crossroads, pavilions, slots, tally |
| Persistence | Versioned local save file replacing registry/install-directory state |
| Online provider | Disabled/offline provider by default; no dependency on obsolete services |

The recovered `mmpath` registry readers at `0x004027d5`, `0x0040c4a1`, and
`0x0040ece1`, the shared root/relative composer at `0x00403cbd`, the legacy
`path1`/`path2` active-root copier at `0x00403cf0`, and the legacy
`wavemix.ini` bootstrap at `0x004033cd` are deliberately replaced by configured
asset roots and the native audio runtime. No original installation state is
required, just as no original disc is required.

The original composer assumes a three-character drive root and overwrites its
scratch buffer from byte three with the relative media name. The native
replacement uses filesystem joins against the packaged `assets`/`assets2`
directories and routes second-disc scenes/categories explicitly. Startup
validates data sentinels only; it performs no `VOL.DAT`, volume-label, registry,
or `mmsatan.ini` check.

The disassembler-split startup body at `0x00401818–0x00401a38` is also excluded.
That original code scans every drive letter with `GetDriveType`, searches for
`X:\\vol.dat`, and refuses startup without a CD-ROM. The native application has
no corresponding drive scan, volume-file read, or insert-disc prompt.

## Completion verification

`scripts/test.ps1` runs extraction-helper tests, a clean native rebuild, the
static dependency check, native regression tests, the complete two-disc media/
trigger probe, and a real Win32 UI-route audit. `scripts/package-native.ps1`
repeats the dependency and complete packaged-asset probes, then launches the
packaged EXE without asset command-line options to prove beside-EXE asset
resolution, profile modal behavior, intro completion, and navigation exit.
That GUI audit also asserts both `Alt+Enter` bounds transitions and exact
windowed-rectangle restoration. Native regression tests additionally assert the
exact `Ctrl+Alt+F1` modifier route and its idempotent `$999999` assignment.

The Windows 95 PE32 `SETUP32/DATA/MM.EXE` is the instruction-level behavioral
reference. The VHD's byte-identical Windows 3.1/`SETUP16` executable and its
installed state files are retained as corroborating format/runtime evidence;
the native port does not attempt to preserve two mutually different platform
implementations. Physical-CD validation is omitted at the user's request, and
the unreachable external Virtual Vegas service is outside the offline target.

Generated extracted assets and disassembly stay ignored. Only original code,
parsers, tests, and behavioral documentation are versioned.
