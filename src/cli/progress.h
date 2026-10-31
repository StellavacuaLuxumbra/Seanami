#pragma once

#include <string>

// 极简日志封装：统一前缀与输出目标，verbose 控制细节。
class Progress {
public:
    explicit Progress(bool verbose) : verbose_(verbose) {}

    void info(const std::string& message) const;
    void detail(const std::string& message) const;
    void error(const std::string& message) const;

private:
    bool verbose_;
};
