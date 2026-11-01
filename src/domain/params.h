#pragma once

#include <string>

// 一次扒谱任务的全部参数。CLI 解析后填充，应用层只读。
struct TranscribeParams {
    std::string input_path;
    std::string output_midi;

    // 运行环境（默认相对 exe 目录，可被命令行覆盖）
    std::string python_exe;
    std::string script_path;
    std::string model_dir;

    // Basic Pitch 参数
    float onset_threshold = 0.5f;
    float frame_threshold = 0.3f;
    float min_note_ms = 127.7f;

    bool verbose = false;
    bool keep_temp = false;
};
