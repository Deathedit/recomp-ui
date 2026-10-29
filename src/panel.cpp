// The panel: a centred window over the darkened frame with a header (the
// app, where you are, the host's chip, Close), a sidebar of the pages, the
// page's rows, a description of the focused row and a footer of button
// hints. The toggle opens and closes it; up/down pick a row, left/right and
// A change it, Y resets it, LB/RB (Tab) switch pages, B (Esc) closes. It
// scales from its 1280x720 layout to the window's height.
#include "internal.h"

#include <rui/gate.h>
#include <rui/settings.h>
#include <rui/stats.h>
#include <rui/style.h>
#include <rui/toasts.h>
#include <rui/widgets.h>

#include <imgui_internal.h>  // ShadeVertsLinearColorGradientKeepAlpha

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>

namespace rui
{
    namespace
    {
        // Text in UTF-8.
#define RUI_TIMES "\xC3\x97"
#define RUI_MIDDOT "\xC2\xB7"

        // The panel at 720p.
        constexpr float PANEL_WIDTH = 860.0f, PANEL_HEIGHT = 560.0f, PANEL_ROUNDING = 14.0f;
        constexpr float HEADER_HEIGHT = 52.0f, SIDEBAR_WIDTH = 184.0f, DESCRIPTION_HEIGHT = 56.0f, FOOTER_HEIGHT = 46.0f;
        constexpr float FADE_SECONDS = 0.15f;

        std::vector<Page> s_pages;
        std::vector<int> s_pageRow;  // each page's focused row, restored on return
        std::vector<std::function<void()>> s_frameHooks;
        std::vector<std::function<void(bool)>> s_foregroundHooks;

        std::string s_appName, s_backendName;
        std::function<std::string()> s_headerChip;
        ImGuiKey s_toggleKey = ImGuiKey_F1;
        uint16_t s_toggleButtons = pad::BACK | pad::START, s_closeButtons = pad::B | pad::START;
        uint32_t s_width = 0, s_height = 0;
        int s_overlayScale = 0;

        bool s_visible = false;
        bool s_justOpened = false;
        float s_openAge = 0.0f;  // for the fade in
        bool s_panelFocused = false;
        int s_page = 0;
        int s_focusRequest = -1;
        bool s_focusAfterOpen = false;
        int s_lastFocused = -1;
        int s_descriptionLines = 2;  // the strip's, from the page's longest description
        bool s_scrollToTop = false;

        // SetTestScript's.
        TestScript s_test;
        int s_testPage = -1;
        float s_testAge = 0.0f;
        bool s_testPress = false;

        int PageCount()
        {
            return int(s_pages.size());
        }

        int FindPage(std::string_view key)
        {
            for (int i = 0; i < PageCount(); i++)
            {
                if (s_pages[size_t(i)].key == key)
                    return i;
            }
            return -1;
        }

        void SelectPage(int page)
        {
            if (PageCount() == 0)
                return;
            page = (page + PageCount()) % PageCount();
            if (page == s_page)
                return;
            s_page = page;
            s_focusRequest = s_pageRow[size_t(page)];
            s_lastFocused = -1;
            s_scrollToTop = true;  // then down to the remembered row
            if (CurrentInput() != InputMode::Mouse)
                ImGui::SetNavCursorVisible(true);
        }

        // ImGui's key for a pad button (rui::pad).
        ImGuiKey PadKey(uint16_t button)
        {
            switch (button)
            {
            case pad::DPAD_UP: return ImGuiKey_GamepadDpadUp;
            case pad::DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
            case pad::DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
            case pad::DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
            case pad::START: return ImGuiKey_GamepadStart;
            case pad::BACK: return ImGuiKey_GamepadBack;
            case pad::LEFT_THUMB: return ImGuiKey_GamepadL3;
            case pad::RIGHT_THUMB: return ImGuiKey_GamepadR3;
            case pad::LEFT_SHOULDER: return ImGuiKey_GamepadL1;
            case pad::RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
            case pad::A: return ImGuiKey_GamepadFaceDown;
            case pad::B: return ImGuiKey_GamepadFaceRight;
            case pad::X: return ImGuiKey_GamepadFaceLeft;
            case pad::Y: return ImGuiKey_GamepadFaceUp;
            default: return ImGuiKey_None;
            }
        }

        // One of `buttons` pressed this frame while the others are held:
        // each alone stays the game's.
        bool ChordPressed(uint16_t buttons)
        {
            if (buttons == 0)
                return false;
            bool pressed = false, held = true;
            for (int bit = 0; bit < 16; bit++)
            {
                const ImGuiKey key = PadKey(uint16_t(1u << bit));
                if ((buttons & (1u << bit)) == 0 || key == ImGuiKey_None)
                    continue;
                pressed |= ImGui::IsKeyPressed(key, false);
                held &= ImGui::IsKeyDown(key);
            }
            return pressed && held;
        }

        bool AnyPressed(uint16_t buttons)
        {
            for (int bit = 0; bit < 16; bit++)
            {
                const ImGuiKey key = PadKey(uint16_t(1u << bit));
                if ((buttons & (1u << bit)) != 0 && key != ImGuiKey_None && ImGui::IsKeyPressed(key, false))
                    return true;
            }
            return false;
        }

        float HintWidth(float glyphWidth, std::string_view label)
        {
            return glyphWidth + S(7.0f) + TextWidth(Face::Sans, 14.0f, label);
        }

        // A footer hint: its glyph (pad), key cap (keyboard) and its label.
        float Hint(ImDrawList* draw, float x, float centerY, std::optional<Glyph> glyph, std::string_view key, std::string_view label)
        {
            float width = 0.0f;
            if (CurrentInput() == InputMode::Pad && glyph)
            {
                if (*glyph == Glyph::LB)
                {
                    width = DrawGlyph(draw, Glyph::LB, x, centerY);
                    width += S(4.0f) + DrawGlyph(draw, Glyph::RB, x + width + S(4.0f), centerY);
                }
                else
                    width = DrawGlyph(draw, *glyph, x, centerY);
            }
            else
                width = DrawKeyCap(draw, key, x, centerY);
            TextMiddle(draw, Face::Sans, 14.0f, x + width + S(7.0f), centerY, TEXT_2, label);
            return HintWidth(width, label);
        }

        void Footer(ImDrawList* draw, ImVec2 min, ImVec2 max)
        {
            const float centerY = (min.y + max.y) * 0.5f;
            float x = min.x + S(18.0f);
            const float gap = S(20.0f);
            const RowInfo& row = CurrentRow();
            if (ImGui::GetIO().WantTextInput)
            {
                x += Hint(draw, x, centerY, std::nullopt, "Enter", "Done") + gap;
                x += Hint(draw, x, centerY, std::nullopt, "Esc", "Cancel") + gap;
            }
            else if (CurrentInput() == InputMode::Mouse)
            {
                for (const auto& [key, label] : { std::pair{ "Click", "Select" }, std::pair{ "Drag", "Adjust" }, std::pair{ "Wheel", "Scroll" },
                         std::pair{ "Right-click", "Default" }, std::pair{ "Esc", "Close" } })
                    x += Hint(draw, x, centerY, std::nullopt, key, label) + gap;
            }
            else
            {
                x += Hint(draw, x, centerY, Glyph::DpadVertical, "\xE2\x86\x91\xE2\x86\x93", "Move") + gap;
                const bool changes = !row.disabled && (row.kind == RowKind::Selector || row.kind == RowKind::Slider || row.kind == RowKind::Toggle);
                if (changes)
                    x += Hint(draw, x, centerY, Glyph::DpadHorizontal, "\xE2\x86\x90\xE2\x86\x92", "Change") + gap;
                if (!row.disabled && !row.actionLabel.empty())
                    x += Hint(draw, x, centerY, Glyph::A, "Enter", row.actionLabel) + gap;
                if (row.resettable)
                    x += Hint(draw, x, centerY, Glyph::Y, "Del", "Default") + gap;
                x += Hint(draw, x, centerY, Glyph::LB, "Tab", "Section") + gap;
                x += Hint(draw, x, centerY, Glyph::B, "Esc", "Close") + gap;
            }

            // The status on the right: the newest toast while there is one,
            // else whether the settings file has the changes.
            const float right = max.x - S(18.0f);
            if (const std::optional<detail::ToastStatus> toast = detail::NewestToast())
            {
                const float width = S(18.0f) + S(7.0f) + TextWidth(Face::SemiBold, 13.5f, toast->title);
                Icon(draw, toast->icon, 18.0f, ImVec2(right - width + S(9.0f), centerY), toast->color);
                TextMiddle(draw, Face::SemiBold, 13.5f, right - width + S(18.0f + 7.0f), centerY, TEXT, toast->title);
                return;
            }
            if (settings::Path().empty())
                return;
            const std::string file = settings::Path().filename().string();
            const settings::Status status = settings::CurrentStatus();
            const std::string text = status == settings::Status::Failed ? std::format("Couldn't write {}", file)
                : status == settings::Status::Pending ? std::format("Saving to {}", file) : std::format("Saved to {}", file);
            const ImU32 color = status == settings::Status::Failed ? BAD : TEXT_3;
            const float width = S(16.0f) + S(5.0f) + TextWidth(Face::Sans, 13.0f, text);
            if (right - width > x)
            {
                Icon(draw, status == settings::Status::Failed ? ICON_WARNING : ICON_CHECK_CIRCLE, 16.0f, ImVec2(right - width + S(8.0f), centerY), color);
                TextMiddle(draw, Face::Sans, 13.0f, right - width + S(16.0f + 5.0f), centerY, color, text);
            }
        }

        // The description strip: the focused (hovered) row's title and text,
        // its default (or a custom row's detail) on the right.
        void DescriptionStrip(ImDrawList* draw, ImVec2 min, ImVec2 max, int lines)
        {
            draw->AddRectFilled(min, max, Alpha(INK_1, 0.6f));
            draw->AddRectFilled(min, ImVec2(max.x, min.y + Stroke(1.0f)), LINE);
            const RowInfo& row = CurrentRow();
            const float top = min.y + S(8.0f);
            const float left = min.x + S(22.0f);
            Icon(draw, ICON_INFO, 18.0f, ImVec2(left + S(9.0f), top + S(11.0f)), TEXT_3);
            const float textRight = max.x - S(22.0f);
            float firstRight = textRight;
            if (!row.right.empty())
            {
                const float width = TextWidth(Face::Sans, 13.0f, row.right);
                Text(draw, Face::Sans, 13.0f, ImVec2(textRight - width, top + S(2.0f)), TEXT_3, row.right);
                firstRight -= width + S(12.0f);
            }
            const float textX = left + S(18.0f + 12.0f);
            if (row.kind == RowKind::None)
            {
                Text(draw, Face::Sans, 14.0f, ImVec2(textX, top + S(2.0f)), TEXT_2,
                    CurrentInput() == InputMode::Mouse ? "Point at a setting to see what it does." : "");
                return;
            }
            if (!row.title.empty())
            {
                TextEllipsis(draw, Face::SemiBold, 14.0f, ImVec2(textX, top + S(1.0f)), firstRight, TEXT, row.title);
                TextWrapped(draw, Face::Sans, 14.0f, ImVec2(textX, top + S(1.0f + 19.0f)), textRight - textX, lines - 1, TEXT_2, row.text);
            }
            else
                TextWrapped(draw, Face::Sans, 14.0f, ImVec2(textX, top + S(1.0f)), textRight - textX, lines, TEXT_2, row.text, firstRight - textX);
        }

        void Header(ImDrawList* draw, ImVec2 min, ImVec2 max)
        {
            const int first = draw->VtxBuffer.Size;
            draw->AddRectFilled(min, max, IM_COL32_WHITE, S(PANEL_ROUNDING) - 1.0f, ImDrawFlags_RoundCornersTop);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, first, draw->VtxBuffer.Size, min, ImVec2(min.x, max.y), IM_COL32(20, 26, 35, 255), INK_2);
            draw->AddRectFilled(ImVec2(min.x, max.y - Stroke(1.0f)), max, LINE);
            const float centerY = (min.y + max.y) * 0.5f;
            float x = min.x + S(18.0f);
            if (!s_appName.empty())
            {
                Text(draw, Face::Mono, 14.0f, ImVec2(x, centerY - std::round(14.0f * Scale()) * 0.52f), TEXT_2, s_appName);
                x += TextWidth(Face::Mono, 14.0f, s_appName) + S(12.0f);
            }
            if (PageCount() != 0)
            {
                const Page& page = s_pages[size_t(s_page)];
                const std::string crumb = std::format("{} / ", page.group);
                TextMiddle(draw, Face::Sans, 16.0f, x, centerY, TEXT_2, crumb);
                TextMiddle(draw, Face::SemiBold, 16.0f, x + TextWidth(Face::Sans, 16.0f, crumb), centerY, TEXT, page.name);
            }

            // Close, then the host's chip.
            float right = max.x - S(16.0f);
            const bool pad = CurrentInput() == InputMode::Pad;
            const float labelWidth = TextWidth(Face::Sans, 14.0f, "Close");
            const float glyphWidth = pad ? S(22.0f) : std::max(S(24.0f), TextWidth(Face::Mono, 12.0f, "Esc") + S(12.0f));
            const ImVec2 closeMin(right - S(4.0f + 6.0f) - labelWidth - S(6.0f) - glyphWidth, centerY - S(15.0f)), closeMax(right, centerY + S(15.0f));
            ImGui::SetCursorScreenPos(closeMin);
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            if (ImGui::InvisibleButton("##close", ImVec2(closeMax.x - closeMin.x, closeMax.y - closeMin.y)))
                SetVisible(false);
            const bool closeHovered = ImGui::IsItemHovered();
            ImGui::PopItemFlag();
            if (closeHovered)
                draw->AddRectFilled(closeMin, closeMax, INK_3, S(7.0f));
            float glyphX = closeMin.x + S(4.0f);
            if (pad)
                DrawGlyph(draw, Glyph::B, glyphX, centerY);
            else
                DrawKeyCap(draw, "Esc", glyphX, centerY);
            TextMiddle(draw, Face::Sans, 14.0f, glyphX + glyphWidth + S(6.0f), centerY, closeHovered ? TEXT : TEXT_2, "Close");
            right = closeMin.x - S(12.0f);

            const std::string chip = s_headerChip ? s_headerChip()
                : s_backendName.empty() ? std::format("{}" RUI_TIMES "{}", s_width, s_height)
                                        : std::format("{} " RUI_MIDDOT " {}" RUI_TIMES "{}", s_backendName, s_width, s_height);
            if (chip.empty())
                return;
            const float chipWidth = TextWidth(Face::Mono, 12.5f, chip) + S(16.0f);
            const ImVec2 chipMin(right - chipWidth, centerY - S(11.0f)), chipMax(right, centerY + S(11.0f));
            draw->AddRectFilled(chipMin, chipMax, INK_3, S(6.0f));
            Text(draw, Face::Mono, 12.5f, ImVec2(chipMin.x + S(8.0f), centerY - std::round(12.5f * Scale()) * 0.52f), TEXT_2, chip);
        }

        void Sidebar(ImDrawList* draw, ImVec2 min, ImVec2 max)
        {
            draw->AddRectFilled(min, max, INK_1);
            draw->AddRectFilled(ImVec2(max.x - Stroke(1.0f), min.y), max, LINE);
            float y = min.y + S(14.0f);
            const float left = min.x + S(10.0f), right = max.x - S(10.0f) - Stroke(1.0f);
            const std::string* group = nullptr;
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            for (int i = 0; i < PageCount(); i++)
            {
                const Page& page = s_pages[size_t(i)];
                if (group == nullptr || page.group != *group)
                {
                    group = &page.group;
                    y += S(8.0f);
                    Text(draw, Face::SemiBold, 12.0f, ImVec2(left + S(10.0f), y + S(1.0f)), TEXT_3, page.group);
                    y += S(22.0f);
                }
                const ImVec2 itemMin(left, y), itemMax(right, y + S(40.0f));
                ImGui::SetCursorScreenPos(itemMin);
                ImGui::PushID(i);
                if (ImGui::InvisibleButton("##page", ImVec2(itemMax.x - itemMin.x, itemMax.y - itemMin.y)))
                    SelectPage(i);
                const bool hovered = ImGui::IsItemHovered() && !NavCursorShown();
                ImGui::PopID();
                const bool current = i == s_page;
                if (current)
                    draw->AddRectFilled(itemMin, itemMax, Alpha(ACCENT, 33.0f / 255.0f), S(8.0f));
                else if (hovered)
                    draw->AddRectFilled(itemMin, itemMax, INK_3, S(8.0f));
                const float centerY = y + S(20.0f);
                Icon(draw, page.icon, 20.0f, ImVec2(left + S(10.0f + 10.0f), centerY), current ? ACCENT : TEXT_3);
                TextMiddle(draw, current ? Face::SemiBold : Face::Sans, 16.0f, left + S(41.0f), centerY, current || hovered ? TEXT : TEXT_2, page.name);
                y += S(40.0f + 2.0f);
            }
            ImGui::PopItemFlag();

            // How to switch pages, at the bottom (not for the mouse).
            const float centerY = max.y - S(14.0f + 10.0f);
            float x = left + S(8.0f);
            if (CurrentInput() == InputMode::Pad)
            {
                x += DrawGlyph(draw, Glyph::LB, x, centerY) + S(6.0f);
                x += DrawGlyph(draw, Glyph::RB, x, centerY) + S(6.0f);
            }
            else if (CurrentInput() == InputMode::Keys)
                x += DrawKeyCap(draw, "Tab", x, centerY) + S(6.0f);
            if (CurrentInput() != InputMode::Mouse)
                TextMiddle(draw, Face::Sans, 13.0f, x, centerY, TEXT_3, "Section");
        }

        // Fades what was drawn into `draw` since `first`.
        void Fade(ImDrawList* draw, int first, float alpha)
        {
            if (alpha >= 1.0f)
                return;
            for (int i = first; i < draw->VtxBuffer.Size; i++)
                draw->VtxBuffer[i].col = Alpha(draw->VtxBuffer[i].col, alpha);
        }

        void Panel()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float fade = std::min(s_openAge / FADE_SECONDS, 1.0f);

            // The scrim: darker towards the edges.
            {
                ImDrawList* background = ImGui::GetBackgroundDrawList();
                const ImVec2 min = viewport->Pos, max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
                const int first = background->VtxBuffer.Size;
                background->AddRectFilled(min, max, SCRIM);
                const float band = S(200.0f);
                const ImU32 edge = IM_COL32(7, 9, 12, 69), clear = IM_COL32(7, 9, 12, 0);
                background->AddRectFilledMultiColor(min, ImVec2(max.x, min.y + band), edge, edge, clear, clear);
                background->AddRectFilledMultiColor(ImVec2(min.x, max.y - band), max, clear, clear, edge, edge);
                background->AddRectFilledMultiColor(min, ImVec2(min.x + band, max.y), edge, clear, clear, edge);
                background->AddRectFilledMultiColor(ImVec2(max.x - band, min.y), max, clear, edge, edge, clear);
                Fade(background, first, fade);
            }

            const ImVec2 size(S(PANEL_WIDTH), S(PANEL_HEIGHT));
            const ImVec2 origin(std::round(viewport->Pos.x + (viewport->Size.x - size.x) * 0.5f), std::round(viewport->Pos.y + (viewport->Size.y - size.y) * 0.5f));
            ImGui::SetNextWindowPos(origin);
            ImGui::SetNextWindowSize(size);
            // The row is focused a frame after the panel opens: in a window
            // that just appeared, SetKeyboardFocusHere centres it.
            if (std::exchange(s_focusAfterOpen, false) && PageCount() != 0)
                s_focusRequest = s_pageRow[size_t(s_page)];
            if (std::exchange(s_justOpened, false))
            {
                s_focusRequest = -1;
                s_focusAfterOpen = true;
                // The cursor shows for the pad and the keyboard (focusing
                // a row doesn't show it by itself).
                if (CurrentInput() != InputMode::Mouse)
                    ImGui::SetNavCursorVisible(true);
            }
            if (!s_panelFocused)
                ImGui::SetNextWindowFocus();  // clicks outside the panel don't take the pad's cursor away
            const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
            ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);  // Tab switches pages
            if (ImGui::Begin("##overlay", nullptr, flags))
            {
                s_panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const int first = draw->VtxBuffer.Size;
                // The panel's shadow: three rings outside it.
                for (int ring = 0; ring < 3; ring++)
                {
                    const float out = S(2.0f + 4.0f * float(ring));
                    static constexpr int ALPHAS[] = { 31, 20, 10 };
                    ImGui::GetBackgroundDrawList()->AddRect(ImVec2(origin.x - out, origin.y - out), ImVec2(origin.x + size.x + out, origin.y + size.y + out),
                        Alpha(IM_COL32(0, 0, 0, ALPHAS[ring]), fade), S(PANEL_ROUNDING) + out, 0, S(4.0f));
                }

                const float headerBottom = origin.y + S(HEADER_HEIGHT);
                const float footerTop = origin.y + size.y - S(FOOTER_HEIGHT);
                const float sidebarRight = origin.x + S(SIDEBAR_WIDTH);
                // The strip: 56 high for two lines, taller for the page's
                // longest description (at most six lines).
                const float lineHeight = std::round(14.0f * Scale()) * 1.35f;
                const int stripLines = std::clamp(s_descriptionLines, 2, 6);
                const float descriptionTop = std::round(footerTop - (S(DESCRIPTION_HEIGHT) + float(stripLines - 2) * lineHeight));
                const float descriptionWidth = origin.x + size.x - S(22.0f) - (sidebarRight + S(22.0f + 18.0f + 12.0f));
                Header(draw, origin, ImVec2(origin.x + size.x, headerBottom));
                Sidebar(draw, ImVec2(origin.x, headerBottom), ImVec2(sidebarRight, footerTop));

                // The page's rows, scrolling.
                ImGui::SetCursorScreenPos(ImVec2(sidebarRight, headerBottom));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(22.0f), S(18.0f)));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8.0f), S(2.0f)));
                const ImVec2 contentSize(origin.x + size.x - sidebarRight, descriptionTop - headerBottom);
                if (std::exchange(s_scrollToTop, false))
                    ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
                int contentFirst = 0;
                ImDrawList* contentDraw = nullptr;
                if (ImGui::BeginChild("##content", contentSize, ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding))
                {
                    contentDraw = ImGui::GetWindowDrawList();
                    contentFirst = contentDraw->VtxBuffer.Size;
                    BeginRows(std::exchange(s_focusRequest, -1), descriptionWidth);
                    if (PageCount() != 0)
                    {
                        const Page& page = s_pages[size_t(s_page)];
                        const ImVec2 top = ImGui::GetCursorScreenPos();
                        const float width = ImGui::GetContentRegionAvail().x;
                        Text(contentDraw, Face::SemiBold, 22.0f, top, TEXT, page.name);
                        TextWrapped(contentDraw, Face::Sans, 14.0f, ImVec2(top.x, top.y + S(27.0f + 3.0f)), width, 1, TEXT_2, page.subtitle);
                        ImGui::Dummy(ImVec2(width, S(27.0f + 3.0f + 19.0f + 8.0f) - S(8.0f)));
                        if (page.draw)
                            page.draw();
                    }
                    s_descriptionLines = DescriptionLines();
                    // Remembered for the page; back at the first row, the
                    // title shows again.
                    const int focused = FocusedRow();
                    if (focused >= 0 && PageCount() != 0)
                    {
                        if (focused == 0 && s_lastFocused > 0 && NavCursorShown())
                            s_scrollToTop = true;
                        s_pageRow[size_t(s_page)] = focused;
                        s_lastFocused = focused;
                    }
                    if (s_testPress && focused >= 0)
                    {
                        s_testPress = false;
                        ImGui::GetIO().AddKeyEvent(ImGuiKey_GamepadFaceDown, true);
                        ImGui::GetIO().AddKeyEvent(ImGuiKey_GamepadFaceDown, false);
                    }
                    ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);

                DescriptionStrip(draw, ImVec2(sidebarRight, descriptionTop), ImVec2(origin.x + size.x, footerTop), stripLines);
                draw->AddRectFilled(ImVec2(origin.x, footerTop), ImVec2(origin.x + size.x, origin.y + size.y), INK_1, S(PANEL_ROUNDING) - 1.0f,
                    ImDrawFlags_RoundCornersBottom);
                draw->AddRectFilled(ImVec2(origin.x, footerTop), ImVec2(origin.x + size.x, footerTop + Stroke(1.0f)), LINE);
                Footer(draw, ImVec2(origin.x, footerTop), ImVec2(origin.x + size.x, origin.y + size.y));

                Fade(draw, first, fade);
                if (contentDraw != nullptr)
                    Fade(contentDraw, contentFirst, fade);
            }
            ImGui::End();
            ImGui::PopItemFlag();
        }

        void RunTestScript()
        {
            if (s_testPage < 0)
                return;
            const float before = s_testAge;
            s_testAge += ImGui::GetIO().DeltaTime;
            if (before == 0.0f)
            {
                s_pageRow[size_t(s_testPage)] = s_test.row;
                s_page = s_testPage;
                SetVisible(true);
            }
            if (s_test.pressAfter >= 0.0f && before < s_test.pressAfter && s_testAge >= s_test.pressAfter)
                s_testPress = true;
            if (s_test.closeAfter >= 0.0f && before < s_test.closeAfter && s_testAge >= s_test.closeAfter)
                SetVisible(false);
        }
    }

    void AddPage(Page page)
    {
        s_pages.push_back(std::move(page));
        s_pageRow.push_back(0);
    }

    bool Visible()
    {
        return s_visible;
    }

    void SetVisible(bool visible)
    {
        if (visible && !s_visible)
        {
            s_justOpened = true;
            s_openAge = 0.0f;
            s_panelFocused = false;
            s_lastFocused = -1;
        }
        s_visible = visible;
    }

    void ShowPage(std::string_view key)
    {
        SetVisible(true);
        if (const int page = FindPage(key); page >= 0)
            SelectPage(page);
    }

    void OnFrame(std::function<void()> hook)
    {
        s_frameHooks.push_back(std::move(hook));
    }

    void OnForeground(std::function<void(bool panelOpen)> hook)
    {
        s_foregroundHooks.push_back(std::move(hook));
    }

    int& OverlayScale()
    {
        return s_overlayScale;
    }

    void OverlaySizeRow()
    {
        static constexpr int SCALES[] = { 0, 90, 110, 125, 150 };
        static const std::string names[] = { "Auto (100%)", "90%", "110%", "125%", "150%" };
        int scale = int(std::size(SCALES)) - 1;
        for (int i = 0; i < int(std::size(SCALES)); i++)
        {
            if (SCALES[i] == s_overlayScale)
            {
                scale = i;
                break;
            }
        }
        Selector({ "Overlay size", "The size of this panel, the HUD and popups. Larger is easier to read from the couch." }, scale, names, 0);
        s_overlayScale = SCALES[scale];
    }

    void RegisterSettings(std::string_view section)
    {
        settings::Choice(section, "hud", stats::Mode(),
            { { stats::HudMode::Off, "off" }, { stats::HudMode::Fps, "fps" }, { stats::HudMode::Compact, "compact" }, { stats::HudMode::Full, "full" } });
        settings::Choice(section, "hud_corner", stats::Corner(),
            { { stats::HudCorner::TopLeft, "top_left" }, { stats::HudCorner::TopRight, "top_right" }, { stats::HudCorner::BottomLeft, "bottom_left" },
                { stats::HudCorner::BottomRight, "bottom_right" } });
        settings::Custom(
            section, "scale",
            [](std::string_view text) {
                if (text == "auto")
                {
                    s_overlayScale = 0;
                    return true;
                }
                // A factor (1.25) or a percentage (125).
                double scale = 0.0;
                if (!settings::ParseNumber(text, scale))
                    return false;
                const int percent = int(std::lround(scale < 10.0 ? scale * 100.0 : scale));
                if (percent == 100)
                    s_overlayScale = 0;
                for (const int allowed : { 90, 110, 125, 150 })
                {
                    if (percent == allowed)
                        s_overlayScale = percent;
                }
                return true;
            },
            [] { return s_overlayScale == 0 ? std::string("auto") : std::format("{:g}", s_overlayScale / 100.0); });
    }

    TestScript ParseTestScript(std::string_view spec)
    {
        TestScript script;
        std::vector<std::string> parts;
        const std::string text(spec);
        for (size_t start = 0;;)
        {
            const size_t colon = text.find(':', start);
            parts.push_back(text.substr(start, colon - start));
            if (colon == std::string::npos)
                break;
            start = colon + 1;
        }
        script.page = parts[0];
        if (parts.size() > 1)
            script.row = std::max(std::atoi(parts[1].c_str()), 0);
        if (parts.size() > 2 && parts[2].starts_with("press"))
            script.pressAfter = parts[2].size() > 6 ? float(std::atof(parts[2].c_str() + 6)) : 1.0f;
        return script;
    }

    bool SetTestScript(const TestScript& script)
    {
        const int page = FindPage(script.page);
        if (page < 0)
            return false;
        s_test = script;
        s_testPage = page;
        s_testAge = 0.0f;
        return true;
    }
}

namespace rui::detail
{
    void InitPanel(const InitInfo& info)
    {
        s_appName = info.appName;
        s_backendName = info.backendName;
        s_headerChip = info.headerChip;
        s_toggleKey = info.toggleKey;
        s_toggleButtons = info.toggleButtons;
        s_closeButtons = info.closeButtons;
        gate::SetToggleButtons(info.toggleButtons);
    }

    void DrawPanelFrame(uint32_t width, uint32_t height)
    {
        s_width = width;
        s_height = height;
        UpdateInputMode();
        UpdateScale(float(width), float(height), s_overlayScale);
        RunTestScript();

        // The key, or the pad's chord; while the panel is open, its close
        // buttons and Escape too.
        const bool typing = ImGui::GetIO().WantTextInput;
        if ((s_toggleKey != ImGuiKey_None && ImGui::IsKeyPressed(s_toggleKey, false)) || ChordPressed(s_toggleButtons))
            SetVisible(!s_visible);
        else if (s_visible && !typing && (AnyPressed(s_closeButtons) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
            SetVisible(false);

        if (s_visible)
        {
            s_openAge += ImGui::GetIO().DeltaTime;
            const bool shift = ImGui::GetIO().KeyShift;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || (!typing && shift && ImGui::IsKeyPressed(ImGuiKey_Tab, false)))
                SelectPage(s_page - 1);
            else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || (!typing && !shift && ImGui::IsKeyPressed(ImGuiKey_Tab, false)))
                SelectPage(s_page + 1);
            Panel();
        }
        DrawHud();
        DrawToasts(s_visible);
        for (const auto& hook : s_foregroundHooks)
            hook(s_visible);
    }

    void RunFrameHooks()
    {
        for (const auto& hook : s_frameHooks)
            hook();
    }
}
