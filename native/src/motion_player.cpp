#include "motion_player.hpp"

#include <algorithm>

namespace metaverse {
namespace {

constexpr std::uint16_t kRelativeTrajectory = 0x0100;

}  // namespace

MotionSpriteBounds MotionSpriteBoundsFromTopLeft(
    std::int32_t x,
    std::int32_t y,
    std::int32_t width,
    std::int32_t height
) noexcept {
    return {
        x,
        y,
        x + std::max<std::int32_t>(0, width),
        y + std::max<std::int32_t>(0, height),
    };
}

bool MotionSpritePointInsideLegacyHitBounds(
    const MotionSpriteBounds& bounds,
    std::int32_t x,
    std::int32_t y
) noexcept {
    return x > bounds.left && x < bounds.right &&
           y > bounds.top && y < bounds.bottom;
}

bool MotionPlayer::Reset(
    const MotionScript& script,
    std::string& error,
    std::int32_t runtime_mode
) {
    script_ = script;
    record_index_ = 0;
    trajectory_index_ = 0;
    x_ = 0;
    y_ = 0;
    frame_ = 0;
    result_code_ = 0;
    runtime_mode_ = runtime_mode;
    record_entry_serial_ = 0;
    last_record_entry_ = RecordEntry::initial;
    mode4_timed_sync_active_ = false;
    mode4_sync_first_tick_pending_ = false;
    mode4_sync_first_catchup_pending_ = false;
    mode4_sync_overrun_milliseconds_ = 0;
    active_ = false;
    complete_ = false;
    error.clear();
    if (script_.records.empty()) {
        error = "motion script has no playable records";
        return false;
    }
    return EnterRecord(0, RecordEntry::initial, error);
}

const MotionRecord* MotionPlayer::CurrentRecord() const {
    return active_ && record_index_ < script_.records.size()
        ? &script_.records[record_index_]
        : nullptr;
}

bool MotionPlayer::EnterRecord(
    std::uint16_t offset,
    RecordEntry entry,
    std::string& error
) {
    const auto found = std::find_if(
        script_.records.begin(), script_.records.end(),
        [offset](const MotionRecord& record) {
            return record.file_offset == offset;
        }
    );
    if (found == script_.records.end()) {
        error = "motion transition targets a missing record";
        active_ = false;
        return false;
    }

    record_index_ = static_cast<std::size_t>(
        std::distance(script_.records.begin(), found)
    );
    ++record_entry_serial_;
    last_record_entry_ = entry;
    trajectory_index_ = 0;
    if (entry == RecordEntry::initial) {
        x_ = found->initial_x;
        y_ = found->initial_y;
    } else if ((found->flags & 0x0004) != 0) {
        if (entry == RecordEntry::timed) {
            // fcn.0040d2ab contains an original typo: its timed-transition
            // path stores both authored coordinates into X, so initial_y wins
            // and Y is left untouched.  The mouse-hit path correctly assigns
            // X and Y separately and is preserved below.
            x_ = found->initial_x;
            x_ = found->initial_y;
        } else {
            x_ = found->initial_x;
            y_ = found->initial_y;
        }
    }
    frame_ = found->loop_frame;
    if (entry == RecordEntry::timed && runtime_mode_ == 4) {
        // fcn.0040d2ab clears field 0x118 on every timed transition in mode 4.
        // The modal loop then calls 0x0040eb52: its first pass only captures
        // timeGetTime, while later passes pace frames on 100 ms boundaries.
        mode4_timed_sync_active_ = true;
        mode4_sync_first_tick_pending_ = true;
        mode4_sync_first_catchup_pending_ = true;
        mode4_sync_overrun_milliseconds_ = 0;
    }
    result_code_ = 0;
    active_ = true;
    complete_ = false;
    error.clear();
    return true;
}

bool MotionPlayer::Tick(std::string& error) {
    error.clear();
    const MotionRecord* record = CurrentRecord();
    if (record == nullptr || complete_) {
        return false;
    }

    ++frame_;
    if (frame_ == record->terminal_frame) {
        if ((record->flags & 0x0201) == 0) {
            if ((record->flags & 0x0008) != 0) {
                complete_ = true;
                active_ = false;
                result_code_ = record->result_code;
                return true;
            }
            return EnterRecord(
                record->timed_transition_offset, RecordEntry::timed, error
            );
        }
        // Generic mode 4 holds the terminal-minus-one host frame while its
        // coordinate path continues. Other modes loop back to field 0x06.
        if ((record->flags & 0x0008) != 0 && runtime_mode_ == 4) {
            mode4_timed_sync_active_ = false;
            mode4_sync_first_tick_pending_ = false;
            mode4_sync_first_catchup_pending_ = false;
            mode4_sync_overrun_milliseconds_ = 0;
            --frame_;
        } else {
            frame_ = record->loop_frame;
        }
    }

    if ((record->flags & 0x0002) != 0) {
        x_ += record->delta_x;
        y_ += record->delta_y;
        if (x_ < -200 || y_ < -200 || x_ > 680) {
            if ((record->flags & 0x0018) != 0) {
                complete_ = true;
                active_ = false;
                result_code_ = runtime_mode_ == 5 ? 50 : record->result_code;
                return true;
            }
            if ((record->flags & 0x0008) != 0) {
                complete_ = true;
                active_ = false;
                result_code_ = record->result_code;
                return true;
            }
            return EnterRecord(
                record->timed_transition_offset, RecordEntry::timed, error
            );
        }
    } else if ((record->flags & 0x0200) != 0) {
        if (trajectory_index_ >= record->trajectory_points.size()) {
            if ((record->flags & 0x0008) != 0) {
                complete_ = true;
                active_ = false;
                result_code_ = record->result_code;
                return true;
            }
            return EnterRecord(
                record->timed_transition_offset, RecordEntry::timed, error
            );
        }
        const auto& point = record->trajectory_points[trajectory_index_++];
        if ((record->flags & kRelativeTrajectory) != 0) {
            // 0x0040d40d/0x0040d434 add each signed pair to the current
            // position.  In particular, D2.MMS is a cumulative falling arc,
            // not a set of offsets from the record's entry point.
            x_ += point[0];
            y_ += point[1];
        } else {
            x_ = point[0];
            y_ = point[1];
        }
    }
    return true;
}

bool MotionPlayer::Hit(std::string& error) {
    error.clear();
    const MotionRecord* record = CurrentRecord();
    if (record == nullptr || complete_ || record->hit_transition_offset == 0) {
        return false;
    }
    return EnterRecord(
        record->hit_transition_offset, RecordEntry::hit, error
    );
}

bool MotionPlayer::Active() const {
    return active_ && !complete_;
}

bool MotionPlayer::Complete() const {
    return complete_;
}

std::int32_t MotionPlayer::X() const {
    return x_;
}

std::int32_t MotionPlayer::Y() const {
    return y_;
}

std::int32_t MotionPlayer::Frame() const {
    return frame_;
}

std::int32_t MotionPlayer::ResultCode() const {
    return result_code_;
}

std::int16_t MotionPlayer::CurrentRecordResultCode() const {
    const MotionRecord* record = CurrentRecord();
    return record != nullptr ? record->result_code : 0;
}

std::int16_t MotionPlayer::SoundIndex() const {
    const MotionRecord* record = CurrentRecord();
    return record != nullptr ? record->sound_index : 0;
}

bool MotionPlayer::PlaysStateSound() const {
    const MotionRecord* record = CurrentRecord();
    return record != nullptr && (record->flags & 0x0060) != 0;
}

bool MotionPlayer::StopsStateSoundOnHit() const {
    const MotionRecord* record = CurrentRecord();
    return record != nullptr && (record->flags & 0x0040) != 0;
}

bool MotionPlayer::StopsStateSound() const {
    const MotionRecord* record = CurrentRecord();
    return record != nullptr && (record->flags & 0x0020) != 0;
}

std::uint64_t MotionPlayer::RecordEntrySerial() const {
    return record_entry_serial_;
}

bool MotionPlayer::LastEntryWasHit() const {
    return last_record_entry_ == RecordEntry::hit;
}

bool MotionPlayer::IgnoresMouseInput() const {
    const MotionRecord* record = CurrentRecord();
    return record != nullptr && (record->flags & 0x0080) != 0;
}

std::int32_t MotionPlayer::TickDelayMilliseconds(
    std::int32_t caller_pacing_factor
) {
    const MotionRecord* record = CurrentRecord();
    if (record == nullptr) {
        return 0;
    }

    if (runtime_mode_ == 4 && mode4_timed_sync_active_) {
        if (mode4_sync_first_tick_pending_) {
            mode4_sync_first_tick_pending_ = false;
            return 0;
        }
        return 100;
    }

    std::int32_t factor = caller_pacing_factor;
    if ((record->flags & 0x0080) != 0) {
        factor = 8;
    }
    if (runtime_mode_ == 4 || runtime_mode_ == 6) {
        factor = 5;
    } else if (runtime_mode_ == 5) {
        // fcn.0040e6f2 compares the active record pointer with the MMS buffer
        // base. Its mode-5 fallback animation gives the root ten pacing units
        // and every linked record one.
        factor = record_index_ == 0 ? 10 : 1;
    }

    std::int32_t interval_units = record->timer_interval / 20;
    if (interval_units == 0) {
        interval_units = 1;
    }
    if (interval_units < 0 || factor <= 0) {
        return 0;
    }
    return interval_units * factor * 2;
}

bool MotionPlayer::UsesMode4TimedSynchronization() const noexcept {
    return active_ && !complete_ && runtime_mode_ == 4 &&
           mode4_timed_sync_active_;
}

std::int32_t MotionPlayer::ApplyMode4TimedCatchUp(
    std::uint32_t elapsed_milliseconds
) {
    if (!UsesMode4TimedSynchronization()) {
        return 0;
    }
    if (mode4_sync_first_catchup_pending_) {
        // +0x110 starts at zero. The first 0x0040eb52 call records
        // timeGetTime and performs no pacing or frame correction.
        mode4_sync_first_catchup_pending_ = false;
        return 0;
    }
    if (elapsed_milliseconds > 100'000U) {
        // The original treats this as a stale/wrapped clock sample: update
        // the anchor while preserving the accumulated +0x114 overrun.
        return 0;
    }
    if (elapsed_milliseconds <= 100U) {
        return 0;
    }

    mode4_sync_overrun_milliseconds_ += elapsed_milliseconds - 100U;
    if (mode4_sync_overrun_milliseconds_ <= 100U) {
        return 0;
    }

    // 0x0040ec2b uses the single-precision 0.01f constant before truncation.
    const auto skipped = static_cast<std::int32_t>(
        static_cast<float>(mode4_sync_overrun_milliseconds_) * 0.01F
    );
    frame_ += skipped;
    const MotionRecord* record = CurrentRecord();
    if (record != nullptr && frame_ >= record->terminal_frame) {
        // This is a direct reset to loop_frame, not a modulo wrap.
        frame_ = record->loop_frame;
    }
    mode4_sync_overrun_milliseconds_ = 0;
    return skipped;
}

}  // namespace metaverse
