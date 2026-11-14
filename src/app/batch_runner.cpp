#include "app/batch_runner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include "json.hpp"

namespace {

std::string lower_extension(const std::string& path) {
    std::string ext = std::filesystem::u8path(path).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string csv_field(const std::string& text) {
    if (text.find_first_of(",\"\r\n") == std::string::npos) {
        return text;
    }
    std::string result = "\"";
    for (char c : text) {
        if (c == '"') result += "\"\"";
        else result += c;
    }
    result += "\"";
    return result;
}

}  // namespace

BatchRunner::BatchRunner(JobFactory factory, BatchOptions options)
    : factory_(std::move(factory)), options_(std::move(options)) {}

BatchSummary BatchRunner::run(const std::vector<TranscribeParams>& tasks) {
    BatchSummary summary;
    summary.total = static_cast<int>(tasks.size());
    summary.items.resize(tasks.size());

    if (tasks.empty()) {
        return summary;
    }

    std::mutex log_mutex;

    auto process_one = [&](size_t index, TranscribeJob& job) {
        const TranscribeParams& task = tasks[index];
        BatchItemResult& item = summary.items[index];
        item.input = task.input_path;
        item.output = task.output_midi;
        item.stats.preset = task.preset;
        item.stats.separate = task.separate;

        std::error_code ec;
        const bool output_exists =
            std::filesystem::exists(std::filesystem::u8path(task.output_midi), ec);

        {
            std::lock_guard<std::mutex> lock(log_mutex);
            std::cout << "[" << (index + 1) << "/" << tasks.size() << "] "
                      << task.input_path;
            if (options_.skip_existing && output_exists) {
                std::cout << "（跳过：输出已存在）";
            }
            std::cout << std::endl;
        }

        if (options_.skip_existing && output_exists) {
            item.status = "skipped";
            std::lock_guard<std::mutex> lock(log_mutex);
            std::cout << "  -- 跳过 " << task.output_midi << std::endl;
            return;
        }

        const auto start = std::chrono::steady_clock::now();
        std::string error;
        TranscribeStats stats;
        const bool ok = job.run(task, error, &stats);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();

        item.elapsed_ms = static_cast<long long>(elapsed);
        item.stats = stats;
        item.stats.preset = task.preset;
        item.stats.separate = task.separate;

        if (ok) {
            item.status = "success";
            std::lock_guard<std::mutex> lock(log_mutex);
            std::cout << "  -> " << task.output_midi << " (" << elapsed << " ms)"
                      << std::endl;
        } else {
            item.status = "failed";
            item.error = error;
            std::lock_guard<std::mutex> lock(log_mutex);
            std::cerr << "  失败: " << error << std::endl;
        }
    };

    if (options_.jobs <= 1 || tasks.size() == 1) {
        std::unique_ptr<TranscribeJob> job = factory_();
        if (!job) {
            for (size_t i = 0; i < tasks.size(); ++i) {
                summary.items[i].input = tasks[i].input_path;
                summary.items[i].output = tasks[i].output_midi;
                summary.items[i].status = "failed";
                summary.items[i].error = "无法创建任务执行器";
            }
        } else {
            for (size_t i = 0; i < tasks.size(); ++i) {
                process_one(i, *job);
            }
        }
    } else {
        const size_t worker_count =
            std::min<size_t>(static_cast<size_t>(options_.jobs), tasks.size());
        std::atomic<size_t> next{0};
        std::vector<std::thread> threads;
        threads.reserve(worker_count);

        for (size_t t = 0; t < worker_count; ++t) {
            threads.emplace_back([&]() {
                std::unique_ptr<TranscribeJob> job = factory_();
                while (true) {
                    const size_t index = next.fetch_add(1);
                    if (index >= tasks.size()) break;
                    if (!job) {
                        BatchItemResult& item = summary.items[index];
                        item.input = tasks[index].input_path;
                        item.output = tasks[index].output_midi;
                        item.status = "failed";
                        item.error = "无法创建任务执行器";
                        std::lock_guard<std::mutex> lock(log_mutex);
                        std::cerr << "  失败: 无法创建任务执行器" << std::endl;
                        continue;
                    }
                    process_one(index, *job);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    }

    for (const BatchItemResult& item : summary.items) {
        if (item.status == "success") {
            ++summary.succeeded;
            summary.outputs.push_back(item.output);
        } else if (item.status == "skipped") {
            ++summary.skipped;
        } else {
            ++summary.failed;
        }
    }
    return summary;
}

bool write_batch_report(const std::string& path,
                        const BatchSummary& summary,
                        std::string& error_message) {
    if (path.empty()) {
        error_message = "报告路径为空";
        return false;
    }

    std::error_code ec;
    const auto fs_path = std::filesystem::u8path(path);
    if (!fs_path.parent_path().empty()) {
        std::filesystem::create_directories(fs_path.parent_path(), ec);
        if (ec) {
            error_message = "无法创建报告目录: " + fs_path.parent_path().u8string();
            return false;
        }
    }

    const std::string ext = lower_extension(path);
    if (ext == ".csv") {
        std::ofstream out(fs_path, std::ios::binary);
        if (!out) {
            error_message = "无法写入报告: " + path;
            return false;
        }
        out << "\xEF\xBB\xBF";
        out << "index,input,output,status,notes,bpm,preset,separate,elapsed_ms,error\n";
        for (size_t i = 0; i < summary.items.size(); ++i) {
            const BatchItemResult& item = summary.items[i];
            out << (i + 1) << ","
                << csv_field(item.input) << ","
                << csv_field(item.output) << ","
                << csv_field(item.status) << ","
                << item.stats.notes << ","
                << item.stats.bpm << ","
                << csv_field(item.stats.preset) << ","
                << csv_field(item.stats.separate) << ","
                << item.elapsed_ms << ","
                << csv_field(item.error) << "\n";
        }
        return true;
    }

    nlohmann::json root;
    root["total"] = summary.total;
    root["succeeded"] = summary.succeeded;
    root["failed"] = summary.failed;
    root["skipped"] = summary.skipped;
    root["items"] = nlohmann::json::array();
    for (const BatchItemResult& item : summary.items) {
        nlohmann::json entry;
        entry["input"] = item.input;
        entry["output"] = item.output;
        entry["status"] = item.status;
        entry["error"] = item.error;
        entry["elapsed_ms"] = item.elapsed_ms;
        entry["notes"] = item.stats.notes;
        entry["bpm"] = item.stats.bpm;
        entry["quantized"] = item.stats.quantized;
        entry["preset"] = item.stats.preset;
        entry["separate"] = item.stats.separate;
        root["items"].push_back(std::move(entry));
    }

    std::ofstream out(fs_path, std::ios::binary);
    if (!out) {
        error_message = "无法写入报告: " + path;
        return false;
    }
    out << root.dump(2) << "\n";
    return true;
}
