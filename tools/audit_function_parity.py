#!/usr/bin/env python3
"""Build an exhaustive, reproducible MM.EXE function-parity ledger.

The disassembly artifacts are deliberately ignored by git, so this tool turns
their raw radare2 JSON into a stable TSV that can be regenerated after every
analysis pass.  A function is never called "ported" merely because its screen
exists: the manual address map below only records behavior that has actually
been traced. Every recovered entry now has a reviewed classification. An
application handler may be classified as partial until its internal branches,
media timing, paint behavior, and failure paths are verified; the current
reviewed map has no such rows. The `implemented-verified` status is reserved for
a complete instruction trace, native comparison, and focused coverage of every
reachable semantic branch.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any


RESOURCE_RE = re.compile(
    r"(?:^|[\\/])(?:BMP|DAT|MOV|WAV|MMS)(?:[\\/]|$)|\.(?:AVI|BMP|DAT|MMS|WAV)$",
    re.IGNORECASE,
)


# Only addresses whose behavior has been traced belong here. Status remains
# intentionally conservative; `implemented-verified` requires exhaustive
# branch/mutation comparison rather than the presence of a broad equivalent.
TRACED_FUNCTIONS: dict[int, tuple[str, str]] = {
    0x00401000: ("replaced-platform-runtime", "global MFC app scaffold; dormant hPrevInstance warning and obsolete 8-bit-display gate replaced by native WinMain and 32-bit rendering"),
    0x00401143: ("replaced-platform-runtime", "global MFC app destruction thunk replaced by native process lifetime"),
    0x004011BD: ("replaced-platform-runtime", "MFC application base-destruction thunk replaced by native process lifetime"),
    0x004012B5: ("implemented-verified", "instruction-complete resource-158 centered one-shot family: exact Ms. Metaverse caption, desktop-centered 640x480 black owner, resource-166 cursor, timer 1 at 100ms, kill/allocate/MCIWndCreate style-0x0a/zoom-200/Play-Notify/active ordering, child-map 0x00430100 active stop plus private-0x0405 result-zero click versus inactive replay, create-failure blank mouse-inert owner, successful-only notify completion, active/inactive OnOK split, and no-op OnCancel; native full-stream probes cover MMINTRO, W1-W10, and CREDIT dimensions and audio-to-visual tails"),
    0x0040152E: ("implemented-verified", "instruction-complete centered one-shot completion: close the media device, destroy its wrapper, then return the supplied modal result; every native intro/winner/credit completion route resets the decoder/audio/bitmap before its distinct continuation, including a forced black repaint before the winner replay prompt or synchronous END2 cue"),
    0x004015C5: ("replaced-platform-runtime", "intro paint-stack cleanup thunk replaced by native window painting"),
    0x00401625: ("replaced-platform-runtime", "intro dialog base-destruction thunk replaced by native object lifetime"),
    0x00401685: ("replaced-platform-runtime", "intro media-child destruction thunk replaced by native decoder lifetime"),
    0x00401690: ("replaced-bundled-media", "top-level dialog scaffold and disassembler-split 0x401818 CD-ROM/vol.dat bootstrap replaced by configured packaged assets"),
    0x00401753: ("replaced-platform-runtime", "top-level MFC dialog/string destruction replaced by native object lifetime"),
    0x004017B6: ("replaced-platform-runtime", "top-level dialog CString cleanup thunk"),
    0x004017C1: ("replaced-platform-runtime", "top-level dialog CString cleanup thunk"),
    0x004017CC: ("replaced-platform-runtime", "top-level dialog CString cleanup thunk"),
    0x004017E1: ("replaced-platform-runtime", "top-level dialog base-destruction thunk"),
    0x00401A76: ("replaced-platform-runtime", "top-level bootstrap local-string cleanup thunk"),
    0x00401AFB: ("replaced-platform-runtime", "top-level paint-stack cleanup thunk"),
    0x00401B03: ("implemented-verified", "instruction-complete active offline orchestrator: install pre-intro IDs 3/4/5/8/10, ascending 0..9 comment table, and raw 5/5/5 weights; centered MMINTRO result gate; ordinary entry-13 Hall to Cryo versus DEMO variant preparation/state-1 shortcut; exact Cryo teardown before ten pavilion draws, Weight then ORDER, stateful navigation actions 2-6 and Hall 13-16 with retired external-game CD loops replaced by packaged offline returns; TALLY judge-cadet WRONG4/stake clamp or winner movie, signed-bit $400 replay decision, Disk-II END2/credit finale, CD-free replay reset preserving reward/weights/comments and consuming five cadet pairs, and the common GUEST/DEMO-excluding silent save tail"),
    0x00402697: ("replaced-platform-runtime", "top-level orchestrator local CString cleanup thunk"),
    0x004026D3: ("replaced-platform-runtime", "top-level orchestrator local CString cleanup thunk"),
    0x004026DE: ("replaced-platform-runtime", "top-level orchestrator local CString cleanup thunk"),
    0x004026F3: ("replaced-platform-runtime", "top-level orchestrator local CString cleanup thunk"),
    0x004027CD: ("replaced-platform-runtime", "top-level close/save path local CString cleanup thunk"),
    0x004027D5: ("replaced-bundled-media", "installer registry mmpath lookup and wavemix.ini bootstrap replaced by configured packaged asset roots and native audio"),
    0x004028FD: ("replaced-platform-runtime", "install/bootstrap local CString cleanup thunk"),
    0x00402905: ("replaced-platform-runtime", "install/bootstrap local CString cleanup thunk"),
    0x00402917: ("replaced-platform-runtime", "install/bootstrap local CString cleanup thunk"),
    0x0040291F: ("implemented-verified", "instruction-complete shipped offline login path: create absent MM.DAT, ignore DoModal creation result while Enter/Escape/close remain inert, exact warning order, GUEST rule, DEMO/BUYVV before the first-match scan, Left(10) authentication with full submitted edit strings retained, tolerant 28-byte records, unmatched $500 start, signed-raw-bit $400 returning floor, and deterministic safe handling of the original undefined partial stack tail; the VVEGAS.INI-selected 26-byte VVT and mutable TLST service state are explicitly obsolete-online rather than active offline branches"),
    0x004029E5: ("replaced-platform-runtime", "profile-load local CString cleanup thunk"),
    0x004029F0: ("replaced-platform-runtime", "profile-load local CString cleanup thunk"),
    0x00402D68: ("replaced-platform-runtime", "profile validation local CString cleanup thunk"),
    0x00402D70: ("replaced-platform-runtime", "profile validation local CString cleanup thunk"),
    0x00402E92: ("replaced-platform-runtime", "profile validation local CString cleanup thunk"),
    0x00402E9A: ("replaced-platform-runtime", "profile validation local CString cleanup thunk"),
    0x00402EA2: ("replaced-platform-runtime", "profile validation local CString cleanup thunk"),
    0x00402EAA: ("replaced-platform-runtime", "profile-load path CString cleanup thunk"),
    0x00402EB5: ("replaced-platform-runtime", "profile-load path CString cleanup thunk"),
    0x00402EC0: ("replaced-platform-runtime", "profile-load path CString cleanup thunk"),
    0x00402ED5: ("replaced-platform-runtime", "profile-load local CString cleanup thunk"),
    0x00402EDD: ("implemented-verified", "instruction-complete shipped offline save path: materialize Left(10) name/password, scan 28-byte records for the first matching pair, seek back and overwrite it or append at EOF, preserve a damaged trailing fragment before deterministic append, write name[11]/password[11]/padding[2]/raw-float[4], and return silently on open/update failure because the caller ignores the outcome; the VVEGAS-selected 26-byte VVT service branch is classified obsolete-online"),
    0x0040304E: ("implemented-verified", "instruction-complete round variant preparation: construct/seed five Brains then five Talent non-repeat objects before file I/O, reopen only the current contestant's two NRFPAV lines, draw Brains then Talent with the shared CRT stream, store one-based results including malformed-state variant zero, and immediately attempt Brains then Talent r+ rewrites with all I/O failures ignored before advancing"),
    0x004031D0: ("implemented-verified", "instruction-complete weighted tally: five-slot byte-rating/integer-weight score order, zero-initialized winner/maximum, strict-greater earliest tie, disqualified-contestant exclusion plus three pending-field clears, all-complete detection, signed stored-float-bit >500 reward selection, and incomplete-round 27.0f additions individually load/round/store for every cleared contestant/category field in original order"),
    0x00403355: ("replaced-bundled-media", "Disk-I insert/VOL.DAT retry loop intentionally omitted for bundled assets"),
    0x004033CD: ("replaced-platform-runtime", "wavemix.ini synthesis/bootstrap replaced by the native decoded-audio and WinMM output path"),
    0x00403706: ("replaced-platform-runtime", "wavemix bootstrap exception-unwind local cleanup thunk"),
    0x0040370F: ("replaced-platform-runtime", "wavemix bootstrap local CString cleanup thunk"),
    0x0040371B: ("replaced-platform-runtime", "wavemix bootstrap local CString cleanup thunk"),
    0x00403727: ("replaced-platform-runtime", "wavemix bootstrap local CString cleanup thunk"),
    0x00403733: ("replaced-platform-runtime", "wavemix bootstrap local CString cleanup thunk"),
    0x0040373C: ("replaced-platform-runtime", "wavemix bootstrap local CString cleanup thunk"),
    0x00403745: ("replaced-platform-runtime", "wavemix bootstrap local CString cleanup thunk"),
    0x0040374E: ("replaced-platform-runtime", "wavemix bootstrap local CString cleanup thunk"),
    0x00403757: ("replaced-platform-runtime", "wavemix bootstrap local CString cleanup thunk"),
    0x00403778: ("replaced-platform-runtime", "legacy CString destruction trampoline replaced by native string lifetime"),
    0x00403809: ("replaced-platform-runtime", "MFC document destruction cleanup tail"),
    0x0040386F: ("replaced-platform-runtime", "MFC document scalar-destruction cleanup tail"),
    0x00403895: ("replaced-platform-runtime", "game-document MFC base destructor replaced by native Application/GameRoundState lifetime"),
    0x004038EB: ("replaced-platform-runtime", "game-document destruction CString cleanup tail"),
    0x0040391F: ("implemented-verified", "instruction-accounted round-document initialization: five zero contestant IDs, zero rating bytes, three pending flags plus two per-slot one fields, clear disqualification, three document entry flags, zero weights/credits/accumulator, two legacy rand draws, exact contestant names/costs, and reverse 9..0 comment mapping; MFC construction and the optional mmsatan.ini install-root override are deliberately replaced by native lifetime and configured packaged roots"),
    0x00403C2D: ("replaced-platform-runtime", "game-document owned-child/string/base destructor replaced by native RAII lifetime"),
    0x00403C8E: ("replaced-platform-runtime", "game-document installed-root CString cleanup tail replaced by native path lifetime"),
    0x00403CA6: ("replaced-platform-runtime", "game-document MFC scalar/base-destruction tail replaced by native GameRoundState lifetime"),
    0x00403CBD: ("replaced-bundled-media", "legacy three-character installed-root scratch composer replaced by filesystem joins against the configured packaged assets/assets2 roots"),
    0x00403CF0: ("replaced-bundled-media", "copy selected path1/path2 install root into active legacy media path replaced by explicit assets/assets2 roots"),
    0x00403D56: ("replaced-platform-runtime", "shared MFC dialog-resource construction and game-document owner binding replaced by native scene lifetime"),
    0x00403FE7: ("implemented-verified", "instruction-complete Cryo C-host helper shared by entry C11 and first-sponsor C12: stop the shared legacy channel, allocate the generic modal runner, dispatch channel 1 take 1/2 in media mode 6 with the active document root, destroy the runner after its audio-clocked modal result, and unconditionally restart CRYO.WAV with flags 0x09 on success or media failure; the native asynchronous decoder holds owner input and defers the caller continuation across the equivalent modal boundary, with exact five-operation and cue coverage"),
    0x004046B7: ("replaced-platform-runtime", "Cryo painter temporary CString cleanup thunk after the first credits TextOut"),
    0x004046C2: ("replaced-platform-runtime", "Cryo painter temporary CString cleanup thunk after the refreshed credits TextOut"),
    0x004046CA: ("replaced-platform-runtime", "Cryo painter temporary CString cleanup thunk after the candidate-cost TextOut"),
    0x004046DC: ("replaced-platform-runtime", "Cryo painter local paint-DC/resource cleanup thunk replaced by native HDC/GDI lifetime handling"),
    0x00404714: ("implemented-verified", "instruction-complete Cryo browse-left handler: one-based 1-to-10 wrap, visible INFO stop/name-bio seek/show before unconditional ROTATE stop/front seek, CRYO.WAV flags 0x19 without default suppression, stored rotation marker, exact cost dirty rectangle, and synchronous repaint; all ten contestant states and both INFO states are covered"),
    0x0040483B: ("implemented-verified", "instruction-complete Cryo browse-right handler: one-based 1-to-10 wrap, visible INFO stop/name-bio seek/show before unconditional ROTATE stop/front seek, CRYO.WAV flags 0x19 without default suppression, stored rotation marker, exact cost dirty rectangle, and synchronous repaint; all ten contestant states and both INFO states are covered"),
    0x00404962: ("implemented-verified", "instruction-complete Cryo ROTATE click: every ten-frame quarter-turn, persistent next marker, inclusive PlayTo endpoint, ordinary IDs 1-6 wrap, and the original IDs 7-10 reverse seek/play-to quirk; all forty normal ranges and ten wraps are covered and all 80 endpoint frames decode"),
    0x004049EC: ("implemented-verified", "instruction-complete Cryo INFO toggle: exact boolean inversion, stop/hide plus ROTATE dirty path, or contestant NAME/BIO seek plus INFO dirty/show path, followed by the exact information-panel repaint; both paths for all ten contestants are covered"),
    0x00404A93: ("implemented-verified", "instruction-complete Cryo sponsorship: duplicate modal before cash mutation, prospective debit, original zero-based affordability order compared against one-based selected IDs, synchronous NOCASH1 flags-zero rejection, accumulated-reward credit, cash repaint, roster insertion and portrait exposure before first C12, unconditional CRYO restart after C12, and fifth-selection close; every reachable roster/candidate float boundary is covered"),
    0x00404C85: ("implemented-verified", "instruction-complete Cryo Name/Bio handler: stop INFO, synchronously play TT0 with flags zero/default fallback, index one-based contestant record*0x30 at 0x004364a8, seek pair 0, then PlayTo its inclusive end; all ten pairs and both endpoint frames are tested"),
    0x00404CF5: ("implemented-verified", "instruction-complete Cryo Ambitions handler: stop INFO, synchronously play TT1 with flags zero/default fallback, index one-based contestant record*0x30 at 0x004364a8, seek pair 1, then PlayTo its inclusive end; all ten pairs and both endpoint frames are tested"),
    0x00404D65: ("implemented-verified", "instruction-complete Cryo Turn Ons handler: stop INFO, synchronously play TT2 with flags zero/default fallback, index one-based contestant record*0x30 at 0x004364a8, seek pair 2, then PlayTo its inclusive end; all ten pairs and both endpoint frames are tested"),
    0x00404DD5: ("implemented-verified", "instruction-complete Cryo Turn Offs handler: stop INFO, synchronously play TT3 with flags zero/default fallback, index one-based contestant record*0x30 at 0x004364a8, seek pair 3, then PlayTo its inclusive end; all ten pairs and both endpoint frames are tested"),
    0x00404E45: ("implemented-verified", "instruction-complete Cryo Ideal Man handler: stop INFO, synchronously play TT4 with flags zero/default fallback, index one-based contestant record*0x30 at 0x004364a8, seek pair 4, then PlayTo its inclusive end; all ten pairs and both endpoint frames are tested"),
    0x00404EB5: ("implemented-verified", "instruction-complete Cryo Favorite Quote handler: stop INFO, synchronously play TT5 with flags zero/default fallback, index one-based contestant record*0x30 at 0x004364a8, seek pair 5, then PlayTo its inclusive end; all ten pairs and both endpoint frames are tested"),
    0x00404F25: ("implemented-verified", "instruction-complete Cryo ROTATE child construction: retained logical wrapper, configured-root MOV\\CRYO\\ROTATE.AVI, style 0x0400000a, width minus three at 258,142, document-owner binding, seek frame zero, show, and zero rotation marker; child plan and all 80 reachable quarter-turn endpoint frames are covered"),
    0x00405032: ("implemented-verified", "instruction-complete Cryo INFO child construction: retained logical wrapper, configured-root MOV\\CRYO\\INFO.AVI, style 0x0400000a, native size at 464,84, document-owner binding, seek frame zero, and initially hidden state; child plan plus all 60 bounded playback ranges and 120 endpoints are covered"),
    0x00405121: ("implemented-verified", "instruction-complete sponsored-portrait insertion: replace the prior logical DIB owner only after roster insertion, format/load current GIRL ID with style-0x30 warning on failure, compute x=217+70*selected-count at y=412, store the exact dirty rectangle, and synchronously expose it; all ten IDs across all five slots are covered"),
    0x0040521D: ("implemented-verified", "instruction-complete Cryo static constructor: allocate/load CRYO then synchronously expose its base-only repaint before INFOBK, BUTTONS, MONEY, BROWSL..SELECT, and NAMEBIO..FAVORITE in exact order; retain exact source dimensions and placement-derived hit rectangles; issue style-0x30 generic warnings independently and continue after every missing bitmap while collapsing only the failed highlight's target; initialize both panel dirty rectangles and hand off to the split mouse map; all fifteen plans, dimensions, availability branches, and package files are covered"),
    0x00405D5C: ("implemented-verified", "instruction-complete disassembler-split Cryo mouse handlers at 0x00405d67/0x00405fb7: exact half-open rectangles and action IDs 1-11, retained old action after an outside second down, visible-INFO gating, independent Ctrl+cash $100 mutation, synchronous pressed/unpressed paint order, same-target release dispatch, and detail-panel repaint after dispatch or cancel; every rectangle edge and release path is covered"),
    0x0040621F: ("implemented-verified", "instruction-complete Cryo dialog teardown: close +0x210 ROTATE child then +0x20c INFO child and its embedded PCM, stop the shared legacy channel, delete BROWSL..SELECT then NAMEBIO..FAVQUOTE, backing/base/BUTTONS/latest portrait/INFOBK/MONEY in exact order, and return the supplied dialog result before outer variant generation; native retained-pixel portrait handles share the original portrait-wrapper boundary and the full 21-operation sequence is regression-covered"),
    0x004064AC: ("replaced-platform-runtime", "Cryo scalar-destructor MFC base cleanup thunk replaced by native Application lifetime"),
    0x004064BC: ("implemented-verified", "instruction-complete Talent constructor/OnInit/timer/painter split: resource-158 geometry and five rectangles, copied-and-disqualification-masked pending scores, exact zero/one field initialization, centered 640x480 canvas, post-construction 500 ms timer, consume-C21-before-dispatch or delayed direct TALENT.WAV start, conditional base/cash/gauge layers, all portraits and white frames, one-paint +0x64 red frame, shared-press triple overlay, exact hover name, cursor preservation, and WM_ACTIVATEAPP base/cash/gauge rearm"),
    0x004066C0: ("replaced-platform-runtime", "Talent painter local paint-context cleanup thunk"),
    0x00406971: ("replaced-platform-runtime", "Talent painter temporary credits CString cleanup thunk"),
    0x00406AAD: ("replaced-platform-runtime", "Talent painter temporary CString cleanup thunk"),
    0x00406AB5: ("replaced-platform-runtime", "Talent painter temporary GDI brush cleanup thunk"),
    0x00406ABD: ("replaced-platform-runtime", "Talent painter temporary GDI brush cleanup thunk"),
    0x00406ACF: ("replaced-platform-runtime", "Talent painter local paint-DC/resource cleanup thunk"),
    0x00406ADA: ("implemented-verified", "instruction-complete Talent resource-construction split: base, palette-equivalent, ACCEPT, Gong, Penalty, rating 0, then five portrait wrappers in exact order; recovered paths, warning-and-continue behavior, bitmap-derived action rectangles, exact rating bounds (69,63)-(131,281), and portrait geometry (287+70*n,412) are retained"),
    0x00406F77: ("implemented-verified", "instruction-complete Talent dynamic-rating and split mouse cluster: direct exit, exact meter formula/bounds, performance stop before speech, WAV-before-DIB load and exact gauge repaint, idle TT0..TT10 versus active positive contestant comments, filename-generic warning, shared press with release-target cross-drags, choose-any-pending activation, single-chain x87 surcharge, exact cash/portrait/gauge repaint ordering, player-verified visible zero-gauge reset on success, NOCASH2 preservation, exact hover strip, all-results strip close, and strict cursor regions"),
    0x0040760A: ("implemented-verified", "instruction-complete Talent performance child creation: destroy the prior wrapper, allocate/store the replacement before open, resolve the relative movie root, call shared creator with style 0x4000000b, ignore its result, move to (208,108) at 320x240, show with command 5, and return one; BI/TI are exclusive clue-pavilion alternatives, and failed open retains blank wrapper ownership"),
    0x004076A2: ("implemented-verified", "instruction-complete Talent ACCEPT: format/load current DIMMED portrait with warning before mutation, write current rating byte, clear only dialog-local pending, invalidate current portrait plus exact (69,63)-(131,281) gauge, force repaint, set base/action/gauge dirties, clear active slot, dispatch X as (0,4,rating), preserve the underlying performance wrapper and retained numeric gauge, and return in place"),
    0x004077D5: ("implemented-verified", "instruction-complete Talent Penalty: tear down/capture current movie and queue exact credits repaint first; WRONG5 precedes the unrounded x87 ten-percent debit and returns with contestant active/no ambient restart; correct branch plays REWARD1 before refund, loads/warns PENALTY before local pending/zero-score writes, repaints credits/current portrait/retained gauge before setting disqualification, clears active before PENALTY and synchronous TP response, gates first C71 on the initial teardown result, then restores TALENT.WAV in place"),
    0x00407A64: ("implemented-verified", "instruction-complete Talent Gong: tear down/report current performance, format/load current GONG portrait with warning before mutation, clear dialog-local pending then write immediate zero score, repaint current portrait and exact gauge, clear active slot, play GONG synchronously, consume/dispatch first C61 only when no performance was interrupted, then perform the outer TALENT.WAV restart while retaining the gauge and dialog"),
    0x00407BC1: ("implemented-verified", "Talent media dispatcher instruction-complete: stop ordinary sound, allocate/run/destroy the generic wrapper, forward exact tuples C21=(2,6,1), C61=(6,6,1), C71=(7,6,1), X=(0,4,rating), and unconditionally restart TALENT.WAV on success or failure; the outer C61/C71 caller performs its separate second restart"),
    0x00407C38: ("implemented-verified", "Talent current-performance stop/destroy instruction-complete: return stored-wrapper presence even after media-open failure, clear ownership, dirty the painter, invalidate exactly 208,108-528,344 without erase, and synchronously repaint; split MM_MCINOTIFY calls this helper then restores TALENT.WAV"),
    0x00407CB7: ("implemented-verified", "instruction-complete Talent pavilion close: commit all five dialog-local pending values before stopping ambient audio; release palette replacement, ACCEPT, Gong, Penalty, base, gauge, and five portraits in exact order; destroy the active performance movie last; return through native state-2 navigation"),
    0x00407E95: ("replaced-platform-runtime", "Talent dialog scalar-destructor MFC base cleanup thunk"),
    0x00407E9D: ("replaced-platform-runtime", "shared MFC constructor identity thunk returning this"),
    0x00407EA0: ("replaced-platform-runtime", "shared MFC/GDI object destructor body replaced by native object lifetime"),
    0x00407EE7: ("replaced-platform-runtime", "shared derived MFC destructor base tail"),
    0x00407F4D: ("replaced-platform-runtime", "shared scalar/base destructor thunk"),
    0x00407F71: ("replaced-platform-runtime", "shared AVI media-child constructor and zero-initialization replaced by native VideoPlayer lifetime"),
    0x00407FD2: ("replaced-platform-runtime", "shared MFC media-window destructor body replaced by native decoder/window lifetime"),
    0x00408019: ("replaced-platform-runtime", "shared MFC media-child base cleanup thunk"),
    0x0040802F: ("implemented-verified", "instruction-complete shared AVI child creator: store owner, derive optional parent HWND, call MCIWndCreateA with app instance/caller style/path, return zero on null child, otherwise subclass/bind the child, mark it created, set exact Ms. Metaverse title, and return one; native static decoder replaces VfW window plumbing while preserving caller-visible success, failure, caption, and ownership behavior"),
    0x004080D7: ("replaced-platform-runtime", "shared MCI child stop/seek helper replaced by native decoded-video stop and frame seek"),
    0x0040853E: ("replaced-platform-runtime", "profile dialog owned-control and MFC base cleanup body replaced by native child-window/dialog lifetime"),
    0x0040815F: ("implemented-verified", "instruction-complete profile-dialog cluster: resource 167 construction/DDX, full-desktop 640x480 centering, exact uppercase name/password styles and 1002/1005 rectangles without a synthetic edit limit, title and PASSWORD/OK load/warning order, native-size OK rectangle, synchronous no-capture press/release repaint, release-inside explicit base-OnOK route, background/pressed paint order and cleanup, and vtable 0x00431198 bare-return OnOK/OnCancel semantics so default Enter/Escape/close are inert"),
    0x0040860E: ("replaced-platform-runtime", "profile dialog painter local paint-DC/resource cleanup thunk"),
    0x0040867B: ("replaced-platform-runtime", "profile dialog name CString member cleanup thunk"),
    0x00408686: ("replaced-platform-runtime", "profile dialog password CString member cleanup thunk"),
    0x0040869B: ("replaced-platform-runtime", "profile dialog scalar-destructor MFC base cleanup thunk"),
    0x004086A3: ("implemented-verified", "instruction-complete Brains constructor/OnInit/timer/painter split: resource-158 geometry and five rectangles, copied-and-disqualification-masked pending scores, exact zero/one field initialization, centered 640x480 canvas, post-construction 500 ms timer, consume-C31-before-dispatch or delayed direct BRAINS.WAV start, conditional base/cash/gauge layers, all portraits and white frames, one-paint +0x64 red frame, shared-press triple overlay, exact hover name, cursor preservation, and WM_ACTIVATEAPP base/cash/gauge rearm"),
    0x004088A7: ("replaced-platform-runtime", "Brains painter local paint-context cleanup thunk"),
    0x00408B81: ("replaced-platform-runtime", "Brains painter temporary credits CString cleanup thunk"),
    0x00408CBD: ("replaced-platform-runtime", "Brains painter temporary CString cleanup thunk"),
    0x00408CC5: ("replaced-platform-runtime", "Brains painter temporary GDI brush cleanup thunk"),
    0x00408CCD: ("replaced-platform-runtime", "Brains painter temporary GDI brush cleanup thunk"),
    0x00408CDF: ("replaced-platform-runtime", "Brains painter local paint-DC/resource cleanup thunk"),
    0x00408CEA: ("implemented-verified", "instruction-complete Brains resource-construction split: base, palette-equivalent, ACCEPT, Gong, Penalty, rating 0, then five portrait wrappers in exact order; recovered paths, warning-and-continue behavior, bitmap-derived action rectangles, exact rating bounds (69,63)-(131,281), and portrait geometry (287+70*n,412) are retained"),
    0x00409187: ("implemented-verified", "instruction-complete Brains dynamic-rating and split mouse cluster: direct exit, exact meter formula/bounds, performance stop before speech, WAV-before-DIB load and filename-bearing warning even for zero, exact gauge repaint, idle TT0..TT10 versus active positive contestant comments, shared press with release-target cross-drags, choose-any-pending activation, single-chain x87 surcharge, exact cash/portrait/gauge repaint ordering, player-verified visible zero-gauge reset on success, NOCASH2 preservation, exact hover strip, all-results C81-delayed close, and strict cursor regions"),
    0x00409844: ("implemented-verified", "instruction-complete Brains performance child creation: destroy the prior wrapper, allocate/store the replacement before open, call shared creator with the caller-resolved path and style 0x4000000b, ignore its result, move to (208,108) at 320x240, show with command 5, and return one; BI/TI are exclusive clue-pavilion alternatives, and failed open retains blank wrapper ownership"),
    0x004098D0: ("implemented-verified", "instruction-complete Brains ACCEPT: format/load current DIMMED portrait with warning before mutation, write current rating byte, clear only dialog-local pending, invalidate current portrait plus exact (69,63)-(131,281) gauge, force repaint, set base/action/gauge dirties, clear active slot, dispatch X as (0,4,rating), preserve the underlying performance wrapper and retained numeric gauge, and return in place"),
    0x00409A03: ("implemented-verified", "instruction-complete Brains Penalty: tear down/capture current movie and queue exact credits repaint first; WRONG5 precedes the unrounded x87 ten-percent debit and returns with contestant active/no ambient restart; correct branch plays REWARD1 before refund, loads/warns PENALTY before local pending/zero-score writes, repaints credits/current portrait/retained gauge before setting disqualification, clears active before PENALTY and synchronous BP response, gates first C71 on the initial teardown result, then restores BRAINS.WAV in place"),
    0x00409C9E: ("implemented-verified", "instruction-complete Brains Gong: tear down/report current performance, format/load current GONG portrait with warning before mutation, clear dialog-local pending then write immediate zero score, repaint current portrait and exact gauge, clear active slot, play GONG synchronously, consume/dispatch first C61 only when no performance was interrupted, then perform the outer BRAINS.WAV restart while retaining the gauge and dialog"),
    0x00409DFF: ("implemented-verified", "Brains media dispatcher instruction-complete: stop ordinary sound, allocate/run/destroy the generic wrapper, forward exact tuples C31=(3,6,1), C61=(6,6,1), C71=(7,6,1), C81=(8,6,1), X=(0,4,rating), and unconditionally restart BRAINS.WAV on success or failure; outer C61/C71 restart again while C81 immediately stops it during close"),
    0x00409E76: ("implemented-verified", "Brains current-performance stop/destroy instruction-complete: return stored-wrapper presence even after media-open failure, clear ownership, dirty the painter, invalidate exactly 208,108-528,344 without erase, and synchronously repaint; split MM_MCINOTIFY calls this helper then restores BRAINS.WAV"),
    0x00409EF5: ("implemented-verified", "instruction-complete Brains pavilion close: commit all five dialog-local pending values before stopping ambient audio; release palette replacement, ACCEPT, Gong, Penalty, base, gauge, and five portraits in exact order; destroy the active performance movie last; return through native state-3 navigation"),
    0x0040A0D3: ("replaced-platform-runtime", "Brains dialog scalar-destructor MFC base cleanup thunk"),
    0x0040A0DB: ("implemented-verified", "instruction-complete 490-byte navigation popup constructor: base/document binding, exact cursor resources 160/161/163/164/165/169/170/0x7f02, recovered field-zero set, persistent menu creation in Talent/Brains/Looks/Slots/Exit/Skip Ahead order with command 203 absent, and initially grayed by-position Skip item 5; native semantic ownership also preserves Hall-entry suppression, Slots-pump re-entrancy, active-MCI Skip enablement, popup idle latching, and the original constructor's unwritten hand-animation bit across cross-scene reconstruction"),
    0x0040A301: ("replaced-platform-runtime", "navigation-popup owner destructor: releases menu bitmaps, host/Simm children, CString state, and MFC bases; native scene RAII replaces it"),
    0x0040A40B: ("replaced-platform-runtime", "navigation-popup embedded MFC document/base cleanup thunk"),
    0x0040A419: ("replaced-platform-runtime", "navigation-popup embedded CString cleanup thunk"),
    0x0040A431: ("replaced-platform-runtime", "navigation-popup shared AVI-child base cleanup thunk"),
    0x0040A43F: ("implemented-verified", "instruction-complete navigation DAT loader: exact object/asset/point/variant parsing across all 30 real entries, per-DAT +0x578 encounter-prefix reset, paused first background frame, leading-zero asset sentinel, initial-entry 0x0400-only cue and 0x0100-only middle-range resolution before the final timeGetTime CRT reseed, shared ZPR.WAV departure channel, 500 ms cursor timer, and delayed-idle timestamp/pool reset; native validation replaces the original unchecked malformed-data/MCI failure behavior, and the intervening physical-CD validator is deliberately omitted"),
    0x0040A8F5: ("replaced-platform-runtime", "navigation DAT-loader temporary CString cleanup thunk"),
    0x0040A8FD: ("replaced-platform-runtime", "navigation DAT-loader temporary CString cleanup thunk"),
    0x0040A90F: ("replaced-platform-runtime", "navigation DAT-loader temporary CString cleanup thunk"),
    0x0040A917: ("replaced-platform-runtime", "shared MCI frame-range seek/play command wrapper replaced by native decoded-video range playback"),
    0x0040A966: ("implemented-verified", "instruction-complete bounded navigation AVI frame-range playback: set active state, release both positional-overlay owners, issue forward MCI notify/from/to or dormant reverse play, dispatch result zero on a forward command failure, and retain exact endpoint state; native equivalents slice matching embedded PCM, cap delayed audio-clock jumps, preserve the endpoint before notification, and map XR1's sole stream-length stop 1686 to final zero-based frame 1685, while package probes decode all other 306 unique endpoints and prove every supplied range is ascending"),
    0x0040AA61: ("implemented-verified", "instruction-complete WM_LBUTTONDOWN transition executor from message-map entry 0x00431598: clear +0x628 first; any left click tests +0x118 before direction and interrupts active manual/automatic/idle/Slots MCI through the normal completion path; enable command-206 Skip Ahead; apply exact right/up/left range-link routing and ZPR channel-2 departure audio with 0x0900 suppression; preserve 0x4000 Slots Up/self activation, signed-float-bit zero-credit test, concurrent spin channel, post-range-start 4500 ms owner-message pump, abort-notify immediate result, and final stop; preserve 0x0010 WaveMix suspension, first-pass range, later destination-frame hold, and popup-constructor +0x144/+0x148 rearm on every cross-scene reconstruction"),
    0x0040ACAD: ("replaced-platform-runtime", "navigation transition temporary CString cleanup thunk"),
    0x0040AEDF: ("replaced-platform-runtime", "navigation transition temporary CString cleanup thunk"),
    0x0040AEE7: ("replaced-platform-runtime", "navigation transition temporary CString cleanup thunk"),
    0x0040AEF9: ("replaced-platform-runtime", "navigation transition temporary MFC object cleanup thunk"),
    0x0040AF01: ("implemented-verified", "instruction-complete 141-byte navigation pointer-direction handler: +0x118 active MCI forces neutral state 4 first; otherwise y<240 with flag 0x04 selects Up/resource 160, then x<320 with 0x02 selects Left/resource 164, x>320 with 0x01 selects Right/resource 163, and the exact x=320 line or any disabled direction records neutral without a SetCursor call; exhaustive tests cover all eight low-flag combinations at both 239/240 and 319/320/321 boundaries"),
    0x0040B4DF: ("implemented-verified", "instruction-complete disassembler-split navigation painter/post-transition cluster: 640x480 logical compositor; exact Slots sign-masked zero gate, symbol RNG, signed-raw-float wager cap, branch-selected result-text side, audio, cash mutation, and overlay; modal-Simm result>10 award gate, 5..15%-of-sponsorship/D0-$250-base amount and unconditional ZPR return; recovered 500 ms timer cadence with strict >1000 Simm-before->10000 idle-video evaluation, popup suppression, backing-first positional overlay alternation, 0x0020 frame-25 return, and idle rearm; destination WAV after action/cross-scene handling, all 60 automatic 0x0100/0x0800/0x2000 middle hops with only the retired FTP dialog bypassed, and exact arrival/overlay ordering"),
    0x0040C0D6: ("replaced-platform-runtime", "navigation painter packed mouse-point construction thunk"),
    0x0040C0DE: ("replaced-platform-runtime", "navigation painter temporary CString cleanup thunk"),
    0x0040C0E9: ("replaced-platform-runtime", "navigation painter temporary CString cleanup thunk"),
    0x0040C0FE: ("implemented-verified", "instruction-complete disassembler-split navigation command family: base/high-word gating, visible commands 200/201/202/204/205/206 plus hidden 203-to-tally dispatch, exact action mapping, per-disc Slots entries 29/30, Exit style-0x24 confirmation, command-206 active-range interruption plus stale-enabled settled middle-hop behavior; the CD-number prompt, MCI product query, path switch, and VOL.DAT retry loop are deliberately replaced by always-available packaged roots"),
    0x0040C3D3: ("replaced-platform-runtime", "navigation command temporary CString cleanup thunk"),
    0x0040C3E5: ("replaced-platform-runtime", "navigation command temporary CString cleanup thunk"),
    0x0040C3ED: ("implemented-verified", "instruction-complete 256-byte Simm selector: count entries equal to one, rearm every nonzero byte only after pass exhaustion, exact CRT float-scaled ordinal, wrapped forward scan, chosen-byte increment, replay carry, and exhaustive ordinal coverage; native failure replaces the unreachable all-zero infinite loop"),
    0x0040C4A1: ("replaced-bundled-media", "navigation installed mmpath registry lookup replaced by configured packaged asset root"),
    0x0040C552: ("replaced-bundled-media", "mounted-disc VOL.DAT validator and rejection prompt intentionally omitted for bundled assets"),
    0x0040C5E2: ("replaced-platform-runtime", "navigation MCI child command wrapper replaced by native VideoPlayer reset/open state"),
    0x0040C64E: ("replaced-platform-runtime", "navigation MCI command temporary CString cleanup thunk"),
    0x0040C6B4: ("replaced-platform-runtime", "navigation MFC scalar-destructor base cleanup thunk"),
    0x0040C729: ("replaced-platform-runtime", "navigation MFC scalar-destructor document cleanup thunk"),
    0x0040C74F: ("replaced-platform-runtime", "shared packed LPARAM-to-signed-point conversion replaced by native GET_X/Y_LPARAM handling"),
    0x0040C766: ("replaced-platform-runtime", "navigation MFC base destructor body replaced by native scene lifetime"),
    0x0040C7AD: ("replaced-platform-runtime", "navigation MFC base destructor cleanup thunk"),
    0x0040C7B5: ("replaced-platform-runtime", "navigation embedded document/base destructor body replaced by native state lifetime"),
    0x0040C80B: ("replaced-platform-runtime", "navigation embedded document/base destructor cleanup thunk"),
    0x0040C813: ("replaced-platform-runtime", "navigation hub's resource-158 MFC base construction, vtable install, and document-pointer storage replaced by the persistent native Win32 host; its vtable's OnOK result-zero route is covered by the verified owner loop"),
    0x0040C97E: ("implemented-verified", "instruction-complete NAVIGATE.DAT owner loop: store document, parse all scene/variant pairs, construct/run/destroy the hub, free table strings, return its modal result, and preserve default-OK result-zero save/application-exit; native validation replaces unchecked malformed-data/allocation behavior and probes lock all 31 supplied entries on both discs"),
    0x0040CB7D: ("implemented-verified", "instruction-complete immediate cross-scene handler: stop the active MCI range, destroy the old navigation popup, preserve ordinary targets or deliberately bypass the bundled-media replacement for extended two-CD targets, reconstruct the popup with its one-shot fields reset, set Hall-only +0x14c suppression for entries 13-16, and load the target DAT/variant; focused tests lock suppression boundaries and cross-scene state rearming"),
    0x0040CECB: ("replaced-bundled-media", "two-CD VOL.DAT prompt/remap intentionally bypassed for bundled assets"),
    0x0040D183: ("replaced-platform-runtime", "disc-remap temporary CString cleanup thunk"),
    0x0040D195: ("replaced-platform-runtime", "disc-remap temporary CString cleanup thunk"),
    0x0040D1F5: ("replaced-platform-runtime", "generic host/Simm media object MFC base cleanup thunk"),
    0x0040D1FD: ("replaced-platform-runtime", "generic media object's resource-158 MFC base/CString/vtable construction and current-cursor save/reapply replaced by native modal state and WM_SETCURSOR ownership; semantic C/X/Simm loading begins in the separately audited 0x0040d9cf split body"),
    0x0040D2AB: ("implemented-verified", "instruction-complete MMS frame update: frame increment/loop/terminal-minus-one mode-4 hold; SPR.dll top-left X/Y; strict x<-200, y<-200, or x>680 delta escape with intentionally no upper-Y test; mutually exclusive 0x0002 delta/0x0200 path motion; cumulative 0x0100 and absolute paths; cursor-plus-four/exclusive-end exhaustion; timed X-overwrite compatibility typo; initial/timed 0x0060 voice attempts followed by 0x0020 stops, distinct from 0x0040 hit-stop; mode-5 result-50 override; completion and pacing state across all ten reachable corpus flag values"),
    0x0040D568: ("replaced-platform-runtime", "MMS update temporary CString cleanup thunk"),
    0x0040D6B0: ("replaced-platform-runtime", "MMS update temporary CString cleanup thunk"),
    0x0040D6C2: ("implemented-verified", "instruction-complete disassembler-split generic-media mouse handler: every mode-6 click completes immediately while only an opaque SPR hit adds synchronous OUCH in teardown; 0x0080 reaction records ignore clicks; otherwise mode-3 applies +16/+16 before SPR.dll's strict-open left/top/right/bottom tests and top-left-pixel color key, outside clicks play RICOCHET, and inside clicks stop a prior 0x0040 voice, follow the MMS hit transition without starting the target voice, display its award, then seek/reposition the reaction entry frame; focused tests lock all four strict edges and both mode-6 outcomes"),
    0x0040D9BD: ("replaced-platform-runtime", "generic-media mouse handler embedded MFC base cleanup thunk"),
    0x0040D9CF: ("implemented-verified", "instruction-complete disassembler-split generic loader/OnInit body at 0x0040d9d7/0x0040da9f for every supplied offline route: centered resource-158 modal state and completion fields; dedicated mode-6 C path/HPNT coordinates/fake 0x0088 record/hand 166/11025 Hz WAV master; mode-4 X select-after-timeGetTime-reseed, MMS/AVI graph master, hand 166, and best-effort unchecked companion-WAV open; mode-3 D/T Simm path, target 162, optional state voices, RICOCHET channel 2, SPR top-left placement, and result-zero construction failures. Corpus probes prove the mode-5 missing-T/B fallback and 0x0400 auxiliary-background branch have no supplied assets/records; native static decoders replace only MCI/SPR/WaveMix plumbing and configured roots replace path1/path2"),
    0x0040E432: ("replaced-platform-runtime", "generic-media loader temporary CString cleanup thunk"),
    0x0040E43D: ("replaced-platform-runtime", "generic-media loader temporary CString cleanup thunk"),
    0x0040E448: ("replaced-platform-runtime", "generic-media loader temporary CString cleanup thunk"),
    0x0040E453: ("replaced-platform-runtime", "generic-media loader temporary CString cleanup thunk"),
    0x0040E45B: ("replaced-platform-runtime", "generic-media loader temporary paint-context cleanup thunk"),
    0x0040E466: ("replaced-platform-runtime", "generic-media loader temporary CString cleanup thunk"),
    0x0040E46E: ("replaced-platform-runtime", "generic-media loader temporary CString cleanup thunk"),
    0x0040E476: ("replaced-platform-runtime", "generic-media loader temporary CString cleanup thunk"),
    0x0040E47E: ("replaced-platform-runtime", "generic-media loader temporary CString cleanup thunk"),
    0x0040E490: ("replaced-platform-runtime", "generic-media loader temporary CString cleanup thunk"),
    0x0040E4BD: ("implemented-verified", "instruction-complete generic-media completion: one-shot guard, mode-6 waveOut reset/unprepare/close and buffer release, opaque-hit-only synchronous OUCH, shared MMS buffer/timer/SPR/AVI teardown, owner active-child clear, and caller-supplied modal result; native completion also covers immediate graph-master X WAV stop, generic Cancel, and argument-independent result-zero WM_ACTIVATEAPP teardown on either activation state"),
    0x0040E5C8: ("implemented-verified", "instruction-complete six-argument generic-media runner: bind owner/media selectors/mode/pacing/root, resolve the installed root through the packaged asset replacement, run the modal, stop the ordinary sound channel for ownerless C/X media, otherwise either reactivate the navigation mix or play ZPR for mode-3 ownership, and return the modal result; every caller destroys the wrapper and executes its ordinary failure/success tail, with Simm awards gated strictly above ten"),
    0x0040E6BF: ("replaced-platform-runtime", "generic media runner temporary CString cleanup thunk"),
    0x0040E6F2: ("implemented-verified", "instruction-complete generic modal media loop: zero both pacing clocks; pump/dispatch pending messages around every wait; signed timer_interval/20 with zero promoted to one; factor priority caller then 0x0080=8, modes 4/6=5, and mode-5 root=10/linked=1; two distinct timeGetTime changes per factor unit; delegate every mode-6 and pre-terminal-hold mode-4 pass to 0x0040eb52; update MMS until completion/cancel and invoke result-zero teardown only for the completion flag; the native event loop preserves these semantics without a blocking MFC busy pump"),
    0x0040E8AE: ("implemented-verified", "instruction-complete configured-root X selector: accepted-score mapping <=3 to group 3, 4..7 to group 1, and >=8 to group 2 with 15/12/11 bounds; per-selection zero-filled tolerant HNRD.DAT read with ignored open/short-read result, raw interleaved int32 zero tests, exhausted-group clear, exact float-scaled forward zero scan and maximum-CRT boundary, selected-word mutation, immediate raw 180-byte create/truncate rewrite before media construction, and timeGetTime reseed ordering; original empty installed-root shortcut is unreachable with the always-configured packaged root"),
    0x0040EB38: ("replaced-platform-runtime", "host-reaction selector temporary CString cleanup thunk"),
    0x0040EB4A: ("replaced-platform-runtime", "host-reaction selector temporary CString cleanup thunk"),
    0x0040EB52: ("implemented-verified", "instruction-complete elapsed-time synchronizer: first-call timeGetTime capture; unsigned >100000 ms stale/wrap guard; mode-6 waveOutGetPosition sample mapping with the displayed floor(samples*10/11025)+1 frame, ahead/behind 100 ms correction, and sample-length completion; ordinary/mode-4 wait to 100 ms; strict >100 overrun accumulation, strict accumulated >100 trigger, single-precision 0.01f truncated catch-up that skips visual frames without coordinate points, direct loop-frame reset rather than modulo, accumulator clear, and final clock capture; native PCM/event timing regression-locks each semantic boundary"),
    0x0040ECE1: ("replaced-bundled-media", "slots installed mmpath registry lookup replaced by configured packaged asset root"),
    0x0040ED92: ("replaced-platform-runtime", "legacy MMIO WAV chunk parser and WaveMix wave-header construction replaced by the bundled static PCM decoder"),
    0x0040EF20: ("replaced-platform-runtime", "legacy WAV-loader temporary CString cleanup thunk"),
    0x0040EF98: ("replaced-platform-runtime", "legacy audio-owner CString cleanup thunk"),
    0x0040EFB0: ("replaced-platform-runtime", "legacy audio-owner MFC base destructor thunk"),
    0x0040EFDE: ("replaced-platform-runtime", "WaveMix initialization, three-channel creation, activation, and per-channel header setup replaced by three native PCM players"),
    0x0040F0F2: ("replaced-platform-runtime", "WaveMix close/free/deactivate/destruction path replaced by native AudioPlayer RAII"),
    0x0040F1A5: ("replaced-platform-runtime", "WaveMix owner MFC base cleanup thunk"),
    0x0040F1AD: ("replaced-platform-runtime", "WaveMix channel 1-3 replace/open-wave dispatcher replaced by independent native PCM channel loading"),
    0x0040F244: ("replaced-platform-runtime", "WaveMix channel 1-3 pump/flush dispatcher replaced by native PCM start/stop control"),
    0x0040F288: ("replaced-platform-runtime", "direct WaveMixPump trampoline replaced by native audio servicing"),
    0x0040F28D: ("replaced-platform-runtime", "WaveMix global activate/deactivate wrapper replaced by native audio-player state"),
    0x0040F2C4: ("implemented-verified", "instruction-complete ORDER dialog family: centered 640x480 construction, zeroed flags/rank-ten state and reversed mapping table, 2,000-ms C51 staging, exact ACCEPT-first mouse-down scan and 313x30 phrase hit rows, union-rectangle HAND movement, surviving-press ACCEPT release, drag-out cancellation, right-double-click result zero, confirmed Cancel result one, activation-time phrase reload, cursor 166, stateful base/ACCEPT/COMMENT/HAND/TH/rank painter, and result-independent continuation to navigation"),
    0x0040F653: ("implemented-verified", "instruction-complete ORDER assignment: duplicate rejection, selected-row rank mutation and synchronous red-text paint, asynchronous TT10-to-TT1 start, THn replacement/repaint, delayed reversed document-table commit, and immediate result-zero teardown when rank reaches zero without clipping TT1"),
    0x0040F9D3: ("replaced-platform-runtime", "ORDER assignment temporary CString cleanup thunk"),
    0x0040F9EB: ("replaced-platform-runtime", "ORDER assignment temporary archive/DIB cleanup thunk replaced by native bitmap lifetime"),
    0x0040F9FD: ("replaced-platform-runtime", "ORDER assignment local paint/resource cleanup thunk"),
    0x0040FA08: ("implemented-verified", "instruction-complete ORDER bitmap construction: required wrappers load in exact ACCEPT, TH10, HAND order with style-0x30 warning-and-continue behavior; recovered origins and native DIB extents produce ACCEPT (21,381)-(217,472), TH (88,30)-(156,362), and HAND (215,30)-(300,105), while ORDER base follows in the first paint"),
    0x0040FC60: ("implemented-verified", "instruction-complete COMMENT helper: formats COMMENT/0-9, releases the prior logical wrapper, warning-loads the next 314x32 DIB at (308,40+40n), marks only the current COMMENT layer, and synchronously repaints its exact rectangle; native retains the ten decoded DIB handles solely to reconstruct Win32 exposure"),
    0x0040FD51: ("implemented-verified", "instruction-complete HAND helper: sets base/HAND dirty layers, derives the 85x75 destination from the selected row, unions old and new backing rectangles, and synchronously repaints that union, including during the pre-C51 delay"),
    0x0040FDEF: ("implemented-verified", "instruction-complete TH helper: formats TH0-TH10, releases the previous thermometer wrapper, warning-loads its replacement, marks only the TH layer, and synchronously repaints exact rectangle (88,30)-(156,362)"),
    0x0040FEC4: ("implemented-verified", "instruction-complete ORDER teardown: null-safe generic/base, ACCEPT, HAND, TH, and final COMMENT ownership release in recovered order followed by caller-supplied modal result; native releases its retained COMMENT set in the final-wrapper position and deliberately leaves asynchronous TT1 alive"),
    0x0040FFC4: ("replaced-platform-runtime", "ORDER scalar-destructor MFC base cleanup thunk replaced by native scene lifetime"),
    0x0040FFCC: ("implemented-verified", "instruction-complete category-weight dialog family: fixed centered 640x480 popup and title, zeroed controls/flags, 500-ms C91 staging with hidden TH*_1 ownership and in-place 5/5/5 replacement, exact OK-first mouse-down dispatch, control-before-OK release/drag-out restoration, surviving-press OK commit without a second coordinate test, and stateful painter consuming base/OK/selected-arrow/current-gauge dirty layers in recovered order while preserving noncurrent gauge flags; WM_SETCURSOR 0x00410f3b forces hand 166 and WM_ACTIVATEAPP 0x00410fd9 recreates all three current gauges on reactivation"),
    0x00410285: ("implemented-verified", "instruction-complete six-way Weight adjustment: pressed-arrow repaint precedes dispatch; controls map Looks/Brains/Talent down then up, clamp by preserving 0/10, set the matching gauge category, replace or blank and synchronously repaint it, then play the resulting WAV/TTn.WAV asynchronously even when the boundary value did not change; exhaustive tests cover every control/value pair"),
    0x004104E4: ("implemented-verified", "instruction-complete Weight commit: signed control sum, exact plain all-zero warning with no document mutation, x87-order Looks/total and Brains/total division followed by 100.0f multiplication and truncate-toward-zero conversion, Talent remainder assignment guaranteeing sum 100, and successful resource teardown/EndDialog result zero; focused coverage exhausts every reachable 0..10 control combination"),
    0x004106DE: ("replaced-platform-runtime", "Weight painter local paint-DC/resource cleanup thunk"),
    0x004106E6: ("implemented-verified", "instruction-complete Weight bitmap construction: WEIGHT base and helper, OK, hidden th1_1/th2_1/th3_1 backings, then six down/up controls load in recovered order with style-0x30 warning-and-continue behavior; every rectangle uses the exact bitmap extent at its recovered origin, while independent visibility keeps the TH1 backings unpainted until C91 replaces them"),
    0x00410E3E: ("implemented-verified", "instruction-complete Weight gauge helper: category selects exact th1/th2/th3 filename and recovered repaint rectangle; nonzero destroys the prior wrapper before loading with style-0x30 failure warning, while zero retains that wrapper but hides it under an immediate base repaint; exhaustive tests cover all 33 reachable category/value plans"),
    0x00410F5B: ("implemented-verified", "instruction-complete Weight teardown: null-safe release of optional generic-media wrapper, base DIB, OK wrapper, six arrow wrappers, and three gauge wrappers in recovered order, followed by result-preserving dialog completion; native result zero continues to ORDER and confirmed result one exits"),
    0x00411080: ("replaced-platform-runtime", "Weight scalar-destructor MFC base cleanup thunk replaced by native scene lifetime"),
    0x004110AE: ("replaced-platform-runtime", "shared legacy DIB object initialization replaced by Win32 HBITMAP lifetime"),
    0x00411116: ("replaced-platform-runtime", "shared legacy DIB object teardown/unlock/free replaced by DeleteObject lifetime"),
    0x0041118F: ("replaced-platform-runtime", "legacy DIB scalar/base cleanup thunk replaced by native HBITMAP lifetime"),
    0x004111C2: ("replaced-platform-runtime", "shared archive-backed BMP read/signature validation replaced by LoadImageW and decoded bitmap loading"),
    0x00411241: ("replaced-platform-runtime", "legacy DIB virtual-dispatch exception wrapper replaced by direct native bitmap rendering"),
    0x004112D2: ("replaced-platform-runtime", "legacy DIB exception landing pad and cleanup fragment"),
    0x004112EF: ("replaced-platform-runtime", "legacy DIB temporary exception-object cleanup thunk"),
    0x00411302: ("replaced-platform-runtime", "disassembler-split legacy DIB wrapper epilogue"),
    0x00411313: ("replaced-platform-runtime", "legacy DIB offscreen DC/palette setup and StretchDIBits renderer replaced by native HDC/HBITMAP compositing"),
    0x0041155B: ("replaced-platform-runtime", "legacy DIB-to-HDC blit helper replaced by native BitBlt/StretchBlt compositing"),
    0x004115AD: ("replaced-platform-runtime", "legacy DIB bit-depth getter replaced by decoded bitmap metadata"),
    0x004115B1: ("replaced-platform-runtime", "legacy DIB header getter replaced by decoded bitmap metadata"),
    0x004115B5: ("replaced-platform-runtime", "21-byte legacy DIB extent getter replaced by GetObjectW/native decoded-frame dimensions at every caller"),
    0x004115CA: ("replaced-platform-runtime", "legacy 8-bit DIB palette-entry setter replaced by native bitmap conversion"),
    0x004115E6: ("replaced-platform-runtime", "legacy 8-bit DIB palette-entry reader replaced by native bitmap conversion"),
    0x00411610: ("replaced-platform-runtime", "shared movable-memory DIB allocation/lock helper replaced by Win32 bitmap loading"),
    0x0041168A: ("replaced-platform-runtime", "legacy 1/4/8-bit DIB color-table to logical-palette builder replaced by native bitmap conversion"),
    0x00411759: ("replaced-platform-runtime", "legacy randomized tile-blit DIB transition helper; no recovered MM.EXE caller, with active native scene transitions handled by the compositor"),
    0x004119EA: ("replaced-platform-runtime", "shared filename BMP open/read wrapper replaced by filesystem path plus LoadImageW"),
    0x00411A64: ("replaced-platform-runtime", "legacy offscreen backing-bitmap allocation/capture helper used by the retired online compositor"),
    0x00411C64: ("replaced-platform-runtime", "legacy multi-pass color-key DIB compositor whose only recovered caller is the retired online transfer painter"),
    0x00412121: ("replaced-platform-runtime", "legacy 8-bit active-window region capture, system-palette synthesis, and optional CPalette export replaced by retained background-video compositing; the navigation timer preserves its exact backing-first/N-overlay-second 500 ms alternation and exact clipped rectangle"),
    0x00412589: ("replaced-platform-runtime", "derived legacy DIB scalar/base cleanup thunk replaced by native bitmap/object lifetime"),
    0x004125B5: ("implemented-verified", "instruction-verified Looks rotate-child mouse-down: exact PtInRect-exclusive 211x312 front bands 0..102/104..207/209..311 with one-pixel gaps, whole-back region 4, parent private-0x0408 dispatch, synchronous-turn input exclusion, and split child WM_SETCURSOR 0x004126cf resource 181 whenever either stable-view flag is active"),
    0x00412712: ("implemented-verified", "instruction-complete Looks resource-158 constructor, split OnInit, timer lifecycle, and 0x004129d3 painter: five embedded wrappers, document binding, null ROTATE child, copied/disqualification-masked pending ratings, exact zero/one field initialization and per-dialog magnifier counters, centered 640x480 canvas, post-resource 500 ms timer retained even after consumed C41, fixed base/ACCEPT/PENALTY/ROTATE/MAG/cash/rating/portrait/selection/hover draw order, every one-shot dirty clear, hidden MAG/111 and LOOK/1 preload, blank pre-timer presentation, WM_SETCURSOR preservation, active-only WM_ACTIVATEAPP parent/retained-child re-show, and native direct-key equivalent for DLGC_WANTALLKEYS"),
    0x0041293C: ("replaced-platform-runtime", "Looks entry exception-unwind temporary CString cleanup thunk"),
    0x00412944: ("implemented-verified", "instruction-complete Looks 500 ms entry callback: kill the timer, consume document C41 before its modal attempt, continue in the already-constructed pavilion without reloading dialog resources, construct and retain ROTATE.AVI at frame zero, clear the current contestant ID, auto-select the first dialog-local pending contestant, hide the retained child if none remain, start LOOKS.WAV, and establish front-view state; later activation can re-show the frame-zero child"),
    0x00412D56: ("replaced-platform-runtime", "Looks painter temporary CString cleanup thunk"),
    0x00412D5E: ("replaced-platform-runtime", "Looks painter temporary GDI brush cleanup thunk"),
    0x00412D66: ("replaced-platform-runtime", "Looks painter temporary GDI brush cleanup thunk"),
    0x00412D6E: ("replaced-platform-runtime", "Looks painter temporary CString cleanup thunk"),
    0x00412D80: ("replaced-platform-runtime", "Looks painter local paint-DC/resource cleanup thunk"),
    0x00412D8B: ("implemented-verified", "instruction-complete Looks bitmap/control construction in exact LOOK base, ACCEPT, PENALTY, ROTATE, hidden retained MAG/111, hidden retained gauge 1, then five portrait order; recovered rectangles, pending/disqualified portrait filename branches, style-0x30 warning-and-continue behavior, and 32-bit no-palette replacement are covered, while the distinct stateful painter is the disassembler-split body at 0x004129d3"),
    0x004132D6: ("implemented-verified", "instruction-complete Looks media dispatcher: stop ambient sound and dirty LOOK backing before allocating the generic wrapper; forward exact (0,4,6,0,1) C41 or (0,0,4,0,current-rating) ACCEPT arguments; release the wrapper; and reposition a retained ROTATE child at 225,64 with 212x312 extent after return"),
    0x00413361: ("implemented-verified", "instruction-complete Looks rating loader plus disassembler-split mouse-down/up/move bodies: direct x<60/y<382 exit; ungated ACCEPT/PENALTY/ROTATE press state; exhaustive 62,13-118,278 meter bands; contestant/media/rating loop; retained gauge hide-versus-replace behavior; parent-presentation click re-showing hidden ROTATE without seek; exact 287,388-640,410 synchronous hover-name repaint; parent cursor 174/166 regions plus lower-right preservation; final-retry-only magnifier warning; no artificial mouse capture; and coded same-target release/cancel semantics including ACCEPT clear, PENALTY's retained logical-and-pixel latch, and ROTATE's pre-hit-test logical clear while its pressed pixels survive both cancelled and dispatched releases"),
    0x00413A36: ("implemented-verified", "instruction-complete Looks ROTATE.AVI child setup: destroy the prior wrapper, construct and retain the replacement independently of MCI open success, use style 0x4000000b, place at 225,64 with a 212x312 window, and return one unconditionally; the native hidden frame-zero child also preserves the completed-entry and reactivation lifetime"),
    0x00413B13: ("implemented-verified", "instruction-complete Looks synchronous temporary category-response AVI helper: construct a disposable style-0xc000000b child, place it at owner-screen origin plus 225,64 with 200x320 extent, show it, issue unchecked Play From 0 Wait without owner-message pumping, stop/seek it, destroy it, and return one; native open failure likewise continues immediately"),
    0x00413BE1: ("implemented-verified", "instruction-verified branchless Looks ROTATE helper replacement: exact inclusive contestant-relative From/To endpoints, owner-blocking MCI-Wait-equivalent input exclusion through the twentieth frame, and ignored media-command result with the caller's unconditional stable-view transition"),
    0x00413C1B: ("implemented-verified", "instruction-complete Looks accept action: score reaction first; current-only DIMMED replacement and warning before immediate document score/dialog-local pending writes; forced portrait and meter repaint; automatic first-pending selection through an in-place retained-child seek with a player-verified numeric and visible meter reset; ambient restart"),
    0x00413D61: ("implemented-verified", "instruction-complete Looks Penalty Box: initial cash dirtying; WRONG5 before the unrounded-x87 ten-percent debit with no presentation teardown or ambient restart; correct REWARD1, exact refund, current-only PENALTY load/warning before score/pending mutation, forced portrait/blank-meter repaint before disqualification, ROTATE hide and blank presentation before PENALTY.WAV, owner-blocking LP at 225,64/200x320, post-response ROTATE restore, automatic in-place selection, and LOOKS.WAV restart, including immediate continuation on LP open failure"),
    0x0041402A: ("implemented-verified", "instruction-verified Looks rotation toggle: show retained child, clear stable flags during the owner-blocking command, exact contestant-relative base..base+19/front-to-back and base+20..base+39/back-to-front ranges, install the destination stable flag, toggle the dialog view bit, and preserve that state transition even when the unchecked MCI command fails"),
    0x004140BF: ("implemented-verified", "instruction-complete Looks automatic activation: clear the numeric rating and prior visible meter, scan all five dialog-local pending values from slot zero, select the first nonzero slot, repaint the full portrait strip, show ROTATE, seek to (contestant_id-1)*40, and restore front-view state; an exhausted scan retains the prior selected contestant, while the entry caller's pre-cleared ID drives its separate hide branch"),
    0x0041434A: ("implemented-verified", "instruction-complete Looks pavilion close: commit all five dialog-local pending values before stopping ambient audio; release ACCEPT, PENALTY, ROTATE control, retained MAG, base, retained gauge, and five portraits in recovered order; replace the obsolete 8-bit palette owner with the 32-bit compositor; stop/destroy ROTATE last; preserve the caller result through the native state-4 navigation continuation"),
    0x00414531: ("replaced-platform-runtime", "Looks dialog scalar/base destructor thunk"),
    0x00414591: ("replaced-platform-runtime", "Looks media-child destructor tail replaced by native decoder lifetime"),
    0x00414599: ("replaced-platform-runtime", "online custom-sprite dialog construction and MFC member initialization; retired offline"),
    0x00414711: ("replaced-platform-runtime", "online custom-sprite dialog destruction and owned-member cleanup; retired offline"),
    0x004147B3: ("replaced-platform-runtime", "online-dialog CString member cleanup thunk"),
    0x004147C1: ("replaced-platform-runtime", "online-dialog socket wrapper cleanup thunk"),
    0x004147CF: ("replaced-platform-runtime", "online-dialog socket wrapper cleanup thunk"),
    0x004147DD: ("replaced-platform-runtime", "online-dialog socket wrapper cleanup thunk"),
    0x004147EB: ("replaced-platform-runtime", "online-dialog CString member cleanup thunk"),
    0x004147F9: ("replaced-platform-runtime", "online-dialog CString member cleanup thunk"),
    0x00414811: ("replaced-platform-runtime", "online custom-sprite dialog MFC base destructor tail"),
    0x00414AE6: ("replaced-platform-runtime", "FTP bootstrap temporary CString cleanup thunk"),
    0x00414AEE: ("obsolete-online", "Virtual Vegas FTP bootstrap: WSA 1.1 startup, ftp.virtualvegas.com resolution, control/data socket setup, async selection, and port-21 connect"),
    0x00414E97: ("replaced-platform-runtime", "FTP bootstrap local CString cleanup thunk"),
    0x00414E9F: ("replaced-platform-runtime", "FTP bootstrap local CString cleanup thunk"),
    0x00414EA7: ("replaced-platform-runtime", "FTP bootstrap local CString cleanup thunk"),
    0x00414F90: ("replaced-platform-runtime", "FTP bootstrap local CString cleanup thunk"),
    0x00414F98: ("replaced-platform-runtime", "FTP bootstrap local CString cleanup thunk"),
    0x00414FA0: ("replaced-platform-runtime", "FTP bootstrap local CString cleanup thunk"),
    0x00414FA8: ("replaced-platform-runtime", "FTP bootstrap local CString cleanup thunk"),
    0x00414FBA: ("replaced-platform-runtime", "FTP bootstrap local CString cleanup thunk"),
    0x00414FC2: ("obsolete-online", "FTP control/data receive and response state machine for the retired Virtual Vegas custom-sprite service"),
    0x00415293: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x0041529E: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x004152A9: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x004152B1: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x004152D8: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x004152E3: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x004152EB: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x004152F3: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x004152FB: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x0041530D: ("replaced-platform-runtime", "FTP response parser local CString cleanup thunk"),
    0x00415315: ("obsolete-online", "WSOCK custom-sprite bitmap receive/validation loop; receives BM payloads, writes the remote filename, and dispatches online transfer completion"),
    0x004155CC: ("replaced-platform-runtime", "online bitmap receiver local CString cleanup thunk"),
    0x004155D4: ("replaced-platform-runtime", "online bitmap receiver local CString cleanup thunk"),
    0x004155E6: ("replaced-platform-runtime", "online bitmap receiver local CString cleanup thunk"),
    0x004155EE: ("replaced-platform-runtime", "MFC virtual dispatch trampoline used by retired online dialog"),
    0x0041561E: ("obsolete-online", "retired online custom-sprite filename/path parser and owner notification setup"),
    0x004157B7: ("replaced-platform-runtime", "online filename parser local CString cleanup thunk"),
    0x004157BF: ("replaced-platform-runtime", "online filename parser local CString cleanup thunk"),
    0x004157C7: ("replaced-platform-runtime", "online filename parser local CString cleanup thunk"),
    0x004157CF: ("replaced-platform-runtime", "online filename parser local CString cleanup thunk"),
    0x004157D7: ("replaced-platform-runtime", "online filename parser local CString cleanup thunk"),
    0x004157DF: ("replaced-platform-runtime", "online filename parser local CString cleanup thunk"),
    0x004157E7: ("replaced-platform-runtime", "online filename parser local CString cleanup thunk"),
    0x004157F9: ("replaced-platform-runtime", "online filename parser local CString cleanup thunk"),
    0x00415801: ("obsolete-online", "active-mode FTP data setup: getsockname, PORT command formatting, and remote custom-sprite transfer command preparation"),
    0x00415A93: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415A9E: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415AA9: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415AB4: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415ABF: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415ACA: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415AD5: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415ADD: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415AE5: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415AED: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415AF5: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415AFD: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415B0F: ("replaced-platform-runtime", "active-mode FTP setup local CString cleanup thunk"),
    0x00415B17: ("obsolete-online", "online custom-sprite transfer completion, list compaction, and 256-byte TLST.DAT cache update"),
    0x00415CAA: ("replaced-platform-runtime", "online cache-update local CString cleanup thunk"),
    0x00415CBC: ("obsolete-online", "retired custom-sprite list fetch/cache orchestration"),
    0x00415F42: ("replaced-platform-runtime", "online list/cache local CString cleanup thunk"),
    0x00415F4A: ("replaced-platform-runtime", "online list/cache local CString cleanup thunk"),
    0x00415F52: ("replaced-platform-runtime", "online list/cache local CString cleanup thunk"),
    0x00415F5A: ("replaced-platform-runtime", "online list/cache local CString cleanup thunk"),
    0x00415F62: ("replaced-platform-runtime", "online list/cache local CString cleanup thunk"),
    0x00415F6A: ("replaced-platform-runtime", "online list/cache local CString cleanup thunk"),
    0x00415F72: ("replaced-platform-runtime", "online list/cache local CString cleanup thunk"),
    0x00415F84: ("replaced-platform-runtime", "online list/cache local CString cleanup thunk"),
    0x00416083: ("replaced-platform-runtime", "online transfer painter temporary GDI object cleanup thunk"),
    0x0041608B: ("obsolete-online", "online socket-progress painter: hides remote controls, composites BMP/SOCK/ONLINE1.BMP, services the transfer, then restores controls"),
    0x00416309: ("replaced-platform-runtime", "online socket-progress painter local paint-context cleanup thunk"),
    0x00416595: ("replaced-platform-runtime", "retired online transfer CString cleanup thunk"),
    0x0041659D: ("replaced-platform-runtime", "retired online transfer CString cleanup thunk"),
    0x004165A5: ("replaced-platform-runtime", "retired online transfer CString cleanup thunk"),
    0x004165AD: ("replaced-platform-runtime", "retired online transfer CString cleanup thunk"),
    0x004165B5: ("replaced-platform-runtime", "retired online transfer CString cleanup thunk"),
    0x004165BD: ("replaced-platform-runtime", "retired online transfer CString cleanup thunk"),
    0x004165C5: ("replaced-platform-runtime", "retired online transfer CString cleanup thunk"),
    0x004165D7: ("replaced-platform-runtime", "retired online transfer CString cleanup thunk"),
    0x004165DF: ("obsolete-online", "retired downloaded custom-sprite bitmap conversion, document-buffer update, owner notification, and selection finalization"),
    0x00416999: ("implemented-verified", "non-repeat object construction, including its time(0)-seed side effect; the ten caller constructions are replayed in original order before NRFPAV I/O"),
    0x004169C7: ("implemented-verified", "set the contestant-specific active width and zero exactly that active flag range before a selected-line read"),
    0x004169E7: ("implemented-verified", "compact zero-valued active flags, return -1/no-rand when none exist, otherwise select by CRT rand modulo zero count, mark the chosen flag, and return its zero-based index"),
    0x00416A54: ("implemented-verified", "mark the selected raw-byte flag as one, retain its index, and trigger cycle reset only when it was the final zero choice"),
    0x00416A73: ("implemented-verified", "zero the full active range at cycle exhaustion and restore the just-selected flag, preventing an immediate boundary repeat"),
    0x00416A96: ("implemented-verified", "bind the shared nrfPav.dat path, zero-based CRLF line index, and active width; native selected-line APIs retain the equivalent binding"),
    0x00416AC4: ("implemented-verified", "best-effort r+ selected-line rewrite: LF-scan from file start, write each raw flag plus ASCII '0', preserve the CRLF line ending, and return silently on open/seek/write failure"),
    0x00416B33: ("implemented-verified", "best-effort r+ selected-line load: LF-scan from file start, read the active byte count without stopping at CR/LF, store raw byte-minus-'0' including wrapped EOF 0xcf, and return silently on open failure"),
    0x00416B9F: ("implemented-verified", "seek a CRLF text table from byte zero by scanning the requested number of LF terminators; EOF/position failures remain non-fatal to the owning read/write helper"),
    0x00416C4A: ("replaced-platform-runtime", "retired custom-sprite bitmap child construction and CString member initialization"),
    0x00416CE0: ("replaced-platform-runtime", "retired custom-sprite bitmap child destruction and owned-GDI cleanup"),
    0x00416D71: ("replaced-platform-runtime", "retired custom-sprite bitmap child CString cleanup thunk"),
    0x00416D86: ("replaced-platform-runtime", "retired custom-sprite bitmap child MFC base cleanup thunk"),
    0x00416DD9: ("replaced-platform-runtime", "retired custom-sprite bitmap painter local paint-context cleanup thunk; split painter body at 0x00416deb is offline-obsolete"),
}


def subsystem(address: int) -> str:
    """Assign a provisional ownership region without claiming semantics."""
    regions = (
        (0x00401B03, "bootstrap/media-helpers"),
        (0x00403D56, "round-orchestration/profile/tally"),
        (0x004064BC, "cryo-selection"),
        (0x004086A3, "talent-pavilion"),
        (0x0040A43F, "brains-pavilion"),
        (0x0040F2C4, "navigation-hub/slots"),
        (0x00412712, "weights/comment-order"),
        (0x0041608B, "looks-pavilion"),
        (0x00421184, "online/common-game-support"),
        (1 << 63, "mfc/crt/platform-runtime"),
    )
    for upper_bound, label in regions:
        if address < upper_bound:
            return label
    raise AssertionError("unreachable")


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def resolve_strings(
    references: list[int], strings: list[dict[str, Any]], starts: list[int]
) -> list[str]:
    resolved: list[str] = []
    for reference in references:
        index = bisect.bisect_right(starts, reference) - 1
        if index < 0:
            continue
        item = strings[index]
        start = int(item.get("vaddr", -1))
        size = max(int(item.get("size", 0)), 1)
        if start <= reference < start + size:
            value = str(item.get("string", "")).replace("\t", "\\t")
            if value and value not in resolved:
                resolved.append(value)
    return resolved


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("artifacts/disassembly/mm"),
        help="directory containing functions.json and r2-strings.json",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("artifacts/function-parity.tsv"),
        help="generated exhaustive TSV ledger",
    )
    args = parser.parse_args()

    functions = sorted(
        load_json(args.input / "functions.json"), key=lambda item: int(item["offset"])
    )
    strings = sorted(
        load_json(args.input / "r2-strings.json"), key=lambda item: int(item["vaddr"])
    )
    string_starts = [int(item["vaddr"]) for item in strings]
    function_addresses = {int(item["offset"]) for item in functions}

    args.output.parent.mkdir(parents=True, exist_ok=True)
    status_counts: Counter[str] = Counter()
    subsystem_counts: Counter[str] = Counter()
    resource_function_count = 0

    columns = (
        "address",
        "name",
        "size",
        "subsystem",
        "parity_status",
        "indegree",
        "outdegree",
        "internal_calls",
        "external_calls",
        "resource_references",
        "direct_strings",
        "audit_note",
    )
    with args.output.open("w", encoding="utf-8", newline="") as destination:
        writer = csv.DictWriter(destination, fieldnames=columns, dialect="excel-tab")
        writer.writeheader()
        for function in functions:
            address = int(function["offset"])
            owner = subsystem(address)
            if address >= 0x004171A1:
                status, note = (
                    "replaced-platform-runtime",
                    "audited as linked MSVC MFC/CRT/platform support; native port uses Win32/C++ runtime",
                )
            elif 0x004170A2 <= address <= 0x00417186:
                status, note = (
                    "replaced-platform-runtime",
                    "VfW, WaveMix, or Winsock import trampoline; native media/audio replaces it or its retired online caller is excluded",
                )
            else:
                status, note = TRACED_FUNCTIONS.get(
                    address,
                    ("unmapped", "requires instruction-level semantic trace"),
                )

            direct_strings = resolve_strings(
                [int(value) for value in function.get("datarefs", [])],
                strings,
                string_starts,
            )
            resources = [value for value in direct_strings if RESOURCE_RE.search(value)]
            if resources:
                resource_function_count += 1

            call_targets = [
                int(reference["addr"])
                for reference in function.get("callrefs", [])
                if reference.get("type") == "CALL" and "addr" in reference
            ]
            internal_calls = sum(target in function_addresses for target in call_targets)
            external_calls = len(call_targets) - internal_calls

            writer.writerow(
                {
                    "address": f"0x{address:08x}",
                    "name": function.get("name", ""),
                    "size": function.get("size", 0),
                    "subsystem": owner,
                    "parity_status": status,
                    "indegree": function.get("indegree", 0),
                    "outdegree": function.get("outdegree", 0),
                    "internal_calls": internal_calls,
                    "external_calls": external_calls,
                    "resource_references": " | ".join(resources),
                    "direct_strings": " | ".join(direct_strings),
                    "audit_note": note,
                }
            )
            status_counts[status] += 1
            subsystem_counts[owner] += 1

    application_count = sum(
        count
        for status, count in status_counts.items()
        if status != "replaced-platform-runtime"
    )
    print(f"Wrote {len(functions)} functions to {args.output}")
    print(f"Application/game-support functions: {application_count}")
    print(f"Functions with direct resource references: {resource_function_count}")
    print("Parity status:")
    for status, count in sorted(status_counts.items()):
        print(f"  {status}: {count}")
    print("Subsystem inventory:")
    for owner, count in subsystem_counts.items():
        print(f"  {owner}: {count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
