#include "comment_order.hpp"

#include <algorithm>
#include <cctype>

namespace metaverse {

std::optional<std::int32_t> CommentPhraseAtPointer(
    std::int32_t x,
    std::int32_t y
) {
    if (x < kCommentPhraseLeft ||
        x >= kCommentPhraseLeft + kCommentPhraseHitWidth) {
        return std::nullopt;
    }
    for (std::int32_t comment = 0;
         comment < static_cast<std::int32_t>(kCommentCount); ++comment) {
        const std::int32_t top =
            kCommentPhraseTop + comment * kCommentPhrasePitch;
        if (y >= top && y < top + kCommentPhraseHitHeight) {
            return comment;
        }
    }
    return std::nullopt;
}

bool CommentAcceptAtPointer(std::int32_t x, std::int32_t y) {
    return x >= kCommentAcceptLeft &&
           x < kCommentAcceptLeft + kCommentAcceptWidth &&
           y >= kCommentAcceptTop &&
           y < kCommentAcceptTop + kCommentAcceptHeight;
}

CommentPaintPlan ConsumeCommentPaint(CommentPainterState& state) {
    CommentPaintPlan plan;
    plan.draw_base = state.base_dirty;
    state.base_dirty = false;
    plan.draw_accept = state.accept_dirty;
    state.accept_dirty = false;
    if (state.comment_dirty && state.current_comment < kCommentCount) {
        plan.comment = state.current_comment;
    }
    state.comment_dirty = false;
    plan.draw_hand = state.hand_dirty;
    state.hand_dirty = false;
    plan.draw_thermometer = state.thermometer_dirty;
    state.thermometer_dirty = false;
    if (state.rank_dirty && state.current_rank_comment >= 0 &&
        state.current_rank_comment < static_cast<std::int32_t>(kCommentCount)) {
        plan.rank_comment = state.current_rank_comment;
    }
    state.rank_dirty = false;
    return plan;
}

CommentOrderState::CommentOrderState() {
    assigned_rank_by_comment.fill(-1);
}

bool CommentOrderState::Complete() const {
    return next_rank == 0;
}

void CommentOrderState::MoveSelection(std::int32_t delta) {
    constexpr std::int32_t count = static_cast<std::int32_t>(kCommentCount);
    selected_comment = (selected_comment + delta + count) % count;
}

std::optional<CommentOrderState::PendingAssignment>
CommentOrderState::BeginSelectedAssignment(std::string& error) {
    error.clear();
    if (Complete()) {
        error = "all comments have already been assigned";
        return std::nullopt;
    }
    if (selected_comment < 0 ||
        selected_comment >= static_cast<std::int32_t>(kCommentCount)) {
        error = "selected comment is outside the original ten-comment range";
        return std::nullopt;
    }

    const std::size_t comment_index =
        static_cast<std::size_t>(selected_comment);
    if (assigned_rank_by_comment[comment_index] != -1) {
        error = "this comment has already been selected; choose another";
        return std::nullopt;
    }

    PendingAssignment assignment;
    assignment.comment = selected_comment;
    assignment.rank = next_rank;
    assigned_rank_by_comment[comment_index] = next_rank;
    --next_rank;
    assignment.mapping_index = static_cast<std::size_t>(next_rank);
    assignment.mapping_value =
        static_cast<std::int32_t>(kCommentCount - 1) - selected_comment;
    return assignment;
}

void CommentOrderState::CommitAssignment(
    const PendingAssignment& assignment,
    std::array<std::int32_t, kCommentCount>& comment_for_rating
) {
    if (assignment.mapping_index < comment_for_rating.size()) {
        comment_for_rating[assignment.mapping_index] = assignment.mapping_value;
    }
}

bool CommentOrderState::AssignSelected(
    std::array<std::int32_t, kCommentCount>& comment_for_rating,
    std::string& error
) {
    const auto assignment = BeginSelectedAssignment(error);
    if (!assignment) {
        return false;
    }
    CommitAssignment(*assignment, comment_for_rating);
    return true;
}

bool BuildJudgeCommentPath(
    char category_prefix,
    std::int32_t rating,
    std::int32_t contestant_slot,
    const std::array<std::int32_t, kCommentCount>& comment_for_rating,
    std::string& relative_path,
    std::string& error
) {
    relative_path.clear();
    error.clear();

    const char prefix = static_cast<char>(
        std::tolower(static_cast<unsigned char>(category_prefix))
    );
    if (prefix != 'b' && prefix != 'l' && prefix != 't') {
        error = "judge comment category must be Brains, Looks, or Talent";
        return false;
    }
    if (rating < 0 || rating > static_cast<std::int32_t>(kCommentCount)) {
        error = "judge rating must be between 0 and 10";
        return false;
    }
    if (contestant_slot < 1 || contestant_slot > 5) {
        error = "judge contestant slot must be between 1 and 5";
        return false;
    }

    if (rating == 0) {
        relative_path = "wav/tt0.wav";
        return true;
    }

    const std::int32_t comment_code =
        comment_for_rating[static_cast<std::size_t>(rating - 1)];
    if (comment_code < 0 ||
        comment_code >= static_cast<std::int32_t>(kCommentCount)) {
        error = "ORDER table contains an invalid host-comment code";
        return false;
    }

    relative_path = "wav/comment/";
    relative_path.push_back(prefix);
    relative_path += std::to_string(comment_code);
    relative_path += std::to_string(contestant_slot);
    relative_path += ".wav";
    return true;
}

}  // namespace metaverse
