#include "cli/progress.h"

#include <iostream>

void Progress::info(const std::string& message) const {
    std::cout << "[tab_tool] " << message << std::endl;
}

void Progress::detail(const std::string& message) const {
    if (verbose_) {
        std::cout << "[tab_tool] " << message << std::endl;
    }
}

void Progress::error(const std::string& message) const {
    std::cerr << "[tab_tool] 错误: " << message << std::endl;
}
