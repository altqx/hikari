#include "left_button_held.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

void setLeftButton(BYTE state)
{
    BYTE keys[256];
    GetKeyboardState(keys);
    keys[VK_LBUTTON] = state;
    SetKeyboardState(keys);
}

}

LeftButtonHeld::LeftButtonHeld() { setLeftButton(0x80); }
LeftButtonHeld::~LeftButtonHeld() { setLeftButton(0); }
#else
LeftButtonHeld::LeftButtonHeld() = default;
LeftButtonHeld::~LeftButtonHeld() = default;
#endif
