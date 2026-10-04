# D1 native gate: Linux compositors in a container (docs/qt/docking.md,
# "Required native gate and review"). The image carries compositors, input
# injection, screenshot and accessibility tools only; HikariSub itself comes
# from the host build tree, mounted read-only at its own path.
FROM archlinux:latest

RUN pacman -Syu --noconfirm --needed \
        sway wlr-randr grim slurp wtype weston wlr-protocols \
        mutter gnome-shell kwin \
        xorg-server-xvfb xorg-xrandr xorg-xwininfo xorg-xprop openbox picom xdotool imagemagick \
        orca speech-dispatcher at-spi2-core python-gobject python-dbus dbus \
        pipewire wireplumber libei gstreamer gst-plugin-pipewire gst-plugins-base gst-plugins-good \
        mesa libglvnd vulkan-swrast \
        libxkbcommon libxkbcommon-x11 xcb-util-cursor xcb-util-wm xcb-util-image \
        xcb-util-keysyms xcb-util-renderutil libpulse alsa-lib libsndfile \
        noto-fonts ttf-dejavu gcc gdb pkgconf procps-ng which strace \
    && pacman -Scc --noconfirm
# Docker's capability bounding set has no CAP_SYS_NICE: compositors that
# carry it as a file capability would not start.
RUN setcap -r /usr/bin/sway && setcap -r /usr/bin/kwin_wayland

# The host user's uid, so evidence written to the mounted worktree is theirs.
RUN useradd -m -u 1000 -s /bin/bash gate
USER gate
WORKDIR /home/gate
