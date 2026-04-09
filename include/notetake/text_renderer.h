#pragma once

#include "notetake/glyph_atlas.h"

#include <glad/glad.h>

#include <cstddef>
#include <string_view>
#include <vector>

namespace notetake
{

struct Color
{
    float r, g, b, a;
};

// Batches text quads and draws them in a single draw call.
// Call begin_frame() each frame, then draw_text() / draw_rect() as needed,
// then flush() to submit to the GPU.
class TextRenderer
{
public:
    TextRenderer() = default;
    ~TextRenderer();

    TextRenderer(const TextRenderer&)            = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    TextRenderer(TextRenderer&&)                 = delete;
    TextRenderer& operator=(TextRenderer&&)      = delete;

    // Must be called once after OpenGL is ready.
    bool init(const GlyphAtlas& atlas);

    // Call at the start of every frame.
    void begin_frame(int viewport_w, int viewport_h);

    // Queue a filled axis-aligned rectangle (no texture).
    void draw_rect(float x, float y, float w, float h, Color color);

    // Queue text starting at (x, baseline_y). Returns the x position after the last glyph.
    // word_wrap_width <= 0 means no wrapping. zoom scales glyph size and advance.
    float draw_text(std::string_view text, float x, float baseline_y,
                    Color color, float word_wrap_width = -1.0f, float zoom = 1.0f);

    // Measure the width of a single line of text (no wrap).
    float measure_text(std::string_view text) const;

    // Measure glyph advance up to byte index `byte_count` in text.
    float measure_text_n(std::string_view text, std::size_t byte_count) const;

    // Submit all queued geometry to the GPU and clear the batch.
    void flush();

private:
    struct Vertex
    {
        float x, y;
        float u, v;
        float r, g, b, a;
        float use_tex; // 1.0 = sample texture, 0.0 = solid colour
    };

    void push_quad(float x0, float y0, float x1, float y1,
                   float u0, float v0, float u1, float v1,
                   Color color, float use_tex);

    const GlyphAtlas* m_atlas = nullptr;

    GLuint m_vao  = 0;
    GLuint m_vbo  = 0;
    GLuint m_prog = 0;

    int m_viewport_w = 0;
    int m_viewport_h = 0;

    std::vector<Vertex> m_vertices;
};

} // namespace notetake
