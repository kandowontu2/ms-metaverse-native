#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace metaverse {

inline constexpr std::int32_t kLooksPresentationLeft = 225;
inline constexpr std::int32_t kLooksPresentationTop = 64;
inline constexpr std::int32_t kLooksPresentationRight = 436;
inline constexpr std::int32_t kLooksPresentationBottom = 377;
inline constexpr std::int32_t kLooksRotateWidth = 211;
inline constexpr std::int32_t kLooksRotateHeight = 312;
inline constexpr std::int32_t kLooksRotateWindowWidth = 212;
inline constexpr std::int32_t kLooksPenaltyResponseWidth = 200;
inline constexpr std::int32_t kLooksPenaltyResponseHeight = 320;
inline constexpr std::int32_t kLooksInitialMagnificationLevel = 1;
inline constexpr std::int32_t kLooksPortraitHoverDirtyLeft = 287;
inline constexpr std::int32_t kLooksPortraitHoverDirtyTop = 388;
inline constexpr std::int32_t kLooksPortraitHoverDirtyRight = 640;
inline constexpr std::int32_t kLooksPortraitHoverDirtyBottom = 410;

struct LooksInputRect {
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

inline constexpr LooksInputRect kLooksMeterRect{62, 13, 118, 278};
inline constexpr LooksInputRect kLooksAcceptRect{17, 381, 168, 478};
inline constexpr LooksInputRect kLooksPenaltyRect{174, 411, 277, 471};
inline constexpr LooksInputRect kLooksRotateRect{506, 237, 618, 378};

enum class LooksControlAction {
    accept,
    penalty,
    rotate,
};

[[nodiscard]] inline constexpr std::optional<LooksControlAction>
LooksControlAtPointer(std::int32_t x, std::int32_t y) {
    if (kLooksAcceptRect.Contains(x, y)) {
        return LooksControlAction::accept;
    }
    if (kLooksPenaltyRect.Contains(x, y)) {
        return LooksControlAction::penalty;
    }
    if (kLooksRotateRect.Contains(x, y)) {
        return LooksControlAction::rotate;
    }
    return std::nullopt;
}

[[nodiscard]] inline constexpr std::optional<std::int32_t>
LooksMeterRatingAtPointer(std::int32_t x, std::int32_t y) {
    if (!kLooksMeterRect.Contains(x, y)) {
        return std::nullopt;
    }
    const std::int32_t raw = (13 - y) / 24 + 10;
    return raw < 0 ? 0 : raw > 10 ? 10 : raw;
}

struct LooksControlReleasePlan {
    bool dispatch = false;
    bool retain_logical_action = false;
    bool retain_pressed_surface = false;
    bool force_update_before_dispatch = false;
    bool restore_after_dispatch_phase = false;
};

// The painter at 0x004129d3 is not a conventional retained redraw. These
// seven fields are one-shot flags consumed in this exact draw order; credits,
// all five portraits, and an optional hover name are drawn on every invocation.
enum class LooksPaintOperation {
    backing,
    accept,
    penalty,
    rotate_control,
    magnifier,
    credits,
    rating,
    portraits,
    selection_frames,
    hover_name,
};

struct LooksPainterState {
    bool backing_dirty = false;          // +0x44
    bool rating_dirty = false;           // +0x48
    bool accept_dirty = false;           // +0x4c
    bool rotate_control_dirty = false;   // +0x50
    bool magnifier_dirty = false;        // +0x54
    bool penalty_dirty = false;          // +0x58
    bool selection_frames_dirty = false; // +0x70
    std::optional<std::size_t> hovered_slot;
};

struct LooksPaintPlan {
    std::array<LooksPaintOperation, 10> operations{};
    std::size_t count = 0;

    constexpr void Append(LooksPaintOperation operation) {
        operations[count++] = operation;
    }
};

[[nodiscard]] inline constexpr LooksPaintPlan ConsumeLooksPaintPlan(
    LooksPainterState& state
) {
    LooksPaintPlan plan;
    if (state.backing_dirty) {
        plan.Append(LooksPaintOperation::backing);
        state.backing_dirty = false;
    }
    if (state.accept_dirty) {
        plan.Append(LooksPaintOperation::accept);
        state.accept_dirty = false;
    }
    if (state.penalty_dirty) {
        plan.Append(LooksPaintOperation::penalty);
        state.penalty_dirty = false;
    }
    if (state.rotate_control_dirty) {
        plan.Append(LooksPaintOperation::rotate_control);
        state.rotate_control_dirty = false;
    }
    if (state.magnifier_dirty) {
        plan.Append(LooksPaintOperation::magnifier);
        state.magnifier_dirty = false;
    }
    plan.Append(LooksPaintOperation::credits);
    if (state.rating_dirty) {
        plan.Append(LooksPaintOperation::rating);
        state.rating_dirty = false;
    }
    plan.Append(LooksPaintOperation::portraits);
    if (state.selection_frames_dirty) {
        plan.Append(LooksPaintOperation::selection_frames);
        state.selection_frames_dirty = false;
    }
    if (state.hovered_slot) {
        plan.Append(LooksPaintOperation::hover_name);
    }
    return plan;
}

[[nodiscard]] inline constexpr LooksControlReleasePlan
PlanLooksControlRelease(
    LooksControlAction action,
    bool release_inside
) {
    switch (action) {
        case LooksControlAction::accept:
            return LooksControlReleasePlan{
                release_inside, false, false, false, true
            };
        case LooksControlAction::penalty:
            return LooksControlReleasePlan{
                release_inside, true, true, false, false
            };
        case LooksControlAction::rotate:
            return LooksControlReleasePlan{
                release_inside, false, true, true, false
            };
    }
    return {};
}

// The parent mouse-move route sets cursor 174 over the direct-exit strip and
// 166 over most of the dialog.  The lower-right corner deliberately makes no
// SetCursor call, preserving whichever cursor was already installed.  The
// retained ROTATE child has its own cursor route and is modelled separately.
enum class LooksParentCursorAction {
    preserve_current,
    set_hand_166,
    set_exit_174,
};

[[nodiscard]] inline constexpr LooksParentCursorAction
LooksParentCursorActionAt(std::int32_t x, std::int32_t y) {
    if (x < 60 && y < 382) {
        return LooksParentCursorAction::set_exit_174;
    }
    if (x > 580 && y >= 382) {
        return LooksParentCursorAction::preserve_current;
    }
    return LooksParentCursorAction::set_hand_166;
}

struct LooksRotationRange {
    std::int64_t first_frame = 0;
    std::int64_t last_frame = 0;
    bool front_after = true;
};

enum class LooksPenaltyPresentationPhase {
    response,
    restored,
};

// The original Looks dialog owns MAG and ROTATE independently. Hiding one
// never releases the other; the stateful painter/window stack only chooses
// which retained child is exposed at a given transition.
enum class LooksPresentationAction {
    show_rotate,
    show_magnifier,
    show_background,
};

struct LooksPresentationPlan {
    bool rotate_visible = false;
    bool magnifier_visible = false;
};

enum class LooksRatingSurfaceAction {
    construction,
    select_value,
    manual_contestant_switch,
    automatic_advance,
    correct_penalty,
};

struct LooksRatingSurfacePlan {
    std::optional<std::int32_t> replacement_rating;
    bool visible = false;
};

// 0x004132d6 forwards five numeric values to the shared media runner.  Keep
// the recovered call contract explicit: C41 uses channel 4/mode 6/take 1,
// while ACCEPT uses mode 4 and forwards the current rating as the X-group
// selector.  Both paths stop ambient sound, dirty LOOK.BMP, and reposition a
// retained ROTATE child after the modal runner returns.
struct LooksMediaDispatchPlan {
    std::int32_t generic_arg_1 = 0;
    std::int32_t primary_index = 0;
    std::int32_t mode = 0;
    std::int32_t generic_arg_4 = 0;
    std::int32_t selector = 0;
    bool stop_ambient_sound = true;
    bool mark_backing_dirty = true;
    bool reposition_rotate_child = true;
};

enum class LooksBitmapOwner {
    base,
    accept,
    penalty,
    rotate_control,
    magnifier,
    rating,
    portrait_0,
    portrait_1,
    portrait_2,
    portrait_3,
    portrait_4,
};

inline constexpr std::array<LooksBitmapOwner, 11>
    kLooksBitmapConstructionOrder{{
        LooksBitmapOwner::base,
        LooksBitmapOwner::accept,
        LooksBitmapOwner::penalty,
        LooksBitmapOwner::rotate_control,
        LooksBitmapOwner::magnifier,
        LooksBitmapOwner::rating,
        LooksBitmapOwner::portrait_0,
        LooksBitmapOwner::portrait_1,
        LooksBitmapOwner::portrait_2,
        LooksBitmapOwner::portrait_3,
        LooksBitmapOwner::portrait_4,
    }};

inline constexpr std::array<LooksBitmapOwner, 11>
    kLooksBitmapTeardownOrder{{
        LooksBitmapOwner::accept,
        LooksBitmapOwner::penalty,
        LooksBitmapOwner::rotate_control,
        LooksBitmapOwner::magnifier,
        LooksBitmapOwner::base,
        LooksBitmapOwner::rating,
        LooksBitmapOwner::portrait_0,
        LooksBitmapOwner::portrait_1,
        LooksBitmapOwner::portrait_2,
        LooksBitmapOwner::portrait_3,
        LooksBitmapOwner::portrait_4,
    }};

[[nodiscard]] inline constexpr bool LooksRotateVisibleForPenaltyPhase(
    LooksPenaltyPresentationPhase phase,
    bool retained_surface_available
) {
    return phase == LooksPenaltyPresentationPhase::restored &&
           retained_surface_available;
}

[[nodiscard]] LooksPresentationPlan PlanLooksPresentation(
    LooksPresentationAction action,
    bool rotate_child_constructed,
    bool magnifier_bitmap_loaded
);

[[nodiscard]] LooksRatingSurfacePlan PlanLooksRatingSurface(
    LooksRatingSurfaceAction action,
    std::int32_t selected_rating,
    bool currently_visible
);

[[nodiscard]] LooksMediaDispatchPlan PlanLooksC41MediaDispatch();
[[nodiscard]] LooksMediaDispatchPlan PlanLooksAcceptMediaDispatch(
    std::int32_t rating
);

// ROTATE.AVI contains forty frames per contestant.  The first twenty turn the
// front view to the back; the second twenty return it to the front.
std::int64_t LooksInitialFrame(std::int32_t contestant_id);
LooksRotationRange LooksRotationFrames(
    std::int32_t contestant_id,
    bool front_before
);

// Exact local hit regions used by the original 211-by-312 rotate child.  The
// front view has head, torso, and legs bands; the back view maps the whole
// contestant to magnifier region four.
std::optional<std::int32_t> LooksMagnifierRegionAt(
    bool front_view,
    std::int32_t local_x,
    std::int32_t local_y
);

std::int32_t LooksJudgeCadetSpecialRegion(std::int32_t contestant_id);

// Returns only the filename below BMP/LOOK/MAG.
std::wstring LooksMagnifierFilename(
    std::int32_t contestant_id,
    std::int32_t region,
    std::int32_t level,
    bool current_contestant_is_looks_judge_cadet
);

}  // namespace metaverse
