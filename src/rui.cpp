#include "internal.h"

#include <rui/gate.h>
#include <rui/imgui_impl_plume.h>
#include <rui/keyboard.h>
#include <rui/settings.h>
#include <rui/style.h>
#include <rui/toasts.h>

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui_internal.h>  // ImGuiContext's windowing config

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <iterator>
#include <mutex>
#include <optional>
#include <utility>

namespace rui
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        std::mutex s_logMutex;
        std::function<void(LogKind, std::string_view)> s_log;

        bool s_active = false;
        bool s_renderer = false;
        bool s_platform = false;  // imgui_impl_sdl3, for the events
        std::function<void(PadState&)> s_readPad;
        Clock::time_point s_lastFrame;

        // QueueEvent's, until NewFrame takes them.
        constexpr size_t EVENT_LIMIT = 256;
        struct QueuedEvent
        {
            SDL_Event event;
            std::string text;  // a text input's characters: SDL's own string is gone by the time it's read
        };
        std::mutex s_eventMutex;
        std::deque<QueuedEvent> s_events;
        std::vector<QueuedEvent> s_taken;  // this frame's, their texts pointed into
        std::atomic<bool> s_wantTextInput{ false };

        // PostToast's, until NewFrame shows them.
        struct Posted
        {
            ToastKind kind;
            ImWchar icon;
            std::string title, body;
            float seconds;
        };
        std::mutex s_postedMutex;
        std::vector<Posted> s_posted;

        // Player 1's controller as ImGui's gamepad keys (the panel's
        // navigation; rui/gate.h keeps the game from seeing it while it's
        // open).
        void FeedPad(ImGuiIO& io)
        {
            PadState pad;
            if (s_readPad)
                s_readPad(pad);
            const bool connected = pad.connected;
            // Plugged in or out after the first frame: a toast.
            static std::optional<bool> s_connected;
            if (s_readPad && s_connected && *s_connected != connected)
                PushToast(connected ? ToastKind::Good : ToastKind::Warn, ICON_SPORTS_ESPORTS, connected ? "Controller connected" : "Controller disconnected",
                    "Player 1");
            s_connected = connected;
            static constexpr std::pair<ImGuiKey, uint16_t> BUTTONS[] = {
                { ImGuiKey_GamepadDpadUp, pad::DPAD_UP },
                { ImGuiKey_GamepadDpadDown, pad::DPAD_DOWN },
                { ImGuiKey_GamepadDpadLeft, pad::DPAD_LEFT },
                { ImGuiKey_GamepadDpadRight, pad::DPAD_RIGHT },
                { ImGuiKey_GamepadFaceDown, pad::A },
                { ImGuiKey_GamepadFaceRight, pad::B },
                { ImGuiKey_GamepadFaceLeft, pad::X },
                { ImGuiKey_GamepadFaceUp, pad::Y },
                { ImGuiKey_GamepadL1, pad::LEFT_SHOULDER },
                { ImGuiKey_GamepadR1, pad::RIGHT_SHOULDER },
                { ImGuiKey_GamepadL3, pad::LEFT_THUMB },
                { ImGuiKey_GamepadR3, pad::RIGHT_THUMB },
                { ImGuiKey_GamepadBack, pad::BACK },
                { ImGuiKey_GamepadStart, pad::START },
            };
            for (const auto& [key, button] : BUTTONS)
                io.AddKeyEvent(key, connected && (pad.buttons & button) != 0);

            // The left stick past the dead zone, 0 to 1 each way.
            const auto axis = [](int16_t value, bool negative) {
                const float v = (negative ? -float(value) : float(value)) - float(pad::LEFT_STICK_DEADZONE);
                return std::clamp(v / (32767.0f - float(pad::LEFT_STICK_DEADZONE)), 0.0f, 1.0f);
            };
            const auto stick = [&](ImGuiKey key, int16_t value, bool negative) {
                const float amount = connected ? axis(value, negative) : 0.0f;
                io.AddKeyAnalogEvent(key, amount > 0.1f, amount);
            };
            stick(ImGuiKey_GamepadLStickLeft, pad.leftX, true);
            stick(ImGuiKey_GamepadLStickRight, pad.leftX, false);
            stick(ImGuiKey_GamepadLStickUp, pad.leftY, false);
            stick(ImGuiKey_GamepadLStickDown, pad.leftY, true);
            // The triggers past XInput's threshold (the keyboard's pages).
            const auto trigger = [&](ImGuiKey key, uint8_t value) {
                io.AddKeyAnalogEvent(key, connected && value > pad::TRIGGER_THRESHOLD, connected ? float(value) / 255.0f : 0.0f);
            };
            trigger(ImGuiKey_GamepadL2, pad.leftTrigger);
            trigger(ImGuiKey_GamepadR2, pad.rightTrigger);
        }
    }

    void SetLog(std::function<void(LogKind kind, std::string_view text)> log)
    {
        std::lock_guard lock(s_logMutex);
        s_log = std::move(log);
    }

    bool Init(const InitInfo& info)
    {
        if (s_active)
            return true;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;  // the panel places itself; options go to the settings file
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
        // No switching windows by holding X (or Ctrl+Tab): the on-screen
        // keyboard keeps the focus over the panel, X being its space.
        GImGui->ConfigNavWindowingWithGamepad = false;
        GImGui->ConfigNavWindowingKeyNext = GImGui->ConfigNavWindowingKeyPrev = 0;
        detail::InitStyle();
        detail::InitPanel(info);
        s_readPad = info.readPad;
        // Events only: ImGui_ImplSDL3_NewFrame isn't called (it uses SDL off
        // the event thread); the pad comes from readPad.
        if (info.window != nullptr)
        {
            ImGui_ImplSDL3_InitForOther(info.window);
            ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
            s_platform = true;
        }
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        if (info.device != nullptr)
        {
            ImGui_ImplPlume_InitInfo plume;
            plume.device = info.device;
            plume.shaderFormat = info.shaderFormat;
            plume.renderTargetFormat = info.renderTargetFormat;
            plume.framesInFlight = info.framesInFlight;
            const auto compile = info.compileShader;
            plume.compileShader = [compile](std::string_view hlsl, ImGui_ImplPlume_ShaderStage stage, std::vector<uint8_t>& code) {
                return compile && compile(hlsl, stage == ImGui_ImplPlume_ShaderStage::Vertex ? ShaderStage::Vertex : ShaderStage::Pixel, code);
            };
            if (!ImGui_ImplPlume_Init(plume))
            {
                if (s_platform)
                    ImGui_ImplSDL3_Shutdown();
                s_platform = false;
                ImGui::DestroyContext();
                return false;
            }
            s_renderer = true;
        }
        else
        {
            // No renderer: the textures ImGui makes are never uploaded.
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        }
        s_lastFrame = Clock::now();
        s_active = true;
        return true;
    }

    void Shutdown()
    {
        if (!s_active)
            return;
        detail::CancelKeyboards();
        if (s_renderer)
            ImGui_ImplPlume_Shutdown();
        if (s_platform)
            ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        s_active = s_renderer = s_platform = false;
        gate::SetCapture(false);
    }

    bool Active()
    {
        return s_active;
    }

    void QueueEvent(const SDL_Event& event)
    {
        QueuedEvent queued{ event, {} };
        switch (event.type)
        {
        case SDL_EVENT_TEXT_INPUT:
            queued.text = event.text.text != nullptr ? event.text.text : "";
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            break;
        default:
            return;
        }
        std::lock_guard lock(s_eventMutex);
        if (s_events.size() >= EVENT_LIMIT)
            s_events.pop_front();
        s_events.push_back(std::move(queued));
    }

    bool WantsTextInput()
    {
        return s_wantTextInput;
    }

    void PostToast(ToastKind kind, ImWchar icon, std::string title, std::string body, float seconds)
    {
        std::lock_guard lock(s_postedMutex);
        s_posted.push_back({ kind, icon, std::move(title), std::move(body), seconds });
    }

    void NewFrame(uint32_t width, uint32_t height)
    {
        if (!s_active)
            return;
        ImGuiIO& io = ImGui::GetIO();
        s_wantTextInput = io.WantTextInput || keyboard::IsOpen();  // the last frame's (a physical keyboard types into it)
        {
            std::lock_guard lock(s_eventMutex);
            s_taken.assign(std::make_move_iterator(s_events.begin()), std::make_move_iterator(s_events.end()));
            s_events.clear();
        }
        for (QueuedEvent& queued : s_taken)
        {
            if (queued.event.type == SDL_EVENT_TEXT_INPUT)
                queued.event.text.text = queued.text.c_str();
            if (s_platform)
                ImGui_ImplSDL3_ProcessEvent(&queued.event);
        }
        FeedPad(io);
        {
            std::lock_guard lock(s_postedMutex);
            for (Posted& posted : s_posted)
                PushToast(posted.kind, posted.icon, std::move(posted.title), std::move(posted.body), posted.seconds);
            s_posted.clear();
        }

        const Clock::time_point now = Clock::now();
        io.DisplaySize = ImVec2(float(width), float(height));
        io.DeltaTime = std::max(std::chrono::duration<float>(now - s_lastFrame).count(), 1e-4f);
        s_lastFrame = now;

        std::lock_guard lock(settings::Mutex());
        ImGui::NewFrame();
        detail::RunFrameHooks();
        const std::string before = settings::Snapshot();
        const bool wasVisible = Visible();
        detail::DrawPanelFrame(width, height);
        ImGui::Render();
        if (settings::Snapshot() != before)
            detail::RunSettingsChanged();
        if (Visible() != wasVisible)
            detail::Log(LogKind::Event, Visible() ? "overlay open" : "overlay closed");
        gate::SetCapture(Visible() || keyboard::IsOpen());
        settings::Update();
    }

    void RecordUploads(plume::RenderCommandList* list)
    {
        if (s_renderer)
            ImGui_ImplPlume_UpdateTextures(ImGui::GetDrawData(), list);
    }

    void Draw(plume::RenderCommandList* list, const plume::RenderFramebuffer* framebuffer)
    {
        if (s_renderer)
            ImGui_ImplPlume_RenderDrawData(ImGui::GetDrawData(), list, framebuffer);
    }

    void DrawInto(plume::RenderCommandList* list, const plume::RenderFramebuffer* framebuffer, uint32_t width, uint32_t height)
    {
        ImDrawData* data = s_renderer ? ImGui::GetDrawData() : nullptr;
        if (data == nullptr || !data->Valid || data->DisplaySize.x <= 0.0f || data->DisplaySize.y <= 0.0f)
            return;
        // The window's coordinates onto the target's pixels.
        ImDrawData scaled = *data;
        scaled.FramebufferScale = ImVec2(float(width) / data->DisplaySize.x, float(height) / data->DisplaySize.y);
        ImGui_ImplPlume_RenderDrawData(&scaled, list, framebuffer);
    }
}

namespace rui::detail
{
    void Log(LogKind kind, std::string_view text)
    {
        std::lock_guard lock(s_logMutex);
        if (s_log)
            s_log(kind, text);
    }
}
