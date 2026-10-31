#include "infra/midi_validator.h"

#include <filesystem>
#include <fstream>
#include <string>

bool validate_midi_file(const std::string& path, std::string& error_message) {
    std::error_code ec;
    const auto fs_path = std::filesystem::u8path(path);

    if (!std::filesystem::exists(fs_path, ec)) {
        error_message = "输出 MIDI 文件不存在: " + path;
        return false;
    }

    const auto size = std::filesystem::file_size(fs_path, ec);
    if (ec || size < 14) {
        error_message = "输出 MIDI 文件为空或过小";
        return false;
    }

    std::ifstream ifs(fs_path, std::ios::binary);
    if (!ifs) {
        error_message = "无法读取输出 MIDI 文件";
        return false;
    }

    char header[4] = {};
    ifs.read(header, 4);
    if (!ifs || std::string(header, 4) != "MThd") {
        error_message = "输出文件不是有效的 MIDI（缺少 MThd 头）";
        return false;
    }

    return true;
}
