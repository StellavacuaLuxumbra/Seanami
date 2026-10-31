#include "infra/python_transcriber.h"

#include <windows.h>

#include "json.hpp"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

std::wstring quote_arg(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring result = L"\"";
    for (wchar_t c : arg) {
        if (c == L'"') result += L"\\\"";
        else result += c;
    }
    result += L"\"";
    return result;
}

std::string read_whole_file(const std::wstring& path) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};

    const DWORD size = GetFileSize(handle, nullptr);
    std::string content(size, '\0');
    DWORD read = 0;
    if (size > 0) {
        ReadFile(handle, content.data(), size, &read, nullptr);
    }
    CloseHandle(handle);
    content.resize(read);
    return content;
}

std::string last_non_empty_line(const std::string& text) {
    std::string last;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) last = line;
    }
    return last;
}

std::string tail(const std::string& text, size_t max_lines) {
    std::vector<std::string> lines;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty()) lines.push_back(line);
    }

    std::ostringstream os;
    const size_t begin = lines.size() > max_lines ? lines.size() - max_lines : 0;
    for (size_t i = begin; i < lines.size(); ++i) {
        os << lines[i] << "\n";
    }
    return os.str();
}

}  // namespace

PythonTranscriber::ProcessResult PythonTranscriber::run_process(
    const std::wstring& command_line) {
    ProcessResult result;

    wchar_t temp_dir[MAX_PATH];
    if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
        result.launch_error = "无法获取临时目录";
        return result;
    }

    wchar_t out_file[MAX_PATH];
    wchar_t err_file[MAX_PATH];
    if (GetTempFileNameW(temp_dir, L"tbo", 0, out_file) == 0 ||
        GetTempFileNameW(temp_dir, L"tbe", 0, err_file) == 0) {
        result.launch_error = "无法创建临时输出文件";
        return result;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE h_out = CreateFileW(out_file, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE h_err = CreateFileW(err_file, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (h_out == INVALID_HANDLE_VALUE || h_err == INVALID_HANDLE_VALUE) {
        if (h_out != INVALID_HANDLE_VALUE) CloseHandle(h_out);
        if (h_err != INVALID_HANDLE_VALUE) CloseHandle(h_err);
        DeleteFileW(out_file);
        DeleteFileW(err_file);
        result.launch_error = "无法创建输出重定向句柄";
        return result;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h_out;
    si.hStdError = h_err;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = command_line;
    const BOOL created = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(h_out);
    CloseHandle(h_err);

    if (!created) {
        const DWORD code = GetLastError();
        DeleteFileW(out_file);
        DeleteFileW(err_file);
        result.launch_error = "无法启动 Python 进程（错误码 " + std::to_string(code) + "）";
        return result;
    }

    result.launched = true;
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = exit_code;

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    result.stdout_text = read_whole_file(out_file);
    result.stderr_text = read_whole_file(err_file);

    DeleteFileW(out_file);
    DeleteFileW(err_file);
    return result;
}

bool PythonTranscriber::transcribe(const TranscribeParams& params,
                                   std::string& error_message) {
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(params.python_exe), ec)) {
        error_message =
            "找不到 Python 解释器: " + params.python_exe +
            "\n请把 Python 运行时放到 exe 同目录的 python\\ 下，或用 --python 指定";
        return false;
    }
    if (!std::filesystem::exists(std::filesystem::u8path(params.script_path), ec)) {
        error_message = "找不到推理脚本: " + params.script_path;
        return false;
    }

    std::wstring cmd;
    cmd += quote_arg(utf8_to_wide(params.python_exe));
    cmd += L" ";
    cmd += quote_arg(utf8_to_wide(params.script_path));
    cmd += L" --input ";
    cmd += quote_arg(utf8_to_wide(params.input_path));
    cmd += L" --output ";
    cmd += quote_arg(utf8_to_wide(params.output_midi));

    if (!params.model_dir.empty()) {
        cmd += L" --model ";
        cmd += quote_arg(utf8_to_wide(params.model_dir));
    }

    cmd += L" --onset-threshold " + utf8_to_wide(std::to_string(params.onset_threshold));
    cmd += L" --frame-threshold " + utf8_to_wide(std::to_string(params.frame_threshold));
    cmd += L" --min-note-ms " + utf8_to_wide(std::to_string(params.min_note_ms));

    if (params.verbose) {
        cmd += L" --verbose";
    }

    const ProcessResult result = run_process(cmd);

    if (!result.launched) {
        error_message = result.launch_error;
        return false;
    }

    // Python 脚本约定：stdout 最后一行输出 JSON 摘要
    const std::string last_line = last_non_empty_line(result.stdout_text);
    if (!last_line.empty()) {
        try {
            const auto summary = nlohmann::json::parse(last_line);
            if (summary.contains("ok") && summary["ok"].is_boolean() &&
                !summary["ok"].get<bool>()) {
                std::string detail = "推理脚本报告失败";
                if (summary.contains("error") && summary["error"].is_string()) {
                    detail = summary["error"].get<std::string>();
                }
                if (!result.stderr_text.empty()) {
                    detail += "\n" + tail(result.stderr_text, 5);
                }
                error_message = detail;
                return false;
            }
        } catch (...) {
            // 非 JSON 输出忽略，继续用退出码判断
        }
    }

    if (result.exit_code != 0) {
        std::ostringstream os;
        os << "Python 推理失败（退出码 " << result.exit_code << "）";
        if (!result.stderr_text.empty()) {
            os << "\n" << tail(result.stderr_text, 8);
        }
        error_message = os.str();
        return false;
    }

    return true;
}
