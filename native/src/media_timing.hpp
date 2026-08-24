#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace metaverse {

// Complete constants recovered from the resource-158 class at
// 0x004012b5-0x004015c0. This one class owns the intro, all ten winner movies,
// and the closing credits.
inline constexpr std::uint16_t kCenteredOneShotDialogResourceId = 158;
inline constexpr std::uint16_t kCenteredOneShotCursorResourceId = 166;
inline constexpr std::uint16_t kCenteredOneShotTimerId = 1;
inline constexpr std::uint32_t kCenteredOneShotPrivateCompleteMessage = 0x0405;
inline constexpr std::uint32_t kCenteredOneShotNotifySuccessful = 1;
inline constexpr std::uint32_t kCenteredOneShotBlacknessRop = 0x42;
inline constexpr int kCenteredOneShotWidth = 640;
inline constexpr int kCenteredOneShotHeight = 480;
inline constexpr int kCenteredOneShotZoomPercent = 200;
inline constexpr std::wstring_view kCenteredOneShotCaption = L"Ms. Metaverse";

enum class CenteredOneShotPurpose {
    intro,
    winner,
    credit,
};

enum class CenteredOneShotInitOperation {
    set_caption,
    center_640x480,
    invalidate_erase,
    update_window,
    arm_timer_1_100ms,
};

inline constexpr std::array<CenteredOneShotInitOperation, 5>
    kCenteredOneShotInitSequence{{
        CenteredOneShotInitOperation::set_caption,
        CenteredOneShotInitOperation::center_640x480,
        CenteredOneShotInitOperation::invalidate_erase,
        CenteredOneShotInitOperation::update_window,
        CenteredOneShotInitOperation::arm_timer_1_100ms,
    }};

enum class CenteredOneShotTimerOperation {
    kill_timer_1,
    allocate_media_wrapper,
    create_child_style_0x0a,
    zoom_200_percent,
    play_notify,
    mark_active,
};

inline constexpr std::array<CenteredOneShotTimerOperation, 6>
    kCenteredOneShotTimerSequence{{
        CenteredOneShotTimerOperation::kill_timer_1,
        CenteredOneShotTimerOperation::allocate_media_wrapper,
        CenteredOneShotTimerOperation::create_child_style_0x0a,
        CenteredOneShotTimerOperation::zoom_200_percent,
        CenteredOneShotTimerOperation::play_notify,
        CenteredOneShotTimerOperation::mark_active,
    }};

enum class CenteredOneShotInputAction {
    ignore,
    play_notify,
    stop_and_complete_zero,
};

inline constexpr CenteredOneShotInputAction kCenteredOneShotCancelAction =
    CenteredOneShotInputAction::ignore;

// The child-map left-click and owner OnOK handlers have the same active-state
// split. A stopped child is restarted with Play Notify; an active child is
// stopped and completes the owner with result zero.
[[nodiscard]] inline constexpr CenteredOneShotInputAction
CenteredOneShotActiveInputAction(bool playback_active) {
    return playback_active
        ? CenteredOneShotInputAction::stop_and_complete_zero
        : CenteredOneShotInputAction::play_notify;
}

[[nodiscard]] inline constexpr bool CenteredOneShotNotifyCompletes(
    std::uint32_t notify_code
) {
    return notify_code == kCenteredOneShotNotifySuccessful;
}

[[nodiscard]] inline constexpr std::int32_t CenteredOneShotPrivateResult(
    std::int32_t supplied_result
) {
    return supplied_result;
}

enum class CenteredOneShotCompletionOperation {
    close_media_device,
    destroy_media_wrapper,
    end_dialog_with_result,
};

inline constexpr std::array<CenteredOneShotCompletionOperation, 3>
    kCenteredOneShotCompletionSequence{{
        CenteredOneShotCompletionOperation::close_media_device,
        CenteredOneShotCompletionOperation::destroy_media_wrapper,
        CenteredOneShotCompletionOperation::end_dialog_with_result,
    }};

enum class CenteredOneShotContinuation {
    enter_game,
    finish_tally,
    exit_application,
};

[[nodiscard]] inline constexpr CenteredOneShotContinuation
CenteredOneShotContinuationFor(CenteredOneShotPurpose purpose) {
    switch (purpose) {
        case CenteredOneShotPurpose::intro:
            return CenteredOneShotContinuation::enter_game;
        case CenteredOneShotPurpose::winner:
            return CenteredOneShotContinuation::finish_tally;
        case CenteredOneShotPurpose::credit:
            return CenteredOneShotContinuation::exit_application;
    }
    return CenteredOneShotContinuation::exit_application;
}

// The centered resource-158 intro/winner/credit player passes style 0x0a to
// MCIWndCreateA. Bit 0x08 is MCIWNDF_NOERRORDLG: an open/play failure leaves
// the modal blank, with its active flag set, until inherited OK/Enter closes
// it. Escape/close remains a no-op, and an owner click cannot substitute for
// the missing child window's WM_LBUTTONDOWN handler.
inline constexpr std::uint32_t kLegacyCenteredOneShotMciStyle = 0x0a;
inline constexpr std::uint32_t kLegacyMciNoErrorDialogStyle = 0x08;

[[nodiscard]] inline constexpr bool LegacyMciSuppressesErrorDialog(
    std::uint32_t style
) {
    return (style & kLegacyMciNoErrorDialogStyle) != 0;
}

[[nodiscard]] inline constexpr bool CenteredOneShotFailureWaitsForOk() {
    return LegacyMciSuppressesErrorDialog(kLegacyCenteredOneShotMciStyle);
}

// Talent/Brains allocate their shared MCI wrapper object before calling
// 0x0040802f and ignore that create/open helper's return value. Their current-
// performance field therefore remains non-null even when MCIWnd cannot open
// the AVI. ACCEPT stays blocked, and Gong/Penalty teardown reports that it
// interrupted a performance, until the object is explicitly destroyed.
[[nodiscard]] inline constexpr bool LegacyWidePerformanceChildIsPresent(
    bool wrapper_object_constructed,
    bool /*decoder_open*/
) {
    return wrapper_object_constructed;
}

inline constexpr std::uint32_t kLegacyWidePerformanceMciStyle = 0x4000000b;
inline constexpr int kLegacyWidePerformanceShowCommand = 5;

// fcn.0040760a / fcn.00409844 destroy the prior wrapper, store a newly
// allocated one before calling shared creator 0x0040802f, ignore that helper's
// result, then move/show the child. This sequence explains why create failure
// still leaves a stored, input-blocking wrapper until explicit teardown.
enum class WidePerformanceCreateOperation {
    destroy_previous_wrapper,
    allocate_and_store_wrapper,
    create_or_open_media_child,
    move_child,
    show_child,
};

inline constexpr std::array<WidePerformanceCreateOperation, 5>
    kWidePerformanceCreateSequence{{
        WidePerformanceCreateOperation::destroy_previous_wrapper,
        WidePerformanceCreateOperation::allocate_and_store_wrapper,
        WidePerformanceCreateOperation::create_or_open_media_child,
        WidePerformanceCreateOperation::move_child,
        WidePerformanceCreateOperation::show_child,
    }};

inline constexpr std::array<int, 4> kLegacyWidePerformanceChildBounds{
    208, 108, 528, 348
};

// fcn.00407c38 and fcn.00409e76 use this exact repaint rectangle after
// stopping and destroying the Talent/Brains MCI child. It is deliberately
// four pixels shorter than the 320x240 child window installed at (208,108).
inline constexpr std::array<int, 4> kLegacyWidePerformanceRepaintBounds{
    208, 108, 528, 344
};

// The centered dialog's temporary MCI child uses the message map at
// 0x00430100. Its WM_LBUTTONDOWN handler at 0x004011d2 stops active playback
// and sends private message 0x0405 to the owner, whose 0x004014c5 handler
// completes the modal with result zero. Before the 100 ms timer constructs
// that child, or if MCIWndCreateA returns null after the timer, the owner dialog
// itself has no left-button handler.
[[nodiscard]] inline constexpr bool CenteredOneShotLeftClickFinishes(
    bool mci_child_created
) {
    return mci_child_created;
}

// The generic C/X/Simm class's WM_ACTIVATEAPP override at 0x0040ef28 ignores
// its BOOL bActive argument and calls its result-zero completion path for both
// activation and deactivation notifications.
[[nodiscard]] inline constexpr bool GenericMediaActivationChangeEndsModal(
    bool /*active*/
) {
    return true;
}

// MM.EXE uses three materially different wait styles around authored media.
// Notify playback returns to the owning dialog, and the Slots deadline loop
// explicitly pumps queued messages. In contrast, the Looks bounded rotation
// and TP/BP/LP response helpers send an MCI "Play ... Wait" command from the
// owner handler; that handler cannot dispatch another owner input message
// until the command returns.
enum class LegacyPlaybackWaitStyle {
    notify,
    synchronous_mci_wait,
    pumped_deadline,
};

[[nodiscard]] inline constexpr bool LegacyPlaybackBlocksOwnerInput(
    LegacyPlaybackWaitStyle style
) {
    return style == LegacyPlaybackWaitStyle::synchronous_mci_wait;
}

// The centered resource-158 modal used for MMINTRO.AVI, W1-W10.AVI, and
// CREDIT.AVI arms timer ID 1 for 100 ms before it constructs the MCI child.
// Pavilion performance and Penalty Box movies use different handlers and do
// not inherit this delay.
inline constexpr auto kCenteredOneShotPlaybackDelay =
    std::chrono::milliseconds(100);

}  // namespace metaverse
