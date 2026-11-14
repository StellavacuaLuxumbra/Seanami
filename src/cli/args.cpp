#include "cli/args.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace {

struct PresetValues {
    const char* name;
    float onset_threshold;
    float frame_threshold;
    float min_note_ms;
    float min_freq;
    float max_freq;
    bool mono;
    bool quantize;
    float merge_gap_ms;
    int program;
};

// 预设只调整“适合该乐器”的检测参数；量化默认不开启，
// 需要对齐节拍时请显式加 --quantize。合并间隔默认 0，避免把
// 快速重复音或连音误并成一个音。
const PresetValues kPresets[] = {
    {"default", 0.5f, 0.3f, 127.7f, 0.0f, 0.0f, false, false, 0.0f, -1},
    {"vocal", 0.45f, 0.3f, 100.0f, 82.41f, 1046.50f, true, false, 0.0f, 53},
    {"guitar", 0.5f, 0.3f, 100.0f, 82.41f, 1318.51f, true, false, 0.0f, 25},
    {"piano", 0.5f, 0.3f, 90.0f, 27.50f, 4186.01f, false, false, 0.0f, 0},
    {"bass", 0.5f, 0.3f, 90.0f, 30.87f, 392.00f, true, false, 0.0f, 33},
};

std::string lower_copy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool parse_float(const std::string& text, float& out) {
    try {
        size_t consumed = 0;
        out = std::stof(text, &consumed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

bool parse_int(const std::string& text, int& out) {
    try {
        size_t consumed = 0;
        const long value = std::stol(text, &consumed);
        if (consumed != text.size()) return false;
        out = static_cast<int>(value);
        return true;
    } catch (...) {
        return false;
    }
}

std::string default_output_path(const std::string& input) {
    std::filesystem::path p = std::filesystem::u8path(input);
    p.replace_extension(".mid");
    return p.u8string();
}

bool apply_preset(const std::string& raw_name, TranscribeParams& params) {
    const std::string name = lower_copy(raw_name);
    for (const PresetValues& preset : kPresets) {
        if (name == preset.name) {
            params.preset = name;
            params.onset_threshold = preset.onset_threshold;
            params.frame_threshold = preset.frame_threshold;
            params.min_note_ms = preset.min_note_ms;
            params.min_freq = preset.min_freq;
            params.max_freq = preset.max_freq;
            params.mono = preset.mono;
            params.quantize = preset.quantize;
            params.merge_gap_ms = preset.merge_gap_ms;
            params.program = preset.program;
            return true;
        }
    }
    return false;
}

bool valid_grid(const std::string& grid) {
    return grid == "1/4" || grid == "1/8" || grid == "1/16" || grid == "1/32" ||
           grid == "1/8T" || grid == "1/16T";
}

bool valid_separate(const std::string& value) {
    return value == "none" || value == "hpss" || value == "demucs";
}

}  // namespace

CliResult parse_args(const std::vector<std::string>& args) {
    CliResult result;

    if (args.size() <= 1) {
        result.show_help = true;
        return result;
    }

    // 先扫描 preset，让显式参数在后面的主循环中覆盖预设默认值。
    std::string preset_name = "default";
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--preset") {
            if (i + 1 >= args.size()) {
                result.ok = false;
                result.error = "参数 --preset 缺少值";
                return result;
            }
            preset_name = args[++i];
        } else if (arg.rfind("--preset=", 0) == 0) {
            preset_name = arg.substr(9);
        }
    }
    if (!apply_preset(preset_name, result.params)) {
        result.ok = false;
        result.error = "未知预设: " + preset_name +
                       "（可选 default/vocal/guitar/piano/bass）";
        return result;
    }

    bool input_set = false;

    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];

        auto need_value = [&](const char* name, std::string& target) -> bool {
            if (i + 1 >= args.size()) {
                result.ok = false;
                result.error = std::string("参数 ") + name + " 缺少值";
                return false;
            }
            target = args[++i];
            return true;
        };

        if (arg == "-h" || arg == "--help") {
            result.show_help = true;
            return result;
        }
        if (arg == "--version") {
            result.show_version = true;
            return result;
        }
        if (arg == "-v" || arg == "--verbose") {
            result.params.verbose = true;
            continue;
        }
        if (arg == "-r" || arg == "--recursive") {
            result.recursive = true;
            continue;
        }
        if (arg == "--open") {
            result.open_after = true;
            continue;
        }
        if (arg == "--keep-temp") {
            result.params.keep_temp = true;
            continue;
        }
        if (arg == "--skip-existing") {
            result.skip_existing = true;
            continue;
        }
        if (arg == "--no-worker") {
            result.use_worker = false;
            continue;
        }
        if (arg == "-o" || arg == "--output") {
            if (!need_value("--output", result.params.output_midi)) return result;
            result.output_explicit = true;
            continue;
        }
        if (arg == "-m" || arg == "--model") {
            if (!need_value("--model", result.params.model_dir)) return result;
            continue;
        }
        if (arg == "--python") {
            if (!need_value("--python", result.params.python_exe)) return result;
            continue;
        }
        if (arg == "--script") {
            if (!need_value("--script", result.params.script_path)) return result;
            continue;
        }
        if (arg == "--preset") {
            std::string ignored;
            if (!need_value("--preset", ignored)) return result;
            continue;
        }
        if (arg.rfind("--preset=", 0) == 0) {
            continue;
        }
        if (arg == "--report") {
            if (!need_value("--report", result.report_path)) return result;
            continue;
        }
        if (arg == "--track-name") {
            if (!need_value("--track-name", result.params.track_name)) return result;
            continue;
        }
        if (arg == "--demucs-model") {
            if (!need_value("--demucs-model", result.params.demucs_model)) return result;
            continue;
        }
        if (arg == "--separate") {
            std::string value;
            if (!need_value("--separate", value)) return result;
            value = lower_copy(value);
            if (!valid_separate(value)) {
                result.ok = false;
                result.error = "--separate 只能是 none、hpss 或 demucs";
                return result;
            }
            result.params.separate = value;
            continue;
        }
        if (arg == "--quantize-grid") {
            std::string value;
            if (!need_value("--quantize-grid", value)) return result;
            if (!valid_grid(value)) {
                result.ok = false;
                result.error = "--quantize-grid 只能是 1/4、1/8、1/16、1/32、1/8T、1/16T";
                return result;
            }
            result.params.quantize_grid = value;
            continue;
        }
        if (arg == "--mono") {
            result.params.mono = true;
            continue;
        }
        if (arg == "--poly") {
            result.params.mono = false;
            continue;
        }
        if (arg == "--quantize") {
            result.params.quantize = true;
            continue;
        }
        if (arg == "--no-quantize") {
            result.params.quantize = false;
            continue;
        }
        if (arg == "--postprocess") {
            result.params.postprocess = true;
            continue;
        }
        if (arg == "--no-postprocess") {
            result.params.postprocess = false;
            continue;
        }
        if (arg == "--tempo-map") {
            result.params.tempo_map = true;
            continue;
        }
        if (arg == "--no-tempo-map") {
            result.params.tempo_map = false;
            continue;
        }
        if (arg == "--onset-threshold") {
            std::string value;
            if (!need_value("--onset-threshold", value)) return result;
            if (!parse_float(value, result.params.onset_threshold)) {
                result.ok = false;
                result.error = "--onset-threshold 需要 0~1 之间的数字";
                return result;
            }
            continue;
        }
        if (arg == "--frame-threshold") {
            std::string value;
            if (!need_value("--frame-threshold", value)) return result;
            if (!parse_float(value, result.params.frame_threshold)) {
                result.ok = false;
                result.error = "--frame-threshold 需要 0~1 之间的数字";
                return result;
            }
            continue;
        }
        if (arg == "--min-note-ms") {
            std::string value;
            if (!need_value("--min-note-ms", value)) return result;
            if (!parse_float(value, result.params.min_note_ms)) {
                result.ok = false;
                result.error = "--min-note-ms 需要数字（毫秒）";
                return result;
            }
            continue;
        }
        if (arg == "--min-freq") {
            std::string value;
            if (!need_value("--min-freq", value)) return result;
            if (!parse_float(value, result.params.min_freq)) {
                result.ok = false;
                result.error = "--min-freq 需要数字（Hz）";
                return result;
            }
            continue;
        }
        if (arg == "--max-freq") {
            std::string value;
            if (!need_value("--max-freq", value)) return result;
            if (!parse_float(value, result.params.max_freq)) {
                result.ok = false;
                result.error = "--max-freq 需要数字（Hz）";
                return result;
            }
            continue;
        }
        if (arg == "--bpm") {
            std::string value;
            if (!need_value("--bpm", value)) return result;
            if (!parse_float(value, result.params.bpm)) {
                result.ok = false;
                result.error = "--bpm 需要数字（0 表示自动）";
                return result;
            }
            continue;
        }
        if (arg == "--merge-gap-ms") {
            std::string value;
            if (!need_value("--merge-gap-ms", value)) return result;
            if (!parse_float(value, result.params.merge_gap_ms)) {
                result.ok = false;
                result.error = "--merge-gap-ms 需要数字（毫秒）";
                return result;
            }
            continue;
        }
        if (arg == "--hpss-margin") {
            std::string value;
            if (!need_value("--hpss-margin", value)) return result;
            if (!parse_float(value, result.params.hpss_margin)) {
                result.ok = false;
                result.error = "--hpss-margin 需要数字";
                return result;
            }
            continue;
        }
        if (arg == "--program") {
            std::string value;
            if (!need_value("--program", value)) return result;
            if (!parse_int(value, result.params.program)) {
                result.ok = false;
                result.error = "--program 需要 0~127 的整数（-1 表示不指定）";
                return result;
            }
            continue;
        }
        if (arg == "--jobs") {
            std::string value;
            if (!need_value("--jobs", value)) return result;
            if (!parse_int(value, result.jobs)) {
                result.ok = false;
                result.error = "--jobs 需要整数";
                return result;
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            result.ok = false;
            result.error = "未知参数: " + arg;
            return result;
        }

        if (!input_set) {
            result.params.input_path = arg;
            input_set = true;
        } else {
            result.ok = false;
            result.error = "只能指定一个输入文件或目录";
            return result;
        }
    }

    if (!input_set) {
        result.ok = false;
        result.error = "缺少输入音频文件";
        return result;
    }

    if (!result.output_explicit) {
        result.params.output_midi = default_output_path(result.params.input_path);
    }

    if (result.params.onset_threshold < 0.0f || result.params.onset_threshold > 1.0f) {
        result.ok = false;
        result.error = "--onset-threshold 必须在 0~1 之间";
        return result;
    }
    if (result.params.frame_threshold < 0.0f || result.params.frame_threshold > 1.0f) {
        result.ok = false;
        result.error = "--frame-threshold 必须在 0~1 之间";
        return result;
    }
    if (result.params.min_note_ms <= 0.0f) {
        result.ok = false;
        result.error = "--min-note-ms 必须大于 0";
        return result;
    }
    if (result.params.min_freq < 0.0f || result.params.max_freq < 0.0f) {
        result.ok = false;
        result.error = "--min-freq / --max-freq 不能为负数";
        return result;
    }
    if (result.params.min_freq > 0.0f && result.params.max_freq > 0.0f &&
        result.params.min_freq >= result.params.max_freq) {
        result.ok = false;
        result.error = "--min-freq 必须小于 --max-freq";
        return result;
    }
    if (result.params.bpm < 0.0f ||
        (result.params.bpm > 0.0f &&
         (result.params.bpm < 20.0f || result.params.bpm > 400.0f))) {
        result.ok = false;
        result.error = "--bpm 必须为 0（自动）或 20~400";
        return result;
    }
    if (result.params.merge_gap_ms < 0.0f) {
        result.ok = false;
        result.error = "--merge-gap-ms 不能为负数";
        return result;
    }
    if (result.params.hpss_margin < 1.0f) {
        result.ok = false;
        result.error = "--hpss-margin 必须 >= 1.0";
        return result;
    }
    if (result.params.program < -1 || result.params.program > 127) {
        result.ok = false;
        result.error = "--program 必须在 -1~127 之间";
        return result;
    }
    if (result.jobs < 1 || result.jobs > 16) {
        result.ok = false;
        result.error = "--jobs 必须在 1~16 之间";
        return result;
    }

    return result;
}

std::string help_text() {
    std::ostringstream os;
    os << "tab_tool - 音频自动扒谱工具\n\n"
       << "用法:\n"
       << "  tab_tool <input.wav|input.mp3|input.flac|目录> [选项]\n\n"
       << "输入可以是单个音频文件，也可以是包含 WAV/MP3/FLAC 的目录。\n\n"
       << "输出:\n"
       << "  -o, --output <path>        输出 MIDI 路径；多文件时为输出目录\n"
       << "  -r, --recursive            目录输入时递归子目录\n"
       << "      --open                 完成后用系统默认程序打开第一个 MIDI\n"
       << "      --track-name <name>    MIDI 轨道名（默认使用输入文件名）\n"
       << "      --program <0-127>      GM 乐器编号（-1 表示不写入）\n"
       << "      --tempo-map            写入检测到的速度地图（默认开启）\n"
       << "      --no-tempo-map         只写单一速度\n\n"
       << "分析:\n"
       << "      --preset <name>        default / vocal / guitar / piano / bass\n"
       << "      --onset-threshold <f>  起音阈值，0~1\n"
       << "      --frame-threshold <f>  音帧阈值，0~1\n"
       << "      --min-note-ms <f>      最短音符毫秒数\n"
       << "      --min-freq <hz>        最低音高频率，0 表示不限制\n"
       << "      --max-freq <hz>        最高音高频率，0 表示不限制\n"
       << "      --mono / --poly        单音 / 复音输出\n"
       << "      --quantize             吸附到节拍网格\n"
       << "      --no-quantize          不量化（默认）\n"
       << "      --quantize-grid <g>    1/4、1/8、1/16、1/32、1/8T、1/16T\n"
       << "      --bpm <f>              固定 BPM；0 表示自动检测\n"
       << "      --merge-gap-ms <f>     合并同音高碎片的最大间隔\n"
       << "      --postprocess          启用后处理（默认开启）\n"
       << "      --no-postprocess       关闭后处理\n"
       << "      --separate <mode>      none / hpss / demucs\n"
       << "      --hpss-margin <f>      HPSS 分离强度，>=1.0（默认 1.0）\n"
       << "      --demucs-model <name>  Demucs 模型（默认 htdemucs）\n\n"
       << "批量:\n"
       << "      --jobs <n>             并行任务数，1~16（默认 1）\n"
       << "      --skip-existing        跳过已存在的输出 MIDI\n"
       << "      --report <path>        写入 JSON 或 CSV 汇总报告\n"
       << "      --no-worker            禁用常驻 Python worker\n\n"
       << "运行环境:\n"
       << "  -m, --model <dir>          模型目录（默认使用 basic-pitch 内置 ONNX 模型）\n"
       << "      --python <path>        Python 解释器（默认 <exe>/python/python.exe）\n"
       << "      --script <path>        推理脚本（默认 <exe>/scripts/transcribe.py）\n"
       << "  -v, --verbose              输出详细日志\n"
       << "      --keep-temp            保留临时文件（调试用）\n"
       << "      --version              显示版本\n"
       << "  -h, --help                 显示帮助\n\n"
       << "示例:\n"
       << "  tab_tool song.wav\n"
       << "  tab_tool song.mp3 -o song.mid --preset vocal\n"
       << "  tab_tool song.wav --preset guitar --quantize --report out.csv\n"
       << "  tab_tool .\\music -r -o .\\out --jobs 4 --skip-existing\n"
       << "  tab_tool song.wav --separate hpss -o song.mid\n";
    return os.str();
}

std::string version_text() {
    return "tab_tool 0.2.0";
}
