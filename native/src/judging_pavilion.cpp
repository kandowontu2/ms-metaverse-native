#include "judging_pavilion.hpp"

#include <cwchar>

namespace metaverse {

PavilionVisitState BeginPavilionVisit(
    const GameRoundState& round,
    JudgingCategory category
) {
    PavilionVisitState visit;
    const std::size_t category_index = static_cast<std::size_t>(category);
    for (std::size_t slot = 0; slot < visit.pending.size(); ++slot) {
        visit.pending[slot] = !round.contestants[slot].disqualified &&
            round.contestants[slot].pending[category_index];
    }
    return visit;
}

std::optional<std::size_t> NextPavilionPendingSlot(
    const PavilionVisitState& visit,
    std::size_t first
) {
    for (std::size_t slot = first; slot < visit.pending.size(); ++slot) {
        if (visit.pending[slot]) {
            return slot;
        }
    }
    return std::nullopt;
}

LooksAutomaticActivationPlan PlanLooksAutomaticActivation(
    const PavilionVisitState& visit
) {
    return LooksAutomaticActivationPlan{
        NextPavilionPendingSlot(visit, 0),
        0,
        true,
    };
}

std::optional<WidePavilionHostDispatchPlan> PlanWidePavilionHostDispatch(
    JudgingCategory category,
    WidePavilionHostDispatchEvent event,
    std::int32_t rating
) {
    if (category != JudgingCategory::talent &&
        category != JudgingCategory::brains) {
        return std::nullopt;
    }

    using Outer = WidePavilionOuterAmbientAction;
    switch (event) {
        case WidePavilionHostDispatchEvent::entry:
            return WidePavilionHostDispatchPlan{
                category == JudgingCategory::talent ? 2 : 3,
                6, 1, true, true, Outer::none, false
            };
        case WidePavilionHostDispatchEvent::score_reaction:
            return WidePavilionHostDispatchPlan{
                0, 4, rating, true, true, Outer::none, false
            };
        case WidePavilionHostDispatchEvent::gong:
            return WidePavilionHostDispatchPlan{
                6, 6, 1, true, true, Outer::restart, false
            };
        case WidePavilionHostDispatchEvent::penalty:
            return WidePavilionHostDispatchPlan{
                7, 6, 1, true, true, Outer::restart, true
            };
        case WidePavilionHostDispatchEvent::completion:
            return WidePavilionHostDispatchPlan{
                8, 6, 1, true, true, Outer::stop, false
            };
    }
    return std::nullopt;
}

WidePavilionRatingTransition PlanWidePavilionRatingTransition(
    WidePavilionRatingEvent event,
    std::int32_t current_rating
) {
    switch (event) {
        case WidePavilionRatingEvent::performance_charge_succeeded:
            return WidePavilionRatingTransition{
                0, true, false, false, false, false
            };
        case WidePavilionRatingEvent::performance_charge_failed:
            return WidePavilionRatingTransition{
                current_rating, false, false, false, false, false
            };
        case WidePavilionRatingEvent::result_completed:
            return WidePavilionRatingTransition{
                current_rating, false, false, false, true, true
            };
    }
    return WidePavilionRatingTransition{
        current_rating, false, false, false, false, false
    };
}

bool ConsumeWidePavilionActiveFrame(
    bool& selection_frame_dirty,
    const PavilionVisitState& visit,
    bool contestant_active,
    std::size_t selected_slot
) {
    const bool draw = selection_frame_dirty &&
        PavilionPortraitHasActiveFrame(
            visit, contestant_active, selected_slot, selected_slot
        );
    selection_frame_dirty = false;
    return draw;
}

void CompletePavilionPendingSlot(
    PavilionVisitState& visit,
    std::size_t slot
) {
    if (slot < visit.pending.size()) {
        visit.pending[slot] = false;
    }
}

void CommitPavilionVisit(
    GameRoundState& round,
    JudgingCategory category,
    const PavilionVisitState& visit
) {
    const std::size_t category_index = static_cast<std::size_t>(category);
    for (std::size_t slot = 0; slot < visit.pending.size(); ++slot) {
        round.contestants[slot].pending[category_index] = visit.pending[slot];
    }
}

std::wstring PavilionCreditText(
    float credits,
    JudgingCategory category
) {
    wchar_t text[40] = {};
    const wchar_t* format = category == JudgingCategory::looks
        ? L"$%.2f"
        : L"$%.2f   ";
    std::swprintf(
        text, sizeof(text) / sizeof(text[0]), format,
        static_cast<double>(credits)
    );
    return text;
}

bool PavilionPortraitHasActiveFrame(
    const PavilionVisitState& visit,
    bool contestant_active,
    std::size_t selected_slot,
    std::size_t portrait_slot
) {
    if (!contestant_active || selected_slot != portrait_slot ||
        portrait_slot >= visit.pending.size()) {
        return false;
    }
    return visit.pending[portrait_slot];
}

std::optional<std::size_t> PavilionPortraitAt(int x, int y) {
    if (y < kPavilionPortraitY ||
        y >= kPavilionPortraitY + kPavilionPortraitSize) {
        return std::nullopt;
    }
    for (std::size_t slot = 0; slot < kContestantsPerRound; ++slot) {
        const int left = kPavilionPortraitX +
            static_cast<int>(slot) * kPavilionPortraitPitch;
        if (x >= left && x < left + kPavilionPortraitSize) {
            return slot;
        }
    }
    return std::nullopt;
}

WidePavilionReleaseAction WidePavilionReleaseAt(
    bool press_armed,
    int x,
    int y
) {
    if (!press_armed) {
        return WidePavilionReleaseAction::none;
    }
    const auto inside = [x, y](int left, int top, int right, int bottom) {
        return x >= left && x < right && y >= top && y < bottom;
    };
    if (inside(25, 368, 161, 460)) {
        return WidePavilionReleaseAction::accept;
    }
    if (inside(547, 164, 619, 194)) {
        return WidePavilionReleaseAction::gong;
    }
    if (inside(534, 218, 635, 276)) {
        return WidePavilionReleaseAction::penalty;
    }
    return WidePavilionReleaseAction::none;
}

}  // namespace metaverse
