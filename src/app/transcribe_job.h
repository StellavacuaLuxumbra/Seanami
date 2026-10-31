#pragma once

#include <memory>
#include <string>

#include "domain/params.h"
#include "domain/transcriber.h"
#include "infra/audio_decoder.h"

// 应用层：编排“解码 -> 推理 -> 校验 -> 清理”的完整流程。
class TranscribeJob {
public:
    TranscribeJob(std::unique_ptr<IAudioDecoder> decoder,
                  std::unique_ptr<ITranscriber> transcriber);

    bool run(const TranscribeParams& params, std::string& error_message);

private:
    std::unique_ptr<IAudioDecoder> decoder_;
    std::unique_ptr<ITranscriber> transcriber_;
};
