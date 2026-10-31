#pragma once

#include <string>
#include <vector>

#include "domain/params.h"

struct CliResult {
    bool ok = true;
    bool show_help = false;
    bool show_version = false;
    bool recursive = false;
    bool open_after = false;
    bool output_explicit = false;
    TranscribeParams params;
    std::string error;
};

// args[0] 为程序名，其余为参数（UTF-8）。
CliResult parse_args(const std::vector<std::string>& args);

std::string help_text();
std::string version_text();
