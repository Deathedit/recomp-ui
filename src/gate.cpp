#include <rui/gate.h>
#include <rui/rui.h>

#include <atomic>

namespace rui::gate
{
    namespace
    {
        std::atomic<bool> s_capture{ false };
        std::atomic<bool> s_release{ false };  // closed: neutral until all is released
        std::atomic<uint16_t> s_toggle{ uint16_t(pad::BACK | pad::START) };
    }

    void SetCapture(bool capture)
    {
        if (s_capture.exchange(capture) && !capture)
            s_release = true;
    }

    void SetToggleButtons(uint16_t buttons)
    {
        s_toggle = buttons;
    }

    bool Captured()
    {
        return s_capture;
    }

    bool Releasing()
    {
        return s_release;
    }

    bool Holds(uint16_t buttons, uint8_t leftTrigger, uint8_t rightTrigger)
    {
        if (s_capture)
            return true;
        if (!s_release)
            return false;
        const bool anything = buttons != 0 || leftTrigger > pad::TRIGGER_THRESHOLD || rightTrigger > pad::TRIGGER_THRESHOLD;
        if (!anything)
            s_release = false;
        return anything;
    }

    uint16_t StripToggle(uint16_t buttons)
    {
        const uint16_t toggle = s_toggle;
        return toggle != 0 && (buttons & toggle) == toggle ? uint16_t(buttons & ~toggle) : buttons;
    }
}
