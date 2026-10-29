// The overlay's look: its colours, fonts and scale, and text and icon
// drawing at the 720p reference sizes.
//
// Every size the overlay's code writes is in pixels at 1280x720; S() scales
// it to the window (Scale), and the text functions take their font size the
// same way. On the drawing thread, between ImGui::NewFrame and Render.
#pragma once

#include <imgui.h>

#include <string_view>

namespace rui
{
    // Colours.
    constexpr ImU32 INK_1 = IM_COL32(12, 16, 22, 255);       // sidebar, footer, cards, HUD
    constexpr ImU32 INK_2 = IM_COL32(17, 22, 29, 255);       // the panel
    constexpr ImU32 INK_3 = IM_COL32(24, 32, 42, 255);       // hovered row
    constexpr ImU32 INK_4 = IM_COL32(29, 39, 53, 255);       // focused row
    constexpr ImU32 ACTIVE_ROW = IM_COL32(34, 50, 73, 255);  // pressed row, slider being dragged
    constexpr ImU32 LINE = IM_COL32(36, 45, 57, 255);        // rules, card borders
    constexpr ImU32 TEXT = IM_COL32(230, 235, 242, 255);
    constexpr ImU32 TEXT_2 = IM_COL32(154, 165, 180, 255);   // secondary text, units, hints
    constexpr ImU32 TEXT_3 = IM_COL32(92, 103, 118, 255);    // ImGuiCol_TextDisabled
    constexpr ImU32 ACCENT = IM_COL32(90, 180, 255, 255);    // focus ring, focused value, toggles on
    constexpr ImU32 ACCENT_INK = IM_COL32(4, 18, 31, 255);   // text on accent fills
    constexpr ImU32 ACCENT_HALO = IM_COL32(90, 180, 255, 41);
    constexpr ImU32 FILL_IDLE = IM_COL32(63, 127, 184, 255); // slider fill and bars when not focused; CPU
    constexpr ImU32 GPU_COLOR = IM_COL32(140, 124, 240, 255);
    constexpr ImU32 TRACK = IM_COL32(39, 49, 64, 255);
    constexpr ImU32 TOGGLE_OFF = IM_COL32(42, 52, 66, 255);
    constexpr ImU32 KNOB = IM_COL32(221, 229, 239, 255);
    constexpr ImU32 KNOB_OFF = IM_COL32(142, 153, 168, 255);
    constexpr ImU32 ARROW_LIMIT = IM_COL32(52, 62, 75, 255);
    constexpr ImU32 PIP = IM_COL32(46, 56, 70, 255);
    constexpr ImU32 KEYCAP = IM_COL32(30, 39, 51, 255);
    constexpr ImU32 KEYCAP_EDGE = IM_COL32(44, 55, 70, 255);
    constexpr ImU32 KEYCAP_SHADOW = IM_COL32(11, 15, 20, 255);
    constexpr ImU32 BUTTON = IM_COL32(34, 44, 57, 255);
    constexpr ImU32 BUTTON_HOVERED = IM_COL32(42, 54, 70, 255);
    constexpr ImU32 BUTTON_ACTIVE = IM_COL32(60, 143, 214, 255);
    constexpr ImU32 WARN = IM_COL32(242, 178, 76, 255);      // restart chip, slow frames
    constexpr ImU32 WARN_INK = IM_COL32(27, 18, 3, 255);
    constexpr ImU32 GOOD = IM_COL32(79, 207, 142, 255);
    constexpr ImU32 BAD = IM_COL32(255, 107, 107, 255);
    constexpr ImU32 SCRIM = IM_COL32(7, 9, 12, 140);
    constexpr ImU32 TOAST_BG = IM_COL32(22, 28, 37, 247);
    constexpr ImU32 GLYPH_BG = IM_COL32(35, 45, 58, 255);    // pad glyphs' circles and shoulder caps
    constexpr ImU32 PAD_A = IM_COL32(107, 214, 154, 255);
    constexpr ImU32 PAD_B = IM_COL32(255, 122, 122, 255);
    constexpr ImU32 PAD_X = IM_COL32(106, 176, 255, 255);
    constexpr ImU32 PAD_Y = IM_COL32(244, 195, 90, 255);

    // Material Symbols Rounded codepoints, the ones in the embedded subset
    // (fonts/README.md; a host's own icon font replaces it: RUI_ICON_FONT).
    constexpr ImWchar ICON_MONITOR = 0xEF5B;
    constexpr ImWchar ICON_VOLUME_UP = 0xE050;
    constexpr ImWchar ICON_FLAG = 0xF0C6;
    constexpr ImWchar ICON_SPORTS_ESPORTS = 0xEA28;
    constexpr ImWchar ICON_SPEED = 0xE9E4;
    constexpr ImWchar ICON_BUG_REPORT = 0xE868;
    constexpr ImWchar ICON_CHEVRON_LEFT = 0xE5CB;
    constexpr ImWchar ICON_CHEVRON_RIGHT = 0xE5CC;
    constexpr ImWchar ICON_RESTART_ALT = 0xF053;
    constexpr ImWchar ICON_INFO = 0xE88E;
    constexpr ImWchar ICON_CHECK_CIRCLE = 0xF0BE;
    constexpr ImWchar ICON_PHOTO_CAMERA = 0xE412;
    constexpr ImWchar ICON_CAMERA = 0xE3AF;
    constexpr ImWchar ICON_CONTENT_COPY = 0xE14D;
    constexpr ImWchar ICON_FOLDER_OPEN = 0xE2C8;
    constexpr ImWchar ICON_KEYBOARD = 0xE312;
    constexpr ImWchar ICON_WARNING = 0xF083;
    constexpr ImWchar ICON_CLOSE = 0xE5CD;
    constexpr ImWchar ICON_EMOJI_EVENTS = 0xEA23;
    constexpr ImWchar ICON_LOCK = 0xE899;
    constexpr ImWchar ICON_MOUSE = 0xE323;
    constexpr ImWchar ICON_LAN = 0xEB2F;

    enum class Face
    {
        Sans,      // IBM Plex Sans Regular: rows, sidebar, descriptions, hints
        SemiBold,  // IBM Plex Sans SemiBold: titles, group labels, chips
        Mono,      // IBM Plex Mono Medium: numbers that change every frame
    };

    // The scale from the 720p reference: the window's height (or width, if
    // narrower than 16:9) over 720, times the overlay size setting, so the
    // 860 x 560 panel always fits (rui::NewFrame sets it each frame).
    float Scale();
    // `pixels` at 720p, at the scale, rounded to whole pixels.
    float S(float pixels);
    // A line's width: at least one pixel.
    float Stroke(float pixels);

    ImFont* Font(Face face);
    // Pushes `face` at `size` (720p pixels) for the scope.
    struct FontScope
    {
        FontScope(Face face, float size);
        ~FontScope();
        FontScope(const FontScope&) = delete;
        FontScope& operator=(const FontScope&) = delete;
    };

    // `color` with its alpha times `alpha` (0-1).
    ImU32 Alpha(ImU32 color, float alpha);

    float TextWidth(Face face, float size, std::string_view text);
    // Text with its top left at `pos` (snapped to whole pixels).
    void Text(ImDrawList* draw, Face face, float size, ImVec2 pos, ImU32 color, std::string_view text);
    // Text whose capital letters centre on `centerY`.
    void TextMiddle(ImDrawList* draw, Face face, float size, float x, float centerY, ImU32 color, std::string_view text);
    // Text cut with an ellipsis before `maxX`.
    void TextEllipsis(ImDrawList* draw, Face face, float size, ImVec2 pos, float maxX, ImU32 color, std::string_view text);
    // Text wrapped at `width` (the first line at `firstWidth`, when set),
    // lines 1.35 apart, at most `lines` of them (an ellipsis ends the
    // last when there's more).
    void TextWrapped(ImDrawList* draw, Face face, float size, ImVec2 pos, float width, int lines, ImU32 color, std::string_view text,
        float firstWidth = -1.0f);
    // The lines TextWrapped would take for all of `text`.
    int WrappedLines(Face face, float size, float width, std::string_view text, float firstWidth = -1.0f);
    // A notice's life bar: `height` (720p) along the bottom of the box
    // `min`-`max` (rounded by `rounding`, its border `border` pixels wide),
    // inside the border and clipped by the round corners, `fraction` of
    // the width from the left.
    void LifeBar(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, float border, float height, float fraction, ImU32 color);
    // An icon `size` pixels (720p) high, its drawing centred on `center`.
    void Icon(ImDrawList* draw, ImWchar icon, float size, ImVec2 center, ImU32 color);
}
