#include "pavilion_rotation.hpp"

#include <algorithm>
#include <fstream>
#include <string_view>
#include <vector>

namespace metaverse {
namespace {

template <std::size_t Capacity>
bool ParseFlags(
    std::string_view line,
    std::size_t active_count,
    std::array<std::uint8_t, Capacity>& flags,
    std::string& error
) {
    if (line.size() != Capacity) {
        error = "legacy pavilion line has the wrong length";
        return false;
    }
    for (std::size_t index = 0; index < Capacity; ++index) {
        if (line[index] != '0' && line[index] != '1') {
            error = "legacy pavilion line contains a value other than 0 or 1";
            return false;
        }
        flags[index] = static_cast<std::uint8_t>(line[index] - '0');
    }
    if (std::any_of(
            flags.begin() + static_cast<std::ptrdiff_t>(active_count),
            flags.end(),
            [](std::uint8_t used) { return used != 0; }
        )) {
        error = "legacy pavilion line marks an unavailable variant as used";
        return false;
    }
    return true;
}

template <std::size_t Capacity>
void WriteFlags(
    std::ostream& output,
    const std::array<std::uint8_t, Capacity>& flags
) {
    for (std::uint8_t used : flags) {
        output.put(static_cast<char>(used + static_cast<std::uint8_t>('0')));
    }
    output << "\r\n";
}

template <std::size_t Capacity>
bool ValidateCanonicalFlags(
    const std::array<std::uint8_t, Capacity>& flags,
    std::size_t active_count,
    std::string& error
) {
    if (std::any_of(
            flags.begin(),
            flags.begin() + static_cast<std::ptrdiff_t>(active_count),
            [](std::uint8_t used) { return used > 1; }
        )) {
        error = "cannot serialize a pavilion flag other than 0 or 1";
        return false;
    }
    if (std::any_of(
            flags.begin() + static_cast<std::ptrdiff_t>(active_count),
            flags.end(),
            [](std::uint8_t used) { return used != 0; }
        )) {
        error = "cannot serialize an unavailable brain variant";
        return false;
    }
    return true;
}

template <std::size_t Capacity>
void ReadSelectionFlags(
    std::string_view bytes,
    std::size_t line_index,
    std::size_t active_count,
    std::array<std::uint8_t, Capacity>& flags
) {
    constexpr std::uint8_t kLegacyEofFlag = 0xcf;
    std::size_t position = 0;
    for (std::size_t skipped = 0; skipped < line_index; ++skipped) {
        const std::size_t newline = bytes.find('\n', position);
        if (newline == std::string_view::npos) {
            position = bytes.size();
            break;
        }
        position = newline + 1;
    }
    const std::size_t count = std::min(active_count, Capacity);
    for (std::size_t index = 0; index < count; ++index) {
        if (position + index >= bytes.size()) {
            // fgetc returns EOF (-1); 0x00416b80 subtracts ASCII '0' from AL
            // and stores the wrapped low byte without checking the result.
            flags[index] = kLegacyEofFlag;
            continue;
        }
        flags[index] = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(bytes[position + index]) -
            static_cast<std::uint8_t>('0')
        );
    }
}

template <std::size_t Capacity>
std::int32_t DrawVariant(
    std::array<std::uint8_t, Capacity>& flags,
    std::size_t active_count,
    LegacyRandom& random
) {
    std::array<std::size_t, Capacity> available{};
    std::size_t available_count = 0;
    for (std::size_t index = 0; index < active_count; ++index) {
        if (flags[index] == 0) {
            available[available_count++] = index;
        }
    }
    if (available_count == 0) {
        // fcn.004169e7 returns -1 without consuming rand(); its caller adds
        // one and stores variant zero. A valid cycle never reaches this state,
        // but malformed/all-used input does.
        return 0;
    }

    // fcn.004169e7 selects its compacted zero list with rand() % count.
    const std::size_t chosen = available[random.Modulo(available_count)];
    flags[chosen] = 1;

    // The legacy generator resets when it consumes the final unused value,
    // then marks that value as used. This prevents an immediate repeat at the
    // boundary between shuffled cycles.
    if (available_count == 1) {
        std::fill(flags.begin(), flags.begin() + active_count, 0);
        flags[chosen] = 1;
    }
    return static_cast<std::int32_t>(chosen + 1);
}

bool ValidateContestant(std::int32_t contestant_id, std::string& error) {
    error.clear();
    if (contestant_id < 1 ||
        contestant_id > static_cast<std::int32_t>(kLegacyContestantCount)) {
        error = "contestant id must be between 1 and 10";
        return false;
    }
    return true;
}

}  // namespace

bool ParseLegacyPavilionRotation(
    std::istream& input,
    PavilionRotationState& state,
    std::string& error
) {
    state = {};
    error.clear();
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (!input.eof() && input.fail()) {
        error = "failed while reading legacy pavilion rotation data";
        return false;
    }
    if (lines.size() != 20) {
        error = "legacy pavilion rotation data must contain 20 lines";
        return false;
    }
    for (std::size_t contestant = 0; contestant < kLegacyContestantCount; ++contestant) {
        if (!ParseFlags(
                lines[contestant], kBrainVariantCounts[contestant],
                state.brain_used[contestant], error
            ) ||
            !ParseFlags(
                lines[contestant + kLegacyContestantCount], kTalentVariantCount,
                state.talent_used[contestant], error
            )) {
            error += " at contestant " + std::to_string(contestant + 1);
            state = {};
            return false;
        }
    }
    return true;
}

bool WriteLegacyPavilionRotation(
    std::ostream& output,
    const PavilionRotationState& state,
    std::string& error
) {
    error.clear();
    for (std::size_t contestant = 0; contestant < kLegacyContestantCount; ++contestant) {
        if (!ValidateCanonicalFlags(
                state.brain_used[contestant],
                kBrainVariantCounts[contestant], error
            )) {
            return false;
        }
        WriteFlags(output, state.brain_used[contestant]);
    }
    for (const auto& flags : state.talent_used) {
        if (!ValidateCanonicalFlags(flags, kTalentVariantCount, error)) {
            return false;
        }
        WriteFlags(output, flags);
    }
    if (!output) {
        error = "failed while writing legacy pavilion rotation data";
        return false;
    }
    return true;
}

bool LoadLegacyPavilionRotation(
    const std::filesystem::path& path,
    PavilionRotationState& state,
    std::string& error
) {
    if (!std::filesystem::exists(path)) {
        state = {};
        error.clear();
        return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open legacy pavilion rotation data: " + path.string();
        return false;
    }
    return ParseLegacyPavilionRotation(input, state, error);
}

void LoadLegacyPavilionRotationForSelection(
    const std::filesystem::path& path,
    PavilionRotationState& state
) {
    state = {};
    // The original uses fopen("r+"), so a readable but non-writable file is
    // also treated as an open failure and leaves the constructor zeros intact.
    std::fstream input(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!input) {
        // fopen(path, "r+") failure at 0x00416b53 simply returns with the
        // active bytes left at the zeros written by fcn.004169c7.
        return;
    }

    const std::string bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    for (std::size_t contestant = 0;
         contestant < kLegacyContestantCount;
         ++contestant) {
        ReadSelectionFlags(
            bytes,
            contestant,
            kBrainVariantCounts[contestant],
            state.brain_used[contestant]
        );
        ReadSelectionFlags(
            bytes,
            contestant + kLegacyContestantCount,
            kTalentVariantCount,
            state.talent_used[contestant]
        );
    }
}

void LoadLegacyPavilionRotationLineForSelection(
    const std::filesystem::path& path,
    PavilionRotationState& state,
    PavilionRotationCategory category,
    std::int32_t contestant_id
) {
    if (contestant_id < 1 ||
        contestant_id > static_cast<std::int32_t>(kLegacyContestantCount)) {
        return;
    }
    const std::size_t contestant =
        static_cast<std::size_t>(contestant_id - 1);
    const std::size_t line_index = category == PavilionRotationCategory::brains
        ? contestant
        : contestant + kLegacyContestantCount;

    if (category == PavilionRotationCategory::brains) {
        state.brain_used[contestant].fill(0);
    } else {
        state.talent_used[contestant].fill(0);
    }

    std::fstream input(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!input) {
        return;
    }
    const std::string bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    if (category == PavilionRotationCategory::brains) {
        ReadSelectionFlags(
            bytes,
            line_index,
            kBrainVariantCounts[contestant],
            state.brain_used[contestant]
        );
    } else {
        ReadSelectionFlags(
            bytes,
            line_index,
            kTalentVariantCount,
            state.talent_used[contestant]
        );
    }
}

bool SaveLegacyPavilionRotation(
    const std::filesystem::path& path,
    const PavilionRotationState& state,
    std::string& error
) {
    if (!path.parent_path().empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(path.parent_path(), directory_error);
        if (directory_error) {
            error = "cannot create pavilion data directory: " +
                    directory_error.message();
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot create legacy pavilion rotation data: " + path.string();
        return false;
    }
    return WriteLegacyPavilionRotation(output, state, error);
}

bool SaveLegacyPavilionRotationLine(
    const std::filesystem::path& path,
    const PavilionRotationState& state,
    PavilionRotationCategory category,
    std::int32_t contestant_id,
    std::string& error
) {
    if (!ValidateContestant(contestant_id, error)) {
        return false;
    }

    // The installed game always supplies the zero-filled 130-byte template.
    // Create that equivalent on a clean native install before switching to
    // the original r+ style, line-local updates.
    if (!std::filesystem::exists(path)) {
        return SaveLegacyPavilionRotation(path, state, error);
    }

    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        error = "cannot update legacy pavilion rotation data: " + path.string();
        return false;
    }

    const std::size_t contestant =
        static_cast<std::size_t>(contestant_id - 1);
    const std::size_t line_index = category == PavilionRotationCategory::brains
        ? contestant
        : contestant + kLegacyContestantCount;
    file.seekg(0, std::ios::beg);
    for (std::size_t skipped = 0; skipped < line_index; ++skipped) {
        char value = 0;
        do {
            if (!file.get(value)) {
                error = "failed while seeking legacy pavilion rotation line";
                return false;
            }
        } while (value != '\n');
    }
    const std::streampos line_position = file.tellg();
    if (line_position == std::streampos(-1)) {
        error = "failed while seeking legacy pavilion rotation line";
        return false;
    }
    file.clear();
    file.seekp(line_position);

    if (category == PavilionRotationCategory::brains) {
        if (std::any_of(
                state.brain_used[contestant].begin() +
                    static_cast<std::ptrdiff_t>(
                        kBrainVariantCounts[contestant]
                    ),
                state.brain_used[contestant].end(),
                [](std::uint8_t used) { return used != 0; }
            )) {
            error = "cannot serialize an unavailable brain variant";
            return false;
        }
        WriteFlags(file, state.brain_used[contestant]);
    } else {
        WriteFlags(file, state.talent_used[contestant]);
    }
    if (!file) {
        error = "failed while updating legacy pavilion rotation data";
        return false;
    }
    error.clear();
    return true;
}

std::int32_t DrawBrainVariant(
    PavilionRotationState& state,
    std::int32_t contestant_id,
    LegacyRandom& random,
    std::string& error
) {
    if (!ValidateContestant(contestant_id, error)) {
        return 0;
    }
    const std::size_t index = static_cast<std::size_t>(contestant_id - 1);
    return DrawVariant(
        state.brain_used[index], kBrainVariantCounts[index], random
    );
}

std::int32_t DrawTalentVariant(
    PavilionRotationState& state,
    std::int32_t contestant_id,
    LegacyRandom& random,
    std::string& error
) {
    if (!ValidateContestant(contestant_id, error)) {
        return 0;
    }
    const std::size_t index = static_cast<std::size_t>(contestant_id - 1);
    return DrawVariant(state.talent_used[index], kTalentVariantCount, random);
}

}  // namespace metaverse
