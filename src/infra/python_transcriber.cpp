#include "infra/python_transcriber.h"

#include <windows.h>

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
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
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0) {
        CloseHandle(handle);
        return {};
    }
    std::string content(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!ReadFile(handle, content.data(), static_cast<DWORD>(content.size()), &read, nullptr)) {
        read = 0;
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

void append_option(std::wstring& cmd, const wchar_t* name, const std::string& value) {
    cmd += L" ";
    cmd += name;
    cmd += L" ";
    cmd += quote_arg(utf8_to_wide(value));
}

void append_option(std::wstring& cmd, const wchar_t* name, const wchar_t* value) {
    cmd += L" ";
    cmd += name;
    cmd += L" ";
    cmd += quote_arg(value);
}

void append_option(std::wstring& cmd, const wchar_t* name, float value) {
    cmd += L" ";
    cmd += name;
    cmd += L" ";
    cmd += utf8_to_wide(std::to_string(value));
}

void append_option(std::wstring& cmd, const wchar_t* name, int value) {
    cmd += L" ";
    cmd += name;
    cmd += L" ";
    cmd += utf8_to_wide(std::to_string(value));
}

std::wstring build_command_line(const TranscribeParams& p) {
    std::wstring cmd;
    cmd += quote_arg(utf8_to_wide(p.python_exe));
    cmd += L" ";
    cmd += quote_arg(utf8_to_wide(p.script_path));

    append_option(cmd, L"--input", p.input_path);
    append_option(cmd, L"--output", p.output_midi);
    if (!p.model_dir.empty()) {
        append_option(cmd, L"--model", p.model_dir);
    }
    append_option(cmd, L"--preset", p.preset);
    append_option(cmd, L"--onset-threshold", p.onset_threshold);
    append_option(cmd, L"--frame-threshold", p.frame_threshold);
    append_option(cmd, L"--min-note-ms", p.min_note_ms);
    append_option(cmd, L"--min-freq", p.min_freq);
    append_option(cmd, L"--max-freq", p.max_freq);
    cmd += p.mono ? L" --mono" : L" --poly";
    cmd += p.quantize ? L" --quantize" : L" --no-quantize";
    append_option(cmd, L"--quantize-grid", p.quantize_grid);
    append_option(cmd, L"--bpm", p.bpm);
    append_option(cmd, L"--separate", p.separate);
    cmd += p.postprocess ? L" --postprocess" : L" --no-postprocess";
    append_option(cmd, L"--merge-gap-ms", p.merge_gap_ms);
    cmd += p.tempo_map ? L" --tempo-map" : L" --no-tempo-map";
    append_option(cmd, L"--track-name", p.track_name);
    append_option(cmd, L"--program", p.program);
    append_option(cmd, L"--hpss-margin", p.hpss_margin);
    append_option(cmd, L"--demucs-model", p.demucs_model);
    if (p.verbose) {
        cmd += L" --verbose";
    }
    return cmd;
}

nlohmann::json build_job_json(const TranscribeParams& p) {
    nlohmann::json job;
    job["input"] = p.input_path;
    job["output"] = p.output_midi;
    job["model"] = p.model_dir;
    job["preset"] = p.preset;
    job["onset_threshold"] = p.onset_threshold;
    job["frame_threshold"] = p.frame_threshold;
    job["min_note_ms"] = p.min_note_ms;
    job["min_freq"] = p.min_freq > 0.0f ? nlohmann::json(p.min_freq) : nlohmann::json(nullptr);
    job["max_freq"] = p.max_freq > 0.0f ? nlohmann::json(p.max_freq) : nlohmann::json(nullptr);
    job["mono"] = p.mono;
    job["quantize"] = p.quantize;
    job["quantize_grid"] = p.quantize_grid;
    job["bpm"] = p.bpm > 0.0f ? nlohmann::json(p.bpm) : nlohmann::json(nullptr);
    job["separate"] = p.separate;
    job["postprocess"] = p.postprocess;
    job["merge_gap_ms"] = p.merge_gap_ms;
    job["tempo_map"] = p.tempo_map;
    job["track_name"] = p.track_name;
    job["program"] = p.program;
    job["hpss_margin"] = p.hpss_margin;
    job["demucs_model"] = p.demucs_model;
    job["verbose"] = p.verbose;
    return job;
}

void fill_stats(const nlohmann::json& summary, TranscribeStats* stats) {
    if (!stats) return;
    try {
        if (summary.contains("notes") && summary["notes"].is_number_integer()) {
            stats->notes = summary["notes"].get<int>();
        }
        if (summary.contains("bpm") && summary["bpm"].is_number()) {
            stats->bpm = summary["bpm"].get<double>();
        }
        if (summary.contains("quantized") && summary["quantized"].is_boolean()) {
            stats->quantized = summary["quantized"].get<bool>();
        }
        if (summary.contains("preset") && summary["preset"].is_string()) {
            stats->preset = summary["preset"].get<std::string>();
        }
        if (summary.contains("separate") && summary["separate"].is_string()) {
            stats->separate = summary["separate"].get<std::string>();
        }
    } catch (...) {
        // 统计字段只用于报告，解析失败不影响主流程。
    }
}

bool read_line_handle(HANDLE handle, std::string& line) {
    line.clear();
    char ch = '\0';
    while (true) {
        DWORD read = 0;
        if (!ReadFile(handle, &ch, 1, &read, nullptr) || read == 0) {
            return false;
        }
        if (ch == '\n') return true;
        if (ch != '\r') line.push_back(ch);
        if (line.size() > 16u * 1024u * 1024u) {
            return false;
        }
    }
}

bool write_all(HANDLE handle, const std::string& text) {
    size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(text.size() - offset, 1u << 20));
        if (!WriteFile(handle, text.data() + offset, chunk, &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

}  // namespace

struct PythonTranscriber::WorkerState {
    HANDLE process = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    std::wstring stderr_path;
};

PythonTranscriber::PythonTranscriber() = default;

PythonTranscriber::~PythonTranscriber() {
    stop_worker();
}

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

bool PythonTranscriber::transcribe_process(const TranscribeParams& params,
                                           std::string& error_message,
                                           TranscribeStats* stats) {
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

    const ProcessResult result = run_process(build_command_line(params));

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
            fill_stats(summary, stats);
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

std::string PythonTranscriber::read_worker_stderr() const {
    if (!worker_ || worker_->stderr_path.empty()) return {};
    return read_whole_file(worker_->stderr_path);
}

void PythonTranscriber::stop_worker() {
    if (!worker_) return;

    if (worker_->stdin_write) {
        CloseHandle(worker_->stdin_write);
        worker_->stdin_write = nullptr;
    }
    if (worker_->stdout_read) {
        CloseHandle(worker_->stdout_read);
        worker_->stdout_read = nullptr;
    }
    if (worker_->process) {
        const DWORD wait = WaitForSingleObject(worker_->process, 3000);
        if (wait == WAIT_TIMEOUT) {
            TerminateProcess(worker_->process, 0);
        }
        CloseHandle(worker_->process);
        worker_->process = nullptr;
    }
    if (!worker_->stderr_path.empty()) {
        DeleteFileW(worker_->stderr_path.c_str());
    }
    worker_.reset();
    worker_key_.clear();
}

bool PythonTranscriber::ensure_worker(const TranscribeParams& params,
                                      std::string& error_message) {
    const std::string key = params.python_exe + "|" + params.script_path + "|" +
                            params.model_dir + "|" + (params.verbose ? "1" : "0");
    if (worker_ && worker_key_ == key) {
        return true;
    }
    stop_worker();
    if (worker_disabled_) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(params.python_exe), ec)) {
        error_message = "找不到 Python 解释器: " + params.python_exe;
        worker_disabled_ = true;
        return false;
    }
    if (!std::filesystem::exists(std::filesystem::u8path(params.script_path), ec)) {
        error_message = "找不到推理脚本: " + params.script_path;
        worker_disabled_ = true;
        return false;
    }

    std::wstring cmd;
    cmd += quote_arg(utf8_to_wide(params.python_exe));
    cmd += L" ";
    cmd += quote_arg(utf8_to_wide(params.script_path));
    cmd += L" --worker";
    if (!params.model_dir.empty()) {
        cmd += L" --model ";
        cmd += quote_arg(utf8_to_wide(params.model_dir));
    }
    if (params.verbose) {
        cmd += L" --verbose";
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE child_in_read = nullptr;
    HANDLE parent_in_write = nullptr;
    HANDLE parent_out_read = nullptr;
    HANDLE child_out_write = nullptr;
    HANDLE stderr_file = INVALID_HANDLE_VALUE;
    wchar_t stderr_path[MAX_PATH] = {};

    auto cleanup_child = [&]() {
        if (child_in_read) CloseHandle(child_in_read);
        if (child_out_write) CloseHandle(child_out_write);
        if (stderr_file != INVALID_HANDLE_VALUE) CloseHandle(stderr_file);
        child_in_read = nullptr;
        child_out_write = nullptr;
        stderr_file = INVALID_HANDLE_VALUE;
    };
    auto cleanup_all = [&]() {
        cleanup_child();
        if (parent_in_write) CloseHandle(parent_in_write);
        if (parent_out_read) CloseHandle(parent_out_read);
        parent_in_write = nullptr;
        parent_out_read = nullptr;
    };

    if (!CreatePipe(&child_in_read, &parent_in_write, &sa, 0) ||
        !CreatePipe(&parent_out_read, &child_out_write, &sa, 0)) {
        cleanup_all();
        error_message = "无法创建 worker 管道";
        worker_disabled_ = true;
        return false;
    }
    if (!SetHandleInformation(parent_in_write, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(parent_out_read, HANDLE_FLAG_INHERIT, 0)) {
        cleanup_all();
        error_message = "无法设置 worker 管道继承属性";
        worker_disabled_ = true;
        return false;
    }

    wchar_t temp_dir[MAX_PATH];
    if (GetTempPathW(MAX_PATH, temp_dir) == 0 ||
        GetTempFileNameW(temp_dir, L"tbw", 0, stderr_path) == 0) {
        cleanup_all();
        error_message = "无法创建 worker 临时日志文件";
        worker_disabled_ = true;
        return false;
    }
    stderr_file = CreateFileW(stderr_path, GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (stderr_file == INVALID_HANDLE_VALUE) {
        cleanup_all();
        DeleteFileW(stderr_path);
        error_message = "无法创建 worker 日志句柄";
        worker_disabled_ = true;
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = child_in_read;
    si.hStdOutput = child_out_write;
    si.hStdError = stderr_file;

    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    const BOOL created = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    cleanup_child();
    if (!created) {
        const DWORD code = GetLastError();
        cleanup_all();
        DeleteFileW(stderr_path);
        error_message = "无法启动 Python worker（错误码 " + std::to_string(code) + "）";
        worker_disabled_ = true;
        return false;
    }
    CloseHandle(pi.hThread);

    std::string ready_line;
    if (!read_line_handle(parent_out_read, ready_line)) {
        cleanup_all();
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hProcess);
        const std::string err_text = read_whole_file(stderr_path);
        DeleteFileW(stderr_path);
        error_message = "Python worker 启动失败";
        if (!err_text.empty()) {
            error_message += "\n" + tail(err_text, 5);
        }
        worker_disabled_ = true;
        return false;
    }

    bool ready = false;
    std::string ready_error;
    try {
        const auto ready_json = nlohmann::json::parse(ready_line);
        ready = ready_json.value("ready", false);
        if (!ready && ready_json.contains("error") && ready_json["error"].is_string()) {
            ready_error = ready_json["error"].get<std::string>();
        }
    } catch (...) {
        ready_error = "worker 返回了无法解析的启动信息: " + ready_line.substr(0, 200);
    }

    if (!ready) {
        cleanup_all();
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hProcess);
        const std::string err_text = read_whole_file(stderr_path);
        DeleteFileW(stderr_path);
        error_message = ready_error.empty() ? "Python worker 未就绪" : ready_error;
        if (!err_text.empty()) {
            error_message += "\n" + tail(err_text, 5);
        }
        worker_disabled_ = true;
        return false;
    }

    auto state = std::make_unique<WorkerState>();
    state->process = pi.hProcess;
    state->stdin_write = parent_in_write;
    state->stdout_read = parent_out_read;
    state->stderr_path = stderr_path;
    worker_ = std::move(state);
    worker_key_ = key;
    return true;
}

bool PythonTranscriber::run_worker_job(const TranscribeParams& params,
                                       std::string& error_message,
                                       TranscribeStats* stats,
                                       bool& worker_broken) {
    worker_broken = false;
    if (!worker_ || !worker_->stdin_write || !worker_->stdout_read) {
        worker_broken = true;
        error_message = "Python worker 未启动";
        return false;
    }

    const std::string request = build_job_json(params).dump() + "\n";
    if (!write_all(worker_->stdin_write, request)) {
        worker_broken = true;
        error_message = "无法向 Python worker 发送任务";
        return false;
    }

    std::string response_line;
    if (!read_line_handle(worker_->stdout_read, response_line)) {
        worker_broken = true;
        error_message = "Python worker 没有返回结果（可能已崩溃）";
        const std::string err_text = read_worker_stderr();
        if (!err_text.empty()) {
            error_message += "\n" + tail(err_text, 8);
        }
        return false;
    }

    try {
        const auto summary = nlohmann::json::parse(response_line);
        if (!summary.value("ok", false)) {
            std::string detail = "推理失败";
            if (summary.contains("error") && summary["error"].is_string()) {
                detail = summary["error"].get<std::string>();
            }
            const std::string err_text = read_worker_stderr();
            if (!err_text.empty()) {
                detail += "\n" + tail(err_text, 5);
            }
            error_message = detail;
            return false;
        }
        fill_stats(summary, stats);
        return true;
    } catch (...) {
        worker_broken = true;
        error_message = "Python worker 返回了无法解析的结果: " +
                        response_line.substr(0, 200);
        return false;
    }
}

bool PythonTranscriber::transcribe(const TranscribeParams& params,
                                   std::string& error_message,
                                   TranscribeStats* stats) {
    if (params.use_worker && !worker_disabled_) {
        std::string worker_error;
        if (ensure_worker(params, worker_error)) {
            bool broken = false;
            if (run_worker_job(params, error_message, stats, broken)) {
                return true;
            }
            if (!broken) {
                return false;
            }
            stop_worker();
            worker_disabled_ = true;
            error_message.clear();
        }
    }

    return transcribe_process(params, error_message, stats);
}
