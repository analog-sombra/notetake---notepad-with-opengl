#include "notetake/text_renderer.h"

#include <fmt/core.h>

#include <cstring>

namespace notetake
{
namespace
{

const char* kVertSrc = R"(
#version 330 core
layout(location = 0) in vec2  a_pos;
layout(location = 1) in vec2  a_uv;
layout(location = 2) in vec4  a_color;
layout(location = 3) in float a_use_tex;

uniform mat4 u_proj;

out vec2  v_uv;
out vec4  v_color;
out float v_use_tex;

void main()
{
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_uv        = a_uv;
    v_color     = a_color;
    v_use_tex   = a_use_tex;
}
)";

const char* kFragSrc = R"(
#version 330 core
in vec2  v_uv;
in vec4  v_color;
in float v_use_tex;

uniform sampler2D u_atlas;

out vec4 frag_color;

void main()
{
    if (v_use_tex > 0.5)
    {
        float alpha = texture(u_atlas, v_uv).r;
        frag_color  = vec4(v_color.rgb, v_color.a * alpha);
    }
    else
    {
        frag_color = v_color;
    }
}
)";

GLuint compile_shader(GLenum type, const char* src)
{
    const GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    GLint ok{};
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512]{};
        glGetShaderInfoLog(id, sizeof(log), nullptr, log);
        fmt::print("Shader compile error: {}\n", log);
        glDeleteShader(id);
        return 0;
    }
    return id;
}

GLuint link_program(GLuint vert, GLuint frag)
{
    const GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok{};
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512]{};
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fmt::print("Program link error: {}\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

} // namespace

TextRenderer::~TextRenderer()
{
    if (m_vao  != 0) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo  != 0) glDeleteBuffers(1, &m_vbo);
    if (m_prog != 0) glDeleteProgram(m_prog);
}

bool TextRenderer::init(const GlyphAtlas& atlas)
{
    m_atlas = &atlas;

    const GLuint vert = compile_shader(GL_VERTEX_SHADER,   kVertSrc);
    const GLuint frag = compile_shader(GL_FRAGMENT_SHADER, kFragSrc);
    if (vert == 0 || frag == 0)
        return false;

    m_prog = link_program(vert, frag);
    glDeleteShader(vert);
    glDeleteShader(frag);
    if (m_prog == 0)
        return false;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    constexpr GLsizei stride = sizeof(Vertex);
    // a_pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(Vertex, x)));
    // a_uv
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(Vertex, u)));
    // a_color
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(Vertex, r)));
    // a_use_tex
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<void*>(offsetof(Vertex, use_tex)));

    glBindVertexArray(0);

    m_vertices.reserve(4096);
    return true;
}

void TextRenderer::begin_frame(int viewport_w, int viewport_h)
{
    m_viewport_w = viewport_w;
    m_viewport_h = viewport_h;
    m_vertices.clear();
}

void TextRenderer::push_quad(float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1,
                              Color color, float use_tex)
{
    // Two triangles: TL-TR-BL, TR-BR-BL
    const Vertex tl{ x0, y0, u0, v0, color.r, color.g, color.b, color.a, use_tex };
    const Vertex tr{ x1, y0, u1, v0, color.r, color.g, color.b, color.a, use_tex };
    const Vertex bl{ x0, y1, u0, v1, color.r, color.g, color.b, color.a, use_tex };
    const Vertex br{ x1, y1, u1, v1, color.r, color.g, color.b, color.a, use_tex };

    m_vertices.push_back(tl);
    m_vertices.push_back(tr);
    m_vertices.push_back(bl);

    m_vertices.push_back(tr);
    m_vertices.push_back(br);
    m_vertices.push_back(bl);
}

void TextRenderer::draw_rect(float x, float y, float w, float h, Color color)
{
    push_quad(x, y, x + w, y + h, 0.0f, 0.0f, 0.0f, 0.0f, color, 0.0f);
}

float TextRenderer::draw_text(std::string_view text, float x, float baseline_y,
                               Color color, float word_wrap_width, float zoom)
{
    const float start_x = x;
    const float line_h  = static_cast<float>(m_atlas->line_height()) * zoom;

    // Helper: measure one "word" (up to next space/newline or end)
    auto word_width = [&](std::size_t from) -> float
    {
        float w = 0.0f;
        for (std::size_t i = from; i < text.size(); ++i)
        {
            const char ch = text[i];
            if (ch == ' ' || ch == '\n') break;
            const GlyphInfo* g = m_atlas->glyph(static_cast<char32_t>(ch));
            if (g) w += static_cast<float>(g->advance) * zoom;
        }
        return w;
    };

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char ch = text[i];

        if (ch == '\n')
        {
            x = start_x;
            baseline_y += line_h;
            continue;
        }

        // Word-wrap: if a new word won't fit, break to next visual line
        if (word_wrap_width > 0.0f && ch != ' ')
        {
            // Are we starting a new word?
            const bool at_word_start = (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\n');
            if (at_word_start)
            {
                const float ww = word_width(i);
                if (x > start_x && (x + ww) > (start_x + word_wrap_width))
                {
                    x = start_x;
                    baseline_y += line_h;
                }
            }
        }

        const GlyphInfo* g = m_atlas->glyph(static_cast<char32_t>(ch));
        if (!g)
        {
            // Advance by a fixed amount for unknown glyphs
            x += static_cast<float>(m_atlas->line_height()) * 0.5f * zoom;
            continue;
        }

        if (g->width > 0 && g->height > 0)
        {
            const float gx = x + static_cast<float>(g->bearing_x) * zoom;
            const float gy = baseline_y - static_cast<float>(g->bearing_y) * zoom;

            push_quad(gx, gy,
                      gx + static_cast<float>(g->width)  * zoom,
                      gy + static_cast<float>(g->height) * zoom,
                      g->uv_x0, g->uv_y0, g->uv_x1, g->uv_y1,
                      color, 1.0f);
        }

        x += static_cast<float>(g->advance) * zoom;
    }

    return x;
}

float TextRenderer::measure_text(std::string_view text) const
{
    float w = 0.0f;
    for (const char ch : text)
    {
        if (ch == '\n') break;
        const GlyphInfo* g = m_atlas->glyph(static_cast<char32_t>(ch));
        if (g) w += static_cast<float>(g->advance);
    }
    return w;
}

float TextRenderer::measure_text_n(std::string_view text, std::size_t byte_count) const
{
    float w = 0.0f;
    const std::size_t limit = std::min(byte_count, text.size());
    for (std::size_t i = 0; i < limit; ++i)
    {
        const char ch = text[i];
        if (ch == '\n') break;
        const GlyphInfo* g = m_atlas->glyph(static_cast<char32_t>(ch));
        if (g) w += static_cast<float>(g->advance);
    }
    return w;
}

void TextRenderer::flush()
{
    if (m_vertices.empty())
        return;

    // Build ortho projection: top-left (0,0), bottom-right (w,h)
    const float W = static_cast<float>(m_viewport_w);
    const float H = static_cast<float>(m_viewport_h);
    // column-major orthographic matrix
    const float proj[16] = {
         2.0f/W,  0.0f,   0.0f, 0.0f,
         0.0f,  -2.0f/H,  0.0f, 0.0f,
         0.0f,   0.0f,  -1.0f, 0.0f,
        -1.0f,   1.0f,   0.0f, 1.0f,
    };

    glUseProgram(m_prog);
    glUniformMatrix4fv(glGetUniformLocation(m_prog, "u_proj"), 1, GL_FALSE, proj);
    glUniform1i(glGetUniformLocation(m_prog, "u_atlas"), 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlas->texture_id());

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_vertices.size() * sizeof(Vertex)),
                 m_vertices.data(),
                 GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));

    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);

    m_vertices.clear();
}

} // namespace notetake
