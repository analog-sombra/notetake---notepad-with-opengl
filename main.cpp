#include <stdlib.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <fmt/core.h>

#include "notetake/editor_state.h"
#include "notetake/editor_ui.h"
#include "notetake/glyph_atlas.h"
#include "notetake/text_renderer.h"

static void framebuffer_size_callback(GLFWwindow* /*window*/, int width, int height)
{
    glViewport(0, 0, width, height);
}

// Locate a system font that actually exists.
static std::string find_font()
{
#ifdef _WIN32
    const char* candidates[] = {
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    for (const char* p : candidates)
    {
        FILE* f = fopen(p, "rb");
        if (f) { fclose(f); return p; }
    }
#endif
    return {};
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (!glfwInit())
    {
        fmt::print("Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1024, 768, "NoteTake", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        fmt::print("Failed to create GLFW window\n");
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
    {
        fmt::print("Failed to initialize GLAD\n");
        return EXIT_FAILURE;
    }

    int fb_w{}, fb_h{};
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // -------------------------------------------------------------------------
    // Build font atlas
    // -------------------------------------------------------------------------
    const std::string font_path = find_font();
    if (font_path.empty())
    {
        fmt::print("No suitable font found. Please put consola.ttf / arial.ttf in C:/Windows/Fonts/\n");
        return EXIT_FAILURE;
    }

    notetake::GlyphAtlas atlas;
    if (!atlas.build(font_path, 16))
    {
        fmt::print("Failed to build glyph atlas from '{}'\n", font_path);
        return EXIT_FAILURE;
    }

    notetake::TextRenderer renderer;
    if (!renderer.init(atlas))
    {
        fmt::print("Failed to initialise TextRenderer\n");
        return EXIT_FAILURE;
    }

    notetake::EditorState editor_state;

    notetake::editor_ui_install_callbacks(window, &editor_state);

    // -------------------------------------------------------------------------
    // Main loop
    // -------------------------------------------------------------------------
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        int w{}, h{};
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);

        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const notetake::EditorUiActions actions =
            notetake::render_editor_ui(window, editor_state, renderer, atlas);

        if (actions.request_exit)
            glfwSetWindowShouldClose(window, GLFW_TRUE);

        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return EXIT_SUCCESS;
}