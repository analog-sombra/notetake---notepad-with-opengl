#pragma once

#include "notetake/editor_state.h"

namespace notetake
{
struct EditorUiActions
{
	bool request_exit = false;
};

EditorUiActions render_editor_ui(EditorState& state);
} // namespace notetake
