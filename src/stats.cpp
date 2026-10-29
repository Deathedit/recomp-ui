#include "internal.h"

#include <rui/stats.h>
#include <rui/style.h>
#include <rui/widgets.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include <imgui_internal.h>  // ImDrawListSharedData::TexUvWhitePixel

namespace rui::stats
{
    namespace
    {
        HudMode s_mode = HudMode::Off;
        HudCorner s_corner = HudCorner::TopRight;
        float s_target = 60.0f;
        std::string s_countLabel, s_countTile;

        // The last frames, oldest first from s_next: 240 for the graphs, 2,000
        // for the lows.
        constexpr int GRAPH_FRAMES = 240, LOW_FRAMES = 2000;
        float s_frames[LOW_FRAMES] = {};
        int s_next = 0, s_count = 0;
        int s_lowCount = 0;  // of those, the frames since ResetLows

        // Averaged over half a second.
        float s_fps = 0.0f, s_frameMs = 0.0f, s_cpuMs = 0.0f, s_gpuMs = -1.0f, s_countAverage = 0.0f;
        float s_low1 = 0.0f, s_low01 = 0.0f;
        float s_time = 0.0f, s_cpuSum = 0.0f, s_gpuSum = 0.0f;
        double s_countSum = 0.0;
        int s_timeFrames = 0, s_cpuFrames = 0, s_gpuFrames = 0;

        float Frame(int age)  // 0: the newest
        {
            return s_frames[(s_next - 1 - age + LOW_FRAMES) % LOW_FRAMES];
        }

        // The frame rates of the slowest 1% and 0.1% of the frames since
        // ResetLows (at least one frame each).
        void UpdateLows()
        {
            if (s_lowCount == 0)
                return;
            std::vector<float> slowest(static_cast<size_t>(s_lowCount));
            for (int i = 0; i < s_lowCount; i++)
                slowest[size_t(i)] = Frame(i);
            const size_t slow1 = std::max<size_t>(1, size_t(std::lround(double(s_lowCount) * 0.01)));
            const size_t slow01 = std::max<size_t>(1, size_t(std::lround(double(s_lowCount) * 0.001)));
            std::partial_sort(slowest.begin(), slowest.begin() + ptrdiff_t(slow1), slowest.end(), std::greater<float>());
            const auto rate = [&](size_t count) {
                double sum = 0.0;
                for (size_t i = 0; i < count; i++)
                    sum += slowest[i];
                return sum > 0.0 ? float(1000.0 * double(count) / sum) : 0.0f;
            };
            s_low1 = rate(slow1);
            s_low01 = rate(slow01);
        }

        ImU32 FpsColor(float fps, float target)
        {
            return fps < target * 0.5f ? BAD : fps < target * 0.9f ? WARN : TEXT;
        }

        // A filled quad with a colour per corner (the graphs' area).
        void GradientQuad(ImDrawList* draw, ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 ca, ImU32 cb, ImU32 cc, ImU32 cd)
        {
            const ImVec2 uv = draw->_Data->TexUvWhitePixel;
            draw->PrimReserve(6, 4);
            const ImDrawIdx index = ImDrawIdx(draw->_VtxCurrentIdx);
            draw->PrimWriteIdx(index);
            draw->PrimWriteIdx(ImDrawIdx(index + 1));
            draw->PrimWriteIdx(ImDrawIdx(index + 2));
            draw->PrimWriteIdx(index);
            draw->PrimWriteIdx(ImDrawIdx(index + 2));
            draw->PrimWriteIdx(ImDrawIdx(index + 3));
            draw->PrimWriteVtx(a, uv, ca);
            draw->PrimWriteVtx(b, uv, cb);
            draw->PrimWriteVtx(c, uv, cc);
            draw->PrimWriteVtx(d, uv, cd);
        }

        // The last `slots` frame times, newest on the right, under `top`
        // milliseconds: dashed lines at 16.7 and 33.3 ms, the area under the
        // line fading down, frames over `spike` ms marked, the average, a dot
        // on the newest.
        void Plot(ImDrawList* draw, ImVec2 min, ImVec2 max, int slots, float top, float spike, bool average)
        {
            const float height = max.y - min.y, inset = S(1.0f);
            const auto y = [&](float ms) { return max.y - inset - std::min(ms, top) / top * (height - 2.0f * inset); };
            const float dash = S(4.0f);
            for (const float line : { 1000.0f / 60.0f, 1000.0f / 30.0f })
            {
                if (line >= top)
                    continue;
                const float lineY = std::round(y(line));
                for (float x = min.x; x < max.x; x += dash * 2.0f)
                    draw->AddRectFilled(ImVec2(x, lineY), ImVec2(std::min(x + dash, max.x), lineY + Stroke(1.0f)), IM_COL32(42, 52, 66, 255));
            }
            const int count = std::min(s_count, slots);
            if (count < 2)
                return;
            const float dx = (max.x - min.x) / float(slots - 1);
            std::vector<ImVec2> points(static_cast<size_t>(count));
            float sum = 0.0f;
            for (int i = 0; i < count; i++)
            {
                const float ms = Frame(count - 1 - i);
                sum += ms;
                points[size_t(i)] = ImVec2(max.x - float(count - 1 - i) * dx, y(ms));
            }
            const auto areaColor = [&](float atY) { return Alpha(ACCENT, 0.28f * std::clamp((max.y - atY) / height, 0.0f, 1.0f)); };
            const ImU32 bottom = Alpha(ACCENT, 0.0f);
            for (int i = 0; i + 1 < count; i++)
            {
                const ImVec2 a = points[size_t(i)], b = points[size_t(i + 1)];
                GradientQuad(draw, a, b, ImVec2(b.x, max.y), ImVec2(a.x, max.y), areaColor(a.y), areaColor(b.y), bottom, bottom);
            }
            draw->AddPolyline(points.data(), count, ACCENT, ImDrawFlags_None, std::max(1.0f, 1.5f * Scale()));
            const float mark = S(1.5f);
            for (int i = 0; i < count; i++)
            {
                if (Frame(count - 1 - i) > spike)
                    draw->AddRectFilled(ImVec2(points[size_t(i)].x - mark, points[size_t(i)].y - mark),
                        ImVec2(points[size_t(i)].x + mark, points[size_t(i)].y + mark), WARN);
            }
            if (average)
            {
                const float averageY = std::round(y(sum / float(count)));
                draw->AddRectFilled(ImVec2(min.x, averageY), ImVec2(max.x, averageY + Stroke(1.0f)), TEXT_2);
            }
            draw->AddCircleFilled(points.back(), S(2.0f), TEXT);
        }

        // Text whose baseline is at `baseline` (the numbers and their units).
        void TextOnBaseline(ImDrawList* draw, Face face, float size, float x, float baseline, ImU32 color, std::string_view text)
        {
            // Plex's ascent: 1025 of its 1300 units of height.
            Text(draw, face, size, ImVec2(x, baseline - std::round(size * Scale()) * 1025.0f / 1300.0f), color, text);
        }

        std::string Milliseconds(float ms)
        {
            return ms < 0.0f ? std::string("-") : std::format("{:.1f} ms", ms);
        }
    }

    const char* HudModeName(HudMode mode)
    {
        switch (mode)
        {
        case HudMode::Fps: return "fps";
        case HudMode::Compact: return "compact";
        case HudMode::Full: return "full";
        default: return "off";
        }
    }

    const char* HudCornerName(HudCorner corner)
    {
        switch (corner)
        {
        case HudCorner::TopLeft: return "top_left";
        case HudCorner::BottomLeft: return "bottom_left";
        case HudCorner::BottomRight: return "bottom_right";
        default: return "top_right";
        }
    }

    HudMode& Mode()
    {
        return s_mode;
    }

    HudCorner& Corner()
    {
        return s_corner;
    }

    void SetCounter(std::string hudLabel, std::string tileLabel)
    {
        s_countLabel = std::move(hudLabel);
        s_countTile = std::move(tileLabel);
    }

    void SetTargetFps(float fps)
    {
        s_target = fps;
    }

    void AddFrame(float frameMilliseconds, float cpuMilliseconds, float gpuMilliseconds, float count)
    {
        if (!Active())
            return;
        s_frames[s_next] = frameMilliseconds;
        s_next = (s_next + 1) % LOW_FRAMES;
        s_count = std::min(s_count + 1, LOW_FRAMES);
        s_lowCount = std::min(s_lowCount + 1, LOW_FRAMES);

        s_time += frameMilliseconds;
        s_timeFrames++;
        s_cpuSum += cpuMilliseconds;
        s_cpuFrames++;
        s_countSum += count;
        if (gpuMilliseconds >= 0.0f)
        {
            s_gpuSum += gpuMilliseconds;
            s_gpuFrames++;
        }
        if (s_time >= 500.0f)
        {
            s_fps = float(s_timeFrames) * 1000.0f / s_time;
            s_frameMs = s_time / float(s_timeFrames);
            s_cpuMs = s_cpuSum / float(std::max(s_cpuFrames, 1));
            s_gpuMs = s_gpuFrames != 0 ? s_gpuSum / float(s_gpuFrames) : -1.0f;
            s_countAverage = float(s_countSum / double(s_timeFrames));
            s_countSum = 0.0;
            s_time = s_cpuSum = s_gpuSum = 0.0f;
            s_timeFrames = s_cpuFrames = s_gpuFrames = 0;
            UpdateLows();
        }
    }

    void ResetLows()
    {
        s_lowCount = 0;
        s_low1 = s_low01 = 0.0f;
    }

    void HudRows()
    {
        {
            static const std::string names[] = { "Off", "FPS", "Compact", "Full" };
            int mode = int(s_mode);
            Selector({ "Performance HUD", "Frame rate in a screen corner. Compact adds a frame time graph; Full adds CPU, GPU and lows." },
                mode, names, int(HudMode::Off));
            s_mode = HudMode(mode);
        }
        {
            static const std::string names[] = { "Top left", "Top right", "Bottom left", "Bottom right" };
            int corner = int(s_corner);
            RowSpec spec{ "HUD corner", "Which screen corner the HUD sits in." };
            if (s_mode == HudMode::Off)
                spec.disabledWhy = "The HUD is off.";
            Selector(spec, corner, names, int(HudCorner::TopRight));
            s_corner = HudCorner(corner);
        }
        if (Button({ "Statistics", "Clears the 1% and 0.1% lows, after a loading screen for example." }, ICON_RESTART_ALT, "Reset lows"))
            ResetLows();
    }
}

namespace rui::detail
{
    using namespace rui::stats;

    void DrawHud()
    {
        const HudMode mode = s_mode;
        const HudCorner corner = s_corner;
        const float targetFps = s_target;
        if (mode == HudMode::Off)
            return;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        const std::string fps = std::format("{:.0f}", s_fps);
        const ImU32 fpsColor = FpsColor(s_fps, targetFps);

        // The panel's size, then its corner.
        ImVec2 size;
        if (mode == HudMode::Fps)
            size = ImVec2(S(10.0f) * 2.0f + TextWidth(Face::Mono, 17.0f, fps) + S(4.0f) + TextWidth(Face::Sans, 12.0f, "fps"), S(4.0f) * 2.0f + S(20.0f));
        else
            size = ImVec2(S(180.0f), S(8.0f + 26.0f + 6.0f + 28.0f + 9.0f) +
                    (mode == HudMode::Full ? S(6.0f + (s_countLabel.empty() ? 3.0f : 4.0f) * 19.0f) : 0.0f));
        const float margin = S(16.0f);
        const bool left = corner == HudCorner::TopLeft || corner == HudCorner::BottomLeft;
        const bool top = corner == HudCorner::TopLeft || corner == HudCorner::TopRight;
        const ImVec2 min(left ? viewport->Pos.x + margin : viewport->Pos.x + viewport->Size.x - margin - size.x,
            top ? viewport->Pos.y + margin : viewport->Pos.y + viewport->Size.y - margin - size.y);
        const ImVec2 max(min.x + size.x, min.y + size.y);
        const float rounding = S(mode == HudMode::Fps ? 8.0f : 10.0f);
        draw->AddRectFilled(min, max, Alpha(INK_1, 0.74f), rounding);
        draw->AddRect(min, max, IM_COL32(255, 255, 255, 15), rounding, 0, Stroke(1.0f));

        if (mode == HudMode::Fps)
        {
            const float baseline = min.y + S(4.0f) + S(16.0f);
            const float x = min.x + S(10.0f);
            TextOnBaseline(draw, Face::Mono, 17.0f, x, baseline, fpsColor, fps);
            TextOnBaseline(draw, Face::Sans, 12.0f, x + TextWidth(Face::Mono, 17.0f, fps) + S(4.0f), baseline, TEXT_2, "fps");
            return;
        }
        const float x = min.x + S(10.0f), right = max.x - S(10.0f);
        float y = min.y + S(8.0f);
        const float baseline = y + S(21.0f);
        TextOnBaseline(draw, Face::Mono, 26.0f, x, baseline, fpsColor, fps);
        TextOnBaseline(draw, Face::Sans, 12.0f, x + TextWidth(Face::Mono, 26.0f, fps) + S(4.0f), baseline, TEXT_2, "fps");
        const std::string ms = Milliseconds(s_frameMs);
        TextOnBaseline(draw, Face::Mono, 13.0f, right - TextWidth(Face::Mono, 13.0f, ms), baseline, TEXT_2, ms);
        y += S(26.0f + 6.0f);
        const float frameTarget = 1000.0f / std::max(targetFps, 1.0f);
        Plot(draw, ImVec2(x, y), ImVec2(x + S(158.0f), y + S(28.0f)), 120, std::max(50.0f, frameTarget * 2.2f), frameTarget * 1.5f, false);
        y += S(28.0f);
        if (mode != HudMode::Full)
            return;
        y += S(6.0f);
        std::vector<std::pair<std::string, std::string>> lines = {
            { "CPU", Milliseconds(s_cpuMs) },
            { "GPU", Milliseconds(s_gpuMs) },
            { "1% low", std::format("{:.0f} fps", s_low1) },
        };
        if (!s_countLabel.empty())
            lines.emplace_back(s_countLabel, std::format("{:.0f}", s_countAverage));
        for (const auto& [label, value] : lines)
        {
            const float lineBaseline = y + S(14.0f);
            TextOnBaseline(draw, Face::Mono, 12.5f, x, lineBaseline, TEXT_2, label);
            TextOnBaseline(draw, Face::Mono, 12.5f, right - TextWidth(Face::Mono, 12.5f, value), lineBaseline, TEXT, value);
            y += S(19.0f);
        }
    }

}

namespace rui::stats
{
    void Cards()
    {
        const float targetFps = s_target;
        // Four tiles.
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        ImGui::Dummy(ImVec2(0.0f, std::max(S(8.0f) - spacing, 0.0f)));
        {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x, gap = S(8.0f);
            const int tileCount = s_countTile.empty() ? 3 : 4;
            const float tileWidth = (width - float(tileCount - 1) * gap) / float(tileCount), tileHeight = S(66.0f);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            struct Tile
            {
                const char* label;
                std::string value;
                const char* unit;
                ImU32 color;
            };
            const Tile tiles[] = {
                { "Average", std::format("{:.1f}", s_fps), "fps", FpsColor(s_fps, targetFps) },
                { "1% low", s_lowCount != 0 ? std::format("{:.1f}", s_low1) : std::string("-"), "fps", FpsColor(s_low1, targetFps) },
                { "0.1% low", s_lowCount != 0 ? std::format("{:.1f}", s_low01) : std::string("-"), "fps", FpsColor(s_low01, targetFps) },
                { s_countTile.c_str(), std::format("{:.0f}", s_countAverage), "", TEXT },
            };
            for (int i = 0; i < tileCount; i++)
            {
                const ImVec2 min(std::round(pos.x + float(i) * (tileWidth + gap)), pos.y);
                const ImVec2 max(std::round(min.x + tileWidth), pos.y + tileHeight);
                draw->AddRectFilled(min, max, INK_1, S(10.0f));
                draw->AddRect(min, max, LINE, S(10.0f), 0, Stroke(1.0f));
                Text(draw, Face::Sans, 12.5f, ImVec2(min.x + S(12.0f), min.y + S(9.0f)), TEXT_2, tiles[i].label);
                const float baseline = min.y + S(9.0f + 19.0f + 23.0f);
                TextOnBaseline(draw, Face::Mono, 24.0f, min.x + S(12.0f), baseline, tiles[i].color, tiles[i].value);
                if (*tiles[i].unit != 0)
                    TextOnBaseline(draw, Face::Sans, 13.0f, min.x + S(12.0f) + TextWidth(Face::Mono, 24.0f, tiles[i].value) + S(3.0f), baseline, TEXT_2,
                        tiles[i].unit);
            }
            ImGui::Dummy(ImVec2(width, tileHeight));
        }

        // The frame time graph.
        float high = 0.0f, sum = 0.0f;
        const int shown = std::min(s_count, GRAPH_FRAMES);
        for (int i = 0; i < shown; i++)
        {
            high = std::max(high, Frame(i));
            sum += Frame(i);
        }
        const float frameTarget = 1000.0f / std::max(targetFps, 1.0f);
        const std::string summary = shown != 0 ? std::format("avg {:.1f} ms \xC2\xB7 max {:.1f} ms", sum / float(shown), high) : std::string();
        Card("Frame time, last 240 frames", summary, TEXT_2, [&](ImDrawList* draw, ImVec2 pos, float width) {
            const float graphHeight = S(120.0f);
            Plot(draw, pos, ImVec2(pos.x + width, pos.y + graphHeight), GRAPH_FRAMES, std::max(40.0f, frameTarget * 2.2f), frameTarget * 1.5f, true);
            // The legend.
            float x = pos.x;
            const float centerY = pos.y + graphHeight + S(4.0f + 10.0f);
            const std::pair<ImU32, const char*> keys[] = { { ACCENT, "frame" }, { TEXT_2, "average" }, { IM_COL32(58, 70, 86, 255), "16.7 / 33.3 ms" } };
            for (const auto& [color, label] : keys)
            {
                draw->AddRectFilled(ImVec2(x, std::round(centerY - S(1.5f))), ImVec2(x + S(10.0f), std::round(centerY - S(1.5f)) + S(3.0f)), color, S(2.0f));
                x += S(15.0f);
                TextMiddle(draw, Face::Sans, 12.5f, x, centerY, TEXT_2, label);
                x += TextWidth(Face::Sans, 12.5f, label) + S(14.0f);
            }
            return graphHeight + S(4.0f + 20.0f);
        });

        // The CPU's and the GPU's milliseconds a frame, side by side.
        const bool gpuKnown = s_gpuMs >= 0.0f;
        const bool cpuBound = !gpuKnown || s_cpuMs >= s_gpuMs;
        Card("CPU and GPU per frame", gpuKnown ? (cpuBound ? "CPU-bound" : "GPU-bound") : "", cpuBound ? ACCENT : IM_COL32(169, 155, 255, 255),
            [&](ImDrawList* draw, ImVec2 pos, float width) {
                const float barTop = pos.y + S(2.0f), barHeight = S(10.0f);
                draw->AddRectFilled(ImVec2(pos.x, barTop), ImVec2(pos.x + width, barTop + barHeight), IM_COL32(34, 43, 54, 255), barHeight * 0.5f);
                const float total = s_cpuMs + (gpuKnown ? s_gpuMs : 0.0f);
                if (total > 0.0f)
                {
                    const float gap = gpuKnown ? S(4.0f) : 0.0f;
                    const float cpuWidth = (width - gap) * s_cpuMs / total;
                    draw->AddRectFilled(ImVec2(pos.x, barTop), ImVec2(pos.x + cpuWidth, barTop + barHeight), FILL_IDLE, barHeight * 0.5f,
                        gpuKnown ? ImDrawFlags_RoundCornersLeft : ImDrawFlags_RoundCornersAll);
                    if (gpuKnown)
                        draw->AddRectFilled(ImVec2(pos.x + cpuWidth + gap, barTop), ImVec2(pos.x + width, barTop + barHeight), GPU_COLOR,
                            barHeight * 0.5f, ImDrawFlags_RoundCornersRight);
                }
                float x = pos.x;
                const float centerY = barTop + barHeight + S(6.0f + 10.0f);
                const std::pair<ImU32, std::string> keys[] = { { FILL_IDLE, "CPU " + Milliseconds(s_cpuMs) },
                    { GPU_COLOR, gpuKnown ? "GPU " + Milliseconds(s_gpuMs) : std::string("GPU not measured") } };
                for (const auto& [color, label] : keys)
                {
                    draw->AddRectFilled(ImVec2(x, std::round(centerY - S(1.5f))), ImVec2(x + S(10.0f), std::round(centerY - S(1.5f)) + S(3.0f)), color, S(2.0f));
                    x += S(15.0f);
                    TextMiddle(draw, Face::Sans, 12.5f, x, centerY, TEXT_2, label);
                    x += TextWidth(Face::Sans, 12.5f, label) + S(14.0f);
                }
                const char* note = "CPU: the game's and renderer's work a frame. GPU: the graphics card's.";
                TextWrapped(draw, Face::Sans, 12.5f, ImVec2(pos.x, centerY + S(14.0f)), width, 2, TEXT_3, note);
                return barHeight + S(2.0f + 6.0f + 20.0f + 4.0f) + S(12.5f * 1.35f * 2.0f);
            });
    }
}
