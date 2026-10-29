// What the framework's sources share and hosts don't see.
#pragma once

#include <rui/rui.h>
#include <rui/stats.h>

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace rui::fonts
{
    // Embedded at build time (CMakeLists.txt, cmake/embed_files.cmake;
    // fonts/README.md has their sources and licences).
    extern const unsigned char SANS_REGULAR[];
    extern const size_t SANS_REGULAR_SIZE;
    extern const unsigned char SANS_SEMIBOLD[];
    extern const size_t SANS_SEMIBOLD_SIZE;
    extern const unsigned char MONO_MEDIUM[];
    extern const size_t MONO_MEDIUM_SIZE;
    extern const unsigned char ICONS[];
    extern const size_t ICONS_SIZE;
}

namespace rui::detail
{
    void Log(LogKind kind, std::string_view text);

    // settings.cpp: the panel changed something (OnChanged hooks, then a
    // save half a second later).
    void RunSettingsChanged();

    // style.cpp: the embedded fonts and the ImGuiStyle at scale 1 (after
    // ImGui::CreateContext), and the scale for a window of `width` x
    // `height` at the overlay size `userPercent` (0: auto). The ImGuiStyle
    // is rebuilt when it changes.
    void InitStyle();
    void UpdateScale(float width, float height, int userPercent);

    // widgets.cpp: the last input used, once a frame before the panel.
    void UpdateInputMode();

    // panel.cpp, on the drawing thread inside the ImGui frame.
    void InitPanel(const InitInfo& info);
    // The toggle, the panel (when open), HUD, toasts and foreground hooks.
    void DrawPanelFrame(uint32_t width, uint32_t height);
    // The OnFrame hooks.
    void RunFrameHooks();

    // toasts.cpp.
    void DrawToasts(bool panelOpen);
    struct ToastStatus
    {
        ImWchar icon;
        ImU32 color;
        std::string title;
    };
    std::optional<ToastStatus> NewestToast();

    // stats.cpp.
    void DrawHud();
}
