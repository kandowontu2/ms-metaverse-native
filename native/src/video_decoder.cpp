#include "video_decoder.hpp"

#include <algorithm>
#include <memory>
#include <limits>
#include <sstream>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace metaverse {
namespace {

std::string AvError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

std::string Utf8Path(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

struct FormatDeleter {
    void operator()(AVFormatContext* context) const {
        if (context != nullptr) {
            avformat_close_input(&context);
        }
    }
};

struct CodecDeleter {
    void operator()(AVCodecContext* context) const {
        avcodec_free_context(&context);
    }
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const {
        av_packet_free(&packet);
    }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const {
        av_frame_free(&frame);
    }
};

struct ScaleDeleter {
    void operator()(SwsContext* context) const {
        sws_freeContext(context);
    }
};

}  // namespace

class VideoDecoder::Impl {
public:
    std::unique_ptr<AVFormatContext, FormatDeleter> format;
    std::unique_ptr<AVCodecContext, CodecDeleter> codec;
    std::unique_ptr<AVPacket, PacketDeleter> packet;
    std::unique_ptr<AVFrame, FrameDeleter> frame;
    std::unique_ptr<SwsContext, ScaleDeleter> scale;
    int stream_index = -1;
    const AVCodec* decoder = nullptr;
    AVRational time_base{0, 1};
    AVRational frame_rate{0, 1};
    std::int64_t start_timestamp = 0;
    std::int64_t frame_count = 0;
    std::int64_t fallback_frame_index = 0;
    double fallback_duration = 1.0 / 30.0;
    bool sent_eof = false;
};

VideoDecoder::VideoDecoder() = default;
VideoDecoder::~VideoDecoder() = default;
VideoDecoder::VideoDecoder(VideoDecoder&&) noexcept = default;
VideoDecoder& VideoDecoder::operator=(VideoDecoder&&) noexcept = default;

void VideoDecoder::Close() {
    impl_.reset();
}

bool VideoDecoder::IsOpen() const {
    return impl_ != nullptr;
}

std::int64_t VideoDecoder::FrameCount() const {
    return impl_ ? impl_->frame_count : 0;
}

std::int64_t VideoDecoder::FrameForAudioSamples(
    std::uint64_t samples,
    std::uint32_t sample_rate
) const {
    if (!impl_ || sample_rate == 0 ||
        impl_->frame_rate.num <= 0 || impl_->frame_rate.den <= 0 ||
        samples > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()
        )) {
        return -1;
    }
    return av_rescale_q_rnd(
        static_cast<std::int64_t>(samples),
        AVRational{1, static_cast<int>(sample_rate)},
        av_inv_q(impl_->frame_rate),
        AV_ROUND_DOWN
    );
}

bool VideoDecoder::Open(const std::filesystem::path& path, std::string& error) {
    Close();
    error.clear();
    auto impl = std::make_unique<Impl>();
    AVFormatContext* raw_format = nullptr;
    const std::string input_path = Utf8Path(path);
    int result = avformat_open_input(&raw_format, input_path.c_str(), nullptr, nullptr);
    if (result < 0) {
        error = "avformat_open_input: " + AvError(result);
        return false;
    }
    impl->format.reset(raw_format);

    result = avformat_find_stream_info(impl->format.get(), nullptr);
    if (result < 0) {
        error = "avformat_find_stream_info: " + AvError(result);
        return false;
    }
    impl->stream_index = av_find_best_stream(
        impl->format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &impl->decoder, 0
    );
    if (impl->stream_index < 0 || impl->decoder == nullptr) {
        error = "no decodable video stream: " + AvError(impl->stream_index);
        return false;
    }

    impl->codec.reset(avcodec_alloc_context3(impl->decoder));
    if (!impl->codec) {
        error = "avcodec_alloc_context3 failed";
        return false;
    }
    result = avcodec_parameters_to_context(
        impl->codec.get(), impl->format->streams[impl->stream_index]->codecpar
    );
    if (result < 0) {
        error = "avcodec_parameters_to_context: " + AvError(result);
        return false;
    }
    result = avcodec_open2(impl->codec.get(), impl->decoder, nullptr);
    if (result < 0) {
        error = "avcodec_open2: " + AvError(result);
        return false;
    }

    impl->packet.reset(av_packet_alloc());
    impl->frame.reset(av_frame_alloc());
    if (!impl->packet || !impl->frame) {
        error = "unable to allocate FFmpeg packet/frame";
        return false;
    }
    AVStream* stream = impl->format->streams[impl->stream_index];
    impl->time_base = stream->time_base;
    const AVRational rate = av_guess_frame_rate(impl->format.get(), stream, nullptr);
    if (rate.num > 0 && rate.den > 0) {
        impl->frame_rate = rate;
        impl->fallback_duration = av_q2d(av_inv_q(rate));
    }
    impl->start_timestamp = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    impl->frame_count = std::max<std::int64_t>(stream->nb_frames, 0);
    impl_ = std::move(impl);
    return true;
}

bool VideoDecoder::Restart(std::string& error) {
    error.clear();
    if (!impl_) {
        error = "video decoder is not open";
        return false;
    }
    const int result = av_seek_frame(
        impl_->format.get(), impl_->stream_index, 0, AVSEEK_FLAG_BACKWARD
    );
    if (result < 0) {
        error = "av_seek_frame: " + AvError(result);
        return false;
    }
    avcodec_flush_buffers(impl_->codec.get());
    av_packet_unref(impl_->packet.get());
    av_frame_unref(impl_->frame.get());
    impl_->sent_eof = false;
    impl_->fallback_frame_index = 0;
    return true;
}

bool VideoDecoder::SeekFrame(
    std::int64_t frame_index,
    VideoFrame& output,
    bool& ended,
    std::string& error
) {
    output = {};
    ended = false;
    error.clear();
    if (!impl_) {
        error = "video decoder is not open";
        return false;
    }
    if (frame_index < 0 || impl_->frame_rate.num <= 0 || impl_->frame_rate.den <= 0) {
        error = "invalid frame seek target or unknown video frame rate";
        return false;
    }

    const std::int64_t relative_timestamp = av_rescale_q(
        frame_index, av_inv_q(impl_->frame_rate), impl_->time_base
    );
    const int result = av_seek_frame(
        impl_->format.get(),
        impl_->stream_index,
        impl_->start_timestamp + relative_timestamp,
        AVSEEK_FLAG_BACKWARD
    );
    if (result < 0) {
        error = "av_seek_frame: " + AvError(result);
        return false;
    }
    avcodec_flush_buffers(impl_->codec.get());
    av_packet_unref(impl_->packet.get());
    av_frame_unref(impl_->frame.get());
    impl_->sent_eof = false;
    impl_->fallback_frame_index = 0;

    while (true) {
        if (!DecodeNext(output, ended, error) || ended) {
            return error.empty();
        }
        if (output.frame_index >= frame_index) {
            return true;
        }
    }
}

bool VideoDecoder::DecodeNext(VideoFrame& output, bool& ended, std::string& error) {
    output = {};
    ended = false;
    error.clear();
    if (!impl_) {
        error = "video decoder is not open";
        return false;
    }

    while (true) {
        int result = avcodec_receive_frame(impl_->codec.get(), impl_->frame.get());
        if (result == 0) {
            break;
        }
        if (result == AVERROR_EOF) {
            ended = true;
            return true;
        }
        if (result != AVERROR(EAGAIN)) {
            error = "avcodec_receive_frame: " + AvError(result);
            return false;
        }
        if (impl_->sent_eof) {
            ended = true;
            return true;
        }

        result = av_read_frame(impl_->format.get(), impl_->packet.get());
        if (result < 0) {
            if (result != AVERROR_EOF) {
                error = "av_read_frame: " + AvError(result);
                return false;
            }
            impl_->sent_eof = true;
            result = avcodec_send_packet(impl_->codec.get(), nullptr);
            if (result < 0 && result != AVERROR_EOF) {
                error = "avcodec_send_packet(eof): " + AvError(result);
                return false;
            }
            continue;
        }
        if (impl_->packet->stream_index == impl_->stream_index) {
            result = avcodec_send_packet(impl_->codec.get(), impl_->packet.get());
            av_packet_unref(impl_->packet.get());
            if (result < 0) {
                error = "avcodec_send_packet: " + AvError(result);
                return false;
            }
        } else {
            av_packet_unref(impl_->packet.get());
        }
    }

    AVFrame* frame = impl_->frame.get();
    output.width = frame->width;
    output.height = frame->height;
    output.stride = frame->width * 4;
    output.codec = impl_->decoder->name != nullptr ? impl_->decoder->name : "unknown";
    if (frame->best_effort_timestamp != AV_NOPTS_VALUE) {
        const std::int64_t relative_timestamp =
            frame->best_effort_timestamp - impl_->start_timestamp;
        output.timestamp_seconds =
            static_cast<double>(relative_timestamp) * av_q2d(impl_->time_base);
        if (impl_->frame_rate.num > 0 && impl_->frame_rate.den > 0) {
            output.frame_index = av_rescale_q_rnd(
                relative_timestamp,
                impl_->time_base,
                av_inv_q(impl_->frame_rate),
                static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX)
            );
            impl_->fallback_frame_index = output.frame_index + 1;
        } else {
            output.frame_index = impl_->fallback_frame_index++;
        }
    } else {
        output.frame_index = impl_->fallback_frame_index++;
    }
    output.duration_seconds = frame->duration > 0
        ? static_cast<double>(frame->duration) * av_q2d(impl_->time_base)
        : impl_->fallback_duration;
    output.bgra.resize(
        static_cast<std::size_t>(output.stride) * static_cast<std::size_t>(output.height)
    );

    impl_->scale.reset(sws_getCachedContext(
        impl_->scale.release(),
        frame->width,
        frame->height,
        static_cast<AVPixelFormat>(frame->format),
        frame->width,
        frame->height,
        AV_PIX_FMT_BGRA,
        SWS_POINT,
        nullptr,
        nullptr,
        nullptr
    ));
    if (!impl_->scale) {
        error = "sws_getCachedContext failed";
        output = {};
        return false;
    }
    std::uint8_t* destination[] = {output.bgra.data(), nullptr, nullptr, nullptr};
    int destination_stride[] = {output.stride, 0, 0, 0};
    const int result = sws_scale(
        impl_->scale.get(),
        frame->data,
        frame->linesize,
        0,
        frame->height,
        destination,
        destination_stride
    );
    if (result != frame->height) {
        std::ostringstream message;
        message << "sws_scale returned " << result << " rows, expected " << frame->height;
        error = message.str();
        output = {};
        return false;
    }
    av_frame_unref(frame);
    return true;
}

bool DecodeFirstVideoFrame(
    const std::filesystem::path& path,
    VideoFrame& output,
    std::string& error
) {
    VideoDecoder decoder;
    if (!decoder.Open(path, error)) {
        return false;
    }
    bool ended = false;
    if (!decoder.DecodeNext(output, ended, error)) {
        return false;
    }
    if (ended) {
        error = "video ended before yielding a frame";
        return false;
    }
    return true;
}

bool DecodeAudioTrack(
    const std::filesystem::path& path,
    AudioTrack& output,
    bool& has_audio,
    std::string& error
) {
    output = {};
    has_audio = false;
    error.clear();
    AVFormatContext* raw_format = nullptr;
    const std::string input_path = Utf8Path(path);
    int result = avformat_open_input(&raw_format, input_path.c_str(), nullptr, nullptr);
    if (result < 0) {
        error = "avformat_open_input: " + AvError(result);
        return false;
    }
    std::unique_ptr<AVFormatContext, FormatDeleter> format(raw_format);
    result = avformat_find_stream_info(format.get(), nullptr);
    if (result < 0) {
        error = "avformat_find_stream_info: " + AvError(result);
        return false;
    }

    const AVCodec* decoder = nullptr;
    const int stream_index = av_find_best_stream(
        format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0
    );
    if (stream_index == AVERROR_STREAM_NOT_FOUND) {
        return true;
    }
    if (stream_index < 0 || decoder == nullptr) {
        error = "no decodable audio stream: " + AvError(stream_index);
        return false;
    }
    has_audio = true;

    std::unique_ptr<AVCodecContext, CodecDeleter> codec(avcodec_alloc_context3(decoder));
    if (!codec) {
        error = "avcodec_alloc_context3(audio) failed";
        return false;
    }
    result = avcodec_parameters_to_context(codec.get(), format->streams[stream_index]->codecpar);
    if (result < 0) {
        error = "avcodec_parameters_to_context(audio): " + AvError(result);
        return false;
    }
    result = avcodec_open2(codec.get(), decoder, nullptr);
    if (result < 0) {
        error = "avcodec_open2(audio): " + AvError(result);
        return false;
    }

    AVChannelLayout output_layout{};
    av_channel_layout_copy(&output_layout, &codec->ch_layout);
    SwrContext* raw_resample = nullptr;
    result = swr_alloc_set_opts2(
        &raw_resample,
        &output_layout,
        AV_SAMPLE_FMT_S16,
        codec->sample_rate,
        &codec->ch_layout,
        codec->sample_fmt,
        codec->sample_rate,
        0,
        nullptr
    );
    av_channel_layout_uninit(&output_layout);
    if (result < 0 || raw_resample == nullptr) {
        error = "swr_alloc_set_opts2: " + AvError(result);
        if (raw_resample != nullptr) {
            swr_free(&raw_resample);
        }
        return false;
    }
    struct ResampleDeleter {
        void operator()(SwrContext* context) const { swr_free(&context); }
    };
    std::unique_ptr<SwrContext, ResampleDeleter> resample(raw_resample);
    result = swr_init(resample.get());
    if (result < 0) {
        error = "swr_init: " + AvError(result);
        return false;
    }

    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    if (!packet || !frame) {
        error = "unable to allocate audio packet/frame";
        return false;
    }
    output.sample_rate = codec->sample_rate;
    output.channels = codec->ch_layout.nb_channels;
    bool sent_eof = false;
    while (true) {
        result = avcodec_receive_frame(codec.get(), frame.get());
        if (result == 0) {
            const int output_capacity = static_cast<int>(av_rescale_rnd(
                swr_get_delay(resample.get(), codec->sample_rate) + frame->nb_samples,
                codec->sample_rate,
                codec->sample_rate,
                AV_ROUND_UP
            ));
            std::vector<std::int16_t> converted(
                static_cast<std::size_t>(output_capacity) *
                static_cast<std::size_t>(output.channels)
            );
            std::uint8_t* destination[] = {
                reinterpret_cast<std::uint8_t*>(converted.data())
            };
            const int samples = swr_convert(
                resample.get(), destination, output_capacity,
                const_cast<const std::uint8_t**>(frame->extended_data), frame->nb_samples
            );
            if (samples < 0) {
                error = "swr_convert: " + AvError(samples);
                return false;
            }
            converted.resize(
                static_cast<std::size_t>(samples) * static_cast<std::size_t>(output.channels)
            );
            output.samples.insert(output.samples.end(), converted.begin(), converted.end());
            av_frame_unref(frame.get());
            continue;
        }
        if (result == AVERROR_EOF) {
            break;
        }
        if (result != AVERROR(EAGAIN)) {
            error = "avcodec_receive_frame(audio): " + AvError(result);
            return false;
        }
        if (sent_eof) {
            break;
        }

        result = av_read_frame(format.get(), packet.get());
        if (result < 0) {
            if (result != AVERROR_EOF) {
                error = "av_read_frame(audio): " + AvError(result);
                return false;
            }
            sent_eof = true;
            result = avcodec_send_packet(codec.get(), nullptr);
            if (result < 0 && result != AVERROR_EOF) {
                error = "avcodec_send_packet(audio eof): " + AvError(result);
                return false;
            }
            continue;
        }
        if (packet->stream_index == stream_index) {
            result = avcodec_send_packet(codec.get(), packet.get());
            av_packet_unref(packet.get());
            if (result < 0) {
                error = "avcodec_send_packet(audio): " + AvError(result);
                return false;
            }
        } else {
            av_packet_unref(packet.get());
        }
    }
    return true;
}

}  // namespace metaverse
