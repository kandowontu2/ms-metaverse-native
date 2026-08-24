#include "category_weight.hpp"

namespace metaverse {

std::optional<std::size_t> CategoryWeightControlAtPointer(int x, int y) {
    for (std::size_t control = 0;
         control < kCategoryWeightControlRects.size(); ++control) {
        if (kCategoryWeightControlRects[control].Contains(x, y)) {
            return control;
        }
    }
    return std::nullopt;
}

bool CategoryWeightOkAtPointer(int x, int y) {
    return kCategoryWeightOkRect.Contains(x, y);
}

std::optional<CategoryWeightAdjustment> AdjustCategoryWeightControl(
    std::array<std::int32_t, 3>& values,
    std::size_t control
) {
    if (control >= kCategoryWeightControlCount) {
        return std::nullopt;
    }

    const std::size_t category = control / 2;
    std::int32_t& value = values[category];
    if ((control % 2) == 0) {
        if (value != 0) {
            --value;
        }
    } else if (static_cast<std::uint32_t>(value) < 10u) {
        // 0x0041033f/71/a6 use unsigned JAE. Values are 0..10 in every
        // reachable dialog state; retain the comparison for completeness.
        ++value;
    }
    return CategoryWeightAdjustment{category, value};
}

std::optional<CategoryWeightGaugePlan> PlanCategoryWeightGauge(
    std::size_t category,
    std::int32_t value
) {
    if (category >= kCategoryWeightGaugePositions.size()) {
        return std::nullopt;
    }

    const auto& position = kCategoryWeightGaugePositions[category];
    const auto& size = kCategoryWeightGaugeSizes[category];
    CategoryWeightGaugePlan plan{
        category,
        value,
        {position.x, position.y,
         position.x + size.width, position.y + size.height},
        value != 0,
        {},
    };
    if (plan.replace_existing_bitmap) {
        plan.bitmap_filename = L"th" + std::to_wstring(category + 1) + L"_" +
            std::to_wstring(value) + L".bmp";
    }
    return plan;
}

CategoryWeightPaintPlan ConsumeCategoryWeightPaint(
    CategoryWeightPainterState& state,
    std::int32_t selected_control
) {
    CategoryWeightPaintPlan plan;
    plan.draw_base = state.base_dirty;
    state.base_dirty = false;
    plan.draw_ok = state.ok_dirty;
    state.ok_dirty = false;

    if (state.control_dirty) {
        state.control_dirty = false;
        if (selected_control >= 0 &&
            static_cast<std::size_t>(selected_control) <
                kCategoryWeightControlCount) {
            plan.control = static_cast<std::size_t>(selected_control);
        }
    }

    if (state.current_gauge < state.gauge_dirty.size() &&
        state.gauge_dirty[state.current_gauge]) {
        state.gauge_dirty[state.current_gauge] = false;
        plan.gauge = state.current_gauge;
    }
    return plan;
}

}  // namespace metaverse
