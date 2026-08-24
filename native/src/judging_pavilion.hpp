#pragma once

#include "game_state.hpp"

#include <array>
#include <chrono>
#include <optional>
#include <string>

namespace metaverse {

inline constexpr std::chrono::milliseconds kJudgingPavilionEntryDelay{500};

// Talent, Brains, and Looks all arm timer 1 after their dialog bitmap wrappers
// are built. Consuming the document-lifetime C21/C31/C41 flag suppresses only
// the modal host call; the same 500 ms callback and post-host continuation still
// run. Looks alone constructs ROTATE and auto-selects a contestant in that tail.
struct JudgingEntryTimerPlan {
    std::chrono::milliseconds delay{};
    bool run_host_before_continuation = false;
    bool construct_rotate_after_timer = false;
    bool auto_select_after_timer = false;
    bool start_ambient_after_timer = true;
};

[[nodiscard]] inline constexpr JudgingEntryTimerPlan PlanJudgingEntryTimer(
    JudgingCategory category,
    bool host_entry_available
) {
    const bool looks = category == JudgingCategory::looks;
    return {
        kJudgingPavilionEntryDelay,
        host_entry_available,
        looks,
        looks,
        true,
    };
}

// The Talent, Brains, and Looks painters share the bottom portrait strip and
// cash origin. The wide painters append spaces because they repaint only the
// old text rectangle; Looks restores its backing before TextOut.
constexpr int kPavilionCreditsX = 465;
constexpr int kPavilionCreditsY = 25;
constexpr int kPavilionPortraitX = 287;
constexpr int kPavilionPortraitY = 412;
constexpr int kPavilionPortraitSize = 60;
constexpr int kPavilionPortraitPitch = 70;
constexpr int kPavilionPortraitNameY =
    kPavilionPortraitY - 24;

inline constexpr std::array<int, 4> kWidePavilionRatingGraphicBounds{
    69, 63, 131, 281
};

inline constexpr std::array<int, 4> kWidePavilionCreditsDirtyBounds{
    465, 24, 600, 40
};

// A successful Talent/Brains activation uses a taller cash invalidation than
// Penalty and separately updates the whole portrait/name strip before erasing
// the retained gauge surface.
inline constexpr std::array<int, 4> kWidePavilionActivationCreditsBounds{
    465, 24, 600, 45
};

inline constexpr std::array<int, 4> kWidePavilionPortraitStripBounds{
    287, 412, 640, 480
};

inline constexpr std::array<int, 4> kPavilionPortraitHoverDirtyBounds{
    287, 388, 640, 410
};

[[nodiscard]] inline constexpr std::array<int, 4>
PavilionPortraitBounds(std::size_t slot) {
    const int left = kPavilionPortraitX +
                     static_cast<int>(slot) * kPavilionPortraitPitch;
    return {
        left,
        kPavilionPortraitY,
        left + kPavilionPortraitSize,
        kPavilionPortraitY + kPavilionPortraitSize,
    };
}

enum class WidePavilionReleaseAction {
    none,
    accept,
    gong,
    penalty,
};

// Each pavilion copies its five category-pending document values into the
// modal dialog. ACCEPT/Gong/correct-Penalty mutate this local array; the close
// helper commits all five values back to the document before teardown.
struct PavilionVisitState {
    std::array<bool, kContestantsPerRound> pending{};
};

struct LooksAutomaticActivationPlan {
    std::optional<std::size_t> next_slot;
    std::int32_t numeric_rating_after = 0;
    // Refreshing the surface after the numeric reset prevents the next
    // contestant from inheriting the previous contestant's visible meter.
    // Looks has no 0.BMP, so its zero state is the clean background.
    bool replace_rating_bitmap = false;
};

enum class WidePavilionRatingEvent {
    performance_charge_succeeded,
    performance_charge_failed,
    result_completed,
};

enum class WidePavilionAcceptOperation {
    replace_current_portrait_with_dimmed,
    write_document_score,
    clear_dialog_pending,
    invalidate_current_portrait,
    invalidate_rating_graphic,
    force_update,
    clear_active_slot,
    dispatch_score_reaction,
};

inline constexpr std::array<WidePavilionAcceptOperation, 8>
    kWidePavilionAcceptSequence{{
        WidePavilionAcceptOperation::replace_current_portrait_with_dimmed,
        WidePavilionAcceptOperation::write_document_score,
        WidePavilionAcceptOperation::clear_dialog_pending,
        WidePavilionAcceptOperation::invalidate_current_portrait,
        WidePavilionAcceptOperation::invalidate_rating_graphic,
        WidePavilionAcceptOperation::force_update,
        WidePavilionAcceptOperation::clear_active_slot,
        WidePavilionAcceptOperation::dispatch_score_reaction,
    }};

enum class WidePavilionGongOperation {
    tear_down_current_performance,
    replace_current_portrait_with_gong,
    clear_dialog_pending,
    write_zero_document_score,
    invalidate_current_portrait,
    invalidate_rating_graphic,
    force_update,
    clear_active_slot,
    play_gong_synchronously,
    conditionally_dispatch_c61,
    restart_ambient,
};

inline constexpr std::array<WidePavilionGongOperation, 11>
    kWidePavilionGongSequence{{
        WidePavilionGongOperation::tear_down_current_performance,
        WidePavilionGongOperation::replace_current_portrait_with_gong,
        WidePavilionGongOperation::clear_dialog_pending,
        WidePavilionGongOperation::write_zero_document_score,
        WidePavilionGongOperation::invalidate_current_portrait,
        WidePavilionGongOperation::invalidate_rating_graphic,
        WidePavilionGongOperation::force_update,
        WidePavilionGongOperation::clear_active_slot,
        WidePavilionGongOperation::play_gong_synchronously,
        WidePavilionGongOperation::conditionally_dispatch_c61,
        WidePavilionGongOperation::restart_ambient,
    }};

enum class WidePavilionWrongPenaltyOperation {
    tear_down_current_performance,
    invalidate_credits,
    play_wrong5_synchronously,
    debit_ten_percent,
    force_update,
};

inline constexpr std::array<WidePavilionWrongPenaltyOperation, 5>
    kWidePavilionWrongPenaltySequence{{
        WidePavilionWrongPenaltyOperation::tear_down_current_performance,
        WidePavilionWrongPenaltyOperation::invalidate_credits,
        WidePavilionWrongPenaltyOperation::play_wrong5_synchronously,
        WidePavilionWrongPenaltyOperation::debit_ten_percent,
        WidePavilionWrongPenaltyOperation::force_update,
    }};

enum class WidePavilionCorrectPenaltyOperation {
    tear_down_current_performance,
    invalidate_credits,
    play_reward1_synchronously,
    refund_sponsorship,
    replace_current_portrait_with_penalty,
    clear_dialog_pending,
    write_zero_document_score,
    invalidate_current_portrait,
    invalidate_rating_graphic,
    force_update,
    set_disqualified,
    clear_active_slot,
    play_penalty_synchronously,
    play_contestant_response_wait,
    conditionally_dispatch_c71,
    restart_ambient,
};

inline constexpr std::array<WidePavilionCorrectPenaltyOperation, 16>
    kWidePavilionCorrectPenaltySequence{{
        WidePavilionCorrectPenaltyOperation::tear_down_current_performance,
        WidePavilionCorrectPenaltyOperation::invalidate_credits,
        WidePavilionCorrectPenaltyOperation::play_reward1_synchronously,
        WidePavilionCorrectPenaltyOperation::refund_sponsorship,
        WidePavilionCorrectPenaltyOperation::replace_current_portrait_with_penalty,
        WidePavilionCorrectPenaltyOperation::clear_dialog_pending,
        WidePavilionCorrectPenaltyOperation::write_zero_document_score,
        WidePavilionCorrectPenaltyOperation::invalidate_current_portrait,
        WidePavilionCorrectPenaltyOperation::invalidate_rating_graphic,
        WidePavilionCorrectPenaltyOperation::force_update,
        WidePavilionCorrectPenaltyOperation::set_disqualified,
        WidePavilionCorrectPenaltyOperation::clear_active_slot,
        WidePavilionCorrectPenaltyOperation::play_penalty_synchronously,
        WidePavilionCorrectPenaltyOperation::play_contestant_response_wait,
        WidePavilionCorrectPenaltyOperation::conditionally_dispatch_c71,
        WidePavilionCorrectPenaltyOperation::restart_ambient,
    }};

enum class WidePavilionHostDispatchEvent {
    entry,
    score_reaction,
    gong,
    penalty,
    completion,
};

enum class WidePavilionOuterAmbientAction {
    none,
    restart,
    stop,
};

// Talent 0x00407cb7 and Brains 0x00409ef5 are byte-equivalent after their
// different document offsets.  Their close helpers release these stored
// owners in this exact order; the 8-bit palette owner has no native object.
enum class WidePavilionCloseOwner {
    palette,
    accept,
    gong,
    penalty,
    base,
    rating,
    portrait_0,
    portrait_1,
    portrait_2,
    portrait_3,
    portrait_4,
};

inline constexpr std::array<WidePavilionCloseOwner, 11>
    kWidePavilionCloseOrder{{
        WidePavilionCloseOwner::palette,
        WidePavilionCloseOwner::accept,
        WidePavilionCloseOwner::gong,
        WidePavilionCloseOwner::penalty,
        WidePavilionCloseOwner::base,
        WidePavilionCloseOwner::rating,
        WidePavilionCloseOwner::portrait_0,
        WidePavilionCloseOwner::portrait_1,
        WidePavilionCloseOwner::portrait_2,
        WidePavilionCloseOwner::portrait_3,
        WidePavilionCloseOwner::portrait_4,
    }};

// Exact three arguments passed to fcn.00407bc1 / fcn.00409dff plus the
// ordinary-sound action performed by their caller after the dispatcher has
// unconditionally restored the pavilion loop.
struct WidePavilionHostDispatchPlan {
    std::int32_t primary = 0;
    std::int32_t mode = 0;
    std::int32_t forwarded_value = 0;
    bool dispatcher_stops_ambient = true;
    bool dispatcher_restarts_ambient = true;
    WidePavilionOuterAmbientAction outer_ambient_action =
        WidePavilionOuterAmbientAction::none;
    bool outer_updates_before_ambient_action = false;

    bool operator==(const WidePavilionHostDispatchPlan&) const = default;
};

[[nodiscard]] std::optional<WidePavilionHostDispatchPlan>
PlanWidePavilionHostDispatch(
    JudgingCategory category,
    WidePavilionHostDispatchEvent event,
    std::int32_t rating = 0
);

struct WidePavilionRatingTransition {
    std::int32_t numeric_rating_after = 0;
    bool replace_rating_bitmap = false;
    // A hidden surface is used only by transitions that intentionally expose
    // the authored pavilion background instead of a numeric gauge.
    bool hide_rating_surface = false;
    bool rebuild_pavilion = false;
    bool restart_ambient = false;
    // ACCEPT's mode-4 host overlay does not own or destroy the current
    // 320x240 performance child. It may finish before or after that overlay.
    bool preserve_current_performance = false;
};

// +0x64 is consumed by every painter invocation. It is armed at construction
// and after a successful contestant activation, but only draws red when the
// current one-based slot is still locally pending. Later full repaints redraw
// all five white frames and deliberately do not restore the red frame.
[[nodiscard]] bool ConsumeWidePavilionActiveFrame(
    bool& selection_frame_dirty,
    const PavilionVisitState& visit,
    bool contestant_active,
    std::size_t selected_slot
);

[[nodiscard]] PavilionVisitState BeginPavilionVisit(
    const GameRoundState& round,
    JudgingCategory category
);

[[nodiscard]] std::optional<std::size_t> NextPavilionPendingSlot(
    const PavilionVisitState& visit,
    std::size_t first
);

[[nodiscard]] LooksAutomaticActivationPlan PlanLooksAutomaticActivation(
    const PavilionVisitState& visit
);

// Talent/Brains retain one rating wrapper for the whole dialog lifetime.
// Selecting a contestant resets it to 0 only after the surcharge succeeds.
// NOCASH2 and every completed result preserve both the numeric value and the
// current gauge wrapper; result handlers merely return to the in-place chooser.
[[nodiscard]] WidePavilionRatingTransition PlanWidePavilionRatingTransition(
    WidePavilionRatingEvent event,
    std::int32_t current_rating
);

void CompletePavilionPendingSlot(
    PavilionVisitState& visit,
    std::size_t slot
);

void CommitPavilionVisit(
    GameRoundState& round,
    JudgingCategory category,
    const PavilionVisitState& visit
);

[[nodiscard]] std::wstring PavilionCreditText(
    float credits,
    JudgingCategory category
);

// The recovered FrameRect branches require both a selected slot and that
// slot's current pavilion-pending flag. A completed final Looks contestant
// stays selected for the direct-exit path but no longer keeps a red border.
[[nodiscard]] bool PavilionPortraitHasActiveFrame(
    const PavilionVisitState& visit,
    bool contestant_active,
    std::size_t selected_slot,
    std::size_t portrait_slot
);

// Mouse-move handlers 0x004074e8, 0x00409722, and 0x00413850 all use the
// same five stored portrait RECTs for the green hover-name overlay.
[[nodiscard]] std::optional<std::size_t> PavilionPortraitAt(
    int x,
    int y
);

// Talent and Brains store only one shared press flag. Their release handlers
// test all three control rectangles, so a drag may release on a different
// control from the one that armed the flag. Looks uses a per-control code and
// deliberately does not use this helper.
[[nodiscard]] WidePavilionReleaseAction WidePavilionReleaseAt(
    bool press_armed,
    int x,
    int y
);

}  // namespace metaverse
