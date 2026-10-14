-- Hyprland for the gate (D3): nested in KWin's virtual backend (hyprland-start.sh),
-- one 1600x1000 output, Hyprland's own defaults otherwise (the window
-- system's floating, move, resize and snapping behaviour under test), no
-- animations (positions are read right after each drag), no wallpaper art.
hl.monitor({ output = "", mode = "1600x1000", position = "0x0", scale = "1" })
hl.config({
    animations = { enabled = false },
    misc = { force_default_wallpaper = 0, disable_hyprland_logo = true, disable_splash_rendering = true },
    input = { kb_layout = "us", follow_mouse = 1 },
    -- no "Hyprland Updated" / donation windows over the app
    ecosystem = { no_update_news = true, no_donation_nag = true },
})
