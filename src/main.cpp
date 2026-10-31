#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

std::filesystem::path executable_dir() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
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

    Progress progress(cli.params.verbose);
    progress.detail("输入: " + cli.params.input_path);
    progress.detail("输出: " + cli.params.output_midi);
    progress.detail("Python: " + cli.params.python_exe);
    progress.detail("脚本: " + cli.params.script_path);
    progress.detail("模型目录: " + cli.params.model_dir);

    auto job = std::make_unique<TranscribeJob>(std::make_unique<AudioDecoder>(),
                                               std::make_unique<PythonTranscriber>());

    std::string error;
    if (!job->run(cli.params, error)) {
        progress.error(error);
        return 3;
    }

    progress.info("完成: " + cli.params.output_midi);
    return 0;
}
