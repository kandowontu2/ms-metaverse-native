#include "category_weight.hpp"
#include "comment_order.hpp"
#include "cryo_media.hpp"
#include "cryo_pavilion.hpp"
#include "game_data.hpp"
#include "game_state.hpp"
#include "host_interstitial.hpp"
#include "host_reaction.hpp"
#include "judging_pavilion.hpp"
#include "legacy_dialog.hpp"
#include "legacy_random.hpp"
#include "looks_pavilion.hpp"
#include "media_timing.hpp"
#include "motion_player.hpp"
#include "pavilion_rotation.hpp"
#include "profile_store.hpp"
#include "scene_navigator.hpp"
#include "slot_machine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void TestLegacyRandom() {
    metaverse::LegacyRandom random(1);
    constexpr std::array<std::int32_t, 5> expected{
        41, 18467, 6334, 26500, 19169
    };
    for (const std::int32_t value : expected) {
        Check(random.Next() == value,
              "Microsoft CRT rand sequence matches MM.EXE");
    }

    metaverse::LegacyRandom modulo(1);
    Check(modulo.Modulo(5) == 1 && modulo.Modulo(3) == 2,
          "legacy modulo selection consumes exact CRT outputs");
    metaverse::LegacyRandom scaled(1);
    Check(scaled.Scale(5) == 0 && scaled.Scale(5) == 2,
          "legacy scaled selection consumes exact CRT outputs");

    // These seeds make the next CRT output land exactly on values where a
    // 32768 integer divisor disagrees with MM.EXE's float 1/32767 constant.
    metaverse::LegacyRandom scale_three_boundary(1000994369u);
    Check(scale_three_boundary.Scale(3) == 2,
          "legacy scale-three boundary uses the original 1/32767 constant");
    metaverse::LegacyRandom scale_five_boundary(1684010561u);
    Check(scale_five_boundary.Scale(5) == 2,
          "legacy scale-five boundary uses the original 1/32767 constant");
    metaverse::LegacyRandom scale_jackpot_boundary(1284044353u);
    Check(scale_jackpot_boundary.Scale(50) == 37,
          "legacy scale-fifty boundary preserves the original jackpot draw");
    metaverse::LegacyRandom scale_maximum_boundary(0xf01bf641u);
    Check(scale_maximum_boundary.Scale(15) == 14,
          "legacy scaled draw remains below its range at rand maximum");
}

void TestLegacyDialogs() {
    using Action = metaverse::LegacyDialogDefaultAction;
    using Role = metaverse::LegacyDialogRole;
    Check(std::wstring(metaverse::kLegacyDialogCaption) == L"Ms. Metaverse",
          "legacy null-caption substitution uses resource 0xe000");
    Check(metaverse::kLegacyPlainMessageBoxStyle == 0x00 &&
              metaverse::kLegacyWarningMessageBoxStyle == 0x30 &&
              metaverse::kLegacyConfirmMessageBoxStyle == 0x24,
          "legacy message-box styles preserve original numeric flags");
    Check(metaverse::LegacyBitmapReadFailureMessage() ==
              L"Can't read bitmap file!",
          "legacy generic bitmap warning text");
    Check(metaverse::LegacyBitmapReadFailureMessage(L"bmp\\Brain\\7.bmp") ==
              L"Can't read bitmap file 'bmp\\Brain\\7.bmp'!",
          "legacy Brains meter warning includes attempted relative filename");
    Check(
        metaverse::LegacyDefaultRoutesFor(Role::profile).on_ok ==
                Action::no_op &&
            metaverse::LegacyDefaultRoutesFor(Role::profile).on_cancel ==
                Action::no_op,
        "painted profile default OK and Cancel commands are inert"
    );
    Check(
        metaverse::LegacyDefaultRoutesFor(Role::navigation_hub).on_ok ==
                Action::exit_without_confirmation &&
            metaverse::LegacyDefaultRoutesFor(Role::navigation_hub).on_cancel ==
                Action::confirm_exit,
        "navigation Enter exits directly while Cancel confirms"
    );
    Check(
        metaverse::LegacyDefaultRoutesFor(Role::order).on_ok == Action::no_op &&
            metaverse::LegacyDefaultRoutesFor(Role::order).on_cancel ==
                Action::confirm_then_continue_navigation,
        "ORDER ignores default OK and confirmed Cancel continues to navigation"
    );
    Check(
        metaverse::NativeDialogCancelActionFor(Role::order) ==
                Action::confirm_exit &&
            metaverse::NativeDialogCancelActionFor(Role::navigation_hub) ==
                Action::confirm_exit &&
            metaverse::NativeDialogCancelActionFor(Role::profile) ==
                Action::no_op,
        "native ORDER confirmation exits while other Cancel routes retain parity"
    );
    for (const Role role : {
             Role::cryo, Role::talent, Role::brains, Role::weight, Role::looks,
         }) {
        const auto routes = metaverse::LegacyDefaultRoutesFor(role);
        Check(
            routes.on_ok == Action::no_op &&
                routes.on_cancel == Action::confirm_exit,
            "gameplay dialog default routes match recovered vtables"
        );
    }
    using Activation = metaverse::LegacyDialogActivationAction;
    Check(
        metaverse::LegacyActivationActionFor(Role::cryo, false) ==
                Activation::repaint_controls &&
            metaverse::LegacyActivationActionFor(Role::talent, true) ==
                Activation::repaint_controls &&
            metaverse::LegacyActivationActionFor(Role::brains, false) ==
                Activation::repaint_controls,
        "Cryo/Talent/Brains rearm painter flags on both activation edges"
    );
    Check(
        metaverse::LegacyActivationActionFor(Role::generic_media, false) ==
                Activation::finish_generic_media &&
            metaverse::LegacyActivationActionFor(Role::generic_media, true) ==
                Activation::finish_generic_media,
        "generic media closes on either application activation edge"
    );
    Check(
        metaverse::LegacyActivationActionFor(Role::order, true) ==
                Activation::reload_order_phrases &&
            metaverse::LegacyActivationActionFor(Role::weight, true) ==
                Activation::reload_weight_gauges &&
            metaverse::LegacyActivationActionFor(Role::looks, true) ==
                Activation::show_looks_media &&
            metaverse::LegacyActivationActionFor(Role::order, false) ==
                Activation::none,
        "reactivation restores ORDER, Weight, and Looks dialog media"
    );
    using Cursor = metaverse::LegacyDialogCursorAction;
    Check(
        metaverse::LegacyCursorActionFor(Role::centered_one_shot) ==
                Cursor::set_hand_166 &&
            metaverse::LegacyCursorActionFor(Role::order) ==
                Cursor::set_hand_166 &&
            metaverse::LegacyCursorActionFor(Role::weight) ==
                Cursor::set_hand_166,
        "centered, ORDER, and Weight dialogs force cursor resource 166"
    );
    Check(
        metaverse::LegacyCursorActionFor(Role::cryo) ==
                Cursor::preserve_current &&
            metaverse::LegacyCursorActionFor(Role::talent) ==
                Cursor::preserve_current &&
            metaverse::LegacyCursorActionFor(Role::brains) ==
                Cursor::preserve_current &&
            metaverse::LegacyCursorActionFor(Role::navigation_hub) ==
                Cursor::preserve_current,
        "mouse-driven gameplay dialogs preserve their current cursor"
    );
    Check(
        metaverse::LegacyCursorActionFor(Role::generic_media) ==
                Cursor::set_generic_mode_cursor &&
            metaverse::LegacyGenericCursorResourceForMode(3) == 162 &&
            metaverse::LegacyGenericCursorResourceForMode(4) == 166 &&
            metaverse::LegacyGenericCursorResourceForMode(6) == 166,
        "generic mode 3 uses targeting cursor 162 while modes 4/6 use hand 166"
    );
    Check(
        metaverse::LegacyCursorActionFor(Role::looks) ==
                Cursor::set_looks_child_cursor &&
            metaverse::LegacyCursorActionFor(Role::profile) ==
                Cursor::use_window_default,
        "Looks owns its child magnifier and Profile keeps the dialog default"
    );
    using Shortcut = metaverse::NativeShortcutAction;
    Check(
        metaverse::NativeShortcutForKey(0x0d, true, false) ==
                Shortcut::toggle_fullscreen &&
            metaverse::NativeShortcutForKey(0x0d, true, true) ==
                Shortcut::toggle_fullscreen &&
            metaverse::NativeShortcutForKey(0x0d, false, false) ==
                Shortcut::none,
        "Alt+Enter is the exact native fullscreen shortcut"
    );
    Check(
        metaverse::NativeShortcutForKey(0x70, true, true) ==
                Shortcut::grant_credit_cheat &&
            metaverse::NativeShortcutForKey(0x70, true, false) ==
                Shortcut::none &&
            metaverse::NativeShortcutForKey(0x70, false, true) ==
                Shortcut::none &&
            metaverse::NativeShortcutForKey(0x71, true, true) ==
                Shortcut::none,
        "only Ctrl+Alt+F1 grants the native credit cheat"
    );
}

void TestNavigation() {
    std::istringstream input("2 null 0 xr1 3");
    std::vector<metaverse::NavigationEntry> entries;
    std::string error;
    Check(metaverse::ParseNavigationData(input, entries, error), "parse navigation table");
    Check(entries.size() == 2, "navigation count");
    Check(entries.size() >= 2 && entries[1].scene == "xr1" && entries[1].variant == 3,
          "navigation values");

    std::istringstream trailing("1 null 0 extra");
    Check(!metaverse::ParseNavigationData(trailing, entries, error),
          "reject trailing navigation token");

    Check(
        metaverse::ShouldPlayNavigationArrivalSound(0x0400, true) &&
            !metaverse::ShouldPlayNavigationArrivalSound(0x0800, true) &&
            metaverse::ShouldPlayNavigationArrivalSound(0x0400, false) &&
            metaverse::ShouldPlayNavigationArrivalSound(0x0800, false),
        "initial entry accepts only 0x0400 while later arrivals accept 0x0400/0x0800"
    );
    Check(
        metaverse::ShouldAdvanceNavigationOnInitialSceneEntry(0x0100) &&
            metaverse::ShouldAdvanceNavigationOnInitialSceneEntry(0x0900) &&
            !metaverse::ShouldAdvanceNavigationOnInitialSceneEntry(0x0800) &&
            !metaverse::ShouldAdvanceNavigationOnInitialSceneEntry(0x2000),
        "scene construction auto-routes only initial 0x0100 objects"
    );

    std::array<bool, 256> encountered{};
    encountered.fill(true);
    metaverse::ResetNavigationEncounterFlagsForScene(encountered, 3);
    Check(
        !encountered[0] && !encountered[1] && !encountered[2] &&
            encountered[3] && encountered.back(),
        "each DAT load clears only its parsed-object encounter prefix"
    );
    Check(
        metaverse::ClampNavigationPlaybackLastFrame(1686, 1686) == 1685 &&
            metaverse::ClampNavigationPlaybackLastFrame(878, 879) == 878 &&
            metaverse::ClampNavigationPlaybackLastFrame(42, 0) == 42,
        "MCI EOF stops clamp to the final zero-based decoder frame"
    );
}

void TestSimmSelectionTable() {
    metaverse::SimmSelectionTable table{};
    metaverse::ArmSimmSelectionPool(table, metaverse::kDemoContestantIds);
    Check(
        metaverse::RearmAndCountEligibleSimms(table) == 6,
        "Simm pool contains D0 plus five sponsored contestant IDs"
    );
    Check(
        metaverse::ConsumeEligibleSimm(table, 0) == 0 && table[0] == 2,
        "Simm pool consumes the selected eligible ordinal"
    );
    Check(
        metaverse::RearmAndCountEligibleSimms(table) == 5,
        "used Simm is suppressed for the active pass"
    );
    for (std::size_t ordinal = 0; ordinal < 5; ++ordinal) {
        Check(
            metaverse::ConsumeEligibleSimm(table, 0) >= 0,
            "consume remaining Simm selection"
        );
    }
    Check(
        metaverse::RearmAndCountEligibleSimms(table) == 6 && table[0] == 1,
        "exhausted Simm pool rearms every nonzero document entry"
    );

    constexpr std::array<std::int32_t, metaverse::kContestantsPerRound>
        second_round{1, 2, 6, 7, 9};
    for (std::size_t ordinal = 0; ordinal < 6; ++ordinal) {
        Check(
            metaverse::ConsumeEligibleSimm(table, 0) >= 0,
            "consume first-round Simm before replay"
        );
    }
    metaverse::ArmSimmSelectionPool(table, second_round);
    Check(
        metaverse::RearmAndCountEligibleSimms(table) == 6,
        "replay rearms only D0 and the newly sponsored IDs immediately"
    );
    for (std::size_t ordinal = 0; ordinal < 6; ++ordinal) {
        Check(
            metaverse::ConsumeEligibleSimm(table, 0) >= 0,
            "consume second-round Simm selection"
        );
    }
    Check(
        metaverse::RearmAndCountEligibleSimms(table) == 11,
        "recycled replay pool retains old nonzero contestant IDs"
    );

    metaverse::SimmSelectionTable every_entry{};
    every_entry.fill(1);
    for (std::size_t ordinal = 0; ordinal < every_entry.size(); ++ordinal) {
        auto selected = every_entry;
        Check(
            metaverse::ConsumeEligibleSimm(selected, ordinal) ==
                    static_cast<std::int32_t>(ordinal) &&
                selected[ordinal] == 2,
            "Simm selector maps every eligible ordinal to its table byte"
        );
    }
}

void TestHostInterstitialData() {
    std::string bytes;
    const auto append_i32 = [&bytes](std::int32_t value) {
        const std::uint32_t encoded = static_cast<std::uint32_t>(value);
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<char>((encoded >> shift) & 0xffU));
        }
    };
    for (std::int32_t index = 0;
         index < static_cast<std::int32_t>(metaverse::kHostPointCount);
         ++index) {
        append_i32(index * 16);
        append_i32(480 - index * 24);
    }

    Check(
        metaverse::PlanMode6HostClick(false) ==
                metaverse::Mode6HostClickOutcome{true, false} &&
            metaverse::PlanMode6HostClick(true) ==
                metaverse::Mode6HostClickOutcome{true, true},
        "every mode-6 click finishes while only a sprite hit queues OUCH"
    );

    metaverse::HostPointTable points{};
    std::string error;
    std::istringstream input(bytes);
    Check(metaverse::ParseHostPointData(input, points, error),
          "parse exact host position table");
    Check(points[5].x == 80 && points[5].y == 360,
          "preserve host position coordinates");
    Check(metaverse::HostClipStem(5, 1) == L"C51",
          "format original host clip stem");
    Check(metaverse::HostClipStem(10, 1).empty(),
          "reject invalid host clip channel");
    Check(metaverse::HostFrameForAudioSamples(0) == 1 &&
              metaverse::HostFrameForAudioSamples(1102) == 1 &&
              metaverse::HostFrameForAudioSamples(1103) == 2 &&
              metaverse::HostFrameForAudioSamples(11025) == 11,
          "mode-6 host frame stays one ahead of the 10-fps PCM cursor");
    Check(
        metaverse::PlanMode4HostCompletion(false, true) ==
                metaverse::Mode4HostCompletionOutcome{false, false} &&
            metaverse::PlanMode4HostCompletion(true, false) ==
                metaverse::Mode4HostCompletionOutcome{true, true} &&
            metaverse::PlanMode4HostCompletion(true, true) ==
                metaverse::Mode4HostCompletionOutcome{true, true},
        "mode-4 host graph completion closes immediately and stops any WAV tail"
    );

    bytes.push_back('\0');
    std::istringstream trailing(bytes);
    Check(!metaverse::ParseHostPointData(trailing, points, error),
          "reject trailing host position bytes");
}

void TestScene() {
    std::istringstream input(
        "mov/nav/test.avi none 1 "
        "7 0 1 2 3 4 5 6 8 9 10 11 "
        "42 43"
    );
    metaverse::SceneDefinition scene;
    std::string error;
    Check(metaverse::ParseSceneData(input, scene, error), "parse basic scene");
    Check(scene.objects.size() == 1, "scene object count");
    Check(scene.objects.size() == 1 && scene.objects[0].ranges[2][1] == 6,
          "scene preserves field order");
    Check(scene.special_object_indices.size() == 2 &&
              scene.special_object_indices[1] == 43,
          "scene special indices");
}

void TestConditionalSceneFields() {
    // 0x0c40 requests both a string asset and an extra coordinate pair.
    std::istringstream input(
        "bg.avi none 1 "
        "0 3136 0 0 0 0 0 0 0 0 0 0 wav/nav/test.wav 172 110"
    );
    metaverse::SceneDefinition scene;
    std::string error;
    Check(metaverse::ParseSceneData(input, scene, error), "parse conditional scene fields");
    Check(scene.objects.size() == 1 && scene.objects[0].asset == "wav/nav/test.wav",
          "conditional asset");
    Check(scene.objects.size() == 1 && scene.objects[0].point.has_value() &&
              (*scene.objects[0].point)[0] == 172 && (*scene.objects[0].point)[1] == 110,
          "conditional point");

    std::istringstream truncated("bg.avi none 1 0 0 0");
    Check(!metaverse::ParseSceneData(truncated, scene, error), "reject truncated object");

    std::istringstream omitted_auxiliary(
        "bg.avi none 1 "
        "17 1280 0 0 2465 2501 0 0 0 8 0 wav/nav/barsalon.wav"
    );
    Check(metaverse::ParseSceneData(omitted_auxiliary, scene, error),
          "accept original short asset record");
    Check(scene.objects.size() == 1 && scene.objects[0].auxiliary == 0 &&
              scene.objects[0].asset == "wav/nav/barsalon.wav",
          "short asset record compatibility values");
}

void TestMotionScript() {
    std::string bytes(88, '\0');
    const auto write_u16 = [&](std::size_t offset, std::uint16_t value) {
        bytes[offset] = static_cast<char>(value & 0xff);
        bytes[offset + 1] = static_cast<char>(value >> 8);
    };
    write_u16(0x0a, 44);
    write_u16(44 + 0x08, 7);
    std::istringstream input(bytes);
    metaverse::MotionScript script;
    std::string error;
    Check(metaverse::ParseMotionScript(input, script, error), "parse motion record graph");
    Check(script.records.size() == 2, "motion record graph count");
    Check(script.records.size() == 2 && script.records[1].terminal_frame == 7,
          "motion record fields");

    write_u16(0x0a, 86);
    std::istringstream invalid(bytes);
    Check(!metaverse::ParseMotionScript(invalid, script, error),
          "reject invalid motion transition");

    std::istringstream empty("");
    Check(metaverse::ParseMotionScript(empty, script, error) && script.records.empty(),
          "accept original empty motion placeholder shape");
}

void TestMotionPlayer() {
    Check(
        metaverse::MotionSpriteBoundsFromTopLeft(116, 452, 240, 320) ==
            metaverse::MotionSpriteBounds{116, 452, 356, 772},
        "SPR MMS coordinates are top-left rendering bounds"
    );
    const metaverse::MotionSpriteBounds legacy_hit_bounds{116, 452, 356, 772};
    Check(
        !metaverse::MotionSpritePointInsideLegacyHitBounds(
            legacy_hit_bounds, 116, 500
        ) &&
            !metaverse::MotionSpritePointInsideLegacyHitBounds(
                legacy_hit_bounds, 200, 452
            ) &&
            metaverse::MotionSpritePointInsideLegacyHitBounds(
                legacy_hit_bounds, 117, 453
            ) &&
            metaverse::MotionSpritePointInsideLegacyHitBounds(
                legacy_hit_bounds, 355, 771
            ) &&
            !metaverse::MotionSpritePointInsideLegacyHitBounds(
                legacy_hit_bounds, 356, 600
            ) &&
            !metaverse::MotionSpritePointInsideLegacyHitBounds(
                legacy_hit_bounds, 200, 772
            ),
        "SPR SpriteHitTest excludes every exact sprite border"
    );
    metaverse::MotionScript script;
    metaverse::MotionRecord moving;
    moving.file_offset = 0;
    moving.loop_frame = 0;
    moving.terminal_frame = 1;
    moving.hit_transition_offset = 44;
    moving.flags = 0x0248;
    moving.trajectory_points = {{{100, 200}}, {{110, 210}}};
    script.records.push_back(moving);

    metaverse::MotionRecord reaction;
    reaction.file_offset = 44;
    reaction.loop_frame = 1;
    reaction.terminal_frame = 3;
    reaction.flags = 0x0088;
    reaction.timer_interval = 200;
    reaction.result_code = 50;
    script.records.push_back(reaction);

    metaverse::MotionPlayer player;
    std::string error;
    Check(player.Reset(script, error) && player.Active() && player.Frame() == 0,
          "reset MMS runtime at root record");
    const std::uint64_t root_entry = player.RecordEntrySerial();
    Check(player.PlaysStateSound(),
          "0x0040 MMS root record starts its state voice");
    Check(player.StopsStateSoundOnHit(),
          "0x0040 MMS root record stops its voice before a hit transition");
    Check(player.Tick(error) && player.X() == 100 && player.Y() == 200 &&
              player.Frame() == 0,
          "single-frame MMS loop updates its absolute top-left position");
    Check(player.Hit(error) && player.Frame() == 1,
          "MMS hit follows the hit-transition record");
    Check(player.RecordEntrySerial() != root_entry,
          "MMS record entry serial changes even before a state sound is read");
    Check(player.LastEntryWasHit(),
          "MMS runtime distinguishes mouse-hit entry from timer entry");
    Check(player.CurrentRecordResultCode() == 50 && player.ResultCode() == 0,
          "MMS hit exposes its authored success sentinel before completion");
    Check(player.IgnoresMouseInput(),
          "0x0080 MMS reaction record ignores further mouse input");
    Check(!player.PlaysStateSound(),
          "silent MMS reaction does not restart the prior state voice");
    Check(!player.StopsStateSound(),
          "silent MMS reaction without 0x0020 leaves channel 3 alone");
    Check(player.TickDelayMilliseconds(2) == 160,
          "0x0080 MMS reaction forces eight two-tick delay units");
    Check(player.Tick(error) && player.Frame() == 2,
          "MMS reaction advances within its frame range");
    Check(player.Tick(error) && player.Complete() &&
              player.ResultCode() == 50,
          "exclusive MMS terminal publishes its original reward code");
    Check(player.CurrentRecordResultCode() == 0,
          "completed MMS has no longer-active record");

    metaverse::MotionScript relative_script;
    metaverse::MotionRecord relative;
    relative.file_offset = 0;
    relative.loop_frame = 0;
    relative.terminal_frame = 1;
    relative.flags = 0x0348;
    relative.initial_x = 300;
    relative.initial_y = 480;
    relative.trajectory_points = {{{-4, 16}}, {{7, -5}}};
    relative_script.records.push_back(relative);
    Check(player.Reset(relative_script, error) && player.Tick(error) &&
              player.X() == 296 && player.Y() == 496,
          "0x0100 MMS flag makes trajectory coordinates relative");
    Check(player.Tick(error) && player.X() == 303 && player.Y() == 491,
          "0x0100 MMS trajectory pairs accumulate from the current position");
    Check(!player.IgnoresMouseInput(),
          "ordinary moving MMS record accepts its authored hit input");
    Check(player.TickDelayMilliseconds(4) == 8,
          "ordinary zero-interval MMS motion uses the caller pacing factor");

    Check(player.Reset(script, error, 4) &&
              player.TickDelayMilliseconds(1) == 10,
          "generic-media mode 4 forces five pacing units");
    Check(player.Hit(error) && player.TickDelayMilliseconds(1) == 100,
          "mode 4 override follows the record timer interval");

    metaverse::MotionScript mode4_sync_script;
    metaverse::MotionRecord mode4_root;
    mode4_root.file_offset = 0;
    mode4_root.loop_frame = 0;
    mode4_root.terminal_frame = 20;
    mode4_root.timed_transition_offset = 44;
    mode4_root.flags = 0x0200;
    mode4_root.trajectory_points = {{{1, 0}}};
    mode4_sync_script.records.push_back(mode4_root);
    metaverse::MotionRecord mode4_reaction;
    mode4_reaction.file_offset = 44;
    mode4_reaction.loop_frame = 1;
    mode4_reaction.terminal_frame = 3;
    mode4_reaction.flags = 0x0208;
    mode4_reaction.trajectory_points = {
        {{0, 0}}, {{0, 0}}, {{0, 0}}, {{0, 0}}, {{0, 0}}
    };
    mode4_sync_script.records.push_back(mode4_reaction);
    Check(player.Reset(mode4_sync_script, error, 4) &&
              player.TickDelayMilliseconds(5) == 10 &&
              player.Tick(error) && player.Tick(error) &&
              player.Frame() == 1,
          "mode 4 path exhaustion enters its timed reaction record");
    Check(player.TickDelayMilliseconds(5) == 0 &&
              player.TickDelayMilliseconds(5) == 100,
          "mode 4 timed reaction captures its clock then uses 100 ms sync");
    Check(player.Tick(error) && player.Frame() == 2 &&
              player.TickDelayMilliseconds(5) == 100 &&
              player.Tick(error) && player.Frame() == 2 &&
              player.TickDelayMilliseconds(5) == 10,
          "mode 4 terminal hold restores fast pacing for the path tail");
    Check(
        player.Reset(mode4_sync_script, error, 4) && player.Tick(error) &&
            player.Tick(error) && player.Frame() == 1 &&
            player.TickDelayMilliseconds(5) == 0 &&
            player.UsesMode4TimedSynchronization() &&
            player.ApplyMode4TimedCatchUp(500) == 0 &&
            player.ApplyMode4TimedCatchUp(150) == 0 &&
            player.ApplyMode4TimedCatchUp(100'001) == 0 &&
            player.ApplyMode4TimedCatchUp(151) == 1 &&
            player.Frame() == 2,
        "mode 4 catch-up captures once, preserves overrun across stale clocks, and uses a strict threshold"
    );

    metaverse::MotionScript precedence_script;
    metaverse::MotionRecord precedence;
    precedence.file_offset = 0;
    precedence.loop_frame = 0;
    precedence.terminal_frame = 1;
    precedence.flags = 0x0303;
    precedence.initial_x = 10;
    precedence.initial_y = 20;
    precedence.delta_x = 3;
    precedence.delta_y = -2;
    precedence.trajectory_points = {{{100, 200}}};
    precedence_script.records.push_back(precedence);
    Check(player.Reset(precedence_script, error) && player.Tick(error) &&
              player.X() == 13 && player.Y() == 18,
          "0x0002 MMS deltas take precedence over a simultaneous path flag");

    metaverse::MotionScript timed_position_script;
    metaverse::MotionRecord timed_root;
    timed_root.file_offset = 0;
    timed_root.loop_frame = 0;
    timed_root.terminal_frame = 1;
    timed_root.timed_transition_offset = 44;
    timed_root.initial_x = 20;
    timed_root.initial_y = 30;
    timed_position_script.records.push_back(timed_root);
    metaverse::MotionRecord timed_target;
    timed_target.file_offset = 44;
    timed_target.flags = 0x0004;
    timed_target.initial_x = 100;
    timed_target.initial_y = 200;
    timed_position_script.records.push_back(timed_target);
    Check(player.Reset(timed_position_script, error) && player.Tick(error) &&
              player.X() == 200 && player.Y() == 30,
          "timed 0x0004 transition preserves the original X-overwrite quirk");
    Check(!player.LastEntryWasHit(),
          "MMS timer transition retains its distinct entry semantics");

    timed_root.timed_transition_offset = 0;
    timed_root.hit_transition_offset = 44;
    timed_position_script.records[0] = timed_root;
    Check(player.Reset(timed_position_script, error) && player.Hit(error) &&
              player.X() == 100 && player.Y() == 200,
          "hit 0x0004 transition assigns both authored coordinates");

    timed_position_script.records[1].flags = 0x0020;
    Check(player.Reset(timed_position_script, error) && player.Hit(error) &&
              player.PlaysStateSound() && player.StopsStateSound() &&
              !player.StopsStateSoundOnHit(),
          "0x0020 entry attempts its cue then stops channel 3 without hit-stop semantics");

    const auto escapes_legacy_canvas = [&](std::int16_t x, std::int16_t y) {
        metaverse::MotionScript escape_script;
        metaverse::MotionRecord escape;
        escape.file_offset = 0;
        escape.loop_frame = 0;
        escape.terminal_frame = 100;
        escape.flags = 0x0053;
        escape.initial_x = x;
        escape.initial_y = y;
        escape.result_code = 77;
        escape_script.records.push_back(escape);
        return player.Reset(escape_script, error) && player.Tick(error) &&
               player.Complete() && player.ResultCode() == 77;
    };
    Check(
        !escapes_legacy_canvas(-200, -200) &&
            !escapes_legacy_canvas(680, 5'000) &&
            escapes_legacy_canvas(-201, 0) &&
            escapes_legacy_canvas(0, -201) &&
            escapes_legacy_canvas(681, 0),
        "delta MMS escape uses strict low-X/low-Y/high-X bounds and no high-Y bound"
    );

    metaverse::MotionScript exact_delta_script;
    metaverse::MotionRecord exact_delta;
    exact_delta.file_offset = 0;
    exact_delta.loop_frame = 0;
    exact_delta.terminal_frame = 100;
    exact_delta.flags = 0x0153;
    exact_delta.initial_x = 680;
    exact_delta.delta_x = 1;
    exact_delta.result_code = 81;
    exact_delta_script.records.push_back(exact_delta);
    Check(
        player.Reset(exact_delta_script, error) && player.Tick(error) &&
            player.Complete() && player.ResultCode() == 81,
        "real 0x0153 D0 delta records retain the 0x0002 escape path"
    );

    metaverse::MotionScript exact_timer_script;
    metaverse::MotionRecord exact_timer_root;
    exact_timer_root.file_offset = 0;
    exact_timer_root.loop_frame = 0;
    exact_timer_root.terminal_frame = 1;
    exact_timer_root.timed_transition_offset = 44;
    exact_timer_root.flags = 0x0180;
    exact_timer_script.records.push_back(exact_timer_root);
    metaverse::MotionRecord exact_timer_result;
    exact_timer_result.file_offset = 44;
    exact_timer_result.loop_frame = 0;
    exact_timer_result.terminal_frame = 1;
    exact_timer_result.flags = 0x0188;
    exact_timer_result.result_code = 82;
    exact_timer_script.records.push_back(exact_timer_result);
    Check(
        player.Reset(exact_timer_script, error) && player.Tick(error) &&
            player.Active() && !player.LastEntryWasHit() &&
            player.Tick(error) && player.Complete() &&
            player.ResultCode() == 82,
        "real 0x0180/0x0188 D0 timer-only records transition then complete"
    );

    metaverse::MotionScript exact_path_script;
    metaverse::MotionRecord exact_path_root;
    exact_path_root.file_offset = 0;
    exact_path_root.loop_frame = 0;
    exact_path_root.terminal_frame = 100;
    exact_path_root.timed_transition_offset = 44;
    exact_path_root.flags = 0x0280;
    exact_path_root.trajectory_points = {{{10, 20}}};
    exact_path_script.records.push_back(exact_path_root);
    metaverse::MotionRecord exact_path_result;
    exact_path_result.file_offset = 44;
    exact_path_result.loop_frame = 0;
    exact_path_result.terminal_frame = 100;
    exact_path_result.flags = 0x0288;
    exact_path_result.result_code = 83;
    exact_path_result.trajectory_points = {{{30, 40}}};
    exact_path_script.records.push_back(exact_path_result);
    Check(
        player.Reset(exact_path_script, error) && player.Tick(error) &&
            player.X() == 10 && player.Y() == 20 && player.Tick(error) &&
            player.X() == 10 && player.Y() == 20 && player.Tick(error) &&
            player.X() == 30 && player.Y() == 40 && player.Tick(error) &&
            player.Complete() && player.ResultCode() == 83,
        "real 0x0280/0x0288 absolute paths transition or complete only after exhaustion"
    );

    metaverse::MotionScript exact_relative_result_script;
    metaverse::MotionRecord exact_relative_result;
    exact_relative_result.file_offset = 0;
    exact_relative_result.loop_frame = 0;
    exact_relative_result.terminal_frame = 100;
    exact_relative_result.flags = 0x0388;
    exact_relative_result.initial_x = 5;
    exact_relative_result.initial_y = 6;
    exact_relative_result.result_code = 84;
    exact_relative_result.trajectory_points = {{{2, 3}}};
    exact_relative_result_script.records.push_back(exact_relative_result);
    Check(
        player.Reset(exact_relative_result_script, error) &&
            player.Tick(error) && player.X() == 7 && player.Y() == 9 &&
            player.Tick(error) && player.Complete() &&
            player.ResultCode() == 84,
        "real 0x0388 relative result path accumulates then completes on exhaustion"
    );

    metaverse::MotionScript mode5_pacing_script;
    metaverse::MotionRecord mode5_root;
    mode5_root.file_offset = 0;
    mode5_root.loop_frame = 0;
    mode5_root.terminal_frame = 100;
    mode5_root.hit_transition_offset = 44;
    mode5_pacing_script.records.push_back(mode5_root);
    metaverse::MotionRecord mode5_linked;
    mode5_linked.file_offset = 44;
    mode5_linked.loop_frame = 0;
    mode5_linked.terminal_frame = 100;
    mode5_pacing_script.records.push_back(mode5_linked);
    Check(
        player.Reset(mode5_pacing_script, error, 5) &&
            player.TickDelayMilliseconds(4) == 20 && player.Hit(error) &&
            player.TickDelayMilliseconds(4) == 2,
        "mode 5 fallback paces its root at ten units and linked records at one"
    );
}

void TestSceneNavigator() {
    Check(metaverse::ShouldPlayNavigationDepartureSound(0x0007),
          "ordinary navigation departure plays ZPR");
    Check(metaverse::ShouldPlayNavigationDepartureSound(0x4007),
          "slot navigation departure plays ZPR before the reel channel");
    Check(!metaverse::ShouldPlayNavigationDepartureSound(0x0107) &&
              !metaverse::ShouldPlayNavigationDepartureSound(0x0807) &&
              !metaverse::ShouldPlayNavigationDepartureSound(0x0907),
          "automatic and 0x0800 sound-bearing nodes suppress ZPR");
    metaverse::SceneDefinition scene;
    scene.objects.resize(4);
    for (std::size_t index = 0; index < scene.objects.size(); ++index) {
        scene.objects[index].index = static_cast<std::int32_t>(index);
    }
    scene.objects[1].flags = 0x07;
    scene.objects[1].ranges = {{{10, 11}, {20, 21}, {30, 31}}};
    scene.objects[1].links = {2, 0, 1};
    scene.objects[3].ranges[1] = {40, 41};
    scene.objects[3].links[1] = 1;
    scene.special_object_indices = {1};

    metaverse::SceneNavigator navigator;
    std::string error;
    Check(navigator.Reset(scene, 1, error), "initialize scene navigator");
    Check(navigator.CurrentObject() != nullptr && navigator.CurrentObject()->index == 1,
          "scene variant entry node");
    metaverse::SceneTransition transition;
    Check(navigator.Move(metaverse::SceneDirection::right, transition, error),
          "move right through scene");
    Check(transition.first_frame == 10 && transition.last_frame == 11 &&
              transition.from_object == 1 && transition.to_object == 2,
          "right branch range and link");
    Check(!navigator.CanMove(metaverse::SceneDirection::up),
          "target node has no movement flags");
    scene.objects[2].ranges[1] = {32, 33};
    scene.objects[2].links[1] = 3;
    Check(
        navigator.AdvanceMiddleUnchecked(transition, error) &&
            transition.direction == metaverse::SceneDirection::up &&
            transition.first_frame == 32 && transition.last_frame == 33 &&
            transition.from_object == 2 && transition.to_object == 3,
        "stale enabled command 206 forces the middle branch without Up flags"
    );
    Check(!navigator.Reset(scene, 2, error), "reject missing scene variant");

    scene.special_object_indices = {3};
    for (const std::uint32_t automatic_flag : {0x0100u, 0x0800u, 0x2000u}) {
        scene.objects[3].flags = automatic_flag;
        Check(navigator.Reset(scene, 1, error),
              "initialize automatic scene entry");
        Check(navigator.CanAdvanceAutomatically(),
              "0x0100/0x0800/0x2000 object advances automatically");
        Check(navigator.AdvanceAutomatically(transition, error) &&
                  transition.first_frame == 40 &&
                  transition.last_frame == 41 &&
                  transition.from_object == 3 && transition.to_object == 1,
              "automatic object uses the middle range/link pair");
    }

    Check(
        metaverse::CrossSceneTransitionForFlags(0x0200) ==
            metaverse::CrossSceneTransition::immediate,
        "ordinary 0x0200 cross-scene node switches immediately"
    );
    Check(
        metaverse::CrossSceneTransitionForFlags(0x1200) ==
            metaverse::CrossSceneTransition::fade_to_white,
        "0x1000 cross-scene node selects the palette-to-white path"
    );
    Check(metaverse::NavigationPaletteFadePassForProgress(0.0) == 0,
          "navigation palette fade begins before the first pass");
    Check(metaverse::NavigationPaletteFadePassForProgress(0.5) == 99,
          "navigation palette fade maps time to the recovered pass count");
    Check(metaverse::NavigationPaletteFadePassForProgress(1.0) == 199,
          "navigation palette fade ends after exactly 199 passes");
    Check(
        metaverse::ApplyNavigationPaletteFadeToColor(0x7f000000u, 1) ==
            0x7f0a0a0au,
        "first palette pass adds ten to all three channels"
    );
    Check(
        metaverse::ApplyNavigationPaletteFadeToColor(0x0001f600u, 1) ==
            0x00fff60au,
        "legacy green saturation branch writes red and preserves green"
    );
    Check(
        metaverse::ApplyNavigationPaletteFadeToColor(0x00000000u, 199) ==
            0x00fffafau,
        "legacy repeated palette passes retain their off-white saturation"
    );
    Check(metaverse::IsPlayableNavigationAsset("wav/nav/test.wav"),
          "normal navigation WAV is playable");
    Check(!metaverse::IsPlayableNavigationAsset("0"),
          "XR2 literal zero asset is a silent sentinel");
    Check(!metaverse::IsPlayableNavigationAsset("0legacy.wav"),
          "navigation constructor treats every leading-zero asset as silent");
    Check(
        metaverse::OneShotTransitionForFlags(0x0110, false) ==
            metaverse::OneShotNavigationTransition::animated_first_pass,
        "0x0010 navigation node animates on its first pass"
    );
    Check(
        metaverse::OneShotTransitionForFlags(0x0110, true) ==
            metaverse::OneShotNavigationTransition::held_frame_after_first_pass,
        "0x0010 navigation node holds the destination frame on later passes"
    );
    Check(
        metaverse::OneShotTransitionForFlags(0x0100, true) ==
            metaverse::OneShotNavigationTransition::ordinary,
        "ordinary automatic navigation remains animated"
    );
    metaverse::NavigationPopupOneShotState navigation_one_shot_state;
    navigation_one_shot_state.audio_reactivation_pending = true;
    navigation_one_shot_state.transition_completed = true;
    navigation_one_shot_state.ResetForConstruction();
    Check(
        !navigation_one_shot_state.audio_reactivation_pending &&
            !navigation_one_shot_state.transition_completed,
        "each reconstructed cross-scene popup rearms the 0x0010 transition"
    );
    Check(
        metaverse::NavigationArrivalHoldFrame(0x0120) == 25 &&
            !metaverse::NavigationArrivalHoldFrame(0x0100),
        "0x0020 arrival seeks the background movie to frame 25"
    );
    Check(
        metaverse::kNavigationIdleAnimationObjects ==
            std::array<std::size_t, 4>{19, 20, 21, 22} &&
            metaverse::kNavigationTimerIntervalMs == 500 &&
            metaverse::kNavigationEncounterDelayMs == 1'000 &&
            metaverse::kNavigationIdleAnimationDelayMs == 10'000,
        "0x0020 delayed animation uses XR1 objects 19-22 after ten seconds"
    );
    const auto navigation_timer_origin =
        std::chrono::steady_clock::time_point{};
    Check(
        !metaverse::NavigationTimerThresholdPassed(
            navigation_timer_origin + std::chrono::milliseconds(1'000),
            navigation_timer_origin + std::chrono::milliseconds(1'000)
        ) &&
            metaverse::NavigationTimerThresholdPassed(
                navigation_timer_origin + std::chrono::milliseconds(1'001),
                navigation_timer_origin + std::chrono::milliseconds(1'000)
            ) &&
            !metaverse::NavigationTimerThresholdPassed(
                navigation_timer_origin + std::chrono::milliseconds(10'000),
                navigation_timer_origin + std::chrono::milliseconds(10'000)
            ),
        "navigation media thresholds are strict and wait for the next 500 ms tick"
    );
    metaverse::NavigationPopupIdleLatch popup_idle_latch;
    Check(
        !popup_idle_latch.SuppressesIdleAnimation(),
        "new navigation scene starts with popup idle suppression clear"
    );
    popup_idle_latch.MarkPopupRequested();
    Check(
        popup_idle_latch.SuppressesIdleAnimation(),
        "right-click popup latches 0x0020 idle-animation suppression"
    );
    popup_idle_latch.ResetForTransition();
    Check(
        !popup_idle_latch.SuppressesIdleAnimation(),
        "the next navigation transition clears popup idle suppression"
    );
    Check(
        metaverse::NavigationSkipAheadEnabled(true, false, true) &&
            metaverse::NavigationSkipAheadEnabled(false, true, true) &&
            !metaverse::NavigationSkipAheadEnabled(false, true, false) &&
            !metaverse::NavigationSkipAheadEnabled(false, false, false),
        "Skip Ahead follows active MCI rather than the remaining Slots deadline"
    );
    Check(
        metaverse::NavigationSkipDispositionForState(false, true, true) ==
                metaverse::NavigationSkipDisposition::reveal_slot_result &&
            metaverse::NavigationSkipDispositionForState(true, false, true) ==
                metaverse::NavigationSkipDisposition::complete_transition &&
            metaverse::NavigationSkipDispositionForState(false, true, false) ==
                metaverse::NavigationSkipDisposition::none,
        "Skip's aborted-play notify reveals Slots immediately and otherwise resolves arrival"
    );
    Check(
        metaverse::NavigationLeftButtonInterruptsPlayback(true, false, true) &&
            metaverse::NavigationLeftButtonInterruptsPlayback(
                false, true, true
            ) &&
            !metaverse::NavigationLeftButtonInterruptsPlayback(
                false, true, false
            ) &&
            !metaverse::NavigationLeftButtonInterruptsPlayback(
                false, false, true
            ),
        "navigation left-down interrupts exactly the active MCI states"
    );
    Check(
        metaverse::kHallMsMetaverseEntry == 13 &&
            metaverse::kHallLastEntry == 16 &&
            !metaverse::NavigationContextMenuSuppressed(12) &&
            metaverse::NavigationContextMenuSuppressed(13) &&
            metaverse::NavigationContextMenuSuppressed(16) &&
            !metaverse::NavigationContextMenuSuppressed(17),
        "ordinary post-intro Hall starts at entry 13 and suppresses menus through entry 16"
    );
    Check(
        metaverse::kNavigationPopupCommands ==
                std::array<metaverse::NavigationPopupCommand, 6>{{
                    {200, L"Talent"},
                    {201, L"Brains"},
                    {202, L"Looks"},
                    {204, L"Slots"},
                    {205, L"Exit"},
                    {206, L"Skip Ahead"},
                }} &&
            metaverse::kNavigationPopupSkipPosition == 5,
        "navigation popup preserves the constructor's command order and Skip position"
    );
    Check(
        metaverse::kNavigationMenuTallyCommand == 203 &&
            std::none_of(
                metaverse::kNavigationPopupCommands.begin(),
                metaverse::kNavigationPopupCommands.end(),
                [](const metaverse::NavigationPopupCommand& command) {
                    return command.command ==
                        metaverse::kNavigationMenuTallyCommand;
                }
            ),
        "hidden navigation command 203 remains dispatchable but absent from the popup"
    );
    std::array<bool, 4> idle_used{};
    Check(
        metaverse::ConsumeNavigationIdleAnimation(idle_used, 2) == 2 &&
            metaverse::ConsumeNavigationIdleAnimation(idle_used, 2) == 3 &&
            metaverse::ConsumeNavigationIdleAnimation(idle_used, 3) == 0 &&
            metaverse::ConsumeNavigationIdleAnimation(idle_used, 0) == 1 &&
            std::none_of(idle_used.begin(), idle_used.end(), [](bool value) {
                return value;
            }),
        "idle animation walks past used entries and clears after all four"
    );
    metaverse::SceneObject one_shot_destination;
    one_shot_destination.ranges[1] = {73, 99};
    Check(
        metaverse::RepeatedOneShotDestinationFrame(one_shot_destination) == 73,
        "repeated 0x0010 traversal uses the destination middle-range start"
    );
    Check(
        metaverse::SceneDirectionAtPointer(0x07, 20, 20) ==
            metaverse::SceneDirection::up,
        "top half gives enabled up direction priority"
    );
    Check(
        metaverse::SceneDirectionAtPointer(0x07, 319, 240) ==
            metaverse::SceneDirection::left,
        "left half boundary selects left"
    );
    Check(
        !metaverse::SceneDirectionAtPointer(0x07, 320, 240),
        "exact horizontal midpoint has no direction"
    );
    Check(
        metaverse::SceneDirectionAtPointer(0x07, 321, 240) ==
            metaverse::SceneDirection::right,
        "right half boundary selects right"
    );
    Check(
        metaverse::SceneDirectionAtPointer(0x03, 100, 100) ==
            metaverse::SceneDirection::left,
        "top half falls back to horizontal direction when up is disabled"
    );
    Check(
        !metaverse::SceneDirectionAtPointer(0x07, 20, 20, true),
        "active navigation MCI forces neutral cursor state regardless of pointer"
    );
    for (std::uint32_t flags = 0; flags <= 0x07; ++flags) {
        for (const int x : {319, 320, 321}) {
            for (const int y : {239, 240}) {
                std::optional<metaverse::SceneDirection> expected;
                if (y < 240 && (flags & 0x04) != 0) {
                    expected = metaverse::SceneDirection::up;
                } else if (x < 320 && (flags & 0x02) != 0) {
                    expected = metaverse::SceneDirection::left;
                } else if (x > 320 && (flags & 0x01) != 0) {
                    expected = metaverse::SceneDirection::right;
                }
                Check(
                    metaverse::SceneDirectionAtPointer(flags, x, y) ==
                        expected,
                    "navigation pointer split matches every low-flag boundary"
                );
            }
        }
    }
}

void TestLegacyProfiles() {
    std::vector<metaverse::PlayerProfile> profiles = {
        {"PLAYER", "SECRET", 375.5f, false},
        metaverse::MakeGuestProfile(),
    };
    std::ostringstream encoded;
    std::string error;
    Check(metaverse::WriteLegacyProfiles(encoded, profiles, error),
          "serialize legacy profiles");
    Check(encoded.str().size() == 28, "guest is omitted from 28-byte profile records");

    std::istringstream input(encoded.str());
    std::vector<metaverse::PlayerProfile> decoded;
    Check(metaverse::ParseLegacyProfiles(input, decoded, error), "parse legacy profiles");
    Check(decoded.size() == 1 && decoded[0].name == "PLAYER" &&
              decoded[0].password == "SECRET",
          "legacy credential layout");
    Check(decoded.size() == 1 && decoded[0].credits == 375.5f,
          "profile parsing preserves an unselected stored balance");
    const auto selected = metaverse::ActivateReturningProfile(decoded[0]);
    Check(selected.credits == 400.0f && decoded[0].credits == 375.5f,
          "only the authenticated returning profile receives the minimum");
    Check(metaverse::IsPersistentProfile(decoded[0]) &&
              !metaverse::IsPersistentProfile(metaverse::MakeGuestProfile()) &&
              !metaverse::IsPersistentProfile({"DEMO", "BUYVV", 500.0f, false}),
          "final profile save excludes the original GUEST and DEMO sessions");
    Check(
        metaverse::kDemoContestantIds ==
            std::array<std::int32_t, metaverse::kContestantsPerRound>{
                3, 4, 5, 8, 10
            },
        "DEMO shortcut preserves the original five contestant IDs"
    );
    Check(
        metaverse::kOrchestratorInitialCommentMapping ==
            std::array<std::int32_t, 10>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
        "top orchestrator installs the exact pre-intro identity comment table"
    );
    Check(decoded.size() == 1 && metaverse::AuthenticateProfile(decoded[0], "SECRET") &&
              !metaverse::AuthenticateProfile(decoded[0], "WRONG"),
          "profile authentication");

    using Login = metaverse::ProfileLoginDisposition;
    const std::vector<metaverse::PlayerProfile> login_profiles = {
        {"PLAYER", "FIRST", 350.0f, false},
        {"PLAYER", "SECOND", 700.0f, false},
        {"DEMO", "BUYVV", 325.0f, false},
    };
    Check(
        metaverse::EvaluateProfileLogin(login_profiles, "", "").disposition ==
            Login::invalid_name &&
        metaverse::EvaluateProfileLogin(login_profiles, "GUEST", "X").disposition ==
            Login::guest_password_not_allowed &&
        metaverse::EvaluateProfileLogin(login_profiles, "GUEST", "").disposition ==
            Login::guest &&
        metaverse::EvaluateProfileLogin(login_profiles, "DEMO", "").disposition ==
            Login::demo_password_wrong &&
        metaverse::EvaluateProfileLogin(login_profiles, "NEW", "").disposition ==
            Login::password_required,
        "profile validation preserves empty/GUEST/DEMO/password warning order"
    );
    const auto duplicate_wrong =
        metaverse::EvaluateProfileLogin(login_profiles, "PLAYER", "SECOND");
    const auto duplicate_right =
        metaverse::EvaluateProfileLogin(login_profiles, "PLAYER", "FIRST");
    Check(
        duplicate_wrong.disposition == Login::existing_password_wrong &&
            duplicate_wrong.existing_profile_index == 0 &&
            duplicate_right.disposition == Login::existing_profile &&
            duplicate_right.existing_profile_index == 0 &&
            metaverse::EvaluateProfileLogin({}, "DEMO", "BUYVV").disposition ==
                Login::new_demo &&
            metaverse::EvaluateProfileLogin({}, "NEW", "PASSWORD").disposition ==
                Login::new_profile,
        "profile scan stops at the first matching record and preserves new DEMO/user paths"
    );
    const std::vector<metaverse::PlayerProfile> ten_character_profile = {
        {"TENCHARS10", "PASSW0RD10", 500.0f, false},
    };
    const auto long_credential_login = metaverse::EvaluateProfileLogin(
        ten_character_profile,
        "TENCHARS10-IGNORED-SUFFIX",
        "PASSW0RD10-IGNORED-SUFFIX"
    );
    Check(
        long_credential_login.disposition == Login::existing_profile &&
            long_credential_login.existing_profile_index == 0,
        "profile scan compares the first ten edit characters without imposing an edit limit"
    );
    const auto long_credential_selection = metaverse::ActivateReturningProfile(
        ten_character_profile[0],
        "TENCHARS10-IGNORED-SUFFIX",
        "PASSW0RD10-IGNORED-SUFFIX"
    );
    Check(
        long_credential_selection.name == "TENCHARS10-IGNORED-SUFFIX" &&
            long_credential_selection.password ==
                "PASSW0RD10-IGNORED-SUFFIX" &&
            long_credential_selection.credits == 500.0f,
        "authenticated login retains the complete resource-edit credentials"
    );

    std::string tolerant_raw(28, '\0');
    std::fill_n(tolerant_raw.begin(), 11, 'X');
    const std::string valid_name = "VALID";
    const std::string valid_password = "PASSWORD";
    std::string valid_record(28, '\0');
    std::copy(valid_name.begin(), valid_name.end(), valid_record.begin());
    std::copy(
        valid_password.begin(), valid_password.end(), valid_record.begin() + 11
    );
    const std::uint32_t valid_credit_bits = std::bit_cast<std::uint32_t>(625.25f);
    for (std::size_t byte = 0; byte < 4; ++byte) {
        valid_record[24 + byte] = static_cast<char>(
            (valid_credit_bits >> (byte * 8)) & 0xff
        );
    }
    tolerant_raw += valid_record;
    tolerant_raw += "damaged-tail";
    std::istringstream tolerant_input(tolerant_raw);
    std::vector<metaverse::PlayerProfile> tolerant_profiles;
    Check(
        metaverse::ParseLegacyProfiles(tolerant_input, tolerant_profiles, error) &&
            tolerant_profiles.size() == 2 &&
            metaverse::FindProfile(tolerant_profiles, "VALID") != nullptr &&
            metaverse::FindProfile(tolerant_profiles, "VALID")->credits == 625.25f,
        "profile scan ignores unrelated malformed records and a trailing fragment"
    );

    constexpr std::uint32_t positive_nan_bits = 0x7fc01234;
    constexpr std::uint32_t negative_nan_bits = 0xffc01234;
    const metaverse::PlayerProfile positive_nan_profile{
        "EDGE", "PASSWORD", std::bit_cast<float>(positive_nan_bits), false
    };
    const metaverse::PlayerProfile negative_nan_profile{
        "EDGE", "PASSWORD", std::bit_cast<float>(negative_nan_bits), false
    };
    Check(
        std::bit_cast<std::uint32_t>(
            metaverse::ActivateReturningProfile(positive_nan_profile).credits
        ) == positive_nan_bits &&
            metaverse::ActivateReturningProfile(negative_nan_profile).credits ==
                metaverse::kReturningProfileMinimumCredits,
        "returning-credit floor uses the original signed raw-bit comparison"
    );
    std::ostringstream nonfinite_output;
    Check(
        metaverse::WriteLegacyProfiles(
            nonfinite_output, {positive_nan_profile}, error
        ) &&
            nonfinite_output.str().size() == 28 &&
            static_cast<std::uint8_t>(nonfinite_output.str()[24]) == 0x34 &&
            static_cast<std::uint8_t>(nonfinite_output.str()[25]) == 0x12 &&
            static_cast<std::uint8_t>(nonfinite_output.str()[26]) == 0xc0 &&
            static_cast<std::uint8_t>(nonfinite_output.str()[27]) == 0x7f,
        "profile serialization preserves non-finite legacy credit bits"
    );

    std::vector<metaverse::PlayerProfile> stored_demo_profiles = {
        {"DEMO", "BUYVV", 325.0f, false},
    };
    const auto* stored_demo = metaverse::FindProfile(stored_demo_profiles, "DEMO");
    Check(stored_demo != nullptr &&
              metaverse::AuthenticateProfile(*stored_demo, "BUYVV") &&
              metaverse::ActivateReturningProfile(*stored_demo).credits == 400.0f &&
              !metaverse::IsPersistentProfile(*stored_demo),
          "stored DEMO authenticates and restores its floored balance but remains unsaved");

    Check(!metaverse::ValidateNewProfile("GUEST", "PASSWORD", error),
          "guest name is reserved");
    Check(metaverse::ValidateNewProfile("PLAYER2", "PASSWORD", error),
          "valid new profile credentials");
    Check(
        metaverse::ValidateNewProfile(
            "LONG-PROFILE-NAME", "LONG-PROFILE-PASSWORD", error
        ),
        "resource 167 permits long credentials before legacy record truncation"
    );

    std::vector<metaverse::PlayerProfile> two_profiles = {
        {"PLAYER", "SECRET", 375.5f, false},
        {"OTHER", "PASSWORD", 123.25f, false},
    };
    std::ostringstream two_encoded;
    Check(metaverse::WriteLegacyProfiles(two_encoded, two_profiles, error),
          "serialize targeted-update profile fixture");
    std::string raw = two_encoded.str();
    raw[28 + 22] = static_cast<char>(0xa5);
    raw[28 + 23] = static_cast<char>(0x5a);
    const auto targeted_path = std::filesystem::temp_directory_path() /
        "ms-metaverse-targeted-profile-test.dat";
    {
        std::ofstream fixture(targeted_path, std::ios::binary | std::ios::trunc);
        fixture.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    }
    two_profiles[0].credits = 612.25f;
    Check(metaverse::SaveLegacyProfile(targeted_path, two_profiles[0], error),
          "targeted profile update succeeds");
    std::ifstream updated_file(targeted_path, std::ios::binary);
    const std::string updated{
        std::istreambuf_iterator<char>(updated_file),
        std::istreambuf_iterator<char>()
    };
    Check(updated.size() == 56 && updated.substr(28, 28) == raw.substr(28, 28),
          "targeted profile update preserves every unrelated record byte");
    std::error_code cleanup_error;
    std::filesystem::remove(targeted_path, cleanup_error);

    const auto long_targeted_path = std::filesystem::temp_directory_path() /
        "ms-metaverse-long-targeted-profile-test.dat";
    std::ostringstream long_targeted_encoded;
    Check(
        metaverse::WriteLegacyProfiles(
            long_targeted_encoded, ten_character_profile, error
        ),
        "serialize long-credential targeted-update fixture"
    );
    const std::string long_targeted_raw = long_targeted_encoded.str();
    {
        std::ofstream fixture(
            long_targeted_path, std::ios::binary | std::ios::trunc
        );
        fixture.write(
            long_targeted_raw.data(),
            static_cast<std::streamsize>(long_targeted_raw.size())
        );
    }
    const metaverse::PlayerProfile long_targeted{
        "TENCHARS10-IGNORED-SUFFIX",
        "PASSW0RD10-IGNORED-SUFFIX",
        777.25f,
        false,
    };
    Check(
        metaverse::SaveLegacyProfile(long_targeted_path, long_targeted, error),
        "long credentials update their Left(10)-matched legacy record"
    );
    std::ifstream long_updated_file(long_targeted_path, std::ios::binary);
    const std::string long_updated{
        std::istreambuf_iterator<char>(long_updated_file),
        std::istreambuf_iterator<char>()
    };
    Check(
        long_updated.size() == 28 &&
            long_updated.substr(0, 10) == "TENCHARS10" &&
            long_updated.substr(11, 10) == "PASSW0RD10",
        "targeted save truncates long credentials and does not append a duplicate"
    );
    cleanup_error.clear();
    std::filesystem::remove(long_targeted_path, cleanup_error);

    const auto damaged_save_path = std::filesystem::temp_directory_path() /
        "ms-metaverse-damaged-profile-save-test.dat";
    const std::string damaged_tail = "damaged-tail";
    {
        std::ofstream fixture(
            damaged_save_path, std::ios::binary | std::ios::trunc
        );
        fixture.write(
            damaged_tail.data(), static_cast<std::streamsize>(damaged_tail.size())
        );
    }
    Check(
        metaverse::SaveLegacyProfile(
            damaged_save_path,
            {"PLAYER", "SECRET", 501.0f, false},
            error
        ),
        "targeted save appends after a damaged trailing fragment"
    );
    std::ifstream damaged_saved_file(damaged_save_path, std::ios::binary);
    const std::string damaged_saved{
        std::istreambuf_iterator<char>(damaged_saved_file),
        std::istreambuf_iterator<char>()
    };
    Check(
        damaged_saved.size() == damaged_tail.size() + 28 &&
            damaged_saved.starts_with(damaged_tail),
        "targeted save preserves damaged trailing bytes before its appended record"
    );
    cleanup_error.clear();
    std::filesystem::remove(damaged_save_path, cleanup_error);

    const auto empty_path = std::filesystem::temp_directory_path() /
        "ms-metaverse-empty-profile-test.dat";
    cleanup_error.clear();
    std::filesystem::remove(empty_path, cleanup_error);
    std::vector<metaverse::PlayerProfile> empty_profiles = {
        {"STALE", "VALUE", 1.0f, false},
    };
    Check(metaverse::LoadLegacyProfiles(empty_path, empty_profiles, error) &&
              empty_profiles.empty() && std::filesystem::exists(empty_path) &&
              std::filesystem::file_size(empty_path) == 0,
          "missing MM.DAT is created empty before the login dialog");
    cleanup_error.clear();
    std::filesystem::remove(empty_path, cleanup_error);
}

void TestCategoryWeights() {
    Check(
        metaverse::kCategoryWeightOkBitmap == L"ok.bmp" &&
            metaverse::kCategoryWeightInitialGaugeBitmaps ==
                std::array<std::wstring_view, 3>{
                    L"th1_1.bmp", L"th2_1.bmp", L"th3_1.bmp"
                } &&
            metaverse::kCategoryWeightControlBitmaps ==
                std::array<std::wstring_view, 6>{
                    L"down1.bmp", L"up1.bmp", L"down2.bmp", L"up2.bmp",
                    L"down3.bmp", L"up3.bmp"
                },
        "Weight constructor uses the exact OK, hidden TH1, and arrow load order"
    );
    Check(metaverse::CategoryWeightControlAtPointer(39, 218) == 0 &&
              metaverse::CategoryWeightControlAtPointer(78, 266) == 0 &&
              metaverse::CategoryWeightControlAtPointer(130, 200) == 1 &&
              metaverse::CategoryWeightControlAtPointer(613, 247) == 5,
          "Weight arrows include their recovered top-left and inner edges");
    Check(!metaverse::CategoryWeightControlAtPointer(79, 266) &&
              !metaverse::CategoryWeightControlAtPointer(78, 267) &&
              !metaverse::CategoryWeightControlAtPointer(129, 200) &&
              !metaverse::CategoryWeightControlAtPointer(614, 247),
          "Weight arrows exclude the Win32 right and bottom rectangle edges");
    Check(metaverse::CategoryWeightOkAtPointer(269, 413) &&
              metaverse::CategoryWeightOkAtPointer(373, 469) &&
              !metaverse::CategoryWeightOkAtPointer(374, 469) &&
              !metaverse::CategoryWeightOkAtPointer(373, 470),
          "Weight OK uses the exact native 105-by-57 bitmap rectangle");

    for (std::size_t control = 0;
         control < metaverse::kCategoryWeightControlCount; ++control) {
        for (std::int32_t initial = 0; initial <= 10; ++initial) {
            std::array<std::int32_t, 3> values{4, 5, 6};
            const std::size_t category = control / 2;
            values[category] = initial;
            const auto before = values;
            const auto adjustment = metaverse::AdjustCategoryWeightControl(
                values, control
            );
            const std::int32_t expected = (control % 2) == 0
                ? std::max<std::int32_t>(0, initial - 1)
                : std::min<std::int32_t>(10, initial + 1);
            Check(
                adjustment && adjustment->category == category &&
                    adjustment->value == expected &&
                    values[category] == expected,
                "Weight dispatcher returns the exact gauge and TTn value"
            );
            for (std::size_t other = 0; other < values.size(); ++other) {
                if (other != category) {
                    Check(
                        values[other] == before[other],
                        "Weight dispatcher mutates only its selected category"
                    );
                }
            }
        }
    }
    std::array<std::int32_t, 3> invalid_adjustment{5, 5, 5};
    Check(
        !metaverse::AdjustCategoryWeightControl(
            invalid_adjustment, metaverse::kCategoryWeightControlCount
        ) && invalid_adjustment == std::array<std::int32_t, 3>{5, 5, 5},
        "native guard rejects an unreachable Weight control without mutation"
    );

    for (std::size_t category = 0;
         category < metaverse::kJudgingCategoryCount; ++category) {
        const auto& position =
            metaverse::kCategoryWeightGaugePositions[category];
        const auto& size = metaverse::kCategoryWeightGaugeSizes[category];
        for (std::int32_t value = 0; value <= 10; ++value) {
            const auto plan = metaverse::PlanCategoryWeightGauge(
                category, value
            );
            const std::wstring expected_file = value == 0
                ? L""
                : L"th" + std::to_wstring(category + 1) + L"_" +
                      std::to_wstring(value) + L".bmp";
            Check(
                plan && plan->category == category &&
                    plan->value == value &&
                    plan->replace_existing_bitmap == (value != 0) &&
                    plan->bitmap_filename == expected_file,
                "Weight gauge plan matches zero-retain and TH filename branches"
            );
            Check(
                plan && plan->dirty_rect.left == position.x &&
                    plan->dirty_rect.top == position.y &&
                    plan->dirty_rect.right == position.x + size.width &&
                    plan->dirty_rect.bottom == position.y + size.height,
                "Weight gauge plan exposes the exact recovered repaint rectangle"
            );
        }
    }
    Check(
        !metaverse::PlanCategoryWeightGauge(
            metaverse::kJudgingCategoryCount, 5
        ),
        "native guard rejects an unreachable Weight gauge category"
    );

    metaverse::CategoryWeightPainterState painter{
        true, true, true, {true, true, true}, 1
    };
    const auto first_paint = metaverse::ConsumeCategoryWeightPaint(painter, 4);
    Check(
        first_paint.draw_base && first_paint.draw_ok &&
            first_paint.control == 4 && first_paint.gauge == 1,
        "Weight painter consumes base, OK, selected arrow, and current gauge"
    );
    Check(
        !painter.base_dirty && !painter.ok_dirty && !painter.control_dirty &&
            painter.gauge_dirty == std::array<bool, 3>{true, false, true},
        "Weight painter leaves noncurrent gauge dirty flags pending"
    );
    painter.current_gauge = 2;
    const auto second_paint = metaverse::ConsumeCategoryWeightPaint(painter, -1);
    Check(
        !second_paint.draw_base && !second_paint.draw_ok &&
            !second_paint.control && second_paint.gauge == 2 &&
            painter.gauge_dirty == std::array<bool, 3>{true, false, false},
        "Weight painter later consumes only the newly current gauge"
    );
    painter.control_dirty = true;
    const auto invalid_control_paint = metaverse::ConsumeCategoryWeightPaint(
        painter, static_cast<std::int32_t>(metaverse::kCategoryWeightControlCount)
    );
    Check(
        !invalid_control_paint.control && !painter.control_dirty,
        "native painter guard consumes an unreachable invalid control safely"
    );

    metaverse::CategoryWeights weights;
    std::string error;
    Check(metaverse::NormalizeCategoryWeights({1, 1, 1}, weights, error),
          "normalize category controls");
    Check(weights.looks == 33 && weights.brains == 33 && weights.talent == 34 &&
              weights.Sum() == 100,
          "category percentages use truncated first two values and a remainder");
    Check(metaverse::NormalizeCategoryWeights({10, 0, 10}, weights, error) &&
              weights.looks == 50 && weights.brains == 0 && weights.talent == 50,
          "normalize uneven category controls");
    weights = {17, 29, 54};
    Check(!metaverse::NormalizeCategoryWeights({0, 0, 0}, weights, error) &&
              error == "weight the categories please" &&
              weights.looks == 17 && weights.brains == 29 &&
              weights.talent == 54,
          "all-zero category warning preserves the existing document weights");
    Check(!metaverse::NormalizeCategoryWeights({11, 0, 0}, weights, error),
          "reject out-of-range category control");

    for (std::int32_t looks = 0; looks <= 10; ++looks) {
        for (std::int32_t brains = 0; brains <= 10; ++brains) {
            for (std::int32_t talent = 0; talent <= 10; ++talent) {
                const std::int32_t total = looks + brains + talent;
                if (total == 0) {
                    continue;
                }
                Check(
                    metaverse::NormalizeCategoryWeights(
                        {looks, brains, talent}, weights, error
                    ) &&
                        weights.looks == (looks * 100) / total &&
                        weights.brains == (brains * 100) / total &&
                        weights.talent ==
                            100 - (looks * 100) / total -
                            (brains * 100) / total &&
                        weights.Sum() == 100,
                    "all reachable Weight commits match the x87 truncation"
                );
            }
        }
    }
}

void TestCommentOrder() {
    Check(
        metaverse::kCommentStaticBitmapLoadOrder ==
            std::array<std::wstring_view, 3>{
                L"ACCEPT.BMP", L"TH10.BMP", L"HAND.BMP"
            },
        "ORDER constructor preserves ACCEPT/TH10/HAND wrapper order"
    );
    constexpr auto phrase_nine = metaverse::CommentPhraseBitmapRect(9);
    constexpr auto hand_zero = metaverse::CommentHandRect(0);
    constexpr auto hand_nine = metaverse::CommentHandRect(9);
    constexpr auto hand_union = metaverse::UnionCommentRects(
        hand_zero, hand_nine
    );
    constexpr auto thermometer = metaverse::CommentThermometerRect();
    constexpr auto rank_nine = metaverse::CommentRankRect(9);
    Check(
        phrase_nine.left == 308 && phrase_nine.top == 400 &&
            phrase_nine.right == 622 && phrase_nine.bottom == 432 &&
            hand_union.left == 215 && hand_union.top == 30 &&
            hand_union.right == 300 && hand_union.bottom == 465 &&
            thermometer.left == 88 && thermometer.top == 30 &&
            thermometer.right == 156 && thermometer.bottom == 362 &&
            rank_nine.left == 317 && rank_nine.top == 405 &&
            rank_nine.right == 334 && rank_nine.bottom == 435,
        "ORDER dirty rectangles match COMMENT/HAND/TH/rank DIB geometry"
    );

    metaverse::CommentPainterState painter{
        true, true, true, true, true, true, 7, 4
    };
    const auto paint = metaverse::ConsumeCommentPaint(painter);
    Check(
        paint.draw_base && paint.draw_accept && paint.comment == 7 &&
            paint.draw_hand && paint.draw_thermometer &&
            paint.rank_comment == 4 && !painter.base_dirty &&
            !painter.accept_dirty && !painter.comment_dirty &&
            !painter.hand_dirty && !painter.thermometer_dirty &&
            !painter.rank_dirty,
        "ORDER painter consumes base/ACCEPT/COMMENT/HAND/TH/rank in order"
    );
    painter.comment_dirty = true;
    painter.rank_dirty = true;
    painter.current_comment = metaverse::kCommentCount;
    painter.current_rank_comment = -1;
    const auto guarded_paint = metaverse::ConsumeCommentPaint(painter);
    Check(
        !guarded_paint.comment && !guarded_paint.rank_comment &&
            !painter.comment_dirty && !painter.rank_dirty,
        "ORDER painter safely consumes unreachable invalid row selectors"
    );
    Check(metaverse::CommentPhraseAtPointer(308, 40) == 0 &&
              metaverse::CommentPhraseAtPointer(620, 69) == 0,
          "ORDER first phrase includes the original rectangle corners");
    Check(!metaverse::CommentPhraseAtPointer(621, 40) &&
              !metaverse::CommentPhraseAtPointer(308, 70) &&
              !metaverse::CommentPhraseAtPointer(308, 79),
          "ORDER phrase hitbox excludes its right edge and ten-pixel row gap");
    Check(metaverse::CommentPhraseAtPointer(308, 80) == 1 &&
              metaverse::CommentPhraseAtPointer(620, 429) == 9 &&
              !metaverse::CommentPhraseAtPointer(620, 430),
          "ORDER phrase hitboxes preserve the 40-pixel pitch through row ten");
    Check(metaverse::CommentAcceptAtPointer(21, 381) &&
              metaverse::CommentAcceptAtPointer(216, 471) &&
              !metaverse::CommentAcceptAtPointer(217, 471) &&
              !metaverse::CommentAcceptAtPointer(216, 472),
          "ORDER ACCEPT uses its exact 196-by-91 logical rectangle");

    metaverse::CommentOrderState order;
    std::array<std::int32_t, metaverse::kCommentCount> mapping{};
    mapping.fill(-1);
    std::string error;

    Check(order.selected_comment == 0 && order.next_rank == 10 &&
              !order.Complete(),
          "comment board starts at the first phrase and rank ten");
    const auto staged = order.BeginSelectedAssignment(error);
    Check(
        staged && staged->rank == 10 && staged->comment == 0 &&
            staged->mapping_index == 9 && staged->mapping_value == 9 &&
            mapping[9] == -1 && order.assigned_rank_by_comment[0] == 10 &&
            order.next_rank == 9,
        "ORDER stages rank state before its delayed document-table commit"
    );
    metaverse::CommentOrderState::CommitAssignment(*staged, mapping);
    Check(mapping[9] == 9, "ORDER commits the staged mapping after media work");

    order = metaverse::CommentOrderState{};
    mapping.fill(-1);
    Check(order.AssignSelected(mapping, error), "assign first comment phrase");
    Check(order.assigned_rank_by_comment[0] == 10 && order.next_rank == 9 &&
              mapping[9] == 9,
          "comment assignment uses reversed asset code at rank-minus-one");
    Check(!order.AssignSelected(mapping, error) && order.next_rank == 9,
          "duplicate comment assignment is rejected without consuming a rank");

    order.MoveSelection(-1);
    Check(order.selected_comment == 9, "comment selection wraps upward");
    Check(order.AssignSelected(mapping, error) && mapping[8] == 0,
          "last visual phrase maps to original comment code zero");

    for (std::int32_t comment = 1; comment <= 8; ++comment) {
        order.selected_comment = comment;
        Check(order.AssignSelected(mapping, error), "assign remaining comment phrase");
    }
    Check(order.Complete() && order.next_rank == 0,
          "ten unique comment choices complete the ordering board");
    Check(!order.AssignSelected(mapping, error),
          "completed comment board rejects additional assignments");

    std::string judge_path;
    Check(
        metaverse::BuildJudgeCommentPath(
            'B', 10, 5, mapping, judge_path, error
        ) && judge_path == "wav/comment/b95.wav",
        "Brains rating uses ORDER mapping and one-based judging slot"
    );
    Check(
        metaverse::BuildJudgeCommentPath(
            'l', 1, 1, mapping, judge_path, error
        ) && judge_path == "wav/comment/l11.wav",
        "Looks rating uses the first ORDER entry"
    );
    Check(
        metaverse::BuildJudgeCommentPath(
            't', 0, 3, mapping, judge_path, error
        ) && judge_path == "wav/tt0.wav",
        "zero rating uses the original generic lowest-response sound"
    );
    Check(
        metaverse::BuildJudgeCommentPath(
            't', 1, 1,
            metaverse::kOrchestratorInitialCommentMapping,
            judge_path,
            error
        ) && judge_path == "wav/comment/t01.wav" &&
            metaverse::BuildJudgeCommentPath(
                't', 10, 5,
                metaverse::kOrchestratorInitialCommentMapping,
                judge_path,
                error
            ) && judge_path == "wav/comment/t95.wav",
        "DEMO identity table selects the exact low/high host comment files"
    );
    Check(
        !metaverse::BuildJudgeCommentPath(
            'b', 11, 1, mapping, judge_path, error
        ) && !error.empty(),
        "out-of-range judge rating is rejected"
    );
    auto invalid_mapping = mapping;
    invalid_mapping[4] = 10;
    Check(
        !metaverse::BuildJudgeCommentPath(
            'b', 5, 1, invalid_mapping, judge_path, error
        ) && !error.empty(),
        "invalid ORDER mapping is rejected"
    );
}

void TestRoundTally() {
    metaverse::GameRoundState cheat_round;
    cheat_round.credits = 12.5f;
    metaverse::ApplyNativeCreditCheat(cheat_round);
    Check(cheat_round.credits == metaverse::kNativeCreditCheatBalance &&
              cheat_round.credits == 999999.0f,
          "native credit cheat assigns the exact requested live balance");
    metaverse::ApplyNativeCreditCheat(cheat_round);
    Check(cheat_round.credits == 999999.0f,
          "native credit cheat is idempotent");

    Check(metaverse::kReplayCreditThreshold == 400.0f,
          "post-tally replay prompt uses the original 400-credit threshold");
    Check(!metaverse::ReplayCreditThresholdReached(399.999f) &&
              metaverse::ReplayCreditThresholdReached(400.0f) &&
              metaverse::ReplayCreditThresholdReached(400.001f),
          "post-tally replay branch retains its exact inclusive boundary");
    const float negative_zero = std::bit_cast<float>(0x80000000U);
    const float positive_nan = std::bit_cast<float>(0x7fc00001U);
    const float negative_nan = std::bit_cast<float>(0xffc00001U);
    Check(std::bit_cast<std::uint32_t>(
              metaverse::ClampJudgeCadetBalance(negative_zero)
          ) == 0x80000000U &&
              std::bit_cast<std::uint32_t>(
                  metaverse::ClampJudgeCadetBalance(-1.0f)
              ) == 0U &&
              std::bit_cast<std::uint32_t>(
                  metaverse::ClampJudgeCadetBalance(positive_nan)
              ) == 0x7fc00001U &&
              std::bit_cast<std::uint32_t>(
                  metaverse::ClampJudgeCadetBalance(negative_nan)
              ) == 0U,
          "judge-cadet stake clamp reproduces the executable's unsigned DWORD compare");
    constexpr std::array<std::wstring_view, 10> exact_contestant_names{
        L"Queen", L"Nancy", L"Suzi", L"Conchita", L"Jane",
        L"Dee", L"Sammy", L"Rhonda", L"Jackie", L"Domina",
    };
    constexpr std::array<float, 10> exact_sponsorship_costs{
        100.0f, 250.0f, 1000.0f, 250.0f, 150.0f,
        50.0f, 50.0f, 500.0f, 50.0f, 150.0f,
    };
    Check(metaverse::kContestantNames == exact_contestant_names,
          "contestant IDs retain the exact MM.EXE name order");
    Check(metaverse::kContestantSponsorCosts == exact_sponsorship_costs,
          "contestant IDs retain the exact MM.EXE sponsorship costs");
    const metaverse::GameRoundState initialized_round;
    constexpr std::array<std::int32_t, 10> exact_initial_comment_mapping{
        9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    };
    bool exact_initial_contestants = true;
    for (const auto& contestant : initialized_round.contestants) {
        exact_initial_contestants = exact_initial_contestants &&
            contestant.contestant_id == 0 &&
            contestant.ratings ==
                std::array<std::uint8_t, metaverse::kJudgingCategoryCount>{} &&
            contestant.pending ==
                std::array<bool, metaverse::kJudgingCategoryCount>{
                    true, true, true
                } &&
            !contestant.disqualified;
    }
    Check(
        exact_initial_contestants &&
            initialized_round.weights.looks == 0 &&
            initialized_round.weights.brains == 0 &&
            initialized_round.weights.talent == 0 &&
            initialized_round.comment_for_rating ==
                exact_initial_comment_mapping &&
            initialized_round.credits == 0.0f &&
            initialized_round.accumulated_reward == 0.0f,
        "round document retains the exact zero/pending and reverse ORDER constructor state"
    );
    constexpr std::array<
        std::array<metaverse::CryoInfoRange, 6>, 10
    > exact_cryo_info_ranges{{
        {{{0, 460}, {461, 710}, {711, 830}, {831, 970}, {971, 1170}, {1171, 1260}}},
        {{{1265, 1560}, {1561, 1620}, {1701, 1850}, {1851, 1960}, {1961, 2060}, {2061, 2108}}},
        {{{2111, 2330}, {2331, 2430}, {2501, 2560}, {2561, 2610}, {2611, 2680}, {2681, 2727}}},
        {{{2731, 3080}, {3081, 3190}, {3191, 3260}, {3261, 3360}, {3361, 3430}, {3431, 3510}}},
        {{{3531, 3850}, {3851, 4070}, {4071, 4210}, {4211, 4290}, {4291, 4400}, {4401, 4490}}},
        {{{4495, 4770}, {4771, 4920}, {4921, 5050}, {5051, 5140}, {5141, 5190}, {5191, 5234}}},
        {{{5237, 5620}, {5621, 5700}, {5701, 5830}, {5831, 5880}, {5881, 5960}, {5961, 6062}}},
        {{{6063, 6300}, {6421, 6480}, {6581, 6700}, {6701, 6760}, {6761, 6840}, {6841, 6915}}},
        {{{6918, 7400}, {7401, 7580}, {7581, 7740}, {7741, 7890}, {7891, 7970}, {7971, 8041}}},
        {{{8042, 8270}, {8341, 8420}, {8501, 8590}, {8591, 8690}, {8691, 8780}, {8781, 8830}}},
    }};
    bool all_cryo_info_ranges_match =
        metaverse::kCryoInfoFramesPerSecond == 10;
    for (std::size_t contestant = 0;
         contestant < exact_cryo_info_ranges.size(); ++contestant) {
        for (std::size_t detail = 0;
             detail < exact_cryo_info_ranges[contestant].size(); ++detail) {
            const auto plan = metaverse::PlanCryoInfoPlayback(
                static_cast<std::int32_t>(contestant + 1), detail
            );
            const auto expected = exact_cryo_info_ranges[contestant][detail];
            all_cryo_info_ranges_match = all_cryo_info_ranges_match && plan &&
                plan->detail == detail &&
                plan->range.first_frame == expected.first_frame &&
                plan->range.last_frame == expected.last_frame &&
                plan->range.Valid();
        }
    }
    using CryoStep = metaverse::CryoInfoPlaybackStep;
    constexpr std::array<CryoStep, 4> exact_cryo_info_order{{
        CryoStep::stop_info_child,
        CryoStep::play_prompt_synchronously,
        CryoStep::seek_first_frame,
        CryoStep::play_to_last_frame,
    }};
    Check(
        all_cryo_info_ranges_match &&
            metaverse::kCryoInfoPlaybackOrder == exact_cryo_info_order &&
            !metaverse::PlanCryoInfoPlayback(0, 0) &&
            !metaverse::PlanCryoInfoPlayback(11, 0) &&
            !metaverse::PlanCryoInfoPlayback(1, 6),
        "all 60 Cryo INFO triggers and stop/prompt/seek/play order match MM.EXE"
    );
    const auto first_quarter = metaverse::NextCryoRotationSegment(1, 0);
    Check(first_quarter.first_frame == 0 && first_quarter.last_frame == 9 &&
              first_quarter.next_frame == 10 &&
              first_quarter.HasForwardPlayback(),
          "Cryo ROTATE advances one ten-frame quarter-turn per click");
    const auto fourth_quarter = metaverse::NextCryoRotationSegment(1, 30);
    Check(fourth_quarter.first_frame == 30 &&
              fourth_quarter.last_frame == 39 &&
              fourth_quarter.next_frame == 40,
          "Cryo ROTATE completes the contestant's fourth quarter-turn");
    const auto early_wrap = metaverse::NextCryoRotationSegment(6, 240);
    Check(early_wrap.first_frame == 200 && early_wrap.last_frame == 209 &&
              early_wrap.next_frame == 210 &&
              early_wrap.HasForwardPlayback(),
          "Cryo contestant IDs one through six wrap to their first quarter");
    const auto late_wrap = metaverse::NextCryoRotationSegment(7, 280);
    Check(late_wrap.first_frame == 250 && late_wrap.last_frame == 249 &&
              late_wrap.next_frame == 250 &&
              !late_wrap.HasForwardPlayback(),
          "Cryo contestant IDs seven through ten preserve the original wrap seek quirk");
    Check(!metaverse::NextCryoRotationSegment(0, 0).Valid(),
          "Cryo ROTATE rejects an invalid contestant ID");

    bool exhaustive_cryo_rotation = true;
    for (std::int32_t contestant_id = 1; contestant_id <= 10;
         ++contestant_id) {
        const std::int64_t base =
            static_cast<std::int64_t>(contestant_id - 1) * 40;
        for (std::int64_t quarter = 0; quarter < 40; quarter += 10) {
            const auto segment = metaverse::NextCryoRotationSegment(
                contestant_id, base + quarter
            );
            exhaustive_cryo_rotation = exhaustive_cryo_rotation &&
                segment.first_frame == base + quarter &&
                segment.last_frame == base + quarter + 9 &&
                segment.next_frame == base + quarter + 10 &&
                segment.HasForwardPlayback();
        }
        const auto wrapped = metaverse::NextCryoRotationSegment(
            contestant_id, base + 40
        );
        if (contestant_id <= 6) {
            exhaustive_cryo_rotation = exhaustive_cryo_rotation &&
                wrapped.first_frame == base &&
                wrapped.last_frame == base + 9 &&
                wrapped.next_frame == base + 10 &&
                wrapped.HasForwardPlayback();
        } else {
            exhaustive_cryo_rotation = exhaustive_cryo_rotation &&
                wrapped.first_frame == base + 10 &&
                wrapped.last_frame == base + 9 &&
                wrapped.next_frame == base + 10 &&
                !wrapped.HasForwardPlayback();
        }
    }
    Check(
        exhaustive_cryo_rotation &&
            !metaverse::NextCryoRotationSegment(11, 0).Valid(),
        "all forty reachable Cryo quarter-turns and all ten wraps match MM.EXE"
    );

    using CryoAction = metaverse::CryoAction;
    bool cryo_control_targets_exact = true;
    for (std::size_t index = 0;
         index < metaverse::kCryoControlRects.size(); ++index) {
        const auto& rect = metaverse::kCryoControlRects[index];
        const CryoAction expected = static_cast<CryoAction>(
            static_cast<std::int32_t>(CryoAction::browse_left) +
            static_cast<std::int32_t>(index)
        );
        const auto down = metaverse::PlanCryoMouseDown(
            CryoAction::none, false, rect.left, rect.top, false
        );
        const auto inside_release = metaverse::PlanCryoMouseUp(
            expected, false, rect.right - 1, rect.bottom - 1
        );
        const auto outside_release = metaverse::PlanCryoMouseUp(
            expected, false, rect.right, rect.bottom - 1
        );
        cryo_control_targets_exact = cryo_control_targets_exact &&
            down.action_after == expected &&
            down.force_synchronous_repaint &&
            inside_release.clear_action &&
            inside_release.repaint_controls_before_dispatch &&
            inside_release.dispatch &&
            !inside_release.repaint_information_after_dispatch &&
            !outside_release.dispatch;
    }
    Check(
        cryo_control_targets_exact,
        "all five Cryo controls preserve exclusive hit edges and same-target release"
    );

    bool cryo_detail_targets_exact = true;
    for (std::size_t index = 0;
         index < metaverse::kCryoDetailRects.size(); ++index) {
        const auto& rect = metaverse::kCryoDetailRects[index];
        const CryoAction expected = static_cast<CryoAction>(
            static_cast<std::int32_t>(CryoAction::name_bio) +
            static_cast<std::int32_t>(index)
        );
        const auto down = metaverse::PlanCryoMouseDown(
            CryoAction::none, true, rect.left, rect.top, false
        );
        const auto hidden_down = metaverse::PlanCryoMouseDown(
            CryoAction::rotate, false, rect.left, rect.top, false
        );
        const auto inside_release = metaverse::PlanCryoMouseUp(
            expected, true, rect.right - 1, rect.bottom - 1
        );
        const auto outside_release = metaverse::PlanCryoMouseUp(
            expected, true, rect.right, rect.bottom - 1
        );
        const auto hidden_release = metaverse::PlanCryoMouseUp(
            expected, false, rect.left, rect.top
        );
        cryo_detail_targets_exact = cryo_detail_targets_exact &&
            down.action_after == expected &&
            hidden_down.action_after == CryoAction::rotate &&
            inside_release.dispatch &&
            inside_release.repaint_information_after_dispatch &&
            !outside_release.dispatch &&
            outside_release.repaint_information_after_dispatch &&
            !hidden_release.dispatch &&
            !hidden_release.repaint_information_after_dispatch;
    }
    Check(
        cryo_detail_targets_exact,
        "all six Cryo detail labels are visibility-gated and repaint after any release"
    );

    const auto retained_cryo_press = metaverse::PlanCryoMouseDown(
        CryoAction::sponsor, false, 639, 479, false
    );
    const auto hidden_cash = metaverse::PlanCryoMouseDown(
        CryoAction::none, false,
        metaverse::kCryoCashRect.left,
        metaverse::kCryoCashRect.top,
        true
    );
    const auto cash_right_edge = metaverse::PlanCryoMouseDown(
        CryoAction::none, false,
        metaverse::kCryoCashRect.right,
        metaverse::kCryoCashRect.top,
        true
    );
    Check(
        retained_cryo_press.action_after == CryoAction::sponsor &&
            hidden_cash.grant_hidden_cash &&
            !cash_right_edge.grant_hidden_cash &&
            metaverse::kCryoCashControlClickAward == 100.0f,
        "Cryo mouse-down retains an old outside press and preserves the Ctrl+$100 target"
    );

    using BrowseStep = metaverse::CryoBrowseStep;
    constexpr std::array<BrowseStep, 6> browse_hidden_steps{{
        BrowseStep::select_wrapped_contestant,
        BrowseStep::stop_rotation,
        BrowseStep::seek_rotation_front,
        BrowseStep::start_ambient_without_interrupting,
        BrowseStep::store_rotation_marker,
        BrowseStep::repaint_candidate_cost,
    }};
    constexpr std::array<BrowseStep, 9> browse_visible_steps{{
        BrowseStep::select_wrapped_contestant,
        BrowseStep::stop_information,
        BrowseStep::seek_information_name_bio,
        BrowseStep::show_information,
        BrowseStep::stop_rotation,
        BrowseStep::seek_rotation_front,
        BrowseStep::start_ambient_without_interrupting,
        BrowseStep::store_rotation_marker,
        BrowseStep::repaint_candidate_cost,
    }};
    bool all_cryo_browse_paths = true;
    for (std::int32_t contestant_id = 1; contestant_id <= 10;
         ++contestant_id) {
        for (const std::int32_t direction : {-1, 1}) {
            const std::int32_t expected_id = direction < 0
                ? (contestant_id == 1 ? 10 : contestant_id - 1)
                : (contestant_id == 10 ? 1 : contestant_id + 1);
            const auto hidden = metaverse::PlanCryoBrowse(
                contestant_id, direction, false
            );
            const auto visible = metaverse::PlanCryoBrowse(
                contestant_id, direction, true
            );
            all_cryo_browse_paths = all_cryo_browse_paths && hidden && visible;
            if (!hidden || !visible) {
                continue;
            }
            all_cryo_browse_paths = all_cryo_browse_paths &&
                hidden->contestant_id_after == expected_id &&
                visible->contestant_id_after == expected_id &&
                hidden->rotation_frame ==
                    static_cast<std::int64_t>(expected_id - 1) * 40 &&
                visible->information_frame ==
                    metaverse::kCryoInfoRanges[
                        static_cast<std::size_t>(expected_id - 1)
                    ][0].first_frame &&
                hidden->legacy_sound_flags == 0x19 &&
                visible->legacy_sound_flags == 0x19 &&
                hidden->step_count == browse_hidden_steps.size() &&
                visible->step_count == browse_visible_steps.size() &&
                std::equal(
                    browse_hidden_steps.begin(), browse_hidden_steps.end(),
                    hidden->steps.begin()
                ) &&
                std::equal(
                    browse_visible_steps.begin(), browse_visible_steps.end(),
                    visible->steps.begin()
                );
        }
    }
    Check(
        all_cryo_browse_paths &&
            !metaverse::PlanCryoBrowse(0, 1, false) &&
            !metaverse::PlanCryoBrowse(1, 0, false),
        "both Cryo browse handlers wrap exactly and update visible INFO before ROTATE"
    );

    using ToggleStep = metaverse::CryoInformationToggleStep;
    constexpr std::array<ToggleStep, 4> show_information_steps{{
        ToggleStep::seek_information_name_bio,
        ToggleStep::mark_information_dirty,
        ToggleStep::show_information,
        ToggleStep::repaint_information_panel,
    }};
    constexpr std::array<ToggleStep, 4> hide_information_steps{{
        ToggleStep::stop_information,
        ToggleStep::hide_information,
        ToggleStep::mark_rotation_dirty,
        ToggleStep::repaint_information_panel,
    }};
    bool all_cryo_information_toggles = true;
    for (std::int32_t contestant_id = 1; contestant_id <= 10;
         ++contestant_id) {
        const auto show = metaverse::PlanCryoInformationToggle(
            false, contestant_id
        );
        const auto hide = metaverse::PlanCryoInformationToggle(
            true, contestant_id
        );
        all_cryo_information_toggles =
            all_cryo_information_toggles && show && hide;
        if (!show || !hide) {
            continue;
        }
        all_cryo_information_toggles = all_cryo_information_toggles &&
            show->visible_after && !hide->visible_after &&
            show->information_frame == metaverse::kCryoInfoRanges[
                static_cast<std::size_t>(contestant_id - 1)
            ][0].first_frame &&
            std::equal(
                show_information_steps.begin(), show_information_steps.end(),
                show->steps.begin()
            ) &&
            std::equal(
                hide_information_steps.begin(), hide_information_steps.end(),
                hide->steps.begin()
            );
    }
    Check(
        all_cryo_information_toggles &&
            !metaverse::PlanCryoInformationToggle(false, 0),
        "Cryo INFO toggle preserves exact seek/show and stop/hide command order"
    );

    Check(
        metaverse::kCryoRotationChildPlan.relative_path ==
                L"MOV\\CRYO\\ROTATE.AVI" &&
            metaverse::kCryoRotationChildPlan.style == 0x0400000aU &&
            metaverse::kCryoRotationChildPlan.x == 258 &&
            metaverse::kCryoRotationChildPlan.y == 142 &&
            metaverse::kCryoRotationChildPlan.width_adjustment == -3 &&
            metaverse::kCryoRotationChildPlan.bind_document_owner &&
            metaverse::kCryoRotationChildPlan.seek_frame_zero &&
            metaverse::kCryoRotationChildPlan.show_after_creation &&
            metaverse::kCryoInformationChildPlan.relative_path ==
                L"MOV\\CRYO\\INFO.AVI" &&
            metaverse::kCryoInformationChildPlan.style == 0x0400000aU &&
            metaverse::kCryoInformationChildPlan.x == 464 &&
            metaverse::kCryoInformationChildPlan.y == 84 &&
            metaverse::kCryoInformationChildPlan.width_adjustment == 0 &&
            metaverse::kCryoInformationChildPlan.bind_document_owner &&
            metaverse::kCryoInformationChildPlan.seek_frame_zero &&
            !metaverse::kCryoInformationChildPlan.show_after_creation,
        "Cryo ROTATE and INFO children preserve exact style, placement, seek, and visibility"
    );

    using CryoEntryStep = metaverse::CryoEntryStep;
    constexpr std::array<CryoEntryStep, 7> exact_cryo_entry_order{{
        CryoEntryStep::load_static_bitmaps,
        CryoEntryStep::start_ambient,
        CryoEntryStep::wait_500_milliseconds,
        CryoEntryStep::run_c11,
        CryoEntryStep::restart_ambient,
        CryoEntryStep::construct_rotation_child,
        CryoEntryStep::construct_information_child,
    }};
    Check(
        metaverse::kCryoEntryOrder == exact_cryo_entry_order,
        "Cryo retains its dialog through C11 and constructs ROTATE then hidden INFO"
    );

    constexpr std::array<metaverse::CryoStaticBitmapPlan, 15>
        exact_cryo_static_load_order{{
            {L"CRYO.BMP", 0, 0, 637, 480, true},
            {L"INFOBK.BMP", 432, 61, 202, 349, false},
            {L"BUTTONS.BMP", 0, 0, 179, 480, false},
            {L"MONEY.BMP", 358, 3, 279, 58, false},
            {L"BROWSL.BMP", 33, 71, 60, 55, false},
            {L"BROWSR.BMP", 98, 80, 54, 52, false},
            {L"ROTATE.BMP", 53, 172, 76, 65, false},
            {L"INFO.BMP", 52, 256, 84, 64, false},
            {L"SELECT.BMP", 54, 341, 75, 79, false},
            {L"NAMEBIO.BMP", 470, 239, 132, 24, false},
            {L"AMBITION.BMP", 488, 265, 96, 22, false},
            {L"TURNON.BMP", 492, 288, 90, 23, false},
            {L"TURNOFF.BMP", 490, 313, 98, 23, false},
            {L"IDEALMAN.BMP", 491, 340, 91, 23, false},
            {L"FAVORITE.BMP", 456, 364, 159, 21, false},
        }};
    bool exact_cryo_static_load = true;
    for (std::size_t index = 0;
         index < exact_cryo_static_load_order.size(); ++index) {
        const auto& actual = metaverse::kCryoStaticBitmapLoadOrder[index];
        const auto& expected = exact_cryo_static_load_order[index];
        exact_cryo_static_load = exact_cryo_static_load &&
            actual.filename == expected.filename && actual.x == expected.x &&
            actual.y == expected.y && actual.width == expected.width &&
            actual.height == expected.height &&
            actual.expose_immediately_after_load ==
                expected.expose_immediately_after_load;
    }
    Check(
        exact_cryo_static_load,
        "Cryo loads fifteen static wrappers in order and exposes CRYO alone first"
    );

    using CryoHostStep = metaverse::CryoHostDispatchOperation;
    constexpr std::array<CryoHostStep, 5> exact_cryo_host_order{{
        CryoHostStep::stop_legacy_audio,
        CryoHostStep::construct_generic_media_runner,
        CryoHostStep::run_mode_six_host_clip,
        CryoHostStep::destroy_generic_media_runner,
        CryoHostStep::restart_cryo_ambient,
    }};
    Check(
        metaverse::kCryoHostDispatchOrder == exact_cryo_host_order &&
            metaverse::kCryoHostChannel == 1 &&
            metaverse::kCryoEntryHostTake == 1 &&
            metaverse::kCryoFirstSponsorHostTake == 2 &&
            metaverse::kCryoHostMediaMode == 6,
        "Cryo C11/C12 stop, run mode-6 host media, destroy, and restart ambience"
    );

    using CryoTeardown = metaverse::CryoDialogTeardownOperation;
    constexpr std::array<CryoTeardown, 21> exact_cryo_teardown_order{{
        CryoTeardown::close_rotation_child,
        CryoTeardown::close_information_child,
        CryoTeardown::stop_legacy_audio,
        CryoTeardown::release_browse_left,
        CryoTeardown::release_browse_right,
        CryoTeardown::release_rotate,
        CryoTeardown::release_information,
        CryoTeardown::release_sponsor,
        CryoTeardown::release_name_bio,
        CryoTeardown::release_measurements,
        CryoTeardown::release_turn_ons,
        CryoTeardown::release_turn_offs,
        CryoTeardown::release_what_she_wants,
        CryoTeardown::release_favorite_quote,
        CryoTeardown::release_backing_store,
        CryoTeardown::release_base,
        CryoTeardown::release_buttons,
        CryoTeardown::release_sponsored_portrait,
        CryoTeardown::release_information_background,
        CryoTeardown::release_money,
        CryoTeardown::end_dialog,
    }};
    Check(
        metaverse::kCryoDialogTeardownOrder == exact_cryo_teardown_order,
        "Cryo closes ROTATE and INFO before audio, DIB teardown, and variant generation"
    );

    auto one_missing_control = metaverse::kCryoAllControlsAvailable;
    one_missing_control[2] = false;
    auto one_missing_detail = metaverse::kCryoAllDetailsAvailable;
    one_missing_detail[4] = false;
    const auto unavailable_control_down = metaverse::PlanCryoMouseDown(
        CryoAction::sponsor, true, 53, 172, false,
        one_missing_control, metaverse::kCryoAllDetailsAvailable
    );
    const auto unavailable_control_up = metaverse::PlanCryoMouseUp(
        CryoAction::rotate, true, 53, 172,
        one_missing_control, metaverse::kCryoAllDetailsAvailable
    );
    const auto unavailable_detail_down = metaverse::PlanCryoMouseDown(
        CryoAction::information, true, 491, 340, false,
        metaverse::kCryoAllControlsAvailable, one_missing_detail
    );
    const auto unavailable_detail_up = metaverse::PlanCryoMouseUp(
        CryoAction::ideal_man, true, 491, 340,
        metaverse::kCryoAllControlsAvailable, one_missing_detail
    );
    Check(
        unavailable_control_down.action_after == CryoAction::sponsor &&
            !unavailable_control_up.dispatch &&
            unavailable_detail_down.action_after == CryoAction::information &&
            !unavailable_detail_up.dispatch,
        "a failed Cryo highlight load collapses only that original hit target"
    );

    bool all_cryo_portrait_insertions = true;
    for (std::int32_t contestant_id = 1; contestant_id <= 10;
         ++contestant_id) {
        for (std::size_t selected_count = 1; selected_count <= 5;
             ++selected_count) {
            const auto insertion = metaverse::PlanCryoPortraitInsertion(
                contestant_id, selected_count
            );
            all_cryo_portrait_insertions =
                all_cryo_portrait_insertions && insertion &&
                insertion->relative_filename ==
                    metaverse::kCryoPortraitFiles[
                        static_cast<std::size_t>(contestant_id - 1)
                    ] &&
                insertion->slot == selected_count - 1 &&
                insertion->x ==
                    217 + static_cast<std::int32_t>(selected_count) * 70 &&
                insertion->y == 412 &&
                insertion->replace_previous_logical_wrapper &&
                insertion->warn_on_load_failure &&
                insertion->force_synchronous_repaint;
        }
    }
    Check(
        all_cryo_portrait_insertions &&
            !metaverse::PlanCryoPortraitInsertion(0, 1) &&
            !metaverse::PlanCryoPortraitInsertion(1, 0) &&
            !metaverse::PlanCryoPortraitInsertion(1, 6),
        "all fifty Cryo sponsorship portrait placements load only at insertion time"
    );
    Check(
        metaverse::kCryoAmbientStartLegacySoundFlags == 0x09 &&
            metaverse::kCryoBrowseLegacySoundFlags == 0x19 &&
            metaverse::kCryoNoCashLegacySoundFlags == 0x00,
        "Cryo ambient, browse, and NOCASH1 flags retain original default fallback"
    );

    metaverse::ContestantRoundState gonged;
    gonged.ratings = {8, 9, 7};
    metaverse::GongContestant(gonged, metaverse::JudgingCategory::brains);
    Check(gonged.ratings[1] == 0 && !gonged.pending[1] && gonged.pending[0] &&
              gonged.pending[2] && !gonged.disqualified,
          "gong gives only the current category a zero without disqualification");

    metaverse::GameRoundState economy;
    economy.credits = 500.0f;
    std::vector<std::int32_t> sponsored;
    std::string economy_error;
    Check(!metaverse::SponsorContestant(
              economy, sponsored, 3, economy_error
          ) && economy.credits == 500.0f,
          "Cryo sponsorship rejects a roster choice that cannot be afforded");
    Check(metaverse::SponsorContestant(economy, sponsored, 1, economy_error) &&
              economy.credits == 400.0f && economy.accumulated_reward == 100.0f,
          "Cryo sponsorship debits the fixed contestant value");
    sponsored.push_back(1);
    Check(!metaverse::SponsorContestant(economy, sponsored, 1, economy_error),
          "Cryo sponsorship rejects a duplicate contestant");
    metaverse::GameRoundState current_candidate_quirk;
    current_candidate_quirk.credits = 300.0f;
    Check(metaverse::SponsorContestant(
              current_candidate_quirk, {}, 6, economy_error
          ) && current_candidate_quirk.credits == 250.0f,
          "Cryo affordability look-ahead counts the current zero-based candidate again");
    metaverse::GameRoundState selected_id_quirk;
    selected_id_quirk.credits = 299.0f;
    const std::array<std::int32_t, 1> selected_id_five{{5}};
    Check(!metaverse::SponsorContestant(
              selected_id_quirk, selected_id_five, 1, economy_error
          ) && selected_id_quirk.credits == 299.0f,
          "Cryo affordability preserves the executable's zero/one-based selected-ID mismatch");
    selected_id_quirk.credits = 300.0f;
    Check(metaverse::SponsorContestant(
              selected_id_quirk, selected_id_five, 1, economy_error
          ) && selected_id_quirk.credits == 200.0f,
          "Cryo affordability accepts the exact quirked look-ahead boundary");
    float surcharge = 0.0f;
    Check(metaverse::ChargePerformanceSurcharge(
              economy, 1, surcharge, economy_error
          ) && surcharge == 10.0f && economy.credits == 390.0f,
          "performance playback charges ten percent of sponsorship");
    metaverse::GameRoundState x87_surcharge_boundary;
    x87_surcharge_boundary.credits = 4.999f;
    Check(!metaverse::ChargePerformanceSurcharge(
              x87_surcharge_boundary, 6, surcharge, economy_error
          ) && x87_surcharge_boundary.credits == 4.999f,
          "performance affordability compares the unrounded x87 ten-percent sum");
    x87_surcharge_boundary.credits = 5.0f;
    Check(metaverse::ChargePerformanceSurcharge(
              x87_surcharge_boundary, 6, surcharge, economy_error
          ) && std::bit_cast<std::uint32_t>(
                    x87_surcharge_boundary.credits
                ) == 0U,
          "performance affordability clamps the accepted tiny negative balance");
    metaverse::GameRoundState exact_roster;
    exact_roster.credits = 500.0f;
    std::vector<std::int32_t> roster;
    for (const std::int32_t contestant_id : {1, 2, 6, 7, 9}) {
        Check(metaverse::SponsorContestant(
                  exact_roster, roster, contestant_id, economy_error
              ),
              "original affordability check permits a complete exact-budget roster");
        roster.push_back(contestant_id);
    }
    Check(exact_roster.credits == 0.0f && roster.size() == 5,
          "five sponsored contestants may consume the exact available balance");

    constexpr std::array<std::int32_t, 10> exact_affordability_order{{
        5, 6, 8, 0, 4, 9, 1, 3, 7, 2,
    }};
    bool exhaustive_cryo_affordability = true;
    for (std::uint32_t mask = 0; mask < (1U << 10); ++mask) {
        const std::size_t selected_count = static_cast<std::size_t>(
            std::popcount(mask)
        );
        if (selected_count > 4) {
            continue;
        }
        std::vector<std::int32_t> selected_ids;
        for (std::int32_t contestant_id = 1; contestant_id <= 10;
             ++contestant_id) {
            if ((mask & (1U << (contestant_id - 1))) != 0) {
                selected_ids.push_back(contestant_id);
            }
        }
        for (std::int32_t candidate = 1; candidate <= 10; ++candidate) {
            if (std::find(
                    selected_ids.begin(), selected_ids.end(), candidate
                ) != selected_ids.end()) {
                continue;
            }
            const std::size_t future_count = 4 - selected_count;
            float future_minimum = 0.0f;
            std::size_t counted = 0;
            if (future_count != 0) {
                for (const std::int32_t zero_based_id :
                     exact_affordability_order) {
                    // This is intentionally the executable's mismatched compare:
                    // zero-based affordability IDs against one-based selections.
                    if (std::find(
                            selected_ids.begin(), selected_ids.end(),
                            zero_based_id
                        ) != selected_ids.end()) {
                        continue;
                    }
                    future_minimum += metaverse::ContestantSponsorCost(
                        zero_based_id + 1
                    );
                    if (++counted == future_count) {
                        break;
                    }
                }
            }
            const float candidate_cost =
                metaverse::ContestantSponsorCost(candidate);
            float accepted_credit = candidate_cost + future_minimum;
            while (accepted_credit - candidate_cost < future_minimum) {
                accepted_credit = std::nextafter(
                    accepted_credit,
                    std::numeric_limits<float>::infinity()
                );
            }
            for (;;) {
                const float prior = std::nextafter(
                    accepted_credit,
                    -std::numeric_limits<float>::infinity()
                );
                if (prior - candidate_cost < future_minimum) {
                    break;
                }
                accepted_credit = prior;
            }
            const float rejected_credit = std::nextafter(
                accepted_credit,
                -std::numeric_limits<float>::infinity()
            );

            metaverse::GameRoundState accepted_state;
            accepted_state.credits = accepted_credit;
            accepted_state.accumulated_reward = 17.25f;
            std::string error;
            const bool accepted = metaverse::SponsorContestant(
                accepted_state, selected_ids, candidate, error
            );
            metaverse::GameRoundState rejected_state;
            rejected_state.credits = rejected_credit;
            rejected_state.accumulated_reward = 17.25f;
            const bool rejected = metaverse::SponsorContestant(
                rejected_state, selected_ids, candidate, error
            );
            exhaustive_cryo_affordability =
                exhaustive_cryo_affordability && accepted && !rejected &&
                accepted_state.credits ==
                    accepted_credit - candidate_cost &&
                accepted_state.accumulated_reward ==
                    17.25f + candidate_cost &&
                rejected_state.credits == rejected_credit &&
                rejected_state.accumulated_reward == 17.25f;
        }
    }
    Check(
        exhaustive_cryo_affordability,
        "Cryo sponsorship exhaustively matches every reachable roster/candidate cash boundary"
    );

    for (std::size_t index = 0; index < roster.size(); ++index) {
        exact_roster.contestants[index].contestant_id = roster[index];
    }
    exact_roster.credits = 250.9f;
    Check(metaverse::SimmMotionPacingFactor(exact_roster) == 3,
          "Simm pacing uses truncated credits over average selected cost");
    Check(std::abs(metaverse::SimmCreditAward(1, 3) - 9.0f) < 0.001f &&
              std::abs(metaverse::SimmCreditAward(0, 3) - 22.5f) < 0.001f,
          "caught Simm award uses the pacing-scaled contestant or D0 base value");
    exact_roster.credits = 2100.0f;
    Check(metaverse::SimmMotionPacingFactor(exact_roster) == 0,
          "high cash clamps Simm movement pacing to zero");
    Check(std::abs(metaverse::SimmCreditAward(2, 0) - 37.5f) < 0.001f,
          "zero pacing factor pays fifteen percent of sponsorship value");
    exact_roster.credits = 0.0f;
    Check(metaverse::SimmMotionPacingFactor(exact_roster) == 5,
          "zero cash clamps Simm movement pacing to five");
    Check(std::abs(metaverse::SimmCreditAward(0, 5) - 12.5f) < 0.001f,
          "maximum pacing factor pays five percent of the D0 base");
    Check(!metaverse::SimmResultEarnsAward(0) &&
              !metaverse::SimmResultEarnsAward(10) &&
              metaverse::SimmResultEarnsAward(50) &&
              metaverse::SimmResultEarnsAward(100),
          "generic-media cancellation/activation changes and low results do not award Simm credits");
    Check(!metaverse::IntroModalResultExits(0) &&
              metaverse::IntroModalResultExits(2),
          "one-shot intro OK/notify continues while nonzero modal results exit");
    Check(metaverse::kCenteredOneShotPlaybackDelay.count() == 100,
          "intro, winner, and credit modal movies share the 100ms start timer");
    Check(
        metaverse::kCenteredOneShotDialogResourceId == 158 &&
            metaverse::kCenteredOneShotCursorResourceId == 166 &&
            metaverse::kCenteredOneShotTimerId == 1 &&
            metaverse::kCenteredOneShotPrivateCompleteMessage == 0x0405 &&
            metaverse::kCenteredOneShotNotifySuccessful == 1 &&
            metaverse::kCenteredOneShotBlacknessRop == 0x42 &&
            metaverse::kCenteredOneShotWidth == 640 &&
            metaverse::kCenteredOneShotHeight == 480 &&
            metaverse::kCenteredOneShotZoomPercent == 200 &&
            metaverse::kCenteredOneShotCaption == L"Ms. Metaverse",
        "centered one-shot resource, cursor, message, paint, geometry, zoom, and caption are exact"
    );
    using InitOp = metaverse::CenteredOneShotInitOperation;
    Check(
        metaverse::kCenteredOneShotInitSequence ==
            std::array<InitOp, 5>{
                InitOp::set_caption,
                InitOp::center_640x480,
                InitOp::invalidate_erase,
                InitOp::update_window,
                InitOp::arm_timer_1_100ms,
            },
        "centered one-shot initialization follows the recovered resource-158 order"
    );
    using TimerOp = metaverse::CenteredOneShotTimerOperation;
    Check(
        metaverse::kCenteredOneShotTimerSequence ==
            std::array<TimerOp, 6>{
                TimerOp::kill_timer_1,
                TimerOp::allocate_media_wrapper,
                TimerOp::create_child_style_0x0a,
                TimerOp::zoom_200_percent,
                TimerOp::play_notify,
                TimerOp::mark_active,
            },
        "centered one-shot timer constructs, zooms, and starts the child in recovered order"
    );
    using WaitStyle = metaverse::LegacyPlaybackWaitStyle;
    Check(
        metaverse::LegacyPlaybackBlocksOwnerInput(
            WaitStyle::synchronous_mci_wait
        ) &&
            !metaverse::LegacyPlaybackBlocksOwnerInput(WaitStyle::notify) &&
            !metaverse::LegacyPlaybackBlocksOwnerInput(
                WaitStyle::pumped_deadline
            ),
        "MCI Wait blocks pavilion input while notify and Slots pump do not"
    );
    Check(
        metaverse::kLegacyCenteredOneShotMciStyle == 0x0a &&
            metaverse::LegacyMciSuppressesErrorDialog(
                metaverse::kLegacyCenteredOneShotMciStyle
            ) &&
            metaverse::CenteredOneShotFailureWaitsForOk(),
        "centered one-shot MCI failure stays blank and waits for OK"
    );
    Check(
        !metaverse::CenteredOneShotLeftClickFinishes(false) &&
            metaverse::CenteredOneShotLeftClickFinishes(true),
        "centered one-shot timer expiry without a child stays mouse-inert"
    );
    using InputAction = metaverse::CenteredOneShotInputAction;
    Check(
        metaverse::CenteredOneShotActiveInputAction(false) ==
                InputAction::play_notify &&
            metaverse::CenteredOneShotActiveInputAction(true) ==
                InputAction::stop_and_complete_zero &&
            metaverse::kCenteredOneShotCancelAction == InputAction::ignore &&
            metaverse::CenteredOneShotNotifyCompletes(1) &&
            !metaverse::CenteredOneShotNotifyCompletes(2) &&
            !metaverse::CenteredOneShotNotifyCompletes(4) &&
            metaverse::CenteredOneShotPrivateResult(0) == 0 &&
            metaverse::CenteredOneShotPrivateResult(7) == 7,
        "centered one-shot child/OK, notify, private-result, and Cancel branches are exact"
    );
    using CompletionOp = metaverse::CenteredOneShotCompletionOperation;
    Check(
        metaverse::kCenteredOneShotCompletionSequence ==
            std::array<CompletionOp, 3>{
                CompletionOp::close_media_device,
                CompletionOp::destroy_media_wrapper,
                CompletionOp::end_dialog_with_result,
            },
        "centered one-shot completion closes media before returning its modal result"
    );
    using Purpose = metaverse::CenteredOneShotPurpose;
    using Continuation = metaverse::CenteredOneShotContinuation;
    Check(
        metaverse::CenteredOneShotContinuationFor(Purpose::intro) ==
                Continuation::enter_game &&
            metaverse::CenteredOneShotContinuationFor(Purpose::winner) ==
                Continuation::finish_tally &&
            metaverse::CenteredOneShotContinuationFor(Purpose::credit) ==
                Continuation::exit_application,
        "all resource-158 callers retain their distinct post-modal continuation"
    );
    Check(
        metaverse::LegacyWidePerformanceChildIsPresent(true, true) &&
            metaverse::LegacyWidePerformanceChildIsPresent(true, false) &&
            !metaverse::LegacyWidePerformanceChildIsPresent(false, false),
        "wide performance input ownership follows wrapper lifetime, not open success"
    );
    Check(
        metaverse::kLegacyWidePerformanceRepaintBounds ==
            std::array<int, 4>{208, 108, 528, 344},
        "wide performance teardown repaints the legacy 208,108-528,344 rectangle"
    );
    using WideCreateOp = metaverse::WidePerformanceCreateOperation;
    Check(
        metaverse::kWidePerformanceCreateSequence ==
                std::array<WideCreateOp, 5>{
                    WideCreateOp::destroy_previous_wrapper,
                    WideCreateOp::allocate_and_store_wrapper,
                    WideCreateOp::create_or_open_media_child,
                    WideCreateOp::move_child,
                    WideCreateOp::show_child,
                } &&
            metaverse::kLegacyWidePerformanceMciStyle == 0x4000000b &&
            metaverse::kLegacyWidePerformanceShowCommand == 5 &&
            metaverse::kLegacyWidePerformanceChildBounds ==
                std::array<int, 4>{208, 108, 528, 348} &&
            metaverse::kCenteredOneShotCaption == L"Ms. Metaverse",
        "wide performance wrapper creation, caption, style, placement, and show contract"
    );
    using WideDispatchEvent = metaverse::WidePavilionHostDispatchEvent;
    using OuterAmbient = metaverse::WidePavilionOuterAmbientAction;
    const auto talent_entry = metaverse::PlanWidePavilionHostDispatch(
        metaverse::JudgingCategory::talent, WideDispatchEvent::entry
    );
    const auto brains_entry = metaverse::PlanWidePavilionHostDispatch(
        metaverse::JudgingCategory::brains, WideDispatchEvent::entry
    );
    const auto score_reaction = metaverse::PlanWidePavilionHostDispatch(
        metaverse::JudgingCategory::talent,
        WideDispatchEvent::score_reaction, 8
    );
    const auto gong_dispatch = metaverse::PlanWidePavilionHostDispatch(
        metaverse::JudgingCategory::brains, WideDispatchEvent::gong
    );
    const auto penalty_dispatch = metaverse::PlanWidePavilionHostDispatch(
        metaverse::JudgingCategory::talent, WideDispatchEvent::penalty
    );
    const auto completion_dispatch = metaverse::PlanWidePavilionHostDispatch(
        metaverse::JudgingCategory::brains, WideDispatchEvent::completion
    );
    Check(
        talent_entry && talent_entry->primary == 2 &&
            talent_entry->mode == 6 && talent_entry->forwarded_value == 1 &&
            brains_entry && brains_entry->primary == 3 &&
            brains_entry->mode == 6 && brains_entry->forwarded_value == 1 &&
            score_reaction && score_reaction->primary == 0 &&
            score_reaction->mode == 4 &&
            score_reaction->forwarded_value == 8 &&
            gong_dispatch && gong_dispatch->primary == 6 &&
            gong_dispatch->mode == 6 &&
            gong_dispatch->forwarded_value == 1 &&
            gong_dispatch->outer_ambient_action == OuterAmbient::restart &&
            !gong_dispatch->outer_updates_before_ambient_action &&
            penalty_dispatch && penalty_dispatch->primary == 7 &&
            penalty_dispatch->mode == 6 &&
            penalty_dispatch->forwarded_value == 1 &&
            penalty_dispatch->outer_ambient_action == OuterAmbient::restart &&
            penalty_dispatch->outer_updates_before_ambient_action &&
            completion_dispatch && completion_dispatch->primary == 8 &&
            completion_dispatch->mode == 6 &&
            completion_dispatch->forwarded_value == 1 &&
            completion_dispatch->outer_ambient_action == OuterAmbient::stop &&
            completion_dispatch->dispatcher_stops_ambient &&
            completion_dispatch->dispatcher_restarts_ambient &&
            !metaverse::PlanWidePavilionHostDispatch(
                metaverse::JudgingCategory::looks, WideDispatchEvent::entry
            ),
        "wide pavilion dispatchers preserve all recovered argument tuples and ambient tails"
    );
    const auto looks_first_entry = metaverse::PlanJudgingEntryTimer(
        metaverse::JudgingCategory::looks, true
    );
    const auto looks_consumed_entry = metaverse::PlanJudgingEntryTimer(
        metaverse::JudgingCategory::looks, false
    );
    const auto talent_consumed_entry = metaverse::PlanJudgingEntryTimer(
        metaverse::JudgingCategory::talent, false
    );
    Check(
        looks_first_entry.delay == std::chrono::milliseconds(500) &&
            looks_first_entry.run_host_before_continuation &&
            looks_first_entry.construct_rotate_after_timer &&
            looks_first_entry.auto_select_after_timer &&
            looks_first_entry.start_ambient_after_timer &&
            looks_consumed_entry.delay == looks_first_entry.delay &&
            !looks_consumed_entry.run_host_before_continuation &&
            looks_consumed_entry.construct_rotate_after_timer &&
            looks_consumed_entry.auto_select_after_timer &&
            looks_consumed_entry.start_ambient_after_timer &&
            talent_consumed_entry.delay == std::chrono::milliseconds(500) &&
            !talent_consumed_entry.run_host_before_continuation &&
            !talent_consumed_entry.construct_rotate_after_timer &&
            !talent_consumed_entry.auto_select_after_timer &&
            talent_consumed_entry.start_ambient_after_timer,
        "consumed C21/C31/C41 still run the category-specific 500 ms entry continuation"
    );
    bool host_entry_played = false;
    metaverse::ConsumeLegacyHostEntryAttempt(host_entry_played);
    Check(
        host_entry_played,
        "generic host entry is consumed even when media construction later fails"
    );
    Check(
        metaverse::CommentRightDoubleClickEndsDialog(false) &&
            !metaverse::CommentRightDoubleClickEndsDialog(true),
        "ORDER right double-click closes only while its owner receives input"
    );
    Check(
        metaverse::GenericMediaActivationChangeEndsModal(false) &&
            metaverse::GenericMediaActivationChangeEndsModal(true),
        "generic C/X/Simm media ends on either WM_ACTIVATEAPP state"
    );

    economy.contestants[0].contestant_id = 1;
    economy.contestants[1].contestant_id = 2;
    economy.contestants[0].ratings = {4, 6, 8};
    economy.contestants[0].pending = {true, true, false};
    economy.contestants[1].ratings = {7, 5, 3};
    economy.contestants[1].pending = {true, false, true};
    economy.judge_cadet_slot = 1;
    economy.judge_cadet_category = metaverse::JudgingCategory::talent;
    Check(
        metaverse::IsJudgeCadetPresentation(
            economy, 1, metaverse::JudgingCategory::talent
        ) &&
        !metaverse::IsJudgeCadetPresentation(
            economy, 1, metaverse::JudgingCategory::brains
        ) &&
        !metaverse::IsJudgeCadetPresentation(
            economy, 0, metaverse::JudgingCategory::talent
        ),
        "judge cadet clue appears only for its slot and assigned pavilion"
    );
    const auto wrong_penalty = metaverse::ApplyPenaltyBox(
        economy, 0, metaverse::JudgingCategory::brains
    );
    Check(!wrong_penalty.correct && wrong_penalty.credit_change == -10.0f &&
              !economy.contestants[0].disqualified &&
              economy.contestants[0].ratings ==
                  std::array<std::uint8_t, 3>{4, 6, 8} &&
              economy.contestants[0].pending ==
                  std::array<bool, 3>{true, true, false},
          "wrong Penalty Box accusation loses ten percent without changing judging state");
    metaverse::GameRoundState x87_penalty_balance;
    x87_penalty_balance.credits = 4.999f;
    x87_penalty_balance.contestants[0].contestant_id = 6;
    x87_penalty_balance.judge_cadet_slot = 1;
    const auto x87_wrong_penalty = metaverse::ApplyPenaltyBox(
        x87_penalty_balance, 0, metaverse::JudgingCategory::talent
    );
    Check(!x87_wrong_penalty.correct &&
              x87_penalty_balance.credits < -0.001f,
          "wrong Penalty Box debit preserves the original unrounded x87 chain");
    const float before_correct_penalty = economy.credits;
    const auto correct_penalty = metaverse::ApplyPenaltyBox(
        economy, 1, metaverse::JudgingCategory::looks
    );
    Check(correct_penalty.correct && correct_penalty.credit_change == 250.0f &&
              economy.credits == before_correct_penalty + 250.0f &&
              economy.contestants[1].disqualified &&
              economy.contestants[1].ratings ==
                  std::array<std::uint8_t, 3>{0, 5, 3} &&
              economy.contestants[1].pending ==
                  std::array<bool, 3>{false, false, true},
          "correct Penalty Box accusation refunds, zeroes only the current category, and disqualifies");

    metaverse::GameRoundState round;
    round.weights = {20, 30, 50};
    round.credits = 100.0f;
    round.accumulated_reward = 750.0f;
    round.contestants[0].ratings = {5, 5, 5};
    round.contestants[1].ratings = {8, 5, 5};
    round.contestants[2].ratings = {8, 5, 5};
    round.contestants[3].ratings = {10, 10, 10};
    round.contestants[3].disqualified = true;
    for (auto& contestant : round.contestants) {
        contestant.pending.fill(false);
    }
    round.contestants[3].pending.fill(true);

    const metaverse::TallyResult complete = metaverse::TallyRound(round);
    Check(complete.winner_index == 1 && complete.winning_score == 560,
          "strict-highest score preserves earliest tie");
    Check(complete.all_judging_complete && complete.credit_award == 750.0f &&
              round.credits == 850.0f,
          "complete round earns accumulated reward above 500");
    Check(!round.contestants[3].pending[0] &&
              !round.contestants[3].pending[1] &&
              !round.contestants[3].pending[2],
          "disqualification clears pending categories");

    metaverse::GameRoundState zero_scores;
    zero_scores.weights = {100, 0, 0};
    const metaverse::TallyResult zero_result =
        metaverse::TallyRound(zero_scores);
    Check(zero_result.winner_index == 0 && zero_result.winning_score == 0,
          "all-zero tally retains the zero-initialized first winner slot");

    metaverse::GameRoundState partial;
    partial.weights = {33, 33, 34};
    partial.credits = 500.0f;
    partial.accumulated_reward = 900.0f;
    partial.contestants[0].pending = {false, false, true};
    const metaverse::TallyResult incomplete = metaverse::TallyRound(partial);
    Check(!incomplete.all_judging_complete &&
              incomplete.completed_category_count == 2 &&
              incomplete.credit_award == 54.0f && partial.credits == 554.0f,
          "partial round earns 27 per completed category");

    metaverse::GameRoundState partial_rounding;
    partial_rounding.credits = std::bit_cast<float>(0x4b7fffe8U);
    partial_rounding.contestants[0].pending = {false, false, true};
    const metaverse::TallyResult rounded =
        metaverse::TallyRound(partial_rounding);
    Check(rounded.completed_category_count == 2 &&
              rounded.credit_award == 54.0f &&
              std::bit_cast<std::uint32_t>(partial_rounding.credits) ==
                  0x4b800010U,
          "partial tally rounds each original 27-credit addition separately");

    metaverse::GameRoundState partial_disqualification;
    partial_disqualification.credits = 500.0f;
    partial_disqualification.contestants[1].disqualified = true;
    const metaverse::TallyResult disqualified_partial =
        metaverse::TallyRound(partial_disqualification);
    Check(!disqualified_partial.all_judging_complete &&
              disqualified_partial.completed_category_count == 3 &&
              disqualified_partial.credit_award == 81.0f &&
              partial_disqualification.credits == 581.0f,
          "partial tally counts three cleared fields for a disqualification");

    metaverse::GameRoundState minimum;
    minimum.weights = {100, 0, 0};
    minimum.accumulated_reward = 500.0f;
    for (auto& contestant : minimum.contestants) {
        contestant.pending.fill(false);
    }
    const metaverse::TallyResult minimum_result = metaverse::TallyRound(minimum);
    Check(minimum_result.credit_award == 500.0f,
          "complete round uses 500 minimum when accumulator is not greater");

    metaverse::GameRoundState raw_accumulator;
    raw_accumulator.accumulated_reward = std::bit_cast<float>(0x7fc00000U);
    for (auto& contestant : raw_accumulator.contestants) {
        contestant.pending.fill(false);
    }
    const metaverse::TallyResult raw_result =
        metaverse::TallyRound(raw_accumulator);
    Check(std::bit_cast<std::uint32_t>(raw_result.credit_award) ==
              0x7fc00000U &&
              std::bit_cast<std::uint32_t>(raw_accumulator.credits) ==
                  0x7fc00000U,
          "complete tally uses the original signed stored-float-bit comparison");
}

void TestPavilionRotation() {
    Check(std::wstring(metaverse::kLegacyPavilionRotationFileName) ==
              L"nrfPav.dat",
          "use the original legacy pavilion rotation filename");
    std::string template_data;
    for (int index = 0; index < 10; ++index) {
        template_data += "0000\r\n";
    }
    for (int index = 0; index < 10; ++index) {
        template_data += "00000\r\n";
    }
    std::istringstream input(template_data);
    metaverse::PavilionRotationState state;
    std::string error;
    Check(metaverse::ParseLegacyPavilionRotation(input, state, error),
          "parse legacy pavilion rotation template");

    metaverse::LegacyRandom random(12345);
    std::array<bool, 4> seen{};
    std::int32_t previous = 0;
    for (int draw = 0; draw < 12; ++draw) {
        const std::int32_t variant =
            metaverse::DrawBrainVariant(state, 2, random, error);
        Check(variant >= 1 && variant <= 4, "brain variant is in candidate range");
        if (draw < 4) {
            seen[static_cast<std::size_t>(variant - 1)] = true;
        }
        if (draw == 4 || draw == 8) {
            Check(variant != previous, "shuffle cycle does not immediately repeat");
        }
        previous = variant;
    }
    Check(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }),
          "first brain cycle visits every variant");

    metaverse::PavilionRotationState exact_draw_state;
    metaverse::LegacyRandom exact_draw_random(12345);
    constexpr std::array<std::int32_t, 5> exact_ids{3, 4, 5, 8, 10};
    constexpr std::array<std::array<std::int32_t, 2>, 5> exact_variants{{
        {1, 5}, {2, 1}, {1, 1}, {2, 4}, {3, 5},
    }};
    for (std::size_t index = 0; index < exact_ids.size(); ++index) {
        const auto brain = metaverse::DrawBrainVariant(
            exact_draw_state, exact_ids[index], exact_draw_random, error
        );
        const auto talent = metaverse::DrawTalentVariant(
            exact_draw_state, exact_ids[index], exact_draw_random, error
        );
        Check(brain == exact_variants[index][0] &&
                  talent == exact_variants[index][1],
              "round preparation consumes Brains then Talent CRT draws");
    }
    Check(exact_draw_random.State() == 0xdb6c5723U,
          "ten pavilion variant draws leave the exact CRT state");

    metaverse::PavilionRotationState exhausted;
    exhausted.brain_used[1] = {1, 0xcf, 2, 1};
    metaverse::LegacyRandom exhausted_random(54321);
    const std::uint32_t exhausted_state = exhausted_random.State();
    Check(metaverse::DrawBrainVariant(
              exhausted, 2, exhausted_random, error
          ) == 0 && exhausted_random.State() == exhausted_state,
          "all-nonzero pavilion line returns variant zero without consuming rand");

    std::ostringstream encoded;
    Check(metaverse::WriteLegacyPavilionRotation(encoded, state, error),
          "serialize legacy pavilion rotation");
    Check(encoded.str().size() == 130, "legacy pavilion rotation remains 130 bytes");
    Check(metaverse::DrawTalentVariant(state, 10, random, error) >= 1,
          "draw talent variant");
    Check(metaverse::DrawBrainVariant(state, 0, random, error) == 0,
          "reject invalid pavilion contestant id");

    const auto rotation_path = std::filesystem::temp_directory_path() /
        "ms-metaverse-pavilion-line-test.dat";
    std::error_code cleanup_error;
    std::filesystem::remove(rotation_path, cleanup_error);
    metaverse::PavilionRotationState persisted;
    persisted.brain_used[1][2] = true;
    Check(metaverse::SaveLegacyPavilionRotationLine(
              rotation_path, persisted,
              metaverse::PavilionRotationCategory::brains, 2, error
          ),
          "initialize pavilion table at the first line-local update");
    persisted.talent_used[1][4] = true;
    Check(metaverse::SaveLegacyPavilionRotationLine(
              rotation_path, persisted,
              metaverse::PavilionRotationCategory::talent, 2, error
          ),
          "update one talent line in place");
    std::ifstream persisted_input(rotation_path, std::ios::binary);
    const std::string persisted_bytes{
        std::istreambuf_iterator<char>(persisted_input),
        std::istreambuf_iterator<char>()
    };
    Check(persisted_bytes.size() == 130,
          "line-local pavilion update preserves the 130-byte table");
    Check(persisted_bytes.substr(6, 6) == "0010\r\n" &&
              persisted_bytes.substr(67, 7) == "00001\r\n",
          "line-local pavilion update writes only the selected contestant lines");
    std::filesystem::remove(rotation_path, cleanup_error);

    const auto tolerant_path = std::filesystem::temp_directory_path() /
        "ms-metaverse-pavilion-tolerant-test.dat";
    std::string malformed;
    malformed += "2000\r\n";
    malformed += "1\r\n";
    for (int index = 2; index < 10; ++index) {
        malformed += "0000\r\n";
    }
    for (int index = 0; index < 10; ++index) {
        malformed += "00000\r\n";
    }
    {
        std::ofstream fixture(
            tolerant_path, std::ios::binary | std::ios::trunc
        );
        fixture.write(
            malformed.data(), static_cast<std::streamsize>(malformed.size())
        );
    }
    metaverse::PavilionRotationState strict_state;
    Check(!metaverse::LoadLegacyPavilionRotation(
              tolerant_path, strict_state, error
          ),
          "strict pavilion diagnostics reject malformed legacy flags");
    metaverse::PavilionRotationState tolerant_state;
    metaverse::LoadLegacyPavilionRotationForSelection(
        tolerant_path, tolerant_state
    );
    Check(tolerant_state.brain_used[0][0] == 2 &&
              tolerant_state.brain_used[1][0] == 1 &&
              tolerant_state.brain_used[1][1] == 0xdd &&
              tolerant_state.brain_used[1][2] == 0xda &&
              tolerant_state.brain_used[1][3] == 0,
          "selection loader preserves raw cross-line byte-minus-'0' behavior");

    metaverse::PavilionRotationState selected_line_state;
    selected_line_state.brain_used[0][0] = 9;
    metaverse::LoadLegacyPavilionRotationLineForSelection(
        tolerant_path, selected_line_state,
        metaverse::PavilionRotationCategory::brains, 2
    );
    Check(selected_line_state.brain_used[0][0] == 9 &&
              selected_line_state.brain_used[1][0] == 1 &&
              selected_line_state.brain_used[1][1] == 0xdd &&
              selected_line_state.brain_used[1][2] == 0xda &&
              selected_line_state.brain_used[1][3] == 0,
          "round loader reopens and replaces only the requested pavilion line");
    std::filesystem::remove(tolerant_path, cleanup_error);

    metaverse::PavilionRotationState missing_selection;
    metaverse::LoadLegacyPavilionRotationForSelection(
        tolerant_path, missing_selection
    );
    Check(std::all_of(
              missing_selection.brain_used[0].begin(),
              missing_selection.brain_used[0].end(),
              [](std::uint8_t value) { return value == 0; }
          ),
          "missing pavilion state leaves constructor-zeroed selection flags");

    {
        std::ofstream empty_fixture(
            tolerant_path, std::ios::binary | std::ios::trunc
        );
    }
    metaverse::LoadLegacyPavilionRotationLineForSelection(
        tolerant_path, missing_selection,
        metaverse::PavilionRotationCategory::brains, 1
    );
    Check(missing_selection.brain_used[0][0] == 0xcf &&
              missing_selection.brain_used[0][1] == 0xcf &&
              missing_selection.brain_used[0][2] == 0xcf,
          "existing empty pavilion file stores wrapped EOF bytes");
    std::filesystem::remove(tolerant_path, cleanup_error);
}

void TestHostReactionRotation() {
    std::string raw(180, '\0');
    std::istringstream input(raw);
    metaverse::HostReactionState state;
    std::string error;
    Check(metaverse::ParseLegacyHostReactionState(input, state, error),
          "parse 180-byte legacy host-reaction state");
    Check(metaverse::HostReactionGroupForScore(-1) == 3 &&
              metaverse::HostReactionGroupForScore(0) == 3 &&
              metaverse::HostReactionGroupForScore(3) == 3 &&
              metaverse::HostReactionGroupForScore(4) == 1 &&
              metaverse::HostReactionGroupForScore(7) == 1 &&
              metaverse::HostReactionGroupForScore(8) == 2 &&
              metaverse::HostReactionGroupForScore(10) == 2 &&
              metaverse::HostReactionGroupForScore(11) == 2,
          "host-reaction score groups preserve every comparison boundary");
    Check(metaverse::HostReactionStem(1, 15) == L"X115" &&
              metaverse::HostReactionStem(2, 12) == L"X212" &&
              metaverse::HostReactionStem(3, 11) == L"X311" &&
              metaverse::HostReactionStem(2, 13).empty() &&
              metaverse::HostReactionStem(3, 12).empty(),
          "host-reaction clip ranges exclude the two unused sentinels");

    constexpr std::array<std::array<std::int32_t, 15>, 3> expected_variants{{
        {4, 15, 9, 7, 1, 3, 10, 14, 12, 5, 8, 11, 13, 2, 6},
        {1, 11, 9, 7, 4, 10, 2, 3, 8, 6, 5, 12, 0, 0, 0},
        {7, 8, 6, 3, 10, 1, 4, 2, 9, 5, 11, 0, 0, 0, 0},
    }};
    metaverse::LegacyRandom random(0x58484f53);
    for (std::int32_t group = 1; group <= 3; ++group) {
        std::array<bool, metaverse::kHostReactionVariantCapacity> seen{};
        const std::size_t count = metaverse::kHostReactionVariantCounts[
            static_cast<std::size_t>(group - 1)
        ];
        for (std::size_t draw = 0; draw < count; ++draw) {
            const std::int32_t variant = metaverse::DrawHostReactionVariant(
                state, group, random, error
            );
            Check(variant >= 1 && variant <= static_cast<std::int32_t>(count),
                  "host-reaction variant is in group range");
            Check(
                variant == expected_variants[
                    static_cast<std::size_t>(group - 1)
                ][draw],
                "host-reaction forward scan consumes the exact CRT sequence"
            );
            Check(!seen[static_cast<std::size_t>(variant - 1)],
                  "host-reaction does not repeat before group exhaustion");
            seen[static_cast<std::size_t>(variant - 1)] = true;
        }
    }
    Check(random.State() == 0x8d7ca459u,
          "host-reaction group cycles consume exactly 38 CRT draws");

    constexpr std::array<std::int32_t, 3> maximum_edge_variants{15, 12, 11};
    for (std::int32_t group = 1; group <= 3; ++group) {
        metaverse::HostReactionState maximum_edge_state;
        metaverse::LegacyRandom maximum_edge_random(0xf01bf641u);
        const std::int32_t variant = metaverse::DrawHostReactionVariant(
            maximum_edge_state, group, maximum_edge_random, error
        );
        Check(
            variant == maximum_edge_variants[
                static_cast<std::size_t>(group - 1)
            ],
            "host-reaction rand-maximum scan selects the final available word"
        );
        Check(maximum_edge_random.State() == 0x7fff0000u,
              "host-reaction rand-maximum scan consumes one CRT value");
    }

    metaverse::HostReactionState raw_sentinel_state;
    raw_sentinel_state.used[1][12] = 7;
    metaverse::LegacyRandom raw_sentinel_random(0xf01bf641u);
    Check(
        metaverse::DrawHostReactionVariant(
            raw_sentinel_state, 2, raw_sentinel_random, error
        ) == 12 &&
            raw_sentinel_state.used[1][12] == 7,
        "host-reaction scan leaves a raw inactive sentinel untouched"
    );

    metaverse::HostReactionState exhausted_state;
    std::fill(
        exhausted_state.used[2].begin(),
        exhausted_state.used[2].begin() + 11,
        9u
    );
    metaverse::LegacyRandom exhausted_random(0xf01bf641u);
    Check(
        metaverse::DrawHostReactionVariant(
            exhausted_state, 3, exhausted_random, error
        ) == 11 &&
            exhausted_state.used[2][10] == 1 &&
            std::ranges::all_of(
                exhausted_state.used[2].begin(),
                exhausted_state.used[2].begin() + 10,
                [](std::uint32_t flag) { return flag == 0; }
            ),
        "host-reaction exhausted group is zeroed before its replacement draw"
    );

    std::ostringstream encoded;
    Check(metaverse::WriteLegacyHostReactionState(encoded, state, error),
          "serialize legacy host-reaction state");
    Check(encoded.str().size() == 180,
          "legacy host-reaction state remains 180 bytes");
    std::istringstream trailing(raw + std::string(1, '\0'));
    Check(!metaverse::ParseLegacyHostReactionState(trailing, state, error),
          "reject trailing legacy host-reaction bytes");
    std::string malformed_flag = raw;
    malformed_flag[0] = 2;
    std::istringstream malformed_flag_input(malformed_flag);
    Check(
        !metaverse::ParseLegacyHostReactionState(
            malformed_flag_input, state, error
        ),
        "strict host-reaction parser rejects a non-boolean word"
    );
    std::string unavailable_flag = raw;
    unavailable_flag[(12 * 3 + 1) * 4] = 1;
    std::istringstream unavailable_flag_input(unavailable_flag);
    Check(
        !metaverse::ParseLegacyHostReactionState(
            unavailable_flag_input, state, error
        ),
        "strict host-reaction parser rejects an inactive marked word"
    );

    const auto tolerant_path = std::filesystem::temp_directory_path() /
        "metaverse-host-reaction-tolerant.dat";
    {
        std::ofstream output(tolerant_path, std::ios::binary | std::ios::trunc);
        const std::array<char, 4> partial{7, 0, 0, 0};
        output.write(partial.data(), static_cast<std::streamsize>(partial.size()));
    }
    metaverse::HostReactionState tolerant;
    metaverse::LoadLegacyHostReactionStateForSelection(tolerant_path, tolerant);
    Check(
        tolerant.used[0][0] == 7 && tolerant.used[1][0] == 0 &&
            tolerant.used[2][0] == 0,
        "X selection preserves a short/non-boolean HNRD word over zero fill"
    );

    tolerant.used[1][12] = 0x78563412u;
    tolerant.used[2][14] = 0x89abcdefu;
    Check(
        metaverse::SaveLegacyHostReactionState(
            tolerant_path, tolerant, error
        ),
        "X selection rewrites malformed and inactive HNRD words raw"
    );
    std::ifstream raw_saved_input(tolerant_path, std::ios::binary);
    const std::string raw_saved{
        std::istreambuf_iterator<char>(raw_saved_input),
        std::istreambuf_iterator<char>()
    };
    const std::size_t group_two_sentinel_offset = (12 * 3 + 1) * 4;
    const std::size_t final_word_offset = (14 * 3 + 2) * 4;
    Check(
        raw_saved.size() == 180 &&
            static_cast<std::uint8_t>(raw_saved[0]) == 7 &&
            static_cast<std::uint8_t>(
                raw_saved[group_two_sentinel_offset]
            ) == 0x12 &&
            static_cast<std::uint8_t>(
                raw_saved[group_two_sentinel_offset + 1]
            ) == 0x34 &&
            static_cast<std::uint8_t>(
                raw_saved[group_two_sentinel_offset + 2]
            ) == 0x56 &&
            static_cast<std::uint8_t>(
                raw_saved[group_two_sentinel_offset + 3]
            ) == 0x78 &&
            static_cast<std::uint8_t>(raw_saved[final_word_offset]) == 0xef,
        "X selection full rewrite retains the exact 45-word little-endian table"
    );
    std::istringstream raw_saved_strict(raw_saved);
    Check(
        !metaverse::ParseLegacyHostReactionState(
            raw_saved_strict, state, error
        ),
        "strict host-reaction diagnostics still reject permissive raw state"
    );
    raw_saved_input.close();
    std::error_code cleanup_error;
    std::filesystem::remove(tolerant_path, cleanup_error);
    metaverse::LoadLegacyHostReactionStateForSelection(tolerant_path, tolerant);
    Check(
        std::ranges::all_of(tolerant.used, [](const auto& group) {
            return std::ranges::none_of(group, [](bool used) { return used; });
        }),
        "missing HNRD state falls back to an unused table at X selection"
    );
}

void TestSlotMachine() {
    Check(metaverse::kSlotSpinDuration == std::chrono::milliseconds(4500),
          "slot animation uses the original 0x1194 millisecond dwell");
    Check(metaverse::kSlotSymbolX == std::array<std::int32_t, 3>{190, 290, 388} &&
              metaverse::kSlotSymbolY == 320,
          "slot symbols use the original native-size canvas positions");
    Check(metaverse::kSlotCreditsX == 40 && metaverse::kSlotCreditsY == 20 &&
              metaverse::kSlotWinAmountX == 450 &&
              metaverse::kSlotLossAmountX == 500 &&
              metaverse::kSlotNoCreditX == 350,
          "slot credit, result, and no-credit text coordinates");

    metaverse::LegacyRandom random(0x4d534d4d);
    float credits = 1'000.0f;
    std::size_t wins = 0;
    std::size_t jackpots = 0;
    for (int spin = 0; spin < 5'000; ++spin) {
        const metaverse::SlotOutcome outcome =
            metaverse::SpinSlots(credits, random);
        Check(outcome.spun, "positive slot balance permits a spin");
        Check(outcome.wager <= 50.0f, "slot wager is capped at 50 credits");
        Check(std::all_of(
                  outcome.symbols.begin(), outcome.symbols.end(),
                  [](std::int32_t symbol) { return symbol >= 0 && symbol < 5; }
              ),
              "slot symbols stay in the original five-symbol range");
        wins += outcome.won ? 1 : 0;
        jackpots += outcome.jackpot ? 1 : 0;
    }
    Check(wins > 3'000 && wins < 3'800,
          "slot forced-match branch reproduces approximately 68 percent wins");
    Check(jackpots > 20 && jackpots < 120,
          "slot jackpot is rare among winning spins");

    float low_credits = 25.0f;
    const metaverse::SlotOutcome low = metaverse::SpinSlots(low_credits, random);
    Check(low.wager == 2.5f, "slot wager uses ten percent below the cap");
    float no_credits = 0.0f;
    const metaverse::SlotOutcome none = metaverse::SpinSlots(no_credits, random);
    Check(!none.spun && no_credits == 0.0f,
          "zero-credit slot spin is rejected without changing balance");
    float negative_zero_credits = std::bit_cast<float>(0x80000000U);
    const metaverse::SlotOutcome negative_zero =
        metaverse::SpinSlots(negative_zero_credits, random);
    Check(
        !negative_zero.spun &&
            std::bit_cast<std::uint32_t>(negative_zero_credits) == 0x80000000U,
        "slot zero-credit gate ignores only the sign bit like MM.EXE"
    );

    const float positive_nan = std::bit_cast<float>(0x7fc00001U);
    const float negative_nan = std::bit_cast<float>(0xffc00001U);
    Check(
        metaverse::SlotWagerForCredits(positive_nan) == 50.0f &&
            std::bit_cast<std::uint32_t>(
                metaverse::SlotWagerForCredits(negative_nan)
            ) == 0xffc00001U,
        "slot wager cap uses MM.EXE's signed raw-float-bit comparison"
    );
    metaverse::SlotOutcome negative_balance_match;
    negative_balance_match.won = true;
    negative_balance_match.credit_change = -5.0f;
    metaverse::SlotOutcome negative_balance_loss;
    negative_balance_loss.won = false;
    negative_balance_loss.credit_change = 5.0f;
    Check(
        metaverse::SlotResultAmountX(negative_balance_match) ==
                metaverse::kSlotWinAmountX &&
            metaverse::SlotResultAmountX(negative_balance_loss) ==
                metaverse::kSlotLossAmountX,
        "slot result text side follows match state rather than amount sign"
    );
}

void TestLooksPavilion() {
    Check(metaverse::kPavilionCreditsX == 465 &&
              metaverse::kPavilionCreditsY == 25,
          "all pavilion painters retain the exact cash origin");
    Check(metaverse::kPavilionPortraitX == 287 &&
              metaverse::kPavilionPortraitY == 412 &&
              metaverse::kPavilionPortraitSize == 60 &&
              metaverse::kPavilionPortraitPitch == 70 &&
              metaverse::kPavilionPortraitNameY == 388,
          "all pavilion painters retain the exact portrait-strip geometry");
    Check(metaverse::PavilionPortraitAt(287, 412) == 0 &&
              metaverse::PavilionPortraitAt(346, 471) == 0 &&
              !metaverse::PavilionPortraitAt(347, 430) &&
              metaverse::PavilionPortraitAt(567, 412) == 4 &&
              !metaverse::PavilionPortraitAt(567, 472),
          "pavilion hover-name hit testing preserves rectangle edges and gaps");
    Check(metaverse::PavilionCreditText(
              12.5f, metaverse::JudgingCategory::talent
          ) == L"$12.50   " &&
              metaverse::PavilionCreditText(
                  12.5f, metaverse::JudgingCategory::brains
              ) == L"$12.50   " &&
              metaverse::PavilionCreditText(
                  12.5f, metaverse::JudgingCategory::looks
              ) == L"$12.50",
          "pavilion cash strings preserve the wide/Looks repaint formats");
    metaverse::GameRoundState frame_state;
    auto visit = metaverse::BeginPavilionVisit(
        frame_state, metaverse::JudgingCategory::looks
    );
    Check(metaverse::PavilionPortraitHasActiveFrame(
              visit, true, 2, 2
          ) &&
              !metaverse::PavilionPortraitHasActiveFrame(
                  visit, true, 2, 1
              ),
          "pavilion red frame follows only the active pending portrait");
    metaverse::CompletePavilionPendingSlot(visit, 2);
    Check(!metaverse::PavilionPortraitHasActiveFrame(
              visit, true, 2, 2
          ) &&
              frame_state.contestants[2].pending[
                  static_cast<std::size_t>(
                      metaverse::JudgingCategory::looks
                  )
              ],
          "completed pavilion slot loses its frame before document commit");
    Check(metaverse::NextPavilionPendingSlot(visit, 2) == 3 &&
              !metaverse::NextPavilionPendingSlot(visit, 5),
          "pavilion visit scans only its dialog-local pending array");
    auto completed_visit = visit;
    for (std::size_t slot = 0; slot < metaverse::kContestantsPerRound; ++slot) {
        metaverse::CompletePavilionPendingSlot(completed_visit, slot);
    }
    Check(!metaverse::NextPavilionPendingSlot(completed_visit, 0),
          "exhausted Looks activation has no replacement pending selection");
    const auto next_looks = metaverse::PlanLooksAutomaticActivation(visit);
    const auto final_looks =
        metaverse::PlanLooksAutomaticActivation(completed_visit);
    Check(next_looks.next_slot == 0 &&
              next_looks.numeric_rating_after == 0 &&
              next_looks.replace_rating_bitmap &&
              !final_looks.next_slot &&
              final_looks.numeric_rating_after == 0 &&
              final_looks.replace_rating_bitmap,
          "Looks automatic activation resets both the value and visible meter");
    using WideEvent = metaverse::WidePavilionRatingEvent;
    const auto charged = metaverse::PlanWidePavilionRatingTransition(
        WideEvent::performance_charge_succeeded, 8
    );
    const auto no_cash = metaverse::PlanWidePavilionRatingTransition(
        WideEvent::performance_charge_failed, 8
    );
    const auto completed = metaverse::PlanWidePavilionRatingTransition(
        WideEvent::result_completed, 8
    );
    Check(charged.numeric_rating_after == 0 &&
              charged.replace_rating_bitmap &&
              !charged.hide_rating_surface &&
              !charged.rebuild_pavilion && !charged.restart_ambient &&
              !charged.preserve_current_performance &&
              no_cash.numeric_rating_after == 8 &&
              !no_cash.replace_rating_bitmap &&
              !no_cash.hide_rating_surface &&
              !no_cash.rebuild_pavilion && !no_cash.restart_ambient &&
              !no_cash.preserve_current_performance &&
              completed.numeric_rating_after == 8 &&
              !completed.replace_rating_bitmap &&
              !completed.hide_rating_surface &&
              !completed.rebuild_pavilion && completed.restart_ambient &&
              completed.preserve_current_performance,
          "wide activation loads a visible zero gauge while results preserve it and an underlying performance child");
    metaverse::CommitPavilionVisit(
        frame_state, metaverse::JudgingCategory::looks, visit
    );
    Check(!frame_state.contestants[2].pending[
              static_cast<std::size_t>(metaverse::JudgingCategory::looks)
          ] &&
              frame_state.contestants[2].pending[
                  static_cast<std::size_t>(
                      metaverse::JudgingCategory::brains
                  )
              ],
          "pavilion close commits all five slots for only its category");
    frame_state.contestants[4].disqualified = true;
    const auto reentered_visit = metaverse::BeginPavilionVisit(
        frame_state, metaverse::JudgingCategory::looks
    );
    Check(!reentered_visit.pending[2] && !reentered_visit.pending[4] &&
              reentered_visit.pending[3],
          "pavilion entry copies document flags and masks disqualified slots");
    using WideAction = metaverse::WidePavilionReleaseAction;
    Check(metaverse::WidePavilionReleaseAt(true, 25, 368) ==
              WideAction::accept &&
              metaverse::WidePavilionReleaseAt(true, 547, 164) ==
                  WideAction::gong &&
              metaverse::WidePavilionReleaseAt(true, 534, 218) ==
                  WideAction::penalty &&
              metaverse::WidePavilionReleaseAt(true, 400, 300) ==
                  WideAction::none &&
              metaverse::WidePavilionReleaseAt(false, 25, 368) ==
                  WideAction::none,
          "wide pavilion shared press flag dispatches from the release target");
    bool wide_frame_dirty = true;
    Check(
        metaverse::ConsumeWidePavilionActiveFrame(
            wide_frame_dirty, reentered_visit, true, 3
        ) && !wide_frame_dirty &&
            !metaverse::ConsumeWidePavilionActiveFrame(
                wide_frame_dirty, reentered_visit, true, 3
            ),
        "wide pavilion red selection frame is consumed by one painter invocation"
    );
    wide_frame_dirty = true;
    Check(
        !metaverse::ConsumeWidePavilionActiveFrame(
            wide_frame_dirty, reentered_visit, true, 4
        ) && !wide_frame_dirty,
        "wide pavilion consumes its selection dirty flag even for a masked slot"
    );
    using WideCloseOwner = metaverse::WidePavilionCloseOwner;
    constexpr std::array<WideCloseOwner, 11> expected_wide_close_order{{
        WideCloseOwner::palette,
        WideCloseOwner::accept,
        WideCloseOwner::gong,
        WideCloseOwner::penalty,
        WideCloseOwner::base,
        WideCloseOwner::rating,
        WideCloseOwner::portrait_0,
        WideCloseOwner::portrait_1,
        WideCloseOwner::portrait_2,
        WideCloseOwner::portrait_3,
        WideCloseOwner::portrait_4,
    }};
    Check(metaverse::kWidePavilionCloseOrder == expected_wide_close_order,
          "wide pavilion releases every bitmap before its movie child in exact order");
    using WideAcceptOp = metaverse::WidePavilionAcceptOperation;
    Check(
        metaverse::kWidePavilionAcceptSequence ==
                std::array<WideAcceptOp, 8>{
                    WideAcceptOp::replace_current_portrait_with_dimmed,
                    WideAcceptOp::write_document_score,
                    WideAcceptOp::clear_dialog_pending,
                    WideAcceptOp::invalidate_current_portrait,
                    WideAcceptOp::invalidate_rating_graphic,
                    WideAcceptOp::force_update,
                    WideAcceptOp::clear_active_slot,
                    WideAcceptOp::dispatch_score_reaction,
                } &&
            metaverse::kWidePavilionRatingGraphicBounds ==
                std::array<int, 4>{69, 63, 131, 281} &&
            metaverse::kWidePavilionActivationCreditsBounds ==
                std::array<int, 4>{465, 24, 600, 45} &&
            metaverse::kWidePavilionPortraitStripBounds ==
                std::array<int, 4>{287, 412, 640, 480} &&
            metaverse::kPavilionPortraitHoverDirtyBounds ==
                std::array<int, 4>{287, 388, 640, 410} &&
            metaverse::PavilionPortraitBounds(0) ==
                std::array<int, 4>{287, 412, 347, 472} &&
            metaverse::PavilionPortraitBounds(4) ==
                std::array<int, 4>{567, 412, 627, 472},
        "wide ACCEPT preserves exact mutation, repaint, active-slot, and X-dispatch order"
    );
    using WideGongOp = metaverse::WidePavilionGongOperation;
    Check(
        metaverse::kWidePavilionGongSequence ==
            std::array<WideGongOp, 11>{
                WideGongOp::tear_down_current_performance,
                WideGongOp::replace_current_portrait_with_gong,
                WideGongOp::clear_dialog_pending,
                WideGongOp::write_zero_document_score,
                WideGongOp::invalidate_current_portrait,
                WideGongOp::invalidate_rating_graphic,
                WideGongOp::force_update,
                WideGongOp::clear_active_slot,
                WideGongOp::play_gong_synchronously,
                WideGongOp::conditionally_dispatch_c61,
                WideGongOp::restart_ambient,
            },
        "wide Gong preserves bitmap-warning, mutation, repaint, C61, and ambient order"
    );
    using WrongPenaltyOp = metaverse::WidePavilionWrongPenaltyOperation;
    using CorrectPenaltyOp = metaverse::WidePavilionCorrectPenaltyOperation;
    Check(
        metaverse::kWidePavilionWrongPenaltySequence ==
                std::array<WrongPenaltyOp, 5>{
                    WrongPenaltyOp::tear_down_current_performance,
                    WrongPenaltyOp::invalidate_credits,
                    WrongPenaltyOp::play_wrong5_synchronously,
                    WrongPenaltyOp::debit_ten_percent,
                    WrongPenaltyOp::force_update,
                } &&
            metaverse::kWidePavilionCorrectPenaltySequence ==
                std::array<CorrectPenaltyOp, 16>{
                    CorrectPenaltyOp::tear_down_current_performance,
                    CorrectPenaltyOp::invalidate_credits,
                    CorrectPenaltyOp::play_reward1_synchronously,
                    CorrectPenaltyOp::refund_sponsorship,
                    CorrectPenaltyOp::replace_current_portrait_with_penalty,
                    CorrectPenaltyOp::clear_dialog_pending,
                    CorrectPenaltyOp::write_zero_document_score,
                    CorrectPenaltyOp::invalidate_current_portrait,
                    CorrectPenaltyOp::invalidate_rating_graphic,
                    CorrectPenaltyOp::force_update,
                    CorrectPenaltyOp::set_disqualified,
                    CorrectPenaltyOp::clear_active_slot,
                    CorrectPenaltyOp::play_penalty_synchronously,
                    CorrectPenaltyOp::play_contestant_response_wait,
                    CorrectPenaltyOp::conditionally_dispatch_c71,
                    CorrectPenaltyOp::restart_ambient,
                } &&
            metaverse::kWidePavilionCreditsDirtyBounds ==
                std::array<int, 4>{465, 24, 600, 40},
        "wide Penalty preserves WRONG5/debit and REWARD/refund/portrait/response/C71 ordering"
    );
    Check(metaverse::LooksInitialFrame(1) == 0,
          "first Looks contestant begins at rotate frame zero");
    Check(metaverse::LooksInitialFrame(10) == 360,
          "tenth Looks contestant begins at rotate frame 360");

    const auto front_to_back = metaverse::LooksRotationFrames(3, true);
    Check(front_to_back.first_frame == 80 && front_to_back.last_frame == 99 &&
              !front_to_back.front_after,
          "Looks front-to-back uses the first twenty-frame segment");
    const auto back_to_front = metaverse::LooksRotationFrames(3, false);
    Check(back_to_front.first_frame == 100 && back_to_front.last_frame == 119 &&
              back_to_front.front_after,
          "Looks back-to-front uses the second twenty-frame segment");
    for (std::int32_t contestant = 1; contestant <= 10; ++contestant) {
        const std::int64_t base =
            static_cast<std::int64_t>(contestant - 1) * 40;
        const auto to_back = metaverse::LooksRotationFrames(contestant, true);
        const auto to_front = metaverse::LooksRotationFrames(contestant, false);
        Check(to_back.first_frame == base &&
                  to_back.last_frame == base + 19 &&
                  !to_back.front_after &&
                  to_front.first_frame == base + 20 &&
                  to_front.last_frame == base + 39 &&
                  to_front.front_after,
              "Looks rotation exhaustively covers both ranges for all contestants");
    }

    Check(metaverse::LooksMagnifierRegionAt(true, 0, 0) == 1,
          "Looks front head hit band");
    Check(!metaverse::LooksMagnifierRegionAt(true, 100, 103),
          "Looks preserves first one-pixel hit-band gap");
    Check(metaverse::LooksMagnifierRegionAt(true, 100, 104) == 2,
          "Looks front torso hit band");
    Check(!metaverse::LooksMagnifierRegionAt(true, 100, 208),
          "Looks preserves second one-pixel hit-band gap");
    Check(metaverse::LooksMagnifierRegionAt(true, 210, 311) == 3,
          "Looks front legs hit band");
    Check(metaverse::LooksMagnifierRegionAt(false, 100, 100) == 4,
          "Looks back view maps the whole body to region four");
    Check(!metaverse::LooksMagnifierRegionAt(false, 211, 100),
          "Looks child right edge remains exclusive");
    Check(metaverse::kLooksPresentationLeft == 225 &&
              metaverse::kLooksPresentationTop == 64 &&
              metaverse::kLooksPresentationRight == 436 &&
              metaverse::kLooksPresentationBottom == 377 &&
              metaverse::kLooksRotateWidth == 211 &&
              metaverse::kLooksRotateHeight == 312 &&
              metaverse::kLooksRotateWindowWidth == 212 &&
              metaverse::kLooksPenaltyResponseWidth == 200 &&
              metaverse::kLooksPenaltyResponseHeight == 320 &&
              metaverse::kLooksInitialMagnificationLevel == 1,
          "Looks rotate, penalty-response, and magnifier extents");
    using PresentationAction = metaverse::LooksPresentationAction;
    const auto initial_presentation = metaverse::PlanLooksPresentation(
        PresentationAction::show_background, false, true
    );
    const auto magnified_presentation = metaverse::PlanLooksPresentation(
        PresentationAction::show_magnifier, true, true
    );
    const auto restored_presentation = metaverse::PlanLooksPresentation(
        PresentationAction::show_rotate, true, true
    );
    const auto failed_magnifier_presentation =
        metaverse::PlanLooksPresentation(
            PresentationAction::show_magnifier, true, false
        );
    Check(!initial_presentation.rotate_visible &&
              !initial_presentation.magnifier_visible &&
              !magnified_presentation.rotate_visible &&
              magnified_presentation.magnifier_visible &&
              restored_presentation.rotate_visible &&
              !restored_presentation.magnifier_visible &&
              !failed_magnifier_presentation.rotate_visible &&
              !failed_magnifier_presentation.magnifier_visible,
          "Looks retains independent MAG/ROTATE owners while exposing one surface");

    using RatingAction = metaverse::LooksRatingSurfaceAction;
    const auto constructed_rating = metaverse::PlanLooksRatingSurface(
        RatingAction::construction, 0, false
    );
    const auto positive_rating = metaverse::PlanLooksRatingSurface(
        RatingAction::select_value, 7, false
    );
    const auto zero_rating = metaverse::PlanLooksRatingSurface(
        RatingAction::select_value, 0, true
    );
    const auto switched_rating = metaverse::PlanLooksRatingSurface(
        RatingAction::manual_contestant_switch, 7, true
    );
    const auto accepted_rating = metaverse::PlanLooksRatingSurface(
        RatingAction::automatic_advance, 0, true
    );
    const auto penalized_rating = metaverse::PlanLooksRatingSurface(
        RatingAction::correct_penalty, 7, true
    );
    Check(constructed_rating.replacement_rating == 1 &&
              !constructed_rating.visible &&
              positive_rating.replacement_rating == 7 &&
              positive_rating.visible &&
              !zero_rating.replacement_rating && !zero_rating.visible &&
              !switched_rating.replacement_rating &&
              !switched_rating.visible &&
              !accepted_rating.replacement_rating &&
              accepted_rating.visible &&
              !penalized_rating.replacement_rating &&
              !penalized_rating.visible,
          "Looks gauge preload, hide-without-release, and ACCEPT retention plan");
    const auto c41_dispatch = metaverse::PlanLooksC41MediaDispatch();
    const auto accept_dispatch =
        metaverse::PlanLooksAcceptMediaDispatch(8);
    Check(c41_dispatch.generic_arg_1 == 0 &&
              c41_dispatch.primary_index == 4 &&
              c41_dispatch.mode == 6 &&
              c41_dispatch.generic_arg_4 == 0 &&
              c41_dispatch.selector == 1 &&
              c41_dispatch.stop_ambient_sound &&
              c41_dispatch.mark_backing_dirty &&
              c41_dispatch.reposition_rotate_child &&
              accept_dispatch.generic_arg_1 == 0 &&
              accept_dispatch.primary_index == 0 &&
              accept_dispatch.mode == 4 &&
              accept_dispatch.generic_arg_4 == 0 &&
              accept_dispatch.selector == 8 &&
              accept_dispatch.stop_ambient_sound &&
              accept_dispatch.mark_backing_dirty &&
              accept_dispatch.reposition_rotate_child &&
              metaverse::HostReactionGroupForScore(
                  accept_dispatch.selector
              ) == 2,
          "Looks dispatcher preserves exact C41 and rating-based X arguments");
    using LooksOwner = metaverse::LooksBitmapOwner;
    constexpr std::array<LooksOwner, 11> expected_construction{{
        LooksOwner::base,
        LooksOwner::accept,
        LooksOwner::penalty,
        LooksOwner::rotate_control,
        LooksOwner::magnifier,
        LooksOwner::rating,
        LooksOwner::portrait_0,
        LooksOwner::portrait_1,
        LooksOwner::portrait_2,
        LooksOwner::portrait_3,
        LooksOwner::portrait_4,
    }};
    constexpr std::array<LooksOwner, 11> expected_teardown{{
        LooksOwner::accept,
        LooksOwner::penalty,
        LooksOwner::rotate_control,
        LooksOwner::magnifier,
        LooksOwner::base,
        LooksOwner::rating,
        LooksOwner::portrait_0,
        LooksOwner::portrait_1,
        LooksOwner::portrait_2,
        LooksOwner::portrait_3,
        LooksOwner::portrait_4,
    }};
    Check(metaverse::kLooksBitmapConstructionOrder == expected_construction &&
              metaverse::kLooksBitmapTeardownOrder == expected_teardown,
          "Looks bitmap construction and close ownership order");
    Check(metaverse::LooksInitialFrame(0) == 0,
          "unselected completed-pavilion ROTATE child remains at frame zero");
    using ParentCursor = metaverse::LooksParentCursorAction;
    Check(
        metaverse::LooksParentCursorActionAt(59, 381) ==
                ParentCursor::set_exit_174 &&
            metaverse::LooksParentCursorActionAt(60, 381) ==
                ParentCursor::set_hand_166 &&
            metaverse::LooksParentCursorActionAt(59, 382) ==
                ParentCursor::set_hand_166 &&
            metaverse::LooksParentCursorActionAt(580, 382) ==
                ParentCursor::set_hand_166 &&
            metaverse::LooksParentCursorActionAt(581, 381) ==
                ParentCursor::set_hand_166 &&
            metaverse::LooksParentCursorActionAt(581, 382) ==
                ParentCursor::preserve_current &&
            metaverse::kLooksPortraitHoverDirtyLeft == 287 &&
            metaverse::kLooksPortraitHoverDirtyTop == 388 &&
            metaverse::kLooksPortraitHoverDirtyRight == 640 &&
            metaverse::kLooksPortraitHoverDirtyBottom == 410,
        "Looks parent cursor boundaries and hover-name repaint strip"
    );
    using LooksControl = metaverse::LooksControlAction;
    Check(
        metaverse::LooksControlAtPointer(17, 381) == LooksControl::accept &&
            metaverse::LooksControlAtPointer(167, 477) ==
                LooksControl::accept &&
            !metaverse::LooksControlAtPointer(168, 477) &&
            metaverse::LooksControlAtPointer(174, 411) ==
                LooksControl::penalty &&
            metaverse::LooksControlAtPointer(276, 470) ==
                LooksControl::penalty &&
            !metaverse::LooksControlAtPointer(277, 470) &&
            metaverse::LooksControlAtPointer(506, 237) ==
                LooksControl::rotate &&
            metaverse::LooksControlAtPointer(617, 377) ==
                LooksControl::rotate &&
            !metaverse::LooksControlAtPointer(618, 377),
        "Looks ACCEPT/PENALTY/ROTATE rectangles preserve exclusive edges"
    );
    for (std::int32_t y = 0; y < 480; ++y) {
        for (std::int32_t x = 0; x < 640; ++x) {
            std::optional<std::int32_t> expected;
            if (x >= 62 && x < 118 && y >= 13 && y < 278) {
                expected = std::clamp((13 - y) / 24 + 10, 0, 10);
            }
            Check(
                metaverse::LooksMeterRatingAtPointer(x, y) == expected,
                "Looks Judge-O-Matic exhaustive 640x480 partition"
            );
        }
    }
    const auto accept_inside = metaverse::PlanLooksControlRelease(
        LooksControl::accept, true
    );
    const auto accept_outside = metaverse::PlanLooksControlRelease(
        LooksControl::accept, false
    );
    const auto penalty_inside = metaverse::PlanLooksControlRelease(
        LooksControl::penalty, true
    );
    const auto penalty_outside = metaverse::PlanLooksControlRelease(
        LooksControl::penalty, false
    );
    const auto rotate_inside = metaverse::PlanLooksControlRelease(
        LooksControl::rotate, true
    );
    const auto rotate_outside = metaverse::PlanLooksControlRelease(
        LooksControl::rotate, false
    );
    Check(
        accept_inside.dispatch && !accept_inside.retain_logical_action &&
            !accept_inside.retain_pressed_surface &&
            !accept_inside.force_update_before_dispatch &&
            accept_inside.restore_after_dispatch_phase &&
            !accept_outside.dispatch &&
            !accept_outside.retain_logical_action &&
            !accept_outside.retain_pressed_surface &&
            !accept_outside.force_update_before_dispatch &&
            accept_outside.restore_after_dispatch_phase &&
            penalty_inside.dispatch &&
            penalty_inside.retain_logical_action &&
            penalty_inside.retain_pressed_surface &&
            !penalty_inside.force_update_before_dispatch &&
            !penalty_inside.restore_after_dispatch_phase &&
            !penalty_outside.dispatch &&
            penalty_outside.retain_logical_action &&
            penalty_outside.retain_pressed_surface &&
            !penalty_outside.force_update_before_dispatch &&
            !penalty_outside.restore_after_dispatch_phase &&
            rotate_inside.dispatch &&
            !rotate_inside.retain_logical_action &&
            rotate_inside.retain_pressed_surface &&
            rotate_inside.force_update_before_dispatch &&
            !rotate_inside.restore_after_dispatch_phase &&
            !rotate_outside.dispatch &&
            !rotate_outside.retain_logical_action &&
            rotate_outside.retain_pressed_surface &&
            rotate_outside.force_update_before_dispatch &&
            !rotate_outside.restore_after_dispatch_phase,
        "Looks coded release preserves logical-latch and pressed-pixel asymmetry"
    );
    using LooksPaint = metaverse::LooksPaintOperation;
    metaverse::LooksPainterState looks_painter{
        true, true, true, true, true, true, true, std::size_t{0}
    };
    const auto full_looks_paint = metaverse::ConsumeLooksPaintPlan(
        looks_painter
    );
    constexpr std::array<LooksPaint, 10> expected_looks_paint{{
        LooksPaint::backing,
        LooksPaint::accept,
        LooksPaint::penalty,
        LooksPaint::rotate_control,
        LooksPaint::magnifier,
        LooksPaint::credits,
        LooksPaint::rating,
        LooksPaint::portraits,
        LooksPaint::selection_frames,
        LooksPaint::hover_name,
    }};
    Check(
        full_looks_paint.count == expected_looks_paint.size() &&
            full_looks_paint.operations == expected_looks_paint &&
            !looks_painter.backing_dirty &&
            !looks_painter.rating_dirty &&
            !looks_painter.accept_dirty &&
            !looks_painter.rotate_control_dirty &&
            !looks_painter.magnifier_dirty &&
            !looks_painter.penalty_dirty &&
            !looks_painter.selection_frames_dirty &&
            looks_painter.hovered_slot == 0,
        "Looks painter consumes dirty flags in recovered operation order"
    );
    const auto steady_looks_paint = metaverse::ConsumeLooksPaintPlan(
        looks_painter
    );
    Check(
        steady_looks_paint.count == 3 &&
            steady_looks_paint.operations[0] == LooksPaint::credits &&
            steady_looks_paint.operations[1] == LooksPaint::portraits &&
            steady_looks_paint.operations[2] == LooksPaint::hover_name,
        "Looks painter always redraws credits, portraits, and active hover name"
    );
    using PenaltyPhase = metaverse::LooksPenaltyPresentationPhase;
    Check(!metaverse::LooksRotateVisibleForPenaltyPhase(
              PenaltyPhase::response, true
          ) &&
              metaverse::LooksRotateVisibleForPenaltyPhase(
                  PenaltyPhase::restored, true
              ) &&
              !metaverse::LooksRotateVisibleForPenaltyPhase(
                  PenaltyPhase::restored, false
              ),
          "Looks penalty hides ROTATE through LP and restores retained media");
    for (bool front_view : {false, true}) {
        for (std::int32_t y = -1;
             y <= metaverse::kLooksRotateHeight; ++y) {
            for (std::int32_t x = -1;
                 x <= metaverse::kLooksRotateWidth; ++x) {
                std::optional<std::int32_t> expected;
                if (x >= 0 && x < metaverse::kLooksRotateWidth &&
                    y >= 0 && y < metaverse::kLooksRotateHeight) {
                    if (!front_view) {
                        expected = 4;
                    } else if (y < 103) {
                        expected = 1;
                    } else if (y >= 104 && y < 208) {
                        expected = 2;
                    } else if (y >= 209) {
                        expected = 3;
                    }
                }
                Check(
                    metaverse::LooksMagnifierRegionAt(front_view, x, y) ==
                        expected,
                    "Looks magnifier exhaustive local-coordinate partition"
                );
            }
        }
    }

    constexpr std::array<std::int32_t, 10> expected_specials = {
        4, 2, 3, 2, 1, 2, 4, 1, 1, 2
    };
    for (std::int32_t contestant = 1; contestant <= 10; ++contestant) {
        const auto index = static_cast<std::size_t>(contestant - 1);
        Check(
            metaverse::LooksJudgeCadetSpecialRegion(contestant) ==
                expected_specials[index],
            "Looks judge-cadet special-region table"
        );
    }
    Check(metaverse::LooksMagnifierFilename(1, 1, 2, false) == L"112.BMP",
          "Looks normal magnifier filename");
    Check(metaverse::LooksMagnifierFilename(10, 2, 5, false) == L"1025.BMP",
          "Looks concatenates multi-digit contestant filename");
    Check(metaverse::LooksMagnifierFilename(7, 4, 3, true) == L"IM74.BMP",
          "Looks judge-cadet substitution filename");
    Check(metaverse::LooksMagnifierFilename(7, 1, 3, true) == L"713.BMP",
          "Looks non-special judge-cadet region remains normal");
}

}  // namespace

int main() {
    TestLegacyDialogs();
    TestLegacyRandom();
    TestNavigation();
    TestSimmSelectionTable();
    TestHostInterstitialData();
    TestScene();
    TestConditionalSceneFields();
    TestMotionScript();
    TestMotionPlayer();
    TestSceneNavigator();
    TestLegacyProfiles();
    TestCategoryWeights();
    TestCommentOrder();
    TestRoundTally();
    TestPavilionRotation();
    TestHostReactionRotation();
    TestSlotMachine();
    TestLooksPavilion();
    if (failures != 0) {
        return EXIT_FAILURE;
    }
    std::cout << "OK game data parser tests\n";
    return EXIT_SUCCESS;
}
