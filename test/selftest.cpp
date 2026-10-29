// The framework without a GPU or a window: the settings file, the input
// gate, the panel built frame by frame (opened with the pad's chord, a row
// pressed by a test script) with no renderer, and the on-screen keyboard
// driven by made-up pad states, keys and clicks.
//
//   rui_selftest <scratch folder>
#include <rui/gate.h>
#include <rui/keyboard.h>
#include <rui/rui.h>
#include <rui/settings.h>
#include <rui/stats.h>
#include <rui/style.h>
#include <rui/widgets.h>

#include <cfloat>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
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
    uint8_t triggerLeft = 0, triggerRight = 0;
    rui::InitInfo info;
    info.appName = "selftest";
    info.backendName = "none";
    info.readPad = [&](rui::PadState& pad) {
        pad.connected = true;
        pad.buttons = buttons;
        pad.leftTrigger = triggerLeft;
        pad.rightTrigger = triggerRight;
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

    // ---- the on-screen keyboard ----
    namespace keyboard = rui::keyboard;
    using namespace rui::pad;
    const auto frame = [&] { rui::NewFrame(1280, 720); };
    // A button pressed for a frame, then released.
    const auto press = [&](uint16_t button, int times = 1) {
        for (int i = 0; i < times; i++)
        {
            buttons = button;
            frame();
            buttons = 0;
            frame();
        }
    };
    // The triggers past their threshold, through readPad.
    const auto trigger = [&](bool right) {
        (right ? triggerRight : triggerLeft) = 255;
        frame();
        triggerRight = triggerLeft = 0;
        frame();
    };
    const auto key = [&](ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key, true);
        frame();
        ImGui::GetIO().AddKeyEvent(key, false);
        frame();
    };
    const auto click = [&](std::string_view name) {
        const std::optional<ImVec2> center = keyboard::KeyCenter(name);
        if (!center)
            return false;
        ImGui::GetIO().AddMousePosEvent(center->x, center->y);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
        return true;
    };
    std::optional<std::string> result;
    int answers = 0;
    const auto onDone = [&](std::optional<std::string> text) {
        result = std::move(text);
        answers++;
    };
    const auto is = [&](const char* text) { return result && *result == text; };

    // With the panel closed, from the host: 5 characters at most.
    rui::SetVisible(false);
    press(0, 2);
    Check(!gate::Captured(), "panel closed, the pad released");
    keyboard::Open("Name", "", 5, onDone);
    frame();
    Check(keyboard::IsOpen() && !rui::Visible() && gate::Captured(), "keyboard open on its own, the gate holds the pad");
    frame();
    Check(rui::WantsTextInput(), "text input wanted while it's open");
    // From q (the second row's first key): down to a, right to h; Y shifts it.
    press(DPAD_DOWN);
    press(DPAD_RIGHT, 5);
    press(Y);
    press(A);
    // Up to y, right to i: lower case again.
    press(DPAD_UP);
    press(DPAD_RIGHT, 2);
    press(A);
    press(X);  // "Hi "
    press(B);  // "Hi"
    // Right to o, the cursor back one (LB), o typed there, the cursor to
    // the end (RB), o three times: the fifth character fits, the sixth not.
    press(DPAD_RIGHT);
    press(LEFT_SHOULDER);
    press(A);
    press(RIGHT_SHOULDER);
    press(A, 3);
    Check(answers == 0 && keyboard::IsOpen(), "still open while typing");
    press(START);
    Check(answers == 1 && is("Hoioo"), "typed, shifted, deleted, cursor moved, cut at maxLength, Start: Done");
    Check(!keyboard::IsOpen() && !gate::Captured() && !rui::Visible(), "closed, the pad released, the panel still closed");

    // Back cancels; Y twice is caps lock, once more turns it off.
    keyboard::Open("Caps", "", 10, onDone);
    frame();
    press(A);
    press(BACK);
    Check(answers == 2 && !result && !keyboard::IsOpen(), "Back cancels");
    keyboard::Open("Caps", "", 10, onDone);
    frame();
    press(Y, 2);
    press(A, 2);
    press(Y);
    press(A);
    press(START);
    Check(answers == 3 && is("QQq"), "Y twice: caps lock, once more: off");

    // B deletes; on the empty text, B cancels.
    keyboard::Open("Delete", "ab", 10, onDone);
    frame();
    press(B, 2);
    Check(keyboard::IsOpen() && answers == 3, "B deleted the text, still open");
    press(B);
    Check(!keyboard::IsOpen() && answers == 4 && !result, "B on an empty text cancels");

    // The key pages: RT to the symbols (- in q's place), RT to the accents
    // (e circumflex), LT back to the symbols.
    keyboard::Open("Pages", "", 10, onDone);
    frame();
    trigger(true);
    press(A);
    trigger(true);
    press(A);
    trigger(false);
    press(A);
    press(START);
    Check(is("-\xC3\xAA-"), "RT / LT switch the pages");

    // A physical keyboard types too (an accent; in an ASCII one, not), Enter
    // is Done, Escape cancels.
    keyboard::Open("Keys", "", 10, onDone);
    frame();
    ImGui::GetIO().AddInputCharactersUTF8("ok\xC3\xA9");
    frame();
    key(ImGuiKey_Backspace);
    ImGui::GetIO().AddInputCharactersUTF8("!");
    frame();
    key(ImGuiKey_Enter);
    Check(is("ok!"), "typed on the keyboard, Backspace, Enter: Done");
    keyboard::Request ascii;
    ascii.title = "ASCII";
    ascii.charset = keyboard::Charset::Ascii;
    ascii.onDone = onDone;
    keyboard::Open(std::move(ascii));
    frame();
    ImGui::GetIO().AddInputCharactersUTF8("\xC3\xA9" "1");
    frame();
    key(ImGuiKey_Enter);
    Check(is("1"), "an ASCII keyboard refuses the accent");
    keyboard::Open("Escape", "x", 10, onDone);
    frame();
    key(ImGuiKey_Escape);
    Check(!result && !keyboard::IsOpen(), "Escape cancels");

    // Clicks.
    keyboard::Open("Mouse", "", 10, onDone);
    frame();
    frame();
    const bool clicked = click("a") && click("Shift") && click("b") && click("Done");
    Check(clicked && is("aB"), "clicks type keys, shift and Done");
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    // Posted and cancelled from another thread; two at once take turns.
    keyboard::Id posted = 0;
    std::thread([&] {
        keyboard::Request request;
        request.title = "Posted";
        request.onDone = onDone;
        posted = keyboard::Post(std::move(request));
    }).join();
    Check(!keyboard::IsOpen(), "a Post waits for the next frame");
    frame();
    Check(keyboard::IsOpen(), "the Post opened it");
    std::thread([&] { keyboard::Cancel(posted); }).join();
    const int answered = answers;
    frame();
    Check(!keyboard::IsOpen() && answers == answered + 1 && !result, "Cancel from another thread");
    keyboard::Open("First", "1", 10, onDone);
    keyboard::Open("Second", "2", 10, onDone);
    frame();
    press(START);
    Check(is("1") && keyboard::IsOpen(), "the first done, the second shows");
    press(START);
    Check(is("2") && !keyboard::IsOpen(), "then the second");

    // A text field edited from the pad opens it; its Done is the value.
    // From the keyboard, it's typed in place as before.
    rui::AddPage({ "text", "Text", "Settings", rui::ICON_KEYBOARD, "A text field.", [&] {
                      rui::TextField({ "Name", "A name, up to 8 characters." }, s_options.name, 8, "(none)");
                  } });
    press(0);
    rui::ShowPage("text");
    for (int i = 0; i < 4; i++)
        frame();
    const int changedBefore = changed;
    press(A);
    Check(keyboard::IsOpen() && rui::Visible(), "A on the text field: the keyboard over the panel");
    press(B, 8);  // "a long n" deleted
    press(A);     // q
    press(START);
    frame();
    Check(!keyboard::IsOpen() && rui::Visible() && s_options.name == "q" && changed == changedBefore + 1,
        "Done set the field, OnChanged ran, the panel stays open");
    press(A);
    Check(keyboard::IsOpen(), "the row has the focus again: A opens it again");
    press(BACK);
    press(0);
    Check(!keyboard::IsOpen() && rui::Visible() && s_options.name == "q", "Back: cancelled, the value kept, the panel open");
    key(ImGuiKey_Enter);
    frame();
    frame();
    Check(!keyboard::IsOpen() && ImGui::GetIO().WantTextInput, "Enter on the field: typed in place, no keyboard");
    key(ImGuiKey_Escape);
    frame();

    // Shutdown answers the one left open.
    keyboard::Open("Left open", "z", 10, onDone);
    frame();
    result = "not answered";
    rui::Shutdown();
    Check(!result && !keyboard::IsOpen(), "Shutdown cancelled the one open");

    std::error_code error;
    std::filesystem::remove(file, error);

    std::printf("%s\n", s_failures == 0 ? "all passed" : "FAILED");
    return s_failures == 0 ? 0 : 1;
}
