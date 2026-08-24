#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace metaverse {

struct CategoryWeightRect {
    int left;
    int top;
    int right;
    int bottom;

    [[nodiscard]] constexpr bool Contains(int x, int y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }

    [[nodiscard]] constexpr int Width() const {
        return right - left;
    }

    [[nodiscard]] constexpr int Height() const {
        return bottom - top;
    }
};

struct CategoryWeightPoint {
    int x;
    int y;
};

struct CategoryWeightSize {
    int width;
    int height;
};

constexpr std::size_t kCategoryWeightControlCount = 6;

inline constexpr std::wstring_view kCategoryWeightOkBitmap = L"ok.bmp";

inline constexpr std::array<std::wstring_view, 3>
    kCategoryWeightInitialGaugeBitmaps{{
        L"th1_1.bmp", L"th2_1.bmp", L"th3_1.bmp",
    }};

inline constexpr std::array<std::wstring_view, kCategoryWeightControlCount>
    kCategoryWeightControlBitmaps{{
        L"down1.bmp", L"up1.bmp", L"down2.bmp",
        L"up2.bmp", L"down3.bmp", L"up3.bmp",
    }};

// fcn.004106e6 builds these from each native bitmap extent. The control order
// is DOWN/UP for Looks, Brains, and Talent, matching 0x00410285's switch.
inline constexpr std::array<CategoryWeightRect, kCategoryWeightControlCount>
    kCategoryWeightControlRects{{
        {39, 218, 79, 267},
        {130, 200, 169, 248},
        {258, 218, 297, 267},
        {348, 200, 387, 248},
        {483, 218, 524, 267},
        {574, 200, 614, 248},
    }};

inline constexpr CategoryWeightRect kCategoryWeightOkRect{
    269, 413, 374, 470
};

inline constexpr std::array<CategoryWeightPoint, 3>
    kCategoryWeightGaugePositions{{
        {80, 85}, {300, 85}, {525, 85},
    }};

inline constexpr std::array<CategoryWeightSize, 3>
    kCategoryWeightGaugeSizes{{
        {46, 262}, {44, 262}, {46, 262},
    }};

[[nodiscard]] std::optional<std::size_t> CategoryWeightControlAtPointer(
    int x,
    int y
);

[[nodiscard]] bool CategoryWeightOkAtPointer(int x, int y);

struct CategoryWeightAdjustment {
    std::size_t category = 0;
    std::int32_t value = 0;
};

// fcn.00410285 dispatches controls 0..5 as Looks down/up, Brains down/up,
// and Talent down/up. It proceeds to the gauge and TTn sound even when the
// selected value is already clamped, so the result always reports that value.
[[nodiscard]] std::optional<CategoryWeightAdjustment>
AdjustCategoryWeightControl(
    std::array<std::int32_t, 3>& values,
    std::size_t control
);

struct CategoryWeightGaugePlan {
    std::size_t category = 0;
    std::int32_t value = 0;
    CategoryWeightRect dirty_rect{};
    bool replace_existing_bitmap = false;
    std::wstring bitmap_filename;
};

// fcn.00410e3e retains the existing wrapper when a category reaches zero and
// exposes the base bitmap over its rectangle. Every nonzero value first
// destroys the prior wrapper and then attempts to load th{1,2,3}_N.bmp.
[[nodiscard]] std::optional<CategoryWeightGaugePlan>
PlanCategoryWeightGauge(std::size_t category, std::int32_t value);

struct CategoryWeightPainterState {
    bool base_dirty = false;
    bool ok_dirty = false;
    bool control_dirty = false;
    std::array<bool, 3> gauge_dirty{};
    std::size_t current_gauge = 0;
};

struct CategoryWeightPaintPlan {
    bool draw_base = false;
    bool draw_ok = false;
    std::optional<std::size_t> control;
    std::optional<std::size_t> gauge;
};

// Painter body 0x004105bb consumes base, OK, the selected arrow, and only the
// current category's gauge flag in that order. Other category flags survive.
[[nodiscard]] CategoryWeightPaintPlan ConsumeCategoryWeightPaint(
    CategoryWeightPainterState& state,
    std::int32_t selected_control
);

}  // namespace metaverse
