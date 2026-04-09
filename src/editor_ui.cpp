#include "notetake/editor_ui.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>

#include <fmt/format.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

#include "notetake/editor_metrics.h"

namespace notetake
{
namespace
{

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------
bool read_text_file(const std::string& path, std::string& content)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool write_text_file(const std::string& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

std::string open_file_dialog()
{
#ifdef _WIN32
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn   = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.lpstrFile       = path;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrFilter     = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.nFilterIndex    = 1;
    ofn.Flags           = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameA(&ofn)) return std::string(path);
#endif
    return {};
}

std::string save_file_dialog(const std::string& current_path)
{
#ifdef _WIN32
    char path[MAX_PATH] = {};
    if (!current_path.empty())
    {
        const std::size_t n = std::min(current_path.size(), static_cast<std::size_t>(MAX_PATH - 1));
        std::copy_n(current_path.data(), n, path);
    }
    OPENFILENAMEA ofn = {};
    ofn.lStructSize   = sizeof(ofn);
    ofn.lpstrFile     = path;
    ofn.nMaxFile      = MAX_PATH;
    ofn.lpstrFilter   = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.nFilterIndex  = 1;
    ofn.Flags         = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    ofn.lpstrDefExt   = "txt";
    if (GetSaveFileNameA(&ofn)) return std::string(path);
#endif
    return {};
}

// ---------------------------------------------------------------------------
// Text editing helpers
// ---------------------------------------------------------------------------

// Insert a string into text at byte position idx, returns new cursor position.
std::size_t text_insert(std::string& text, std::size_t idx, std::string_view ins)
{
    idx = std::min(idx, text.size());
    text.insert(idx, ins);
    return idx + ins.size();
}

// Delete one code unit before cursor. Returns new cursor position.
std::size_t text_backspace(std::string& text, std::size_t idx)
{
    if (idx == 0 || text.empty()) return idx;
    text.erase(idx - 1, 1);
    return idx - 1;
}

// Delete one code unit at cursor.
void text_delete(std::string& text, std::size_t idx)
{
    if (idx < text.size())
        text.erase(idx, 1);
}

// Move cursor left one position.
std::size_t cursor_left(const std::string& text, std::size_t idx)
{
    return (idx > 0) ? idx - 1 : 0;
}

// Move cursor right one position.
std::size_t cursor_right(const std::string& text, std::size_t idx)
{
    return (idx < text.size()) ? idx + 1 : idx;
}

// Find start of the logical line containing idx.
std::size_t line_start(std::string_view text, std::size_t idx)
{
    idx = std::min(idx, text.size());
    while (idx > 0 && text[idx - 1] != '\n') --idx;
    return idx;
}

// Find end of the logical line containing idx (points at '\n' or text.size()).
std::size_t line_end(std::string_view text, std::size_t idx)
{
    idx = std::min(idx, text.size());
    while (idx < text.size() && text[idx] != '\n') ++idx;
    return idx;
}

// Delete selection [lo, hi) from text, return new cursor position.
std::size_t delete_selection(std::string& text, std::size_t lo, std::size_t hi)
{
    if (lo > hi) std::swap(lo, hi);
    lo = std::min(lo, text.size());
    hi = std::min(hi, text.size());
    text.erase(lo, hi - lo);
    return lo;
}

// ---------------------------------------------------------------------------
} // namespace

// ---------------------------------------------------------------------------
// GLFW callbacks
// ---------------------------------------------------------------------------

static EditorState* s_state = nullptr;

static void char_callback(GLFWwindow* /*w*/, unsigned int codepoint)
{
    if (!s_state) return;
    EditorState& st = *s_state;

    // Replace selection if one exists
    if (st.sel_anchor != st.sel_cursor)
    {
        const std::size_t lo = std::min(st.sel_anchor, st.sel_cursor);
        const std::size_t hi = std::max(st.sel_anchor, st.sel_cursor);
        st.cursor_index = delete_selection(st.text, lo, hi);
        st.sel_anchor = st.sel_cursor = st.cursor_index;
    }

    // Only handle ASCII printable for now
    if (codepoint < 128)
    {
        const char ch = static_cast<char>(codepoint);
        st.cursor_index = text_insert(st.text, st.cursor_index, std::string_view(&ch, 1));
        st.sel_anchor = st.sel_cursor = st.cursor_index;
    }
}

static void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int mods)
{
    if (!s_state) return;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    EditorState& st = *s_state;
    const bool ctrl  = (mods & GLFW_MOD_CONTROL) != 0;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;

    auto clear_sel = [&]{ st.sel_anchor = st.sel_cursor = st.cursor_index; };

    // ----- Ctrl shortcuts -----
    if (ctrl)
    {
        switch (key)
        {
        case GLFW_KEY_N:
            st.text.clear();
            st.current_file_path.clear();
            st.cursor_index = 0;
            st.sel_anchor = st.sel_cursor = 0;
            st.scroll_y = 0.0f;
            return;
        case GLFW_KEY_O:
            {
                const std::string path = open_file_dialog();
                if (!path.empty())
                {
                    std::string content;
                    if (read_text_file(path, content))
                    {
                        st.text = std::move(content);
                        st.current_file_path = path;
                        st.cursor_index = 0;
                        st.sel_anchor = st.sel_cursor = 0;
                        st.scroll_y = 0.0f;
                    }
                }
            }
            return;
        case GLFW_KEY_S:
            {
                std::string path = st.current_file_path;
                if (path.empty()) path = save_file_dialog(path);
                if (!path.empty() && write_text_file(path, st.text))
                    st.current_file_path = path;
            }
            return;
        case GLFW_KEY_A:
            // Select all
            st.sel_anchor = 0;
            st.sel_cursor = st.text.size();
            st.cursor_index = st.text.size();
            return;
        case GLFW_KEY_C:
            if (st.sel_anchor != st.sel_cursor)
            {
                const std::size_t lo = std::min(st.sel_anchor, st.sel_cursor);
                const std::size_t hi = std::max(st.sel_anchor, st.sel_cursor);
                const std::string selected = st.text.substr(lo, hi - lo);
                glfwSetClipboardString(window, selected.c_str());
            }
            return;
        case GLFW_KEY_X:
            if (st.sel_anchor != st.sel_cursor)
            {
                const std::size_t lo = std::min(st.sel_anchor, st.sel_cursor);
                const std::size_t hi = std::max(st.sel_anchor, st.sel_cursor);
                const std::string selected = st.text.substr(lo, hi - lo);
                glfwSetClipboardString(window, selected.c_str());
                st.cursor_index = delete_selection(st.text, lo, hi);
                st.sel_anchor = st.sel_cursor = st.cursor_index;
            }
            return;
        case GLFW_KEY_V:
            {
                const char* clip = glfwGetClipboardString(window);
                if (clip)
                {
                    if (st.sel_anchor != st.sel_cursor)
                    {
                        const std::size_t lo = std::min(st.sel_anchor, st.sel_cursor);
                        const std::size_t hi = std::max(st.sel_anchor, st.sel_cursor);
                        st.cursor_index = delete_selection(st.text, lo, hi);
                    }
                    st.cursor_index = text_insert(st.text, st.cursor_index, clip);
                    st.sel_anchor = st.sel_cursor = st.cursor_index;
                }
            }
            return;
        case GLFW_KEY_EQUAL: // Ctrl++
            st.zoom_percent = std::clamp(st.zoom_percent + 10.0f, 25.0f, 300.0f);
            return;
        case GLFW_KEY_MINUS: // Ctrl+-
            st.zoom_percent = std::clamp(st.zoom_percent - 10.0f, 25.0f, 300.0f);
            return;
        case GLFW_KEY_0:
            st.zoom_percent = 100.0f;
            return;
        default: break;
        }
    }

    // ----- Navigation / editing -----
    switch (key)
    {
    case GLFW_KEY_LEFT:
        if (!shift && st.sel_anchor != st.sel_cursor)
        {
            // Jump to left edge of selection
            st.cursor_index = std::min(st.sel_anchor, st.sel_cursor);
            clear_sel();
        }
        else
        {
            st.cursor_index = cursor_left(st.text, st.cursor_index);
            if (shift) st.sel_cursor = st.cursor_index;
            else       clear_sel();
        }
        break;
    case GLFW_KEY_RIGHT:
        if (!shift && st.sel_anchor != st.sel_cursor)
        {
            st.cursor_index = std::max(st.sel_anchor, st.sel_cursor);
            clear_sel();
        }
        else
        {
            st.cursor_index = cursor_right(st.text, st.cursor_index);
            if (shift) st.sel_cursor = st.cursor_index;
            else       clear_sel();
        }
        break;
    case GLFW_KEY_HOME:
        st.cursor_index = line_start(st.text, st.cursor_index);
        if (shift) st.sel_cursor = st.cursor_index;
        else       clear_sel();
        break;
    case GLFW_KEY_END:
        st.cursor_index = line_end(st.text, st.cursor_index);
        if (shift) st.sel_cursor = st.cursor_index;
        else       clear_sel();
        break;
    case GLFW_KEY_BACKSPACE:
        if (st.sel_anchor != st.sel_cursor)
        {
            const std::size_t lo = std::min(st.sel_anchor, st.sel_cursor);
            const std::size_t hi = std::max(st.sel_anchor, st.sel_cursor);
            st.cursor_index = delete_selection(st.text, lo, hi);
            clear_sel();
        }
        else
        {
            st.cursor_index = text_backspace(st.text, st.cursor_index);
            clear_sel();
        }
        break;
    case GLFW_KEY_DELETE:
        if (st.sel_anchor != st.sel_cursor)
        {
            const std::size_t lo = std::min(st.sel_anchor, st.sel_cursor);
            const std::size_t hi = std::max(st.sel_anchor, st.sel_cursor);
            st.cursor_index = delete_selection(st.text, lo, hi);
            clear_sel();
        }
        else
        {
            text_delete(st.text, st.cursor_index);
            clear_sel();
        }
        break;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
        if (st.sel_anchor != st.sel_cursor)
        {
            const std::size_t lo = std::min(st.sel_anchor, st.sel_cursor);
            const std::size_t hi = std::max(st.sel_anchor, st.sel_cursor);
            st.cursor_index = delete_selection(st.text, lo, hi);
            clear_sel();
        }
        st.cursor_index = text_insert(st.text, st.cursor_index, "\n");
        clear_sel();
        break;
    case GLFW_KEY_TAB:
        if (st.sel_anchor != st.sel_cursor)
        {
            const std::size_t lo = std::min(st.sel_anchor, st.sel_cursor);
            const std::size_t hi = std::max(st.sel_anchor, st.sel_cursor);
            st.cursor_index = delete_selection(st.text, lo, hi);
            clear_sel();
        }
        st.cursor_index = text_insert(st.text, st.cursor_index, "    ");
        clear_sel();
        break;
    case GLFW_KEY_ESCAPE:
        clear_sel();
        break;
    default: break;
    }
}

static void scroll_callback(GLFWwindow* /*w*/, double /*dx*/, double dy)
{
    if (!s_state) return;
    s_state->scroll_y -= static_cast<float>(dy) * 30.0f;
    if (s_state->scroll_y < 0.0f) s_state->scroll_y = 0.0f;
}

void editor_ui_install_callbacks(GLFWwindow* window, EditorState* state)
{
    s_state = state;
    glfwSetCharCallback(window, char_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetScrollCallback(window, scroll_callback);
}

// ---------------------------------------------------------------------------
// Mouse click → byte index helper
// ---------------------------------------------------------------------------
static std::size_t click_to_index(const EditorState& st,
                                   const GlyphAtlas& atlas,
                                   float text_x, float text_y,
                                   float text_w,
                                   float click_x, float click_y,
                                   float zoom)
{
    const float lh   = static_cast<float>(atlas.line_height()) * zoom;
    const float asc  = static_cast<float>(atlas.ascender())    * zoom;

    // relative to text area origin, adjusted for scroll
    const float rel_y = click_y - text_y + st.scroll_y;
    const float rel_x = click_x - text_x;

    // Which visual line was clicked?
    int click_visual_line = static_cast<int>(rel_y / lh);
    if (click_visual_line < 0) click_visual_line = 0;

    // Walk the text, track visual line and x position
    int   visual_line = 0;
    float pen_x       = 0.0f;
    const float wrap_limit = (st.word_wrap && text_w > 0.0f) ? text_w : -1.0f;

    std::size_t best_idx = 0;

    auto word_w_from = [&](std::size_t from) -> float
    {
        float w = 0.0f;
        for (std::size_t k = from; k < st.text.size(); ++k)
        {
            const char c = st.text[k];
            if (c == ' ' || c == '\n') break;
            const GlyphInfo* g = atlas.glyph(static_cast<char32_t>(c));
            if (g) w += static_cast<float>(g->advance) * zoom;
        }
        return w;
    };

    for (std::size_t i = 0; i <= st.text.size(); ++i)
    {
        // Once we've passed the clicked line and the cursor hasn't moved past it
        if (visual_line > click_visual_line)
            break;

        if (visual_line == click_visual_line)
        {
            // Is this position close enough horizontally?
            if (pen_x >= rel_x)
            {
                best_idx = i;
                break;
            }
            best_idx = i; // keep updating; final value is end of this line
        }

        if (i == st.text.size()) break;

        const char ch = st.text[i];

        if (ch == '\n')
        {
            if (visual_line == click_visual_line)
            {
                best_idx = i;
                break;
            }
            ++visual_line;
            pen_x = 0.0f;
            continue;
        }

        // Word-wrap check
        if (wrap_limit > 0.0f && ch != ' ')
        {
            const bool at_word_start = (i == 0 || st.text[i-1] == ' ' || st.text[i-1] == '\n');
            if (at_word_start && pen_x > 0.0f)
            {
                const float ww = word_w_from(i);
                if (pen_x + ww > wrap_limit)
                {
                    if (visual_line == click_visual_line)
                    {
                        best_idx = i;
                        break;
                    }
                    ++visual_line;
                    pen_x = 0.0f;
                }
            }
        }

        const GlyphInfo* g = atlas.glyph(static_cast<char32_t>(ch));
        if (g) pen_x += static_cast<float>(g->advance) * zoom;
    }

    return std::min(best_idx, st.text.size());
}

// ---------------------------------------------------------------------------
// Mouse button callback — needs access to layout info so stored as statics
// ---------------------------------------------------------------------------
static float  s_text_x   = 0.0f;
static float  s_text_y   = 0.0f;
static float  s_text_w   = 0.0f;
static float  s_zoom     = 1.0f;
static const GlyphAtlas* s_atlas = nullptr;

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (!s_state || !s_atlas) return;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    double mx{}, my{};
    glfwGetCursorPos(window, &mx, &my);

    if (action == GLFW_PRESS)
    {
        const std::size_t idx = click_to_index(*s_state, *s_atlas,
                                               s_text_x, s_text_y, s_text_w,
                                               static_cast<float>(mx),
                                               static_cast<float>(my),
                                               s_zoom);
        s_state->cursor_index = idx;
        s_state->sel_anchor   = idx;
        s_state->sel_cursor   = idx;
    }
}

static void cursor_pos_callback(GLFWwindow* window, double mx, double my)
{
    if (!s_state || !s_atlas) return;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) return;

    const std::size_t idx = click_to_index(*s_state, *s_atlas,
                                           s_text_x, s_text_y, s_text_w,
                                           static_cast<float>(mx),
                                           static_cast<float>(my),
                                           s_zoom);
    s_state->sel_cursor   = idx;
    s_state->cursor_index = idx;
}

// ---------------------------------------------------------------------------
// Menu bar rendering (pure OpenGL rectangles + text)
// ---------------------------------------------------------------------------
namespace
{

struct MenuState
{
    bool file_open = false;
    bool view_open = false;
};

static MenuState s_menu{};

// Draw a coloured rectangle and optionally centred/left-aligned text label.
// Returns true if the rect was clicked this frame.
bool draw_button(TextRenderer& r, const GlyphAtlas& atlas,
                 float x, float y, float w, float h,
                 const char* label,
                 Color bg, Color fg,
                 GLFWwindow* window)
{
    double mx{}, my{};
    glfwGetCursorPos(window, &mx, &my);
    const bool hovered = mx >= x && mx <= x + w && my >= y && my <= y + h;
    const Color actual_bg = hovered
        ? Color{bg.r + 0.1f, bg.g + 0.1f, bg.b + 0.1f, bg.a}
        : bg;

    r.draw_rect(x, y, w, h, actual_bg);

    if (label && label[0] != '\0')
    {
        const float tw = r.measure_text(label);
        const float tx = x + (w - tw) * 0.5f;
        const float ty = y + static_cast<float>(atlas.ascender()) + (h - static_cast<float>(atlas.line_height())) * 0.5f;
        r.draw_text(label, tx, ty, fg);
    }

    if (!hovered) return false;
    return glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}

// Returns true if the item was clicked.
bool draw_menu_item(TextRenderer& r, const GlyphAtlas& atlas,
                    float x, float y, float w, float h,
                    const char* label, const char* shortcut,
                    GLFWwindow* window)
{
    double mx{}, my{};
    glfwGetCursorPos(window, &mx, &my);
    const bool hovered = mx >= x && mx <= x + w && my >= y && my <= y + h;

    if (hovered)
        r.draw_rect(x, y, w, h, {0.25f, 0.25f, 0.55f, 1.0f});

    const float ty = y + static_cast<float>(atlas.ascender()) + (h - static_cast<float>(atlas.line_height())) * 0.5f;
    r.draw_text(label, x + 8.0f, ty, {0.9f, 0.9f, 0.9f, 1.0f});

    if (shortcut && shortcut[0] != '\0')
    {
        const float sw = r.measure_text(shortcut);
        r.draw_text(shortcut, x + w - sw - 8.0f, ty, {0.6f, 0.6f, 0.6f, 1.0f});
    }

    // Detect rising edge click: only fire when button goes from pressed to released
    // We use a simple hovered + pressed approach; good enough for a menu.
    if (!hovered) return false;

    static bool s_was_pressed = false;
    const bool now_pressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const bool clicked = s_was_pressed && !now_pressed;
    s_was_pressed = now_pressed;
    return clicked;
}

} // namespace

// ---------------------------------------------------------------------------
// Main render function
// ---------------------------------------------------------------------------
EditorUiActions render_editor_ui(GLFWwindow* window,
                                  EditorState& state,
                                  TextRenderer& renderer,
                                  const GlyphAtlas& atlas)
{
    EditorUiActions actions;

    int vp_w{}, vp_h{};
    glfwGetFramebufferSize(window, &vp_w, &vp_h);

    const float zoom = state.zoom_percent / 100.0f;
    const float lh   = static_cast<float>(atlas.line_height()) * zoom;
    const float asc  = static_cast<float>(atlas.ascender())    * zoom;

    renderer.begin_frame(vp_w, vp_h);

    // -------------------------------------------------------------------------
    // Background
    // -------------------------------------------------------------------------
    renderer.draw_rect(0.0f, 0.0f, static_cast<float>(vp_w), static_cast<float>(vp_h),
                       {0.12f, 0.12f, 0.14f, 1.0f});

    // -------------------------------------------------------------------------
    // Menu bar
    // -------------------------------------------------------------------------
    constexpr float kMenuH   = 25.0f;
    constexpr float kMenuBtnW = 60.0f;
    renderer.draw_rect(0.0f, 0.0f, static_cast<float>(vp_w), kMenuH,
                       {0.18f, 0.18f, 0.22f, 1.0f});

    // --- File menu button ---
    {
        const float bx = 0.0f;
        const float by = 0.0f;
        double mx{}, my{};
        glfwGetCursorPos(window, &mx, &my);
        const bool hovered = mx >= bx && mx < bx + kMenuBtnW && my >= by && my < by + kMenuH;

        if (s_menu.file_open)
            renderer.draw_rect(bx, by, kMenuBtnW, kMenuH, {0.25f, 0.25f, 0.5f, 1.0f});
        else if (hovered)
            renderer.draw_rect(bx, by, kMenuBtnW, kMenuH, {0.22f, 0.22f, 0.35f, 1.0f});

        const float ty = by + asc + (kMenuH - lh) * 0.5f;
        // Use atlas line_height for the base font (un-zoomed) for menu bar
        const float menu_asc = static_cast<float>(atlas.ascender());
        const float menu_lh  = static_cast<float>(atlas.line_height());
        const float menu_ty  = by + menu_asc + (kMenuH - menu_lh) * 0.5f;
        renderer.draw_text("File", bx + 10.0f, menu_ty, {0.9f, 0.9f, 0.9f, 1.0f});

        if (hovered && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
            s_menu.file_open = !s_menu.file_open;
    }

    // --- View menu button ---
    {
        const float bx = kMenuBtnW;
        const float by = 0.0f;
        double mx{}, my{};
        glfwGetCursorPos(window, &mx, &my);
        const bool hovered = mx >= bx && mx < bx + kMenuBtnW && my >= by && my < by + kMenuH;

        if (s_menu.view_open)
            renderer.draw_rect(bx, by, kMenuBtnW, kMenuH, {0.25f, 0.25f, 0.5f, 1.0f});
        else if (hovered)
            renderer.draw_rect(bx, by, kMenuBtnW, kMenuH, {0.22f, 0.22f, 0.35f, 1.0f});

        const float menu_asc = static_cast<float>(atlas.ascender());
        const float menu_lh  = static_cast<float>(atlas.line_height());
        const float menu_ty  = by + menu_asc + (kMenuH - menu_lh) * 0.5f;
        renderer.draw_text("View", bx + 10.0f, menu_ty, {0.9f, 0.9f, 0.9f, 1.0f});

        if (hovered && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
            s_menu.view_open = !s_menu.view_open;
    }

    // Close menus when clicking outside
    {
        double mx{}, my{};
        glfwGetCursorPos(window, &mx, &my);
        const bool in_menu_bar = my < kMenuH;
        const bool clicking    = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (clicking && !in_menu_bar)
        {
            // clicked outside - will close after dropdown rendering below
        }
    }

    // -------------------------------------------------------------------------
    // Status bar
    // -------------------------------------------------------------------------
    constexpr float kStatusH = 22.0f;
    const float status_y = static_cast<float>(vp_h) - (state.show_status_bar ? kStatusH : 0.0f);

    if (state.show_status_bar)
    {
        renderer.draw_rect(0.0f, status_y, static_cast<float>(vp_w), kStatusH,
                           {0.15f, 0.15f, 0.20f, 1.0f});

        const float sb_asc = static_cast<float>(atlas.ascender());
        const float sb_lh  = static_cast<float>(atlas.line_height());
        const float sb_ty  = status_y + sb_asc + (kStatusH - sb_lh) * 0.5f;

        // Word Wrap toggle button
        const char* ww_label = state.word_wrap ? "[Wrap: On]" : "[Wrap: Off]";
        const float ww_w     = renderer.measure_text(ww_label) + 8.0f;
        double mx{}, my{};
        glfwGetCursorPos(window, &mx, &my);
        const bool ww_hov = mx >= 4.0f && mx <= 4.0f + ww_w &&
                            my >= status_y && my <= status_y + kStatusH;
        if (ww_hov)
            renderer.draw_rect(4.0f, status_y, ww_w, kStatusH, {0.22f, 0.22f, 0.4f, 1.0f});
        renderer.draw_text(ww_label, 8.0f, sb_ty, {0.7f, 0.85f, 1.0f, 1.0f});
        // Toggle on click (handled via static edge detection)
        {
            static bool s_ww_prev = false;
            const bool now = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            if (ww_hov && s_ww_prev && !now)
                state.word_wrap = !state.word_wrap;
            s_ww_prev = now;
        }

        // Zoom
        const std::string zoom_str = fmt::format("{}%", static_cast<int>(state.zoom_percent));
        const float zoom_x = 8.0f + ww_w + 8.0f;
        renderer.draw_text(zoom_str, zoom_x, sb_ty, {0.55f, 0.55f, 0.55f, 1.0f});

        // Stats on the right
        const std::string stats = fmt::format("Ln {}  Col {}  Words {}  Lines {}  Chars {}",
            state.metrics.cursor_line,
            state.metrics.cursor_column,
            state.metrics.total_words,
            state.metrics.total_lines,
            state.metrics.total_chars);
        const float stats_w = renderer.measure_text(stats);
        renderer.draw_text(stats, static_cast<float>(vp_w) - stats_w - 8.0f, sb_ty,
                           {0.6f, 0.6f, 0.6f, 1.0f});
    }

    // -------------------------------------------------------------------------
    // Text area layout
    // -------------------------------------------------------------------------
    const float text_area_top = kMenuH;
    const float text_area_h   = status_y - text_area_top;

    // Gutter width: 4 digits + padding
    const float digit_w  = renderer.measure_text("0");
    const float gutter_w = digit_w * 4.0f + 16.0f;
    const float divider_x = gutter_w;

    const float text_x = gutter_w + 1.0f + 6.0f; // +6 for left padding
    const float text_y = text_area_top;
    const float text_w = static_cast<float>(vp_w) - text_x;

    // Update statics for mouse callbacks
    s_text_x = text_x;
    s_text_y = text_y;
    s_text_w = text_w;
    s_zoom   = zoom;
    s_atlas  = &atlas;

    // Install mouse callbacks if not done yet
    {
        static bool s_mouse_installed = false;
        if (!s_mouse_installed)
        {
            glfwSetMouseButtonCallback(window, mouse_button_callback);
            glfwSetCursorPosCallback(window, cursor_pos_callback);
            s_mouse_installed = true;
        }
    }

    // Clamp scroll
    {
        const float total_text_h = static_cast<float>(state.metrics.total_lines) * lh;
        const float max_scroll   = std::max(0.0f, total_text_h - text_area_h + lh);
        state.scroll_y = std::clamp(state.scroll_y, 0.0f, max_scroll);
    }

    // -------------------------------------------------------------------------
    // Gutter background
    // -------------------------------------------------------------------------
    renderer.draw_rect(0.0f, text_area_top, gutter_w, text_area_h,
                       {0.14f, 0.14f, 0.17f, 1.0f});

    // Divider
    renderer.draw_rect(divider_x, text_area_top, 1.0f, text_area_h,
                       {0.3f, 0.3f, 0.45f, 1.0f});

    // -------------------------------------------------------------------------
    // Determine visual lines for rendering (word-wrap aware)
    // We need to iterate the text to find where each logical line starts,
    // and for word-wrap, where visual lines break.
    // -------------------------------------------------------------------------

    // Helper: advance through text computing visual line breaks,
    // return a list of (byte_index_start, logical_line_number).
    struct VisLine
    {
        std::size_t start; // byte index where this visual line begins
        int         logical; // 1-based logical line number
    };

    std::vector<VisLine> vis_lines;
    vis_lines.reserve(256);

    {
        int  ln      = 1;
        float pen_x  = 0.0f;
        std::size_t line_byte_start = 0;
        vis_lines.push_back({0, 1});

        auto word_w = [&](std::size_t from) -> float
        {
            float w = 0.0f;
            for (std::size_t k = from; k < state.text.size(); ++k)
            {
                const char c = state.text[k];
                if (c == ' ' || c == '\n') break;
                const GlyphInfo* g = atlas.glyph(static_cast<char32_t>(c));
                if (g) w += static_cast<float>(g->advance) * zoom;
            }
            return w;
        };

        for (std::size_t i = 0; i < state.text.size(); ++i)
        {
            const char ch = state.text[i];

            if (ch == '\n')
            {
                ++ln;
                pen_x = 0.0f;
                vis_lines.push_back({i + 1, ln});
                continue;
            }

            if (state.word_wrap && text_w > 0.0f)
            {
                const bool at_word_start = (i == 0 || state.text[i-1] == ' ' || state.text[i-1] == '\n');
                if (at_word_start && pen_x > 0.0f)
                {
                    const float ww = word_w(i);
                    if (pen_x + ww > text_w)
                    {
                        pen_x = 0.0f;
                        vis_lines.push_back({i, ln}); // visual continuation
                    }
                }
            }

            const GlyphInfo* g = atlas.glyph(static_cast<char32_t>(ch));
            if (g) pen_x += static_cast<float>(g->advance) * zoom;
        }
    }

    // Which visual lines are visible?
    const int first_vis = static_cast<int>(state.scroll_y / lh);
    const int  num_vis  = static_cast<int>(text_area_h / lh) + 2;
    const int  last_vis = std::min(first_vis + num_vis,
                                   static_cast<int>(vis_lines.size()));

    // -------------------------------------------------------------------------
    // Current-line highlight (behind everything)
    // -------------------------------------------------------------------------
    {
        const std::size_t cur_ln = state.metrics.cursor_line;
        for (int vi = first_vis; vi < last_vis; ++vi)
        {
            if (static_cast<std::size_t>(vis_lines[vi].logical) == cur_ln)
            {
                const float vy = text_area_top + static_cast<float>(vi) * lh - state.scroll_y;
                renderer.draw_rect(text_x - 6.0f, vy,
                                   text_w + 6.0f, lh,
                                   {0.18f, 0.18f, 0.38f, 0.8f});
                // Also highlight gutter row
                renderer.draw_rect(0.0f, vy, gutter_w, lh,
                                   {0.20f, 0.20f, 0.42f, 0.8f});
            }
        }
    }

    // -------------------------------------------------------------------------
    // Selection highlight
    // -------------------------------------------------------------------------
    if (state.sel_anchor != state.sel_cursor)
    {
        const std::size_t sel_lo = std::min(state.sel_anchor, state.sel_cursor);
        const std::size_t sel_hi = std::max(state.sel_anchor, state.sel_cursor);

        // Walk visual lines and highlight selected spans
        for (int vi = first_vis; vi < last_vis; ++vi)
        {
            const std::size_t line_start_idx = vis_lines[vi].start;
            const std::size_t line_end_idx   = (vi + 1 < static_cast<int>(vis_lines.size()))
                                               ? vis_lines[vi + 1].start
                                               : state.text.size();

            // The selection must overlap this visual line
            if (sel_hi <= line_start_idx || sel_lo >= line_end_idx)
                continue;

            const std::size_t hl_lo = std::max(sel_lo, line_start_idx);
            const std::size_t hl_hi = std::min(sel_hi, line_end_idx);

            // Measure x offsets
            std::string_view line_sv(state.text.data() + line_start_idx,
                                     line_end_idx - line_start_idx);
            const float x0 = renderer.measure_text_n(line_sv, hl_lo - line_start_idx) * zoom;
            const float x1 = renderer.measure_text_n(line_sv, hl_hi - line_start_idx) * zoom;

            const float vy = text_area_top + static_cast<float>(vi) * lh - state.scroll_y;
            renderer.draw_rect(text_x + x0, vy, x1 - x0, lh,
                               {0.2f, 0.4f, 0.8f, 0.5f});
        }
    }

    // -------------------------------------------------------------------------
    // Render visible text lines
    // -------------------------------------------------------------------------
    for (int vi = first_vis; vi < last_vis; ++vi)
    {
        const float vy          = text_area_top + static_cast<float>(vi) * lh - state.scroll_y;
        const float baseline    = vy + asc;
        const std::size_t start = vis_lines[vi].start;
        const std::size_t end   = (vi + 1 < static_cast<int>(vis_lines.size()))
                                  ? vis_lines[vi + 1].start
                                  : state.text.size();

        // Build a view for this visual line (strip trailing \n for display)
        std::size_t draw_len = end - start;
        if (draw_len > 0 && state.text[start + draw_len - 1] == '\n')
            --draw_len;

        if (draw_len == 0) continue;

        std::string_view sv(state.text.data() + start, draw_len);
        renderer.draw_text(sv, text_x, baseline, {0.88f, 0.88f, 0.88f, 1.0f}, -1.0f, zoom);
    }

    // -------------------------------------------------------------------------
    // Cursor (blinking caret)
    // -------------------------------------------------------------------------
    {
        const double t       = glfwGetTime();
        const bool   visible = std::fmod(t, 1.0) < 0.6;

        if (visible)
        {
            // Find which visual line contains cursor_index
            int cursor_vi = 0;
            for (int vi = static_cast<int>(vis_lines.size()) - 1; vi >= 0; --vi)
            {
                if (vis_lines[vi].start <= state.cursor_index)
                {
                    cursor_vi = vi;
                    break;
                }
            }

            const std::size_t line_start_idx = vis_lines[cursor_vi].start;
            const std::size_t col_len        = state.cursor_index - line_start_idx;
            std::string_view  pre(state.text.data() + line_start_idx, col_len);

            float cursor_x = text_x;
            for (char ch : pre)
            {
                const GlyphInfo* g = atlas.glyph(static_cast<char32_t>(ch));
                if (g) cursor_x += static_cast<float>(g->advance) * zoom;
            }

            const float cy = text_area_top + static_cast<float>(cursor_vi) * lh - state.scroll_y;
            renderer.draw_rect(cursor_x, cy, 2.0f, lh, {0.9f, 0.9f, 1.0f, 0.9f});
        }
    }

    // -------------------------------------------------------------------------
    // Line numbers
    // -------------------------------------------------------------------------
    {
        const float ln_asc = static_cast<float>(atlas.ascender());
        const float ln_lh  = static_cast<float>(atlas.line_height());

        int prev_logical = -1;
        for (int vi = first_vis; vi < last_vis; ++vi)
        {
            const int logical = vis_lines[vi].logical;
            if (logical == prev_logical) continue; // continuation line — no number
            prev_logical = logical;

            const float vy       = text_area_top + static_cast<float>(vi) * lh - state.scroll_y;
            const float baseline = vy + asc;

            const std::string num = std::to_string(logical);
            const float       nw  = renderer.measure_text(num);
            const float       nx  = gutter_w - nw - 6.0f;

            const bool is_cursor_line = (static_cast<std::size_t>(logical) ==
                                         state.metrics.cursor_line);
            const Color ln_col = is_cursor_line
                ? Color{0.85f, 0.85f, 1.0f, 1.0f}
                : Color{0.45f, 0.45f, 0.55f, 1.0f};

            renderer.draw_text(num, nx, baseline, ln_col);
        }
    }

    // -------------------------------------------------------------------------
    // Drop-down menus (rendered last so they appear on top)
    // -------------------------------------------------------------------------

    static bool s_file_was_pressed = false;
    static bool s_view_was_pressed = false;
    const bool mouse_pressed = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    if (s_menu.file_open)
    {
        constexpr float mw = 200.0f;
        constexpr float mh = 22.0f;
        constexpr float mx_start = 0.0f;
        constexpr float my_start = kMenuH;

        struct Item { const char* label; const char* shortcut; };
        const Item items[] = {
            {"New",  "Ctrl+N"},
            {"Open", "Ctrl+O"},
            {"Save", "Ctrl+S"},
            {"-",    ""},
            {"Exit", ""},
        };
        constexpr int kCount = static_cast<int>(std::size(items));

        renderer.draw_rect(mx_start, my_start, mw, kCount * mh, {0.20f, 0.20f, 0.26f, 1.0f});
        renderer.draw_rect(mx_start, my_start, mw, 1.0f,        {0.4f, 0.4f, 0.6f, 1.0f});

        double cmx{}, cmy{};
        glfwGetCursorPos(window, &cmx, &cmy);

        for (int i = 0; i < kCount; ++i)
        {
            const float iy = my_start + static_cast<float>(i) * mh;
            const bool hov = cmx >= mx_start && cmx <= mx_start + mw &&
                             cmy >= iy && cmy <= iy + mh;

            if (hov) renderer.draw_rect(mx_start, iy, mw, mh, {0.28f, 0.28f, 0.55f, 1.0f});

            if (std::strcmp(items[i].label, "-") == 0)
            {
                renderer.draw_rect(mx_start + 4.0f, iy + mh * 0.5f, mw - 8.0f, 1.0f,
                                   {0.35f, 0.35f, 0.45f, 1.0f});
                continue;
            }

            const float mn_asc = static_cast<float>(atlas.ascender());
            const float mn_lh  = static_cast<float>(atlas.line_height());
            const float mn_ty  = iy + mn_asc + (mh - mn_lh) * 0.5f;
            renderer.draw_text(items[i].label,    mx_start + 8.0f, mn_ty, {0.9f, 0.9f, 0.9f, 1.0f});
            if (items[i].shortcut[0] != '\0')
            {
                const float sw = renderer.measure_text(items[i].shortcut);
                renderer.draw_text(items[i].shortcut, mx_start + mw - sw - 8.0f, mn_ty,
                                   {0.55f, 0.55f, 0.55f, 1.0f});
            }

            // Click detection
            const bool clicked = hov && s_file_was_pressed && !mouse_pressed;
            if (clicked)
            {
                s_menu.file_open = false;
                if (i == 0) // New
                {
                    state.text.clear(); state.current_file_path.clear();
                    state.cursor_index = 0; state.sel_anchor = 0; state.sel_cursor = 0;
                    state.scroll_y = 0.0f;
                }
                else if (i == 1) // Open
                {
                    const std::string path = open_file_dialog();
                    if (!path.empty())
                    {
                        std::string content;
                        if (read_text_file(path, content))
                        {
                            state.text = std::move(content);
                            state.current_file_path = path;
                            state.cursor_index = 0; state.sel_anchor = 0; state.sel_cursor = 0;
                            state.scroll_y = 0.0f;
                        }
                    }
                }
                else if (i == 2) // Save
                {
                    std::string path = state.current_file_path;
                    if (path.empty()) path = save_file_dialog(path);
                    if (!path.empty() && write_text_file(path, state.text))
                        state.current_file_path = path;
                }
                else if (i == 4) // Exit
                {
                    actions.request_exit = true;
                }
            }
        }

        // Close menu if clicking outside
        double cmx2{}, cmy2{};
        glfwGetCursorPos(window, &cmx2, &cmy2);
        if (s_file_was_pressed && !mouse_pressed &&
            !(cmx2 >= mx_start && cmx2 <= mx_start + mw &&
              cmy2 >= my_start && cmy2 <= my_start + kCount * mh) &&
            !(cmx2 >= 0.0 && cmx2 < kMenuBtnW && cmy2 >= 0.0 && cmy2 < kMenuH))
        {
            s_menu.file_open = false;
        }
    }
    s_file_was_pressed = mouse_pressed;

    if (s_menu.view_open)
    {
        constexpr float mw       = 220.0f;
        constexpr float mh       = 22.0f;
        constexpr float mx_start = kMenuBtnW;
        constexpr float my_start = kMenuH;

        struct Item { const char* label; const char* shortcut; };
        const Item items[] = {
            {"Zoom In",        "Ctrl++"},
            {"Zoom Out",       "Ctrl+-"},
            {"Reset Zoom",     "Ctrl+0"},
            {"-",              ""},
            {"Word Wrap",      ""},
            {"Show Status Bar",""},
        };
        constexpr int kCount = static_cast<int>(std::size(items));

        renderer.draw_rect(mx_start, my_start, mw, kCount * mh, {0.20f, 0.20f, 0.26f, 1.0f});
        renderer.draw_rect(mx_start, my_start, mw, 1.0f,        {0.4f, 0.4f, 0.6f, 1.0f});

        double cmx{}, cmy{};
        glfwGetCursorPos(window, &cmx, &cmy);

        for (int i = 0; i < kCount; ++i)
        {
            const float iy = my_start + static_cast<float>(i) * mh;
            const bool hov = cmx >= mx_start && cmx <= mx_start + mw &&
                             cmy >= iy && cmy <= iy + mh;

            if (hov) renderer.draw_rect(mx_start, iy, mw, mh, {0.28f, 0.28f, 0.55f, 1.0f});

            if (std::strcmp(items[i].label, "-") == 0)
            {
                renderer.draw_rect(mx_start + 4.0f, iy + mh * 0.5f, mw - 8.0f, 1.0f,
                                   {0.35f, 0.35f, 0.45f, 1.0f});
                continue;
            }

            const float mn_asc = static_cast<float>(atlas.ascender());
            const float mn_lh  = static_cast<float>(atlas.line_height());
            const float mn_ty  = iy + mn_asc + (mh - mn_lh) * 0.5f;

            // Checkmark for toggles
            std::string label_str = items[i].label;
            if (i == 4 && state.word_wrap)       label_str = std::string("[x] ") + items[i].label;
            if (i == 5 && state.show_status_bar)  label_str = std::string("[x] ") + items[i].label;

            renderer.draw_text(label_str.c_str(), mx_start + 8.0f, mn_ty, {0.9f, 0.9f, 0.9f, 1.0f});
            if (items[i].shortcut[0] != '\0')
            {
                const float sw = renderer.measure_text(items[i].shortcut);
                renderer.draw_text(items[i].shortcut, mx_start + mw - sw - 8.0f, mn_ty,
                                   {0.55f, 0.55f, 0.55f, 1.0f});
            }

            const bool clicked = hov && s_view_was_pressed && !mouse_pressed;
            if (clicked)
            {
                s_menu.view_open = false;
                if      (i == 0) state.zoom_percent = std::clamp(state.zoom_percent + 10.0f, 25.0f, 300.0f);
                else if (i == 1) state.zoom_percent = std::clamp(state.zoom_percent - 10.0f, 25.0f, 300.0f);
                else if (i == 2) state.zoom_percent = 100.0f;
                else if (i == 4) state.word_wrap = !state.word_wrap;
                else if (i == 5) state.show_status_bar = !state.show_status_bar;
            }
        }

        double cmx2{}, cmy2{};
        glfwGetCursorPos(window, &cmx2, &cmy2);
        if (s_view_was_pressed && !mouse_pressed &&
            !(cmx2 >= mx_start && cmx2 <= mx_start + mw &&
              cmy2 >= my_start && cmy2 <= my_start + kCount * mh) &&
            !(cmx2 >= kMenuBtnW && cmx2 < 2.0 * kMenuBtnW && cmy2 >= 0.0 && cmy2 < kMenuH))
        {
            s_menu.view_open = false;
        }
    }
    s_view_was_pressed = mouse_pressed;

    // -------------------------------------------------------------------------
    // Update metrics
    // -------------------------------------------------------------------------
    state.metrics = calculate_editor_metrics(state.text, state.cursor_index);

    // -------------------------------------------------------------------------
    // Flush everything to the GPU
    // -------------------------------------------------------------------------
    renderer.flush();

    return actions;
}

} // namespace notetake

