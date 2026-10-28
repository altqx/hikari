# Windows installer tests (Winix VM)

The checks of the installers spec, section 8.3, for the Qt payload. They run in their own
guest work tree (workspace `inst-qt-windows`), so the main tree's build in the VM is left alone;
the Qt SDK of the main tree (`C:/Winix/work/hikari-qt/out/sdk`) is reused read-only.

1. **Payload.** `winix --config tests/packaging/windows/winix.yaml sync .`, then
   `winix --config tests/packaging/windows/winix.yaml run package --wait`
   (`--env HIKARI_VERSION_SUFFIX=test.3` for a prerelease version resource). It runs the
   `windows-x64-package` preset and zips the payload to
   `out/build/windows-x64-package/payload.zip`; fetch it with `winix pull`.
2. **Installers.** On Linux with Ubuntu 24.04's `nsis` (a container is enough):
   `packaging/windows/build-installer.sh --payload <payload>/HikariSub --version <V> --codebase qt
   --icon HikariSub/Bitmaps/HikariLargeIcon.ico --icon-header HikariSub/FileTypeIcons.h
   --out <dir>/HikariSub-<V>-windows-x64-setup.exe`. For the upgrade-over-legacy step, build a
   legacy setup the same way from the `HikariSub_x64` folder of a legacy release zip, with
   `--codebase legacy` and a 0.0.x version.
3. **Silent checks.** `tests/packaging/windows/run-installer-tests.sh <dir> <legacy V> <V1> <V2> <V3>`
   runs every silent step (installs in both scopes, legacy to Qt, Qt to Qt, the running-app
   waits, `/UPDATE` with `/WAITPID` and `/RELAUNCH`, downgrade and scope refusals, uninstall).
   `installer.ps1` holds the checks; each task takes its parameters from `--env`.
4. **Interactive pages**, with `winix ui`: launch a setup with `ui launch --exe`, read it with
   `ui tree --pid`, press buttons by automation id (`1` Next/Install/Finish, `2` Cancel,
   `1200`/`1202` the scope radios, `1203` the Finish page's "Start HikariSub"). The UAC prompt
   is on the secure desktop: `ui` cannot see it; the VM's user is not an administrator, so the
   prompt asks for credentials and can only be refused (`virsh -c qemu:///session send-key
   winix-dev KEY_ESC`), which brings back the scope page with its message.

Jobs end their child processes, so an app that must outlive a step starts through the
`inst-detach` task.

No setup or uninstaller may stay on the desktop after a session: it covers the windows of the
desktop tests that follow and takes their focus. `run-installer-tests.sh` ends any it leaves,
however it stops; an interactive session ends with
`winix --config tests/packaging/windows/winix.yaml run inst-admin --env ACTION=end-setups --wait`
(elevated, so an all-users one goes too; `tools/winix/end-setups.ps1`). The `end-setups-test` task
checks that script (`--env HS_SETUP=<guest path of a setup>` adds a real setup and uninstaller).
