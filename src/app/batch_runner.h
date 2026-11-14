#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/transcribe_job.h"
#include "domain/params.h"

struct BatchOptions {
    int jobs = 1;
    bool skip_existing = false;
    std::string report_path;
};

struct BatchItemResult {
    std::string input;
    std::string output;
    std::string status;  // success / failed / skipped
    std::string error;
    long long elapsed_ms = 0;
    TranscribeStats stats;
};

struct BatchSummary {
    int total = 0;
    int succeeded = 0;
    int failed = 0;
    int skipped = 0;
    std::vector<std::string> outputs;
    std::vector<BatchItemResult> items;
};

// 应用层：执行多个扒谱任务，支持顺序、并行、跳过已存在文件。
class BatchRunner {
public:
    using JobFactory = std::function<std::unique_ptr<TranscribeJob>()>;

    BatchRunner(JobFactory factory, BatchOptions options);

    BatchSummary run(const std::vector<TranscribeParams>& tasks);

private:
    JobFactory factory_;
    BatchOptions options_;
};

// 根据扩展名写 JSON 或 CSV（.csv 写 CSV，其余写 JSON）。
bool write_batch_report(const std::string& path,
                        const BatchSummary& summary,
                        std::string& error_message);
