// The on-screen keyboard: text typed with the pad on a grid of keys, over
// the game's frame or the panel, for players with only a controller (a
// handheld). A text field (rui/widgets.h's TextField) opens it when it's
// edited from the pad; a host opens it itself to answer a game's own
// request for text, with the panel open or closed.
//
// Pad: the D-pad or left stick moves (and repeats), A types the key, B
// deletes before the cursor (on an empty text: cancels), X types a space,
// Y shifts the next letter (twice quickly or held: caps lock), LB / RB move
// the cursor, LT / RT switch the key page, Start is Done, Back cancels. A
// click types a key; a physical keyboard types too (Enter: Done, Esc:
// cancel). While it's open, the input gate keeps the pad from the game, as
// the panel does (rui/gate.h).
//
// The keys: letters (a digit row over QWERTY), symbols, and accented Latin
// letters, all in the embedded IBM Plex Sans; for digits only, a phone's pad.
#pragma once

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace rui::keyboard
{
    // What it may type (keys and a physical keyboard alike).
    enum class Charset
    {
        Any,    // every key: ASCII, the accented letters, a few symbols (€ £ ¥ ° ...)
        Ascii,  // printable ASCII: no accent page, the other symbols greyed
        Digits, // 0-9 on a phone's pad (a number, a PIN): no space, shift or pages
    };

    struct Request
    {
        std::string title;        // over the text: "Name"
        std::string description;  // a line under it; empty: none
        std::string initial;      // the text it starts with (the cursor at its end)
        std::string placeholder;  // greyed while the text is empty
        size_t maxLength = 64;    // characters (code points)
        size_t maxBytes = std::string::npos;  // and UTF-8 bytes, if they're the limit
        Charset charset = Charset::Any;
        // Once: the text on Done, nullopt when cancelled (B on an empty
        // text, Back, Esc, Cancel, or Shutdown). On the drawing thread,
        // inside NewFrame (the ImGui frame, rui::settings::Mutex held). It
        // may open another.
        std::function<void(std::optional<std::string> text)> onDone;
    };

    // A request's number, for Cancel.
    using Id = uint64_t;

    // On the drawing thread (in a frame or between frames): shown from now
    // on, or after the ones before it when one is open. Its input counts
    // from the next frame.
    Id Open(Request request);
    Id Open(std::string title, std::string initial, size_t maxLength, std::function<void(std::optional<std::string> text)> onDone);
    // From any thread: opened at the next NewFrame (kept until there is one).
    Id Post(Request request);
    // From any thread: the request cancelled at the next NewFrame (its
    // onDone gets nullopt), shown or waiting. Unknown or finished: nothing.
    void Cancel(Id id);

    // The drawing thread: one is shown.
    bool IsOpen();

    // ---- testing ----

    // Where a key of the shown page was drawn last frame (its centre, in
    // the window's coordinates), for clicks a test makes up: a key by what
    // it types ("a", "é", "?"; either case), or "Shift", "Page", "Space",
    // "Backspace", "Done".
    std::optional<ImVec2> KeyCenter(std::string_view key);
}
