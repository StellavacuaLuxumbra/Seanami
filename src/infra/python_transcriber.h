#pragma once

#include <memory>
#include <string>

#include "domain/params.h"
#include "domain/transcriber.h"

// 通过子进程调用 Python 脚本完成推理。
// 不链接 Python 开发库，进程隔离，崩溃不影响主程序。
// 批量处理时自动使用常驻 worker：模型只加载一次，文件之间复用。
class PythonTranscriber : public ITranscriber {
public:
    PythonTranscriber();
    ~PythonTranscriber() override;

    PythonTranscriber(const PythonTranscriber&) = delete;
    PythonTranscriber& operator=(const PythonTranscriber&) = delete;

    bool transcribe(const TranscribeParams& params,
                    std::string& error_message,
                    TranscribeStats* stats = nullptr) override;

private:
    struct ProcessResult {
        bool launched = false;
        unsigned long exit_code = 0;
        std::string stdout_text;
        std::string stderr_text;
        std::string launch_error;
    };

    struct WorkerState;

    ProcessResult run_process(const std::wstring& command_line);
    bool transcribe_process(const TranscribeParams& params,
                            std::string& error_message,
                            TranscribeStats* stats);
    bool ensure_worker(const TranscribeParams& params, std::string& error_message);
    bool run_worker_job(const TranscribeParams& params,
                        std::string& error_message,
                        TranscribeStats* stats,
                        bool& worker_broken);
    void stop_worker();
    std::string read_worker_stderr() const;

    std::unique_ptr<WorkerState> worker_;
    std::string worker_key_;
    bool worker_disabled_ = false;
};
