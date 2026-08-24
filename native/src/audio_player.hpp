#pragma once

#include "video_decoder.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace metaverse {

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(AudioPlayer&&) = delete;
    AudioPlayer& operator=(AudioPlayer&&) = delete;
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool Play(const AudioTrack& track, bool loop, std::string& error);
    void Stop();
    bool IsPlaying() const;
    std::uint64_t PositionSamples() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace metaverse
