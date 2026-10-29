// The input gate: while the panel is open it takes the pad, and the game
// gets a neutral one from the real controllers (and no keystrokes), until
// every button is released after it closes (B closing it doesn't reach the
// game). The toggle's buttons held together never reach the game. Inputs
// the host makes up itself (a replay, a virtual pad) needn't go through it.
//
// rui::NewFrame sets the capture; the host's controller path (its XInput
// functions, on the game's threads) asks the gate about each real state it
// hands the game. Thread-safe.
#pragma once

#include <cstdint>

namespace rui::gate
{
    // rui::NewFrame's (the panel's visibility); a host without the panel
    // can hold the pad itself.
    void SetCapture(bool capture);
    // The buttons that toggle the panel (InitInfo::toggleButtons; Back +
    // Start until Init).
    void SetToggleButtons(uint16_t buttons);

    // The panel holds the pad now.
    bool Captured();
    // It closed and holds the pad until everything is released.
    bool Releasing();
    // Whether the game gets a neutral pad, given the real one's buttons and
    // triggers (a trigger past XInput's threshold counts as held). Ends the
    // hold after closing once nothing is.
    bool Holds(uint16_t buttons, uint8_t leftTrigger, uint8_t rightTrigger);
    // The buttons without the toggle's, when all of those are held.
    uint16_t StripToggle(uint16_t buttons);

    // A real controller's state as the game should see it, for an
    // XINPUT_GAMEPAD-shaped struct: neutral while held, else the toggle
    // stripped.
    template <typename Gamepad>
    void FilterXInput(Gamepad& pad)
    {
        if (Holds(pad.wButtons, pad.bLeftTrigger, pad.bRightTrigger))
        {
            pad = {};
            return;
        }
        pad.wButtons = StripToggle(pad.wButtons);
    }
}
