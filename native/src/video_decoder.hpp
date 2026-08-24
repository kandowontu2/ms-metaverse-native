#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace metaverse {

struct VideoFrame {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::string codec;
    std::int64_t frame_index = 0;
    double timestamp_seconds = 0.0;
    double duration_seconds = 0.0;
    std::vector<std::uint8_t> bgra;
};

struct AudioTrack {
    int sample_rate = 0;
    int channels = 0;
    std::vector<std::int16_t> samples;
};

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(VideoDecoder&&) noexcept;
    VideoDecoder& operator=(VideoDecoder&&) noexcept;
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool Open(const std::filesystem::path& path, std::string& error);
    void Close();
    bool IsOpen() const;
    bool DecodeNext(VideoFrame& output, bool& ended, std::string& error);
    bool Restart(std::string& error);
    [[nodiscard]] std::int64_t FrameCount() const;
    bool SeekFrame(
        std::int64_t frame_index,
        VideoFrame& output,
        bool& ended,
        std::string& error
    );
    [[nodiscard]] std::int64_t FrameForAudioSamples(
        std::uint64_t samples,
        std::uint32_t sample_rate
    ) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

bool DecodeFirstVideoFrame(
    const std::filesystem::path& path,
    VideoFrame& output,
    std::string& error
);

bool DecodeAudioTrack(
    const std::filesystem::path& path,
    AudioTrack& output,
    bool& has_audio,
    std::string& error
);

}  // namespace metaverse
