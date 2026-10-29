# recomp-ui

An in-game overlay for the host runtime of a statically recompiled game: a settings panel
the player opens over the game's frame, a settings file behind it, a performance HUD,
toasts, and an input gate that keeps the game from seeing the pad while the panel is open.
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
| Drawing thread | `Init`, `NewFrame`, `RecordUploads`, `Draw`, `DrawInto`, `Shutdown`, `rui::stats::*`, `PushToast`, `ShowPage`, `SetVisible`, and everything in pages and hooks (widgets, style). The ImGui context is only used here. The settings' variables belong to this thread from `Init` on, inside `rui::settings::Mutex()` (`NewFrame` holds it while pages and hooks run). |
| Event thread | `QueueEvent`, `WantsTextInput` |
| Any | `PostToast`, `rui::gate::*`, `rui::settings::SavePending` |

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

### `rui/gate.h`

- `SetCapture` (`NewFrame` sets it), `SetToggleButtons`, `Captured`, `Releasing`,
  `Holds(buttons, leftTrigger, rightTrigger)`, `StripToggle(buttons)`,
  `FilterXInput(XINPUT_GAMEPAD-shaped pad)`.

### `rui/widgets.h`

- Rows: `Selector`, `Slider`, `Toggle`, `Button`, `TextField`, `CustomRow` (a row of your
  own drawing, such as a list entry), each with a `RowSpec { label, description, chip,
  disabledWhy }`. `Chip::Restart(pending)` ("Restart" / amber "Restart required"),
  `Chip::Note("Next level")`. A disabled row says why in the description strip and is
  skipped by up/down. Rows return true when they changed (Button: when pressed).
- `GroupHeader`, `Card(title, right, color, content)`, `KeyValues`.
- `InputMode` (Pad, Keys, Mouse: the last used), `CurrentInput`, `NavCursorShown`,
  `DrawGlyph`, `DrawKeyCap`; `BeginRows`, `FocusedRow`, `CurrentRow`, `DescriptionLines`
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
| A | Enter, Space | click | toggle, next option, press, edit |
| Y | Delete | right click | the row back to its default |

## Self-test

`test/` builds the framework on its own against Dear ImGui, SDL3 and plume source folders,
and runs a self-test that needs no GPU or window (settings file, gate, the panel built and
driven frame by frame without a renderer):

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
