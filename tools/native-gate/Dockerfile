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
        hyprland \
        ibus ibus-hangul noto-fonts-cjk \
        xdg-desktop-portal xdg-desktop-portal-kde xdg-desktop-portal-gtk qt6-wayland dconf gsettings-desktop-schemas \
    && pacman -Scc --noconfirm
# Docker's bounding set lacks CAP_SYS_NICE: compositors carrying it as a file capability would not start.
RUN setcap -r /usr/bin/sway && setcap -r /usr/bin/kwin_wayland && (setcap -r /usr/bin/Hyprland 2>/dev/null || true)

# libdbus aborts a client that opens a private bus without a machine id.
RUN dbus-uuidgen --ensure=/etc/machine-id

RUN useradd -m -u 1000 -s /bin/bash gate
USER gate
WORKDIR /home/gate
