#pragma once

#include <cstddef>
#include <string_view>

#include "notetake/editor_state.h"

namespace notetake
{
EditorMetrics calculate_editor_metrics(std::string_view text, std::size_t cursor_index);
} // namespace notetake
