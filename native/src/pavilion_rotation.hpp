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

constexpr std::size_t kLegacyContestantCount = 10;
constexpr std::array<std::size_t, kLegacyContestantCount> kBrainVariantCounts = {
    3, 4, 4, 3, 4, 3, 3, 4, 3, 3,
};
constexpr std::size_t kTalentVariantCount = 5;
inline constexpr wchar_t kLegacyPavilionRotationFileName[] = L"nrfPav.dat";

struct PavilionRotationState {
    // fcn.00416b33 stores fgetc()-'0' as a raw byte and only tests zero versus
    // nonzero during selection. Keep the byte so the permissive gameplay path
    // can preserve malformed legacy input; the strict parser still accepts
    // only canonical 0/1 files.
    std::array<std::array<std::uint8_t, 4>, kLegacyContestantCount> brain_used{};
    std::array<std::array<std::uint8_t, kTalentVariantCount>, kLegacyContestantCount>
        talent_used{};
};

enum class PavilionRotationCategory {
    brains,
    talent,
};

bool ParseLegacyPavilionRotation(
    std::istream& input,
    PavilionRotationState& state,
    std::string& error
);
bool WriteLegacyPavilionRotation(
    std::ostream& output,
    const PavilionRotationState& state,
    std::string& error
);
bool LoadLegacyPavilionRotation(
    const std::filesystem::path& path,
    PavilionRotationState& state,
    std::string& error
);

// MM.EXE opens each selected line with stdio "r+" only when round variants are
// generated. Open/seek/read failure is ignored, and every obtained byte is
// stored as byte-'0' without validation. This best-effort loader reproduces
// that gameplay behavior; LoadLegacyPavilionRotation remains the strict tool
// and diagnostic API.
void LoadLegacyPavilionRotationForSelection(
    const std::filesystem::path& path,
    PavilionRotationState& state
);
void LoadLegacyPavilionRotationLineForSelection(
    const std::filesystem::path& path,
    PavilionRotationState& state,
    PavilionRotationCategory category,
    std::int32_t contestant_id
);
bool SaveLegacyPavilionRotation(
    const std::filesystem::path& path,
    const PavilionRotationState& state,
    std::string& error
);
bool SaveLegacyPavilionRotationLine(
    const std::filesystem::path& path,
    const PavilionRotationState& state,
    PavilionRotationCategory category,
    std::int32_t contestant_id,
    std::string& error
);

std::int32_t DrawBrainVariant(
    PavilionRotationState& state,
    std::int32_t contestant_id,
    LegacyRandom& random,
    std::string& error
);
std::int32_t DrawTalentVariant(
    PavilionRotationState& state,
    std::int32_t contestant_id,
    LegacyRandom& random,
    std::string& error
);

}  // namespace metaverse
