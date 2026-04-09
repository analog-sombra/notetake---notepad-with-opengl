#pragma once

#include "notetake/editor_state.h"
#include "notetake/text_renderer.h"
#include "notetake/glyph_atlas.h"

#include <GLFW/glfw3.h>

namespace notetake
{

struct EditorUiActions
{
    bool request_exit = false;
};

// Must be called once to bind GLFW callbacks.
void editor_ui_install_callbacks(GLFWwindow* window, EditorState* state);

// Called every frame. Renders the editor into the current OpenGL framebuffer.
EditorUiActions render_editor_ui(GLFWwindow* window,
                                  EditorState& state,
                                  TextRenderer& renderer,
                                  const GlyphAtlas& atlas);

} // namespace notetake
