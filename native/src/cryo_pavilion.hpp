#pragma once

#include "cryo_media.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace metaverse {

struct CryoRect {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;

    [[nodiscard]] constexpr bool Contains(
        std::int32_t x, std::int32_t y
    ) const {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

enum class CryoAction : std::int32_t {
    none = 0,
    name_bio = 1,
    ambitions = 2,
    turn_ons = 3,
    turn_offs = 4,
    ideal_man = 5,
    favorite_quote = 6,
    browse_left = 7,
    browse_right = 8,
    rotate = 9,
    information = 10,
    sponsor = 11,
};

inline constexpr std::array<CryoRect, 5> kCryoControlRects{{
    {33, 71, 93, 126},
    {98, 80, 152, 132},
    {53, 172, 129, 237},
    {52, 256, 136, 320},
    {54, 341, 129, 420},
}};

inline constexpr std::array<CryoRect, kCryoInfoDetailCount>
    kCryoDetailRects{{
        {470, 239, 602, 263},
        {488, 265, 584, 287},
        {492, 288, 582, 311},
        {490, 313, 588, 336},
        {491, 340, 582, 363},
        {456, 364, 615, 385},
    }};

inline constexpr CryoRect kCryoCashRect{465, 24, 565, 50};
inline constexpr CryoRect kCryoControlReleaseDirtyRect{33, 71, 157, 430};
inline constexpr CryoRect kCryoInformationPanelRect{432, 61, 640, 410};
inline constexpr CryoRect kCryoInformationReleaseDirtyRect{456, 242, 654, 410};
inline constexpr CryoRect kCryoCandidateCostDirtyRect{305, 129, 360, 143};
inline constexpr float kCryoCashControlClickAward = 100.0f;

inline constexpr std::array<std::wstring_view, 5>
    kCryoControlHighlightFiles{{
        L"BROWSL.BMP", L"BROWSR.BMP", L"ROTATE.BMP", L"INFO.BMP",
        L"SELECT.BMP",
    }};

inline constexpr std::array<std::wstring_view, kCryoInfoDetailCount>
    kCryoDetailHighlightFiles{{
        L"NAMEBIO.BMP", L"AMBITION.BMP", L"TURNON.BMP", L"TURNOFF.BMP",
        L"IDEALMAN.BMP", L"FAVORITE.BMP",
    }};

inline constexpr std::array<std::wstring_view, kCryoContestantCount>
    kCryoPortraitFiles{{
        L"GIRL1.BMP", L"GIRL2.BMP", L"GIRL3.BMP", L"GIRL4.BMP",
        L"GIRL5.BMP", L"GIRL6.BMP", L"GIRL7.BMP", L"GIRL8.BMP",
        L"GIRL9.BMP", L"GIRL10.BMP",
    }};

struct CryoStaticBitmapPlan {
    std::wstring_view filename;
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
    bool expose_immediately_after_load;
};

// Constructor 0x0040521d loads each wrapper independently and continues after
// every style-0x30 warning. CRYO is synchronously exposed by itself before the
// remaining fourteen wrappers are allocated and loaded.
inline constexpr std::array<CryoStaticBitmapPlan, 15>
    kCryoStaticBitmapLoadOrder{{
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

inline constexpr std::array<bool, 5> kCryoAllControlsAvailable{{
    true, true, true, true, true,
}};
inline constexpr std::array<bool, kCryoInfoDetailCount>
    kCryoAllDetailsAvailable{{
        true, true, true, true, true, true,
    }};

struct CryoMouseDownPlan {
    CryoAction action_after = CryoAction::none;
    bool grant_hidden_cash = false;
    bool force_synchronous_repaint = true;
};

// The handler at 0x00405d67 never clears +0xa8 before scanning. Thus a
// second button-down outside all targets retains an earlier press until the
// eventual button-up clears it. Target scans are likewise independent rather
// than an else-if chain, so a later matching rectangle wins.
[[nodiscard]] inline constexpr CryoMouseDownPlan PlanCryoMouseDown(
    CryoAction retained_action,
    bool information_visible,
    std::int32_t x,
    std::int32_t y,
    bool control_key_down,
    const std::array<bool, 5>& control_available =
        kCryoAllControlsAvailable,
    const std::array<bool, kCryoInfoDetailCount>& detail_available =
        kCryoAllDetailsAvailable
) {
    CryoMouseDownPlan plan{
        retained_action,
        control_key_down && kCryoCashRect.Contains(x, y),
        true,
    };
    for (std::size_t index = 0; index < kCryoControlRects.size(); ++index) {
        if (control_available[index] &&
            kCryoControlRects[index].Contains(x, y)) {
            plan.action_after = static_cast<CryoAction>(
                static_cast<std::int32_t>(CryoAction::browse_left) +
                static_cast<std::int32_t>(index)
            );
        }
    }
    if (information_visible) {
        for (std::size_t index = 0; index < kCryoDetailRects.size(); ++index) {
            if (detail_available[index] &&
                kCryoDetailRects[index].Contains(x, y)) {
                plan.action_after = static_cast<CryoAction>(
                    static_cast<std::int32_t>(CryoAction::name_bio) +
                    static_cast<std::int32_t>(index)
                );
            }
        }
    }
    return plan;
}

struct CryoMouseUpPlan {
    CryoAction captured_action = CryoAction::none;
    bool clear_action = true;
    bool repaint_controls_before_dispatch = false;
    bool dispatch = false;
    bool repaint_information_after_dispatch = false;
};

[[nodiscard]] inline constexpr CryoMouseUpPlan PlanCryoMouseUp(
    CryoAction captured_action,
    bool information_visible,
    std::int32_t x,
    std::int32_t y,
    const std::array<bool, 5>& control_available =
        kCryoAllControlsAvailable,
    const std::array<bool, kCryoInfoDetailCount>& detail_available =
        kCryoAllDetailsAvailable
) {
    const std::int32_t raw = static_cast<std::int32_t>(captured_action);
    CryoMouseUpPlan plan{
        captured_action,
        true,
        captured_action != CryoAction::none,
        false,
        information_visible && raw >= 1 && raw <= 6,
    };
    if (raw >= 7 && raw <= 11) {
        const std::size_t index = static_cast<std::size_t>(raw - 7);
        plan.dispatch = control_available[index] &&
            kCryoControlRects[index].Contains(x, y);
    } else if (information_visible && raw >= 1 && raw <= 6) {
        const std::size_t index = static_cast<std::size_t>(raw - 1);
        plan.dispatch = detail_available[index] &&
            kCryoDetailRects[index].Contains(x, y);
    }
    return plan;
}

enum class CryoBrowseStep {
    select_wrapped_contestant,
    stop_information,
    seek_information_name_bio,
    show_information,
    stop_rotation,
    seek_rotation_front,
    start_ambient_without_interrupting,
    store_rotation_marker,
    repaint_candidate_cost,
};

struct CryoBrowsePlan {
    std::int32_t contestant_id_after = 0;
    std::int64_t information_frame = -1;
    std::int64_t rotation_frame = -1;
    std::uint32_t legacy_sound_flags = 0;
    std::array<CryoBrowseStep, 9> steps{};
    std::size_t step_count = 0;
};

// fcn.00404714 and fcn.0040483b are byte-for-byte twins after their opposite
// wrap arithmetic. When INFO is visible, its stop/seek/show sequence precedes
// the unconditional ROTATE stop/seek. 0x19 is ASYNC|LOOP|NOSTOP, deliberately
// without NODEFAULT.
[[nodiscard]] inline constexpr std::optional<CryoBrowsePlan> PlanCryoBrowse(
    std::int32_t contestant_id_before,
    std::int32_t direction,
    bool information_visible
) {
    if (contestant_id_before < 1 ||
        contestant_id_before > static_cast<std::int32_t>(kCryoContestantCount) ||
        (direction != -1 && direction != 1)) {
        return std::nullopt;
    }
    CryoBrowsePlan plan;
    plan.contestant_id_after = contestant_id_before + direction;
    if (plan.contestant_id_after < 1) {
        plan.contestant_id_after = static_cast<std::int32_t>(
            kCryoContestantCount
        );
    } else if (plan.contestant_id_after >
               static_cast<std::int32_t>(kCryoContestantCount)) {
        plan.contestant_id_after = 1;
    }
    plan.information_frame = kCryoInfoRanges[
        static_cast<std::size_t>(plan.contestant_id_after - 1)
    ][0].first_frame;
    plan.rotation_frame =
        static_cast<std::int64_t>(plan.contestant_id_after - 1) * 40;
    plan.legacy_sound_flags = 0x19;
    plan.steps[plan.step_count++] = CryoBrowseStep::select_wrapped_contestant;
    if (information_visible) {
        plan.steps[plan.step_count++] = CryoBrowseStep::stop_information;
        plan.steps[plan.step_count++] = CryoBrowseStep::seek_information_name_bio;
        plan.steps[plan.step_count++] = CryoBrowseStep::show_information;
    }
    plan.steps[plan.step_count++] = CryoBrowseStep::stop_rotation;
    plan.steps[plan.step_count++] = CryoBrowseStep::seek_rotation_front;
    plan.steps[plan.step_count++] =
        CryoBrowseStep::start_ambient_without_interrupting;
    plan.steps[plan.step_count++] = CryoBrowseStep::store_rotation_marker;
    plan.steps[plan.step_count++] = CryoBrowseStep::repaint_candidate_cost;
    return plan;
}

enum class CryoInformationToggleStep {
    stop_information,
    hide_information,
    mark_rotation_dirty,
    seek_information_name_bio,
    mark_information_dirty,
    show_information,
    repaint_information_panel,
};

struct CryoInformationTogglePlan {
    bool visible_after = false;
    std::int64_t information_frame = -1;
    std::array<CryoInformationToggleStep, 4> steps{};
    std::size_t step_count = 0;
};

[[nodiscard]] inline constexpr std::optional<CryoInformationTogglePlan>
PlanCryoInformationToggle(bool visible_before, std::int32_t contestant_id) {
    if (contestant_id < 1 ||
        contestant_id > static_cast<std::int32_t>(kCryoContestantCount)) {
        return std::nullopt;
    }
    CryoInformationTogglePlan plan;
    plan.visible_after = !visible_before;
    plan.information_frame = kCryoInfoRanges[
        static_cast<std::size_t>(contestant_id - 1)
    ][0].first_frame;
    if (visible_before) {
        plan.steps[plan.step_count++] =
            CryoInformationToggleStep::stop_information;
        plan.steps[plan.step_count++] =
            CryoInformationToggleStep::hide_information;
        plan.steps[plan.step_count++] =
            CryoInformationToggleStep::mark_rotation_dirty;
    } else {
        plan.steps[plan.step_count++] =
            CryoInformationToggleStep::seek_information_name_bio;
        plan.steps[plan.step_count++] =
            CryoInformationToggleStep::mark_information_dirty;
        plan.steps[plan.step_count++] =
            CryoInformationToggleStep::show_information;
    }
    plan.steps[plan.step_count++] =
        CryoInformationToggleStep::repaint_information_panel;
    return plan;
}

struct CryoMediaChildPlan {
    std::wstring_view relative_path;
    std::uint32_t style;
    std::int32_t x;
    std::int32_t y;
    std::int32_t width_adjustment;
    bool bind_document_owner;
    bool seek_frame_zero;
    bool show_after_creation;
};

inline constexpr CryoMediaChildPlan kCryoRotationChildPlan{
    L"MOV\\CRYO\\ROTATE.AVI", 0x0400000aU,
    258, 142, -3, true, true, true,
};

inline constexpr CryoMediaChildPlan kCryoInformationChildPlan{
    L"MOV\\CRYO\\INFO.AVI", 0x0400000aU,
    464, 84, 0, true, true, false,
};

enum class CryoEntryStep {
    load_static_bitmaps,
    start_ambient,
    wait_500_milliseconds,
    run_c11,
    restart_ambient,
    construct_rotation_child,
    construct_information_child,
};

// Constructor 0x0040521d owns the static DIBs. Timer 0x00403f47 then runs
// this sequence without reconstructing the dialog: C11 is modal, ROTATE is
// created and shown after it returns, and INFO is created second but hidden.
// The continuation is identical when the generic host helper returns failure.
inline constexpr std::array<CryoEntryStep, 7> kCryoEntryOrder{{
    CryoEntryStep::load_static_bitmaps,
    CryoEntryStep::start_ambient,
    CryoEntryStep::wait_500_milliseconds,
    CryoEntryStep::run_c11,
    CryoEntryStep::restart_ambient,
    CryoEntryStep::construct_rotation_child,
    CryoEntryStep::construct_information_child,
}};

enum class CryoHostDispatchOperation {
    stop_legacy_audio,
    construct_generic_media_runner,
    run_mode_six_host_clip,
    destroy_generic_media_runner,
    restart_cryo_ambient,
};

inline constexpr std::array<CryoHostDispatchOperation, 5>
    kCryoHostDispatchOrder{{
        CryoHostDispatchOperation::stop_legacy_audio,
        CryoHostDispatchOperation::construct_generic_media_runner,
        CryoHostDispatchOperation::run_mode_six_host_clip,
        CryoHostDispatchOperation::destroy_generic_media_runner,
        CryoHostDispatchOperation::restart_cryo_ambient,
    }};
inline constexpr std::int32_t kCryoHostChannel = 1;
inline constexpr std::int32_t kCryoEntryHostTake = 1;
inline constexpr std::int32_t kCryoFirstSponsorHostTake = 2;
inline constexpr std::int32_t kCryoHostMediaMode = 6;

enum class CryoDialogTeardownOperation {
    close_rotation_child,
    close_information_child,
    stop_legacy_audio,
    release_browse_left,
    release_browse_right,
    release_rotate,
    release_information,
    release_sponsor,
    release_name_bio,
    release_measurements,
    release_turn_ons,
    release_turn_offs,
    release_what_she_wants,
    release_favorite_quote,
    release_backing_store,
    release_base,
    release_buttons,
    release_sponsored_portrait,
    release_information_background,
    release_money,
    end_dialog,
};

// Cryo destructor 0x0040621f closes ROTATE (+0x210) before INFO (+0x20c),
// then stops the shared sndPlaySound channel and deletes its owned DIBs in
// this order. The outer round orchestrator does not start generating the ten
// Pavilion variants until this whole sequence has returned.
inline constexpr std::array<CryoDialogTeardownOperation, 21>
    kCryoDialogTeardownOrder{{
        CryoDialogTeardownOperation::close_rotation_child,
        CryoDialogTeardownOperation::close_information_child,
        CryoDialogTeardownOperation::stop_legacy_audio,
        CryoDialogTeardownOperation::release_browse_left,
        CryoDialogTeardownOperation::release_browse_right,
        CryoDialogTeardownOperation::release_rotate,
        CryoDialogTeardownOperation::release_information,
        CryoDialogTeardownOperation::release_sponsor,
        CryoDialogTeardownOperation::release_name_bio,
        CryoDialogTeardownOperation::release_measurements,
        CryoDialogTeardownOperation::release_turn_ons,
        CryoDialogTeardownOperation::release_turn_offs,
        CryoDialogTeardownOperation::release_what_she_wants,
        CryoDialogTeardownOperation::release_favorite_quote,
        CryoDialogTeardownOperation::release_backing_store,
        CryoDialogTeardownOperation::release_base,
        CryoDialogTeardownOperation::release_buttons,
        CryoDialogTeardownOperation::release_sponsored_portrait,
        CryoDialogTeardownOperation::release_information_background,
        CryoDialogTeardownOperation::release_money,
        CryoDialogTeardownOperation::end_dialog,
    }};

struct CryoPortraitInsertionPlan {
    std::wstring_view relative_filename;
    std::size_t slot = 0;
    std::int32_t x = 0;
    std::int32_t y = 412;
    bool replace_previous_logical_wrapper = true;
    bool warn_on_load_failure = true;
    bool force_synchronous_repaint = true;
};

[[nodiscard]] inline constexpr std::optional<CryoPortraitInsertionPlan>
PlanCryoPortraitInsertion(
    std::int32_t contestant_id,
    std::size_t selected_count_after
) {
    if (contestant_id < 1 ||
        contestant_id > static_cast<std::int32_t>(kCryoContestantCount) ||
        selected_count_after < 1 || selected_count_after > 5) {
        return std::nullopt;
    }
    return CryoPortraitInsertionPlan{
        kCryoPortraitFiles[static_cast<std::size_t>(contestant_id - 1)],
        selected_count_after - 1,
        217 + static_cast<std::int32_t>(selected_count_after) * 70,
        412,
        true,
        true,
        true,
    };
}

inline constexpr std::uint32_t kCryoAmbientStartLegacySoundFlags = 0x09;
inline constexpr std::uint32_t kCryoBrowseLegacySoundFlags = 0x19;
inline constexpr std::uint32_t kCryoNoCashLegacySoundFlags = 0x00;

}  // namespace metaverse
