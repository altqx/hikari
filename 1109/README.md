# #1109: a still pointer steers keyboard menu navigation (xcb)

Real app (hikarisub, main 657ade4ff) under Xvfb at 1280x1024 with a DejaVu-only fontconfig, as on the GitHub Ubuntu runner. Input from xdotool (XTEST).

Keys: Alt+W, Down, Down, Right, Right.

- `resting-0-start.png`: the pointer is moved to (640,512), Xvfb's start position, then left still.
- `resting-1-first-right.png`: Right opens View › Panels under the pointer. Agent, the row under the pointer, becomes current and opens its submenu. The keyboard's row is Video.
- `resting-2-second-right.png`: the second Right closes the View chain and opens Help.
- `parked-right-right.png`: the pointer is at (1279,1023), off the window. The same keys open Video › Show.
