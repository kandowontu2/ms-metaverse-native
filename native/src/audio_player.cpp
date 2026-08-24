#include "audio_player.hpp"

#include <windows.h>
#include <mmsystem.h>

#include <limits>
#include <vector>

namespace metaverse {
namespace {

std::string WaveError(MMRESULT result) {
    char message[MAXERRORLENGTH] = {};
    if (waveOutGetErrorTextA(result, message, MAXERRORLENGTH) == MMSYSERR_NOERROR) {
        return message;
    }
    return "waveOut error " + std::to_string(result);
}

}  // namespace

class AudioPlayer::Impl {
public:
    HWAVEOUT device = nullptr;
    WAVEHDR header{};
    std::vector<std::int16_t> samples;
    std::uint32_t sample_rate = 0;
    std::uint16_t block_align = 0;
    bool prepared = false;
};

AudioPlayer::AudioPlayer() = default;
AudioPlayer::~AudioPlayer() {
    Stop();
}
bool AudioPlayer::Play(const AudioTrack& track, bool loop, std::string& error) {
    Stop();
    error.clear();
    if (track.sample_rate <= 0 || track.channels <= 0 || track.samples.empty()) {
        error = "audio track is empty or has an invalid format";
        return false;
    }
    const std::size_t byte_count = track.samples.size() * sizeof(std::int16_t);
    if (byte_count > std::numeric_limits<DWORD>::max()) {
        error = "audio track exceeds the waveOut buffer limit";
        return false;
    }

    auto impl = std::make_unique<Impl>();
    impl->samples = track.samples;
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(track.channels);
    format.nSamplesPerSec = static_cast<DWORD>(track.sample_rate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * sizeof(std::int16_t));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    impl->sample_rate = format.nSamplesPerSec;
    impl->block_align = format.nBlockAlign;
    MMRESULT result = waveOutOpen(
        &impl->device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL
    );
    if (result != MMSYSERR_NOERROR) {
        error = "waveOutOpen: " + WaveError(result);
        return false;
    }

    impl->header.lpData = reinterpret_cast<LPSTR>(impl->samples.data());
    impl->header.dwBufferLength = static_cast<DWORD>(byte_count);
    if (loop) {
        impl->header.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
        impl->header.dwLoops = std::numeric_limits<DWORD>::max();
    }
    result = waveOutPrepareHeader(impl->device, &impl->header, sizeof(impl->header));
    if (result != MMSYSERR_NOERROR) {
        error = "waveOutPrepareHeader: " + WaveError(result);
        waveOutClose(impl->device);
        return false;
    }
    impl->prepared = true;
    result = waveOutWrite(impl->device, &impl->header, sizeof(impl->header));
    if (result != MMSYSERR_NOERROR) {
        error = "waveOutWrite: " + WaveError(result);
        waveOutUnprepareHeader(impl->device, &impl->header, sizeof(impl->header));
        waveOutClose(impl->device);
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

void AudioPlayer::Stop() {
    if (!impl_) {
        return;
    }
    if (impl_->device != nullptr) {
        waveOutReset(impl_->device);
        if (impl_->prepared) {
            waveOutUnprepareHeader(impl_->device, &impl_->header, sizeof(impl_->header));
        }
        waveOutClose(impl_->device);
    }
    impl_.reset();
}

bool AudioPlayer::IsPlaying() const {
    return impl_ != nullptr &&
           (impl_->header.dwFlags & WHDR_DONE) == 0;
}

std::uint64_t AudioPlayer::PositionSamples() const {
    if (!impl_ || impl_->device == nullptr) {
        return 0;
    }
    MMTIME position{};
    position.wType = TIME_SAMPLES;
    if (waveOutGetPosition(
            impl_->device, &position, sizeof(position)
        ) != MMSYSERR_NOERROR) {
        return 0;
    }
    if (position.wType == TIME_SAMPLES) {
        return position.u.sample;
    }
    if (position.wType == TIME_BYTES) {
        return impl_->block_align == 0
            ? 0
            : position.u.cb / impl_->block_align;
    }
    if (position.wType == TIME_MS) {
        return static_cast<std::uint64_t>(position.u.ms) *
            impl_->sample_rate / 1000U;
    }
    return 0;
}

}  // namespace metaverse
