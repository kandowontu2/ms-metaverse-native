#include "slot_machine.hpp"

#include <bit>

namespace metaverse {

float SlotWagerForCredits(float credits) {
    float wager = credits * kSlotWagerFraction;
    if (std::bit_cast<std::int32_t>(wager) >
        std::bit_cast<std::int32_t>(kSlotMaximumWager)) {
        wager = kSlotMaximumWager;
    }
    return wager;
}

std::int32_t SlotResultAmountX(const SlotOutcome& outcome) {
    return outcome.won ? kSlotWinAmountX : kSlotLossAmountX;
}

SlotOutcome SpinSlots(float& credits, LegacyRandom& random) {
    SlotOutcome outcome;
    if (credits == 0.0f) {
        return outcome;
    }

    outcome.spun = true;
    outcome.wager = SlotWagerForCredits(credits);

    const bool forced_match = random.Scale(3) > 0;
    if (forced_match) {
        const std::int32_t symbol = static_cast<std::int32_t>(random.Scale(5));
        outcome.symbols.fill(symbol);
    } else {
        for (std::int32_t& symbol : outcome.symbols) {
            symbol = static_cast<std::int32_t>(random.Scale(5));
        }
    }

    outcome.won = outcome.symbols[0] == outcome.symbols[1] &&
                  outcome.symbols[1] == outcome.symbols[2];
    if (outcome.won) {
        outcome.jackpot = random.Scale(50) == 37;
        outcome.credit_change = outcome.jackpot
            ? outcome.wager * kSlotJackpotMultiplier
            : outcome.wager;
    } else {
        outcome.credit_change = -outcome.wager;
    }
    credits += outcome.credit_change;
    return outcome;
}

}  // namespace metaverse
