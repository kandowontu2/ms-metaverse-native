#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace metaverse {

struct HostPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(const HostPoint&) const = default;
};

constexpr std::size_t kHostPointCount = 10;
using HostPointTable = std::array<HostPoint, kHostPointCount>;

bool ParseHostPointData(
    std::istream& input,
    HostPointTable& points,
    std::string& error
);

bool LoadHostPointData(
    const std::filesystem::path& path,
    HostPointTable& points,
    std::string& error
);

std::wstring HostClipStem(std::int32_t channel, std::int32_t take);
std::int64_t HostFrameForAudioSamples(std::uint64_t samples);

struct Mode6HostClickOutcome {
    bool finish_modal = false;
    bool play_ouch_on_completion = false;

    bool operator==(const Mode6HostClickOutcome&) const = default;
};

// The generic mouse body at 0x0040d6ca ends a mode-6 C clip after every
// left click. SpriteHitTest controls only +0xf0, which makes the completion
// routine play OUCH.WAV; transparent/background clicks still end the modal.
Mode6HostClickOutcome PlanMode6HostClick(bool sprite_hit) noexcept;

struct Mode4HostCompletionOutcome {
    bool finish_modal = false;
    bool stop_companion_audio = false;

    bool operator==(const Mode4HostCompletionOutcome&) const = default;
};

// MMS completion at 0x0040d4a7 calls the shared 0x0040e4bd teardown
// immediately. That routine stops the companion channel, so mode-4 X clips do
// not wait for a WAV tail after their authored motion graph completes.
Mode4HostCompletionOutcome PlanMode4HostCompletion(
    bool motion_graph_complete,
    bool companion_audio_playing
) noexcept;

// The delayed C21/C31/C41 callbacks clear their document one-shot field
// before entering the generic modal runner (0x004066ea, 0x004088d1, and
// 0x00412968).  A failed media construction therefore still consumes the
// entry.  C11/C51/C91 use newly constructed dialog state but have the same
// consume-on-attempt ordering for that dialog instance.
inline void ConsumeLegacyHostEntryAttempt(bool& already_played) {
    already_played = true;
}

}  // namespace metaverse
