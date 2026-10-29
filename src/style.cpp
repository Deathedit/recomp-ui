#include "internal.h"

#include <rui/style.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include <imgui_internal.h>  // ImTextCharToUtf8, ImFontCalcWordWrapPositionEx

namespace rui
{
    namespace
    {
        ImFont* s_fonts[3] = {};
        float s_scale = 0.0f;

        // The text faces' size at which the icons are merged in, and the
        // icons': Material Symbols' em (960 of its 1152 units of height) is
        // then 20 pixels on 16.5 pixel text.
        constexpr float TEXT_REFERENCE = 16.5f, ICON_REFERENCE = 24.0f;
        constexpr float ICON_EM = ICON_REFERENCE * 960.0f / 1152.0f / TEXT_REFERENCE;  // an icon's height per pixel of font size

        // Plex's capital letters (698 of 1300 units of height) centre this far
        // down a line of text, per pixel of font size.
        constexpr float CAP_MIDDLE = (1025.0f - 698.0f * 0.5f) / 1300.0f;

        ImVec4 Hex(uint32_t rgb, float alpha)
        {
            return ImVec4(float((rgb >> 16) & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f, float(rgb & 0xFF) / 255.0f, alpha);
        }

        // The ImGuiStyle, at scale 1.
        ImGuiStyle BaseStyle()
        {
            ImGuiStyle style;
            style.WindowPadding = ImVec2(0.0f, 0.0f);
            style.WindowRounding = 14.0f;
            style.WindowBorderSize = 1.0f;
            style.WindowMinSize = ImVec2(32.0f, 32.0f);
            style.ChildRounding = 10.0f;
            style.ChildBorderSize = 1.0f;
            style.PopupRounding = 10.0f;
            style.PopupBorderSize = 1.0f;
            style.FramePadding = ImVec2(10.0f, 6.0f);
            style.FrameRounding = 7.0f;
            style.FrameBorderSize = 0.0f;
            style.ItemSpacing = ImVec2(8.0f, 2.0f);
            style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
            style.CellPadding = ImVec2(6.0f, 3.0f);
            style.IndentSpacing = 16.0f;
            style.ScrollbarSize = 6.0f;
            style.ScrollbarRounding = 3.0f;
            style.GrabMinSize = 16.0f;
            style.GrabRounding = 8.0f;
            style.TabRounding = 8.0f;
            style.TabBorderSize = 0.0f;
            style.SeparatorTextBorderSize = 1.0f;
            style.ImageBorderSize = 0.0f;
            style.TreeLinesSize = 1.0f;
            style.DisplaySafeAreaPadding = ImVec2(0.0f, 0.0f);
            style.AntiAliasedLines = style.AntiAliasedFill = true;
            style.CircleTessellationMaxError = 0.30f;
            style.Alpha = 1.0f;
            style.DisabledAlpha = 0.45f;

            ImVec4* colors = style.Colors;
            const ImVec4 accent = Hex(0x5AB4FF, 1.0f), clear = Hex(0x000000, 0.0f);
            colors[ImGuiCol_Text] = Hex(0xE6EBF2, 1.0f);
            colors[ImGuiCol_TextDisabled] = Hex(0x5C6776, 1.0f);
            colors[ImGuiCol_WindowBg] = Hex(0x11161D, 0.97f);
            colors[ImGuiCol_ChildBg] = clear;
            colors[ImGuiCol_PopupBg] = Hex(0x161C25, 0.98f);
            colors[ImGuiCol_Border] = Hex(0xFFFFFF, 0.07f);
            colors[ImGuiCol_BorderShadow] = clear;
            colors[ImGuiCol_FrameBg] = Hex(0x18202A, 1.0f);
            colors[ImGuiCol_FrameBgHovered] = Hex(0x222C39, 1.0f);
            colors[ImGuiCol_FrameBgActive] = Hex(0x223249, 1.0f);
            colors[ImGuiCol_TitleBg] = colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_TitleBgCollapsed] = Hex(0x141A23, 1.0f);
            colors[ImGuiCol_MenuBarBg] = Hex(0x0C1016, 1.0f);
            colors[ImGuiCol_ScrollbarBg] = clear;
            colors[ImGuiCol_ScrollbarGrab] = Hex(0x2C3746, 1.0f);
            colors[ImGuiCol_ScrollbarGrabHovered] = Hex(0x3A4757, 1.0f);
            colors[ImGuiCol_ScrollbarGrabActive] = accent;
            colors[ImGuiCol_CheckMark] = accent;
            colors[ImGuiCol_CheckboxSelectedBg] = accent;
            colors[ImGuiCol_SliderGrab] = Hex(0xDDE5EF, 1.0f);
            colors[ImGuiCol_SliderGrabActive] = accent;
            colors[ImGuiCol_Button] = Hex(0x222C39, 1.0f);
            colors[ImGuiCol_ButtonHovered] = Hex(0x2A3646, 1.0f);
            colors[ImGuiCol_ButtonActive] = Hex(0x3C8FD6, 1.0f);
            colors[ImGuiCol_Header] = Hex(0x5AB4FF, 0.13f);
            colors[ImGuiCol_HeaderHovered] = Hex(0x18202A, 1.0f);
            colors[ImGuiCol_HeaderActive] = Hex(0x223249, 1.0f);
            colors[ImGuiCol_Separator] = Hex(0x242D39, 1.0f);
            colors[ImGuiCol_SeparatorHovered] = Hex(0x3A4757, 1.0f);
            colors[ImGuiCol_SeparatorActive] = accent;
            colors[ImGuiCol_ResizeGrip] = colors[ImGuiCol_ResizeGripHovered] = colors[ImGuiCol_ResizeGripActive] = clear;
            colors[ImGuiCol_InputTextCursor] = Hex(0xE6EBF2, 1.0f);
            colors[ImGuiCol_Tab] = colors[ImGuiCol_TabDimmed] = Hex(0x0C1016, 1.0f);
            colors[ImGuiCol_TabHovered] = colors[ImGuiCol_TabDimmedSelected] = Hex(0x18202A, 1.0f);
            colors[ImGuiCol_TabSelected] = Hex(0x5AB4FF, 0.13f);
            colors[ImGuiCol_TabSelectedOverline] = accent;
            colors[ImGuiCol_TabDimmedSelectedOverline] = clear;
            colors[ImGuiCol_PlotLines] = accent;
            colors[ImGuiCol_PlotLinesHovered] = Hex(0xF2B24C, 1.0f);
            colors[ImGuiCol_PlotHistogram] = Hex(0x3F7FB8, 1.0f);
            colors[ImGuiCol_PlotHistogramHovered] = accent;
            colors[ImGuiCol_TableHeaderBg] = Hex(0x0C1016, 1.0f);
            colors[ImGuiCol_TableBorderStrong] = Hex(0x242D39, 1.0f);
            colors[ImGuiCol_TableBorderLight] = Hex(0x1B222C, 1.0f);
            colors[ImGuiCol_TableRowBg] = clear;
            colors[ImGuiCol_TableRowBgAlt] = Hex(0xFFFFFF, 0.02f);
            colors[ImGuiCol_TextLink] = accent;
            colors[ImGuiCol_TextSelectedBg] = Hex(0x5AB4FF, 0.30f);
            colors[ImGuiCol_TreeLines] = Hex(0x242D39, 1.0f);
            colors[ImGuiCol_DragDropTarget] = colors[ImGuiCol_UnsavedMarker] = Hex(0xF2B24C, 1.0f);
            colors[ImGuiCol_DragDropTargetBg] = clear;
            colors[ImGuiCol_NavCursor] = Hex(0x5AB4FF, 0.0f);  // hidden: rows draw their own ring
            colors[ImGuiCol_NavWindowingHighlight] = Hex(0x5AB4FF, 0.70f);
            colors[ImGuiCol_NavWindowingDimBg] = Hex(0x07090C, 0.60f);
            colors[ImGuiCol_ModalWindowDimBg] = Hex(0x07090C, 0.70f);
            return style;
        }

        ImFont* AddFace(const unsigned char* data, size_t size, const char* name, bool icons)
        {
            ImGuiIO& io = ImGui::GetIO();
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;  // static arrays (internal.h)
            std::snprintf(config.Name, sizeof(config.Name), "%s", name);
            ImFont* font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), int(size), TEXT_REFERENCE, &config);
            if (font != nullptr && icons)
            {
                ImFontConfig merge;
                merge.FontDataOwnedByAtlas = false;
                merge.MergeMode = true;
                // Icons inline with text: 20 pixels wide at least, centred on
                // the line (Icon() places them by their drawing instead).
                merge.GlyphMinAdvanceX = 20.0f;
                merge.GlyphOffset.y = 5.0f;
                static const ImWchar RANGES[] = { 0xE000, 0xF8FF, 0 };
                merge.GlyphRanges = RANGES;
                std::snprintf(merge.Name, sizeof(merge.Name), "Icons");
                io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(fonts::ICONS), int(fonts::ICONS_SIZE),
                    ICON_REFERENCE, &merge);
            }
            return font;
        }

        float FontPixels(float size)
        {
            return std::max(1.0f, std::round(size * s_scale));
        }

        // How much of `text` fits in `width` with an ellipsis after it.
        size_t FittingPrefix(ImFont* font, float pixels, std::string_view text, float width)
        {
            size_t end = 0;
            const char* begin = text.data();
            const char* stop = text.data() + text.size();
            for (const char* p = begin; p < stop;)
            {
                unsigned int c = 0;
                const int length = ImTextCharFromUtf8(&c, p, stop);
                const char* next = p + std::max(length, 1);
                if (font->CalcTextSizeA(pixels, FLT_MAX, 0.0f, begin, next).x > width)
                    break;
                end = size_t(next - begin);
                p = next;
            }
            return end;
        }
    }

}

namespace rui::detail
{
    void InitStyle()
    {
        s_fonts[int(Face::Sans)] = AddFace(fonts::SANS_REGULAR, fonts::SANS_REGULAR_SIZE, "IBM Plex Sans", true);
        s_fonts[int(Face::SemiBold)] = AddFace(fonts::SANS_SEMIBOLD, fonts::SANS_SEMIBOLD_SIZE, "IBM Plex Sans SemiBold", true);
        s_fonts[int(Face::Mono)] = AddFace(fonts::MONO_MEDIUM, fonts::MONO_MEDIUM_SIZE, "IBM Plex Mono Medium", false);
        ImGui::GetIO().FontDefault = s_fonts[int(Face::Sans)];
        ImGuiIO& io = ImGui::GetIO();
        io.KeyRepeatDelay = 0.35f;
        io.KeyRepeatRate = 0.08f;
        UpdateScale(1280.0f, 720.0f, 0);
    }

    void UpdateScale(float width, float height, int userPercent)
    {
        float scale = std::min(width / 1280.0f, height / 720.0f);
        scale *= userPercent > 0 ? float(userPercent) / 100.0f : 1.0f;
        scale = std::min({ scale, 0.96f * height / 560.0f, 0.96f * width / 860.0f });
        scale = std::clamp(scale, 0.75f, 4.0f);
        if (scale == s_scale)
            return;
        s_scale = scale;
        // A fresh copy each time: sizes scaled once.
        ImGuiStyle style = BaseStyle();
        style.ScaleAllSizes(scale);
        style.FontSizeBase = FontPixels(TEXT_REFERENCE);
        style.FontScaleMain = 1.0f;
        ImGui::GetStyle() = style;
    }
}

namespace rui
{
    float Scale()
    {
        return s_scale;
    }

    float S(float pixels)
    {
        return std::round(pixels * s_scale);
    }

    float Stroke(float pixels)
    {
        return std::max(1.0f, std::round(pixels * s_scale));
    }

    ImFont* Font(Face face)
    {
        return s_fonts[int(face)];
    }

    FontScope::FontScope(Face face, float size)
    {
        ImGui::PushFont(Font(face), FontPixels(size));
    }

    FontScope::~FontScope()
    {
        ImGui::PopFont();
    }

    ImU32 Alpha(ImU32 color, float alpha)
    {
        const float a = float((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);
        return (color & ~IM_COL32_A_MASK) | (ImU32(a + 0.5f) << IM_COL32_A_SHIFT);
    }

    float TextWidth(Face face, float size, std::string_view text)
    {
        return Font(face)->CalcTextSizeA(FontPixels(size), FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
    }

    void Text(ImDrawList* draw, Face face, float size, ImVec2 pos, ImU32 color, std::string_view text)
    {
        draw->AddText(Font(face), FontPixels(size), ImVec2(std::round(pos.x), std::round(pos.y)), color, text.data(), text.data() + text.size());
    }

    void TextMiddle(ImDrawList* draw, Face face, float size, float x, float centerY, ImU32 color, std::string_view text)
    {
        Text(draw, face, size, ImVec2(x, centerY - FontPixels(size) * CAP_MIDDLE), color, text);
    }

    void TextEllipsis(ImDrawList* draw, Face face, float size, ImVec2 pos, float maxX, ImU32 color, std::string_view text)
    {
        ImFont* font = Font(face);
        const float pixels = FontPixels(size);
        const float width = maxX - pos.x;
        if (font->CalcTextSizeA(pixels, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x <= width)
        {
            Text(draw, face, size, pos, color, text);
            return;
        }
        constexpr std::string_view ELLIPSIS = "\xE2\x80\xA6";
        const float ellipsis = font->CalcTextSizeA(pixels, FLT_MAX, 0.0f, ELLIPSIS.data(), ELLIPSIS.data() + ELLIPSIS.size()).x;
        std::string cut(text.substr(0, FittingPrefix(font, pixels, text, width - ellipsis)));
        while (!cut.empty() && cut.back() == ' ')
            cut.pop_back();
        cut += ELLIPSIS;
        Text(draw, face, size, pos, color, cut);
    }

    void TextWrapped(ImDrawList* draw, Face face, float size, ImVec2 pos, float width, int lines, ImU32 color, std::string_view text, float firstWidth)
    {
        // Lines 1.35 apart (ImGui's own wrapping sets them 1.0 apart).
        ImFont* font = Font(face);
        const float pixels = FontPixels(size);
        const char* p = text.data();
        const char* end = text.data() + text.size();
        for (int line = 0; line < lines && p < end; line++)
        {
            while (p < end && *p == ' ')
                p++;
            const float lineWidth = line == 0 && firstWidth >= 0.0f ? firstWidth : width;
            const char* stop = ImFontCalcWordWrapPositionEx(font, pixels, p, end, lineWidth);
            if (stop <= p)
                stop = std::min(p + 1, end);
            std::string_view part(p, size_t(stop - p));
            const bool last = line == lines - 1 && stop < end;
            const ImVec2 at(pos.x, pos.y + std::round(float(line) * pixels * 1.35f));
            if (last)
                TextEllipsis(draw, face, size, at, pos.x + lineWidth, color, std::string_view(p, size_t(end - p)));
            else
                Text(draw, face, size, at, color, part);
            p = stop;
            if (p < end && *p == '\n')
                p++;
        }
    }

    int WrappedLines(Face face, float size, float width, std::string_view text, float firstWidth)
    {
        ImFont* font = Font(face);
        const float pixels = FontPixels(size);
        const char* p = text.data();
        const char* end = text.data() + text.size();
        int lines = 0;
        while (p < end)
        {
            while (p < end && *p == ' ')
                p++;
            if (p >= end)
                break;
            const char* stop = ImFontCalcWordWrapPositionEx(font, pixels, p, end, lines == 0 && firstWidth >= 0.0f ? firstWidth : width);
            p = stop <= p ? std::min(p + 1, end) : stop;
            if (p < end && *p == '\n')
                p++;
            lines++;
        }
        return lines;
    }

    void LifeBar(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, float border, float height, float fraction, ImU32 color)
    {
        // Inside the border: a box with corners `radius` round. Its edges at
        // a height, a few points down the bar.
        const float left = min.x + border, right = max.x - border, bottom = max.y - border;
        const float radius = std::max(rounding - border, 0.0f);
        const float top = bottom - S(height);
        const float end = left + (right - left) * std::clamp(fraction, 0.0f, 1.0f);
        if (end <= left)
            return;
        const auto inset = [&](float y) {
            const float up = bottom - y;  // from the bottom edge
            if (up >= radius)
                return 0.0f;
            const float d = radius - up;
            return radius - std::sqrt(std::max(radius * radius - d * d, 0.0f));
        };
        constexpr int STEPS = 4;
        ImVec2 points[2 * (STEPS + 1)];
        int count = 0;
        for (int i = 0; i <= STEPS; i++)
        {
            const float y = top + (bottom - top) * float(i) / float(STEPS);
            points[count++] = ImVec2(left + inset(y), y);
        }
        for (int i = STEPS; i >= 0; i--)
        {
            const float y = top + (bottom - top) * float(i) / float(STEPS);
            points[count++] = ImVec2(std::max(std::min(end, right - inset(y)), left + inset(y)), y);
        }
        draw->AddConvexPolyFilled(points, count, color);
    }

    void Icon(ImDrawList* draw, ImWchar icon, float size, ImVec2 center, ImU32 color)
    {
        ImFont* font = Font(Face::Sans);
        const float pixels = std::max(1.0f, std::round(size * s_scale / ICON_EM));
        ImFontGlyph* glyph = font->GetFontBaked(pixels)->FindGlyph(icon);
        if (glyph == nullptr)
            return;
        char text[5] = {};
        ImTextCharToUtf8(text, icon);
        const ImVec2 pos(std::round(center.x - (glyph->X0 + glyph->X1) * 0.5f), std::round(center.y - (glyph->Y0 + glyph->Y1) * 0.5f));
        draw->AddText(font, pixels, pos, color, text);
    }
}
