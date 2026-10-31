#pragma once

#include <string>

#include "domain/params.h"
#include "domain/transcriber.h"

// 通过子进程调用 Python 脚本完成推理。
// 不链接 Python 开发库，进程隔离，崩溃不影响主程序。
class PythonTranscriber : public ITranscriber {
public:
    bool transcribe(const TranscribeParams& params,
                    std::string& error_message) override;

private:
    struct ProcessResult {
        bool launched = false;
        unsigned long exit_code = 0;
        std::string stdout_text;
        std::string stderr_text;
        std::string launch_error;
    };

    ProcessResult run_process(const std::wstring& command_line);
};
