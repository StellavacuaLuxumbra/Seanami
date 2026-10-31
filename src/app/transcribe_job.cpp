#include "app/transcribe_job.h"

#include <chrono>
#include <filesystem>
#include <system_error>

#include "infra/midi_validator.h"

TranscribeJob::TranscribeJob(std::unique_ptr<IAudioDecoder> decoder,
                             std::unique_ptr<ITranscriber> transcriber)
    : decoder_(std::move(decoder)), transcriber_(std::move(transcriber)) {}

bool TranscribeJob::run(const TranscribeParams& params, std::string& error_message) {
    std::error_code ec;

    // 1. 检查输入文件
    if (!std::filesystem::exists(std::filesystem::u8path(params.input_path), ec)) {
        error_message = "输入文件不存在: " + params.input_path;
        return false;
    }

    // 2. 准备临时 WAV
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temp_wav =
        std::filesystem::temp_directory_path() /
        ("tab_tool_" + std::to_string(stamp) + ".wav");

    auto cleanup = [&]() {
        if (!params.keep_temp) {
            std::filesystem::remove(temp_wav, ec);
        }
    };

    // 3. 解码为统一格式，避免 Python 端依赖 MP3 解码器
    if (!decoder_->decode_to_wav(params.input_path, temp_wav.u8string(), error_message)) {
        cleanup();
        return false;
    }

    // 4. 调用推理后端（第一版为 Python 子进程）
    TranscribeParams run_params = params;
    run_params.input_path = temp_wav.u8string();
    if (!transcriber_->transcribe(run_params, error_message)) {
        cleanup();
        return false;
    }

    // 5. 校验产物
    if (!validate_midi_file(params.output_midi, error_message)) {
        cleanup();
        return false;
    }

    cleanup();
    return true;
}
