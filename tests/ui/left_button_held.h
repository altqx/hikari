#pragma once

// On Windows KDDW drops a separator move unless GetKeyState says the left button is down, and a synthetic press doesn't set it.
class LeftButtonHeld {
public:
    LeftButtonHeld();
    ~LeftButtonHeld();
    LeftButtonHeld(const LeftButtonHeld &) = delete;
    LeftButtonHeld &operator=(const LeftButtonHeld &) = delete;
};
