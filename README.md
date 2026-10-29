# recomp-ui

An in-game overlay for the host runtime of a statically recompiled game: a settings panel
the player opens over the game's frame, a settings file behind it, a performance HUD,
toasts, an on-screen keyboard, and an input gate that keeps the game from seeing the pad
while the panel or the keyboard is open.
Dear ImGui, drawn through [plume](https://github.com/renderbag/plume) on D3D12 or Vulkan.
Pad first: everything works with a controller, the keyboard and the mouse.

The framework knows nothing about the game. The game adds its own **pages** (functions that
lay out rows) and its own **settings keys** (bound to its own variables); the framework does
the panel, navigation, looks, the file, the HUD and the plumbing between threads.

| Part | Headers | What |
|---|---|---|
| Core shell | `rui/rui.h`, `rui/style.h`, `rui/widgets.h`, `rui/toasts.h` | Init and the frame's calls, the panel (header, sidebar of pages, rows, description strip, footer of hints), open/close (a key and a pad chord), pad/keyboard/mouse navigation, the look (colours, fonts, 720p-based scaling), rows and cards, toasts |
| Settings | `rui/settings.h` | An ini file of `[section] key = value`, keys bound to the game's variables, defaults, saved half a second after a change and at exit |
| Stats HUD | `rui/stats.h` | FPS / frame time / CPU / GPU (and a count of the host's, such as draws) in a screen corner, 1% and 0.1% lows, cards for a page |
| On-screen keyboard | `rui/keyboard.h` | Text typed with the pad on a grid of keys (letters, symbols, accented letters), over the game or the panel; text fields open it from the pad, a host opens it for anything else (a game's own request for text) |
| Input gate | `rui/gate.h` | A neutral pad for the game while the panel is open and until the buttons are released after it closes; the toggle chord never reaches the game |
| Renderer backend | `rui/imgui_impl_plume.h`, `rui/imgui_impl_plume_shaders.h` | Dear ImGui on plume: one pipeline, buffers per frame in flight, textures as ImGui asks (1.92's `RendererHasTextures`) |

Only the plume backend is supported (no stock DX12/Vulkan ImGui backends): it records into
the host's own command list and framebuffer, so the overlay goes wherever the host draws.

## Requirements

- C++20 (`std::format`), CMake 3.20+, Clang or MSVC on Windows, Clang or GCC on Linux.
- From the host, as CMake targets: **Dear ImGui 1.92+** with its SDL3 platform backend
  (`backends/imgui_impl_sdl3.cpp`) compiled in, **SDL3**, **plume**.
- A way to compile HLSL to DXIL (D3D12) and SPIR-V (Vulkan), such as DXC: the framework
  calls the host's `compileShader` with the HLSL of its two shaders (or the host answers
  from a precompiled cache; `rui/imgui_impl_plume_shaders.h` has the sources).

## Bringing it in

Put the framework inside your repository so a checkout (or a `git bundle`) is complete
without network access: `git subtree` does that, a submodule doesn't.

```
git subtree add --prefix=external/recomp-ui <path or url of recomp-ui> main --squash
git subtree pull --prefix=external/recomp-ui <path or url of recomp-ui> main --squash   # later updates
```

Then in CMake, name your targets and add it:

```cmake
set(RUI_IMGUI_TARGET my_imgui)          # default: imgui
set(RUI_SDL_TARGET SDL3::SDL3-static)   # default: SDL3::SDL3
set(RUI_PLUME_TARGET plume)             # default: plume
# set(RUI_ICON_FONT ${CMAKE_SOURCE_DIR}/fonts/my-icons.ttf)  # optional, fonts/README.md
add_subdirectory(external/recomp-ui ${CMAKE_BINARY_DIR}/recomp-ui)
target_link_libraries(my_runtime PRIVATE recomp_ui)
```

On Windows the target defines `NOMINMAX` for its users: `rui/rui.h` includes plume's
header, which includes `windows.h`.

## Integrating it

The host has three threads that matter: the **event thread** (pumps SDL's events, usually
the main thread), the **drawing thread** (records the frame the swap chain presents), and
the **game's threads** (read the pad through the host's XInput functions).

1. **Settings, at startup** (any thread, before the overlay starts). Register your keys,
   then `rui::RegisterSettings("overlay")` where the overlay's own keys (HUD, size) should
   sit in the file, then load:

   ```cpp
   rui::SetLog([](rui::LogKind, std::string_view text) { MyLog("[ui] {}", text); });
   rui::settings::Bool("video", "vsync", g_options.vsync);
   rui::settings::Int("audio", "volume", g_options.volume, 0, 100);
   rui::RegisterSettings("overlay");
   rui::settings::OnChanged([] { ApplyOptions(g_options); });  // a change made in the panel
   rui::settings::Load(userFolder / "settings.ini");
   ApplyOptions(g_options);
   ```

2. **Pages**, before or after `Init` (see the example below).

3. **Init on the drawing thread**, once the device and swap chain exist:

   ```cpp
   rui::InitInfo info;
   info.window = sdlWindow;            // for SDL's events
   info.device = plumeDevice;
   info.shaderFormat = vulkan ? plume::RenderShaderFormat::SPIRV : plume::RenderShaderFormat::DXIL;
   info.renderTargetFormat = plume::RenderFormat::B8G8R8A8_UNORM;  // the swap chain's
   info.framesInFlight = 2;            // the host waits for frame N-2 before recording N
   info.compileShader = [](std::string_view hlsl, rui::ShaderStage stage, std::vector<uint8_t>& code) {
       return CompileHlsl(hlsl, stage == rui::ShaderStage::Vertex ? "vs_6_0" : "ps_6_0", code);  // SPIR-V: -fvk-invert-y for the vertex shader
   };
   info.appName = "my-recomp";
   info.backendName = vulkan ? "Vulkan" : "D3D12";
   info.readPad = [](rui::PadState& pad) {
       XINPUT_STATE state{};
       pad.connected = XInputGetState(0, &state) == ERROR_SUCCESS;
       pad.buttons = state.Gamepad.wButtons;
       pad.leftX = state.Gamepad.sThumbLX;
       pad.leftY = state.Gamepad.sThumbLY;
   };
   // info.toggleKey = ImGuiKey_F1; info.toggleButtons = rui::pad::BACK | rui::pad::START;  (the defaults)
   if (!rui::Init(info))
       MyLog("no overlay");
   ```

4. **Events from the event thread**: hand each polled event over, and start SDL's text
   input while a text field wants it.

   ```cpp
   while (SDL_PollEvent(&event)) { rui::QueueEvent(event); /* ... */ }
   if (rui::WantsTextInput() != textInputOn) { /* SDL_StartTextInput / SDL_StopTextInput */ }
   ```

5. **Each presented frame, on the drawing thread**, in this order:

   ```cpp
   rui::NewFrame(swapWidth, swapHeight);   // input, the toggle, pages, HUD, toasts
   rui::RecordUploads(list);               // before the swap image's render pass (copies would end one)
   // ... the swap image to COLOR_WRITE, the game's frame drawn or blitted into it ...
   rui::Draw(list, swapFramebuffer);       // last, before present
   rui::stats::AddFrame(frameMs, cpuMs, gpuMs, float(drawCount));
   ```

   **Screenshots stay overlay-free**: capture the game's frame before `rui::Draw` (or copy
   the image you blit from). `rui::DrawInto` draws the same overlay frame into another
   target when you do want it in a capture.

6. **The input gate in the host's XInput path** (the game's threads): filter each real
   controller state the game gets, and drop keystrokes while held.

   ```cpp
   if (XInputGetState(user, &state) == ERROR_SUCCESS)
       rui::gate::FilterXInput(state.Gamepad);  // neutral while held; the toggle chord stripped
   // keystrokes:
   if (rui::gate::Captured() || (rui::gate::Releasing() && XInputGetState(k.UserIndex, &s) == ERROR_SUCCESS &&
                                 rui::gate::Holds(s.Gamepad.wButtons, s.Gamepad.bLeftTrigger, s.Gamepad.bRightTrigger)))
       return ERROR_EMPTY;
   ```

   Inputs the host makes up itself (replays, a virtual pad) needn't go through it.

7. **At exit**: `rui::settings::SavePending()` (any thread) writes a change still waiting.

### Threading rules

| Thread | May call |
|---|---|
| Any, before the overlay starts | `rui::settings` registration and `Load`, `rui::AddPage`, `rui::OnFrame`, `rui::OnForeground`, `rui::SetLog` |
| Drawing thread | `Init`, `NewFrame`, `RecordUploads`, `Draw`, `DrawInto`, `Shutdown`, `rui::stats::*`, `PushToast`, `ShowPage`, `SetVisible`, `rui::keyboard::Open`, `IsOpen`, and everything in pages and hooks (widgets, style). The ImGui context is only used here. The settings' variables belong to this thread from `Init` on, inside `rui::settings::Mutex()` (`NewFrame` holds it while pages and hooks run). |
| Event thread | `QueueEvent`, `WantsTextInput` |
| Any | `PostToast`, `rui::keyboard::Post`, `rui::keyboard::Cancel`, `rui::gate::*`, `rui::settings::SavePending` |

`ImGui_ImplSDL3_NewFrame` is never called (it would call SDL off the event thread):
`NewFrame` sets the display size and time itself, and the mouse cursor is left alone.

## A page

```cpp
#include <rui/rui.h>
#include <rui/widgets.h>
#include <rui/style.h>

struct Options { bool vsync = true; int volume = 100; int quality = 1; } g_options;
const Options g_defaults;

void AddPages()
{
    rui::AddPage({ "video", "Video", "Settings", rui::ICON_MONITOR, "Resolution and image quality.", [] {
        rui::GroupHeader("Display");
        rui::Toggle({ "VSync", "No tearing; the frame rate follows the display." }, g_options.vsync, g_defaults.vsync);
        static const std::string names[] = { "Low", "Medium", "High" };
        rui::Selector({ "Quality", "Higher looks better and costs GPU time.", rui::Chip::Restart() },
            g_options.quality, names, g_defaults.quality);
        if (rui::Button({ "Screenshot", "Saves the next frame." }, rui::ICON_PHOTO_CAMERA, "Save PNG"))
            RequestScreenshot();
    } });
    rui::AddPage({ "audio", "Audio", "Settings", rui::ICON_VOLUME_UP, "Volume.", [] {
        rui::Slider({ "Volume", "Everything the game plays." }, g_options.volume, 0, 100, 5, "%d", g_defaults.volume);
    } });
    rui::AddPage({ "performance", "Performance", "Tools", rui::ICON_SPEED, "Live numbers from this run.", [] {
        rui::stats::HudRows();
        rui::stats::Cards();
    } });
}
```

A row bound to a registered key needs nothing more: when a frame of the panel leaves any
registered key's value different, the framework runs the `OnChanged` hooks and saves.

## The on-screen keyboard

`rui/keyboard.h`: text typed with the pad on a grid of keys, for players with only a
controller (a handheld, a couch). It draws over the game's frame or over the panel, in the
panel's look and scale, and while it's open the input gate keeps the pad from the game as
the panel does.

```
 Name                                         4 / 15
 Your name, up to 15 characters.
 ┌──────────────────────────────────────────────────┐
 │ Ride|                                            │
 └──────────────────────────────────────────────────┘
              LT [ ABC ] [ #+= ] [ ÀÉ ] RT
 [1][2][3][4][5][6][7][8][9][0]
 [q][w][e][r][t][y][u][i][o][p]
 [a][s][d][f][g][h][j][k][l]['] 
 [z][x][c][v][b][n][m][,][.][-]
 [Shift][#+=][     Space     ][ ⌫ ][Done]
 A Type  B Delete  X Space  Y Shift  LB RB Cursor  Start Done  Back Cancel
```

Three pages of keys: letters (a digit row over QWERTY), symbols (all of ASCII's and € £ ¥ ¢
° § ¡ ¿) and accented Latin letters (à-ÿ, œ, ß, ł, č, ę, ń, ś, š, ž and their capitals).
Every key is a character the embedded IBM Plex Sans has. `Charset::Ascii` keeps a request
to printable ASCII: no accent page, the other symbols greyed, and a physical keyboard's
other characters dropped.

| Pad | Keyboard | Mouse | Does |
|---|---|---|---|
| D-pad / left stick | - | - | move between keys (repeats when held; round at the edges) |
| A | (typing) | click a key | type the key (Shift, the page key, Space, Backspace, Done too) |
| B | Backspace, Delete | the Backspace key | delete before the cursor; on an empty text, cancel |
| X | Space | the Space key | a space |
| Y | - | the Shift key | shift the next letter; twice quickly or held: caps lock; again: off |
| LB / RB | Left / Right, Home / End | click in the text | move the cursor |
| LT / RT | - | a page's tab | previous / next key page |
| Start | Enter | the Done key | done |
| Back | Esc | - | cancel |

The footer shows these for the input used last (pad, keys or mouse). A physical keyboard
always types while it's open: `rui::WantsTextInput` is true then, so the host's SDL text
input is on. (On some systems, Steam Deck desktop mode among them, `SDL_StartTextInput`
also brings up the system's own on-screen keyboard; set `SDL_HINT_ENABLE_SCREEN_KEYBOARD`
to `"0"` before `SDL_Init` to keep it away.)

**Text fields use it themselves**: `TextField` opens it when the edit starts from the pad
(A on the row), titled with the row's label and described with its description; its Done
sets the value (the next frame, which `TextField` reports as a change, so a bound settings
key saves). Started with Enter or a click, the field is typed in place as before.

**A host opens it** for anything else, with the panel open or closed:

```cpp
rui::keyboard::Open("Server address", g_server, 64, [](std::optional<std::string> text) {
    if (text)
        g_server = *text;  // Done; nullopt: cancelled
});

rui::keyboard::Request request;
request.title = "Message";
request.description = "Sent to everyone in the lobby.";
request.placeholder = "Say something";
request.maxLength = 80;                         // characters
request.maxBytes = 120;                         // and UTF-8 bytes, if the receiver counts them
request.charset = rui::keyboard::Charset::Ascii;
request.onDone = [](std::optional<std::string> text) { /* ... */ };
const rui::keyboard::Id id = rui::keyboard::Post(std::move(request));  // from any thread
rui::keyboard::Cancel(id);                                             // from any thread
```

- `Open` is for the drawing thread (in a frame or between); `Post` and `Cancel` are safe
  from any thread and take effect at the next `NewFrame`.
- One shows at a time; more wait their turn. Each one's `onDone` runs exactly once, on the
  drawing thread inside `NewFrame` (the ImGui frame, `rui::settings::Mutex()` held): the
  text on Done, `std::nullopt` on cancel, on `Cancel(id)` and at `Shutdown`. It may open
  another.
- The press that opens it isn't a key: input counts from its second frame. While it's
  open the panel's own input waits (and the toggle chord until its buttons are released
  after it closes, Back being the keyboard's cancel); afterwards the panel's focus is back
  on its row.
- `IsOpen()` (drawing thread). `KeyCenter(key)` gives where a key was drawn, for tests that
  click.

### Answering a game's keyboard request

A console game that asks the system for text (on the Xbox 360, `XamShowKeyboardUI`: a
title, a description, the default text, a buffer of `cchMaxText` UTF-16 characters, and an
`XOVERLAPPED` or a blocking call) can get it from this keyboard. The request is
asynchronous on both sides, so nothing waits on the drawing thread:

```cpp
// The game's thread, in the host's implementation of the system call:
uint32_t ShowKeyboard(const std::u16string& title, const std::u16string& description, const std::u16string& initial,
                      char16_t* buffer, uint32_t maxChars, Overlapped* overlapped)
{
    auto request = std::make_shared<PendingRequest>(buffer, maxChars, overlapped);
    rui::keyboard::Request keys;
    keys.title = ToUtf8(title);
    keys.description = ToUtf8(description);
    keys.initial = ToUtf8(initial);
    keys.maxLength = maxChars - 1;  // the buffer's terminator
    keys.onDone = [request](std::optional<std::string> text) {
        // The drawing thread: fill the game's buffer, then complete the request
        // (the overlapped's result and event, or wake the blocked caller).
        if (text)
            request->Complete(ToUtf16(*text), SUCCESS);
        else
            request->Complete(ToUtf16(""), CANCELLED);
    };
    request->id = rui::keyboard::Post(std::move(keys));
    return overlapped != nullptr ? IO_PENDING : request->Wait();
}
// The game cancelling its overlapped: rui::keyboard::Cancel(request->id).
```

Write into the game's memory and complete the request (set the result, signal the event,
queue the completion) the way the host's other asynchronous system calls do; the only
difference is that `onDone` runs on the drawing thread. With `maxLength` in characters, a
UTF-16 buffer of `cchMaxText` holds the result for everything the keys type (all of it is
in the Basic Multilingual Plane). If the game can't draw some characters (its font has
ASCII only, say), ask for `Charset::Ascii`.

## API reference

### `rui/rui.h`

- `InitInfo`: `window`, `device` (null: no renderer, the UI is still built; for tests),
  `shaderFormat`, `renderTargetFormat`, `framesInFlight`, `compileShader`, `appName`,
  `backendName`, `headerChip` (the header's chip text; default `<backend> · <w>×<h>`),
  `readPad`, `toggleKey`, `toggleButtons` (all held, one just pressed), `closeButtons`
  (and Escape).
- `PadState`, `pad::*` (XInput's button bits, dead zone and trigger threshold).
- `SetLog`, `Init`, `Shutdown`, `Active`.
- `NewFrame`, `RecordUploads`, `Draw`, `DrawInto`.
- `QueueEvent`, `WantsTextInput`.
- `Page { key, name, group, icon, subtitle, draw }`, `AddPage`, `Visible`, `SetVisible`,
  `ShowPage(key)`.
- `OnFrame(hook)`: each frame inside the ImGui frame before the panel (a change a hook makes
  isn't a panel change; call `rui::settings::Changed` to save it).
  `OnForeground(hook(panelOpen))`: after the HUD and toasts, for the game's own notices.
- `OverlayScale()`, `OverlaySizeRow()`, `RegisterSettings(section)`: the overlay's size
  (auto, 90-150%) and the HUD's keys (`hud`, `hud_corner`, `scale`).
- `ToastKind`, `PostToast` (any thread).
- `TestScript`, `ParseTestScript("<page>[:<row>[:press[=<s>]]]")`, `SetTestScript`: open a
  page at the first frame, press a row, close again; for screenshots of the panel.

### `rui/settings.h`

- Keys: `Bool`, `Int(low, high)`, `String(maxLength)`, `Choice(names, aliases)`,
  `Custom(read, write)`, `Obsolete(read)` (read, never written), `Alias(oldKey, key)`,
  `AfterLoad(hook)`. The file is written in registration order, a section where its first
  key was registered; a key's default is its variable's value when registered.
- `Load(path)`, `Path()`, `WasRead(section, key)`, `DefaultText(section, key)`.
- `Snapshot()`, `Save()`, `Changed()`, `OnChanged(hook)`, `Update()`, `SavePending()`,
  `CurrentStatus()` (Saved, Pending, Failed: the footer shows it), `Mutex()`.
- `ParseBool`, `ParseInt`, `ParseNumber` for custom readers.

### `rui/stats.h`

- `HudMode` (Off, Fps, Compact, Full), `HudCorner`, `Mode()`, `Corner()`, their names.
- `AddFrame(frameMs, cpuMs, gpuMs, count)` (negative GPU: not measured),
  `SetCounter(hudLabel, tileLabel)`, `SetTargetFps(fps)` (the FPS's colour: amber under 90%,
  red under half), `ResetLows()`.
- `HudRows()`, `Cards()`: for a page.

### `rui/keyboard.h`

- `Request { title, description, initial, placeholder, maxLength (characters), maxBytes,
  charset, onDone(optional<string>) }`, `Charset` (Any, Ascii).
- `Open(request)`, `Open(title, initial, maxLength, onDone)`: the drawing thread.
  `Post(request)`, `Cancel(id)`: any thread, at the next frame. Each returns or takes an `Id`.
- `IsOpen()`, `KeyCenter(key)` (for tests).

### `rui/gate.h`

- `SetCapture` (`NewFrame` sets it), `SetToggleButtons`, `Captured`, `Releasing`,
  `Holds(buttons, leftTrigger, rightTrigger)`, `StripToggle(buttons)`,
  `FilterXInput(XINPUT_GAMEPAD-shaped pad)`.

### `rui/widgets.h`

- Rows: `Selector`, `Slider`, `Toggle`, `Button`, `TextField` (the on-screen keyboard when
  edited from the pad; a `Charset`), `CustomRow` (a row of your own drawing, such as a list
  entry), each with a `RowSpec { label, description, chip,
  disabledWhy }`. `Chip::Restart(pending)` ("Restart" / amber "Restart required"),
  `Chip::Note("Next level")`. A disabled row says why in the description strip and is
  skipped by up/down. Rows return true when they changed (Button: when pressed).
- `GroupHeader`, `Card(title, right, color, content)`, `KeyValues`.
- `InputMode` (Pad, Keys, Mouse: the last used), `CurrentInput`, `NavCursorShown`,
  `DrawGlyph` (A, B, X, Y, LB, RB, LT, RT, Start, Back, the D-pad's axes), `DrawKeyCap`; `BeginRows`, `FocusedRow`, `CurrentRow`, `DescriptionLines`
  (the panel's own use).

### `rui/style.h`

- Colours (`INK_1` ... `PAD_Y`), icons (`ICON_*`), faces (`Face::Sans`, `SemiBold`, `Mono`).
- Sizes are 1280x720 pixels: `S(px)` scales one, `Stroke(px)` for lines, `Scale()`.
- `Text`, `TextMiddle`, `TextEllipsis`, `TextWrapped`, `WrappedLines`, `TextWidth`, `Icon`,
  `LifeBar`, `Alpha`, `FontScope`.

### `rui/toasts.h`

- `PushToast(kind, icon, title, body, seconds)`: the drawing thread. Bottom right, three at
  most; while the panel is open, the newest shows in its footer.

## Controls

| Pad | Keyboard | Mouse | Does |
|---|---|---|---|
| the toggle chord (Back + Start) | the toggle key (F1) | - | open / close |
| B, Start | Esc | Close in the header | close |
| LB / RB | Shift+Tab / Tab | a sidebar item | previous / next page |
| D-pad / left stick up, down | Up, Down | hover | previous / next row |
| D-pad / left stick left, right | Left, Right | the arrows; drag a slider | change (repeats when held) |
| A | Enter, Space | click | toggle, next option, press, edit (a text field from the pad: the on-screen keyboard, its own controls above) |
| Y | Delete | right click | the row back to its default |

## Self-test

`test/` builds the framework on its own against Dear ImGui, SDL3 and plume source folders,
and runs a self-test that needs no GPU or window (settings file, gate, the panel built and
driven frame by frame without a renderer, the on-screen keyboard driven with made-up pad
states, keys and clicks, a text field through it):

```
cmake -S test -B build -G Ninja -DRUI_TEST_IMGUI_DIR=<imgui> -DRUI_TEST_SDL_DIR=<SDL> -DRUI_TEST_PLUME_DIR=<plume>
ninja -C build rui_selftest && build/rui_selftest build
```

## Licences

recomp-ui's own code is MIT (`LICENSE`).

IBM Plex Sans and Mono (SIL Open Font License 1.1, unmodified) and a 22-icon subset of
Material Symbols Rounded (Apache License 2.0), embedded at build time. `fonts/README.md` has
their sources, how the subset was made, how to use your own icon font, and the licence texts
to ship with a release. Dear ImGui is MIT, SDL3 zlib, plume MIT (the host brings them).
