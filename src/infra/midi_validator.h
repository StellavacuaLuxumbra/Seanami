#pragma once

#include <string>

// 校验 Python 侧产出的 MIDI 文件是否可用。
bool validate_midi_file(const std::string& path, std::string& error_message);
