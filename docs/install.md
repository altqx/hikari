# Installing HikariSub legacy

The [README](../README.md#download) has the short version. This page has the details of each package.

## Installing on Windows

Each release has two Windows downloads, for Windows 10 version 1809 or later, x64:

* **`HikariSub-<version>-windows-x64-setup.exe`**, the installer. It asks whether to install for **Just me** (in `%LOCALAPPDATA%\Programs\HikariSub`, no administrator rights needed) or for **All users** (in `Program Files`, after an administrator prompt). It adds a Start menu entry and, unless you untick it, registers HikariSub for `.ass`, `.ssa`, `.srt` and `.sub` files, without taking a file type that another program already opens. A desktop shortcut is optional. Running a newer installer updates the existing installation in place, for the same choice of users; to switch between Just me and All users, uninstall first. Uninstall from Settings > Apps; it removes the program files, shortcuts and file types, and keeps your settings and any files you added to the folder. An all-users uninstall started without administrator rights asks for them only when the installation folder cannot be changed without them; otherwise run the uninstaller as administrator. The installer is not code-signed, so Windows SmartScreen may ask you to confirm it ("More info", then "Run anyway").
* **`HikariSub-<version>-windows-x64.zip`**, the portable package. Extract it anywhere and run `HikariSub.exe`; it keeps its settings beside the executable and installs nothing.

The installer also runs unattended:

| Switch | Meaning |
| --- | --- |
| `/S` | silent: no pages or messages |
| `/CURRENTUSER`, `/ALLUSERS` | install for you alone or for all users (the latter from an elevated prompt). Without either, a silent install updates the existing installation, or installs for you alone |
| `/COMPONENTS=assoc,desktop` | a new silent install's options: `assoc` registers the file types (the default), `desktop` adds a desktop shortcut; `/COMPONENTS=` selects neither |
| `/D=<folder>` | the installation folder; it must come last and is not quoted. For all users it must be a new folder directly in Program Files, or the folder of the existing all-users installation |

Its exit code is 0 on success, 1 when cancelled, 2 on another failure, 3 when HikariSub is still running, 4 when administrator rights are needed or were refused, 5 when a newer version is already installed, and 6 when HikariSub is installed for the other choice of users. The uninstaller, `Uninstall HikariSub.exe` in the installation folder, takes `/S` too.

## Installing on Linux

Every release on the [releases page](https://github.com/altqx/hikari/releases)
has these Linux x86_64 packages. All of them need glibc 2.35 or newer: Ubuntu
22.04, Debian 12, Fedora, openSUSE Tumbleweed, Arch, Linux Mint 21 and later.

* **AppImage** (`HikariSub-<version>-linux-x86_64.AppImage`): one file with
  everything inside, GTK, GLib and GStreamer included, so it runs on any of
  those distributions with nothing else to install. Make it executable and run
  it:

  ```bash
  chmod +x HikariSub-*-linux-x86_64.AppImage
  ./HikariSub-*-linux-x86_64.AppImage
  ```

  It needs FUSE, but not the old `libfuse2`. Where FUSE is missing (in a
  container, for example), run it with `--appimage-extract-and-run`. It adds
  no menu entry of its own; AppImageLauncher or Gear Lever can add one.
* **Debian, Ubuntu and Mint** (`hikarisub-<version>-linux-amd64.deb`):

  ```bash
  sudo apt install ./hikarisub-*-linux-amd64.deb
  ```

* **Fedora** and **openSUSE** (`hikarisub-<version>-linux-x86_64.rpm`):

  ```bash
  sudo dnf install --nogpgcheck ./hikarisub-*-linux-x86_64.rpm
  sudo zypper install --allow-unsigned-rpm ./hikarisub-*-linux-x86_64.rpm
  ```

  On Fedora and openSUSE, H.264, AAC and other patented formats play only
  with the full codec sets from RPM Fusion or Packman; the FFMS2 path used for
  typesetting and timing decodes them either way.
* **Arch Linux**: install one of the two AUR packages with an AUR helper, and
  update it as any other package (`yay -Syu`):
  * `hikarisub-stable-bin` (`yay -S hikarisub-stable-bin`) carries stable
    releases only;
  * `hikarisub-beta-bin` (`yay -S hikarisub-beta-bin`) carries every release,
    release candidates included, so it always has the newest one.

  They conflict with each other: install one.
* **Portable** (`hikarisub-<version>-linux-x86_64.tar.gz`): extract it
  anywhere and run `./hikarisub`. It uses the system's GTK and GStreamer, so
  install those first (see [Package names on other Linux distributions](build.md#2-package-names-on-other-linux-distributions)),
  and run `./install-desktop-integration.sh` for a menu entry.

The deb, rpm and AUR packages install HikariSub in `/opt/hikarisub`, with
`hikarisub` on the `PATH`, the menu entry, file types and icons, and pull in
everything it needs, the GStreamer plugins included. Removing the package
leaves your settings in your home directory alone.

When a new release is out, HikariSub tells you. The AppImage replaces itself,
and the deb and rpm download the new package and install it after the
system's password prompt. The AUR packages only tell you; update with your
AUR helper as usual. `hikarisub-stable-bin` is told about stable releases
only and `hikarisub-beta-bin` about release candidates too, so the "Stable
versions only" option is not shown for them.

## Where your scripts keep their files

The portable package keeps everything in its `Automation` folder. An installed copy cannot write to its program folder, so it keeps what scripts write (`?user`, `?local` and `?temp`, logs and autosaves) in `%LOCALAPPDATA%\HikariSub\Legacy\Automation` on Windows or `~/.local/share/HikariSub/Legacy/Automation` on Linux. It autoloads scripts from `automation/Autoload` there, and from DependencyControl's `automation/autoload`, as well as the shipped ones; a script there replaces a shipped script of the same name.
