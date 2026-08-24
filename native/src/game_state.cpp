#include "game_state.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace metaverse {
namespace {

constexpr std::size_t ToIndex(JudgingCategory category) {
    return static_cast<std::size_t>(category);
}

constexpr std::array<std::int32_t, 10> kCryoAffordabilityOrder{
    5, 6, 8, 0, 4, 9, 1, 3, 7, 2,
};

constexpr std::int32_t SignedFloatBits(float value) {
    return std::bit_cast<std::int32_t>(value);
}

float LegacyMultiplyAddToFloat(
    float value,
    float multiplier,
    float addend
) {
    // MM.EXE performs these chains in the x87 register stack and stores only
    // the final sum.  A binary64 intermediate is exact for the product and
    // sum of these binary32 operands, avoiding the extra binary32 rounding a
    // modern `float product; addend += product;` sequence would introduce.
    return static_cast<float>(
        static_cast<double>(value) * static_cast<double>(multiplier) +
        static_cast<double>(addend)
    );
}

}  // namespace

bool ReplayCreditThresholdReached(float credits) {
    // 0x0040244e performs a signed DWORD comparison against the bit pattern
    // for 400.0f.  For ordinary finite non-negative balances this is the
    // familiar credits >= 400 test, while retaining the original edge cases.
    return SignedFloatBits(credits) >= SignedFloatBits(kReplayCreditThreshold);
}

float ClampJudgeCadetBalance(float credits) {
    // 0x004023b4 uses an unsigned compare with 0x80000000 after removing the
    // judge cadet's stake.  It preserves positive values and negative zero,
    // and clears every other sign-bit-set representation to positive zero.
    if (std::bit_cast<std::uint32_t>(credits) > 0x80000000U) {
        return 0.0f;
    }
    return credits;
}

void ApplyNativeCreditCheat(GameRoundState& round) {
    round.credits = kNativeCreditCheatBalance;
}

void ArmSimmSelectionPool(
    SimmSelectionTable& table,
    std::span<const std::int32_t> contestant_ids
) {
    // State one at 0x00402164 always rearms D0 and each sponsored contestant
    // without clearing any other document entry.
    table[0] = 1;
    for (const std::int32_t contestant_id : contestant_ids) {
        if (contestant_id > 0 &&
            contestant_id < static_cast<std::int32_t>(table.size())) {
            table[static_cast<std::size_t>(contestant_id)] = 1;
        }
    }
}

std::size_t RearmAndCountEligibleSimms(SimmSelectionTable& table) {
    const auto count_eligible = [&table]() {
        return static_cast<std::size_t>(std::count(
            table.begin(), table.end(), static_cast<std::uint8_t>(1)
        ));
    };
    std::size_t eligible = count_eligible();
    if (eligible != 0) {
        return eligible;
    }
    for (std::uint8_t& value : table) {
        if (value != 0) {
            value = 1;
            ++eligible;
        }
    }
    return eligible;
}

std::int32_t ConsumeEligibleSimm(
    SimmSelectionTable& table,
    std::size_t eligible_ordinal
) {
    for (std::size_t index = 0; index < table.size(); ++index) {
        if (table[index] != 1) {
            continue;
        }
        if (eligible_ordinal == 0) {
            ++table[index];
            return static_cast<std::int32_t>(index);
        }
        --eligible_ordinal;
    }
    return -1;
}

CryoRotationSegment NextCryoRotationSegment(
    std::int32_t contestant_id,
    std::int64_t next_frame
) {
    if (contestant_id < 1 ||
        contestant_id > static_cast<std::int32_t>(kContestantNames.size())) {
        return {};
    }

    const std::int64_t candidate_first =
        static_cast<std::int64_t>(contestant_id - 1) *
        kCryoCandidateFrameCount;
    const std::int64_t candidate_last =
        candidate_first + kCryoCandidateFrameCount - 1;
    if (next_frame >= candidate_first && next_frame <= candidate_last) {
        return {
            next_frame,
            next_frame + kCryoRotationSegmentFrameCount - 1,
            next_frame + kCryoRotationSegmentFrameCount,
        };
    }

    const std::int64_t wrapped_first = candidate_first +
        (contestant_id > 6 ? kCryoRotationSegmentFrameCount : 0);
    const std::int64_t wrapped_last =
        candidate_first + kCryoRotationSegmentFrameCount - 1;
    return {wrapped_first, wrapped_last, wrapped_last + 1};
}

std::int32_t CategoryWeights::Sum() const {
    return looks + brains + talent;
}

std::int32_t CategoryWeights::For(JudgingCategory category) const {
    switch (category) {
        case JudgingCategory::looks:
            return looks;
        case JudgingCategory::brains:
            return brains;
        case JudgingCategory::talent:
            return talent;
    }
    return 0;
}

bool NormalizeCategoryWeights(
    const std::array<std::int32_t, kJudgingCategoryCount>& controls,
    CategoryWeights& weights,
    std::string& error
) {
    error.clear();
    if (std::any_of(
            controls.begin(), controls.end(),
            [](std::int32_t value) { return value < 0 || value > 10; }
        )) {
        error = "category controls must be between 0 and 10";
        return false;
    }

    const std::int32_t total = controls[0] + controls[1] + controls[2];
    if (total == 0) {
        error = "weight the categories please";
        return false;
    }

    // 0x00410512-0x00410540 and 0x0041055e-0x0041057f divide in the x87
    // register stack first, multiply by the binary-exact 100.0f constant,
    // then call the truncate-toward-zero helper. Keep that operation order;
    // MinGW's long double supplies the same extended intermediate precision.
    const auto percentage = [total](std::int32_t control) {
        const long double ratio = static_cast<long double>(control) /
                                  static_cast<long double>(total);
        return static_cast<std::int32_t>(ratio * 100.0L);
    };
    weights.looks = percentage(controls[0]);
    weights.brains = percentage(controls[1]);
    weights.talent = 100 - weights.looks - weights.brains;
    return true;
}

void GongContestant(
    ContestantRoundState& contestant,
    JudgingCategory category
) {
    const std::size_t index = ToIndex(category);
    contestant.ratings[index] = 0;
    contestant.pending[index] = false;
}

float ContestantSponsorCost(std::int32_t contestant_id) {
    if (contestant_id < 1 ||
        contestant_id > static_cast<std::int32_t>(kContestantSponsorCosts.size())) {
        return 0.0f;
    }
    return kContestantSponsorCosts[static_cast<std::size_t>(contestant_id - 1)];
}

bool SponsorContestant(
    GameRoundState& round,
    std::span<const std::int32_t> already_selected,
    std::int32_t contestant_id,
    std::string& error
) {
    error.clear();
    const float cost = ContestantSponsorCost(contestant_id);
    if (cost <= 0.0f) {
        error = "contestant id is outside the original one-to-ten range";
        return false;
    }
    if (already_selected.size() >= kContestantsPerRound) {
        error = "five contestants are already selected";
        return false;
    }
    if (std::find(already_selected.begin(), already_selected.end(), contestant_id) !=
        already_selected.end()) {
        error = "this contestant is already selected";
        return false;
    }

    const float remaining = round.credits - cost;
    const std::size_t future_count =
        kContestantsPerRound - already_selected.size() - 1;
    float minimum_future_cost = 0.0f;
    std::size_t counted = 0;
    if (future_count != 0) {
        for (const std::int32_t zero_based_id : kCryoAffordabilityOrder) {
            // This deliberately preserves the 1996 executable's comparison of
            // the zero-based affordability list with the one-based selected IDs.
            if (std::find(
                    already_selected.begin(), already_selected.end(), zero_based_id
                ) != already_selected.end()) {
                continue;
            }
            minimum_future_cost +=
                kContestantSponsorCosts[static_cast<std::size_t>(zero_based_id)];
            if (++counted == future_count) {
                break;
            }
        }
    }
    if (remaining < minimum_future_cost) {
        error = "not enough cash to sponsor this contestant and complete the roster";
        return false;
    }

    round.credits = remaining;
    round.accumulated_reward += cost;
    return true;
}

bool ChargePerformanceSurcharge(
    GameRoundState& round,
    std::int32_t contestant_id,
    float& charge,
    std::string& error
) {
    error.clear();
    charge = ContestantSponsorCost(contestant_id) * kPerformanceSurchargeFraction;
    if (charge <= 0.0f) {
        error = "contestant id is outside the original one-to-ten range";
        return false;
    }

    // Talent/Brains keep cost * -0.1 and the cash addition in one x87 chain,
    // compare that un-stored value with the exact -0.001f constant, and only
    // then round it to the document's float field.
    const double remaining_extended =
        static_cast<double>(ContestantSponsorCost(contestant_id)) *
            static_cast<double>(-kPerformanceSurchargeFraction) +
        static_cast<double>(round.credits);
    if (!(remaining_extended >= static_cast<double>(-0.001f))) {
        error = "not enough cash for this performance's ten-percent surcharge";
        return false;
    }
    float remaining = static_cast<float>(remaining_extended);
    // 0x0040728b/0x004094c0 use an unsigned stored-DWORD comparison with
    // 0x80000000.  This clears ordinary negative values but preserves -0.0f.
    if (std::bit_cast<std::uint32_t>(remaining) > 0x80000000U) {
        remaining = 0.0f;
    }
    round.credits = remaining;
    return true;
}

PenaltyResult ApplyPenaltyBox(
    GameRoundState& round,
    std::size_t contestant_slot,
    JudgingCategory category
) {
    PenaltyResult result;
    if (contestant_slot >= round.contestants.size()) {
        return result;
    }

    ContestantRoundState& contestant = round.contestants[contestant_slot];
    const float stake = ContestantSponsorCost(contestant.contestant_id);
    result.correct = contestant_slot == round.judge_cadet_slot;
    if (result.correct) {
        result.credit_change = stake;
        round.credits = LegacyMultiplyAddToFloat(stake, 1.0f, round.credits);
        contestant.ratings[ToIndex(category)] = 0;
        contestant.pending[ToIndex(category)] = false;
        contestant.disqualified = true;
    } else {
        result.credit_change = -stake * kWrongPenaltyFraction;
        // The three pavilion handlers keep stake * -0.1 in x87 extended
        // precision until it has been added to the balance.
        round.credits = LegacyMultiplyAddToFloat(
            stake, -kWrongPenaltyFraction, round.credits
        );
    }
    return result;
}

bool IsJudgeCadetPresentation(
    const GameRoundState& round,
    std::size_t contestant_slot,
    JudgingCategory category
) {
    return contestant_slot == round.judge_cadet_slot &&
           category == round.judge_cadet_category;
}

std::int32_t SimmMotionPacingFactor(const GameRoundState& round) {
    std::int32_t total_cost = 0;
    for (const ContestantRoundState& contestant : round.contestants) {
        total_cost += static_cast<std::int32_t>(
            ContestantSponsorCost(contestant.contestant_id)
        );
    }
    const std::int32_t average_cost =
        total_cost / static_cast<std::int32_t>(round.contestants.size());
    if (average_cost <= 0) {
        return 5;
    }

    const std::int32_t credits = static_cast<std::int32_t>(round.credits);
    const std::int32_t quotient = credits / average_cost;
    return std::clamp(5 - quotient, 0, 5);
}

float SimmCreditAward(
    std::int32_t simm_number,
    std::int32_t pacing_factor
) {
    const float base_value = simm_number == 0
        ? 250.0f
        : ContestantSponsorCost(simm_number);
    if (base_value <= 0.0f) {
        return 0.0f;
    }
    const std::int32_t factor = std::clamp(pacing_factor, 0, 5);
    return static_cast<float>(15 - factor * 2) * base_value * 0.01f;
}

bool SimmResultEarnsAward(std::int32_t modal_result) {
    return modal_result > 10;
}

bool IntroModalResultExits(std::int32_t modal_result) {
    return modal_result != 0;
}

std::int32_t WeightedScore(
    const ContestantRoundState& contestant,
    const CategoryWeights& weights
) {
    return
        static_cast<std::int32_t>(contestant.ratings[ToIndex(JudgingCategory::looks)]) *
            weights.looks +
        static_cast<std::int32_t>(contestant.ratings[ToIndex(JudgingCategory::brains)]) *
            weights.brains +
        static_cast<std::int32_t>(contestant.ratings[ToIndex(JudgingCategory::talent)]) *
            weights.talent;
}

TallyResult TallyRound(GameRoundState& round) {
    TallyResult result;
    bool all_complete = true;

    // The original starts both the maximum and winner at zero and only
    // replaces them on a strictly greater score. Ties therefore favor the
    // earliest contestant and an all-zero tally returns contestant zero.
    std::int32_t maximum_score = 0;
    for (std::size_t index = 0; index < round.contestants.size(); ++index) {
        ContestantRoundState& contestant = round.contestants[index];
        if (contestant.disqualified) {
            contestant.pending.fill(false);
            continue;
        }

        const std::int32_t score = WeightedScore(contestant, round.weights);
        if (score > maximum_score) {
            maximum_score = score;
            result.winner_index = index;
        }
        if (std::any_of(contestant.pending.begin(), contestant.pending.end(),
                        [](bool pending) { return pending; })) {
            all_complete = false;
        }
    }

    result.winning_score = maximum_score;
    result.all_judging_complete = all_complete;

    for (const ContestantRoundState& contestant : round.contestants) {
        result.completed_category_count += static_cast<std::size_t>(std::count(
            contestant.pending.begin(), contestant.pending.end(), false
        ));
    }

    if (all_complete) {
        // 0x004032b7 also compares the stored float as a signed DWORD.  The
        // result is numerically identical for every normal reachable reward,
        // but the literal operation matters to an instruction-faithful port.
        result.credit_award =
            SignedFloatBits(round.accumulated_reward) >
                SignedFloatBits(kCompleteRoundMinimumAward)
            ? round.accumulated_reward
            : kCompleteRoundMinimumAward;
        round.credits += result.credit_award;
    } else {
        result.credit_award = static_cast<float>(result.completed_category_count) *
                              kCompletedCategoryAward;
        // 0x004032f6-0x00403344 does not multiply the number of completed
        // pairs and add once. It loads/stores the balance around each separate
        // 27.0f addition, in contestant order and field order. That per-step
        // single-precision rounding is observable at large reachable balances.
        for (const ContestantRoundState& contestant : round.contestants) {
            for (bool pending : contestant.pending) {
                if (!pending) {
                    round.credits += kCompletedCategoryAward;
                }
            }
        }
    }
    return result;
}

}  // namespace metaverse
