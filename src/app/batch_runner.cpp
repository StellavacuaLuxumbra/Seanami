#include "app/batch_runner.h"

#include <iostream>

BatchRunner::BatchRunner(TranscribeJob& job) : job_(job) {}

BatchSummary BatchRunner::run(const std::vector<TranscribeParams>& tasks) {
    BatchSummary summary;
    summary.total = static_cast<int>(tasks.size());

    for (size_t i = 0; i < tasks.size(); ++i) {
        const TranscribeParams& task = tasks[i];
        std::cout << "[" << (i + 1) << "/" << tasks.size() << "] " << task.input_path
                  << std::endl;

        std::string error;
        if (job_.run(task, error)) {
            ++summary.succeeded;
            summary.outputs.push_back(task.output_midi);
            std::cout << "  -> " << task.output_midi << std::endl;
        } else {
            ++summary.failed;
            std::cerr << "  失败: " << error << std::endl;
        }
    }

    return summary;
}
