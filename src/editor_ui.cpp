#include "notetake/editor_ui.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

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
constexpr ImGuiWindowFlags kEditorWindowFlags = ImGuiWindowFlags_NoDecoration |
                                               ImGuiWindowFlags_NoMove |
                                               ImGuiWindowFlags_NoResize |
                                               ImGuiWindowFlags_NoSavedSettings;

constexpr float kMinZoomPercent = 25.0f;
constexpr float kMaxZoomPercent = 300.0f;
constexpr float kZoomStepPercent = 10.0f;

bool read_text_file(const std::string& path, std::string& content)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return false;
    }

    content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool write_text_file(const std::string& path, const std::string& content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return false;
    }

    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return output.good();
}

std::string open_file_dialog()
{
#ifdef _WIN32
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;

    if (GetOpenFileNameA(&ofn) != 0)
    {
        return std::string(path);
    }
#endif
    return {};
}

std::string save_file_dialog(const std::string& current_path)
{
#ifdef _WIN32
    char path[MAX_PATH] = {};
    if (!current_path.empty())
    {
        const std::size_t copy_size = std::min(current_path.size(), static_cast<std::size_t>(MAX_PATH - 1));
        std::copy_n(current_path.data(), copy_size, path);
        path[copy_size] = '\0';
    }

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    ofn.lpstrDefExt = "txt";

    if (GetSaveFileNameA(&ofn) != 0)
    {
        return std::string(path);
    }
#endif
    return {};
}

void clamp_zoom(EditorState& state)
{
    state.zoom_percent = std::clamp(state.zoom_percent, kMinZoomPercent, kMaxZoomPercent);
}

std::size_t get_cursor_index()
{
    const ImGuiID item_id = ImGui::GetItemID();
    if (ImGuiInputTextState* input_state = ImGui::GetInputTextState(item_id))
    {
        return static_cast<std::size_t>(std::max(input_state->GetCursorPos(), 0));
    }

    return 0;
}

std::size_t get_line_start_index(std::string_view text, std::size_t cursor_index)
{
    const std::size_t clamped = std::min(cursor_index, text.size());
    if (clamped == 0)
    {
        return 0;
    }

    std::size_t index = clamped;
    while (index > 0)
    {
        if (text[index - 1] == '\n')
        {
            break;
        }
        --index;
    }

    return index;
}

float get_column_pixel_offset(std::string_view text, std::size_t cursor_index)
{
    const std::size_t line_start = get_line_start_index(text, cursor_index);
    const std::size_t clamped = std::min(cursor_index, text.size());

    const char* start = text.data() + line_start;
    const char* end = text.data() + clamped;
    return ImGui::CalcTextSize(start, end).x;
}
} // namespace

EditorUiActions render_editor_ui(EditorState& state)
{
    static float s_scroll_y = 0.0f;
    static float s_scroll_x = 0.0f;

    EditorUiActions actions;

    ImGuiIO& io = ImGui::GetIO();

    // Keyboard shortcuts
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false))
    {
        state.text.clear();
        state.current_file_path.clear();
        state.cursor_index = 0;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false))
    {
        const std::string path = open_file_dialog();
        if (!path.empty())
        {
            std::string content;
            if (read_text_file(path, content))
            {
                state.text = std::move(content);
                state.current_file_path = path;
                state.cursor_index = std::min(state.cursor_index, state.text.size());
            }
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        std::string path = state.current_file_path;
        if (path.empty())
        {
            path = save_file_dialog(path);
        }
        if (!path.empty() && write_text_file(path, state.text))
        {
            state.current_file_path = path;
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Equal, false))
    {
        state.zoom_percent += kZoomStepPercent;
        clamp_zoom(state);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Minus, false))
    {
        state.zoom_percent -= kZoomStepPercent;
        clamp_zoom(state);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false))
    {
        state.zoom_percent = 100.0f;
    }

    float menu_bar_height = 0.0f;
    if (ImGui::BeginMainMenuBar())
    {
        menu_bar_height = ImGui::GetWindowSize().y;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Ctrl+N"))
            {
                state.text.clear();
                state.current_file_path.clear();
                state.cursor_index = 0;
            }

            if (ImGui::MenuItem("Open", "Ctrl+O"))
            {
                const std::string path = open_file_dialog();
                if (!path.empty())
                {
                    std::string content;
                    if (read_text_file(path, content))
                    {
                        state.text = std::move(content);
                        state.current_file_path = path;
                        state.cursor_index = std::min(state.cursor_index, state.text.size());
                    }
                }
            }

            if (ImGui::MenuItem("Save", "Ctrl+S"))
            {
                std::string path = state.current_file_path;
                if (path.empty())
                {
                    path = save_file_dialog(path);
                }

                if (!path.empty() && write_text_file(path, state.text))
                {
                    state.current_file_path = path;
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
            {
                actions.request_exit = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Zoom In", "Ctrl++"))
            {
                state.zoom_percent += kZoomStepPercent;
                clamp_zoom(state);
            }
            if (ImGui::MenuItem("Zoom Out", "Ctrl+-"))
            {
                state.zoom_percent -= kZoomStepPercent;
                clamp_zoom(state);
            }
            if (ImGui::MenuItem("Reset Zoom", "Ctrl+0"))
            {
                state.zoom_percent = 100.0f;
            }

            ImGui::Separator();
            ImGui::MenuItem("Show Status Bar", nullptr, &state.show_status_bar);
            ImGui::MenuItem("Word Wrap", nullptr, &state.word_wrap);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + menu_bar_height));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - menu_bar_height));

    if (!ImGui::Begin("##editor_root", nullptr, kEditorWindowFlags))
    {
        ImGui::End();
        return actions;
    }

    clamp_zoom(state);
    ImGui::SetWindowFontScale(state.zoom_percent / 100.0f);

    const ImGuiStyle& style        = ImGui::GetStyle();
    const float status_bar_height  = state.show_status_bar
        ? (ImGui::GetFrameHeightWithSpacing() + style.FramePadding.y * 2.0f)
        : 0.0f;
    const ImVec2 available         = ImGui::GetContentRegionAvail();
    const ImVec2 editor_size(available.x, std::max(available.y - status_bar_height, 0.0f));

    // Metrics shortcuts
    const float font_size   = ImGui::GetTextLineHeight();
    const float ln_pad_x    = style.ItemSpacing.x;
    const float ln_width    = ImGui::CalcTextSize("9999").x + ln_pad_x * 2.0f;
    const int   total_ln    = static_cast<int>(state.metrics.total_lines);
    const int   cursor_ln   = static_cast<int>(state.metrics.cursor_line);
    // Visible lines: how many fit in the editor area including one extra to avoid a gap at bottom
    const int   visible_ln  = static_cast<int>(editor_size.y / font_size) + 2;
    const int   first_ln    = static_cast<int>(s_scroll_y / font_size);

    // -------------------------------------------------------------------------
    // LINE NUMBERS panel
    // Remove padding so that line i is exactly at content-Y = i * font_size
    // -------------------------------------------------------------------------
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##ln", ImVec2(ln_width, editor_size.y), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);

    // Sync scroll with the text area (one frame latency — imperceptible at 60 fps)
    ImGui::SetScrollY(s_scroll_y);

    // Current-line highlight in the line-number column
    {
        ImDrawList* dl      = ImGui::GetWindowDrawList();
        const ImVec2 wp     = ImGui::GetWindowPos();
        const float hy      = wp.y + (cursor_ln - 1) * font_size - s_scroll_y;
        if (hy + font_size > wp.y && hy < wp.y + editor_size.y)
            dl->AddRectFilled({wp.x, hy}, {wp.x + ln_width, hy + font_size},
                IM_COL32(55, 55, 110, 210));
    }

    // Render visible line numbers / dashes
    for (int i = first_ln; i < first_ln + visible_ln; ++i)
    {
        const float item_y = static_cast<float>(i) * font_size;
        if (item_y - s_scroll_y > editor_size.y) break;
        ImGui::SetCursorPosY(item_y);

        if (i < total_ln)
        {
            const std::string num  = std::to_string(i + 1);
            const float       nw   = ImGui::CalcTextSize(num.c_str()).x;
            ImGui::SetCursorPosX(ln_width - nw - ln_pad_x);
            if (i + 1 == cursor_ln)
                ImGui::TextColored({0.85f, 0.85f, 1.0f, 1.0f}, "%s", num.c_str());
            else
                ImGui::TextDisabled("%s", num.c_str());
        }
        else
        {
            // Visible area beyond written content — show a dash
            const float dw = ImGui::CalcTextSize("-").x;
            ImGui::SetCursorPosX(ln_width - dw - ln_pad_x);
            ImGui::TextDisabled("-");
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2); // WindowPadding, ItemSpacing

    // Thin vertical divider between line numbers and text
    {
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            {sp.x, sp.y}, {sp.x, sp.y + editor_size.y},
            IM_COL32(80, 80, 130, 180), 1.0f);
    }
    ImGui::SameLine(0.0f, 1.0f);

    // -------------------------------------------------------------------------
    // TEXT AREA
    // -------------------------------------------------------------------------
    const ImGuiWindowFlags ta_flags = state.word_wrap
        ? ImGuiWindowFlags_None
        : ImGuiWindowFlags_HorizontalScrollbar;

    // Zero top padding so the first text line aligns with line-number row 1
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 0.0f));
    ImGui::BeginChild("##ta", ImVec2(0.0f, editor_size.y), false, ta_flags);

    ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_AllowTabInput;
    if (state.word_wrap)
        input_flags |= ImGuiInputTextFlags_NoHorizontalScroll;

    // Zero frame-padding Y so text starts at exactly the top of the widget
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 0.0f));
    ImGui::InputTextMultiline("##input", &state.text, ImGui::GetContentRegionAvail(), input_flags);
    const ImGuiID input_id = ImGui::GetItemID();
    const ImVec2 input_min = ImGui::GetItemRectMin();
    const ImVec2 input_max = ImGui::GetItemRectMax();
    ImGui::PopStyleVar(); // FramePadding

    if (ImGui::IsItemActive())
        state.cursor_index = get_cursor_index();
    else if (state.cursor_index > state.text.size())
        state.cursor_index = state.text.size();

    state.metrics = calculate_editor_metrics(state.text, state.cursor_index);

    // Draw row and column highlights for the current caret position.
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        const float row_y = input_min.y + (static_cast<float>(state.metrics.cursor_line) - 1.0f) * font_size - s_scroll_y;
        const float row_h = font_size;
        if (row_y + row_h > input_min.y && row_y < input_max.y)
        {
            draw_list->AddRectFilled(
                ImVec2(input_min.x, row_y),
                ImVec2(input_max.x, row_y + row_h),
                IM_COL32(45, 45, 95, 95));
        }

        const float text_offset_x = get_column_pixel_offset(state.text, state.cursor_index);
        const float col_x = input_min.x + 4.0f + text_offset_x - s_scroll_x;
        if (col_x >= input_min.x && col_x <= input_max.x)
        {
            draw_list->AddRectFilled(
                ImVec2(col_x, input_min.y),
                ImVec2(col_x + 2.0f, input_max.y),
                IM_COL32(95, 95, 190, 90));
        }
    }

    // --- Scroll sync ---
    // Primary: InputTextState.Scroll.y is updated by ImGui when the widget is active
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        if (g.InputTextState.ID == input_id)
        {
            s_scroll_y = g.InputTextState.Scroll.y;
            s_scroll_x = g.InputTextState.Scroll.x;
        }
        else
        {
            // Fallback: read from InputTextMultiline's internal child window
            // (the only child that ##ta creates is the scroll region inside InputTextMultiline)
            ImGuiWindow* ta_win = ImGui::GetCurrentWindow();
            for (ImGuiWindow* child : ta_win->DC.ChildWindows)
            {
                if (child != nullptr)
                {
                    s_scroll_y = child->Scroll.y;
                    s_scroll_x = child->Scroll.x;
                    break;
                }
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(); // WindowPadding

    if (state.show_status_bar)
    {
        // ---------------------------------------------------------------------
        // STATUS BAR
        // ---------------------------------------------------------------------
        ImGui::Separator();
        ImGui::BeginChild("StatusBar", ImVec2(0.0f, 0.0f), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const char* wrap_label = state.word_wrap ? "Word Wrap: On" : "Word Wrap: Off";
        if (ImGui::Button(wrap_label))
        {
            state.word_wrap = !state.word_wrap;
        }

        ImGui::SameLine();
        ImGui::TextDisabled("%d%%", static_cast<int>(state.zoom_percent));

        const std::string status_text = fmt::format(
            "Ln {}, Col {}   Words {}   Lines {}   Chars {}",
            state.metrics.cursor_line,
            state.metrics.cursor_column,
            state.metrics.total_words,
            state.metrics.total_lines,
            state.metrics.total_chars);

        const float status_width = ImGui::CalcTextSize(status_text.c_str()).x;
        const float right_edge   = ImGui::GetWindowContentRegionMax().x;
        const float next_x       = std::max(ImGui::GetCursorPosX() + style.ItemSpacing.x,
                                            right_edge - status_width);
        ImGui::SameLine(next_x);
        ImGui::TextUnformatted(status_text.c_str());

        ImGui::EndChild();
    }

    ImGui::End();
    return actions;
}
} // namespace notetake
