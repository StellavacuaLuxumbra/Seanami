#include "cli/progress.h"

#include <iostream>

void Progress::info(const std::string& message) const {
    std::cout << "[Seanami] " << message << std::endl;
}

void Progress::detail(const std::string& message) const {
    if (verbose_) {
        std::cout << "[Seanami] " << message << std::endl;
    }
}

void Progress::error(const std::string& message) const {
    std::cerr << "[Seanami] 错误: " << message << std::endl;
}
