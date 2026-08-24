#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <ostream>
#include <string>

#include "legacy_random.hpp"

namespace metaverse {

constexpr std::size_t kHostReactionGroupCount = 3;
constexpr std::size_t kHostReactionVariantCapacity = 15;
constexpr std::array<std::size_t, kHostReactionGroupCount>
    kHostReactionVariantCounts{15, 12, 11};

// The original HNRD.DAT is 45 little-endian int32 flags, interleaved by
// variant then group. Selection tests each complete word only for equality
// with zero and rewrites untouched words verbatim, so retain the raw values
// rather than normalizing malformed legacy state to bool.
struct HostReactionState {
    std::array<
        std::array<std::uint32_t, kHostReactionVariantCapacity>,
        kHostReactionGroupCount
    > used{};
};

// The mode-4 caller forwards the accepted Judge-O-Matic value. It is not the
// contestant id: scores <=3 use group 3, 4..7 use group 1, and >=8 use group 2.
std::int32_t HostReactionGroupForScore(std::int32_t score);
std::wstring HostReactionStem(std::int32_t group, std::int32_t variant);

bool ParseLegacyHostReactionState(
    std::istream& input,
    HostReactionState& state,
    std::string& error
);
bool WriteLegacyHostReactionState(
    std::ostream& output,
    const HostReactionState& state,
    std::string& error
);
bool LoadLegacyHostReactionState(
    const std::filesystem::path& path,
    HostReactionState& state,
    std::string& error
);
// fcn.0040e8ae zeroes its 180-byte stack table, attempts one raw read, and
// ignores both the open result and the byte count before selecting an X clip.
// Use this permissive path at the recovered selection boundary; keep the strict
// parser above for validation and tooling. The corresponding Save call writes
// all 45 raw words, exactly as the original create/truncate rewrite does.
void LoadLegacyHostReactionStateForSelection(
    const std::filesystem::path& path,
    HostReactionState& state
);
bool SaveLegacyHostReactionState(
    const std::filesystem::path& path,
    const HostReactionState& state,
    std::string& error
);

std::int32_t DrawHostReactionVariant(
    HostReactionState& state,
    std::int32_t group,
    LegacyRandom& random,
    std::string& error
);

}  // namespace metaverse
