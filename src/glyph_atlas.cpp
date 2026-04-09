#include "notetake/glyph_atlas.h"

#include <fmt/core.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace notetake
{

GlyphAtlas::~GlyphAtlas()
{
    if (m_texture != 0)
    {
        glDeleteTextures(1, &m_texture);
    }
}

bool GlyphAtlas::build(const std::string& font_path, unsigned int pixel_height)
{
    FT_Library ft{};
    if (FT_Init_FreeType(&ft) != 0)
    {
        fmt::print("GlyphAtlas: failed to init FreeType\n");
        return false;
    }

    FT_Face face{};
    if (FT_New_Face(ft, font_path.c_str(), 0, &face) != 0)
    {
        fmt::print("GlyphAtlas: failed to load font '{}'\n", font_path);
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, pixel_height);

    // Store ascender and line height in pixels
    m_ascender    = static_cast<int>(face->size->metrics.ascender  >> 6);
    m_line_height = static_cast<int>(face->size->metrics.height    >> 6);

    // --- First pass: measure total atlas dimensions ---
    constexpr int kPad = 1; // 1px padding between glyphs
    int row_w = 0;
    int row_h = 0;
    m_atlas_w = 0;
    m_atlas_h = 0;

    // Max atlas width (power-of-two friendly)
    constexpr int kMaxRowWidth = 1024;

    for (char32_t cp = 32; cp < 128; ++cp)
    {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER) != 0)
            continue;

        const int gw = static_cast<int>(face->glyph->bitmap.width) + kPad;
        const int gh = static_cast<int>(face->glyph->bitmap.rows)  + kPad;

        if (row_w + gw > kMaxRowWidth)
        {
            m_atlas_w  = std::max(m_atlas_w, row_w);
            m_atlas_h += row_h;
            row_w = 0;
            row_h = 0;
        }

        row_w += gw;
        row_h  = std::max(row_h, gh);
    }
    m_atlas_w  = std::max(m_atlas_w, row_w);
    m_atlas_h += row_h;

    // --- Allocate CPU-side atlas (single-channel R8) ---
    std::vector<uint8_t> pixels(static_cast<std::size_t>(m_atlas_w) * m_atlas_h, 0u);

    // --- Second pass: copy bitmaps into atlas, record UVs ---
    int pen_x = 0;
    int pen_y = 0;
    row_h     = 0;

    for (char32_t cp = 32; cp < 128; ++cp)
    {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER) != 0)
            continue;

        FT_GlyphSlot slot = face->glyph;
        const int gw = static_cast<int>(slot->bitmap.width);
        const int gh = static_cast<int>(slot->bitmap.rows);

        if (pen_x + gw + kPad > kMaxRowWidth)
        {
            pen_y += row_h;
            pen_x  = 0;
            row_h  = 0;
        }

        // Copy rows
        for (int row = 0; row < gh; ++row)
        {
            const std::size_t dst_offset =
                static_cast<std::size_t>(pen_y + row) * m_atlas_w + pen_x;
            std::memcpy(pixels.data() + dst_offset,
                        slot->bitmap.buffer + static_cast<std::size_t>(row) * slot->bitmap.width,
                        static_cast<std::size_t>(gw));
        }

        GlyphInfo info{};
        info.uv_x0     = static_cast<float>(pen_x)      / m_atlas_w;
        info.uv_y0     = static_cast<float>(pen_y)      / m_atlas_h;
        info.uv_x1     = static_cast<float>(pen_x + gw) / m_atlas_w;
        info.uv_y1     = static_cast<float>(pen_y + gh) / m_atlas_h;
        info.width     = gw;
        info.height    = gh;
        info.bearing_x = slot->bitmap_left;
        info.bearing_y = slot->bitmap_top;
        info.advance   = static_cast<int>(slot->advance.x >> 6);

        m_glyphs[cp] = info;

        pen_x += gw + kPad;
        row_h  = std::max(row_h, gh + kPad);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    // --- Upload to GPU ---
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                 m_atlas_w, m_atlas_h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

const GlyphInfo* GlyphAtlas::glyph(char32_t codepoint) const
{
    const auto it = m_glyphs.find(codepoint);
    if (it == m_glyphs.end())
        return nullptr;
    return &it->second;
}

} // namespace notetake
