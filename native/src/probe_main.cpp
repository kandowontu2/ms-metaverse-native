#include "game_data.hpp"
#include "game_state.hpp"
#include "cryo_media.hpp"
#include "cryo_pavilion.hpp"
#include "host_interstitial.hpp"
#include "host_reaction.hpp"
#include "judging_pavilion.hpp"
#include "looks_pavilion.hpp"
#include "motion_player.hpp"
#include "pavilion_rotation.hpp"
#include "scene_navigator.hpp"
#include "slot_machine.hpp"
#include "video_decoder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

bool ReadBitmapDimensions(
    const std::filesystem::path& path,
    std::int32_t& width,
    std::int32_t& height
) {
    std::ifstream input(path, std::ios::binary);
    char signature[2] = {};
    input.read(signature, sizeof(signature));
    if (!input || signature[0] != 'B' || signature[1] != 'M') {
        return false;
    }
    input.seekg(18, std::ios::beg);
    input.read(reinterpret_cast<char*>(&width), sizeof(width));
    input.read(reinterpret_cast<char*>(&height), sizeof(height));
    return static_cast<bool>(input) && width > 0 && height != 0;
}

bool FilesHaveSameContents(
    const std::filesystem::path& left,
    const std::filesystem::path& right
) {
    std::error_code error;
    const auto left_size = std::filesystem::file_size(left, error);
    if (error) {
        return false;
    }
    const auto right_size = std::filesystem::file_size(right, error);
    if (error || left_size != right_size) {
        return false;
    }

    std::ifstream left_input(left, std::ios::binary);
    std::ifstream right_input(right, std::ios::binary);
    std::array<char, 64 * 1024> left_buffer{};
    std::array<char, 64 * 1024> right_buffer{};
    while (left_input && right_input) {
        left_input.read(left_buffer.data(), left_buffer.size());
        right_input.read(right_buffer.data(), right_buffer.size());
        const auto left_count = left_input.gcount();
        const auto right_count = right_input.gcount();
        if (left_count != right_count || !std::equal(
                left_buffer.begin(),
                left_buffer.begin() + left_count,
                right_buffer.begin()
            )) {
            return false;
        }
    }
    return left_input.eof() && right_input.eof();
}

std::size_t CountAviFiles(const std::filesystem::path& root) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".AVI") {
            ++count;
        }
    }
    return count;
}

struct VideoTimingSummary {
    std::size_t frame_count = 0;
    int width = 0;
    int height = 0;
    double end_seconds = 0.0;
};

bool SummarizeVideoTiming(
    const std::filesystem::path& path,
    VideoTimingSummary& summary,
    std::string& error
) {
    summary = {};
    metaverse::VideoDecoder decoder;
    if (!decoder.Open(path, error)) {
        return false;
    }
    double previous_timestamp = -1.0;
    while (true) {
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!decoder.DecodeNext(frame, ended, error)) {
            return false;
        }
        if (ended) {
            return summary.frame_count != 0;
        }
        if (frame.width <= 0 || frame.height <= 0 ||
            frame.duration_seconds <= 0.0 ||
            frame.timestamp_seconds < previous_timestamp ||
            (summary.frame_count != 0 &&
             (frame.width != summary.width || frame.height != summary.height))) {
            error = "invalid or inconsistent decoded video timeline";
            return false;
        }
        if (summary.frame_count == 0) {
            summary.width = frame.width;
            summary.height = frame.height;
        }
        ++summary.frame_count;
        previous_timestamp = frame.timestamp_seconds;
        summary.end_seconds = std::max(
            summary.end_seconds,
            frame.timestamp_seconds + frame.duration_seconds
        );
    }
}

bool AuditNavigationRangeEndpoints(
    const std::filesystem::path& path,
    const metaverse::SceneDefinition& scene,
    std::size_t& decoded_endpoint_count,
    std::size_t& eof_boundary_count,
    std::string& error
) {
    VideoTimingSummary timing;
    if (!SummarizeVideoTiming(path, timing, error)) {
        return false;
    }
    metaverse::VideoDecoder decoder;
    if (!decoder.Open(path, error) ||
        decoder.FrameCount() != static_cast<std::int64_t>(timing.frame_count)) {
        error = error.empty()
            ? "reported frame count does not match decoded navigation timeline"
            : error;
        return false;
    }

    std::set<std::int32_t> endpoints;
    for (const auto& object : scene.objects) {
        for (const auto& range : object.ranges) {
            endpoints.insert(range[0]);
            endpoints.insert(range[1]);
        }
    }
    for (const std::int32_t endpoint : endpoints) {
        if (endpoint < 0 ||
            endpoint > static_cast<std::int64_t>(timing.frame_count)) {
            error = "navigation DAT endpoint exceeds the AVI timeline: " +
                    std::to_string(endpoint) + " > " +
                    std::to_string(timing.frame_count);
            return false;
        }
        if (endpoint == static_cast<std::int64_t>(timing.frame_count)) {
            ++eof_boundary_count;
            continue;
        }
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!decoder.SeekFrame(endpoint, frame, ended, error) || ended ||
            frame.frame_index != endpoint) {
            if (error.empty()) {
                error = "navigation DAT endpoint seek mismatch at " +
                        std::to_string(endpoint);
            }
            return false;
        }
        ++decoded_endpoint_count;
    }
    return true;
}

double AudioDurationSeconds(const metaverse::AudioTrack& audio) {
    if (audio.sample_rate <= 0 || audio.channels <= 0) {
        return 0.0;
    }
    return static_cast<double>(audio.samples.size()) /
        static_cast<double>(audio.sample_rate * audio.channels);
}

struct JudgingResponseFamily {
    const char* directory;
    const char* prefix;
    int width;
    int height;
};

bool AuditJudgingOutcomeAssets(
    const std::filesystem::path& root,
    std::initializer_list<JudgingResponseFamily> response_families,
    std::string& error
) {
    for (const char* relative : {
             "WAV/GONG.WAV",
             "WAV/PENALTY.WAV",
             "WAV/ANNOUNCE/REWARD1.WAV",
             "WAV/ANNOUNCE/WRONG5.WAV",
         }) {
        if (!std::filesystem::is_regular_file(root / relative)) {
            error = std::string("missing judging outcome sound ") + relative;
            return false;
        }
    }

    for (int contestant_id = 1; contestant_id <= 10; ++contestant_id) {
        const std::string filename = std::to_string(contestant_id) + ".BMP";
        for (const char* directory : {"BMP/GONG", "BMP/PENALTY"}) {
            std::int32_t width = 0;
            std::int32_t height = 0;
            if (!ReadBitmapDimensions(
                    root / directory / filename, width, height
                ) || width != 60 || height != 60) {
                error = std::string("invalid judging portrait ") + directory +
                    "/" + filename;
                return false;
            }
        }

        for (const auto& family : response_families) {
            metaverse::VideoFrame frame;
            const auto path = root / family.directory /
                (std::string(family.prefix) + std::to_string(contestant_id) +
                 ".AVI");
            error.clear();
            if (!metaverse::DecodeFirstVideoFrame(path, frame, error) ||
                frame.width != family.width || frame.height != family.height) {
                if (error.empty()) {
                    error = "unexpected judging response dimensions";
                }
                error = path.generic_string() + ": " + error;
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: ms_metaverse_probe <disk1-assets> [disk2-assets]\n";
        return 2;
    }

    const std::filesystem::path assets(argv[1]);
    const auto pavilion_template = assets / "SETUP32/DATA/NRFPAV.DAT";
    const auto transfer_template = assets / "SETUP32/DATA/TLST.DAT";
    if (std::filesystem::exists(pavilion_template) ||
        std::filesystem::exists(transfer_template)) {
        std::error_code template_error;
        if (std::filesystem::file_size(pavilion_template, template_error) != 130 ||
            template_error) {
            std::cerr << "FAIL SETUP32/DATA/NRFPAV.DAT size\n";
            return 1;
        }
        std::ifstream pavilion_input(pavilion_template, std::ios::binary);
        metaverse::PavilionRotationState pavilion_state;
        std::string pavilion_error;
        if (!pavilion_input || !metaverse::ParseLegacyPavilionRotation(
                pavilion_input, pavilion_state, pavilion_error
            )) {
            std::cerr << "FAIL SETUP32/DATA/NRFPAV.DAT: " << pavilion_error << '\n';
            return 1;
        }
        template_error.clear();
        if (std::filesystem::file_size(transfer_template, template_error) != 256 ||
            template_error) {
            std::cerr << "FAIL SETUP32/DATA/TLST.DAT size\n";
            return 1;
        }
        std::cout << "OK legacy state NRFPAV.DAT=130 TLST.DAT=256 distinct=1\n";
    } else {
        std::cout << "OK data-only package omits legacy installer templates\n";
    }
    std::int32_t profile_width = 0;
    std::int32_t profile_height = 0;
    std::int32_t profile_ok_width = 0;
    std::int32_t profile_ok_height = 0;
    if (!ReadBitmapDimensions(
            assets / "BMP/PASSWORD/PASSWORD.BMP",
            profile_width,
            profile_height
        ) || profile_width != 640 || profile_height != 480 ||
        !ReadBitmapDimensions(
            assets / "BMP/PASSWORD/OK.BMP",
            profile_ok_width,
            profile_ok_height
        ) || profile_ok_width != 53 || profile_ok_height != 52) {
        std::cerr << "FAIL original profile artwork dimensions\n";
        return 1;
    }
    std::cout << "OK profile artwork=640x480 OK-pressed=53x52 "
                 "edits=205,208/295@208x16\n";
    const metaverse::HostPointTable expected_host_points{{
        {0, 0}, {160, 240}, {80, 256}, {80, 256}, {304, 256},
        {96, 200}, {160, 240}, {160, 240}, {160, 240}, {144, 120},
    }};
    metaverse::HostPointTable host_points{};
    std::string host_error;
    if (!metaverse::LoadHostPointData(
            assets / "DAT/HOST/HPNT.DAT", host_points, host_error
        ) || host_points != expected_host_points) {
        std::cerr << "FAIL DAT/HOST/HPNT.DAT: " << host_error << '\n';
        return 1;
    }
    const std::array<std::wstring, 9> primary_host_stems = {
        L"C11", L"C12", L"C21", L"C31", L"C41",
        L"C51", L"C61", L"C71", L"C81",
    };
    double maximum_host_silent_tail_seconds = 0.0;
    for (const std::wstring& stem : primary_host_stems) {
        const auto movie = assets / L"MOV/HOST" / (stem + L".AVI");
        const auto sound = assets / L"WAV/HOST" / (stem + L".WAV");
        metaverse::VideoDecoder host_decoder;
        metaverse::AudioTrack host_audio;
        bool has_host_audio = false;
        if (!std::filesystem::is_regular_file(movie) ||
            !std::filesystem::is_regular_file(sound) ||
            !host_decoder.Open(movie, host_error) ||
            host_decoder.FrameCount() <= 0 ||
            !metaverse::DecodeAudioTrack(
                sound, host_audio, has_host_audio, host_error
            ) || !has_host_audio || host_audio.sample_rate != 11'025 ||
            host_audio.channels != 1) {
            std::cerr << "FAIL bundled host cue "
                      << std::string(stem.begin(), stem.end()) << '\n';
            return 1;
        }
        const double movie_seconds =
            static_cast<double>(host_decoder.FrameCount()) / 10.0;
        const double audio_seconds = AudioDurationSeconds(host_audio);
        if (audio_seconds > movie_seconds + 0.001) {
            std::cerr << "FAIL host cue WAV outlasts its fake MMS timeline "
                      << std::string(stem.begin(), stem.end()) << '\n';
            return 1;
        }
        maximum_host_silent_tail_seconds = std::max(
            maximum_host_silent_tail_seconds,
            movie_seconds - audio_seconds
        );
    }
    metaverse::VideoDecoder c91_decoder;
    metaverse::AudioTrack c91_audio;
    bool has_c91_audio = false;
    if (!c91_decoder.Open(assets / "MOV/HOST/C91.AVI", host_error) ||
        c91_decoder.FrameCount() <= 0 ||
        !metaverse::DecodeAudioTrack(
            assets / "WAV/HOST/C91.WAV", c91_audio, has_c91_audio, host_error
        ) || !has_c91_audio || c91_audio.sample_rate != 11'025 ||
        c91_audio.channels != 1 ||
        AudioDurationSeconds(c91_audio) >
            static_cast<double>(c91_decoder.FrameCount()) / 10.0 + 0.001) {
        std::cerr << "FAIL bundled host cue C91\n";
        return 1;
    }
    maximum_host_silent_tail_seconds = std::max(
        maximum_host_silent_tail_seconds,
        static_cast<double>(c91_decoder.FrameCount()) / 10.0 -
            AudioDurationSeconds(c91_audio)
    );
    std::cout << "OK DAT/HOST entry-cues=C11,C21,C31,C41,C51,C91 "
                 "positions=HPNT.DAT audio-master=11025x1 max-silent-tail="
              << maximum_host_silent_tail_seconds << "s\n";
    if (metaverse::kPavilionCreditsX != 465 ||
        metaverse::kPavilionCreditsY != 25 ||
        metaverse::kPavilionPortraitX != 287 ||
        metaverse::kPavilionPortraitY != 412 ||
        metaverse::kPavilionPortraitSize != 60 ||
        metaverse::kPavilionPortraitPitch != 70 ||
        metaverse::PavilionCreditText(
            12.5f, metaverse::JudgingCategory::talent
        ) != L"$12.50   " ||
        metaverse::PavilionCreditText(
            12.5f, metaverse::JudgingCategory::brains
        ) != L"$12.50   " ||
        metaverse::PavilionCreditText(
            12.5f, metaverse::JudgingCategory::looks
        ) != L"$12.50") {
        std::cerr << "FAIL recovered pavilion painter invariants\n";
        return 1;
    }
    std::cout << "OK judging painters cash=green@465,25 formats=wide-spaced/"
                 "Looks portrait-frames=white/red@287+70n,412:60x60 "
                 "hover-name=green@portrait-x,388\n";
    const auto validate_host_reactions = [&](const std::filesystem::path& root) {
        std::size_t active_clips = 0;
        std::set<std::wstring> audio_outlasts_graph;
        for (std::int32_t group = 1; group <= 3; ++group) {
            const auto count = metaverse::kHostReactionVariantCounts[
                static_cast<std::size_t>(group - 1)
            ];
            for (std::int32_t variant = 1;
                 variant <= static_cast<std::int32_t>(count); ++variant) {
                const std::wstring stem =
                    metaverse::HostReactionStem(group, variant);
                metaverse::MotionScript script;
                if (!metaverse::LoadMotionScript(
                        root / L"DAT/HOST" / (stem + L".MMS"),
                        script,
                        host_error
                    ) || script.records.empty() ||
                    !std::filesystem::is_regular_file(
                        root / L"MOV/HOST" / (stem + L".AVI")
                    ) ||
                    !std::filesystem::is_regular_file(
                        root / L"WAV/HOST" / (stem + L".WAV")
                    )) {
                    std::cerr << "FAIL host reaction "
                              << std::string(stem.begin(), stem.end()) << ": "
                              << host_error << '\n';
                    return false;
                }
                metaverse::MotionPlayer motion;
                if (!motion.Reset(script, host_error, 4)) {
                    std::cerr << "FAIL host reaction motion reset "
                              << std::string(stem.begin(), stem.end()) << ": "
                              << host_error << '\n';
                    return false;
                }
                std::int32_t maximum_frame = motion.Frame();
                std::size_t ticks = 0;
                std::int64_t graph_duration_ms = 0;
                while (motion.Active() && ticks++ < 10'000) {
                    graph_duration_ms += motion.TickDelayMilliseconds(5);
                    if (!motion.Tick(host_error)) {
                        std::cerr << "FAIL host reaction motion tick "
                                  << std::string(stem.begin(), stem.end()) << ": "
                                  << host_error << '\n';
                        return false;
                    }
                    if (motion.Active()) {
                        maximum_frame = std::max(maximum_frame, motion.Frame());
                    }
                }
                if (!motion.Complete()) {
                    std::cerr << "FAIL host reaction does not complete: "
                              << std::string(stem.begin(), stem.end()) << '\n';
                    for (const auto& record : script.records) {
                        std::cerr << "  off=" << record.file_offset
                                  << " mode=" << record.media_mode
                                  << " frames=" << record.loop_frame << ".."
                                  << record.terminal_frame
                                  << " hit=" << record.hit_transition_offset
                                  << " timed=" << record.timed_transition_offset
                                  << " flags=0x" << std::hex << record.flags
                                  << std::dec << " delta=" << record.delta_x
                                  << ',' << record.delta_y
                                  << " timer=" << record.timer_interval
                                  << " result=" << record.result_code
                                  << " initial=" << record.initial_x << ','
                                  << record.initial_y << " points="
                                  << record.trajectory_points.size() << '\n';
                    }
                    return false;
                }
                metaverse::AudioTrack companion_audio;
                bool has_companion_audio = false;
                if (!metaverse::DecodeAudioTrack(
                        root / L"WAV/HOST" / (stem + L".WAV"),
                        companion_audio,
                        has_companion_audio,
                        host_error
                    ) || !has_companion_audio) {
                    std::cerr << "FAIL host reaction companion audio "
                              << std::string(stem.begin(), stem.end()) << ": "
                              << host_error << '\n';
                    return false;
                }
                if (AudioDurationSeconds(companion_audio) * 1000.0 >
                    static_cast<double>(graph_duration_ms) + 0.5) {
                    audio_outlasts_graph.insert(stem);
                }
                metaverse::VideoDecoder decoder;
                metaverse::VideoFrame last_requested_frame;
                bool seek_ended = false;
                if (maximum_frame < 0 || !decoder.Open(
                        root / L"MOV/HOST" / (stem + L".AVI"), host_error
                    ) ||
                    !decoder.SeekFrame(
                        maximum_frame,
                        last_requested_frame,
                        seek_ended,
                        host_error
                    )) {
                    std::cerr << "FAIL host reaction MMS frame seek "
                              << maximum_frame << " in "
                              << std::string(stem.begin(), stem.end()) << ": "
                              << host_error << '\n';
                    return false;
                }
                ++active_clips;
            }
        }
        const std::set<std::wstring> expected_audio_tails{
            L"X14", L"X18", L"X211", L"X212",
        };
        if (active_clips != 38 ||
            audio_outlasts_graph != expected_audio_tails ||
            !std::filesystem::is_regular_file(root / "WAV/HOST/OUCH.WAV") ||
            !std::filesystem::is_regular_file(root / "DAT/HOST/X213.MMS") ||
            !std::filesystem::is_empty(root / "DAT/HOST/X312.MMS")) {
            std::cerr << "FAIL host reaction active/sentinel inventory\n";
            return false;
        }
        return true;
    };
    if (!validate_host_reactions(assets)) {
        return 1;
    }
    std::cout << "OK DAT/HOST score-reactions=38 groups=15,12,11 "
                 "hit-response=OUCH graph-master-WAV-tails=X14,X18,X211,X212 "
                 "inactive-script-sentinels=X213,X312\n";
    if (!std::filesystem::is_regular_file(
            assets / "MOV/HOST/X11DB.AVI"
        ) || std::filesystem::exists(assets / "DAT/HOST/X11DB.MMS") ||
        std::filesystem::exists(assets / "WAV/HOST/X11DB.WAV")) {
        std::cerr << "FAIL installed inactive host artifact X11DB inventory\n";
        return 1;
    }
    std::cout << "OK MOV/HOST installed-inactive=X11DB movie-only\n";
    const std::array<std::filesystem::path, 3> samples = {
        "MOV/BRAIN/B101.AVI",  // Cinepak
        "MOV/HOST/C11.AVI",    // 8-bit RLE
        "MOV/NAV/T1.AVI",      // Microsoft Video 1
    };
    for (const auto& relative : samples) {
        metaverse::VideoFrame frame;
        std::string error;
        const auto path = assets / relative;
        if (!metaverse::DecodeFirstVideoFrame(path, frame, error)) {
            std::cerr << "FAIL " << relative.string() << ": " << error << '\n';
            return 1;
        }
        std::cout << "OK " << relative.string() << " codec=" << frame.codec << ' '
                  << frame.width << 'x' << frame.height << " stride=" << frame.stride
                  << '\n';
    }

    metaverse::VideoDecoder streaming_decoder;
    std::string streaming_error;
    if (!streaming_decoder.Open(assets / samples[0], streaming_error)) {
        std::cerr << "FAIL streaming open: " << streaming_error << '\n';
        return 1;
    }
    double previous_timestamp = -1.0;
    for (int index = 0; index < 8; ++index) {
        metaverse::VideoFrame frame;
        bool ended = false;
        if (!streaming_decoder.DecodeNext(frame, ended, streaming_error) || ended ||
            frame.timestamp_seconds < previous_timestamp || frame.duration_seconds <= 0.0) {
            std::cerr << "FAIL streaming frame " << index << ": " << streaming_error << '\n';
            return 1;
        }
        previous_timestamp = frame.timestamp_seconds;
    }
    if (!streaming_decoder.Restart(streaming_error)) {
        std::cerr << "FAIL streaming restart: " << streaming_error << '\n';
        return 1;
    }
    metaverse::VideoFrame sought_frame;
    bool seek_ended = false;
    if (!streaming_decoder.SeekFrame(20, sought_frame, seek_ended, streaming_error) ||
        seek_ended || sought_frame.frame_index != 20) {
        std::cerr << "FAIL streaming seek frame 20: " << streaming_error
                  << " got=" << sought_frame.frame_index << '\n';
        return 1;
    }
    if (streaming_decoder.FrameForAudioSamples(0, 11025) != 0 ||
        streaming_decoder.FrameForAudioSamples(11024, 11025) != 9 ||
        streaming_decoder.FrameForAudioSamples(11025, 11025) != 10) {
        std::cerr << "FAIL streaming decoder embedded-audio frame clock\n";
        return 1;
    }
    std::cout << "OK streaming decoder frames=8 restart=ok seek=20\n";

    metaverse::VideoDecoder looks_decoder;
    if (!looks_decoder.Open(assets / "MOV/LOOK/ROTATE.AVI", streaming_error)) {
        std::cerr << "FAIL Looks ROTATE.AVI open: " << streaming_error << '\n';
        return 1;
    }
    std::size_t looks_rotation_keyframes = 0;
    for (std::int32_t contestant = 1; contestant <= 10; ++contestant) {
        const auto forward = metaverse::LooksRotationFrames(contestant, true);
        const auto reverse = metaverse::LooksRotationFrames(contestant, false);
        const std::array<std::int64_t, 4> frames = {
            forward.first_frame,
            forward.last_frame,
            reverse.first_frame,
            reverse.last_frame,
        };
        for (const std::int64_t requested : frames) {
            metaverse::VideoFrame frame;
            bool ended = false;
            if (!looks_decoder.SeekFrame(
                    requested, frame, ended, streaming_error
                ) || ended || frame.frame_index != requested ||
                frame.width != 200 || frame.height != 320) {
                std::cerr << "FAIL Looks ROTATE.AVI frame " << requested
                          << ": " << streaming_error << " got="
                          << frame.frame_index << '\n';
                return 1;
            }
            ++looks_rotation_keyframes;
        }
    }

    const auto magnifier_directory = assets / "BMP/LOOK/MAG";
    std::size_t magnifier_bitmaps = 0;
    for (const auto& entry : std::filesystem::directory_iterator(
             magnifier_directory
         )) {
        if (!entry.is_regular_file() || entry.path().extension() != ".BMP") {
            continue;
        }
        ++magnifier_bitmaps;
        if (entry.file_size() != 67'434) {
            std::cerr << "FAIL Looks magnifier dimensions/size: "
                      << entry.path().filename().string() << " bytes="
                      << entry.file_size() << '\n';
            return 1;
        }
    }
    for (std::int32_t contestant = 1; contestant <= 10; ++contestant) {
        for (std::int32_t region = 1; region <= 4; ++region) {
            const auto normal = metaverse::LooksMagnifierFilename(
                contestant, region, 1, false
            );
            if (!std::filesystem::is_regular_file(magnifier_directory / normal)) {
                std::cerr << "FAIL Looks magnifier base image: "
                          << std::filesystem::path(normal).string() << '\n';
                return 1;
            }
        }
        const std::int32_t special_region =
            metaverse::LooksJudgeCadetSpecialRegion(contestant);
        const auto special = metaverse::LooksMagnifierFilename(
            contestant, special_region, 1, true
        );
        if (!std::filesystem::is_regular_file(magnifier_directory / special)) {
            std::cerr << "FAIL Looks judge-cadet image: "
                      << std::filesystem::path(special).string() << '\n';
            return 1;
        }
    }
    if (magnifier_bitmaps != 132) {
        std::cerr << "FAIL Looks magnifier count=" << magnifier_bitmaps << '\n';
        return 1;
    }
    const auto looks_initial_gauge = assets / "BMP/LOOK/1.BMP";
    const auto looks_initial_magnifier = magnifier_directory / "111.BMP";
    if (!std::filesystem::is_regular_file(looks_initial_gauge) ||
        std::filesystem::file_size(looks_initial_gauge) != 19'294 ||
        !std::filesystem::is_regular_file(looks_initial_magnifier) ||
        std::filesystem::file_size(looks_initial_magnifier) != 67'434) {
        std::cerr << "FAIL Looks hidden construction preload assets\n";
        return 1;
    }
    std::cout << "OK Looks rotation-keyframes=" << looks_rotation_keyframes
              << " magnifier-bitmaps=" << magnifier_bitmaps
              << " base-regions=40 judge-cadet-images=10"
              << " hidden-preload=MAG111+LOOK1\n";

    metaverse::AudioTrack intro_audio;
    bool has_audio = false;
    if (!metaverse::DecodeAudioTrack(
            assets / "MOV/INTRO/MMINTRO.AVI", intro_audio, has_audio, streaming_error
        ) ||
        !has_audio || intro_audio.sample_rate != 11025 || intro_audio.channels != 1 ||
        intro_audio.samples.size() < 1'000'000) {
        std::cerr << "FAIL intro audio decode: " << streaming_error << '\n';
        return 1;
    }
    VideoTimingSummary intro_timing;
    if (!SummarizeVideoTiming(
            assets / "MOV/INTRO/MMINTRO.AVI",
            intro_timing,
            streaming_error
        ) || intro_timing.width != 320 || intro_timing.height != 240 ||
        intro_timing.end_seconds < AudioDurationSeconds(intro_audio) ||
        intro_timing.end_seconds - AudioDurationSeconds(intro_audio) > 0.101) {
        std::cerr << "FAIL intro video timeline: " << streaming_error << '\n';
        return 1;
    }
    std::cout << "OK centered one-shot intro frames="
              << intro_timing.frame_count << " video-seconds="
              << intro_timing.end_seconds << " audio-seconds="
              << AudioDurationSeconds(intro_audio) << " zoom=200%\n";
    constexpr std::array<std::pair<const char*, bool>, 4>
        primary_navigation_audio{{
            {"MOV/NAV/BRAINNAV.AVI", false},
            {"MOV/NAV/HALL.AVI", false},
            {"MOV/NAV/LKSPVL.AVI", false},
            {"MOV/NAV/XRDS1F.AVI", true},
        }};
    for (const auto& [relative, expected_audio] : primary_navigation_audio) {
        metaverse::AudioTrack navigation_audio;
        streaming_error.clear();
        if (!metaverse::DecodeAudioTrack(
                assets / relative,
                navigation_audio,
                has_audio,
                streaming_error
            ) || has_audio != expected_audio ||
            (has_audio && (navigation_audio.sample_rate != 11025 ||
                           navigation_audio.channels != 1 ||
                           navigation_audio.samples.empty()))) {
            std::cerr << "FAIL navigation background audio " << relative
                      << " expected=" << expected_audio << " actual="
                      << has_audio << ": " << streaming_error << '\n';
            return 1;
        }
    }
    metaverse::AudioTrack cryo_info_audio;
    if (!metaverse::DecodeAudioTrack(
            assets / "MOV/CRYO/INFO.AVI",
            cryo_info_audio,
            has_audio,
            streaming_error
        ) || !has_audio || cryo_info_audio.sample_rate != 11025 ||
        cryo_info_audio.channels != 1) {
        std::cerr << "FAIL Cryo INFO.AVI audio decode: " << streaming_error
                  << '\n';
        return 1;
    }
    metaverse::VideoDecoder cryo_info_decoder;
    if (!cryo_info_decoder.Open(
            assets / "MOV/CRYO/INFO.AVI", streaming_error
        )) {
        std::cerr << "FAIL Cryo INFO.AVI video open: " << streaming_error
                  << '\n';
        return 1;
    }
    metaverse::VideoDecoder cryo_rotation_decoder;
    if (!cryo_rotation_decoder.Open(
            assets / "MOV/CRYO/ROTATE.AVI", streaming_error
        )) {
        std::cerr << "FAIL Cryo ROTATE.AVI video open: " << streaming_error
                  << '\n';
        return 1;
    }
    std::size_t cryo_rotation_trigger_count = 0;
    for (std::int32_t contestant_id = 1; contestant_id <= 10;
         ++contestant_id) {
        const std::int64_t base =
            static_cast<std::int64_t>(contestant_id - 1) * 40;
        for (std::int64_t quarter = 0; quarter < 40; quarter += 10) {
            const auto segment = metaverse::NextCryoRotationSegment(
                contestant_id, base + quarter
            );
            for (const std::int64_t requested : {
                     segment.first_frame, segment.last_frame
                 }) {
                metaverse::VideoFrame trigger_frame;
                bool trigger_ended = false;
                streaming_error.clear();
                if (!cryo_rotation_decoder.SeekFrame(
                        requested, trigger_frame, trigger_ended,
                        streaming_error
                    ) || trigger_ended ||
                    trigger_frame.frame_index != requested) {
                    std::cerr << "FAIL Cryo ROTATE.AVI trigger frame "
                              << requested << ": " << streaming_error << '\n';
                    return 1;
                }
                ++cryo_rotation_trigger_count;
            }
        }
    }
    std::size_t cryo_info_trigger_count = 0;
    for (const auto& contestant : metaverse::kCryoInfoRanges) {
        for (const auto range : contestant) {
            for (const std::int64_t requested : {
                     range.first_frame, range.last_frame
                 }) {
                metaverse::VideoFrame trigger_frame;
                bool trigger_ended = false;
                streaming_error.clear();
                if (!cryo_info_decoder.SeekFrame(
                        requested, trigger_frame, trigger_ended,
                        streaming_error
                    ) || trigger_ended ||
                    trigger_frame.frame_index != requested) {
                    std::cerr << "FAIL Cryo INFO.AVI trigger frame "
                              << requested << ": " << streaming_error << '\n';
                    return 1;
                }
                ++cryo_info_trigger_count;
            }
        }
    }
    for (std::int32_t detail = 0; detail <= 5; ++detail) {
        metaverse::AudioTrack prompt;
        if (!metaverse::DecodeAudioTrack(
                assets / "WAV" / ("TT" + std::to_string(detail) + ".WAV"),
                prompt,
                has_audio,
                streaming_error
            ) || !has_audio || prompt.sample_rate != 11025 ||
            prompt.channels != 1 || prompt.samples.empty()) {
            std::cerr << "FAIL Cryo TT" << detail << ".WAV prompt: "
                      << streaming_error << '\n';
            return 1;
        }
    }
    for (const auto& bitmap : metaverse::kCryoStaticBitmapLoadOrder) {
        int width = 0;
        int height = 0;
        const auto path = assets / "BMP/CRYO" /
            std::filesystem::path(bitmap.filename);
        if (!ReadBitmapDimensions(path, width, height) ||
            width != bitmap.width || height != bitmap.height) {
            std::cerr << "FAIL Cryo static bitmap " << path.string()
                      << " dimensions=" << width << 'x' << height << '\n';
            return 1;
        }
    }
    for (const std::wstring_view portrait :
         metaverse::kCryoPortraitFiles) {
        int width = 0;
        int height = 0;
        const std::filesystem::path path =
            assets / "BMP/CRYO" / std::filesystem::path(portrait);
        if (!ReadBitmapDimensions(path, width, height) ||
            width != 60 || height != 60) {
            std::cerr << "FAIL Cryo sponsorship portrait "
                      << path.string() << " dimensions="
                      << width << 'x' << height << '\n';
            return 1;
        }
    }
    std::cout << "OK AVI audio rate=" << intro_audio.sample_rate
              << " channels=" << intro_audio.channels
              << " samples=" << intro_audio.samples.size() << '\n';
    std::cout << "OK DAT/NAV primary background embedded-audio=XRDS1F-only"
                 " range-gated\n";
    std::cout << "OK Cryo detail prompts=TT0-TT5 INFO.AVI-audio=11025x1"
              << " authored-trigger-frames=" << cryo_info_trigger_count
              << '\n';
    std::cout << "OK Cryo ROTATE.AVI quarter-turn-trigger-frames="
              << cryo_rotation_trigger_count << '\n';
    std::cout << "OK Cryo static-bitmaps=15 exact-dimensions"
                 " control-highlights=5 detail-highlights=6"
                 " sponsorship-portraits=10x60x60\n";

    constexpr std::array<const char*, 3> order_static_bitmaps{{
        "ORDER.BMP", "ACCEPT.BMP", "HAND.BMP",
    }};
    for (const char* bitmap : order_static_bitmaps) {
        if (!std::filesystem::is_regular_file(
                assets / "BMP/ORDER" / bitmap
            )) {
            std::cerr << "FAIL ORDER bitmap " << bitmap << '\n';
            return 1;
        }
    }
    for (std::int32_t rank = 0; rank <= 10; ++rank) {
        const auto bitmap = "TH" + std::to_string(rank) + ".BMP";
        if (!std::filesystem::is_regular_file(
                assets / "BMP/ORDER" / bitmap
            )) {
            std::cerr << "FAIL ORDER thermometer " << bitmap << '\n';
            return 1;
        }
    }
    for (std::int32_t comment = 0; comment < 10; ++comment) {
        const auto bitmap = std::to_string(comment) + ".BMP";
        if (!std::filesystem::is_regular_file(
                assets / "BMP/ORDER/COMMENT" / bitmap
            )) {
            std::cerr << "FAIL ORDER phrase " << bitmap << '\n';
            return 1;
        }
    }
    std::cout << "OK ORDER phrases=10 thermometers=11 ACCEPT=release HAND=exact\n";

    constexpr std::array<const char*, 3> brain_controls{{
        "ACCEPT.BMP", "GONG.BMP", "PENALTY.BMP",
    }};
    for (const char* bitmap : brain_controls) {
        if (!std::filesystem::is_regular_file(
                assets / "BMP/BRAIN" / bitmap
            )) {
            std::cerr << "FAIL Brains control " << bitmap << '\n';
            return 1;
        }
    }
    constexpr std::array<const char*, 3> looks_controls{{
        "ACCEPT.BMP", "PENALTY.BMP", "ROTATE.BMP",
    }};
    for (const char* bitmap : looks_controls) {
        if (!std::filesystem::is_regular_file(
                assets / "BMP/LOOK" / bitmap
            )) {
            std::cerr << "FAIL Looks control " << bitmap << '\n';
            return 1;
        }
    }
    std::cout << "OK judging controls Brains=3 shared-wide-release "
                 "Looks=3 coded-same-target-release\n";
    std::string judging_outcome_error;
    if (!AuditJudgingOutcomeAssets(
            assets,
            {
                {"MOV/BRAIN", "BP", 320, 240},
                {"MOV/LOOK", "LP", 200, 320},
            },
            judging_outcome_error
        )) {
        std::cerr << "FAIL Disk I Gong/Penalty outcome assets: "
                  << judging_outcome_error
                  << '\n';
        return 1;
    }
    std::cout << "OK judging outcomes Disk-I Gong=10 Penalty=20 responses\n";

    constexpr std::array<const char*, 8> weight_static_bitmaps{{
        "WEIGHT.BMP", "OK.BMP", "DOWN1.BMP", "UP1.BMP",
        "DOWN2.BMP", "UP2.BMP", "DOWN3.BMP", "UP3.BMP",
    }};
    for (const char* bitmap : weight_static_bitmaps) {
        if (!std::filesystem::is_regular_file(
                assets / "BMP/WEIGHT" / bitmap
            )) {
            std::cerr << "FAIL Weight bitmap " << bitmap << '\n';
            return 1;
        }
    }
    std::size_t weight_gauges = 0;
    for (std::int32_t category = 1; category <= 3; ++category) {
        for (std::int32_t value = 1; value <= 10; ++value) {
            const auto bitmap = "TH" + std::to_string(category) + "_" +
                std::to_string(value) + ".BMP";
            if (!std::filesystem::is_regular_file(
                    assets / "BMP/WEIGHT" / bitmap
                )) {
                std::cerr << "FAIL Weight gauge " << bitmap << '\n';
                return 1;
            }
            ++weight_gauges;
        }
    }
    for (std::int32_t value = 6; value <= 10; ++value) {
        metaverse::AudioTrack prompt;
        if (!metaverse::DecodeAudioTrack(
                assets / "WAV" / ("TT" + std::to_string(value) + ".WAV"),
                prompt,
                has_audio,
                streaming_error
            ) || !has_audio || prompt.sample_rate != 11025 ||
            prompt.channels != 1 || prompt.samples.empty()) {
            std::cerr << "FAIL Weight TT" << value << ".WAV prompt: "
                      << streaming_error << '\n';
            return 1;
        }
    }
    std::cout << "OK Weight gauges=" << weight_gauges
              << " controls=6 C91=blank-then-5/5/5 "
                 "OK=surviving-press-release TT0-TT10\n";

    constexpr std::array<std::int32_t, 3> expected_slot_widths{{88, 82, 88}};
    constexpr std::array<std::int32_t, 3> expected_slot_heights{{84, 84, 86}};
    for (std::int32_t symbol = 0; symbol < 5; ++symbol) {
        for (std::size_t reel = 0; reel < expected_slot_widths.size(); ++reel) {
            const auto bitmap = assets / "BMP/SLOT" /
                ("S" + std::to_string(symbol) + std::to_string(reel) + ".BMP");
            std::int32_t width = 0;
            std::int32_t height = 0;
            if (!ReadBitmapDimensions(bitmap, width, height) ||
                width != expected_slot_widths[reel] ||
                (height < 0 ? -height : height) != expected_slot_heights[reel]) {
                std::cerr << "FAIL Slots bitmap dimensions: "
                          << bitmap.string() << '\n';
                return 1;
            }
        }
    }
    constexpr std::array<const char*, 4> slot_sounds{{
        "SLOTSPIN.WAV", "SLOTLOSE.WAV", "WINDING.WAV", "WINBIG.WAV",
    }};
    for (const char* filename : slot_sounds) {
        metaverse::AudioTrack sound;
        if (!metaverse::DecodeAudioTrack(
                assets / "WAV/NAV" / filename,
                sound,
                has_audio,
                streaming_error
            ) || !has_audio || sound.channels != 1 || sound.samples.empty()) {
            std::cerr << "FAIL Slots " << filename << ": "
                      << streaming_error << '\n';
            return 1;
        }
    }
    metaverse::AudioTrack navigation_departure_sound;
    if (!metaverse::DecodeAudioTrack(
            assets / "WAV/NAV/ZPR.WAV",
            navigation_departure_sound,
            has_audio,
            streaming_error
        ) || !has_audio || navigation_departure_sound.sample_rate != 11025 ||
        navigation_departure_sound.channels != 1 ||
        navigation_departure_sound.samples.size() != 8448) {
        std::cerr << "FAIL navigation departure ZPR.WAV: "
                  << streaming_error << '\n';
        return 1;
    }
    metaverse::AudioTrack navigation_ricochet_sound;
    if (!metaverse::DecodeAudioTrack(
            assets / "WAV/NAV/RICOCHET.WAV",
            navigation_ricochet_sound,
            has_audio,
            streaming_error
        ) || !has_audio || navigation_ricochet_sound.sample_rate != 11025 ||
        navigation_ricochet_sound.channels != 1 ||
        navigation_ricochet_sound.samples.size() != 17406) {
        std::cerr << "FAIL navigation miss RICOCHET.WAV: "
                  << streaming_error << '\n';
        return 1;
    }
    if (metaverse::kSlotSpinDuration != std::chrono::milliseconds(4500) ||
        metaverse::kSlotSymbolX != std::array<std::int32_t, 3>{{190, 290, 388}} ||
        metaverse::kSlotSymbolY != 320 || metaverse::kSlotCreditsX != 40 ||
        metaverse::kSlotCreditsY != 20 || metaverse::kSlotWinAmountX != 450 ||
        metaverse::kSlotLossAmountX != 500 || metaverse::kSlotNoCreditX != 350) {
        std::cerr << "FAIL Slots recovered timing/layout constants\n";
        return 1;
    }
    std::cout << "OK Slots bitmaps=15 sizes=88x84,82x84,88x86"
                 " positions=190,290,388@320 dwell-ms=4500 sounds=4"
                 " navigation-ZPR=8448 ricochet=17406@11025x1\n";

    std::vector<metaverse::NavigationEntry> navigation;
    std::string error;
    if (!metaverse::LoadNavigationData(assets / "DAT/NAV/NAVIGATE.DAT", navigation, error)) {
        std::cerr << "FAIL DAT/NAV/NAVIGATE.DAT: " << error << '\n';
        return 1;
    }
    if (navigation.size() != 31 || navigation.front().scene != "null" ||
        navigation.back().scene != "xr2" || navigation.back().variant != 8) {
        std::cerr << "FAIL DAT/NAV/NAVIGATE.DAT: unexpected table contents\n";
        return 1;
    }
    std::cout << "OK DAT/NAV/NAVIGATE.DAT entries=" << navigation.size() << '\n';

    struct SceneExpectation {
        const char* filename;
        std::size_t objects;
        std::size_t special_indices;
    };
    constexpr std::array<SceneExpectation, 4> scene_expectations = {{
        {"BRAINS.DAT", 21, 3},
        {"LOOKS.DAT", 16, 2},
        {"HALL.DAT", 16, 4},
        {"XR1.DAT", 27, 8},
    }};
    std::size_t slot_nodes = 0;
    bool primary_slot_exact = false;
    bool secondary_slot_exact = false;
    bool tally_action_exact = false;
    std::size_t navigation_arrival_cues = 0;
    std::size_t navigation_silent_asset_sentinels = 0;
    std::array<std::size_t, 16> navigation_flag_counts{};
    std::uint32_t navigation_flag_union = 0;
    std::vector<std::int32_t> fade_targets;
    std::vector<std::array<std::int32_t, 3>> navigation_point_nodes;
    std::vector<std::array<std::int32_t, 3>> secondary_navigation_point_nodes;
    std::vector<std::pair<std::string, std::int32_t>> one_shot_nodes;
    std::vector<std::pair<std::string, std::int32_t>> frame_25_nodes;
    std::vector<std::array<std::int32_t, 3>> xr1_idle_animation_ranges;
    std::vector<std::pair<
        std::string, std::array<std::int32_t, 2>
    >> navigation_action_nodes;
    std::size_t navigation_descending_ranges = 0;
    std::size_t navigation_automatic_nodes = 0;
    std::size_t navigation_decoded_endpoints = 0;
    std::size_t navigation_eof_boundaries = 0;
    for (const auto& expectation : scene_expectations) {
        metaverse::SceneDefinition scene;
        const auto relative = std::filesystem::path("DAT/NAV") / expectation.filename;
        if (!metaverse::LoadSceneData(assets / relative, scene, error)) {
            std::cerr << "FAIL " << relative.string() << ": " << error << '\n';
            return 1;
        }
        if (scene.objects.size() != expectation.objects ||
            scene.special_object_indices.size() != expectation.special_indices) {
            std::cerr << "FAIL " << relative.string() << ": unexpected record counts\n";
            return 1;
        }
        if (!AuditNavigationRangeEndpoints(
                assets / scene.background_video,
                scene,
                navigation_decoded_endpoints,
                navigation_eof_boundaries,
                error
            )) {
            std::cerr << "FAIL " << relative.string()
                      << " range endpoints: " << error << '\n';
            return 1;
        }
        for (std::size_t variant = 1; variant <= scene.special_object_indices.size(); ++variant) {
            metaverse::SceneNavigator navigator;
            if (!navigator.Reset(scene, variant, error)) {
                std::cerr << "FAIL " << relative.string() << " variant " << variant
                          << ": " << error << '\n';
                return 1;
            }
        }
        const auto assets_count = std::count_if(
            scene.objects.begin(), scene.objects.end(),
            [](const metaverse::SceneObject& object) { return object.asset.has_value(); }
        );
        const auto points_count = std::count_if(
            scene.objects.begin(), scene.objects.end(),
            [](const metaverse::SceneObject& object) { return object.point.has_value(); }
        );
        slot_nodes += static_cast<std::size_t>(std::count_if(
            scene.objects.begin(), scene.objects.end(),
            [](const metaverse::SceneObject& object) {
                return (object.flags & 0x4000) != 0;
            }
        ));
        for (const auto& object : scene.objects) {
            for (const auto& range : object.ranges) {
                if (range[0] > range[1]) {
                    ++navigation_descending_ranges;
                }
            }
            if ((object.flags & metaverse::kSceneObjectAutomaticMask) != 0) {
                ++navigation_automatic_nodes;
            }
            navigation_flag_union |= object.flags;
            for (std::size_t bit = 0; bit < navigation_flag_counts.size(); ++bit) {
                if ((object.flags & (1u << bit)) != 0) {
                    ++navigation_flag_counts[bit];
                }
            }
            if ((object.flags & 0x0010) != 0) {
                one_shot_nodes.emplace_back(expectation.filename, object.index);
            }
            if ((object.flags & 0x0020) != 0) {
                frame_25_nodes.emplace_back(expectation.filename, object.index);
            }
            if (std::string(expectation.filename) == "XR1.DAT" &&
                object.index >= 19 && object.index <= 22) {
                xr1_idle_animation_ranges.push_back({
                    object.index,
                    object.ranges[1][0],
                    object.ranges[1][1],
                });
            }
            if ((object.flags & 0x0008) != 0) {
                navigation_action_nodes.push_back({
                    expectation.filename,
                    {object.index, object.auxiliary},
                });
            }
            if ((object.flags & 0x4000) != 0) {
                if (std::string(expectation.filename) != "XR1.DAT" ||
                    primary_slot_exact || object.index != 15 ||
                    object.flags != 0x4007 ||
                    object.ranges[1] != std::array<std::int32_t, 2>{{812, 869}} ||
                    object.links[1] != 15) {
                    std::cerr << "FAIL DAT/NAV: unexpected primary slot node\n";
                    return 1;
                }
                primary_slot_exact = true;
            }
            if (object.asset) {
                if (metaverse::IsPlayableNavigationAsset(*object.asset)) {
                    ++navigation_arrival_cues;
                    const auto cue = assets / std::filesystem::path(*object.asset);
                    if (!std::filesystem::is_regular_file(cue)) {
                        std::cerr << "FAIL " << relative.string()
                                  << ": missing navigation arrival cue "
                                  << cue.string() << '\n';
                        return 1;
                    }
                } else {
                    ++navigation_silent_asset_sentinels;
                }
            }
            if ((object.flags & 0x1000) == 0) {
                continue;
            }
            if ((object.flags & 0x0200) == 0 ||
                metaverse::CrossSceneTransitionForFlags(object.flags) !=
                    metaverse::CrossSceneTransition::fade_to_white) {
                std::cerr << "FAIL " << relative.string()
                          << ": invalid 0x1000 transition node\n";
                return 1;
            }
            fade_targets.push_back(object.auxiliary);
        }
        for (const auto& object : scene.objects) {
            if (!object.point) {
                continue;
            }
            navigation_point_nodes.push_back({
                object.index, (*object.point)[0], (*object.point)[1]
            });
            const auto overlay = assets / "BMP/NAV" /
                ("N" + std::to_string(object.index) + ".BMP");
            if (!std::filesystem::is_regular_file(overlay)) {
                std::cerr << "FAIL " << relative.string()
                          << ": missing positional overlay "
                          << overlay.filename().string() << '\n';
                return 1;
            }
        }
        std::cout << "OK " << relative.string() << " objects=" << scene.objects.size()
                  << " assets=" << assets_count << " points=" << points_count
                  << " specials=" << scene.special_object_indices.size() << '\n';
    }
    if (slot_nodes != 1 || !primary_slot_exact) {
        std::cerr << "FAIL DAT/NAV: unexpected primary 0x4000 slot-machine node\n";
        return 1;
    }
    std::cout << "OK DAT/NAV XR1-slot=node15 up-self frames=812-869\n";
    if (fade_targets.size() != 2) {
        std::cerr << "FAIL DAT/NAV: unexpected primary-tree 0x1000 node count\n";
        return 1;
    }
    const std::vector<std::array<std::int32_t, 3>> expected_point_nodes = {
        {1, 172, 110},
        {2, 186, 34},
        {3, 210, 80},
        {4, 148, 82},
    };
    if (navigation_point_nodes != expected_point_nodes) {
        std::cerr << "FAIL DAT/NAV: unexpected 0x0040 overlay positions\n";
        return 1;
    }
    std::cout << "OK DAT/NAV positional-overlays=N1,N2,N3,N4\n";
    if (navigation_arrival_cues != 23) {
        std::cerr << "FAIL DAT/NAV: unexpected primary arrival-cue count\n";
        return 1;
    }
    std::cout << "OK DAT/NAV primary-arrival-cues="
              << navigation_arrival_cues << '\n';
    const std::vector<std::pair<std::string, std::int32_t>>
        expected_one_shot_nodes = {{"XR1.DAT", 18}};
    const std::vector<std::pair<std::string, std::int32_t>>
        expected_frame_25_nodes = {
            {"XR1.DAT", 1}, {"XR1.DAT", 19}, {"XR1.DAT", 20},
            {"XR1.DAT", 21}, {"XR1.DAT", 22},
        };
    if (one_shot_nodes != expected_one_shot_nodes ||
        frame_25_nodes != expected_frame_25_nodes) {
        std::cerr << "FAIL DAT/NAV: unexpected 0x0010/0x0020 node map\n";
        return 1;
    }
    const std::vector<std::array<std::int32_t, 3>>
        expected_xr1_idle_animation_ranges = {
            {19, 870, 1079},
            {20, 1080, 1303},
            {21, 1304, 1390},
            {22, 1503, 1686},
        };
    if (xr1_idle_animation_ranges != expected_xr1_idle_animation_ranges) {
        std::cerr << "FAIL DAT/NAV: unexpected XR1 delayed-idle ranges\n";
        return 1;
    }
    metaverse::VideoDecoder xr1_decoder;
    metaverse::VideoFrame xr1_hold_frame;
    bool xr1_ended = false;
    if (!xr1_decoder.Open(assets / "MOV/NAV/XRDS1F.AVI", error) ||
        !xr1_decoder.SeekFrame(25, xr1_hold_frame, xr1_ended, error) ||
        xr1_ended || xr1_hold_frame.frame_index != 25) {
        std::cerr << "FAIL DAT/NAV: XR1 frame-25 arrival hold: " << error
                  << '\n';
        return 1;
    }
    std::cout << "OK DAT/NAV one-shot=XR1:18 frame25=XR1:1,19,20,21,22"
                 " idle=19:870-1079,20:1080-1303,21:1304-1390,22:1503-1686"
                 " timer-ms=500 simm-threshold=>1000 idle-threshold=>10000\n";

    std::size_t motion_files = 0;
    std::size_t empty_motion_files = 0;
    std::size_t motion_records = 0;
    std::size_t trajectory_points = 0;
    std::size_t interval_60_records = 0;
    std::size_t interval_200_records = 0;
    std::size_t other_nonzero_interval_records = 0;
    std::map<std::uint16_t, std::size_t> motion_flag_counts;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(assets / "DAT")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".MMS") {
            continue;
        }
        metaverse::MotionScript script;
        if (!metaverse::LoadMotionScript(entry.path(), script, error)) {
            std::cerr << "FAIL " << entry.path().string() << ": " << error << '\n';
            return 1;
        }
        ++motion_files;
        empty_motion_files += script.raw_bytes.empty() ? 1 : 0;
        motion_records += script.records.size();
        for (const auto& record : script.records) {
            ++motion_flag_counts[record.flags];
            trajectory_points += record.trajectory_points.size();
            interval_60_records += record.timer_interval == 60 ? 1 : 0;
            interval_200_records += record.timer_interval == 200 ? 1 : 0;
            other_nonzero_interval_records +=
                record.timer_interval != 0 && record.timer_interval != 60 &&
                record.timer_interval != 200 ? 1 : 0;
        }
    }
    if (motion_files != 51 || empty_motion_files != 1 ||
        interval_60_records != 1 || interval_200_records != 8 ||
        other_nonzero_interval_records != 0 ||
        motion_flag_counts != std::map<std::uint16_t, std::size_t>{
            {0x0053, 3}, {0x0088, 10}, {0x0153, 2}, {0x0180, 4},
            {0x0188, 1}, {0x0248, 9}, {0x0280, 39}, {0x0288, 8},
            {0x0348, 1}, {0x0388, 31},
        }) {
        std::cerr << "FAIL DAT/**/*.MMS: unexpected file counts\n";
        return 1;
    }
    std::cout << "OK DAT/**/*.MMS files=" << motion_files
              << " empty=" << empty_motion_files << " records=" << motion_records
              << " trajectory-points=" << trajectory_points
              << " timers=60x" << interval_60_records
              << ",200x" << interval_200_records
              << " flag-combinations=" << motion_flag_counts.size() << '\n';
    for (std::int32_t simm = 0; simm <= 10; ++simm) {
        metaverse::MotionScript script;
        if (!metaverse::LoadMotionScript(
                assets / "DAT/NAV" / ("D" + std::to_string(simm) + ".MMS"),
                script,
                error
            )) {
            std::cerr << "FAIL D" << simm << " motion load: " << error << '\n';
            return 1;
        }
        metaverse::MotionPlayer motion;
        if (!motion.Reset(script, error, 3)) {
            std::cerr << "FAIL D" << simm << " motion reset: " << error << '\n';
            return 1;
        }
        if (motion.IgnoresMouseInput()) {
            std::cerr << "FAIL D" << simm
                      << " root record unexpectedly suppresses its hit target\n";
            return 1;
        }
        std::size_t ticks = 0;
        while (motion.Active() && ticks++ < 10'000) {
            if (!motion.Tick(error)) {
                std::cerr << "FAIL D" << simm << " motion tick: " << error << '\n';
                return 1;
            }
        }
        if (!motion.Complete() || motion.ResultCode() != 0) {
            std::cerr << "FAIL D" << simm
                      << " escape path does not complete with zero sentinel at "
                      << motion.X() << ',' << motion.Y() << " frame="
                      << motion.Frame() << " result=" << motion.ResultCode()
                      << '\n';
            for (const auto& record : script.records) {
                std::cerr << "  off=" << record.file_offset
                          << " frames=" << record.loop_frame << ".."
                          << record.terminal_frame
                          << " hit=" << record.hit_transition_offset
                          << " timed=" << record.timed_transition_offset
                          << " flags=0x" << std::hex << record.flags << std::dec
                          << " delta=" << record.delta_x << ',' << record.delta_y
                          << " result=" << record.result_code
                          << " initial=" << record.initial_x << ','
                          << record.initial_y << " points="
                          << record.trajectory_points.size() << '\n';
            }
            return 1;
        }
        if (!motion.Reset(script, error, 3)) {
            std::cerr << "FAIL D" << simm << " hit-path reset: " << error << '\n';
            return 1;
        }
        ticks = 0;
        while (motion.Active() && ticks++ < 10'000) {
            if (!motion.IgnoresMouseInput()) {
                motion.Hit(error);
            }
            if (!motion.Tick(error)) {
                std::cerr << "FAIL D" << simm << " hit-path tick: " << error
                          << '\n';
                return 1;
            }
        }
        const std::int32_t expected_success_sentinel = simm == 0 ? 100 : 50;
        if (!motion.Complete() ||
            motion.ResultCode() != expected_success_sentinel) {
            std::cerr << "FAIL D" << simm
                      << " hit-path success sentinel: expected "
                      << expected_success_sentinel << " got "
                      << motion.ResultCode() << '\n';
            return 1;
        }
    }
    std::cout << "OK DAT/NAV D0-D10 terminals escape=0 success-sentinels=100,50"
                 " caller-award=5..15%-of-base\n";

    if (argc == 3) {
        const std::filesystem::path assets2(argv[2]);
        std::vector<metaverse::NavigationEntry> secondary_navigation;
        if (!metaverse::LoadNavigationData(
                assets2 / "DAT/NAV/NAVIGATE.DAT",
                secondary_navigation,
                error
            ) || secondary_navigation.size() != navigation.size()) {
            std::cerr << "FAIL Disk II DAT/NAV/NAVIGATE.DAT: "
                      << (error.empty() ? "entry count mismatch" : error)
                      << '\n';
            return 1;
        }
        for (std::size_t index = 0; index < navigation.size(); ++index) {
            if (secondary_navigation[index].scene != navigation[index].scene ||
                secondary_navigation[index].variant != navigation[index].variant) {
                std::cerr << "FAIL DAT/NAV/NAVIGATE.DAT differs between discs at entry "
                          << index << '\n';
                return 1;
            }
        }

        std::size_t initial_automatic_entries = 0;
        std::size_t initial_cue_entries = 0;
        std::size_t max_scene_objects = 0;
        std::map<std::string, bool> audited_scenes;
        for (std::size_t entry_index = 1;
             entry_index < navigation.size();
             ++entry_index) {
            const auto& entry = navigation[entry_index];
            const bool secondary = entry.scene == "xr2" ||
                                   entry.scene == "talnav" ||
                                   entry.scene == "tally";
            std::string filename = entry.scene;
            std::transform(
                filename.begin(), filename.end(), filename.begin(),
                [](unsigned char value) {
                    return static_cast<char>(std::toupper(value));
                }
            );
            filename += ".DAT";
            metaverse::SceneDefinition entry_scene;
            if (!metaverse::LoadSceneData(
                    (secondary ? assets2 : assets) / "DAT/NAV" / filename,
                    entry_scene,
                    error
                ) || entry_scene.background_sprite != "none") {
                std::cerr << "FAIL navigation loader corpus entry " << entry_index
                          << ": "
                          << (error.empty() ? "unexpected background sprite" : error)
                          << '\n';
                return 1;
            }
            for (std::size_t object_index = 0;
                 object_index < entry_scene.objects.size();
                 ++object_index) {
                if (entry_scene.objects[object_index].index !=
                    static_cast<std::int32_t>(object_index)) {
                    std::cerr << "FAIL " << filename
                              << ": object IDs are not ordinal at "
                              << object_index << '\n';
                    return 1;
                }
            }
            metaverse::SceneNavigator entry_navigator;
            if (!entry_navigator.Reset(
                    entry_scene,
                    static_cast<std::size_t>(entry.variant),
                    error
                )) {
                std::cerr << "FAIL navigation loader corpus entry " << entry_index
                          << ": " << error << '\n';
                return 1;
            }
            const auto* initial = entry_navigator.CurrentObject();
            if (initial == nullptr) {
                std::cerr << "FAIL navigation loader corpus entry " << entry_index
                          << ": missing initial object\n";
                return 1;
            }
            if ((initial->flags & metaverse::kSceneObjectAutomaticMask) != 0) {
                ++initial_automatic_entries;
                if (!metaverse::ShouldAdvanceNavigationOnInitialSceneEntry(
                        initial->flags
                    )) {
                    std::cerr << "FAIL navigation loader corpus entry " << entry_index
                              << ": initial automatic flags are not 0x0100-routed\n";
                    return 1;
                }
            }
            if (metaverse::ShouldPlayNavigationArrivalSound(
                    initial->flags, true
                )) {
                ++initial_cue_entries;
            }
            max_scene_objects = std::max(
                max_scene_objects, entry_scene.objects.size()
            );
            audited_scenes[entry.scene] = true;
        }
        if (audited_scenes.size() != 7 || initial_automatic_entries != 29 ||
            initial_cue_entries != 13 || max_scene_objects != 27) {
            std::cerr << "FAIL navigation loader corpus summary scenes="
                      << audited_scenes.size() << " automatic="
                      << initial_automatic_entries << " cues="
                      << initial_cue_entries << " max-objects="
                      << max_scene_objects << '\n';
            return 1;
        }
        std::cout << "OK DAT/NAV loader entries=30 scenes=7"
                     " initial-0x0100=29 initial-0x0400-cues=13"
                     " background-sprite=none ordinal-ids max-objects=27\n";

        constexpr std::array<std::pair<const char*, bool>, 3>
            secondary_navigation_audio{{
                {"MOV/NAV/TALLY.AVI", false},
                {"MOV/NAV/TLNTMSHP.AVI", false},
                {"MOV/NAV/XRDS2F.AVI", true},
            }};
        for (const auto& [relative, expected_audio] : secondary_navigation_audio) {
            metaverse::AudioTrack navigation_audio;
            streaming_error.clear();
            if (!metaverse::DecodeAudioTrack(
                    assets2 / relative,
                    navigation_audio,
                    has_audio,
                    streaming_error
                ) || has_audio != expected_audio ||
                (has_audio && (navigation_audio.sample_rate != 11025 ||
                               navigation_audio.channels != 1 ||
                               navigation_audio.samples.empty()))) {
                std::cerr << "FAIL Disk II navigation background audio "
                          << relative << " expected=" << expected_audio
                          << " actual=" << has_audio << ": "
                          << streaming_error << '\n';
                return 1;
            }
        }
        std::cout << "OK DAT/NAV secondary background embedded-audio=XRDS2F-only"
                     " range-gated\n";
        metaverse::AudioTrack secondary_navigation_departure_sound;
        if (!metaverse::DecodeAudioTrack(
                assets2 / "WAV/NAV/ZPR.WAV",
                secondary_navigation_departure_sound,
                has_audio,
                streaming_error
            ) || !has_audio ||
            secondary_navigation_departure_sound.sample_rate !=
                navigation_departure_sound.sample_rate ||
            secondary_navigation_departure_sound.channels !=
                navigation_departure_sound.channels ||
            secondary_navigation_departure_sound.samples !=
                navigation_departure_sound.samples) {
            std::cerr << "FAIL Disk II navigation ZPR.WAV parity: "
                      << streaming_error << '\n';
            return 1;
        }
        metaverse::AudioTrack secondary_navigation_ricochet_sound;
        if (!metaverse::DecodeAudioTrack(
                assets2 / "WAV/NAV/RICOCHET.WAV",
                secondary_navigation_ricochet_sound,
                has_audio,
                streaming_error
            ) || !has_audio ||
            secondary_navigation_ricochet_sound.sample_rate !=
                navigation_ricochet_sound.sample_rate ||
            secondary_navigation_ricochet_sound.channels !=
                navigation_ricochet_sound.channels ||
            secondary_navigation_ricochet_sound.samples !=
                navigation_ricochet_sound.samples) {
            std::cerr << "FAIL Disk II navigation RICOCHET.WAV parity: "
                      << streaming_error << '\n';
            return 1;
        }
        constexpr std::array<const char*, 3> talent_controls{{
            "ACCEPT.BMP", "GONG.BMP", "PENALTY.BMP",
        }};
        for (const char* bitmap : talent_controls) {
            if (!std::filesystem::is_regular_file(
                    assets2 / "BMP/TALENT" / bitmap
                )) {
                std::cerr << "FAIL Talent control " << bitmap << '\n';
                return 1;
            }
        }
        std::cout << "OK judging controls Talent=3 shared-wide-release\n";
        if (!AuditJudgingOutcomeAssets(
                assets2,
                {{"MOV/TALENT", "TP", 320, 240}},
                judging_outcome_error
            )) {
            std::cerr << "FAIL Disk II Gong/Penalty outcome assets: "
                      << judging_outcome_error
                      << '\n';
            return 1;
        }
        std::cout << "OK judging outcomes Disk-II Gong=10 Penalty=10 responses\n";
        metaverse::HostPointTable secondary_host_points{};
        if (!metaverse::LoadHostPointData(
                assets2 / "DAT/HOST/HPNT.DAT",
                secondary_host_points,
                host_error
            ) || secondary_host_points != expected_host_points ||
            !std::filesystem::is_regular_file(
                assets2 / "MOV/HOST/C21.AVI"
            ) || !std::filesystem::is_regular_file(
                assets2 / "WAV/HOST/C21.WAV"
            )) {
            std::cerr << "FAIL Disk II host cue/position table: "
                      << host_error << '\n';
            return 1;
        }
        for (const char* stem : {"C61", "C71"}) {
            const std::string movie = std::string("MOV/HOST/") + stem + ".AVI";
            const std::string sound = std::string("WAV/HOST/") + stem + ".WAV";
            if (!std::filesystem::is_regular_file(assets / movie) ||
                std::filesystem::exists(assets2 / movie) ||
                !std::filesystem::is_regular_file(assets / sound) ||
                !std::filesystem::is_regular_file(assets2 / sound) ||
                !FilesHaveSameContents(assets / sound, assets2 / sound)) {
                std::cerr << "FAIL shared " << stem
                          << " host cue root ownership\n";
                return 1;
            }
        }
        if (!FilesHaveSameContents(
                assets / "MOV/HOST/C21.AVI",
                assets2 / "MOV/HOST/C21.AVI"
            ) || FilesHaveSameContents(
                assets / "WAV/HOST/C21.WAV",
                assets2 / "WAV/HOST/C21.WAV"
            )) {
            std::cerr << "FAIL Disk II-specific C21 audio ownership\n";
            return 1;
        }
        std::cout << "OK host roots C21=Disk-II-audio "
                     "C61/C71=Disk-I-movie+identical-WAV\n";
        if (!validate_host_reactions(assets2)) {
            return 1;
        }
        const std::array<std::filesystem::path, 3> disk2_samples = {
            "MOV/TALENT/T11.AVI",
            "MOV/TALLY/W1.AVI",
            "MOV/CREDIT/CREDIT.AVI",
        };
        for (const auto& relative : disk2_samples) {
            metaverse::VideoFrame frame;
            if (!metaverse::DecodeFirstVideoFrame(
                    assets2 / relative, frame, error
                )) {
                std::cerr << "FAIL Disk II " << relative.string()
                          << ": " << error << '\n';
                return 1;
            }
            std::cout << "OK Disk II " << relative.string() << " codec="
                      << frame.codec << ' ' << frame.width << 'x' << frame.height
                      << '\n';
        }

        const std::array<std::pair<std::filesystem::path, std::size_t>, 3>
            disk2_scenes = {{
                {"DAT/NAV/TALNAV.DAT", 3},
                {"DAT/NAV/TALLY.DAT", 2},
                {"DAT/NAV/XR2.DAT", 8},
            }};
        for (const auto& [relative, variants] : disk2_scenes) {
            metaverse::SceneDefinition scene;
            if (!metaverse::LoadSceneData(assets2 / relative, scene, error) ||
                scene.special_object_indices.size() != variants) {
                std::cerr << "FAIL Disk II " << relative.string() << ": "
                          << (error.empty() ? "unexpected variant count" : error)
                          << '\n';
                return 1;
            }
            if (!AuditNavigationRangeEndpoints(
                    assets2 / scene.background_video,
                    scene,
                    navigation_decoded_endpoints,
                    navigation_eof_boundaries,
                    error
                )) {
                std::cerr << "FAIL Disk II " << relative.string()
                          << " range endpoints: " << error << '\n';
                return 1;
            }
            std::cout << "OK Disk II " << relative.string() << " objects="
                      << scene.objects.size() << " variants=" << variants << '\n';
            for (const auto& object : scene.objects) {
                for (const auto& range : object.ranges) {
                    if (range[0] > range[1]) {
                        ++navigation_descending_ranges;
                    }
                }
                if ((object.flags & metaverse::kSceneObjectAutomaticMask) != 0) {
                    ++navigation_automatic_nodes;
                }
                navigation_flag_union |= object.flags;
                for (std::size_t bit = 0; bit < navigation_flag_counts.size(); ++bit) {
                    if ((object.flags & (1u << bit)) != 0) {
                        ++navigation_flag_counts[bit];
                    }
                }
                if ((object.flags & 0x0010) != 0) {
                    one_shot_nodes.emplace_back(
                        relative.filename().string(), object.index
                    );
                }
                if ((object.flags & 0x0020) != 0) {
                    frame_25_nodes.emplace_back(
                        relative.filename().string(), object.index
                    );
                }
                if ((object.flags & 0x0008) != 0) {
                    navigation_action_nodes.push_back({
                        relative.filename().string(),
                        {object.index, object.auxiliary},
                    });
                }
                if (object.asset) {
                    if (metaverse::IsPlayableNavigationAsset(*object.asset)) {
                        ++navigation_arrival_cues;
                        const auto cue = assets2 / std::filesystem::path(*object.asset);
                        if (!std::filesystem::is_regular_file(cue)) {
                            std::cerr << "FAIL Disk II " << relative.string()
                                      << ": missing navigation arrival cue "
                                      << cue.string() << '\n';
                            return 1;
                        }
                    } else {
                        ++navigation_silent_asset_sentinels;
                    }
                }
                if ((object.flags & 0x4000) != 0) {
                    ++slot_nodes;
                    if (relative.filename() != "XR2.DAT" ||
                        secondary_slot_exact || object.index != 15 ||
                        object.flags != 0x4007 ||
                        object.ranges[1] !=
                            std::array<std::int32_t, 2>{{821, 878}} ||
                        object.links[1] != 15) {
                        std::cerr << "FAIL DAT/NAV: unexpected secondary slot node\n";
                        return 1;
                    }
                    secondary_slot_exact = true;
                }
                if (relative.filename() == "TALLY.DAT" &&
                    (object.flags & 0x0008) != 0) {
                    if (tally_action_exact || object.index != 3 ||
                        object.flags != 0x0008 || object.auxiliary != 5) {
                        std::cerr << "FAIL Disk II TALLY.DAT action-5 node\n";
                        return 1;
                    }
                    tally_action_exact = true;
                }
                if (object.point) {
                    secondary_navigation_point_nodes.push_back({
                        object.index, (*object.point)[0], (*object.point)[1]
                    });
                    const auto overlay = assets2 / "BMP/NAV" /
                        ("N" + std::to_string(object.index) + ".BMP");
                    if (!std::filesystem::is_regular_file(overlay)) {
                        std::cerr << "FAIL Disk II " << relative.string()
                                  << ": missing positional overlay "
                                  << overlay.filename().string() << '\n';
                        return 1;
                    }
                }
                if ((object.flags & 0x1000) == 0) {
                    continue;
                }
                if ((object.flags & 0x0200) == 0) {
                    std::cerr << "FAIL Disk II " << relative.string()
                              << ": 0x1000 node is not cross-scene\n";
                    return 1;
                }
                fade_targets.push_back(object.auxiliary);
            }
        }
        std::sort(fade_targets.begin(), fade_targets.end());
        if (fade_targets != std::vector<std::int32_t>{7, 9, 10}) {
            std::cerr << "FAIL DAT/NAV: unexpected 0x1000 transition targets\n";
            return 1;
        }
        std::cout << "OK DAT/NAV fade-to-white-targets=7,9,10\n";
        if (one_shot_nodes != expected_one_shot_nodes ||
            frame_25_nodes != expected_frame_25_nodes) {
            std::cerr << "FAIL DAT/NAV: supplemental trees changed the "
                         "0x0010/0x0020 node map\n";
            return 1;
        }
        if (navigation_arrival_cues != 35 ||
            navigation_silent_asset_sentinels != 1) {
            std::cerr << "FAIL DAT/NAV: unexpected bundled arrival-cue count\n";
            return 1;
        }
        std::cout << "OK DAT/NAV bundled-arrival-cues="
                  << navigation_arrival_cues << " silent-sentinels="
                  << navigation_silent_asset_sentinels << '\n';
        if (navigation_descending_ranges != 0) {
            std::cerr << "FAIL DAT/NAV: descending MCI range count="
                      << navigation_descending_ranges << '\n';
            return 1;
        }
        std::cout << "OK DAT/NAV all scene ranges=ascending"
                     " reverse-MCI-branch=dormant\n";
        if (navigation_decoded_endpoints != 306 ||
            navigation_eof_boundaries != 1) {
            std::cerr << "FAIL DAT/NAV endpoint audit decoded="
                      << navigation_decoded_endpoints << " eof-boundaries="
                      << navigation_eof_boundaries << '\n';
            return 1;
        }
        std::cout << "OK DAT/NAV decoded-range-endpoints="
                  << navigation_decoded_endpoints
                  << " eof-boundaries=1(XR1:1686->1685)\n";
        if (navigation_automatic_nodes != 60 ||
            metaverse::kSceneObjectAutomaticMask != 0x2900) {
            std::cerr << "FAIL DAT/NAV: automatic route mask/count="
                      << navigation_automatic_nodes << '\n';
            return 1;
        }
        std::cout << "OK DAT/NAV automatic-route-mask=0x2900 nodes=60"
                     " online-dialog=bypassed\n";
        if (secondary_navigation_point_nodes != expected_point_nodes) {
            std::cerr << "FAIL DAT/NAV: unexpected secondary 0x0040 overlay positions\n";
            return 1;
        }
        std::cout << "OK DAT/NAV secondary-positional-overlays=N1,N2,N3,N4\n";
        if (slot_nodes != 2 || !secondary_slot_exact) {
            std::cerr << "FAIL DAT/NAV: unexpected bundled slot-node count\n";
            return 1;
        }
        std::cout << "OK DAT/NAV XR2-slot=node15 up-self frames=821-878"
                     " bundled-slot-nodes=" << slot_nodes << '\n';
        if (!tally_action_exact) {
            std::cerr << "FAIL Disk II TALLY.DAT missing exact action-5 node\n";
            return 1;
        }
        std::cout << "OK Disk II TALLY action=node3 auxiliary=5\n";
        const std::vector<std::pair<
            std::string, std::array<std::int32_t, 2>
        >> expected_navigation_action_nodes = {
            {"BRAINS.DAT", {16, 3}},
            {"LOOKS.DAT", {12, 4}},
            {"HALL.DAT", {8, 13}},
            {"HALL.DAT", {9, 14}},
            {"HALL.DAT", {10, 15}},
            {"HALL.DAT", {11, 16}},
            {"XR1.DAT", {11, 6}},
            {"TALNAV.DAT", {14, 2}},
            {"TALLY.DAT", {3, 5}},
            {"XR2.DAT", {11, 6}},
        };
        if (navigation_action_nodes != expected_navigation_action_nodes ||
            metaverse::NavigationSlotShortcutEntry(false) != 29 ||
            metaverse::NavigationSlotShortcutEntry(true) != 30 ||
            metaverse::NavigationContextMenuSuppressed(12) ||
            !metaverse::NavigationContextMenuSuppressed(13) ||
            !metaverse::NavigationContextMenuSuppressed(16) ||
            metaverse::NavigationContextMenuSuppressed(17)) {
            std::cerr << "FAIL DAT/NAV: action/menu command map changed\n";
            return 1;
        }
        std::cout << "OK DAT/NAV actions=2,3,4,5,6,13,14,15,16 "
                     "menu-slots=29/30 hall-menu-suppressed=13-16\n";
        constexpr std::array<std::size_t, 16> expected_flag_counts = {
            51, 51, 53, 10, 1, 5, 8, 13,
            52, 12, 30, 6, 3, 2, 2, 0,
        };
        if (navigation_flag_union != 0x7fff ||
            navigation_flag_counts != expected_flag_counts) {
            std::cerr << "FAIL DAT/NAV: unexpected bundled flag inventory\n";
            return 1;
        }
        std::cout << "OK DAT/NAV flag-union=0x" << std::hex
                  << navigation_flag_union << std::dec << " counts=";
        for (std::size_t bit = 0; bit < navigation_flag_counts.size(); ++bit) {
            if (navigation_flag_counts[bit] != 0) {
                std::cout << " 0x" << std::hex << (1u << bit) << std::dec
                          << ':' << navigation_flag_counts[bit];
            }
        }
        std::cout << '\n';

        std::size_t talent_movies = 0;
        for (const auto& entry : std::filesystem::directory_iterator(
                 assets2 / "MOV/TALENT"
             )) {
            if (entry.is_regular_file() && entry.path().extension() == ".AVI") {
                ++talent_movies;
            }
        }
        std::size_t talent_comments = 0;
        for (const auto& entry : std::filesystem::directory_iterator(
                 assets2 / "WAV/COMMENT"
             )) {
            const std::string name = entry.path().filename().string();
            if (entry.is_regular_file() && name.size() >= 5 && name[0] == 'T' &&
                entry.path().extension() == ".WAV") {
                ++talent_comments;
            }
        }
        if (talent_movies != 70 || talent_comments != 50) {
            std::cerr << "FAIL Disk II media counts: talent-movies="
                      << talent_movies << " talent-comments=" << talent_comments
                      << '\n';
            return 1;
        }
        std::cout << "OK Disk II media talent-movies=" << talent_movies
                  << " talent-comments=" << talent_comments << '\n';

        std::size_t centered_winner_frames = 0;
        double centered_min_video_tail = 1.0;
        double centered_max_video_tail = 0.0;
        for (std::int32_t contestant = 1; contestant <= 10; ++contestant) {
            metaverse::VideoFrame frame;
            const auto winner = assets2 / "MOV/TALLY" /
                ("W" + std::to_string(contestant) + ".AVI");
            if (!metaverse::DecodeFirstVideoFrame(winner, frame, error) ||
                frame.width != 320 || frame.height != 240) {
                std::cerr << "FAIL Disk II tally winner W" << contestant
                          << ".AVI: " << error << '\n';
                return 1;
            }
            metaverse::AudioTrack winner_audio;
            if (!metaverse::DecodeAudioTrack(
                    winner,
                    winner_audio,
                    has_audio,
                    streaming_error
                ) || !has_audio || winner_audio.sample_rate != 11025 ||
                winner_audio.channels != 1 || winner_audio.samples.empty()) {
                std::cerr << "FAIL Disk II tally winner audio W" << contestant
                          << ".AVI: " << streaming_error << '\n';
                return 1;
            }
            VideoTimingSummary winner_timing;
            if (!SummarizeVideoTiming(
                    winner, winner_timing, streaming_error
                ) || winner_timing.width != 320 || winner_timing.height != 240) {
                std::cerr << "FAIL Disk II tally winner timeline W"
                          << contestant << ".AVI: " << streaming_error << '\n';
                return 1;
            }
            centered_winner_frames += winner_timing.frame_count;
            const double winner_video_tail =
                winner_timing.end_seconds - AudioDurationSeconds(winner_audio);
            centered_min_video_tail = std::min(
                centered_min_video_tail, winner_video_tail
            );
            centered_max_video_tail = std::max(
                centered_max_video_tail, winner_video_tail
            );
            if (winner_video_tail < 0.0 || winner_video_tail > 0.101) {
                std::cerr << "FAIL Disk II tally winner stream boundary W"
                          << contestant << ".AVI tail=" << winner_video_tail
                          << '\n';
                return 1;
            }
        }
        for (const char* relative : {
                 "WAV/END2.WAV", "WAV/ANNOUNCE/WRONG4.WAV",
             }) {
            metaverse::AudioTrack cue;
            if (!metaverse::DecodeAudioTrack(
                    assets2 / relative,
                    cue,
                    has_audio,
                    streaming_error
                ) || !has_audio || cue.sample_rate != 11025 ||
                cue.channels != 1 || cue.samples.empty()) {
                std::cerr << "FAIL Disk II tally cue " << relative << ": "
                          << streaming_error << '\n';
                return 1;
            }
        }
        metaverse::AudioTrack credit_audio;
        if (!metaverse::DecodeAudioTrack(
                assets2 / "MOV/CREDIT/CREDIT.AVI",
                credit_audio,
                has_audio,
                streaming_error
            ) || !has_audio || credit_audio.sample_rate != 11025 ||
            credit_audio.channels != 1 || credit_audio.samples.empty()) {
            std::cerr << "FAIL Disk II closing credit audio: "
                      << streaming_error << '\n';
            return 1;
        }
        VideoTimingSummary credit_timing;
        if (!SummarizeVideoTiming(
                assets2 / "MOV/CREDIT/CREDIT.AVI",
                credit_timing,
                streaming_error
            ) || credit_timing.width != 320 || credit_timing.height != 240) {
            std::cerr << "FAIL Disk II closing credit timeline: "
                      << streaming_error << '\n';
            return 1;
        }
        const double credit_video_tail =
            credit_timing.end_seconds - AudioDurationSeconds(credit_audio);
        centered_min_video_tail = std::min(
            centered_min_video_tail, credit_video_tail
        );
        centered_max_video_tail = std::max(
            centered_max_video_tail, credit_video_tail
        );
        if (credit_video_tail < 0.0 || credit_video_tail > 0.101) {
            std::cerr << "FAIL Disk II closing credit stream boundary tail="
                      << credit_video_tail << '\n';
            return 1;
        }
        std::cout << "OK Disk II tally winners=10 END2+WRONG4=11025x1"
                     " winner+credit-audio=11025x1 replay-threshold="
                  << static_cast<int>(metaverse::kReplayCreditThreshold) << '\n';
        std::cout << "OK centered one-shot Disk-II winner-frames="
                  << centered_winner_frames << " credit-frames="
                  << credit_timing.frame_count << " video-tail-seconds="
                  << centered_min_video_tail << ".."
                  << centered_max_video_tail << " dimensions=320x240 zoom=200%\n";
        const auto primary_avi_count = CountAviFiles(assets / "MOV");
        const auto secondary_avi_count = CountAviFiles(assets2 / "MOV");
        if (primary_avi_count != 132 || secondary_avi_count != 134) {
            std::cerr << "FAIL AVI inventory primary=" << primary_avi_count
                      << " secondary=" << secondary_avi_count << '\n';
            return 1;
        }
        std::cout << "OK AVI trigger census installed=266 primary=132"
                     " secondary=134 inactive=X11DB-only\n";
    }
    return 0;
}
