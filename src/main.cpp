#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "app/batch_runner.h"
#include "app/transcribe_job.h"
#include "cli/args.h"
#include "cli/progress.h"
#include "infra/audio_decoder.h"
#include "infra/python_transcriber.h"

namespace {

std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

std::filesystem::path executable_dir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

std::string lower_extension(const std::filesystem::path& path) {
    std::string ext = path.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool is_supported_audio(const std::filesystem::path& path) {
    const std::string ext = lower_extension(path);
    return ext == ".wav" || ext == ".mp3";
}

std::vector<std::string> collect_inputs(const std::string& input, bool recursive) {
    std::vector<std::string> files;
    std::error_code ec;
    const auto path = std::filesystem::u8path(input);

    if (std::filesystem::is_regular_file(path, ec)) {
        files.push_back(input);
        return files;
    }

    if (!std::filesystem::is_directory(path, ec)) {
        return files;
    }

    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec)) {
            if (ec) break;
            if (entry.is_regular_file(ec) && is_supported_audio(entry.path())) {
                files.push_back(entry.path().u8string());
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            if (ec) break;
            if (entry.is_regular_file(ec) && is_supported_audio(entry.path())) {
                files.push_back(entry.path().u8string());
            }
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::string default_midi_path(const std::string& input) {
    auto path = std::filesystem::u8path(input);
    path.replace_extension(".mid");
    return path.u8string();
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    int argc = 0;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv_w) {
        std::cerr << "[tab_tool] 错误: 无法读取命令行参数" << std::endl;
        return 1;
    }

    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(wide_to_utf8(argv_w[i]));
    }
    LocalFree(argv_w);

    CliResult cli = parse_args(args);

    if (cli.show_help) {
        std::cout << help_text();
        return 0;
    }
    if (cli.show_version) {
        std::cout << version_text() << std::endl;
        return 0;
    }
    if (!cli.ok) {
        std::cerr << "[tab_tool] 错误: " << cli.error << std::endl;
        std::cerr << "使用 --help 查看用法" << std::endl;
        return 1;
    }

    // 默认运行环境：全部相对 exe 目录，保证从任意工作目录启动都能找到
    const auto base = executable_dir();
    if (cli.params.python_exe.empty()) {
        cli.params.python_exe = (base / "python" / "python.exe").u8string();
    }
    if (cli.params.script_path.empty()) {
        cli.params.script_path = (base / "scripts" / "transcribe.py").u8string();
    }
    // 默认不指定模型目录，让 Python 使用 basic_pitch 包内置模型；
    // 只有用户显式传 -m 时才覆盖。

    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(cli.params.input_path), ec)) {
        std::cerr << "[tab_tool] 错误: 输入不存在: " << cli.params.input_path << std::endl;
        return 2;
    }

    const std::vector<std::string> inputs =
        collect_inputs(cli.params.input_path, cli.recursive);
    if (inputs.empty()) {
        std::cerr << "[tab_tool] 错误: 没有找到可处理的音频文件（WAV/MP3）: "
                  << cli.params.input_path << std::endl;
        return 2;
    }

    // 单文件时 -o 视为文件路径；多文件时视为输出目录
    const bool multiple = inputs.size() > 1;
    const bool input_is_dir =
        std::filesystem::is_directory(std::filesystem::u8path(cli.params.input_path), ec);
    bool output_is_dir = false;
    if (cli.output_explicit) {
        output_is_dir =
            multiple ||
            std::filesystem::is_directory(std::filesystem::u8path(cli.params.output_midi), ec);
    }

    std::vector<TranscribeParams> tasks;
    tasks.reserve(inputs.size());
    for (const std::string& input : inputs) {
        TranscribeParams task = cli.params;
        task.input_path = input;

        if (output_is_dir) {
            const auto out_dir = std::filesystem::u8path(cli.params.output_midi);
            std::filesystem::create_directories(out_dir, ec);

            std::filesystem::path relative;
            if (input_is_dir) {
                relative = std::filesystem::relative(
                    std::filesystem::u8path(input),
                    std::filesystem::u8path(cli.params.input_path), ec);
            } else {
                relative = std::filesystem::u8path(input).filename();
            }
            relative.replace_extension(".mid");

            const auto target = out_dir / relative;
            std::filesystem::create_directories(target.parent_path(), ec);
            task.output_midi = target.u8string();
        } else if (cli.output_explicit) {
            task.output_midi = cli.params.output_midi;
        } else {
            task.output_midi = default_midi_path(input);
        }

        tasks.push_back(std::move(task));
    }

    Progress progress(cli.params.verbose);
    progress.detail("Python: " + cli.params.python_exe);
    progress.detail("脚本: " + cli.params.script_path);
    progress.detail("待处理: " + std::to_string(tasks.size()) + " 个文件");

    auto job = std::make_unique<TranscribeJob>(std::make_unique<AudioDecoder>(),
                                               std::make_unique<PythonTranscriber>());

    BatchRunner runner(*job);
    const BatchSummary summary = runner.run(tasks);

    if (summary.failed == 0) {
        progress.info("全部完成: " + std::to_string(summary.succeeded) + " 个文件");
    } else {
        progress.error("完成 " + std::to_string(summary.succeeded) + " 个，失败 " +
                       std::to_string(summary.failed) + " 个");
    }

    if (cli.open_after && !summary.outputs.empty()) {
        const std::wstring target = utf8_to_wide(summary.outputs.front());
        ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    return summary.failed == 0 ? 0 : 4;
}
