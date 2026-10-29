// The framework without a GPU or a window: the settings file, the input
// gate, and the panel built frame by frame (opened with the pad's chord,
// a row pressed by a test script) with no renderer.
//
//   rui_selftest <scratch folder>
#include <rui/gate.h>
#include <rui/rui.h>
#include <rui/settings.h>
#include <rui/stats.h>
#include <rui/style.h>
#include <rui/widgets.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace
{
    int s_failures = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        s_failures += ok ? 0 : 1;
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

    // An app's settings.
    enum class Quality
    {
        Low,
        High,
    };
    struct Options
    {
        bool fullscreen = false;
        int volume = 100;
        Quality quality = Quality::High;
        std::string name;
        bool oldFlag = false;
    } s_options;
}

int main(int argc, char** argv)
{
    const std::filesystem::path scratch = argc > 1 ? argv[1] : std::filesystem::temp_directory_path();
    const std::filesystem::path file = scratch / "selftest_settings.ini";
    rui::SetLog([](rui::LogKind kind, std::string_view text) { std::printf("  [log%s] %.*s\n", kind == rui::LogKind::Event ? " event" : "", int(text.size()), text.data()); });

    // ---- settings ----
    namespace settings = rui::settings;
    settings::Bool("video", "fullscreen", s_options.fullscreen);
    settings::Choice("video", "quality", s_options.quality, { { Quality::Low, "low" }, { Quality::High, "high" } }, { { Quality::High, "ultra" } });
    settings::Int("audio", "volume", s_options.volume, 0, 100);
    rui::RegisterSettings("overlay");
    settings::String("profile", "name", s_options.name, 8);
    settings::Alias("audio", "master", "volume");
    settings::Obsolete("debug", "old_flag", [](std::string_view text) { return settings::ParseBool(text, s_options.oldFlag); });
    bool afterLoad = false;
    settings::AfterLoad([&] { afterLoad = true; });
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "# a comment\n[video]\nfullscreen = 1\nquality = ultra\nunknown = 3\n[audio]\nmaster = 250\n[overlay]\nhud = full\nscale = 1.25\n"
               "[profile]\nname =  a long name here \n[debug]\nold_flag = true\n[other]\nkey = value\n";
    }
    settings::Load(file);
    Check(afterLoad, "AfterLoad ran");
    Check(s_options.fullscreen, "bool read (1)");
    Check(s_options.quality == Quality::High, "choice alias read");
    Check(s_options.volume == 100, "int alias read, clamped");
    Check(rui::stats::Mode() == rui::stats::HudMode::Full, "overlay hud read");
    Check(rui::OverlayScale() == 125, "overlay scale read as a factor");
    Check(s_options.name == "a long n", "string trimmed and cut");
    Check(s_options.oldFlag, "obsolete key read");
    Check(settings::WasRead("overlay", "hud") && !settings::WasRead("overlay", "hud_corner"), "WasRead");
    Check(settings::DefaultText("audio", "volume") == "100", "default text");
    Check(settings::Save(), "saved");
    const std::string expected = "[video]\nfullscreen = true\nquality = high\n\n[audio]\nvolume = 100\n\n[overlay]\nhud = full\nhud_corner = top_right\nscale = 1.25\n"
                                 "\n[profile]\nname = a long n\n";
    Check(ReadFile(file) == expected, "file written in registration order, obsolete keys left out");
    Check(settings::CurrentStatus() == settings::Status::Saved, "status saved");

    // ---- the gate ----
    namespace gate = rui::gate;
    using rui::pad::A;
    using rui::pad::BACK;
    using rui::pad::START;
    Check(gate::StripToggle(BACK | START | A) == A, "toggle chord stripped");
    Check(gate::StripToggle(BACK | A) == (BACK | A), "half a chord kept");
    Check(!gate::Holds(A, 0, 0), "not held while closed");
    gate::SetCapture(true);
    Check(gate::Holds(0, 0, 0), "held while captured");
    gate::SetCapture(false);
    Check(gate::Releasing() && gate::Holds(A, 0, 0), "held after closing while A is down");
    Check(gate::Holds(0, 200, 0), "held while a trigger is down");
    Check(!gate::Holds(0, 10, 0) && !gate::Releasing(), "released once nothing is held");

    // ---- the panel, no renderer ----
    bool pressed = false;
    bool toggle = false;
    int changed = 0;
    settings::OnChanged([&] { changed++; });
    rui::AddPage({ "first", "First", "Settings", rui::ICON_MONITOR, "A page of rows.", [&] {
                      rui::GroupHeader("Group");
                      if (rui::Button({ "Action", "Presses a button." }, rui::ICON_CHECK_CIRCLE, "Press"))
                          pressed = true;
                      rui::Toggle({ "Fullscreen", "A toggle bound to a setting.", rui::Chip::Restart() }, s_options.fullscreen, false);
                      rui::Toggle({ "Plain", "Not a setting.", rui::Chip::Note("Next level") }, toggle, false);
                      rui::OverlaySizeRow();
                  } });
    rui::AddPage({ "stats", "Performance", "Tools", rui::ICON_SPEED, "Live numbers.", [] {
                      rui::stats::HudRows();
                      rui::stats::Cards();
                  } });
    uint16_t buttons = 0;
    rui::InitInfo info;
    info.appName = "selftest";
    info.backendName = "none";
    info.readPad = [&](rui::PadState& pad) {
        pad.connected = true;
        pad.buttons = buttons;
    };
    Check(rui::Init(info), "Init without a renderer");
    rui::stats::SetCounter("Things", "Things / frame");
    // Row 1 (Fullscreen) of the first page, pressed after 0.05 s.
    rui::TestScript script = rui::ParseTestScript("first:1:press=0.05");
    Check(script.page == "first" && script.row == 1 && script.pressAfter > 0.04f, "test script parsed");
    Check(!rui::SetTestScript(rui::ParseTestScript("missing")), "unknown page refused");
    Check(rui::SetTestScript(script), "test script set");
    const bool before = s_options.fullscreen;
    for (int frame = 0; frame < 30; frame++)
    {
        rui::stats::AddFrame(16.0f, 8.0f, 4.0f, 100.0f);
        rui::NewFrame(1280, 720);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Check(rui::Visible(), "opened by the test script");
    Check(gate::Captured(), "the gate holds the pad while open");
    Check(s_options.fullscreen != before && changed == 1, "the row pressed changed the setting, OnChanged ran");
    Check(settings::CurrentStatus() == settings::Status::Pending, "a save pending");
    const ImDrawData* data = ImGui::GetDrawData();
    Check(data != nullptr && data->TotalVtxCount > 1000, "the panel drew");

    // Back + Start: closes it.
    buttons = BACK;
    rui::NewFrame(1280, 720);
    buttons = BACK | START;
    rui::NewFrame(1280, 720);
    Check(!rui::Visible() && gate::Releasing(), "closed by the chord, the gate waits for release");
    buttons = 0;
    rui::NewFrame(1280, 720);
    rui::ShowPage("stats");
    rui::stats::Mode() = rui::stats::HudMode::Full;
    for (int frame = 0; frame < 3; frame++)
        rui::NewFrame(1920, 1080);
    Check(rui::Visible(), "ShowPage opened it");
    settings::SavePending();
    Check(settings::CurrentStatus() == settings::Status::Saved && ReadFile(file).find("fullscreen = false") != std::string::npos, "SavePending wrote it");
    rui::Shutdown();
    std::error_code error;
    std::filesystem::remove(file, error);

    std::printf("%s\n", s_failures == 0 ? "all passed" : "FAILED");
    return s_failures == 0 ? 0 : 1;
}
