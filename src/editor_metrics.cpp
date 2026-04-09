#include "notetake/editor_metrics.h"

#include <algorithm>
#include <cctype>

namespace notetake
{
namespace
{
std::size_t count_words(std::string_view text)
{
    std::size_t total_words = 0;
    bool in_word = false;

    for (const char ch : text)
    {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0)
        {
            in_word = false;
            continue;
        }

        if (!in_word)
        {
            ++total_words;
            in_word = true;
        }
    }

    return total_words;
}
} // namespace

EditorMetrics calculate_editor_metrics(std::string_view text, std::size_t cursor_index)
{
    EditorMetrics metrics;
    metrics.total_chars = text.size();
    metrics.total_words = count_words(text);
    metrics.total_lines = 1;

    const std::size_t clamped_cursor = std::min(cursor_index, text.size());

    metrics.cursor_line = 1;
    metrics.cursor_column = 1;

    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] == '\n')
        {
            ++metrics.total_lines;
        }

        if (index >= clamped_cursor)
        {
            continue;
        }

        if (text[index] == '\n')
        {
            ++metrics.cursor_line;
            metrics.cursor_column = 1;
            continue;
        }

        ++metrics.cursor_column;
    }

    return metrics;
}
} // namespace notetake
