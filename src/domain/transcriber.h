#pragma once

#include <string>

#include "domain/params.h"

// 推理后端抽象。第一版由 PythonTranscriber 实现（子进程），
// 未来可替换为 ONNX Runtime 等实现而不影响上层。
class ITranscriber {
public:
    virtual ~ITranscriber() = default;

    // 成功返回 true；失败时 error_message 包含可读的错误原因。
    virtual bool transcribe(const TranscribeParams& params,
                            std::string& error_message) = 0;
};
