#include "cli/args.h"

#include <filesystem>
#include <sstream>

namespace {

bool parse_float(const std::string& text, float& out) {
    try {
        size_t consumed = 0;
        out = std::stof(text, &consumed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

std::string default_output_path(const std::string& input) {
    std::filesystem::path p(input);
    p.replace_extension(".mid");
    return p.string();
}

}  // namespace

CliResult parse_args(const std::vector<std::string>& args) {
    CliResult result;

    if (args.size() <= 1) {
        result.show_help = true;
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

    return result;
}

std::string help_text() {
    std::ostringstream os;
    os << "tab_tool - 音频自动扒谱工具\n\n"
       << "用法:\n"
       << "  tab_tool <input.wav|input.mp3|目录> [选项]\n\n"
       << "输入可以是单个音频文件，也可以是包含 WAV/MP3 的目录。\n\n"
       << "选项:\n"
       << "  -o, --output <path>        输出 MIDI 路径；多文件时为输出目录\n"
       << "  -r, --recursive            目录输入时递归子目录\n"
       << "      --open                 完成后用系统默认程序打开第一个 MIDI\n"
       << "  -m, --model <dir>          模型目录（默认使用 basic-pitch 内置模型）\n"
       << "      --python <path>        Python 解释器（默认 <exe>/python/python.exe）\n"
       << "      --script <path>        推理脚本（默认 <exe>/scripts/transcribe.py）\n"
       << "      --onset-threshold <f>  起音阈值，0~1，默认 0.5\n"
       << "      --frame-threshold <f>  音帧阈值，0~1，默认 0.3\n"
       << "      --min-note-ms <f>      最短音符毫秒数，默认 58\n"
       << "  -v, --verbose              输出详细日志\n"
       << "      --keep-temp            保留临时文件（调试用）\n"
       << "      --version              显示版本\n"
       << "  -h, --help                 显示帮助\n\n"
       << "示例:\n"
       << "  tab_tool song.wav\n"
       << "  tab_tool song.mp3 -o song.mid --onset-threshold 0.4 -v\n"
       << "  tab_tool .\\music -r -o .\\out\n";
    return os.str();
}

std::string version_text() {
    return "tab_tool 0.1.0";
}
