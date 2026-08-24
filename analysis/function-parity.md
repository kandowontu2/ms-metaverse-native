# MM.EXE function-parity audit

## Scope and method

`tools/audit_function_parity.py` inventories every function recovered in
`artifacts/disassembly/mm/functions.json`, resolves its direct strings and
resource references, counts internal/external calls, assigns a provisional
subsystem, and records a deliberately conservative parity status. Run it from
the repository root:

```powershell
python tools/audit_function_parity.py
```

The generated `artifacts/function-parity.tsv` contains all 1,046 recovered
functions. The raw disassembly and generated ledger remain ignored because they
are derived artifacts; the generator and this reviewed status report are
versioned.

Current inventory:

| Classification | Functions |
|---|---:|
| Recovered functions | 1,046 |
| Replaced platform/runtime functions | 928 |
| Directly references a DAT/BMP/AVI/WAV/MMS resource | 53 |
| Implemented and instruction-verified | 100 |
| Implemented partially after a semantic trace | 0 |
| Replaced bundled-media/install-path routines | 9 |
| Excluded obsolete online-service routines | 9 |
| Functions still requiring instruction-level mapping | 0 |

`implemented-verified` means every reachable semantic branch and state mutation
in that recovered function has an instruction-level native comparison and
focused regression coverage. There are currently no `implemented-partial`
rows; that classification remains reserved for a connected behavior whose
branch, paint, error, or timing comparison is incomplete. The pure
tally routine `0x004031d0`, the round pavilion-selection family
`0x0040304e`/`0x00416999–0x00416b9f`, and the configured-root X host-reaction
selector `0x0040e8ae` meet the stricter verified standard. The category-weight
adjustment and commit functions `0x00410285`/`0x004104e4` are also verified
across every reachable control/value pair and control triple.
The complete Weight and ORDER dialog families are now verified as well,
including staged C91/C51 completion, bitmap ownership/failure order, clipped
stateful painters, exact pointer dirty rectangles, assignment/commit ordering,
and result-preserving teardown.
Cryo constructor `0x0040521d` and destructor `0x0040621f` are verified as an
ownership pair: base-only first exposure, fifteen exact loads/dimensions,
per-bitmap warning/disabled-target failure behavior, and the final
ROTATE/INFO/audio/DIB sequence before Pavilion variant generation. This closes
the prior native presentation and media-lifetime gaps at both modal boundaries.
Ten additional Cryo rows are now instruction-verified: browse left/right,
quarter-turn rotation, INFO toggle, sponsorship, both media-child constructors,
the C11/C12 host dispatcher, sponsored-portrait insertion, and the
disassembler-split mouse handlers. Their
coverage exhausts all contestant/wrap states, INFO visibility branches, control
and label edges, every reachable sponsorship subset and adjacent float cash
boundary, the 60 INFO ranges, and the 80 reachable ROTATE endpoint frames. The
recovered row at `0x00403fe7` is only the 118-byte host helper: stop legacy
audio, construct the generic runner, dispatch channel 1/take 1 or 2 in mode 6,
destroy the runner, and restart `CRYO.WAV` even after allocation/media failure.
The adjacent painter begins at `0x0040405d`; it is documented as a
disassembler-split class body and is not silently counted as part of this
verified ledger row.
The centered resource-158 constructor/lifecycle at `0x004012b5` and completion
helper at `0x0040152e` are now instruction-verified too. Coverage locks its
caption, 640×480 black owner, 100 ms timer, style `0x0A`, 200% child zoom,
successful-only notify, active/inactive click and OnOK split, private `0x0405`
result, inert Cancel/create-failure owner, and close/destroy-before-continuation
ordering across intro, ten winners, and credits.
The disassembler-split Looks rotate-child handler `0x004125b5` is also verified
across every local coordinate: its PtInRect-exclusive front bands retain the
two one-pixel gaps, the back view maps the whole child to region four,
synchronous turns exclude input, and split `WM_SETCURSOR` selects embedded
resource 181 only in a stable front/back view. Because it is a message-map
entry inside the range between catalogued symbols, it does not inflate the
1,046-row inventory's verified count.
Catalogued Looks rotation toggle `0x0041402a` and its branchless bounded-play
helper `0x00413be1` are instruction-verified for both twenty-frame branches,
all ten contestant-relative ranges, inclusive endpoints, stable-view flag
transitions, synchronous owner-input exclusion, and the original unchecked-MCI
failure behavior that still toggles the logical front/back state.
The adjacent ROTATE constructor `0x00413a36`, temporary LP response helper
`0x00413b13`, and automatic selector `0x004140bf` are instruction-verified as
well. This includes their distinct 212×312/200×320 child extents, unchecked
media failures, first-pending scan, and the hidden retained frame-zero child
that a later app reactivation can expose after completed-pavilion re-entry.
Looks ACCEPT `0x00413c1b` and Penalty `0x00413d61` are now verified through
their full state, warning, repaint, and media order. In particular, ACCEPT
preserves its gauge DIB while resetting the numeric value, and correct Penalty
blanks ROTATE before `PENALTY.WAV`/LP, restores it only after LP returns, and
writes disqualification after the forced PENALTY-portrait repaint.
The shared Looks media dispatcher `0x004132d6` is verified as well. It stops
ambient audio and dirties the LOOK backing before constructing the modal media
wrapper, forwards `(0,4,6,0,1)` for C41 and
`(0,0,4,0,current-rating)` for ACCEPT, then releases that wrapper and restores
the retained ROTATE child to `(225,64,212,312)`. The contestant ID therefore
does not drive X reaction grouping; the accepted rating does.
The 500 ms Looks entry callback `0x00412944` is verified too: C41 is consumed
before its modal attempt, completion resumes inside the existing pavilion
without reloading controls or portraits, ROTATE is then constructed/selected
or retained hidden at frame zero, and `LOOKS.WAV` starts last. When C41 was
already consumed, the timer still waits 500 ms and runs that same continuation;
only the host call is skipped.
The catalogued `0x00412d8b` Looks bitmap-construction routine is now verified:
it loads LOOK, ACCEPT, PENALTY, ROTATE, the initial MAG/111, gauge 1, and five
portraits in recovered order. MAG/111 and gauge 1 are retained but initially
hidden; zero/manual-switch/Penalty blanking no longer destroys those owners,
and clicking the exposed presentation parent after magnification re-shows the
same stopped ROTATE child without a seek. Exhausted automatic selection also
leaves the four untouched portraits alone instead of reconstructing them and
creating spurious failure warnings. Its close helper `0x0041434a` is verified
for pending commit, audio stop, exact bitmap release order, and last-child
ROTATE stop/destruction. The disassembler-split `0x004129d3` painter is now
instruction-complete through its owning constructor row: fixed dirty-layer
order, unconditional cash and five portraits, conditional rating/selection/
hover work, and every one-shot flag clear are modelled without fabricating a
catalog row. In particular, the red selection frame is not a persistent retained
property; a later intersecting paint after `+0x70` is consumed can erase it.
The adjacent catalogued `0x00413361` rating loader and its disassembler-split
mouse-down/up/move bodies are now verified too. Focused coverage exhausts the
640×480 Judge-O-Matic partition, all exclusive control edges, cursor zones,
portrait-name strip, and all inside/outside release plans. The native handler
also preserves the original lack of mouse capture and separates logical action
codes from painted pixels. PENALTY retains both after an outside or wrong
release. ROTATE clears code 9 before its hit test, but its pre-test UpdateWindow
has no dirty backing/control layer, so the pressed pixels survive cancelled and
successfully dispatched turns alike.

The PE's pre-online MFC message maps have now been enumerated directly from the
24-byte `AFX_MSGMAP_ENTRY` arrays in `.rdata`, rather than inferred from which
handlers the disassembler happened to name:

| Map | Recovered owner | Gameplay-bearing messages |
|---|---|---|
| `0x00430100` / `0x00430150` | centered one-shot MCI child / owner | child left-down, owner timer, MCI notify, private `0x0405`, paint |
| `0x004303c8` | startup physical-CD gate | timer-driven drive scan; deliberately omitted |
| `0x00430958` | Cryo | left-down/up, right-double-click no-op, key-down, timer |
| `0x00430c00` / `0x004312b8` | Talent / Brains | left-down/up, move, key-down, MCI notify, timer |
| `0x00430f48` / `0x004310d0` | shared MCI wrapper / profile | wrapper lifecycle; profile left-down/up and paint |
| `0x00431598` / `0x004319f8` | navigation scene / hub | left/right-down, move, timer, MCI notify, private scene-switch messages |
| `0x00431bc8` | generic C/X/Simm media | left-down, paint, create, cursor, activation-change |
| `0x00431f78` / `0x004321e0` | ORDER / Weight | painted controls, move/release, timer; ORDER right-double-click |
| `0x00432648` / `0x004326b0` | Looks rotate child / pavilion | magnifier left-down, pavilion painted controls, move/release, timer |

This reconciliation and the corresponding CDialog vtable-slot audit exposed
five reachable routes previously absent or incorrect in the native host. The
centered child sends private message `0x0405` after any active
left-click stops its movie, completing intro/winner/credit with result zero;
the owner remains inert before that child exists, including after timer expiry
when `MCIWndCreateA` returned null. ORDER alone maps
`WM_RBUTTONDBLCLK` to `0x0040ff2d`, which immediately ends it with result zero;
the outer orchestrator ignores that result and enters navigation with any
unassigned comment-table words left unchanged. Profile OnOK/OnCancel are both
bare returns, so default Enter/Escape/close are inert. ORDER OnCancel confirms
and ends with result one, but its caller also ignores that result and continues
to navigation. Navigation-hub OnOK ends result-zero without confirmation, which
both callers route to the common save-and-exit tail. All five routes are now
implemented.

The audited dialog vtables point slot 31/32 to these OnOK/OnCancel families:

| Vtable | Owner | OnOK | OnCancel |
|---|---|---|---|
| `0x00430220` | centered movie | `0x004014d3`, active-media result zero | `0x0040152d`, no-op |
| `0x00430a98` / `0x00430d60` / `0x00431418` / `0x004327f0` | Cryo / Talent / Brains / Looks | no-op | quit confirmation, result one |
| `0x00431198` | profile | `0x00408616`, no-op | `0x00408617`, no-op |
| `0x00431ab0` | navigation hub | `0x0040cb12`, result zero | quit confirmation, result zero |
| `0x00431cb8` | generic C/X/Simm | inherited IDOK | completion-flag override |
| `0x00432088` | ORDER | `0x0040f516`, no-op | confirmation then result one; caller continues |
| `0x004322d8` | Weight | `0x004101ec`, no-op | confirmation then result one; caller exits |

The `WM_ACTIVATEAPP` entries are now reconciled too. Generic C/X/Simm closes on
either activation edge. Cryo/Talent/Brains rearm their painter flags on either
edge; ORDER reloads ten phrase DIBs, Weight recreates its three live gauges, and
Looks shows an existing ROTATE child when activation becomes true. The latter
three restore operations intentionally also work during the blank entry-timer
interval, matching `0x0040ff37`, `0x00410fd9`, and `0x0041449d`.

The `WM_SETCURSOR` family is now reconciled independently from mouse movement.
Centered one-shots, ORDER, and Weight force hand resource 166. Talent, Brains,
the navigation scene, and the Looks pavilion return handled without replacing
their current directional/exit cursor; the Looks ROTATE child explicitly sets
magnifier 181. The generic object's stored cursor is hand 166 for modes 4 and
6 but target/crosshair 162 for the modal navigation-Simm mode 3. The native
window procedure now preserves these routes and embeds 162 alongside the other
original cursors. Talent/Brains/Looks `WM_GETDLGCODE` returns
`DLGC_WANTALLKEYS`; direct native `WM_KEYDOWN` dispatch is equivalent because
the top-level loop does not use `IsDialogMessage`. Cryo/Talent/Brains palette
realization handlers are classified as platform equivalents of the native
32-bit decoded-bitmap compositor, which has no logical-palette ownership to
reestablish.

The shared legacy DIB support cluster at `0x004110ae`, `0x00411116`,
`0x004111c2`, `0x00411610`, and `0x004119ea` is now traced and classified as a
platform replacement: object initialization/destruction, movable-memory
allocation, BMP signature/read validation, and filename opening are handled by
native `HBITMAP`/filesystem loading. The per-screen failure branches are now
preserved by the owning native scene handlers: a failed required bitmap shows
`Can't read bitmap file!` with style `0x30` and the resource-`0xe000`
`Ms. Metaverse` caption. Dynamic Brains meters preserve the sole formatted
variant, `Can't read bitmap file 'bmp\Brain\%d.bmp'!`. Intentional Looks
magnifier level probes stay silent until the wrapped level-one retry also
fails, matching `0x0041426c-0x00414300`. The final failure still discards the
old magnifier, hides ROTATE, advances the counter, and synchronously repaints
the exact presentation rectangle.

The disassembler also promoted fourteen compiler unwind/destructor fragments
from `0x00403706` through `0x004038eb` to apparent functions. Instruction
tracing shows only local `CString` cleanup and MFC document/base destruction;
the native string, `Application`, and `GameRoundState` lifetimes replace them.
They are platform-runtime classifications, not fourteen missing gameplay
handlers.

The five remaining Cryo-only one-block functions are now classified as well:
`0x004046b7`, `0x004046c2`, and `0x004046ca` destroy the painter's temporary
`CString` values; `0x004046dc` cleans its local paint/DC resource; and
`0x004064ac` is the class scalar-destructor base thunk. Native strings, HDC/GDI
lifetime handling, and `Application` destruction replace them, so none is a
missing Cryo gameplay or media trigger.

Twenty additional one-block entries around `0x00401b03`, `0x004027d5`,
`0x0040291f`, and the game-document teardown have now been instruction-checked.
Nineteen merely load an owning function's stack-local `CString` address and
jump to the shared string destructor at `0x004255c1`; `0x00403ca6` is the
document's MFC scalar/base-destruction tail. They are native string/object
lifetime replacements, not omitted profile decisions, tally branches, media
triggers, or persistence functions.

The adjacent common exit tail at `0x00402627–0x004026c0` compares the selected
name against `GUEST` and `DEMO`, calls `0x00402edd` exactly once for every other
profile, and ignores its outcome. The native process now does the same after
its message loop: targeted MM.DAT failure is silent instead of producing the
former port-only save-error popup.

The same audit was applied to the ORDER/Weight boundary. ORDER thunks
`0x0040f9d3`, `0x0040f9eb`, `0x0040f9fd`, and `0x0040ffc4`, Weight thunks
`0x004106de` and `0x00411080`, plus legacy-DIB base tails `0x0041118f` and
`0x00412589`, contain only temporary string/archive/paint or scalar-destructor
cleanup. They are platform replacements. The encompassing Weight dialog family
at `0x0040ffcc` is instruction-verified, including its fixed popup setup,
500-ms C91 staging, cancel and pointer paths, and exact stateful dirty-layer
painter. Weight constructor `0x004106e6` is
instruction-complete: it loads the base, OK, three hidden `TH*_1` backing
wrappers, and six arrows in recovered order, with exact bitmap-derived
rectangles and warning-and-continue behavior. Its disassembler-split Weight
input bodies make six arrows mutate and play
`TT%d.WAV` on mouse-down, mouse-up clears their pressed state, moving outside
cancels that state without undoing the value, and OK commits only after its
pressed state survives to release. Press, release, and drag-out invalidate only
the exact affected rectangle, restoring a malformed simultaneous arrow/OK state
in control-before-OK order. The forced pressed-art and changed-gauge
repaints precede the value's audio trigger, and the three value-5 gauges remain
absent until the 500 ms C91 timer's modal host runner returns. The now-verified
`0x00410e3e` helper preserves its unusual ownership rule: zero hides but retains
the previous gauge wrapper, while nonzero destroys it before loading the exact
`th1`/`th2`/`th3` replacement and immediately repaints only that gauge rectangle.
The adjacent `0x00410f5b` teardown is verified through both result paths and
releases that retained wrapper in its original OK/arrows/gauges ownership order.
ORDER similarly
defers its ten phrase rows and initial hand until after C51 rather than showing
them under the host.

The old MFC painter is represented explicitly: it consumes base, OK, selected
arrow, and only the current gauge in order, preserving any noncurrent dirty
gauge flags. Focused tests cover combined and deferred flag states.

Talent `0x00407c38` and Brains `0x00409e76` are the byte-equivalent
performance-stop routines. They destroy the active 320×240 child, return its
stored-wrapper presence even after open failure, invalidate exactly
`(208,108)-(528,344)` without erase, and force the repaint. Their native shared
path now preserves every reachable branch and is `implemented-verified`.
Talent `0x00407cb7` and Brains `0x00409ef5` are the paired pavilion-close
routines: they commit all five dialog-local pending values before teardown,
stop ambient playback, release the palette replacement, ACCEPT, Gong, Penalty,
base, gauge, and five portraits in exact order, then destroy the current movie
last and return through navigation states two/three. The native close now owns
that sequence explicitly instead of deferring it to the generic scene reset,
so both are `implemented-verified`.
Twenty-four surrounding Talent/Brains/profile entries
were separately verified as duplicated MFC constructor/destructor, temporary
`CString`, brush, or paint-DC cleanup and are platform-runtime replacements.

The complete Looks/retired-online region through `0x00416083` has now been
instruction-checked. Looks `0x0041434a` is the pavilion-close routine: it copies
the five dialog-local pending values into the round document before it stops
ambient playback, releases ACCEPT/PENALTY/ROTATE-control, MAG, base, gauge, and
the five portraits in exact order, then stops/destroys the retained movie child
last and returns through navigation state four. The native close now performs
that complete sequence, so it is `implemented-verified`. Seventy-one other
entries in this range are MFC scalar/base destructors or compiler-generated
`CString`, socket-wrapper, paint, and unwind cleanup thunks and are classified
as platform replacements. The only additional semantic bodies are retired
online custom-sprite functions `0x0041561e` and `0x00415cbc`, now explicitly
excluded with the six already mapped Virtual Vegas service functions. No Looks
gameplay handler remains `unmapped` merely because the disassembler split out a
cleanup landing pad.

The remaining active offline region is now instruction-mapped too. Shared MCI
helpers `0x004080d7`, `0x0040a917`, and `0x0040c5e2` stop, seek, or range-play
AVI children and are replaced by the native decoded-video state machine. The
navigation loader `0x0040a43f`, bounded player `0x0040a966`, and transition
executor `0x0040aa61` are now instruction-verified as one connected cluster.
The loader covers all 30 real NAVIGATE entries and all 133 parsed scene
objects, exact initial `0x0400` cue/`0x0100` auto-route rules, per-DAT encounter
rearming, leading-zero silence, and the final CRT reseed position. The bounded
player gates embedded crossroads PCM to the
same inclusive frame range: opening/pausing and single-frame seeks are silent,
normal completion or Skip cuts the range before the destination WAV. It also
clears both positional-overlay DIBs at every range boundary; the reverse-play
branch is retained in the audit as dormant because all supplied DAT ranges are
ascending. Corpus decoding verifies 306 ordinary endpoints and XR1's sole
MCI stream-length boundary, which maps requested stop 1686 to final decoded
frame 1685. The transition executor preserves left-click/Skip interruption,
direction/range/link routing, ZPR suppression, zero-credit and 4.5-second Slots
paths, and XR1's one-shot animation. Crucially, the recovered popup constructor
zeros one-shot fields `+0x144/+0x148`; cross-scene wrapper `0x0040cb7d`
reconstructs that popup, so native cross-scene loads now re-arm frames
1391–1502 while same-popup revisits retain the held-frame path. That immediate
wrapper is now instruction-verified through its MCI stop, popup destruction,
extended-target normalization bypass, reconstruction, Hall-entry `+0x14c`
suppression, and target DAT/variant load. Its fade sibling at `0x0040cc1b`
also stops the range before executing exactly 199 palette passes; the native
32-bit compositor applies the recovered `+10` channel recurrence, including
the original green/blue saturation branches that accidentally write red,
before loading the next scene. The old loop had no clock and therefore remains
hardware-speed-dependent; native pacing only supplies an event-loop duration.
The
adjacent 141-byte pointer handler `0x0040af01` is verified separately across
all low-flag combinations and exact 239/240 and 319/320/321 boundaries,
including its active-MCI neutral-state priority and no-cursor-write neutral
path. Its split 500 ms timer/painter body and the post-transition cluster at
`0x0040b4df` are now instruction-verified together rather than being silently
folded into the small handler. That timer gates its strict `>1000` Simm test and later
strict `>10000` idle-movie test on the recovered 500 ms cadence, eliminating
the earlier 10 ms-pump trigger drift. The
MMS frame-update body at `0x0040d2ab` is instruction-verified as well. Its
delta escape is intentionally asymmetric: strict `x < -200`, `y < -200`, or
`x > 680`, with no upper-Y test. The native graph runner locks those exact
edges plus all ten reachable corpus flag values, the cursor-plus-four path
stream, mode-4 terminal hold, mode-5 result override, state-voice ordering,
and the original timed-transition write-both-coordinates-to-X typo.
The enclosing modal loop `0x0040e6f2` and its elapsed-time synchronizer
`0x0040eb52` are now instruction-verified too. The native event pump replaces
the MFC busy loop while retaining signed `timer_interval / 20`, zero-to-one,
all factor overrides (including dormant mode-5 root 10/linked 1), first-clock
capture, the unsigned 100,000 ms stale-clock guard, and mode-6's companion-WAV
sample clock. Mode-4 stalls accumulate only time strictly beyond 100 ms and
catch up only when that accumulator is strictly above 100; the recovered
single-precision `0.01f` conversion skips AVI frames without consuming MMS
coordinate points, resets directly to the loop frame on overflow, and clears
the accumulator. Focused tests cover those strict edges and stale-clock carry.
The owning disassembler-split loader at `0x0040d9cf`/`0x0040da9f` is now
instruction-verified across all supplied offline modes as well. Mode 6 builds
its synthetic `0x0088` record from C AVI length and HPNT coordinates; mode 4
selects X after reseeding and treats its companion WAV as an unchecked,
best-effort WaveMix open; mode 3 resolves the selected D/T pair, independent
state voice, target cursor, and ricochet channel. All source, buffer, child,
and sprite construction failures retain result-zero caller continuation. The
exact 108-record corpus proves no supplied MMS uses the auxiliary `0x0400`
background branch, and neither disc supplies the B-numbered movie needed by
the missing-T mode-5 fallback; those dormant paths cannot be selected by the
packaged asset set.
The
legacy audio cluster `0x0040ed92` through `0x0040f28d` parses RIFF/WAV chunks,
initializes three WaveMix channels, replaces per-channel waves, pumps playback,
activates/deactivates the mixer, and tears it down. It is classified as a
platform replacement by the bundled static PCM decoder and three independent
native audio players; this preserves the semantic separation between
navigation departure, response/effect, and per-record voice channels without a
runtime WaveMix dependency.

The post-intro ordinary-profile branch at `0x00401d63` is also restored: it
constructs the navigation wrapper at entry 13 and displays `HALL.DAT` before
Cryo. Hall action 13 jumps directly to first-round Cryo construction at
`0x004025e4`; it does not execute the later replay reset at `0x0040254c` and
therefore does not consume that reset's ten judge-cadet RNG calls. DEMO remains
the separate direct navigation-state-1 shortcut.

All formerly open navigation and bitmap entries through `0x00412121` are now
classified. The small entries are compiler-generated CString/MFC/GDI cleanup,
packed-point conversion, or destructor thunks. The larger shared DIB entries
perform bit-depth/palette conversion, offscreen backing capture, randomized
tile blits, or color-key compositing and are replaced by the 32-bit native
compositor. `0x00411c64` is reached only by the retired online transfer painter;
`0x00411759` has no recovered caller. Neither is an omitted offline gameplay
handler.

Finally, `0x004165df` is the last semantic body in the retired online path: it
converts a downloaded custom-sprite bitmap into the document buffer, notifies
the owner, and finalizes the remote selection. It is the ninth explicitly
excluded online-service function. `0x004170a2` through `0x00417186` are direct
VfW, WaveMix, and Winsock import trampolines; all recovered functions from
`0x004171a1` onward are linked MSVC MFC/CRT/platform support. Consequently every
one of the 1,046 recovered function entries now has a reviewed classification.
All active offline semantic functions are instruction-accounted; the remaining
differences are the explicit platform, bundled-media/CD-free, obsolete-online,
and undefined-behavior boundaries below rather than untraced function roots.

## Recovered top-level control flow

`fcn.00401b03` is the offline game orchestrator. Its verified setup order is:

1. Use the player already selected by outer call `0x00401a19`.
2. Install IDs 3, 4, 5, 8, and 10, ascending comment codes, and raw 5/5/5
   weights for every profile.
3. Play `MOV/INTRO/MMINTRO.AVI` synchronously (`0x00401bc1`).
4. For
   ordinary/GUEST sessions, Cryo replaces them with five sponsored contestants.
   `DEMO` with its required `BUYVV` password preserves them, prepares their
   pavilion variants, and jumps directly to navigation state 1.
5. Run category weights (`fcn.0040ffcc`, call at `0x0040209a`).
6. Run ORDER (`fcn.0040f2c4`, call at `0x00402107`).
7. Enter the `NAVIGATE.DAT` hub with state 1.
8. Dispatch hub return 2 to Talent, 3 to Brains, 4 to Looks, and 5 to tally.
9. After a pavilion closes, return to the hub with that pavilion's state.

This is not a linear scene list. The native flow now follows that order, drives
the hub from the original navigation table, resolves all automatic `0x0100`,
sound-bearing `0x0800`, and retired-dialog `0x2000` nodes through their common
middle route,
cross-scene `0x0200` nodes, `0x1000` fade-to-white scene switches, and `0x0008`
action exits, and no longer exposes generic stage-skipping keys. Extended
targets remain unchanged because both extracted asset roots are simultaneously
available; the original `VOL.DAT` swap/remap routine is deliberately bypassed.
All 35 destination-node WAVs now run at their original post-transition point
and only once per arrival, including each automatic hop; initial scene
construction separately accepts only a `0x0400` entry cue before resolving an
initial `0x0100` hop, while later arrivals accept `0x0400|0x0800`. The lone XR2
literal `0` asset is retained as a silent sentinel. The four `XR1.DAT` `0x0040`
nodes alternate their captured backing and N1-N4 positional overlays at the
recovered coordinates on the original backing-first 500 ms timer. XR1's
single `0x0010` object now mutes sound around its first long transition and
uses the original held-destination-frame shortcut on later passes; all five
`0x0020` arrivals reactivate navigation sound and hold `XRDS1F.AVI` frame 25.
After ten idle seconds on any of those `0x0020` nodes, the four-entry original
pool now plays XR1 object 19/20/21/22 middle ranges (870–1079, 1080–1303,
1304–1390, or 1503–1686), without changing the settled node, then returns to
frame 25 and rearms the ten-second timestamp. Its exact scaled CRT draw,
forward-wrap collision handling, and fourth-choice pool clear are retained.
All scaled gameplay draws now reproduce the executable's float
`floor(rand * range * 0x38000100)` rule rather than the usual integer
`rand * range / 32768`; regression seeds lock the reachable three-, five-, and
fifty-way boundary differences used by Slots and the shared selector.
The mode-4 X selector at `0x0040e8ae` is now instruction-verified as well. It
maps Judge-O-Matic values 4–7 to group 1, values 8 and above to group 2, and
values 3 and below (including an accepted zero) to group 3; those groups have
15/12/11 active X clips. It reads a newly zeroed 45-word
interleaved HNRD table on every selection, ignores open and short-read results,
clears only an exhausted active group, and walks forward to the float-scaled
ordinal zero. Focused tests lock the exact 38-draw sequence, maximum CRT output,
exhausted-group reset, missing/short/non-Boolean input, and raw persistence of
untouched malformed and inactive words. The original empty installed-root
shortcut is unreachable because the native packaged root is always configured;
no CD or registry lookup is reintroduced.
The separate `WAV/NAV/ZPR.WAV` departure channel now runs before valid manual
moves (including slot Up/self), with the original `0x0100`/`0x0800` suppression
mask and independent mixing.

`fcn.0040391f` is now mapped as the round-document initializer. Its name slots
establish contestant IDs 1–10 as Queen, Nancy, Suzi, Conchita, Jane, Dee,
Sammy, Rhonda, Jackie, and Domina; its adjacent cost slots establish
$100/$250/$1,000/$250/$150/$50/$50/$500/$50/$150. The native gameplay table
uses this exact pairing. The initializer also clears all five contestant IDs
and fifteen rating bytes, arms all fifteen pending-category fields, clears all
five disqualification fields, zeros the three category weights, cash, and
accumulated reward, performs the two Judge Cadet draws, and stores the default
comment mapping as `9,8,7,6,5,4,3,2,1,0`. The native document state and focused
constructor test retain each of those gameplay-observable defaults. The top
orchestrator then installs the five DEMO IDs and overwrites the comment table
with `0,1,2,3,4,5,6,7,8,9` before the intro exactly as the original does.
Ordinary sponsorship and ORDER replace those values; DEMO keeps both.

The tail makes two `GetPrivateProfileStringA` calls for `path1` and `path2` in
`mmsatan.ini`, both with a null application/section pointer and an empty default,
then retains the second non-empty result as its optional three-character media
root. That install override is deliberately replaced by configured packaged
`assets`/`assets2` roots. The 51-byte builder at `0x00403cbd` copies that legacy
root into a scratch buffer and writes the caller's relative name starting at
byte three; `0x00403cf0` selects the first- or second-disc root. Native
`std::filesystem` joins and scene/category root routing replace both helpers,
so no INI, registry, volume-label, or original-disc lookup is reintroduced.

## Verified media trigger map

The companion [AVI inventory and trigger audit](media-trigger-audit.md)
reconciles all 266 installed movies by disc and family. Its only inactive
installed movie is the movie-only debug artifact `MOV/HOST/X11DB.AVI`; all
other files belong to an active trigger family below.

| Original path | Trigger and ordering | Native status |
|---|---|---|
| `MOV/INTRO/MMINTRO.AVI` | One-shot startup movie after profile selection; Escape/close is a no-op; active-playback Enter/OK or a left-click on the created MCI child stops MCI and returns zero, continuing into entry-13 Hall/DEMO navigation; the owner has no left-click route during the initial 100 ms before child creation | Instruction-verified with the recovered post-login ordering, exact timer/create/200%-zoom command sequence, modal child boundaries, and blank/no-error-dialog failure state |
| `BMP/PASSWORD/PASSWORD.BMP` + `OK.BMP` | Borderless full-desktop-centered 640×480 login canvas; unlimited uppercase edit IDs 1002/1005 at `(205,208)` / `(205,295)` with `208×16` extents; OK press art and hit target at `(535,378)` | Instruction-verified with exact resource styles, artwork/load warnings, geometry, synchronous no-capture down/up repaint and release-inside submission, inert default commands, and recovered validation/message loop |
| Shared dialog resource 158 | Borderless `WS_POPUP` game canvas; exact 640×480 outer size centered by the recovered desktop-rectangle calculation | Implemented by the native host window; former 960×720 resizable frame removed |
| Shared AVI child setup `0x0040802f` | Bind owner/create the movie child, then set the visible caption to exactly `Ms. Metaverse` | Implemented; native scene/node/player/credit diagnostics no longer alter the caption |
| `MOV/CRYO/ROTATE.AVI` | Contestant-specific 40-frame block; ROTATE plays one persistent ten-frame quarter-turn per click, including the ID 7–10 wrap seek/play-to quirk | Implemented |
| `WAV/CRYO.WAV` on browse | Browse-left/right reissue the loop with flags `0x19` (`ASYNC | LOOP | NOSTOP`), preserving a current announcement or restoring ambience after one completes | Implemented from `0x004047d0` / `0x004048f1` |
| `WAV/TT0.WAV`–`TT5.WAV` → `MOV/CRYO/INFO.AVI` | On a detail-label click, stop the old segment, play its prompt synchronously with flags zero/default fallback, then seek and PlayTo the contestant-specific inclusive range from the 120-DWORD table at `0x004364a8` | Instruction-verified for all six handlers and all 60 ranges; each bounded range is synchronized to its sliced embedded-PCM cursor and all 120 endpoints are decoder-probed |
| `BMP/CRYO/BROWSL,BROWSR,ROTATE,INFO,SELECT.BMP` + six detail labels | Original control rectangles; mouse-down action IDs 1–11 select pressed art, then mouse-up synchronously restores unpressed art before committing only inside the same target | Implemented from `0x00405d67`/`0x00405fb7`; port-only hover behavior removed |
| Cryo money/cost text and cash rectangle | `$%d` green at `(305,129)` and `$%.2f` black at `(465,24)` in resource 158's transparent 8-point MS Sans Serif; Ctrl-click `(465,24)-(565,50)` adds $100 | Implemented from painter `0x0040405d` and mouse-down `0x00405d67` |
| `BMP/CRYO/GIRL%d.BMP` | After the quirked zero-based affordability look-ahead succeeds, debit cash/add the stake to reward, add the portrait at `x = 217 + 70 * selected_count` after the count increment—equivalently `x = 287 + 70 * zero_based_slot`—at `y = 412`, and force the cash/portrait repaint before first-sponsor C12; fifth-selection teardown stops CRYO.WAV before pavilion variant generation | Implemented |
| `MOV/BRAIN/BI%d.AVI` or `B%d%d.AVI` | The impostor slot uses `BI` only when the separate clue-pavilion selector is Brains; every other activation uses exactly one rotation-selected `B` movie | Implemented from `0x00409574`–`0x004095fd` |
| `MOV/TALENT/TI%d.AVI` or `T%d%d.AVI` | The impostor slot uses `TI` only when the separate clue-pavilion selector is Talent; every other activation uses exactly one rotation-selected `T` movie | Implemented from `0x00407343`–`0x004073c3` |
| `WAV/ANNOUNCE/NOCASH2.WAV` | A failed 10% Talent/Brains performance surcharge plays synchronously, clears the active contestant, preserves the chooser's prior numeric gauge without reconstructing the dialog, and returns silent; Looks has no surcharge path | Implemented from `0x00407266` / `0x004073f2` and the Brains twin, including the single unrounded x87 multiply-add, `-0.001f` comparison, and unsigned stored-float negative clamp |
| `MOV/LOOK/ROTATE.AVI` + `BMP/LOOK/MAG/%d%d%d.BMP` | Seek the contestant's 40-frame block; turn in exact 20-frame ranges using owner-blocking MCI Wait; body region selects/cycles a close-up | Implemented, including unchecked-command state toggling, missing-level wrap, and the non-reentrant input boundary |
| `BMP/LOOK/MAG/IM%d%d.BMP` | Substitute only when both the judge-cadet slot and its separately randomized clue-pavilion selector identify Looks, then only for the contestant-specific special region | Implemented for all ten table entries |
| `MOV/HOST/C11,C21,C31,C41,C51,C91.AVI` | Modal host entry at Cryo, Talent, Brains, Looks, ORDER, and weights; C51 begins after 2,000 ms and the other five after 500 ms; Cryo ambience starts before C11's timer while pavilion ambience starts after C21/C31/C41; Looks keeps its presentation area blank through C41 and only then constructs ROTATE/auto-selects; position comes from `HPNT.DAT`; every click ends the clip and only an opaque sprite hit adds synchronous `OUCH.WAV` | Implemented with traced timers, transparent AVI overlay, companion WAV, color-key hit testing, immediate click completion, and the exact `floor(wave samples * 10 / 11025) + 1` audio-master frame clock |
| `MOV/HOST/C12,C61,C71,C81.AVI` | First sponsorship (then resume looping `CRYO.WAV` after any synchronous `OUCH`); first eligible Talent/Brains gong and correct Penalty per pavilion-dialog visit; C81 only on a portrait-strip click after all five Brains results | Implemented, including per-entry C61/C71 rearming, active-performance suppression, and deferred C81 close |
| `WAV/TT10.WAV` → `TT1.WAV` in ORDER | Each assignment plays asynchronously; final `TT1` survives ORDER teardown and continues into the first navigation frame | Implemented from `0x0040f6f0`–`0x0040f74e` |
| `BMP/ORDER/COMMENT/*.BMP`, `HAND.BMP`, `ACCEPT.BMP`, `TH*.BMP` | Phrase art draws at 314×32 but uses exact 313×30 hit rectangles on a 40-pixel pitch; ACCEPT is 196×91, cancels on mouse-move outside, and commits on mouse-up whenever its press flag survives; right-double-click closes result-zero and continues with the partial mapping | Implemented from map `0x00431f78` and handlers `0x0040f35c`, `0x0040f538`, `0x0040f5ca`, `0x0040f607`, and `0x0040ff2d` |
| `BMP/WEIGHT/TH*`, six arrow bitmaps, `OK.BMP` | No gauges until C91 returns, then start at 5/5/5; arrows synchronously paint pressed art and the changed gauge before `TTn`; zero hides but retains the old gauge wrapper, nonzero replaces it; OK cancels on drag-out and commits whenever its press survives to mouse-up | Implemented from `0x00410087`, `0x00410152`, `0x0041020e`, verified helper `0x00410e3e`, `0x00410285`, `0x004103be`, and `0x0041043d` |
| `DAT/HOST/X*.MMS` + `MOV/HOST/X*.AVI` | Meter selection starts the ordered comment asynchronously; Talent/Brains ACCEPT replace the portrait with DIMMED before X, while Looks runs X before committing/dimming; then judging advances | Implemented for all 38 active X clips with legacy `hnrd.dat` state, exact SPR top-left X/Y placement, the loader's pre-selection `timeGetTime` CRT reseed, fast root-path cadence, timed-record clock capture, 100 ms phase through the terminal hold, fast path-tail cadence, and immediate graph-master teardown that stops the four longer WAV tails |
| `DAT/NAV/D*.MMS` + `MOV/NAV/T*.AVI` + `WAV/NAV/RICOCHET.WAV` | After one second at a previously unused `0x0080` navigation object, choose without replacement from D0 plus the five sponsored contestant IDs and run generic mode 3 modally; an outside-sprite click plays RICOCHET and stays modal, while a hit follows the MMS link and per-state channel-3 voice; `0x0080` reaction records ignore further clicks | Implemented with document-lifetime eligibility/count table, pool recycling, all flagged DAT trees, zero/50/100 terminal sentinels, exact 5–15%-of-sponsorship (D0 fixed-$250-base) caller awards, immediate `(500,20)` award display with delayed cash commit, exact SPR top-left placement, +16/+16 click adjustment, strict exclusion of all four sprite-border pixels, and top-left-key color testing, immediate reaction-entry frame seek/reposition, reaction-click suppression, per-record-entry 0x0040 voice starts (including D0's repeated sound index), hit/0x0020 stops, independent sprite-voice/session lifetime, byte-identical RICOCHET copies, exact select-before-loader-reseed ordering, credit/average-cost-derived movement pacing, 0x0080 factor-8 reaction pacing, and ZPR/idle-clock rearm on return or loader failure |
| `WAV/NAV/ZPR.WAV` | WaveMix channel 2 before a valid manual departure unless source flags include `0x0100` or `0x0800`; also precedes slot Up/self activation | Implemented on an independent PCM channel; both disc copies validated identical |
| DAT-selected `MOV/NAV/*.AVI` backgrounds | Scene construction opens, seeks, and pauses silently; bounded transitions play only their matching embedded-audio interval and cut it on completion, popup Skip, or any left-button down before a destination WAV; only XRDS1F/XRDS2F contain PCM | Implemented with cached 11,025 Hz mono crossroads tracks, inclusive frame-range slicing, absolute-frame synchronization to the live sliced-PCM cursor, and the `WM_LBUTTONDOWN` message-map interruption at `0x00431598`; all seven backgrounds are probe-classified |
| `MOV/NAV/XRDS1F.AVI` frames 870–1079, 1080–1303, 1304–1390, 1503–1686 | After 10,000 ms idle on an XR1 `0x0020` node, select objects 19–22 without replacement, play its middle range without moving, return to frame 25, and rearm the delay | Implemented with exact CRT scaling, used-entry forward wrap, fourth-choice clear, completion/Skip path, and probe-locked ranges |
| XR1/XR2 object 15 + `SLOTSPIN`, `SLOTLOSE`, `WINDING`, `WINBIG` + `BMP/SLOT/S*.BMP` | Up/self activation; play frames 812–869 or 821–878 for 4,500 ms while explicitly pumping queued Windows messages, stop the MCI movie including embedded audio, then draw the native-size result and updated balance in place; popup Skip or any left-button down aborts the notified range and takes that result path immediately | Implemented with exact node/range, pumped deadline, immediate left-click/Skip notify result, active-range neutral cursor, video/PCM stop boundary, wager/random branch, sounds, symbol sizes/positions, text/sign, and zero-credit behavior |
| `MOV/BRAIN/BP%d.AVI` | Correct Brains Penalty Box response, owner-blocking `Play From 0 Wait` before advancing | Implemented; Escape/close/pavilion input cannot truncate the response |
| `MOV/LOOK/LP%d.AVI` | Correct Looks Penalty Box response, owner-blocking `Play From 0 Wait` at `(225,64)` and 200×320 while the 212×312 ROTATE child is hidden | Implemented; Escape/close/pavilion input cannot truncate the response |
| `MOV/TALENT/TP%d.AVI` | Correct Talent Penalty Box response, owner-blocking `Play From 0 Wait` before advancing | Implemented; Escape/close/pavilion input cannot truncate the response |
| Disk II `MOV/TALLY/W%d.AVI` | Winning contestant movie selected immediately after `TALLY.DAT` object 3 action 5, except for a judge-cadet winner; the shared modal waits 100 ms before creating it, Escape/close is disabled, and active Enter/OK or child left-click skips into the finale | Instruction-verified for all ten contestant IDs; teardown clears the last frame and exposes black before replay prompt or `END2.WAV` |
| Disk II `WAV/ANNOUNCE/WRONG4.WAV` | Judge-cadet winner: synchronous announcement, remove her sponsorship stake, apply the original unsigned stored-float-bit clamp, and omit the winner movie | Implemented |
| Disk II `WAV/END2.WAV` → `MOV/CREDIT/CREDIT.AVI` | Below the original signed stored-float-bit comparison with $400, play `END2` synchronously then construct the shared modal; at or above it, a No answer goes directly there; it waits 100 ms before creating the credit movie, disables Escape/close, and lets active Enter/OK or child left-click exit | Implemented; style `0x0A` suppresses an open/decode error dialog and leaves a failed one-shot blank until its normal result-zero completion route runs |

The ordinary one-shot MCI families—intro, Talent/Brains presentations,
TP/BP/LP Penalty responses, winners, and credits—carry embedded PCM. Their
native renderer derives the target frame from the live PCM sample cursor and
seeks over delayed ticks. When PCM ends before the AVI, the remaining authored
visual tail resumes normal frame cadence (notably about one second for TP/BP).
This keeps MCI-style synchronization and video-notify completion timing instead
of either accumulating drift or truncating the silent tail.

The centered resource-158 class keeps its dialog-entry timer at all three call
sites: intro `0x00401bc1`, winner `0x0040240f`, and credit `0x00402493`. Each
instance centers and paints at 640×480, waits 100 ms on timer ID 1, and only
then kills the timer, allocates/creates its style-`0x0A` AVI child, sends zoom
200%, starts Play Notify, and marks it active. Only successful notification
code 1 completes. Playback notification and active OnOK stop/close/destroy that
child before the modal result reaches the outer controller.
The child's `WM_LBUTTONDOWN` at `0x004011d2` performs the same stop and sends
private owner message `0x0405`, whose `0x004014c5` handler completes result-zero;
the owner has no left-click mapping before the child is created. OnCancel is a
no-op. Pavilion performance and Penalty movies use different
handlers and do not inherit this delay.

Brains and Talent videos are composited at the original child-window rectangle
`(208,108,320,240)`, recovered at `0x00407682` and `0x004098b0`. Looks rotate
and Penalty children stretch their 200×320 AVI frames to the original
`(225,64,212,312)` child rectangle. Magnifier BMPs replace the child at
`(225,64)` using their native 211×313 dimensions.

Correct Penalty Box ordering in all three pavilions is
`REWARD1.WAV` → state/portrait mutation and forced repaint →
`PENALTY.WAV` → category response AVI
with owner-blocking MCI-Wait semantics → next contestant. Escape, caption close,
and pavilion pointer actions cannot truncate or re-enter that response. Wrong
accusations play `WRONG5.WAV` and
do not advance or restart pavilion ambience; Talent/Brains have already torn
down the active performance at that point, while Looks retains its current
presentation. Brains/Talent Gong plays
`GONG.WAV` synchronously before the first `C61` response and advancement, after
tearing down/repainting the active movie and forcing the new GONG portrait to
paint.
Normal Judge-O-Matic speech is dispatched asynchronously when
the meter value is selected; ACCEPT does not replay it and silently accepts
the initial zero when no band was selected (the Brains/Talent `> 0` guard is
the one-based active-contestant slot). ACCEPT/GONG/PENALTY/ROTATE show their ripped pressed
bitmap on mouse-down. Looks commits only on mouse-up inside the same coded
action rectangle; Talent/Brains use the original shared flag and dispatch the
wide rectangle under mouse-up, including cross-control drags. All three restore
the green hovered-contestant label over the bottom portrait strip;
Talent/Brains contestants are already DIMMED while X is visible, whereas Looks
retains its active portrait until X returns.

Talent and Brains retain their dialog resources through every contestant. A
successful surcharge resets the local number and visibly loads `0.BMP`; a
failed surcharge preserves both while NOCASH2 clears the
attempted active slot. ACCEPT, Gong, and correct Penalty replace only the
current portrait, clear the active slot before their modal media, preserve the
numeric gauge, and return to the same chooser without reloading background,
controls, gauge, or the other four portraits. Wrong Penalty is the exception:
it leaves the contestant active after the debit so judging can continue.

The wide-pavilion meter remains active on the empty chooser and formats
`TT0..TT10`; with a performance child open it first issues the recovered MCI
stop, whose notify tears down the child and restores pavilion ambience before
the response WAV starts. A direct ACCEPT press is disabled until that child is
gone, but the shared wide-control latch can be armed on Gong/Penalty and
released over ACCEPT. That recovered drag path runs X while retaining the
underlying performance, whose notify may arrive on either side of X completion.
Wide active-slot zero is silent. For Talent/Brains, the response WAV is started
before the dynamically selected rating bitmap is loaded and warning-checked;
only then is the exact stored gauge rectangle synchronously repainted. Brains'
dynamic warning includes the attempted `BMP\\BRAIN\\<rating>.BMP` filename,
including for rating zero.
Looks has its own lifecycle: entry and post-result handling automatically
highlight the first pending slot, portrait clicks may switch directly without
the wide-dialog warning, and its meter writes the highlighted slot immediately
(positive ratings speak, zero does not). Its automatic selector resets the
numeric value and hides the accepted gauge DIB; manual switches now perform the
same numeric and visible reset. Both paths seek the retained ROTATE child
without recreating it. Talent/Brains instead show the exact
modal `Please judge the previous contestant.` when another portrait is clicked
while a contestant is active; idle clicks on completed portraits are no-ops.

The round initializer has two independent impostor fields: `c4` is the
zero-based sponsored-contestant slot and `c8` is the one-based clue pavilion
(Brains 1, Talent 2, Looks 3). Penalty correctness and the invalid-winner branch
compare only the slot. Presentation media compare both fields. Consequently
`BI%d`, `TI%d`, and the Looks `IM` close-ups are mutually exclusive alternatives,
not introductions/clues that all play for the same impostor.

Talent/Brains performance playback uses MCI notify semantics. Normal completion
calls `0x0040672c` / `0x00408913`, destroys the 320x240 child, repaints its
exact `(208,108)-(528,344)` rectangle, and restores the category loop. Gong and Penalty capture the return
from their explicit teardown: C61/C71 is eligible only when that return is zero,
so an action that interrupts a still-running movie deliberately skips the first
host event. The native decoder now maintains the same active-versus-completed
lifecycle rather than retaining the last frame as an open movie.

The two 119-byte wide media dispatchers are instruction-complete. Their
forwarded tuples are Talent/Brains entry `(2|3,6,1)`, X `(0,4,rating)`, Gong
`(6,6,1)`, Penalty `(7,6,1)`, and Brains close `(8,6,1)`. Both stop ordinary
sound before generic construction and restart their category loop after the
wrapper is destroyed on every return. Gong/Penalty then repeat that restart in
their outer tail; C81 instead calls close, which immediately stops it.

The generic host runner clears the ordinary `sndPlaySound` channel before
dispatch and lets the owning scene restore its ambient loop afterward. A C
mode-6 clip takes its frame from the live companion-WAV sample cursor using the
recovered `floor(samples * 10 / 11025) + 1` mapping and returns when that audio
ends, including before a longer AVI's silent tail. If its visual ends first,
the last visual remains while the WAV finishes. An X mode-4 reaction continues pumping
an underlying Talent/Brains performance, allowing that movie's normal-completion
notify and ambient restart to occur while the host is still on screen. The
native runtime now gives host speech a separate statically linked PCM/WinMM
channel and preserves the same concurrent lifecycle.

Pavilion completion is input-driven. The Talent handler's all-zero test at
`0x00407200` and Brains handler's equivalent at `0x00409424` occur inside the
portrait-strip click loop. Completing the fifth result therefore returns to the
now-complete chooser and does not close immediately. A subsequent strip click
closes Talent; Brains dispatches `C81` and closes after that modal host object
returns. All three pavilions also close immediately from the independently
tested `x < 60 && y < 382` left-edge region; Looks has only that close path.

C21/C31/C41 entry suppression and C61/C71 event suppression have different
lifetimes. The former use the round document's `+0x234`, `+0x230`, and `+0x238`
flags and remain consumed after a pavilion dialog is destroyed. Talent/Brains
constructors initialize their local `+0x6c`/`+0x70` event flags on every entry,
so leaving an incomplete pavilion and returning rearms its first eligible Gong
and Penalty host events without replaying its entry clip. The replay reset at
`0x0040254c` also leaves those three document fields untouched, so C21/C31/C41
remain consumed in later rounds of the same process; newly constructed Cryo,
Weight, and ORDER dialogs replay C11/C91/C51 normally.
That reset also preserves the document's accumulated sponsorship pool,
category weights, and comment mapping until later dialogs overwrite the latter
two. Its judge-cadet draws occur inside the five-contestant clearing loop, so
five slot/category pairs are consumed and only the last pair remains.

## Global non-repeat state and excluded online transfer functions

`fcn.0040304e` builds the five Brains and five Talent media choices from
`NRFPAV.DAT`. The file is not profile data: it is an install-global, 130-byte
CRLF text table containing ten four-flag Brains lines followed by ten five-flag
Talent lines. Helpers `0x00416999` through `0x00416b9f` read the contestant's
line, choose uniformly among zero flags, retain the final choice across a cycle
reset, and rewrite that line immediately. The native port now uses the exact
`nrfPav.dat` filename and layout and saves selection state at the draw point,
including guest rounds. It reloads the table at round preparation and follows
the original per-contestant Brains-then-Talent in-place line-write order rather
than batching a full-table rewrite after all ten draws. The audit now also
covers ten pre-I/O `srand(time(0))` calls, unchecked raw byte-minus-`'0'` reads
across CR/LF and EOF, the all-nonzero variant-zero/no-rand branch, and ignored
stdio failures. `hnrd.dat` is likewise
global, is reopened for every X selection, and is saved as soon as the response
is selected; only `MM.DAT` profile records are skipped for `GUEST`. The
`0x0040e947` branch compares the installed root with the empty string; the
no-file fast path is therefore unreachable under the native configured-assets
contract and is not a CD check.

The similarly named `TLST.DAT` is unrelated. Startup reads its 256-byte bitmap
into online client state. `0x00414aee` starts Winsock 1.1, resolves
`ftp.virtualvegas.com`, creates the control/data sockets, and connects to FTP
port 21. `0x00414fc2` parses the FTP control/data response stream, while
`0x00415801` obtains the local socket address and formats active-mode `PORT`
commands used by the custom-sprite request path. `0x00415315` receives and
validates the remote BMP payload, then `0x00415b17` compacts the remote list and
updates the cache after transfer completion. `0x0041608b` hides the online
controls, composites `BMP/SOCK/ONLINE1.BMP` while servicing that transfer, and
restores the controls afterward. `0x0041561e` parses the remotely selected
custom-sprite path and sets up owner notification, while `0x00415cbc`
orchestrates the remote list fetch/cache path. `0x004165df` converts the
downloaded custom-sprite bitmap into the document buffer and finalizes the
selection. These nine functions depend on the retired
Virtual Vegas service and are explicitly classified `obsolete-online`; they are
not offline gameplay gaps and are not implemented by the native target.

## Verified offline coverage and explicit scope boundaries

The exhaustive ledger has no `implemented-partial` rows. These boundaries and
notable recovered quirks remain explicit:

- the recovered offline navigation branch matrix is implemented; exact legacy
  bitmap-load warning presentation is recovered for profile, Cryo, ORDER,
  Weight, judging, and final Looks magnifier failures. Deterministic media-open,
  decode, and malformed-data branches are now accounted; unsafe partial stack
  reads and null-dereference/allocation crashes use deterministic native
  failure behavior. The apparent state-5
  Simm-face cursor path is dead in the recovered offline binary—the only
  navigation writes to the cursor-state field are 0, 1, 3, and 4, while 5 is
  only compared, so resources 169/170 are loaded but never selected;
  backing-first 500 ms positional N1-N4 overlay alternation,
  destination-WAV timing,
  directional hover/click boundaries, original Up/Right/Left cursors, the
  500 ms 165/161 neutral-hand animation, exact right-click menu and shortcuts,
  its `+0x628` popup-used latch that suppresses delayed idle movies until the
  next transition, any-left-click active-MCI interruption, Skip Ahead state,
  DAT actions 2-6/13-16, and quit
  confirmation are recovered;
  all 60 `0x2900` automatic-route nodes are recovered, with only the obsolete
  FTP-backed dialog side effect of the two `0x2000` nodes bypassed;
  the 320×240 navigation movie is composed into the original 640×480 logical
  canvas, and the `0x1000` transition now stops the active range before applying
  the recovered 199-pass palette-to-white recurrence; the one-second `0x0080`
  Simm trigger is implemented;
- remaining active-button/cursor rectangles and original invalidation timing;
  pavilion ACCEPT/GONG/PENALTY/ROTATE pressed art and release semantics, exact
  Judge-O-Matic meter rectangles, Looks rotation/magnification hit rectangles,
  live green pavilion cash at `(465,25)`, exact spaced/non-spaced cash formats,
  white/red portrait selection frames, and green portrait-hover names,
  category-specific DIMMED timing and immediate GONG timing, dialog-local GONG versus persistent
  DIMMED/PENALTY reconstruction, and the choose-any-pending bottom-strip
  portrait loop are recovered; Talent/Brains shared-button mouse-up forces the
  original all-three-control synchronous restore before dispatch, while Looks
  forces it only for ROTATE; completed pavilions still construct on later
  hub visits, including per-dialog Brains C81 dispatch; normal cursor 166,
  pavilion-exit cursor 174,
  and Looks magnifier cursor 181 are now embedded and selected on the recovered
  regions; the 20-frame Looks turns and TP/BP/LP response helpers also retain
  their non-reentrant MCI-Wait owner-input boundary, while the distinct Slots
  deadline retains its recovered nested message pump;
- centered one-shot MCI failure and input presentation is recovered: `0x0040802f`
  receives style `0x0A` (`MCIWNDF_NOERRORDLG`), while `0x004013e0` sets the
  active flag after the play request. Failed intro/winner/credit media therefore
  stays blank and modal until a result-zero completion route runs. Enter/OK
  follows `0x004014d3`/`0x0040152e`; after child creation, left-down follows
  map `0x00430100`, `0x004011d2`, private `0x0405`, and `0x004014c5`. Both use
  intro's zero-result continuation at `0x00401bd3`; Penalty response failure
  still advances through its separate synchronous helper. Generic mode-3/4/6
  Cancel is traced
  through its overridden OnCancel at `0x0040e6e0` (result zero), while
  `0x0040ef28` is separately the generic `WM_ACTIVATEAPP` route, ignores its
  `bActive` argument, and tears down on either activation state (also result
  zero); both block parent
  menu/release input, discard a pending Simm award under the caller's `>10`
  test, and still execute the caller's normal ZPR/advance/ambient tail;
- shared Talent/Brains performance open failure is recovered separately from
  centered media: each caller stores its allocated wrapper before
  `0x0040802f`, ignores the helper result, and treats the wrapper as the active
  child even without an open decoder. The native empty child now remains black,
  blocks ACCEPT, and makes meter/Gong/Penalty teardown return true exactly like
  `0x00407c38` / `0x00409e76`;
- Talent `0x0040760a`, Brains `0x00409844`, and their shared creator
  `0x0040802f` are instruction-verified as one creation cluster: destroy the
  prior wrapper, store its replacement before open, bind the owner, create with
  style `0x4000000b`, set the exact caption, ignore open failure at the outer
  caller, move to `(208,108)` at `320x240`, and show with command 5. The native
  decoder retains the same blank-wrapper lifetime while replacing only the VfW
  child-window plumbing;
- Talent `0x004076a2` and Brains `0x004098d0` are instruction-verified as the
  paired wide ACCEPT bodies. DIMMED load/warning precedes the score and local
  pending writes; only the current portrait and `(69,63)-(131,281)` gauge are
  synchronously repainted; the active slot is cleared before exact
  `(0,4,rating)` X dispatch; and a drag-released ACCEPT preserves any underlying
  performance child until its independent completion notification;
- Talent `0x00407a64` and Brains `0x00409c9e` are instruction-verified Gong
  twins. They tear down the movie first, load/warn the current GONG portrait
  before pending/zero-score mutation, repaint only portrait plus gauge, clear
  the active slot before synchronous GONG, gate first C61 on the teardown return,
  and retain the dialog/gauge through the outer ambient restart;
- Talent `0x004077d5` and Brains `0x00409a03` are instruction-verified Penalty
  twins. Both queue `(465,24)-(600,40)` before their synchronous announcement;
  WRONG5 then precedes the unrounded debit and retains the active contestant,
  while REWARD1 precedes refund, PENALTY load/warning, local pending and zero
  score, exact repaint, post-paint disqualification, PENALTY WAV, synchronous
  TP/BP response, conditional C71, and the in-place ambient return;
- startup profile artwork, field/OK geometry, press/release path,
  validation/error loop, and ignored dialog-creation/modal-failure result are
  recovered; normal Enter/Escape/close remain inside the dialog through its
  bare-return OnOK/OnCancel. Missing `MM.DAT` is created empty before the
  dialog. Its complete 28-byte records are scanned without database-wide
  validation, so an unrelated malformed record or non-finite balance no longer
  forces guest mode. Raw balance bits remain unchanged until the first matching
  named record authenticates, at which point only that selected player receives
  the original signed-integer-bit `$400` floor; valid DEMO/BUYVV still scans
  MM.DAT and can restore a stored DEMO balance even though DEMO remains excluded
  from final save. Matching and targeted persistence both apply `Left(10)` but
  retain the complete submitted edit strings during play. The retired
  VVEGAS.INI/VVT branch and mutable online TLST cache are classified
  `obsolete-online`; MM.EXE's unsafe nonzero partial-record stack read is
  replaced by deterministic ignored-tail loading and preserve-then-append
  saving;
- no unclassified or partially mapped recovered functions remain.

These are tracked as scope or safety replacements rather than being hidden
behind a broad claim that an asset or native equivalent is automatically exact.

The native runtime intentionally performs no physical-CD, volume-label, drive,
`VOL.DAT`, or copy-protection check. It reads only its configured or packaged
extracted asset directories. Original entries 21–24 are loaded directly, never
remapped to the decline-path entries 25–28, and the external-game disc prompts
at entries 14/15 are not ported. The Disk-I insert loop at `0x00403355`, mounted-
disc validator at `0x0040c552`, and swap/remapper at `0x0040cecb` are all
explicitly classified as bundled-media replacements rather than open porting
work. The earlier top-level check was split out of the function inventory by
the disassembler: `0x00401818–0x00401a38` scans drive letters with
`GetDriveType`, looks for `X:\\vol.dat`, and refuses startup when no CD-ROM is
found. Its owning dialog scaffold at `0x00401690` is now explicitly marked as a
bundled-media replacement too; none of that split handler is reproduced.

The preceding MFC application scaffold is similarly split around
`0x00401000`. Its `InitInstance` body contains a dormant `hPrevInstance`
warning (`You are already running Ms. Metaverse`) and requires an 8-bit/256-
color display before constructing the CD-scanning dialog. Win32 always passes
`hPrevInstance == NULL`, and the function has no mutex/window-enumeration
single-instance mechanism, so the warning is not an active execution guard.
The native WinMain and 32-bit compositor replace that platform bootstrap:
modern color depths are accepted, multiple processes are not newly prohibited,
and no legacy MFC lifetime thunk remains gameplay work.

The same replacement policy now covers the installer-path functions at
`0x004027d5`, `0x00403cbd`, `0x00403cf0`, `0x0040c4a1`, and `0x0040ece1`. The original used
the `mmpath` registry value as a base for relative media names and made
`0x004033cd` synthesize a legacy `wavemix.ini`. The native port uses explicit
`assets`/`assets2` filesystem joins and decoded audio playback instead. None of
the old installation checks or prompts execute.

The navigation command cluster split across `0x0040c0fe`/`0x0040c12a` is now
instruction-accounted. The visible popup retains IDs 200, 201, 202, 204, 205,
and 206; the handler also accepts the original hidden ID 203 and forwards tally
action 5 without adding a menu item. Talent/Brains/Looks map to actions 2/3/4,
Slots selects NAVIGATE entry 29 on the primary tree or 30 on the secondary,
Exit uses the exact `0x24` confirmation, and Skip preserves both active-range
and stale-enabled settled-state behavior. Only the intervening CD-number/MCI
product query, root swap, and `VOL.DAT` retry are omitted because both packaged
asset roots are concurrently available.
