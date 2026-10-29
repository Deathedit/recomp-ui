// recomp-ui: an in-game overlay for a recompiled game's host runtime (Dear
// ImGui, drawn through plume on D3D12 or Vulkan). The panel's pages, the
// settings file, the performance HUD, toasts and the input gate; the game
// adds its own pages and settings keys (README.md).
//
// Threads: settings load on any thread before Init (rui/settings.h). Init,
// NewFrame, RecordUploads, Draw and DrawInto run on the drawing thread (the
// one that records the frame the swap chain presents), and so does every
// page, hook and widget: the ImGui context is only used there. QueueEvent
// and WantsTextInput are for the thread that pumps SDL's events; PostToast,
// rui/gate.h and the settings' SavePending are safe from any thread.
#pragma once

#include <imgui.h>
#include <plume_render_interface.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

union SDL_Event;
struct SDL_Window;

namespace rui
{
    // ---- set-up ----

    // Pad buttons: XInput's bits (XINPUT_GAMEPAD_*), so a host on XInput or
    // an XInput-shaped shim passes wButtons as it is.
    namespace pad
    {
        constexpr uint16_t DPAD_UP = 0x0001, DPAD_DOWN = 0x0002, DPAD_LEFT = 0x0004, DPAD_RIGHT = 0x0008;
        constexpr uint16_t START = 0x0010, BACK = 0x0020, LEFT_THUMB = 0x0040, RIGHT_THUMB = 0x0080;
        constexpr uint16_t LEFT_SHOULDER = 0x0100, RIGHT_SHOULDER = 0x0200;
        constexpr uint16_t A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000;
        constexpr int16_t LEFT_STICK_DEADZONE = 7849;  // XInput's
        constexpr uint8_t TRIGGER_THRESHOLD = 30;      // XInput's
    }

    // A controller's state now (XINPUT_GAMEPAD's fields, and whether it's
    // connected).
    struct PadState
    {
        bool connected = false;
        uint16_t buttons = 0;
        uint8_t leftTrigger = 0, rightTrigger = 0;
        int16_t leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    };

    enum class ShaderStage
    {
        Vertex,
        Pixel,
    };

    struct InitInfo
    {
        // The window, for SDL's events (imgui_impl_sdl3, events only: SDL
        // isn't called from the drawing thread).
        SDL_Window* window = nullptr;
        // Null: no renderer. The UI is still built each frame (and Draw does
        // nothing), for tests and headless runs.
        plume::RenderDevice* device = nullptr;
        // DXIL (D3D12) or SPIR-V (Vulkan); the format of the targets the
        // overlay draws into (the swap chain's).
        plume::RenderShaderFormat shaderFormat = plume::RenderShaderFormat::DXIL;
        plume::RenderFormat renderTargetFormat = plume::RenderFormat::B8G8R8A8_UNORM;
        // The host waits for the frame recorded this many frames ago before
        // it records another (rui/imgui_impl_plume.h).
        uint32_t framesInFlight = 2;
        // Compiles the backend's HLSL (entry point main, shader model 6.0)
        // to shaderFormat; for SPIR-V, vertex shaders flip y (DXC's
        // -fvk-invert-y). From a precompiled cache, if the host has one
        // (rui/imgui_impl_plume_shaders.h has the sources).
        std::function<bool(std::string_view hlsl, ShaderStage stage, std::vector<uint8_t>& code)> compileShader;

        // The panel header's name, on the left (the project's, say).
        std::string appName;
        // The header's chip on the right, redrawn each frame; empty: none.
        // Unset: "<backendName> · <width>×<height>".
        std::function<std::string()> headerChip;
        std::string backendName;  // "D3D12", "Vulkan"

        // Player 1's controller, read once a frame for the panel's
        // navigation (and a toast when it's plugged in or out). Unset: no
        // pad.
        std::function<void(PadState&)> readPad;

        // Opens and closes the panel: the key, or the pad buttons all held
        // (one of them pressed this frame, the others down).
        ImGuiKey toggleKey = ImGuiKey_F1;
        uint16_t toggleButtons = pad::BACK | pad::START;
        // Close it too (and Escape).
        uint16_t closeButtons = pad::B | pad::START;
    };

    // How the overlay's messages reach the host's log: Event marks the ones
    // a timeline wants (the panel opening and closing).
    enum class LogKind
    {
        Info,
        Event,
    };
    // Any time (before LoadSettings, to see its messages). From whichever
    // thread logs.
    void SetLog(std::function<void(LogKind kind, std::string_view text)> log);

    // On the drawing thread, once the device is up: the ImGui context, the
    // fonts and style, and the plume backend. False (and the frame functions
    // do nothing) when the backend couldn't start.
    bool Init(const InitInfo& info);
    // After the GPU is done with the overlay's frames.
    void Shutdown();
    bool Active();

    // ---- a frame ----

    // In this order, for each presented frame: NewFrame builds the overlay
    // (input, the toggle, pages, HUD, toasts) for a window of `width` x
    // `height`; RecordUploads goes before the swap image's render pass (its
    // copies would end one); Draw inside it, after the game's frame, last
    // before present.
    void NewFrame(uint32_t width, uint32_t height);
    void RecordUploads(plume::RenderCommandList* list);
    void Draw(plume::RenderCommandList* list, const plume::RenderFramebuffer* framebuffer);
    // The same frame of the overlay into another target, `width` x
    // `height` (the window's aspect). Screenshots normally leave it out:
    // capture the game's frame before Draw.
    void DrawInto(plume::RenderCommandList* list, const plume::RenderFramebuffer* framebuffer, uint32_t width, uint32_t height);

    // ---- input ----

    // From the thread that pumps SDL's events: each event, as it's polled.
    // The ones ImGui uses (keys, text, the mouse, focus) are kept, text
    // copied, until the next NewFrame (256 at most; the oldest go).
    void QueueEvent(const SDL_Event& event);
    // A text field is being typed in (as of the last frame): the event
    // thread starts SDL's text input (SDL_StartTextInput) while it's true.
    bool WantsTextInput();

    // ---- the panel ----

    // A page in the sidebar, under its group's heading, in the order added.
    // `draw` lays out its rows (rui/widgets.h) between ImGui::NewFrame and
    // Render; it runs only while the page is shown.
    struct Page
    {
        std::string key;  // for ShowPage and test scripts: "video"
        std::string name;
        std::string group;  // the sidebar's heading: "Settings"
        ImWchar icon = 0;   // rui/style.h's ICON_*
        std::string subtitle;
        std::function<void()> draw;
    };
    void AddPage(Page page);

    bool Visible();
    void SetVisible(bool visible);
    // Opens the panel on the page with `key`.
    void ShowPage(std::string_view key);

    // Each frame, inside the ImGui frame, before the panel (and before the
    // settings are compared: what a hook changes isn't a change made in the
    // panel; rui::settings::Changed saves it).
    void OnFrame(std::function<void()> hook);
    // Each frame after the HUD and toasts: notices of the game's own
    // (ImGui::GetForegroundDrawList), `panelOpen` to keep clear of it.
    void OnForeground(std::function<void(bool panelOpen)> hook);

    // The overlay's size, percent of the size fitted to the window: 0
    // (auto: 100), 90, 110, 125 or 150. rui::RegisterSettings binds it.
    int& OverlayScale();
    // The overlay size row (rui/widgets.h's Selector), for a page of the game's.
    void OverlaySizeRow();
    // [section] hud, hud_corner (rui/stats.h) and scale (OverlayScale) in
    // the settings file, at this point of its order.
    void RegisterSettings(std::string_view section);

    // ---- toasts ----

    enum class ToastKind
    {
        Info,  // the accent
        Good,  // green: done, saved, connected
        Warn,  // amber: restart required, disconnected
    };
    // From any thread, shown from the next frame (kept until there is one).
    // On the drawing thread rui::PushToast (rui/toasts.h) shows it at once.
    void PostToast(ToastKind kind, ImWchar icon, std::string title, std::string body, float seconds = 4.0f);

    // ---- testing ----

    // Opens the panel at the first frame on a page and row, presses the row
    // (A) `pressAfter` seconds later and closes it again `closeAfter`
    // seconds after the first frame (-1: never): for screenshots of the
    // panel. From a host's environment variable, say.
    struct TestScript
    {
        std::string page;
        int row = 0;
        float pressAfter = -1.0f;
        float closeAfter = -1.0f;
    };
    // "<page>[:<row>[:press[=<seconds>]]]" (press alone: a second).
    TestScript ParseTestScript(std::string_view spec);
    // False when no page has its key.
    bool SetTestScript(const TestScript& script);
}
