#pragma once

#include <string>
#include <vector>

#include "app/transcribe_job.h"
#include "domain/params.h"

struct BatchSummary {
    int total = 0;
    int succeeded = 0;
    int failed = 0;
    std::vector<std::string> outputs;
};

// 应用层：顺序执行多个扒谱任务，汇总结果。
class BatchRunner {
public:
    explicit BatchRunner(TranscribeJob& job);

    BatchSummary run(const std::vector<TranscribeParams>& tasks);

private:
    TranscribeJob& job_;
};
