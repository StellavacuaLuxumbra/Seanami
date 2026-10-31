#pragma once

#include <string>

// 音频解码抽象：把任意支持的输入统一转成标准 PCM WAV，
// 让 Python 端只面对一种格式，避免 MP3 解码器差异。
class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;

    // 成功返回 true，out_wav 为生成的临时 WAV 路径。
    virtual bool decode_to_wav(const std::string& input_path,
                               const std::string& out_wav_path,
                               std::string& error_message) = 0;
};

// 基于 dr_wav / dr_mp3 的默认实现。
class AudioDecoder : public IAudioDecoder {
public:
    bool decode_to_wav(const std::string& input_path,
                       const std::string& out_wav_path,
                       std::string& error_message) override;
};
