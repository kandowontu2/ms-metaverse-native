#include "looks_pavilion.hpp"

#include <array>

namespace metaverse {
namespace {

constexpr std::array<std::int32_t, 10> kJudgeCadetSpecialRegions = {
    4, 2, 3, 2, 1, 2, 4, 1, 1, 2
};

bool ValidContestant(std::int32_t contestant_id) {
    return contestant_id >= 1 && contestant_id <= 10;
}

}  // namespace

LooksPresentationPlan PlanLooksPresentation(
    LooksPresentationAction action,
    bool rotate_child_constructed,
    bool magnifier_bitmap_loaded
) {
    switch (action) {
        case LooksPresentationAction::show_rotate:
            return LooksPresentationPlan{rotate_child_constructed, false};
        case LooksPresentationAction::show_magnifier:
            return LooksPresentationPlan{false, magnifier_bitmap_loaded};
        case LooksPresentationAction::show_background:
            return LooksPresentationPlan{false, false};
    }
    return {};
}

LooksRatingSurfacePlan PlanLooksRatingSurface(
    LooksRatingSurfaceAction action,
    std::int32_t selected_rating,
    bool currently_visible
) {
    switch (action) {
        case LooksRatingSurfaceAction::construction:
            // 0x0041312d preloads LOOK/1.BMP but leaves +0x48 clear.
            return LooksRatingSurfacePlan{1, false};
        case LooksRatingSurfaceAction::select_value:
            if (selected_rating > 0) {
                return LooksRatingSurfacePlan{selected_rating, true};
            }
            // A zero meter selection restores the LOOK backing without
            // deleting the last rating wrapper.
            return LooksRatingSurfacePlan{std::nullopt, false};
        case LooksRatingSurfaceAction::manual_contestant_switch:
        case LooksRatingSurfaceAction::correct_penalty:
            return LooksRatingSurfacePlan{std::nullopt, false};
        case LooksRatingSurfaceAction::automatic_advance:
            return LooksRatingSurfacePlan{std::nullopt, currently_visible};
    }
    return {};
}

LooksMediaDispatchPlan PlanLooksC41MediaDispatch() {
    return LooksMediaDispatchPlan{
        0, 4, 6, 0, 1, true, true, true
    };
}

LooksMediaDispatchPlan PlanLooksAcceptMediaDispatch(
    std::int32_t rating
) {
    return LooksMediaDispatchPlan{
        0, 0, 4, 0, rating, true, true, true
    };
}

std::int64_t LooksInitialFrame(std::int32_t contestant_id) {
    if (!ValidContestant(contestant_id)) {
        return 0;
    }
    return static_cast<std::int64_t>(contestant_id - 1) * 40;
}

LooksRotationRange LooksRotationFrames(
    std::int32_t contestant_id,
    bool front_before
) {
    const std::int64_t base = LooksInitialFrame(contestant_id);
    if (front_before) {
        return LooksRotationRange{base, base + 19, false};
    }
    return LooksRotationRange{base + 20, base + 39, true};
}

std::optional<std::int32_t> LooksMagnifierRegionAt(
    bool front_view,
    std::int32_t local_x,
    std::int32_t local_y
) {
    if (local_x < 0 || local_x >= 211 || local_y < 0 || local_y >= 312) {
        return std::nullopt;
    }
    if (!front_view) {
        return 4;
    }
    if (local_y < 103) {
        return 1;
    }
    if (local_y >= 104 && local_y < 208) {
        return 2;
    }
    if (local_y >= 209) {
        return 3;
    }
    return std::nullopt;
}

std::int32_t LooksJudgeCadetSpecialRegion(std::int32_t contestant_id) {
    if (!ValidContestant(contestant_id)) {
        return 0;
    }
    return kJudgeCadetSpecialRegions[
        static_cast<std::size_t>(contestant_id - 1)
    ];
}

std::wstring LooksMagnifierFilename(
    std::int32_t contestant_id,
    std::int32_t region,
    std::int32_t level,
    bool current_contestant_is_looks_judge_cadet
) {
    if (!ValidContestant(contestant_id) || region < 1 || region > 4 || level < 1) {
        return {};
    }
    if (current_contestant_is_looks_judge_cadet &&
        region == LooksJudgeCadetSpecialRegion(contestant_id)) {
        return L"IM" + std::to_wstring(contestant_id) +
               std::to_wstring(region) + L".BMP";
    }
    return std::to_wstring(contestant_id) + std::to_wstring(region) +
           std::to_wstring(level) + L".BMP";
}

}  // namespace metaverse
