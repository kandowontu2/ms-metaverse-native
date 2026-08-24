#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include "legacy_random.hpp"

namespace metaverse {

constexpr float kSlotWagerFraction = 0.1f;
constexpr float kSlotMaximumWager = 50.0f;
constexpr float kSlotJackpotMultiplier = 20.0f;

// fcn.0040aa61 plays the slot node's middle/self transition, pumps the
// Windows message queue until timeGetTime() reaches start + 0x1194, then
// stops the navigation movie. The result painter in fcn.0040b4e7 uses the
// fixed 640x480 coordinates below without stretching the symbol bitmaps.
constexpr auto kSlotSpinDuration = std::chrono::milliseconds(4500);
constexpr std::array<std::int32_t, 3> kSlotSymbolX{{190, 290, 388}};
constexpr std::int32_t kSlotSymbolY = 320;
constexpr std::int32_t kSlotCreditsX = 40;
constexpr std::int32_t kSlotCreditsY = 20;
constexpr std::int32_t kSlotWinAmountX = 450;
constexpr std::int32_t kSlotLossAmountX = 500;
constexpr std::int32_t kSlotNoCreditX = 350;

struct SlotOutcome {
    std::array<std::int32_t, 3> symbols{};
    float wager = 0.0f;
    float credit_change = 0.0f;
    bool spun = false;
    bool won = false;
    bool jackpot = false;
};

// fcn.0040b4e7 stores credits*0.1f, then performs a signed integer comparison
// of those float bits against 50.0f before optionally replacing the wager.
// This differs from std::min for a positive NaN loaded from a legacy profile.
float SlotWagerForCredits(float credits);

// The result painter chooses x=450 for the matched-symbol branch and x=500
// for the unmatched branch. It does not infer the side from the amount sign;
// negative legacy balances can make either branch's amount sign counterintuitive.
std::int32_t SlotResultAmountX(const SlotOutcome& outcome);

// Reproduces the slot branch embedded in MM.EXE's navigation handler. Random
// draws use the original 0..32767 domain before integer truncation.
SlotOutcome SpinSlots(float& credits, LegacyRandom& random);

}  // namespace metaverse
