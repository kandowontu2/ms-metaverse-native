# AVI inventory and trigger audit

## Result

The supplied discs contain 266 AVI files: 132 on Disk I and 134 on Disk II.
Every file belongs to a recovered runtime family except Disk I
`MOV/HOST/X11DB.AVI`. That movie has no matching MMS, WAV, executable string,
or second-disc counterpart and is classified as an installed inactive artifact.
It is not a native playback omission.

The original executable exposes thirteen literal AVI path templates. Host X
reaction names are assembled from the score group and persisted rotation index,
Simm T names are assembled from the selected D script, and each navigation
background filename is read from its DAT header. The native port has an owning
trigger for every active form.

## Disk I reconciliation

| Family | Files | Recovered trigger | Native boundary |
|---|---:|---|---|
| `INTRO/MMINTRO` | 1 | 100 ms after the centered modal paints, following profile validation and before ordinary entry-13 Hall or DEMO navigation | One-shot with embedded audio; Escape/close is disabled, active Enter/OK returns zero and continues to `HALL.DAT`/DEMO |
| `CRYO/ROTATE`, `CRYO/INFO` | 2 | Constructed ROTATE-then-INFO after C11 returns; browse seeks both; ROTATE button plays a quarter-turn; detail prompt completion starts bounded INFO | Initial/front seeks are silent; exact contestant ranges; INFO starts after synchronous `TT0`-`TT5` |
| `BRAIN/B<id><variant>` | 34 | Selected Brains performance | Rotation-selected normal clip |
| `BRAIN/BI<id>` | 10 | Brains judge-cadet/impostor slot | Selected instead of the normal clip only for the Brains clue pavilion |
| `BRAIN/BP<id>` | 10 | Correct Brains Penalty Box result | Modal response before an in-place chooser return; the prior numeric gauge is retained |
| `LOOK/ROTATE` | 1 | Looks contestant load and rotate control | Contestant block seek plus exact turn ranges |
| `LOOK/LP<id>` | 10 | Correct Looks Penalty Box result | Modal child at the original Looks media rectangle |
| `HOST/C11,C12,C21,C31,C41,C51,C61,C71,C81,C91` | 10 | Screen entry, first sponsor, first eligible gong/penalty, completed Brains strip | Delayed/modal host sprite with companion WAV; every click ends it and only an opaque hit adds OUCH |
| `HOST/X<group><variant>` | 38 | Judge-O-Matic score response | MMS-driven score reaction selected through `hnrd.dat` rotation |
| `HOST/X11DB` | 1 | None | Installed inactive movie; deliberately not dispatched |
| DAT-selected navigation backgrounds | 4 | Scene construction and directional/automatic ranges | Frame-range playback; only `XRDS1F` embedded PCM is range-gated |
| `NAV/T0`-`NAV/T10` | 11 | One-second dwell at unused DAT `0x0080` object | MMS-driven modal Simm encounter; overridden Cancel and either `WM_ACTIVATEAPP` state tear down with result 0, earn no award, and resume ZPR |
| **Disk I total** | **132** | | |

## Disk II reconciliation

| Family | Files | Recovered trigger | Native boundary |
|---|---:|---|---|
| `TALENT/T<id><variant>` | 50 | Selected Talent performance | Rotation-selected normal clip |
| `TALENT/TI<id>` | 10 | Talent judge-cadet/impostor slot | Selected instead of the normal clip only for the Talent clue pavilion |
| `TALENT/TP<id>` | 10 | Correct Talent Penalty Box result | Modal response before an in-place chooser return; the prior numeric gauge is retained |
| `HOST/C21` | 1 | Talent pavilion entry | Disk-II root; its AVI matches Disk I but its companion WAV is intentionally different |
| `HOST/X<group><variant>` | 38 | Talent Judge-O-Matic score response | MMS-driven reaction from the active Disk II root |
| DAT-selected navigation backgrounds | 3 | Scene construction and directional/automatic ranges | Frame-range playback; only `XRDS2F` embedded PCM is range-gated |
| `NAV/T0`-`NAV/T10` | 11 | One-second dwell at unused DAT `0x0080` object | MMS-driven modal Simm encounter from the active Disk II root; overridden Cancel and either `WM_ACTIVATEAPP` state tear down with result 0, earn no award, and resume ZPR |
| `TALLY/W1`-`TALLY/W10` | 10 | Tally action 5 chooses a non-cadet winner | Shared centered modal paints blank for 100 ms, then starts the winner movie with embedded audio before replay/credit handling |
| `CREDIT/CREDIT` | 1 | Finale credit branch | Shared centered modal paints blank for 100 ms, then starts the closing movie with embedded audio; Enter/OK or completion exits, Escape/close is disabled |
| **Disk II total** | **134** | | |

## Ordering and audio invariants

- Ordinary and guest intro completion constructs navigation entry 13
  (`HALL.DAT`, variant 1) before first-round Cryo. Its action 13 enters Cryo
  directly; only the post-tally replay branch runs the five-iteration reset.
- Cryo owns its static chamber DIBs and begins `CRYO.WAV` before its 500 ms C11
  timer. C11 temporarily stops that legacy channel. After the modal returns,
  including failure, the same retained dialog restarts the loop, constructs and
  shows ROTATE at frame zero, then constructs INFO at frame zero but leaves it
  hidden. No contestant movie is constructed or decoded before C11.
- The recovered `0x00403fe7` helper is shared by both Cryo host calls. In exact
  order it stops legacy audio, allocates the generic media runner, calls mode 6
  with channel 1/take 1 for C11 or take 2 for C12, destroys that runner, and
  unconditionally restarts `CRYO.WAV`. C12 therefore finishes—including a
  synchronous opaque-sprite `OUCH.WAV` response from immediate click teardown—before first-sponsorship handling
  resumes, and a loader/allocation failure still restores Cryo ambience.
- Cryo browsing stops/seeks/shows visible INFO first, then stops/seeks ROTATE
  and attempts `CRYO.WAV` with `ASYNC | LOOP | NOSTOP`. An INFO detail click
  stops the old range, completes its synchronous `TT0`-`TT5` prompt, and only
  then starts the authored inclusive AVI interval with embedded PCM.
- The fifth sponsorship completes Cryo destruction before any outer-round
  work: ROTATE closes first, INFO and any active embedded PCM close second,
  then the shared `CRYO.WAV` channel stops and the owned DIBs are released in
  original order. Only after that boundary does `0x0040304e` draw and persist
  the five Brains plus five Talent variants. An INFO segment therefore cannot
  bleed into variant generation, Weight, or ORDER.
- `MMINTRO.AVI`, all ten `W*.AVI` winner movies, and `CREDIT.AVI` are three
  call sites of the same resource-158 object at `0x004012b5`. Each modal paints
  its blank centered 640×480 surface, waits for timer ID 1's 100 ms boundary,
  and only then kills the timer, constructs the style-`0x0A` movie child,
  applies 200% zoom to its 320×240 source, and starts embedded audio. The probe
  decodes all 4,970 frames across these twelve files; video outlasts PCM by
  0.03161–0.1 seconds, and the native cadence preserves each final visual tail.
  Successful notify, active OK, and active child-click completion all close and
  clear media before their caller continues. Winner completion exposes the
  black owner before the replay prompt or `END2.WAV`. Penalty and pavilion
  movies use separate handlers and start without this delay.
- Loading a DAT navigation AVI, pausing it, or seeking a single frame is silent.
  Only a bounded transition from `XRDS1F` or `XRDS2F` starts the corresponding
  11,025 Hz mono PCM interval. Completion, the 4.5-second slot boundary, and
  Skip Ahead stop it before the destination WAV; a skipped Slots play takes its
  aborted-notify result path immediately rather than waiting out the boundary.
  While that interval is live,
  its PCM sample cursor selects the corresponding absolute AVI frame; silent
  navigation backgrounds continue on the nominal frame clock.
- Destination WAVs run after the transition settles. A DAT `0x0800` node plays
  that cue before its automatic middle-range hop; `0x0100` is silent, and the
  retired online dialog side effect of `0x2000` is bypassed while its route is
  preserved. Together these use the recovered automatic mask `0x2900`.
- During a Talent/Brains performance, direct ACCEPT press is blocked but the
  Judge-O-Matic, GONG, and PENALTY controls remain live. The shared press latch
  still permits a Gong/Penalty-to-ACCEPT drag release; that exceptional ACCEPT
  path leaves the performance alive under X until its own notify. A meter click
  stops the performance, and Gong/Penalty capture whether they interrupted it
  for the C61/C71 gate.
  Penalty movies, host C interstitials, and X reactions resolve their owning modal
  boundary on completion, cancellation, or decode failure. The centered
  intro/winner/credit class uses MCIWnd style `0x0A`: open/decode failure is a
  blank modal with no error window until Enter/OK; Escape/close remains a no-op.
- Every click during a mode-6 C host clip ends that clip immediately. An opaque
  sprite hit additionally plays `OUCH.WAV` from the shared completion routine;
  transparent/background clicks dismiss silently. SPR.dll excludes a point
  exactly on any of the sprite's four borders before applying its top-left
  pixel color key; native host and Simm hit tests now preserve that one-pixel
  rule. Mode-4 X reactions ignore clicks because every reachable record carries
  `0x0080`.
- Intro, normal Talent/Brains performances, TP/BP/LP Penalty responses, winner
  movies, and credits select frames from their embedded PCM playback cursor,
  mirroring MCI's interleaved media clock. Delayed UI ticks seek to the authored
  frame instead of extending a clip by accumulating nominal frame sleeps. If
  PCM ends first, the authored visual tail continues at AVI cadence; this is
  about one second in the TP/BP responses and up to two frames in ordinary
  presentations.
- Host C interstitials use their 11,025 Hz companion WAV cursor as the video
  clock (`floor(samples * 10 / 11025) + 1`) and return at audio end; nominal AVI
  timing and any longer silent AVI tail do not extend the modal boundary.
- X reactions use the MMS graph as their completion boundary. The shared
  teardown stops the companion WAV immediately when that graph returns; it does
  not wait for a longer audio tail. Four authored WAVs outlast their graphs
  (`X14`, `X18`, `X211`, and `X212`), with X18 proving the largest 420 ms
  difference, so this distinction is observable and regression-locked. The
  original does not test the WaveMix open result for this companion; an audio
  failure leaves the X MMS/AVI response running silently, as does the native
  loader.
- C61 and C71 can be triggered independently by Talent or Brains, but their AVI
  files exist only in the Disk I tree. Their duplicate WAVs are byte-identical,
  so both event families deliberately resolve from the primary asset root. C21
  resolves from Disk II because its two installed WAV copies differ. The asset
  probe locks these ownership rules as well as the trigger timing.
- The X reaction table has 38 active clips. `X213.MMS` is an out-of-range script
  sentinel and `X312.MMS` is empty; neither names an installed X movie. They are
  distinct from the movie-only `X11DB` artifact.

`ms_metaverse_probe` enforces the two disc totals, the active family counts,
the `X11DB` classification, host-root ownership, navigation audio-bearing
backgrounds, trigger DAT nodes, and representative frame/audio decoding. The
more detailed recovered call boundaries remain in `analysis/function-parity.md`.
