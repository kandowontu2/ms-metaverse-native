#pragma once

#include "game_data.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace metaverse {

struct MotionSpriteBounds {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;

    bool operator==(const MotionSpriteBounds&) const = default;
};

// SPR.dll SetSpritePosition stores the supplied X/Y directly as the top-left
// of the sprite rectangle. Rendering uses this half-open extent; the adjacent
// helper preserves SpriteHitTest's stricter edge comparisons.
MotionSpriteBounds MotionSpriteBoundsFromTopLeft(
    std::int32_t x,
    std::int32_t y,
    std::int32_t width,
    std::int32_t height
) noexcept;

// SPR.dll SpriteHitTest uses strict comparisons on every edge: a point must
// be greater than the stored top-left coordinate and less than top-left plus
// the sprite extent. This differs from the half-open rectangle used to draw
// the same sprite by exactly one pixel on its left and top borders.
[[nodiscard]] bool MotionSpritePointInsideLegacyHitBounds(
    const MotionSpriteBounds& bounds,
    std::int32_t x,
    std::int32_t y
) noexcept;

// Runtime for the on-disc MMS motion graphs. Coordinates are expressed in the
// original 640x480 logical canvas and identify a sprite's top-left corner.
class MotionPlayer {
public:
    bool Reset(
        const MotionScript& script,
        std::string& error,
        std::int32_t runtime_mode = 0
    );
    bool Tick(std::string& error);
    bool Hit(std::string& error);

    [[nodiscard]] bool Active() const;
    [[nodiscard]] bool Complete() const;
    [[nodiscard]] std::int32_t X() const;
    [[nodiscard]] std::int32_t Y() const;
    [[nodiscard]] std::int32_t Frame() const;
    [[nodiscard]] std::int32_t ResultCode() const;
    [[nodiscard]] std::int16_t CurrentRecordResultCode() const;
    [[nodiscard]] std::int16_t SoundIndex() const;
    [[nodiscard]] bool PlaysStateSound() const;
    [[nodiscard]] bool StopsStateSoundOnHit() const;
    [[nodiscard]] bool StopsStateSound() const;
    [[nodiscard]] std::uint64_t RecordEntrySerial() const;
    [[nodiscard]] bool LastEntryWasHit() const;
    [[nodiscard]] bool IgnoresMouseInput() const;
    // fcn.0040e6f2 paces each record by waiting for two timeGetTime tick
    // changes per delay unit.  Modes 4/6 force five units and 0x0080
    // reaction records force eight, irrespective of the caller's value.
    [[nodiscard]] std::int32_t TickDelayMilliseconds(
        std::int32_t caller_pacing_factor
    );
    [[nodiscard]] bool UsesMode4TimedSynchronization() const noexcept;
    // 0x0040eb52 corrects a delayed mode-4 timer pass before the ordinary
    // 0x0040d2ab update. The first call only captures the clock. Later calls
    // accumulate elapsed time beyond 100 ms and, once the strict >100 ms
    // threshold is crossed, skip authored AVI frames without consuming extra
    // coordinate-path entries.
    std::int32_t ApplyMode4TimedCatchUp(std::uint32_t elapsed_milliseconds);

private:
    enum class RecordEntry {
        initial,
        hit,
        timed,
    };

    bool EnterRecord(
        std::uint16_t offset,
        RecordEntry entry,
        std::string& error
    );
    const MotionRecord* CurrentRecord() const;

    MotionScript script_;
    std::size_t record_index_ = 0;
    std::size_t trajectory_index_ = 0;
    std::int32_t x_ = 0;
    std::int32_t y_ = 0;
    std::int32_t frame_ = 0;
    std::int32_t result_code_ = 0;
    std::int32_t runtime_mode_ = 0;
    std::uint64_t record_entry_serial_ = 0;
    RecordEntry last_record_entry_ = RecordEntry::initial;
    bool mode4_timed_sync_active_ = false;
    bool mode4_sync_first_tick_pending_ = false;
    bool mode4_sync_first_catchup_pending_ = false;
    std::uint32_t mode4_sync_overrun_milliseconds_ = 0;
    bool active_ = false;
    bool complete_ = false;
};

}  // namespace metaverse
