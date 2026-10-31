#include "infra/audio_decoder.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}
#endif

std::string lower_extension(const std::string& path) {
    std::string ext = std::filesystem::u8path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool load_wav(const std::string& path,
              std::vector<float>& pcm,
              uint32_t& sample_rate,
              uint32_t& channels,
              std::string& error) {
    drwav wav{};
#ifdef _WIN32
    const bool opened = drwav_init_file_w(&wav, utf8_to_wide(path).c_str(), nullptr);
#else
    const bool opened = drwav_init_file(&wav, path.c_str(), nullptr);
#endif
    if (!opened) {
        error = "无法打开 WAV 文件（文件损坏或不是有效的 WAV）";
        return false;
    }

    sample_rate = wav.sampleRate;
    channels = wav.channels;
    const drwav_uint64 frame_count = wav.totalPCMFrameCount;
    if (frame_count == 0 || channels == 0) {
        drwav_uninit(&wav);
        error = "WAV 文件不包含音频数据";
        return false;
    }

    pcm.resize(static_cast<size_t>(frame_count) * channels);
    drwav_read_pcm_frames_f32(&wav, frame_count, pcm.data());
    drwav_uninit(&wav);
    return true;
}

bool load_mp3(const std::string& path,
              std::vector<float>& pcm,
              uint32_t& sample_rate,
              uint32_t& channels,
              std::string& error) {
    drmp3 mp3{};
#ifdef _WIN32
    const bool opened = drmp3_init_file_w(&mp3, utf8_to_wide(path).c_str(), nullptr);
#else
    const bool opened = drmp3_init_file(&mp3, path.c_str(), nullptr);
#endif
    if (!opened) {
        error = "无法打开 MP3 文件（文件损坏或不是有效的 MP3）";
        return false;
    }

    sample_rate = mp3.sampleRate;
    channels = mp3.channels;
    if (channels == 0) {
        drmp3_uninit(&mp3);
        error = "MP3 文件声道信息异常";
        return false;
    }

    const uint32_t frame_capacity = 4096 / channels;
    std::vector<float> buffer(static_cast<size_t>(frame_capacity) * channels);
    while (true) {
        const drmp3_uint64 frames =
            drmp3_read_pcm_frames_f32(&mp3, frame_capacity, buffer.data());
        if (frames == 0) break;
        pcm.insert(pcm.end(), buffer.begin(),
                   buffer.begin() + static_cast<size_t>(frames) * channels);
    }
    drmp3_uninit(&mp3);

    if (pcm.empty()) {
        error = "MP3 文件不包含音频数据";
        return false;
    }
    return true;
}

}  // namespace

bool AudioDecoder::decode_to_wav(const std::string& input_path,
                                 const std::string& out_wav_path,
                                 std::string& error_message) {
    const std::string ext = lower_extension(input_path);
    std::vector<float> pcm;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;

    if (ext == ".wav") {
        if (!load_wav(input_path, pcm, sample_rate, channels, error_message)) {
            return false;
        }
    } else if (ext == ".mp3") {
        if (!load_mp3(input_path, pcm, sample_rate, channels, error_message)) {
            return false;
        }
    } else {
        error_message = "不支持的输入格式（仅支持 WAV / MP3）";
        return false;
    }

    drwav_data_format format{};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = channels;
    format.sampleRate = sample_rate;
    format.bitsPerSample = 16;

    drwav wav_out{};
#ifdef _WIN32
    const bool created =
        drwav_init_file_write_w(&wav_out, utf8_to_wide(out_wav_path).c_str(), &format, nullptr);
#else
    const bool created = drwav_init_file_write(&wav_out, out_wav_path.c_str(), &format, nullptr);
#endif
    if (!created) {
        error_message = "无法创建临时 WAV 文件";
        return false;
    }

    std::vector<drwav_int16> pcm16(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        const float clamped = std::max(-1.0f, std::min(1.0f, pcm[i]));
        pcm16[i] = static_cast<drwav_int16>(clamped * 32767.0f);
    }

    drwav_write_pcm_frames(&wav_out, pcm16.size() / channels, pcm16.data());
    drwav_uninit(&wav_out);
    return true;
}
