// Toasts: short notices for things that just happened (a setting that needs
// a restart, a screenshot saved, a controller connected), bottom right, up
// to three, newest at the bottom, gone after a few seconds. They take no
// input. While the panel is open the newest shows in its footer instead, so
// the stack doesn't cover it.
//
// PushToast: the drawing thread only (pages, hooks); rui::PostToast from
// any other.
#pragma once

#include "rui.h"
#include "style.h"

#include <optional>
#include <string>

namespace rui
{
    void PushToast(ToastKind kind, ImWchar icon, std::string title, std::string body, float seconds = 4.0f);
}
