#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace metaverse {

constexpr std::size_t kCommentCount = 10;

// Logical-canvas geometry constructed by ORDER at 0x0040f3ab-0x0040f405.
// The phrase artwork is 314x32, but its selectable rectangle is deliberately
// one pixel narrower and two pixels shorter.
constexpr std::int32_t kCommentPhraseLeft = 308;
constexpr std::int32_t kCommentPhraseTop = 40;
constexpr std::int32_t kCommentPhrasePitch = 40;
constexpr std::int32_t kCommentPhraseHitWidth = 313;
constexpr std::int32_t kCommentPhraseHitHeight = 30;
constexpr std::int32_t kCommentAcceptLeft = 21;
constexpr std::int32_t kCommentAcceptTop = 381;
constexpr std::int32_t kCommentAcceptWidth = 196;
constexpr std::int32_t kCommentAcceptHeight = 91;
constexpr std::int32_t kCommentThermometerLeft = 88;
constexpr std::int32_t kCommentThermometerTop = 30;
constexpr std::int32_t kCommentHandLeft = 215;
constexpr std::int32_t kCommentHandTop = 30;
constexpr std::int32_t kCommentRankLeft = 317;
constexpr std::int32_t kCommentRankTop = 45;
constexpr std::int32_t kCommentPhraseBitmapWidth = 314;
constexpr std::int32_t kCommentPhraseBitmapHeight = 32;
constexpr std::int32_t kCommentHandWidth = 85;
constexpr std::int32_t kCommentHandHeight = 75;
constexpr std::int32_t kCommentThermometerWidth = 68;
constexpr std::int32_t kCommentThermometerHeight = 332;
constexpr std::int32_t kCommentRankDirtyWidth = 17;
constexpr std::int32_t kCommentRankDirtyHeight = 30;

// 0x0040fa08 constructs these wrappers in this order. ORDER.BMP itself is
// loaded by the first paint after OnInit returns, not by the constructor.
constexpr std::array<std::wstring_view, 3> kCommentStaticBitmapLoadOrder{{
    L"ACCEPT.BMP", L"TH10.BMP", L"HAND.BMP",
}};

struct CommentRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

[[nodiscard]] constexpr CommentRect CommentPhraseBitmapRect(
    std::size_t comment
) {
    const std::int32_t top = kCommentPhraseTop +
        static_cast<std::int32_t>(comment) * kCommentPhrasePitch;
    return {
        kCommentPhraseLeft,
        top,
        kCommentPhraseLeft + kCommentPhraseBitmapWidth,
        top + kCommentPhraseBitmapHeight,
    };
}

[[nodiscard]] constexpr CommentRect CommentHandRect(std::int32_t comment) {
    const std::int32_t top = kCommentHandTop + comment * kCommentPhrasePitch;
    return {
        kCommentHandLeft,
        top,
        kCommentHandLeft + kCommentHandWidth,
        top + kCommentHandHeight,
    };
}

[[nodiscard]] constexpr CommentRect CommentThermometerRect() {
    return {
        kCommentThermometerLeft,
        kCommentThermometerTop,
        kCommentThermometerLeft + kCommentThermometerWidth,
        kCommentThermometerTop + kCommentThermometerHeight,
    };
}

[[nodiscard]] constexpr CommentRect CommentAcceptRect() {
    return {
        kCommentAcceptLeft,
        kCommentAcceptTop,
        kCommentAcceptLeft + kCommentAcceptWidth,
        kCommentAcceptTop + kCommentAcceptHeight,
    };
}

[[nodiscard]] constexpr CommentRect CommentRankRect(std::int32_t comment) {
    const std::int32_t top = kCommentRankTop + comment * kCommentPhrasePitch;
    return {
        kCommentRankLeft,
        top,
        kCommentRankLeft + kCommentRankDirtyWidth,
        top + kCommentRankDirtyHeight,
    };
}

[[nodiscard]] constexpr CommentRect UnionCommentRects(
    CommentRect first,
    CommentRect second
) {
    return {
        first.left < second.left ? first.left : second.left,
        first.top < second.top ? first.top : second.top,
        first.right > second.right ? first.right : second.right,
        first.bottom > second.bottom ? first.bottom : second.bottom,
    };
}

struct CommentPainterState {
    bool base_dirty = false;
    bool accept_dirty = false;
    bool comment_dirty = false;
    bool hand_dirty = false;
    bool thermometer_dirty = false;
    bool rank_dirty = false;
    std::size_t current_comment = 0;
    std::int32_t current_rank_comment = 0;
};

struct CommentPaintPlan {
    bool draw_base = false;
    bool draw_accept = false;
    std::optional<std::size_t> comment;
    bool draw_hand = false;
    bool draw_thermometer = false;
    std::optional<std::int32_t> rank_comment;
};

// The ORDER painter at 0x0040f75a consumes the six dirty layers in this
// strict order. COMMENT/0-9 share one legacy wrapper/current index, and a rank
// repaint refers only to the most recently assigned row.
[[nodiscard]] CommentPaintPlan ConsumeCommentPaint(
    CommentPainterState& state
);

[[nodiscard]] std::optional<std::int32_t> CommentPhraseAtPointer(
    std::int32_t x,
    std::int32_t y
);

[[nodiscard]] bool CommentAcceptAtPointer(
    std::int32_t x,
    std::int32_t y
);

// ORDER's message map at 0x00431f78 uniquely binds WM_RBUTTONDBLCLK to
// 0x0040ff2d. It ends the dialog with result zero immediately, unless the
// generic C51 child currently owns modal input and the ORDER owner cannot
// receive the message.
[[nodiscard]] inline constexpr bool CommentRightDoubleClickEndsDialog(
    bool owner_input_blocked
) {
    return !owner_input_blocked;
}

// State for the original pre-game comment-ordering board. The player assigns
// each phrase to one rating, beginning at ten and counting down to one.
struct CommentOrderState {
    CommentOrderState();

    std::int32_t selected_comment = 0;
    std::int32_t next_rank = 10;
    std::array<std::int32_t, kCommentCount> assigned_rank_by_comment{};

    struct PendingAssignment {
        std::int32_t comment = 0;
        std::int32_t rank = 0;
        std::size_t mapping_index = 0;
        std::int32_t mapping_value = 0;
    };

    [[nodiscard]] bool Complete() const;
    void MoveSelection(std::int32_t delta);

    // Performs the state mutation before ORDER paints the rank, plays TTn,
    // and replaces THn. The document table write is deliberately returned to
    // the caller because 0x0040f73d commits it only after those side effects.
    [[nodiscard]] std::optional<PendingAssignment> BeginSelectedAssignment(
        std::string& error
    );

    static void CommitAssignment(
        const PendingAssignment& assignment,
        std::array<std::int32_t, kCommentCount>& comment_for_rating
    );

    // Reproduces MM.EXE 0x40f653. comment_for_rating is the original ten-word
    // game-state table at +0x9c: its index is rank-1 and its value is the
    // reversed comment asset number (9-selected_comment).
    bool AssignSelected(
        std::array<std::int32_t, kCommentCount>& comment_for_rating,
        std::string& error
    );
};

// Reproduces the sound-name construction in the Talent, Brains, and Looks
// Judge-O-Matic handlers (MM.EXE 0x40715a, 0x40937e, and 0x4135dd).
// A zero rating uses the generic TT0 response. Ratings 1..10 are translated
// through the player's ORDER table and paired with the contestant's 1..5
// judging slot, for example "wav/comment/b35.wav".
[[nodiscard]] bool BuildJudgeCommentPath(
    char category_prefix,
    std::int32_t rating,
    std::int32_t contestant_slot,
    const std::array<std::int32_t, kCommentCount>& comment_for_rating,
    std::string& relative_path,
    std::string& error
);

}  // namespace metaverse
