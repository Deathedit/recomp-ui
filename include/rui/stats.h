// Frame statistics: the performance HUD in a screen corner, and the cards
// for a page of the game's (a Performance page, say).
//
// The host feeds it each presented frame's time, the CPU's work on it and
// the GPU's time (AddFrame), and optionally a count of its own (draws, say).
// FPS, milliseconds, CPU, GPU and the count are averaged over half a
// second; the 1% and 0.1% lows are over the last 2,000 frames (the mean of
// the slowest 20 and 2). The drawing thread only.
#pragma once

#include <string>

namespace rui::stats
{
    // Off, FPS only, Compact (FPS, ms and a sparkline) or Full (plus CPU and
    // GPU ms, the 1% low and the count).
    enum class HudMode
    {
        Off,
        Fps,
        Compact,
        Full,
    };
    enum class HudCorner
    {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };
    const char* HudModeName(HudMode mode);        // "off", "fps", "compact", "full"
    const char* HudCornerName(HudCorner corner);  // "top_left", ...

    // The HUD's options, shown whether the panel is open or not
    // (rui::RegisterSettings binds them to the settings file).
    HudMode& Mode();
    HudCorner& Corner();

    // A presented frame: its time and cost (CPU work, GPU time; negative:
    // not known), and the host's count for it (SetCounter).
    void AddFrame(float frameMilliseconds, float cpuMilliseconds, float gpuMilliseconds, float count = 0.0f);
    // The count's name in the HUD's Full lines and in the cards' tile
    // ("Draws", "Draws / frame"); empty (the default): no count shown.
    void SetCounter(std::string hudLabel, std::string tileLabel);
    // The frame rate the FPS is coloured against (amber under 90%, red
    // under half) and the graphs are scaled to: the game's frame cap.
    void SetTargetFps(float fps);
    // Forgets the frames the lows are taken over (after a loading screen).
    void ResetLows();

    // For a page: the HUD's mode and corner and a Reset lows button (rows),
    // then the tiles (average, lows, the count), the frame time graph and
    // the CPU/GPU split (cards).
    void HudRows();
    void Cards();
}
