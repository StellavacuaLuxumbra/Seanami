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

    // 预设与后处理
    std::string preset = "default";
    float min_freq = 0.0f;   // 0 表示不限制
    float max_freq = 0.0f;   // 0 表示不限制
    bool mono = false;
    bool quantize = false;
    std::string quantize_grid = "1/16";
    float bpm = 0.0f;        // 0 表示自动检测
    std::string separate = "none";  // none / hpss / demucs
    bool postprocess = true;
    float merge_gap_ms = 0.0f;
    bool tempo_map = true;
    std::string track_name;
    int program = -1;        // -1 表示不写入乐器
    float hpss_margin = 1.0f;
    std::string demucs_model = "htdemucs";

    bool verbose = false;
    bool keep_temp = false;
    bool use_worker = true;
};
