#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace metaverse {

constexpr std::size_t kContestantsPerRound = 5;
constexpr std::size_t kJudgingCategoryCount = 3;
constexpr float kCompleteRoundMinimumAward = 500.0f;
constexpr float kCompletedCategoryAward = 27.0f;
// fcn.00401b03 compares the post-tally balance with float 400.0 before
// offering another round; a lower balance takes the END2.WAV closing branch.
constexpr float kReplayCreditThreshold = 400.0f;
constexpr float kPerformanceSurchargeFraction = 0.1f;
constexpr float kWrongPenaltyFraction = 0.1f;
constexpr float kNativeCreditCheatBalance = 999999.0f;

// The 1996 executable compares these float fields as their stored 32-bit
// integers rather than issuing x87 comparisons.  Keep those exact branches in
// named helpers so the finale controller does not silently normalize them to
// modern floating-point comparison semantics.
[[nodiscard]] bool ReplayCreditThresholdReached(float credits);
[[nodiscard]] float ClampJudgeCadetBalance(float credits);

// MM.EXE fcn.0040391f stores the ID-ordered names at game state
// +0x108..+0x1bc and their matching costs at +0xd4..+0xf8.
inline constexpr std::array<std::wstring_view, 10> kContestantNames{
    L"Queen", L"Nancy", L"Suzi", L"Conchita", L"Jane",
    L"Dee", L"Sammy", L"Rhonda", L"Jackie", L"Domina",
};

inline constexpr std::array<float, 10> kContestantSponsorCosts{
    100.0f, 250.0f, 1000.0f, 250.0f, 150.0f,
    50.0f, 50.0f, 500.0f, 50.0f, 150.0f,
};

// fcn.00401b03 installs these five document slots before MMINTRO.AVI for every
// profile. Ordinary Cryo replaces them; DEMO (password BUYVV) keeps them,
// skips Cryo/Weight/ORDER, and enters NAVIGATE.DAT state 1 after the intro.
inline constexpr std::array<std::int32_t, kContestantsPerRound>
    kDemoContestantIds{3, 4, 5, 8, 10};

// The document constructor first writes the reverse ORDER default, but the
// top orchestrator at 0x00401b6e overwrites all ten words with their ascending
// indices before MMINTRO.AVI. DEMO skips ORDER and therefore judges with this
// identity mapping for the entire round.
inline constexpr std::array<std::int32_t, 10>
    kOrchestratorInitialCommentMapping{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

// The navigation Simm chooser at fcn.0040c3ed uses the round document's
// 256-byte table at +0x304. Value one means eligible now, value two means used
// in the current pass, and zero means that script has never been enabled.
constexpr std::size_t kSimmSelectionTableSize = 256;
using SimmSelectionTable =
    std::array<std::uint8_t, kSimmSelectionTableSize>;

void ArmSimmSelectionPool(
    SimmSelectionTable& table,
    std::span<const std::int32_t> contestant_ids
);

// If the active pass is exhausted, fcn.0040c3ed changes every nonzero entry
// back to one. The table itself survives navigation-popup destruction and the
// replay reset, so old contestant IDs can remain in the recycled pool.
std::size_t RearmAndCountEligibleSimms(SimmSelectionTable& table);

// Consumes the zero-based ordinal among entries whose value is exactly one.
// Returns -1 when the caller supplies an out-of-range ordinal.
std::int32_t ConsumeEligibleSimm(
    SimmSelectionTable& table,
    std::size_t eligible_ordinal
);

constexpr std::int64_t kCryoCandidateFrameCount = 40;
constexpr std::int64_t kCryoRotationSegmentFrameCount = 10;

struct CryoRotationSegment {
    std::int64_t first_frame = -1;
    std::int64_t last_frame = -1;
    std::int64_t next_frame = -1;

    [[nodiscard]] bool Valid() const {
        return first_frame >= 0 && last_frame >= 0 && next_frame >= 0;
    }

    [[nodiscard]] bool HasForwardPlayback() const {
        return Valid() && first_frame <= last_frame;
    }
};

// fcn.00404962 uses a persistent marker to play one ten-frame quarter-turn
// per ROTATE click. Its wrap branch contains a visible original quirk: IDs
// 7-10 seek to the second quarter but issue PLAYTO for the preceding frame.
CryoRotationSegment NextCryoRotationSegment(
    std::int32_t contestant_id,
    std::int64_t next_frame
);

enum class JudgingCategory : std::size_t {
    looks = 0,
    brains = 1,
    talent = 2,
};

struct CategoryWeights {
    std::int32_t looks = 0;
    std::int32_t brains = 0;
    std::int32_t talent = 0;

    [[nodiscard]] std::int32_t Sum() const;
    [[nodiscard]] std::int32_t For(JudgingCategory category) const;
};

bool NormalizeCategoryWeights(
    const std::array<std::int32_t, kJudgingCategoryCount>& controls,
    CategoryWeights& weights,
    std::string& error
);

struct ContestantRoundState {
    std::int32_t contestant_id = 0;
    std::array<std::uint8_t, kJudgingCategoryCount> ratings{};

    // The original stores one integer per pavilion. One means that judging is
    // still pending; zero means it was completed. A successful Penalty Box
    // accusation disqualifies the contestant; a gong only completes the
    // current Brains or Talent category with a zero rating.
    std::array<bool, kJudgingCategoryCount> pending{true, true, true};
    bool disqualified = false;
};

void GongContestant(
    ContestantRoundState& contestant,
    JudgingCategory category
);

struct GameRoundState {
    std::array<ContestantRoundState, kContestantsPerRound> contestants{};
    CategoryWeights weights{};
    // fcn.0040391f initializes the ten document words at +0x9c..+0xc0 to
    // 9,8,...,0. ORDER overwrites them during an ordinary round, while the
    // reverse constructor state remains observable before that dialog.
    std::array<std::int32_t, 10> comment_for_rating{
        9, 8, 7, 6, 5, 4, 3, 2, 1, 0
    };
    // The document constructor zeros +0xd0. Login/profile activation installs
    // the real starting balance before gameplay begins.
    float credits = 0.0f;
    float accumulated_reward = 0.0f;
    // Zero-based position among the five selected contestants. The original
    // chooses this with rand() % 5 before pavilion play. Its separate c8
    // field chooses the one pavilion in which that impostor clue is visible.
    std::size_t judge_cadet_slot = 0;
    JudgingCategory judge_cadet_category = JudgingCategory::looks;
};

// Port-only Ctrl+Alt+F1 assigns this balance rather than adding to the current
// value, making repeated use idempotent and the displayed/persisted result
// exactly $999999.00.
void ApplyNativeCreditCheat(GameRoundState& round);

[[nodiscard]] bool IsJudgeCadetPresentation(
    const GameRoundState& round,
    std::size_t contestant_slot,
    JudgingCategory category
);

[[nodiscard]] float ContestantSponsorCost(std::int32_t contestant_id);

// Navigation fcn.0040b26c derives the generic-media movement pacing from the
// integer-truncated cash balance divided by the average selected sponsorship
// cost, then converts that quotient into a clamped zero-to-five factor.
[[nodiscard]] std::int32_t SimmMotionPacingFactor(
    const GameRoundState& round
);

// A positive MMS terminal only reports that the Simm was caught. The caller
// computes the actual award as 5..15 percent of the selected contestant's
// sponsorship value (D0 uses the original fixed $250 base).
[[nodiscard]] float SimmCreditAward(
    std::int32_t simm_number,
    std::int32_t pacing_factor
);

// The generic modal returns its MMS terminal result to the navigation caller.
// Cancel/deactivation results stay below the caller's greater-than-ten gate.
[[nodiscard]] bool SimmResultEarnsAward(std::int32_t modal_result);

// The intro caller exits only when the one-shot dialog returns nonzero.
// Successful MCI notify and the active-playback OnOK override both return 0;
// OnCancel is a no-op in this class.
[[nodiscard]] bool IntroModalResultExits(std::int32_t modal_result);

// Reproduces the Cryo selection debit and its look-ahead affordability check.
bool SponsorContestant(
    GameRoundState& round,
    std::span<const std::int32_t> already_selected,
    std::int32_t contestant_id,
    std::string& error
);

bool ChargePerformanceSurcharge(
    GameRoundState& round,
    std::int32_t contestant_id,
    float& charge,
    std::string& error
);

struct PenaltyResult {
    bool correct = false;
    float credit_change = 0.0f;
};

PenaltyResult ApplyPenaltyBox(
    GameRoundState& round,
    std::size_t contestant_slot,
    JudgingCategory category
);

struct TallyResult {
    std::size_t winner_index = 0;
    std::int32_t winning_score = 0;
    bool all_judging_complete = false;
    std::size_t completed_category_count = 0;
    float credit_award = 0.0f;
};

[[nodiscard]] std::int32_t WeightedScore(
    const ContestantRoundState& contestant,
    const CategoryWeights& weights
);

// Reproduces MM.EXE fcn.004031d0. It intentionally mutates a disqualified
// contestant's pending flags, just as the original tally path does.
TallyResult TallyRound(GameRoundState& round);

}  // namespace metaverse
