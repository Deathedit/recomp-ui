// The on-screen keyboard (rui/keyboard.h): a box in the middle of the
// window over a scrim, with the title and the count, the text and its
// cursor, the key pages' tabs, the grid of keys and a footer of hints. It
// scales from its 1280x720 layout like the panel, and draws in a window of
// its own over everything (the panel too) that takes the focus and the
// mouse while it's open; the panel's input waits (panel.cpp).
#include "internal.h"

#include <rui/keyboard.h>
#include <rui/style.h>
#include <rui/widgets.h>

#include <imgui_internal.h>  // ImTextCharFromUtf8, ImTextCharToUtf8

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

namespace rui::keyboard
{
    namespace
    {
        // The box at 720p (its width is the board's).
        constexpr float BOX_ROUNDING = 14.0f, PAD = 22.0f;
        constexpr float FIELD_HEIGHT = 52.0f, TABS_HEIGHT = 26.0f, FOOTER_HEIGHT = 46.0f;
        constexpr float KEY_HEIGHT = 50.0f, KEY_GAP = 6.0f, KEY_ROUNDING = 8.0f;
        constexpr float KEY_TEXT = 19.0f, FIELD_TEXT = 20.0f;
        constexpr float FADE_SECONDS = 0.12f, FLASH_SECONDS = 0.12f, FULL_SECONDS = 0.6f;
        constexpr double DOUBLE_TAP = 0.35, CAPS_HOLD = 0.5;

        // A page's grid (its board's rows and columns, four by ten at most),
        // then the board's bottom row, the same on each of its pages.
        constexpr int COLUMNS = 10, GRID_ROWS = 4, ROWS = GRID_ROWS + 1;
        struct Page
        {
            const char* name;  // its tab
            char32_t lower[GRID_ROWS][COLUMNS];
            char32_t upper[GRID_ROWS][COLUMNS];  // shifted; 0: the same
        };
        constexpr Page PAGES[] = {
            { "ABC",
                {
                    { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0' },
                    { 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p' },
                    { 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '\'' },
                    { 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '-' },
                },
                {
                    {},
                    { 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P' },
                    { 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0 },
                    { 'Z', 'X', 'C', 'V', 'B', 'N', 'M', 0, 0, 0 },
                } },
            { "#+=",
                {
                    { '!', '@', '#', '$', '%', '^', '&', '*', '(', ')' },
                    { '-', '_', '=', '+', '[', ']', '{', '}', '\\', '|' },
                    { ';', ':', '\'', '"', ',', '.', '/', '?', '<', '>' },
                    // ` ~ € £ ¥ ¢ ° § ¡ ¿
                    { '`', '~', 0x20AC, 0xA3, 0xA5, 0xA2, 0xB0, 0xA7, 0xA1, 0xBF },
                },
                {} },
            { "\xC3\x80\xC3\x89",  // ÀÉ
                {
                    // à á â ä ã å æ ç è é
                    { 0xE0, 0xE1, 0xE2, 0xE4, 0xE3, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9 },
                    // ê ë ì í î ï ñ ò ó ô
                    { 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF1, 0xF2, 0xF3, 0xF4 },
                    // ö õ ø œ ù ú û ü ý ÿ
                    { 0xF6, 0xF5, 0xF8, 0x153, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFF },
                    // ß ð þ ł č ę ń ś š ž
                    { 0xDF, 0xF0, 0xFE, 0x142, 0x10D, 0x119, 0x144, 0x15B, 0x161, 0x17E },
                },
                {
                    { 0xC0, 0xC1, 0xC2, 0xC4, 0xC3, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9 },
                    { 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD1, 0xD2, 0xD3, 0xD4 },
                    { 0xD6, 0xD5, 0xD8, 0x152, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0x178 },
                    { 0x1E9E, 0xD0, 0xDE, 0x141, 0x10C, 0x118, 0x143, 0x15A, 0x160, 0x17D },
                } },
            // Digits: a phone's pad (its 0 is in the bottom row).
            { "123",
                {
                    { '1', '2', '3' },
                    { '4', '5', '6' },
                    { '7', '8', '9' },
                },
                {} },
        };

        enum class Special
        {
            Char,  // types its character
            Shift,
            Page,  // the next page
            Space,
            Backspace,
            Done,
        };
        struct BottomKey
        {
            Special special;
            float units;  // of a grid key's width
            const char* name;  // KeyCenter's
            char32_t c = 0;    // a Char's
        };
        constexpr BottomKey TEXT_BOTTOM[] = {
            { Special::Shift, 1.5f, "Shift" },
            { Special::Page, 1.5f, "Page" },
            { Special::Space, 4.0f, "Space" },
            { Special::Backspace, 1.5f, "Backspace" },
            { Special::Done, 1.5f, "Done" },
        };
        constexpr BottomKey DIGITS_BOTTOM[] = {
            { Special::Backspace, 1.0f, "Backspace" },
            { Special::Char, 1.0f, "0", '0' },
            { Special::Done, 1.0f, "Done" },
        };

        // A charset's keys: its pages (into PAGES), their grid, the bottom
        // row, the box's width at 720p and the key focused first.
        struct Board
        {
            std::span<const int> pages;
            int rows, columns;
            std::span<const BottomKey> bottom;
            float width;
            int startRow, startCol;
        };
        constexpr int ALL_PAGES[] = { 0, 1, 2 }, ASCII_PAGES[] = { 0, 1 }, DIGIT_PAGES[] = { 3 };
        constexpr Board TEXT_BOARD = { ALL_PAGES, GRID_ROWS, COLUMNS, TEXT_BOTTOM, 720.0f, 1, 0 };  // from q
        constexpr Board ASCII_BOARD = { ASCII_PAGES, GRID_ROWS, COLUMNS, TEXT_BOTTOM, 720.0f, 1, 0 };
        constexpr Board DIGITS_BOARD = { DIGIT_PAGES, 3, 3, DIGITS_BOTTOM, 480.0f, 0, 0 };

        struct Entry
        {
            Id id = 0;
            Request request;
        };

        std::atomic<Id> s_nextId{ 1 };
        std::mutex s_postMutex;
        std::vector<Entry> s_posted;  // Post's, until the next frame
        std::vector<Id> s_cancels;    // Cancel's

        // The drawing thread's.
        std::deque<Entry> s_waiting;
        std::optional<Entry> s_current;
        std::u32string s_text;
        size_t s_cursor = 0;
        int s_page = 0;  // into Pages()
        int s_row = 1, s_col = 0;
        float s_column = 0.5f;  // the focus's centre in key units, kept going up and down
        bool s_shift = false, s_caps = false;
        double s_shiftPressed = -1.0;  // ImGui::GetTime() of the last shift press
        bool s_shiftHeld = false;      // Y held since it turned shift on
        double s_cursorMoved = 0.0;    // the caret shows solid for a moment after
        float s_scroll = 0.0f;         // the text's, to keep the caret in view
        int s_frames = 0;              // shown for; input counts from the second
        float s_age = 0.0f;
        int s_flashRow = -1, s_flashCol = -1;
        float s_flash = 0.0f, s_full = 0.0f;  // a key typed, a key refused (full)
        std::vector<std::pair<std::string, ImVec2>> s_keyCenters;  // the last frame's, for KeyCenter

        // ---- text ----

        void AppendUtf8(std::string& out, char32_t c)
        {
            char buffer[5] = {};
            ImTextCharToUtf8(buffer, unsigned(c));
            out += buffer;
        }

        std::string Utf8(std::u32string_view text)
        {
            std::string out;
            for (const char32_t c : text)
                AppendUtf8(out, c);
            return out;
        }

        std::u32string Decode(std::string_view text)
        {
            std::u32string out;
            const char* end = text.data() + text.size();
            for (const char* p = text.data(); p < end;)
            {
                unsigned int c = 0;
                p += std::max(ImTextCharFromUtf8(&c, p, end), 1);
                out += char32_t(c);
            }
            return out;
        }

        size_t Utf8Size(char32_t c)
        {
            return c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        }

        size_t Bytes()
        {
            size_t bytes = 0;
            for (const char32_t c : s_text)
                bytes += Utf8Size(c);
            return bytes;
        }

        // What the request's charset and the embedded fonts allow.
        bool Allowed(char32_t c)
        {
            if (c < 0x20 || c == 0x7F)
                return false;
            if (s_current->request.charset == Charset::Digits)
                return c >= '0' && c <= '9';
            if (c < 0x7F)
                return true;
            if (s_current->request.charset == Charset::Ascii || c < 0xA0 || c > 0xFFFF || (c >= 0xE000 && c <= 0xF8FF))
                return false;  // C1 controls, the icons' private use area
            return Font(Face::Sans)->IsGlyphInFont(ImWchar(c));
        }

        // ---- the keys ----

        const Board& CurrentBoard()
        {
            switch (s_current ? s_current->request.charset : Charset::Any)
            {
            case Charset::Ascii:
                return ASCII_BOARD;
            case Charset::Digits:
                return DIGITS_BOARD;
            default:
                return TEXT_BOARD;
            }
        }

        std::span<const int> Pages()
        {
            return CurrentBoard().pages;
        }

        const Page& CurrentPage()
        {
            return PAGES[Pages()[size_t(s_page)]];
        }

        // The grid's rows and the bottom one.
        int Rows()
        {
            return CurrentBoard().rows + 1;
        }

        bool InGrid(int row)
        {
            return row < CurrentBoard().rows;
        }

        int RowKeys(int row)
        {
            return InGrid(row) ? CurrentBoard().columns : int(CurrentBoard().bottom.size());
        }

        // The board has the key (Shift, Space: Y and X do nothing without it).
        bool HasKey(Special special)
        {
            for (const BottomKey& key : CurrentBoard().bottom)
            {
                if (key.special == special)
                    return true;
            }
            return false;
        }

        // A key's left edge and width, in key units.
        float KeyLeft(int row, int col)
        {
            if (InGrid(row))
                return float(col);
            float left = 0.0f;
            for (int i = 0; i < col; i++)
                left += CurrentBoard().bottom[size_t(i)].units;
            return left;
        }

        float KeyUnits(int row, int col)
        {
            return InGrid(row) ? 1.0f : CurrentBoard().bottom[size_t(col)].units;
        }

        int KeyAt(int row, float column)
        {
            for (int col = 0; col < RowKeys(row); col++)
            {
                if (column < KeyLeft(row, col) + KeyUnits(row, col))
                    return col;
            }
            return RowKeys(row) - 1;
        }

        Special SpecialOf(int row, int col)
        {
            return InGrid(row) ? Special::Char : CurrentBoard().bottom[size_t(col)].special;
        }

        // A Char key's character (of the current page).
        char32_t KeyChar(int row, int col, bool shifted)
        {
            if (!InGrid(row))
                return CurrentBoard().bottom[size_t(col)].c;
            const Page& page = CurrentPage();
            const char32_t upper = page.upper[row][col];
            return shifted && upper != 0 ? upper : page.lower[row][col];
        }

        char32_t KeyChar(int row, int col)
        {
            return KeyChar(row, col, s_shift || s_caps);
        }

        // ---- editing ----

        void Moved()
        {
            s_cursorMoved = ImGui::GetTime();
        }

        bool Insert(char32_t c)
        {
            if (!Allowed(c))
                return false;
            const Request& request = s_current->request;
            if (s_text.size() >= request.maxLength || (request.maxBytes != std::string::npos && Bytes() + Utf8Size(c) > request.maxBytes))
            {
                s_full = FULL_SECONDS;
                return false;
            }
            s_text.insert(s_cursor, 1, c);
            s_cursor++;
            Moved();
            return true;
        }

        void Backspace()
        {
            if (s_cursor == 0)
                return;
            s_text.erase(s_cursor - 1, 1);
            s_cursor--;
            Moved();
        }

        void MoveCursor(int step)
        {
            s_cursor = size_t(std::clamp(int(s_cursor) + step, 0, int(s_text.size())));
            Moved();
        }

        // A tap: shift on or off; twice quickly: caps lock; with caps lock
        // on: both off.
        void PressShift()
        {
            const double now = ImGui::GetTime();
            if (s_caps)
                s_caps = s_shift = false;
            else if (s_shift && now - s_shiftPressed < DOUBLE_TAP)
                s_caps = true;
            else
                s_shift = !s_shift;
            s_shiftPressed = now;
        }

        void SwitchPage(int step)
        {
            const int count = int(Pages().size());
            s_page = (s_page + step + count) % count;
        }

        void Flash(int row, int col)
        {
            s_flashRow = row;
            s_flashCol = col;
            s_flash = FLASH_SECONDS;
        }

        void ShowNext();

        // Closes the shown one and answers it (after the state is reset, so
        // onDone may open another).
        void Finish(std::optional<std::string> text)
        {
            Entry entry = std::move(*s_current);
            s_current.reset();
            detail::Log(LogKind::Event, text ? std::format("keyboard done ({} characters)", Decode(*text).size()) : std::string("keyboard cancelled"));
            if (entry.request.onDone)
                entry.request.onDone(std::move(text));
            ShowNext();
        }

        void ShowNext()
        {
            if (s_current || s_waiting.empty())
                return;
            s_current = std::move(s_waiting.front());
            s_waiting.pop_front();
            s_text = Decode(s_current->request.initial);
            s_cursor = s_text.size();
            s_page = 0;
            s_row = CurrentBoard().startRow;
            s_col = CurrentBoard().startCol;
            s_column = KeyLeft(s_row, s_col) + KeyUnits(s_row, s_col) * 0.5f;
            s_shift = s_caps = s_shiftHeld = false;
            s_shiftPressed = -1.0;
            s_scroll = 0.0f;
            s_frames = 0;
            s_age = 0.0f;
            s_flash = s_full = 0.0f;
            s_keyCenters.clear();
            Moved();
            detail::Log(LogKind::Event, std::format("keyboard open: {}", s_current->request.title));
        }

        void CancelId(Id id)
        {
            if (s_current && s_current->id == id)
            {
                Finish(std::nullopt);
                return;
            }
            for (auto it = s_waiting.begin(); it != s_waiting.end(); ++it)
            {
                if (it->id == id)
                {
                    Entry entry = std::move(*it);
                    s_waiting.erase(it);
                    if (entry.request.onDone)
                        entry.request.onDone(std::nullopt);
                    return;
                }
            }
        }

        // Activates the key at `row`, `col` (A, a click); false when that
        // closed the keyboard.
        bool TypeKey(int row, int col)
        {
            switch (SpecialOf(row, col))
            {
            case Special::Char:
                if (Insert(KeyChar(row, col)))
                {
                    Flash(row, col);
                    if (s_shift && !s_caps)
                        s_shift = false;  // for one letter
                }
                return true;
            case Special::Shift:
                PressShift();
                return true;
            case Special::Page:
                SwitchPage(1);
                return true;
            case Special::Space:
                if (Insert(' '))
                    Flash(row, col);
                return true;
            case Special::Backspace:
                Backspace();
                Flash(row, col);
                return true;
            case Special::Done:
                Finish(Utf8(s_text));
                return false;
            }
            return true;
        }

        // ---- layout ----

        struct Layout
        {
            ImVec2 min, max;  // the box
            float titleY = 0.0f, descriptionY = 0.0f;
            ImVec2 fieldMin, fieldMax;
            float tabsY = 0.0f;  // the tabs' centre
            std::vector<std::pair<ImVec2, ImVec2>> tabs;
            ImVec2 keys[ROWS][COLUMNS][2];
            float footerTop = 0.0f;
        };

        float TabWidth(const char* name)
        {
            return TextWidth(Face::SemiBold, 13.5f, name) + S(24.0f);
        }

        Layout Arrange()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const bool description = !s_current->request.description.empty();
            const bool tabs = Pages().size() > 1;  // one page: no tabs
            const float gridHeight = float(Rows()) * S(KEY_HEIGHT) + float(Rows() - 1) * S(KEY_GAP);
            const float height = S(PAD) + S(20.0f) + (description ? S(4.0f + 18.0f) : 0.0f) + S(12.0f) + S(FIELD_HEIGHT) + S(12.0f) +
                (tabs ? S(TABS_HEIGHT) + S(12.0f) : 0.0f) + gridHeight + S(PAD) + S(FOOTER_HEIGHT);
            const float width = S(CurrentBoard().width);
            Layout layout;
            layout.min = ImVec2(std::round(viewport->Pos.x + (viewport->Size.x - width) * 0.5f), std::round(viewport->Pos.y + (viewport->Size.y - height) * 0.5f));
            layout.max = ImVec2(layout.min.x + width, layout.min.y + height);
            const float left = layout.min.x + S(PAD), right = layout.max.x - S(PAD);
            float y = layout.min.y + S(PAD);
            layout.titleY = y + S(10.0f);
            y += S(20.0f);
            if (description)
            {
                layout.descriptionY = y + S(4.0f);
                y += S(4.0f + 18.0f);
            }
            y += S(12.0f);
            layout.fieldMin = ImVec2(left, y);
            layout.fieldMax = ImVec2(right, y + S(FIELD_HEIGHT));
            y += S(FIELD_HEIGHT) + S(12.0f);

            // The tabs, centred.
            if (tabs)
            {
                layout.tabsY = y + S(TABS_HEIGHT * 0.5f);
                float tabsWidth = 0.0f;
                for (const int page : Pages())
                    tabsWidth += TabWidth(PAGES[page].name) + S(6.0f);
                float x = std::round((left + right - (tabsWidth - S(6.0f))) * 0.5f);
                for (const int page : Pages())
                {
                    const float tab = TabWidth(PAGES[page].name);
                    layout.tabs.emplace_back(ImVec2(x, y), ImVec2(x + tab, y + S(TABS_HEIGHT)));
                    x += tab + S(6.0f);
                }
                y += S(TABS_HEIGHT) + S(12.0f);
            }

            // The keys: a unit is a grid key's width and the gap after it.
            const float gap = S(KEY_GAP);
            const float unit = (right - left + gap) / float(CurrentBoard().columns);
            for (int row = 0; row < Rows(); row++)
            {
                const float top = y + float(row) * (S(KEY_HEIGHT) + gap);
                for (int col = 0; col < RowKeys(row); col++)
                {
                    const float keyLeft = std::round(left + KeyLeft(row, col) * unit);
                    const float keyRight = std::round(left + (KeyLeft(row, col) + KeyUnits(row, col)) * unit - gap);
                    layout.keys[row][col][0] = ImVec2(keyLeft, top);
                    layout.keys[row][col][1] = ImVec2(keyRight, top + S(KEY_HEIGHT));
                }
            }
            layout.footerTop = layout.max.y - S(FOOTER_HEIGHT);
            return layout;
        }

        void RecordCenters(const Layout& layout)
        {
            s_keyCenters.clear();
            for (int row = 0; row < Rows(); row++)
            {
                for (int col = 0; col < RowKeys(row); col++)
                {
                    const ImVec2 center((layout.keys[row][col][0].x + layout.keys[row][col][1].x) * 0.5f,
                        (layout.keys[row][col][0].y + layout.keys[row][col][1].y) * 0.5f);
                    if (InGrid(row))
                    {
                        std::string lower, upper;
                        AppendUtf8(lower, KeyChar(row, col, false));
                        AppendUtf8(upper, KeyChar(row, col, true));
                        s_keyCenters.emplace_back(lower, center);
                        if (upper != lower)
                            s_keyCenters.emplace_back(upper, center);
                    }
                    else
                        s_keyCenters.emplace_back(CurrentBoard().bottom[size_t(col)].name, center);
                }
            }
        }

        // ---- input ----

        bool Pressed(ImGuiKey dpad, ImGuiKey stick)
        {
            return ImGui::IsKeyPressed(dpad, true) || ImGui::IsKeyPressed(stick, true);
        }

        void MoveFocus(int rowStep, int colStep)
        {
            if (rowStep != 0)
            {
                s_row = (s_row + rowStep + Rows()) % Rows();
                s_col = KeyAt(s_row, s_column);
            }
            if (colStep != 0)
            {
                s_col = (s_col + colStep + RowKeys(s_row)) % RowKeys(s_row);
                s_column = KeyLeft(s_row, s_col) + KeyUnits(s_row, s_col) * 0.5f;
            }
        }

        // The caret's place for a click at `x` in the field.
        size_t CursorAt(const Layout& layout, float x)
        {
            const float textLeft = layout.fieldMin.x + S(14.0f) - s_scroll;
            size_t best = 0;
            float bestDistance = std::abs(x - textLeft);
            for (size_t i = 1; i <= s_text.size(); i++)
            {
                const float distance = std::abs(x - (textLeft + TextWidth(Face::Sans, FIELD_TEXT, Utf8(std::u32string_view(s_text).substr(0, i)))));
                if (distance < bestDistance)
                {
                    best = i;
                    bestDistance = distance;
                }
            }
            return best;
        }

        // This frame's pad, keys and clicks; false when they closed it.
        bool HandleInput(const Layout& layout)
        {
            // Back, Escape: cancel. Start, Enter: done.
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                Finish(std::nullopt);
                return false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
            {
                Finish(Utf8(s_text));
                return false;
            }

            // The focus, round at the edges.
            if (Pressed(ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp))
                MoveFocus(-1, 0);
            else if (Pressed(ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown))
                MoveFocus(1, 0);
            if (Pressed(ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft))
                MoveFocus(0, -1);
            else if (Pressed(ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight))
                MoveFocus(0, 1);

            // Y: shift; held, caps lock.
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) && HasKey(Special::Shift))
            {
                PressShift();
                s_shiftHeld = s_shift && !s_caps;
            }
            if (!ImGui::IsKeyDown(ImGuiKey_GamepadFaceUp))
                s_shiftHeld = false;
            else if (s_shiftHeld && ImGui::GetTime() - s_shiftPressed >= CAPS_HOLD)
            {
                s_caps = true;
                s_shiftHeld = false;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL2, false))
                SwitchPage(-1);
            else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR2, false))
                SwitchPage(1);
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, true) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
                MoveCursor(-1);
            else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, true) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
                MoveCursor(1);
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false))
                MoveCursor(-int(s_text.size()));
            else if (ImGui::IsKeyPressed(ImGuiKey_End, false))
                MoveCursor(int(s_text.size()));

            // A: the focused key (Backspace repeats while held).
            const bool repeats = SpecialOf(s_row, s_col) == Special::Backspace;
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, repeats) && !TypeKey(s_row, s_col))
                return false;

            // B: delete; on an empty text, a fresh press cancels (holding it
            // down to empty the text doesn't).
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, true))
            {
                if (!s_text.empty())
                    Backspace();
                else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
                {
                    Finish(std::nullopt);
                    return false;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) && HasKey(Special::Space))
                Insert(' ');

            // A physical keyboard.
            ImGuiIO& io = ImGui::GetIO();
            for (const ImWchar c : io.InputQueueCharacters)
                Insert(char32_t(c));
            if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true))
                Backspace();
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, true) && s_cursor < s_text.size())
            {
                s_text.erase(s_cursor, 1);
                Moved();
            }

            // The mouse: keys, tabs, the caret.
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                for (int row = 0; row < Rows(); row++)
                {
                    for (int col = 0; col < RowKeys(row); col++)
                    {
                        if (!ImGui::IsMouseHoveringRect(layout.keys[row][col][0], layout.keys[row][col][1]))
                            continue;
                        s_row = row;
                        s_col = col;
                        s_column = KeyLeft(row, col) + KeyUnits(row, col) * 0.5f;
                        if (!TypeKey(row, col))
                            return false;
                    }
                }
                for (size_t i = 0; i < layout.tabs.size(); i++)
                {
                    if (ImGui::IsMouseHoveringRect(layout.tabs[i].first, layout.tabs[i].second))
                        s_page = int(i);
                }
                if (ImGui::IsMouseHoveringRect(layout.fieldMin, layout.fieldMax))
                {
                    s_cursor = CursorAt(layout, io.MousePos.x);
                    Moved();
                }
            }
            return true;
        }

        // ---- drawing ----

        // Shift: an arrow, filled while on, a bar under it for caps lock.
        void DrawShift(ImDrawList* draw, ImVec2 center, ImU32 color)
        {
            const float s = Scale();
            static constexpr ImVec2 ARROW[] = { { 0.0f, -9.0f }, { 8.5f, -0.5f }, { 4.0f, -0.5f }, { 4.0f, 6.0f }, { -4.0f, 6.0f }, { -4.0f, -0.5f },
                { -8.5f, -0.5f } };
            const float lift = s_caps ? -2.0f : 0.0f;
            ImVec2 points[std::size(ARROW)];
            for (size_t i = 0; i < std::size(ARROW); i++)
                points[i] = ImVec2(center.x + ARROW[i].x * s, center.y + (ARROW[i].y + lift) * s);
            if (s_shift || s_caps)
                draw->AddConcavePolyFilled(points, int(std::size(points)), color);
            else
                draw->AddPolyline(points, int(std::size(points)), color, ImDrawFlags_Closed, Stroke(1.6f));
            if (s_caps)
                draw->AddRectFilled(ImVec2(center.x - 4.0f * s, center.y + 8.0f * s), ImVec2(center.x + 4.0f * s, center.y + 10.0f * s), color);
        }

        // Backspace: a key pointing left with a cross in it.
        void DrawBackspace(ImDrawList* draw, ImVec2 center, ImU32 color)
        {
            const float s = Scale();
            static constexpr ImVec2 SHAPE[] = { { -11.0f, 0.0f }, { -5.0f, -7.5f }, { 10.0f, -7.5f }, { 10.0f, 7.5f }, { -5.0f, 7.5f } };
            ImVec2 points[std::size(SHAPE)];
            for (size_t i = 0; i < std::size(SHAPE); i++)
                points[i] = ImVec2(center.x + SHAPE[i].x * s, center.y + SHAPE[i].y * s);
            draw->AddPolyline(points, int(std::size(points)), color, ImDrawFlags_Closed, Stroke(1.6f));
            const ImVec2 middle(center.x + 2.5f * s, center.y);
            const float arm = 3.5f * s;
            draw->AddLine(ImVec2(middle.x - arm, middle.y - arm), ImVec2(middle.x + arm, middle.y + arm), color, Stroke(1.6f));
            draw->AddLine(ImVec2(middle.x - arm, middle.y + arm), ImVec2(middle.x + arm, middle.y - arm), color, Stroke(1.6f));
        }

        void DrawKey(ImDrawList* draw, const Layout& layout, int row, int col)
        {
            const ImVec2 min = layout.keys[row][col][0], max = layout.keys[row][col][1];
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const Special special = SpecialOf(row, col);
            const char32_t c = special == Special::Char ? KeyChar(row, col) : 0;
            const bool disabled = special == Special::Char && !Allowed(c);
            const bool focused = row == s_row && col == s_col && CurrentInput() != InputMode::Mouse;
            const bool hovered = CurrentInput() == InputMode::Mouse && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max);
            const bool flashing = s_flash > 0.0f && row == s_flashRow && col == s_flashCol;
            const float rounding = S(KEY_ROUNDING);

            ImU32 fill = special == Special::Char ? BUTTON : special == Special::Done ? IM_COL32(29, 56, 84, 255) : IM_COL32(27, 35, 46, 255);
            ImU32 ink = special == Special::Done ? ACCENT : special == Special::Char ? TEXT : TEXT_2;
            if (disabled)
            {
                fill = IM_COL32(24, 31, 40, 255);
                ink = TEXT_3;
            }
            else if (flashing)
            {
                fill = BUTTON_ACTIVE;
                ink = ACCENT_INK;
            }
            else if (focused)
            {
                fill = ACCENT;
                ink = ACCENT_INK;
            }
            else if (hovered)
            {
                fill = BUTTON_HOVERED;
                ink = special == Special::Done ? ACCENT : TEXT;
            }
            if (special == Special::Shift && (s_shift || s_caps) && !focused && !flashing)
                ink = ACCENT;
            draw->AddRectFilled(min, max, fill, rounding);
            if (focused)
            {
                const float halo = S(3.0f);
                draw->AddRect(ImVec2(min.x - halo, min.y - halo), ImVec2(max.x + halo, max.y + halo), ACCENT_HALO, rounding + halo, 0, Stroke(4.0f));
            }

            switch (special)
            {
            case Special::Char:
            {
                std::string label;
                AppendUtf8(label, c);
                TextMiddle(draw, Face::Sans, KEY_TEXT, std::round(center.x - TextWidth(Face::Sans, KEY_TEXT, label) * 0.5f), center.y, ink, label);
                break;
            }
            case Special::Shift:
                DrawShift(draw, center, ink);
                break;
            case Special::Page:
            {
                const char* next = PAGES[Pages()[size_t((s_page + 1) % int(Pages().size()))]].name;
                TextMiddle(draw, Face::SemiBold, 15.0f, std::round(center.x - TextWidth(Face::SemiBold, 15.0f, next) * 0.5f), center.y, ink, next);
                break;
            }
            case Special::Space:
                TextMiddle(draw, Face::Sans, 14.0f, std::round(center.x - TextWidth(Face::Sans, 14.0f, "Space") * 0.5f), center.y, ink, "Space");
                break;
            case Special::Backspace:
                DrawBackspace(draw, center, ink);
                break;
            case Special::Done:
            {
                const float width = S(18.0f) + S(6.0f) + TextWidth(Face::SemiBold, 15.0f, "Done");
                const float x = std::round(center.x - width * 0.5f);
                Icon(draw, ICON_CHECK_CIRCLE, 18.0f, ImVec2(x + S(9.0f), center.y), ink);
                TextMiddle(draw, Face::SemiBold, 15.0f, x + S(18.0f + 6.0f), center.y, ink, "Done");
                break;
            }
            }
        }

        void DrawField(ImDrawList* draw, const Layout& layout)
        {
            const Request& request = s_current->request;
            draw->AddRectFilled(layout.fieldMin, layout.fieldMax, INK_1, S(9.0f));
            draw->AddRect(layout.fieldMin, layout.fieldMax, Alpha(ACCENT, 0.55f), S(9.0f), 0, Stroke(1.5f));
            const float centerY = std::round((layout.fieldMin.y + layout.fieldMax.y) * 0.5f);
            const float left = layout.fieldMin.x + S(14.0f), right = layout.fieldMax.x - S(14.0f);
            const std::string text = Utf8(s_text);
            const float caretX = TextWidth(Face::Sans, FIELD_TEXT, Utf8(std::u32string_view(s_text).substr(0, s_cursor)));
            // Scrolled to keep the caret in.
            const float room = right - left - S(2.0f);
            if (caretX - s_scroll > room)
                s_scroll = caretX - room;
            if (caretX < s_scroll)
                s_scroll = caretX;
            s_scroll = std::clamp(s_scroll, 0.0f, std::max(TextWidth(Face::Sans, FIELD_TEXT, text) - room, 0.0f));

            draw->PushClipRect(ImVec2(left - S(2.0f), layout.fieldMin.y), ImVec2(right + S(2.0f), layout.fieldMax.y), true);
            if (text.empty() && !request.placeholder.empty())
                TextMiddle(draw, Face::Sans, FIELD_TEXT, left, centerY, TEXT_3, request.placeholder);
            else
                TextMiddle(draw, Face::Sans, FIELD_TEXT, left - s_scroll, centerY, TEXT, text);
            // The caret: solid for a moment after it moves, then blinking.
            const double since = ImGui::GetTime() - s_cursorMoved;
            if (since < 0.5 || std::fmod(since - 0.5, 1.0) < 0.55)
            {
                const float x = std::round(left - s_scroll + caretX);
                draw->AddRectFilled(ImVec2(x, centerY - S(13.0f)), ImVec2(x + Stroke(2.0f), centerY + S(13.0f)), ACCENT);
            }
            draw->PopClipRect();
        }

        void DrawTabs(ImDrawList* draw, const Layout& layout)
        {
            const std::span<const int> pages = Pages();
            const bool pad = CurrentInput() == InputMode::Pad;
            for (size_t i = 0; i < layout.tabs.size(); i++)
            {
                const auto& [min, max] = layout.tabs[i];
                const bool current = int(i) == s_page;
                const bool hovered = CurrentInput() == InputMode::Mouse && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max);
                if (current)
                    draw->AddRectFilled(min, max, Alpha(ACCENT, 33.0f / 255.0f), S(7.0f));
                else if (hovered)
                    draw->AddRectFilled(min, max, INK_3, S(7.0f));
                const char* name = PAGES[pages[i]].name;
                TextMiddle(draw, Face::SemiBold, 13.5f, std::round((min.x + max.x - TextWidth(Face::SemiBold, 13.5f, name)) * 0.5f), layout.tabsY,
                    current ? ACCENT : hovered ? TEXT : TEXT_2, name);
            }
            if (pad && !layout.tabs.empty())
            {
                DrawGlyph(draw, Glyph::LT, layout.tabs.front().first.x - S(10.0f + 30.0f), layout.tabsY);
                DrawGlyph(draw, Glyph::RT, layout.tabs.back().second.x + S(10.0f), layout.tabsY);
            }
        }

        void DrawFooter(ImDrawList* draw, const Layout& layout)
        {
            const ImVec2 min(layout.min.x, layout.footerTop);
            draw->AddRectFilled(min, layout.max, INK_1, S(BOX_ROUNDING) - 1.0f, ImDrawFlags_RoundCornersBottom);
            draw->AddRectFilled(min, ImVec2(layout.max.x, min.y + Stroke(1.0f)), LINE);
            const float centerY = (min.y + layout.max.y) * 0.5f;
            float x = min.x + S(18.0f);
            const float gap = S(16.0f);
            switch (CurrentInput())
            {
            case InputMode::Pad:
                x += detail::Hint(draw, x, centerY, Glyph::A, "", "Type") + gap;
                x += detail::Hint(draw, x, centerY, Glyph::B, "", s_text.empty() ? "Cancel" : "Delete") + gap;
                if (HasKey(Special::Space))
                    x += detail::Hint(draw, x, centerY, Glyph::X, "", "Space") + gap;
                if (HasKey(Special::Shift))
                    x += detail::Hint(draw, x, centerY, Glyph::Y, "", s_caps ? "Caps off" : "Shift") + gap;
                x += detail::Hint(draw, x, centerY, Glyph::LB, "", "Cursor") + gap;
                x += detail::Hint(draw, x, centerY, Glyph::Start, "", "Done") + gap;
                x += detail::Hint(draw, x, centerY, Glyph::Back, "", "Cancel") + gap;
                break;
            case InputMode::Keys:
                x += detail::Hint(draw, x, centerY, std::nullopt, "Enter", "Done") + gap;
                x += detail::Hint(draw, x, centerY, std::nullopt, "Esc", "Cancel") + gap;
                x += detail::Hint(draw, x, centerY, std::nullopt, "\xE2\x86\x90\xE2\x86\x92", "Cursor") + gap;
                break;
            case InputMode::Mouse:
                x += detail::Hint(draw, x, centerY, std::nullopt, "Click", "Type") + gap;
                x += detail::Hint(draw, x, centerY, std::nullopt, "Enter", "Done") + gap;
                x += detail::Hint(draw, x, centerY, std::nullopt, "Esc", "Cancel") + gap;
                break;
            }
        }

        void Draw(const Layout& layout, bool panelOpen)
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const int first = draw->VtxBuffer.Size;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            // Over the panel a scrim of its own: the panel behind recedes.
            draw->AddRectFilled(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), panelOpen ? IM_COL32(7, 9, 12, 170) : SCRIM);
            for (int ring = 0; ring < 3; ring++)
            {
                const float out = S(2.0f + 4.0f * float(ring));
                static constexpr int ALPHAS[] = { 40, 26, 13 };
                draw->AddRect(ImVec2(layout.min.x - out, layout.min.y - out), ImVec2(layout.max.x + out, layout.max.y + out), IM_COL32(0, 0, 0, ALPHAS[ring]),
                    S(BOX_ROUNDING) + out, 0, S(4.0f));
            }
            draw->AddRectFilled(layout.min, layout.max, INK_2, S(BOX_ROUNDING));
            draw->AddRect(layout.min, layout.max, LINE, S(BOX_ROUNDING), 0, Stroke(1.0f));

            // The title, the count on its right, the description under it.
            const Request& request = s_current->request;
            const float left = layout.min.x + S(PAD), right = layout.max.x - S(PAD);
            Icon(draw, ICON_KEYBOARD, 20.0f, ImVec2(left + S(10.0f), layout.titleY), TEXT_3);
            const std::string count = request.maxLength < 1000 ? std::format("{} / {}", s_text.size(), request.maxLength) : std::format("{}", s_text.size());
            const float countWidth = TextWidth(Face::Mono, 12.5f, count);
            TextEllipsis(draw, Face::SemiBold, 16.0f, ImVec2(left + S(20.0f + 10.0f), layout.titleY - std::round(16.0f * Scale()) * 0.52f), right - countWidth - S(12.0f),
                TEXT, request.title);
            Text(draw, Face::Mono, 12.5f, ImVec2(right - countWidth, layout.titleY - std::round(12.5f * Scale()) * 0.52f),
                s_full > 0.0f ? WARN : s_text.size() >= request.maxLength ? TEXT_2 : TEXT_3, count);
            if (!request.description.empty())
                TextEllipsis(draw, Face::Sans, 13.5f, ImVec2(left, layout.descriptionY), right, TEXT_2, request.description);

            DrawField(draw, layout);
            DrawTabs(draw, layout);
            for (int row = 0; row < Rows(); row++)
            {
                for (int col = 0; col < RowKeys(row); col++)
                    DrawKey(draw, layout, row, col);
            }
            DrawFooter(draw, layout);

            const float fade = std::min(s_age / FADE_SECONDS, 1.0f);
            if (fade < 1.0f)
            {
                for (int i = first; i < draw->VtxBuffer.Size; i++)
                    draw->VtxBuffer[i].col = Alpha(draw->VtxBuffer[i].col, fade);
            }
        }
    }

    Id Open(Request request)
    {
        const Id id = s_nextId++;
        s_waiting.push_back({ id, std::move(request) });
        ShowNext();
        return id;
    }

    Id Open(std::string title, std::string initial, size_t maxLength, std::function<void(std::optional<std::string> text)> onDone)
    {
        Request request;
        request.title = std::move(title);
        request.initial = std::move(initial);
        request.maxLength = maxLength;
        request.onDone = std::move(onDone);
        return Open(std::move(request));
    }

    Id Post(Request request)
    {
        const Id id = s_nextId++;
        std::lock_guard lock(s_postMutex);
        s_posted.push_back({ id, std::move(request) });
        return id;
    }

    void Cancel(Id id)
    {
        std::lock_guard lock(s_postMutex);
        s_cancels.push_back(id);
    }

    bool IsOpen()
    {
        return s_current.has_value();
    }

    std::optional<ImVec2> KeyCenter(std::string_view key)
    {
        for (const auto& [name, center] : s_keyCenters)
        {
            if (name == key)
                return center;
        }
        return std::nullopt;
    }
}

namespace rui::detail
{
    bool KeyboardFrameStart()
    {
        using namespace keyboard;
        std::vector<Entry> posted;
        std::vector<Id> cancels;
        {
            std::lock_guard lock(s_postMutex);
            posted.swap(s_posted);
            cancels.swap(s_cancels);
        }
        for (Entry& entry : posted)
            s_waiting.push_back(std::move(entry));
        for (const Id id : cancels)
            CancelId(id);
        ShowNext();
        return IsOpen();
    }

    void DrawKeyboard(bool panelOpen)
    {
        using namespace keyboard;
        if (!s_current)
            return;
        const float delta = ImGui::GetIO().DeltaTime;
        s_flash = std::max(s_flash - delta, 0.0f);
        s_full = std::max(s_full - delta, 0.0f);

        // A window over everything: the focus and the mouse are its while
        // it's open.
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowFocus();
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("##keyboard", nullptr, flags);
        Layout layout = Arrange();
        // Input from its second frame: the press that opened it isn't a key.
        bool open = s_frames == 0 || HandleInput(layout);
        if (!open && s_current)
        {
            // Done, and another was waiting: it shows from now.
            layout = Arrange();
            open = true;
        }
        if (open)
        {
            RecordCenters(layout);
            Draw(layout, panelOpen);
            s_frames++;
            s_age += delta;
        }
        ImGui::End();
    }

    void CancelKeyboards()
    {
        using namespace keyboard;
        {
            std::lock_guard lock(s_postMutex);
            for (Entry& entry : s_posted)
                s_waiting.push_back(std::move(entry));
            s_posted.clear();
            s_cancels.clear();
        }
        std::deque<Entry> waiting;
        waiting.swap(s_waiting);
        if (s_current)
            waiting.push_front(std::move(*s_current));
        s_current.reset();
        for (Entry& entry : waiting)
        {
            if (entry.request.onDone)
                entry.request.onDone(std::nullopt);
        }
    }
}
