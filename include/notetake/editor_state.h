#pragma once

#include <cstddef>
#include <string>

namespace notetake
{
struct EditorMetrics
{
    std::size_t total_chars = 0;
    std::size_t total_words = 0;
    std::size_t total_lines = 1;
    std::size_t cursor_line = 1;
    std::size_t cursor_column = 1;
};

struct EditorState
{
    std::string text;
    std::string current_file_path;
    bool word_wrap = true;
    bool show_status_bar = true;
    float zoom_percent = 100.0f;
    std::size_t cursor_index = 0;
    EditorMetrics metrics;
};
} // namespace notetake
