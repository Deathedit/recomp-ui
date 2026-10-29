#include "internal.h"

#include <rui/toasts.h>

#include <algorithm>
#include <cmath>
#include <deque>

namespace rui
{
    namespace
    {
        constexpr size_t MAX_TOASTS = 3;
        constexpr float SLIDE_SECONDS = 0.2f;

        struct Toast
        {
            ToastKind kind;
            ImWchar icon;
            std::string title, body;
            float seconds;
            float age = 0.0f;
        };
        std::deque<Toast> s_toasts;

        ImU32 KindColor(ToastKind kind)
        {
            return kind == ToastKind::Good ? GOOD : kind == ToastKind::Warn ? WARN : ACCENT;
        }
    }

    void PushToast(ToastKind kind, ImWchar icon, std::string title, std::string body, float seconds)
    {
        detail::Log(LogKind::Info, std::format("toast: {}: {}", title, body));
        s_toasts.push_back({ kind, icon, std::move(title), std::move(body), seconds });
        while (s_toasts.size() > MAX_TOASTS)
            s_toasts.pop_front();
    }

}

namespace rui::detail
{
    void DrawToasts(bool overlayOpen)
    {
        const float delta = ImGui::GetIO().DeltaTime;
        for (Toast& toast : s_toasts)
            toast.age += delta;
        std::erase_if(s_toasts, [](const Toast& toast) { return toast.age >= toast.seconds; });
        if (overlayOpen || s_toasts.empty())
            return;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        const float width = S(320.0f), margin = S(16.0f), gap = S(8.0f);
        const float bodyWidth = width - S(12.0f + 22.0f + 10.0f + 14.0f);
        const float lineHeight = std::round(13.5f * Scale()) * 1.35f;
        float bottom = viewport->Pos.y + viewport->Size.y - margin;
        // Newest at the bottom, the older ones above it.
        for (auto it = s_toasts.rbegin(); it != s_toasts.rend(); ++it)
        {
            const Toast& toast = *it;
            const int lines = toast.body.empty() ? 0 : TextWidth(Face::Sans, 13.5f, toast.body) > bodyWidth ? 2 : 1;
            const float height = S(10.0f + 20.0f + 2.0f) + float(lines) * lineHeight + S(12.0f);
            // Slides in from the right, fades out sliding back.
            const float in = std::min(toast.age / SLIDE_SECONDS, 1.0f), out = std::min((toast.seconds - toast.age) / SLIDE_SECONDS, 1.0f);
            const float alpha = std::min(in, out);
            const float eased = 1.0f - (1.0f - alpha) * (1.0f - alpha);
            const float offset = (1.0f - eased) * S(24.0f);
            const ImVec2 min(viewport->Pos.x + viewport->Size.x - margin - width + offset, bottom - height);
            const ImVec2 max(min.x + width, bottom);
            const ImU32 color = KindColor(toast.kind);
            const float rounding = S(10.0f);
            draw->AddRectFilled(min, max, Alpha(TOAST_BG, alpha), rounding);
            draw->AddRect(min, max, Alpha(IM_COL32(255, 255, 255, 20), alpha), rounding, 0, Stroke(1.0f));
            Icon(draw, toast.icon, 22.0f, ImVec2(min.x + S(12.0f + 11.0f), min.y + S(10.0f + 12.0f)), Alpha(color, alpha));
            const float textX = min.x + S(12.0f + 22.0f + 10.0f);
            TextEllipsis(draw, Face::SemiBold, 15.0f, ImVec2(textX, min.y + S(10.0f)), max.x - S(14.0f), Alpha(TEXT, alpha), toast.title);
            if (lines != 0)
                TextWrapped(draw, Face::Sans, 13.5f, ImVec2(textX, min.y + S(10.0f + 20.0f + 2.0f)), bodyWidth, 2, Alpha(TEXT_2, alpha), toast.body);
            // Its life, shrinking.
            const float life = std::clamp(1.0f - toast.age / toast.seconds, 0.0f, 1.0f);
            LifeBar(draw, min, max, rounding, Stroke(1.0f), 2.0f, life, Alpha(color, 0.6f * alpha));
            bottom = min.y - gap;
        }
    }

    std::optional<ToastStatus> NewestToast()
    {
        if (s_toasts.empty())
            return std::nullopt;
        const Toast& toast = s_toasts.back();
        return ToastStatus{ toast.icon, KindColor(toast.kind), toast.title };
    }
}
