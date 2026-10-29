#include "internal.h"

#include <rui/widgets.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include <imgui_internal.h>  // NavCursorVisible

namespace rui
{
    namespace
    {
        // Layout at 720p.
        constexpr float ROW_HEIGHT = 44.0f, ROW_ROUNDING = 8.0f, ROW_LEFT = 12.0f, ROW_RIGHT = 8.0f;
        constexpr float CONTROL_WIDTH = 232.0f, ARROW = 28.0f;
        constexpr float SLIDER_VALUE = 44.0f, SLIDER_GAP = 12.0f;
        constexpr float TOGGLE_WIDTH = 44.0f, TOGGLE_HEIGHT = 24.0f, TOGGLE_KNOB = 18.0f;
        constexpr float BUTTON_HEIGHT = 30.0f;
        constexpr float LABEL_SIZE = 16.5f;
        constexpr float TOGGLE_SECONDS = 0.12f;

        InputMode s_input = InputMode::Pad;
        float s_descriptionWidth = 0.0f;
        int s_descriptionLines = 2;
        int s_rowIndex = 0, s_focusRequest = -1, s_focused = -1;
        RowInfo s_current;
        ImGuiID s_editing = 0;  // the text field being typed in
        bool s_focusEditor = false;

        // Pressed, with key repeat, on the pad, the keyboard or the left stick.
        bool Pressed(ImGuiKey dpad, ImGuiKey arrow, ImGuiKey stick)
        {
            return ImGui::IsKeyPressed(dpad, true) || ImGui::IsKeyPressed(arrow, true) || ImGui::IsKeyPressed(stick, true);
        }

        ImU32 Mix(ImU32 a, ImU32 b, float t)
        {
            const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
        }

        ImU32 Darker(ImU32 color, float factor)
        {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
            return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x * factor, c.y * factor, c.z * factor, c.w));
        }

        // A row as laid out and as the player touched it this frame.
        struct Row
        {
            ImVec2 min, max;
            ImGuiID id = 0;
            bool disabled = false;
            bool focused = false;  // ImGui's nav focus
            bool ring = false;     // focused with the pad/keyboard cursor showing
            bool hovered = false;  // the mouse, while the cursor is hidden
            bool active = false;   // held down
            bool pressed = false;  // A, Enter, Space or a click
            bool clicked = false;  // pressed with the mouse
            bool reset = false;    // Y, Delete or a right click
            bool describe = false; // the description strip and footer are this row's
            int step = 0;          // left -1, right 1
        };

        Row BeginRow(std::string_view id, float height, bool disabled)
        {
            Row row;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size(ImGui::GetContentRegionAvail().x, S(height));
            bool takeFocus = false;
            if (s_rowIndex == s_focusRequest)
            {
                if (disabled)
                    s_focusRequest++;  // the next one that can take it
                else
                {
                    takeFocus = true;
                    s_focusRequest = -1;
                }
            }
            if (takeFocus)
                ImGui::SetKeyboardFocusHere();
            ImGui::PushID(int(ImHashStr(id.data(), id.size())));
            if (disabled)
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            row.pressed = ImGui::InvisibleButton("##row", size, ImGuiButtonFlags_EnableNav | ImGuiButtonFlags_MouseButtonLeft);
            row.id = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            row.focused = !disabled && ImGui::IsItemFocused();
            row.active = !disabled && ImGui::IsItemActive();
            if (disabled)
                ImGui::PopItemFlag();
            ImGui::PopID();

            row.min = pos;
            row.max = ImVec2(pos.x + size.x, pos.y + size.y);
            row.disabled = disabled;
            row.ring = row.focused && NavCursorShown();
            row.hovered = hovered && !NavCursorShown();
            row.clicked = row.pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            row.describe = row.ring || row.hovered;
            if (row.focused)
            {
                s_focused = s_rowIndex;
                if (Pressed(ImGuiKey_GamepadDpadLeft, ImGuiKey_LeftArrow, ImGuiKey_GamepadLStickLeft))
                    row.step = -1;
                else if (Pressed(ImGuiKey_GamepadDpadRight, ImGuiKey_RightArrow, ImGuiKey_GamepadLStickRight))
                    row.step = 1;
                if (!ImGui::GetIO().WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false)))
                    row.reset = true;
            }
            if (!disabled && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                row.reset = true;
            s_rowIndex++;

            // Hovered, pressed or focused (with the ring and its halo).
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float rounding = S(ROW_ROUNDING);
            if (row.active)
                draw->AddRectFilled(row.min, row.max, ACTIVE_ROW, rounding);
            else if (row.ring)
                draw->AddRectFilled(row.min, row.max, INK_4, rounding);
            else if (row.hovered && !disabled)
                draw->AddRectFilled(row.min, row.max, INK_3, rounding);
            if (row.ring)
            {
                const float halo = S(3.0f);
                draw->AddRect(row.min, row.max, ACCENT, rounding, 0, Stroke(2.0f));
                draw->AddRect(ImVec2(row.min.x - halo, row.min.y - halo), ImVec2(row.max.x + halo, row.max.y + halo), ACCENT_HALO,
                    rounding + halo, 0, Stroke(4.0f));
            }
            return row;
        }

        // The label and its chip.
        void DrawLabel(const Row& row, const RowSpec& spec)
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float centerY = (row.min.y + row.max.y) * 0.5f;
            const float x = row.min.x + S(ROW_LEFT);
            TextMiddle(draw, Face::Sans, LABEL_SIZE, x, centerY, row.disabled ? TEXT_3 : TEXT, spec.label);
            if (spec.chip.kind == Chip::Kind::None)
                return;
            const std::string_view text = spec.chip.kind == Chip::Kind::RestartPending ? "Restart required"
                : spec.chip.kind == Chip::Kind::Restart ? "Restart" : spec.chip.text;
            const bool pending = spec.chip.kind == Chip::Kind::RestartPending;
            const float left = x + TextWidth(Face::Sans, LABEL_SIZE, spec.label) + S(8.0f);
            const float width = S(4.0f) + S(14.0f) + S(3.0f) + TextWidth(Face::SemiBold, 12.0f, text) + S(6.0f);
            const ImVec2 min(left, centerY - S(10.0f)), max(left + width, centerY + S(10.0f));
            draw->AddRectFilled(min, max, pending ? WARN : IM_COL32(255, 255, 255, 10), S(5.0f));
            const ImU32 color = pending ? WARN_INK : TEXT_3;
            Icon(draw, ICON_RESTART_ALT, 14.0f, ImVec2(left + S(4.0f + 7.0f), centerY), color);
            TextMiddle(draw, Face::SemiBold, 12.0f, left + S(4.0f + 14.0f + 3.0f), centerY, color, text);
        }

        void Describe(const Row& row, const RowSpec& spec, RowKind kind, std::string defaultText, const char* action)
        {
            // Every row's lines in the strip, so it's as tall as the
            // section's longest (the first line goes beside the default).
            const std::string_view text = row.disabled && !spec.disabledWhy.empty() ? spec.disabledWhy : spec.description;
            const std::string right = defaultText.empty() ? std::string() : "Default: " + defaultText;
            const float firstWidth = right.empty() ? s_descriptionWidth : s_descriptionWidth - TextWidth(Face::Sans, 13.0f, right) - S(12.0f);
            s_descriptionLines = std::max(s_descriptionLines, WrappedLines(Face::Sans, 14.0f, s_descriptionWidth, text, firstWidth));
            if (!row.describe)
                return;
            s_current = {};
            s_current.kind = kind;
            s_current.disabled = row.disabled;
            s_current.text = std::string(text);
            s_current.resettable = !row.disabled && !defaultText.empty();
            s_current.right = right;
            s_current.actionLabel = action;
        }

        float ControlRight(const Row& row)
        {
            return row.max.x - S(ROW_RIGHT);
        }

        // A selector's arrow; true when the mouse is on it.
        bool Arrow(ImDrawList* draw, const Row& row, float left, bool right, bool atLimit)
        {
            const float centerY = (row.min.y + row.max.y) * 0.5f;
            const ImVec2 min(left, centerY - S(ARROW * 0.5f)), max(left + S(ARROW), centerY + S(ARROW * 0.5f));
            const bool under = !row.disabled && row.hovered && ImGui::IsMouseHoveringRect(min, max);
            ImU32 color = row.disabled ? TEXT_3 : atLimit ? (row.ring ? IM_COL32(58, 71, 87, 255) : ARROW_LIMIT) : row.ring ? TEXT : TEXT_2;
            if (under && !atLimit)
            {
                draw->AddRectFilled(min, max, IM_COL32(255, 255, 255, 15), S(6.0f));
                color = TEXT;
            }
            Icon(draw, right ? ICON_CHEVRON_RIGHT : ICON_CHEVRON_LEFT, 22.0f, ImVec2((min.x + max.x) * 0.5f, centerY), color);
            return ImGui::IsMouseHoveringRect(min, max);
        }
    }

}

namespace rui::detail
{
    void UpdateInputMode()
    {
        ImGuiIO& io = ImGui::GetIO();
        for (int key = ImGuiKey_GamepadStart; key <= ImGuiKey_GamepadR3; key++)
        {
            if (ImGui::IsKeyPressed(ImGuiKey(key), false))
                s_input = InputMode::Pad;
        }
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_GamepadStart; key++)
        {
            if (ImGui::IsKeyPressed(ImGuiKey(key), false))
                s_input = InputMode::Keys;
        }
        if (std::abs(io.MouseDelta.x) + std::abs(io.MouseDelta.y) > 2.0f || ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right) || io.MouseWheel != 0.0f)
            s_input = InputMode::Mouse;
    }
}

namespace rui
{
    InputMode CurrentInput()
    {
        return s_input;
    }

    bool NavCursorShown()
    {
        return GImGui->NavCursorVisible;
    }

    void BeginRows(int focusRow, float descriptionWidth)
    {
        s_descriptionWidth = descriptionWidth;
        s_descriptionLines = 2;
        s_rowIndex = 0;
        s_focusRequest = focusRow;
        s_focused = -1;
        s_current = {};
    }

    int DescriptionLines()
    {
        return s_descriptionLines;
    }

    int FocusedRow()
    {
        return s_focused;
    }

    const RowInfo& CurrentRow()
    {
        return s_current;
    }

    bool Selector(const RowSpec& spec, int& index, std::span<const std::string> names, int defaultIndex)
    {
        const int count = int(names.size());
        index = std::clamp(index, 0, count - 1);
        const Row row = BeginRow(spec.label, ROW_HEIGHT, !spec.disabledWhy.empty());
        DrawLabel(row, spec);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float right = ControlRight(row), left = right - S(CONTROL_WIDTH);
        const float centerY = (row.min.y + row.max.y) * 0.5f;
        const bool onLeft = Arrow(draw, row, left, false, index <= 0);
        const bool onRight = Arrow(draw, row, right - S(ARROW), true, index >= count - 1);

        // The value over its option pips.
        const float middle = (left + right) * 0.5f;
        const float top = centerY - S(13.5f);
        const std::string& value = names[size_t(index)];
        const float valueRoom = S(CONTROL_WIDTH - 2.0f * ARROW - 8.0f);
        const float valueWidth = std::min(TextWidth(Face::Sans, LABEL_SIZE, value), valueRoom);
        const ImU32 valueColor = row.disabled ? TEXT_3 : row.ring ? ACCENT : TEXT;
        TextEllipsis(draw, Face::Sans, LABEL_SIZE, ImVec2(middle - valueWidth * 0.5f, top + S(10.0f) - std::round(LABEL_SIZE * Scale()) * 0.52f),
            middle + valueRoom * 0.5f, valueColor, value);
        const float pip = S(4.0f), current = S(12.0f), gap = S(4.0f);
        const float pipsWidth = pip * float(count - 1) + current + gap * float(count - 1);
        float x = middle - pipsWidth * 0.5f;
        const float pipTop = top + S(23.0f);
        for (int i = 0; i < count; i++)
        {
            const float width = i == index ? current : pip;
            const ImU32 color = i != index ? PIP : row.disabled ? PIP : row.ring ? ACCENT : TEXT_3;
            draw->AddRectFilled(ImVec2(x, pipTop), ImVec2(x + width, pipTop + pip), color, pip * 0.5f);
            x += width + gap;
        }

        int step = row.step;
        if (row.clicked)
            step = onLeft ? -1 : onRight ? 1 : 0;
        int changed = index;
        if (!row.disabled)
        {
            if (step != 0)
                changed = std::clamp(index + step, 0, count - 1);  // stops at the ends
            else if (row.pressed && !row.clicked)
                changed = (index + 1) % count;  // A: the next, round
            if (row.reset)
                changed = defaultIndex;
        }
        Describe(row, spec, RowKind::Selector, defaultIndex >= 0 && defaultIndex < count ? names[size_t(defaultIndex)] : std::string(), "Next");
        if (changed == index)
            return false;
        index = changed;
        return true;
    }

    bool Slider(const RowSpec& spec, int& value, int low, int high, int step, const char* format, int defaultValue)
    {
        const Row row = BeginRow(spec.label, ROW_HEIGHT, !spec.disabledWhy.empty());
        DrawLabel(row, spec);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float right = ControlRight(row), left = right - S(CONTROL_WIDTH);
        const float trackRight = right - S(SLIDER_VALUE + SLIDER_GAP);
        const float centerY = (row.min.y + row.max.y) * 0.5f;

        int changed = value;
        if (!row.disabled)
        {
            // Dragged with the mouse from a press on the track (it's a
            // little taller than it's drawn).
            static ImGuiID s_dragging = 0;
            if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                if (mouse.x >= left - S(8.0f) && mouse.x <= trackRight + S(8.0f))
                    s_dragging = row.id;
            }
            if (s_dragging == row.id)
            {
                if (row.active && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    const float t = std::clamp((ImGui::GetIO().MousePos.x - left) / (trackRight - left), 0.0f, 1.0f);
                    changed = low + int(std::lround(t * float(high - low) / float(step))) * step;
                }
                else
                    s_dragging = 0;
            }
            if (row.step != 0)
            {
                // Snapped to the steps, should the file hold something between.
                const int steps = int(std::lround(double(value - low) / double(step))) + row.step;
                changed = std::clamp(low + steps * step, low, high);
            }
            if (row.reset)
                changed = defaultValue;
        }

        const float t = std::clamp(float(changed - low) / float(high - low), 0.0f, 1.0f);
        const float trackHalf = S(3.0f);
        const float knobX = left + (trackRight - left) * t;
        const bool dragging = row.active && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        draw->AddRectFilled(ImVec2(left, centerY - trackHalf), ImVec2(trackRight, centerY + trackHalf), TRACK, trackHalf);
        const ImU32 fill = row.disabled ? IM_COL32(47, 58, 72, 255) : row.ring || dragging ? ACCENT : FILL_IDLE;
        if (knobX > left)
            draw->AddRectFilled(ImVec2(left, centerY - trackHalf), ImVec2(knobX, centerY + trackHalf), fill, trackHalf);
        const float knob = S(8.0f);
        if (row.disabled)
            draw->AddCircleFilled(ImVec2(knobX, centerY), knob, IM_COL32(70, 81, 95, 255));
        else if (dragging)
        {
            draw->AddCircleFilled(ImVec2(knobX, centerY), knob + S(5.0f), Alpha(ACCENT, 0.25f));
            draw->AddCircleFilled(ImVec2(knobX, centerY), knob, ACCENT);
        }
        else if (row.ring)
        {
            draw->AddCircleFilled(ImVec2(knobX, centerY), knob + S(4.0f), Alpha(ACCENT, 0.30f));
            draw->AddCircleFilled(ImVec2(knobX, centerY), knob, IM_COL32_WHITE);
        }
        else
            draw->AddCircleFilled(ImVec2(knobX, centerY), knob, KNOB);
        char text[32];
        std::snprintf(text, sizeof(text), format, changed);
        TextMiddle(draw, Face::Mono, 15.0f, right - TextWidth(Face::Mono, 15.0f, text), centerY, row.disabled ? TEXT_3 : row.ring ? ACCENT : TEXT, text);

        char defaultText[32];
        std::snprintf(defaultText, sizeof(defaultText), format, defaultValue);
        Describe(row, spec, RowKind::Slider, defaultText, "");
        if (changed == value)
            return false;
        value = changed;
        return true;
    }

    bool Toggle(const RowSpec& spec, bool& value, bool defaultValue)
    {
        const Row row = BeginRow(spec.label, ROW_HEIGHT, !spec.disabledWhy.empty());
        DrawLabel(row, spec);
        bool changed = value;
        if (!row.disabled)
        {
            if (row.pressed)
                changed = !value;
            else if (row.step != 0)
                changed = row.step > 0;
            if (row.reset)
                changed = defaultValue;
        }

        // The knob slides over 120 ms.
        float& slide = *ImGui::GetStateStorage()->GetFloatRef(row.id, changed ? 1.0f : 0.0f);
        const float delta = ImGui::GetIO().DeltaTime / TOGGLE_SECONDS;
        slide = changed ? std::min(slide + delta, 1.0f) : std::max(slide - delta, 0.0f);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float right = ControlRight(row);
        const float centerY = (row.min.y + row.max.y) * 0.5f;
        const ImVec2 min(right - S(TOGGLE_WIDTH), centerY - S(TOGGLE_HEIGHT * 0.5f)), max(right, centerY + S(TOGGLE_HEIGHT * 0.5f));
        ImU32 track = row.disabled ? IM_COL32(34, 43, 54, 255) : Mix(TOGGLE_OFF, ACCENT, slide);
        if (row.active)
            track = Darker(track, 0.85f);
        const ImU32 knob = row.disabled ? IM_COL32(71, 82, 95, 255) : Mix(KNOB_OFF, IM_COL32_WHITE, slide);
        draw->AddRectFilled(min, max, track, S(TOGGLE_HEIGHT * 0.5f));
        const float inset = S(3.0f), radius = S(TOGGLE_KNOB * 0.5f);
        const float knobX = min.x + inset + radius + (max.x - min.x - 2.0f * (inset + radius)) * slide;
        draw->AddCircleFilled(ImVec2(knobX, centerY), radius, knob);
        const char* state = changed ? "On" : "Off";
        TextMiddle(draw, Face::Sans, 14.0f, min.x - S(10.0f) - TextWidth(Face::Sans, 14.0f, state), centerY,
            row.disabled ? TEXT_3 : row.ring ? TEXT : TEXT_2, state);

        Describe(row, spec, RowKind::Toggle, defaultValue ? "On" : "Off", "Toggle");
        if (changed == value)
            return false;
        value = changed;
        return true;
    }

    bool Button(const RowSpec& spec, ImWchar icon, std::string_view text)
    {
        const Row row = BeginRow(spec.label, ROW_HEIGHT, !spec.disabledWhy.empty());
        DrawLabel(row, spec);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float right = ControlRight(row);
        const float centerY = (row.min.y + row.max.y) * 0.5f;
        const Face face = row.ring ? Face::SemiBold : Face::Sans;
        const float width = S(11.0f) + S(18.0f) + S(7.0f) + TextWidth(face, 15.0f, text) + S(14.0f);
        const ImVec2 min(right - width, centerY - S(BUTTON_HEIGHT * 0.5f)), max(right, centerY + S(BUTTON_HEIGHT * 0.5f));
        const bool onButton = ImGui::IsMouseHoveringRect(min, max);
        const bool held = row.active && (onButton || !ImGui::IsMouseDown(ImGuiMouseButton_Left));
        ImU32 fill = BUTTON, ink = TEXT, iconColor = TEXT_2;
        if (row.disabled)
        {
            fill = IM_COL32(26, 34, 44, 255);
            ink = iconColor = TEXT_3;
        }
        else if (held)
        {
            fill = BUTTON_ACTIVE;
            ink = iconColor = ACCENT_INK;
        }
        else if (row.ring)
        {
            fill = ACCENT;
            ink = iconColor = ACCENT_INK;
        }
        else if (row.hovered)
            fill = BUTTON_HOVERED;
        draw->AddRectFilled(min, max, fill, S(7.0f));
        Icon(draw, icon, 18.0f, ImVec2(min.x + S(11.0f + 9.0f), centerY), iconColor);
        TextMiddle(draw, face, 15.0f, min.x + S(11.0f + 18.0f + 7.0f), centerY, ink, text);

        Describe(row, spec, RowKind::Button, "", "Press");
        return !row.disabled && row.pressed && (!row.clicked || onButton);
    }

    bool TextField(const RowSpec& spec, std::string& value, size_t maxLength, std::string_view shown)
    {
        const Row row = BeginRow(spec.label, ROW_HEIGHT, !spec.disabledWhy.empty());
        DrawLabel(row, spec);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float right = ControlRight(row), left = right - S(CONTROL_WIDTH);
        const float centerY = (row.min.y + row.max.y) * 0.5f;
        const bool editing = s_editing == row.id;
        if (!editing && !row.disabled && row.pressed)
        {
            s_editing = row.id;
            s_focusEditor = true;
        }

        bool changed = false;
        if (s_editing == row.id)
        {
            // The field in the value's place, over the row.
            const ImVec2 after = ImGui::GetCursorScreenPos();
            FontScope font(Face::Sans, LABEL_SIZE);
            ImGui::SetCursorScreenPos(ImVec2(left, centerY - ImGui::GetFrameHeight() * 0.5f));
            ImGui::SetNextItemWidth(right - left);
            if (std::exchange(s_focusEditor, false))
                ImGui::SetKeyboardFocusHere();
            char buffer[64] = {};
            std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
            ImGui::PushID(int(row.id));
            const bool entered = ImGui::InputText("##text", buffer, std::min(sizeof(buffer), maxLength + 1),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const bool finished = ImGui::IsItemDeactivated();
            ImGui::PopID();
            changed = value != buffer;
            value = buffer;
            if (entered || finished)
                s_editing = 0;
            ImGui::SetCursorScreenPos(after);
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        }
        else
        {
            const std::string text(value.empty() ? shown : std::string_view(value));
            const float width = std::min(TextWidth(Face::Sans, LABEL_SIZE, text), right - left);
            TextEllipsis(draw, Face::Sans, LABEL_SIZE, ImVec2(right - S(8.0f) - width, centerY - std::round(LABEL_SIZE * Scale()) * 0.52f), right,
                row.disabled ? TEXT_3 : row.ring ? ACCENT : value.empty() ? TEXT_2 : TEXT, text);
        }
        Describe(row, spec, RowKind::Text, "", "Edit");
        return changed;
    }

    void CustomRow(int id, float height, const RowInfo& info, const std::function<void(ImDrawList*, ImVec2, ImVec2, bool, bool)>& draw)
    {
        const std::string name = std::to_string(id);
        const Row row = BeginRow(name, height, false);
        draw(ImGui::GetWindowDrawList(), row.min, row.max, row.ring, row.hovered);
        if (row.describe)
            s_current = info;
    }

    void GroupHeader(std::string_view name)
    {
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        ImGui::Dummy(ImVec2(0.0f, std::max(S(8.0f) - spacing, 0.0f)));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float centerY = pos.y + S(15.0f);
        TextMiddle(draw, Face::SemiBold, 12.0f, pos.x, centerY, TEXT_3, name);
        const float ruleLeft = pos.x + TextWidth(Face::SemiBold, 12.0f, name) + S(10.0f);
        draw->AddRectFilled(ImVec2(ruleLeft, std::round(centerY)), ImVec2(pos.x + width, std::round(centerY) + Stroke(1.0f)), LINE);
        ImGui::Dummy(ImVec2(width, S(30.0f)));
    }

    void Card(std::string_view title, std::string_view right, ImU32 rightColor, const std::function<float(ImDrawList*, ImVec2, float)>& content)
    {
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        ImGui::Dummy(ImVec2(0.0f, std::max(S(8.0f) - spacing, 0.0f)));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->ChannelsSplit(2);
        draw->ChannelsSetCurrent(1);
        float y = pos.y + S(12.0f);
        if (!title.empty())
        {
            Text(draw, Face::SemiBold, 12.0f, ImVec2(pos.x + S(14.0f), y), TEXT_3, title);
            if (!right.empty())
                Text(draw, Face::Mono, 12.0f, ImVec2(pos.x + width - S(14.0f) - TextWidth(Face::Mono, 12.0f, right), y), rightColor, right);
            y += S(18.0f + 6.0f);
        }
        y += content(draw, ImVec2(pos.x + S(14.0f), y), width - S(28.0f));
        y += S(12.0f);
        draw->ChannelsSetCurrent(0);
        draw->AddRectFilled(pos, ImVec2(pos.x + width, y), INK_1, S(10.0f));
        draw->AddRect(pos, ImVec2(pos.x + width, y), LINE, S(10.0f), 0, Stroke(1.0f));
        draw->ChannelsMerge();
        ImGui::Dummy(ImVec2(width, y - pos.y));
    }

    float KeyValues(ImDrawList* draw, ImVec2 pos, float width, std::span<const std::pair<std::string, std::string>> lines)
    {
        const float pitch = S(22.0f), key = S(150.0f + 12.0f);
        float y = pos.y;
        for (const auto& [label, value] : lines)
        {
            const float centerY = y + pitch * 0.5f;
            TextMiddle(draw, Face::Sans, 14.0f, pos.x, centerY, TEXT_2, label);
            TextEllipsis(draw, Face::Mono, 13.5f, ImVec2(pos.x + key, centerY - std::round(13.5f * Scale()) * 0.52f), pos.x + width, TEXT, value);
            y += pitch;
        }
        return y - pos.y;
    }

    float DrawGlyph(ImDrawList* draw, Glyph glyph, float x, float centerY)
    {
        switch (glyph)
        {
        case Glyph::A:
        case Glyph::B:
        case Glyph::X:
        case Glyph::Y:
        {
            static constexpr const char* LETTERS[] = { "A", "B", "X", "Y" };
            static constexpr ImU32 COLORS[] = { PAD_A, PAD_B, PAD_X, PAD_Y };
            const int i = int(glyph) - int(Glyph::A);
            const float radius = S(11.0f);
            draw->AddCircleFilled(ImVec2(x + radius, centerY), radius, GLYPH_BG);
            const float width = TextWidth(Face::SemiBold, 12.5f, LETTERS[i]);
            TextMiddle(draw, Face::SemiBold, 12.5f, x + radius - width * 0.5f, centerY, COLORS[i], LETTERS[i]);
            return radius * 2.0f;
        }
        case Glyph::LB:
        case Glyph::RB:
        {
            const char* text = glyph == Glyph::LB ? "LB" : "RB";
            const float width = S(30.0f);
            draw->AddRectFilled(ImVec2(x, centerY - S(10.0f)), ImVec2(x + width, centerY + S(10.0f)), GLYPH_BG, S(6.0f));
            TextMiddle(draw, Face::SemiBold, 11.5f, x + (width - TextWidth(Face::SemiBold, 11.5f, text)) * 0.5f, centerY, TEXT, text);
            return width;
        }
        default:
        {
            // A cross with the axis in use lit.
            const float size = S(20.0f), bar = S(6.0f), rounding = S(2.0f);
            const float top = centerY - size * 0.5f, middle = x + (size - bar) * 0.5f;
            const bool vertical = glyph == Glyph::DpadVertical;
            const ImU32 dim = IM_COL32(58, 70, 86, 255);
            draw->AddRectFilled(ImVec2(x, top + (size - bar) * 0.5f), ImVec2(x + size, top + (size + bar) * 0.5f), vertical ? dim : TEXT, rounding);
            draw->AddRectFilled(ImVec2(middle, top), ImVec2(middle + bar, top + size), vertical ? TEXT : dim, rounding);
            return size;
        }
        }
    }

    float DrawKeyCap(ImDrawList* draw, std::string_view key, float x, float centerY)
    {
        const float width = std::max(S(24.0f), TextWidth(Face::Mono, 12.0f, key) + S(12.0f));
        const ImVec2 min(x, centerY - S(11.0f)), max(x + width, centerY + S(11.0f));
        const float rounding = S(5.0f);
        draw->AddRectFilled(min, max, KEYCAP, rounding);
        draw->AddRectFilled(ImVec2(min.x, max.y - S(2.0f)), max, KEYCAP_SHADOW, rounding, ImDrawFlags_RoundCornersBottom);
        draw->AddRect(min, max, KEYCAP_EDGE, rounding, 0, Stroke(1.0f));
        TextMiddle(draw, Face::Mono, 12.0f, x + (width - TextWidth(Face::Mono, 12.0f, key)) * 0.5f, centerY - S(1.0f), TEXT, key);
        return width;
    }
}
