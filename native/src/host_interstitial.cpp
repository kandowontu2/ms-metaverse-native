#include "host_interstitial.hpp"

#include <fstream>
#include <istream>

namespace metaverse {
namespace {

bool ReadI32(std::istream& input, std::int32_t& value) {
    unsigned char bytes[4] = {};
    if (!input.read(reinterpret_cast<char*>(bytes), sizeof(bytes))) {
        return false;
    }
    const std::uint32_t encoded =
        static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    value = static_cast<std::int32_t>(encoded);
    return true;
}

}  // namespace

bool ParseHostPointData(
    std::istream& input,
    HostPointTable& points,
    std::string& error
) {
    HostPointTable parsed{};
    for (HostPoint& point : parsed) {
        if (!ReadI32(input, point.x) || !ReadI32(input, point.y)) {
            error = "HPNT.DAT is truncated; expected ten coordinate pairs";
            return false;
        }
    }
    char trailing = 0;
    if (input.read(&trailing, 1)) {
        error = "HPNT.DAT contains trailing data";
        return false;
    }
    if (!input.eof()) {
        error = "HPNT.DAT could not be read completely";
        return false;
    }
    points = parsed;
    error.clear();
    return true;
}

bool LoadHostPointData(
    const std::filesystem::path& path,
    HostPointTable& points,
    std::string& error
) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "could not open host position table: " + path.string();
        return false;
    }
    return ParseHostPointData(input, points, error);
}

std::wstring HostClipStem(std::int32_t channel, std::int32_t take) {
    if (channel < 0 || channel > 9 || take < 0 || take > 9) {
        return {};
    }
    return L"C" + std::to_wstring(channel) + std::to_wstring(take);
}

std::int64_t HostFrameForAudioSamples(std::uint64_t samples) {
    // Mode-6 fcn.0040eb52 uses 0.000907029... (10 / 11025) and
    // requests one frame beyond the truncated PCM position.
    return static_cast<std::int64_t>(samples * 10U / 11025U) + 1;
}

Mode6HostClickOutcome PlanMode6HostClick(bool sprite_hit) noexcept {
    return {
        true,
        sprite_hit,
    };
}

Mode4HostCompletionOutcome PlanMode4HostCompletion(
    bool motion_graph_complete,
    bool companion_audio_playing
) noexcept {
    (void)companion_audio_playing;
    return {
        motion_graph_complete,
        motion_graph_complete,
    };
}

}  // namespace metaverse
