#pragma once

#include <glad/glad.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <string>
#include <unordered_map>

namespace notetake
{

struct GlyphInfo
{
    float uv_x0; // left UV
    float uv_y0; // top UV
    float uv_x1; // right UV
    float uv_y1; // bottom UV
    int   width;
    int   height;
    int   bearing_x; // offset from baseline to left
    int   bearing_y; // offset from baseline to top
    int   advance;   // horizontal advance in pixels (26.6 fixed >> 6)
};

// Builds a GPU texture atlas from a .ttf font using FreeType.
// Covers ASCII 32-126. Call build() once after OpenGL is initialised.
class GlyphAtlas
{
public:
    GlyphAtlas() = default;
    ~GlyphAtlas();

    GlyphAtlas(const GlyphAtlas&)            = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;
    GlyphAtlas(GlyphAtlas&&)                 = delete;
    GlyphAtlas& operator=(GlyphAtlas&&)      = delete;

    // Load font from path and rasterise glyphs at the given pixel height.
    // Returns false on failure.
    bool build(const std::string& font_path, unsigned int pixel_height);

    const GlyphInfo* glyph(char32_t codepoint) const;

    GLuint texture_id()     const { return m_texture; }
    int    atlas_width()    const { return m_atlas_w; }
    int    atlas_height()   const { return m_atlas_h; }
    int    line_height()    const { return m_line_height; }
    int    ascender()       const { return m_ascender; }

private:
    GLuint m_texture      = 0;
    int    m_atlas_w      = 0;
    int    m_atlas_h      = 0;
    int    m_line_height  = 0;
    int    m_ascender     = 0;
    std::unordered_map<char32_t, GlyphInfo> m_glyphs;
};

} // namespace notetake
