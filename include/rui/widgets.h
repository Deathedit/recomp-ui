// The panel's controls: full-width rows, each a label and one control
// (selector, slider, toggle, button, text), rows of a page's own drawing,
// read-only cards, and the pad/key glyphs of the hints. A page's draw
// function lays them out top to bottom.
//
// A row is one nav target (an InvisibleButton): up/down move between rows,
// left/right and A change the focused one, Y (or Delete, or a right click)
// resets it. The focused row (pad, keyboard) or the hovered one (mouse) is
// what the description strip and the footer describe (CurrentRow).
#pragma once

#include "style.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace rui
{
    // The last input the player used, for the hints' glyphs.
    enum class InputMode
    {
        Pad,
        Keys,
        Mouse,
    };
    InputMode CurrentInput();
    // Whether the pad/keyboard cursor shows (the mouse hides it).
    bool NavCursorShown();

    // The chip after a row's label.
    struct Chip
    {
        enum class Kind
        {
            None,
            Restart,         // grey "Restart": applies at the next start
            RestartPending,  // amber "Restart required": changed from what's running, not applied yet
            Note,            // grey, its own text: when else it applies ("Next start", "Next level")
        };
        Kind kind = Kind::None;
        std::string_view text;  // a Note's

        static constexpr Chip Restart(bool pending = false) { return { pending ? Kind::RestartPending : Kind::Restart, {} }; }
        static constexpr Chip Note(std::string_view text) { return { Kind::Note, text }; }
    };

    struct RowSpec
    {
        std::string_view label;
        std::string_view description;
        Chip chip = {};
        // Set: the row is disabled (skipped by up/down, no changes), and the
        // description says this instead.
        std::string_view disabledWhy;
    };

    enum class RowKind
    {
        None,
        Selector,
        Slider,
        Toggle,
        Button,
        Text,
        Custom,  // CustomRow: takes the cursor, nothing to change
    };

    // What the focused or hovered row says, for the description strip and
    // the footer's hints.
    struct RowInfo
    {
        RowKind kind = RowKind::None;
        bool disabled = false;
        bool resettable = false;
        std::string title;        // SemiBold, above the text (custom rows)
        std::string text;
        std::string right;        // "Default: On", a custom row's detail
        std::string actionLabel;  // the A hint: "Next", "Toggle", "Press", "Edit"
    };

    // The rows of a section: `focusRow` gets the cursor (-1: none);
    // `descriptionWidth`: the description strip's text width. Before its
    // first row.
    void BeginRows(int focusRow, float descriptionWidth);
    // The lines the longest description of the rows took (2 at least).
    int DescriptionLines();
    // The focused row's index after them (-1: none), and what it says.
    int FocusedRow();
    const RowInfo& CurrentRow();

    // `index` among `count` options; true when it changed.
    bool Selector(const RowSpec& spec, int& index, std::span<const std::string> names, int defaultIndex);
    // `value` from `low` to `high` in steps of `step`, shown with `format`.
    bool Slider(const RowSpec& spec, int& value, int low, int high, int step, const char* format, int defaultValue);
    bool Toggle(const RowSpec& spec, bool& value, bool defaultValue);
    // True when pressed.
    bool Button(const RowSpec& spec, ImWchar icon, std::string_view text);
    // Text typed in place of the value (A, Enter or a click to edit, Enter
    // or leaving the field to keep it); `shown` stands for an empty value.
    bool TextField(const RowSpec& spec, std::string& value, size_t maxLength, std::string_view shown);
    // A row of its own drawing (a list entry): `height` (720p) tall, focus
    // and hover drawn; `draw` gets the row's rectangle and whether it's
    // focused (the ring shows) or hovered. `info` is what the description
    // strip shows for it (kind Custom).
    void CustomRow(int id, float height, const RowInfo& info, const std::function<void(ImDrawList*, ImVec2, ImVec2, bool, bool)>& draw);

    // A group header: its name and a rule to the right edge.
    void GroupHeader(std::string_view name);
    // A read-only card the content's width: a title (and text on its
    // right), then `content`, which draws from its top left at `width` and
    // returns its height (720p pixels times the scale, as drawn).
    void Card(std::string_view title, std::string_view right, ImU32 rightColor, const std::function<float(ImDrawList*, ImVec2, float)>& content);
    // Label / value lines in a card (mono values); returns their height.
    float KeyValues(ImDrawList* draw, ImVec2 pos, float width, std::span<const std::pair<std::string, std::string>> lines);

    // Hint glyphs, drawn from their left edge on `centerY`; each returns
    // its width.
    enum class Glyph
    {
        A,
        B,
        X,
        Y,
        LB,
        RB,
        DpadVertical,
        DpadHorizontal,
    };
    float DrawGlyph(ImDrawList* draw, Glyph glyph, float x, float centerY);
    float DrawKeyCap(ImDrawList* draw, std::string_view key, float x, float centerY);
}
