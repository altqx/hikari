# D1 native gate on Windows: the guest side of gate_windows.py (winix.yaml
# tasks gate-win-*). Runs in the VM workspace ($PWD); everything it writes
# stays under out\native-gate-win.
#   -Action prepare     scratch documents, the media fixture, paths (GATE_LAYOUT line)
#   -Action fresh       end the HikariSub under test, set its profile aside (never deleted)
#   -Action kill        end the HikariSub under test
#   -Action status      ALIVE yes|no, the layout files, the newest app log
#   -Action fullscreen  GATE_FULLSCREEN=on|off: the main window borderless over the whole monitor, or back
#   -Action dpi         GATE_DPI_PERCENT: the display scale of the (single) monitor, live (DPI_RESULT line)
#   -Action keys        GATE_KEYS: a JSON list of chords ("alt+w", "down") and pauses (seconds), sent with
#                       SendInput as a keyboard sends them: scan codes, and the extended-key flag on the
#                       navigation keys (`winix ui keys` sends arrows without it, i.e. as keypad keys,
#                       which NVDA takes as review-cursor commands). GATE_KEYS_DELAY: seconds between chords.
param([Parameter(Mandatory)][ValidateSet('prepare', 'fresh', 'kill', 'status', 'fullscreen', 'dpi', 'keys')][string]$Action)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = $PWD.Path
$scratch = Join-Path $root 'out\native-gate-win'
$build = Join-Path $root 'out\build\windows-x64-release'
$qtBin = Join-Path $root 'out\sdk\qt-6.11.2-windows\6.11.2\msvc2022_64\bin'
# QStandardPaths on Windows: AppConfigLocation (hikari.ini, layout.json) and AppDataLocation (Recovery).
$profileDirs = [ordered]@{
    local   = Join-Path $env:LOCALAPPDATA 'HikariSub\HikariSub'
    roaming = Join-Path $env:APPDATA 'HikariSub\HikariSub'
}
$utf8 = New-Object System.Text.UTF8Encoding($false)
New-Item -ItemType Directory -Force $scratch | Out-Null

function AppProcesses {
    # Only the build under test (the legacy release is also HikariSub.exe).
    @(Get-Process hikarisub -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -and $_.Path.StartsWith($build, [StringComparison]::OrdinalIgnoreCase) })
}

function StopApp {
    $procs = AppProcesses
    $procs | Stop-Process -Force -ErrorAction SilentlyContinue
    foreach ($i in 1..60) { if (-not (AppProcesses).Count) { break }; Start-Sleep -Milliseconds 250 }
    Write-Output "stopped $($procs.Count) process(es)"
}

$win32 = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class GateWin {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO { public int cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr(IntPtr h, int i);
    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")] public static extern IntPtr SetWindowLongPtr(IntPtr h, int i, IntPtr v);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint f);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint f);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromPoint(POINT p, uint f);
    [DllImport("user32.dll")] public static extern bool GetMonitorInfo(IntPtr m, ref MONITORINFO mi);
    [DllImport("user32.dll", EntryPoint = "SystemParametersInfoW")] public static extern bool SpiGetInt(uint a, uint b, ref int c, uint d);
    [DllImport("user32.dll", EntryPoint = "SystemParametersInfoW")] public static extern bool SpiSet(uint a, int b, IntPtr c, uint d);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    [DllImport("shcore.dll")] public static extern int GetDpiForMonitor(IntPtr m, int type, out uint x, out uint y);

    // The visible top-level window of one of these processes titled
    // "HikariSub" or "<document> - HikariSub" (the main window).
    public static IntPtr FindMain(uint[] pids) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (Array.IndexOf(pids, pid) < 0 || !IsWindowVisible(h)) return true;
            var sb = new StringBuilder(512); GetWindowText(h, sb, 512);
            var t = sb.ToString();
            if (t == "HikariSub" || t.EndsWith(" - HikariSub")) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    // The primary monitor's effective DPI as a per-monitor-aware thread sees it.
    public static uint PrimaryDpi() {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        var m = MonitorFromPoint(new POINT { X = 0, Y = 0 }, 1);
        uint x, y; GetDpiForMonitor(m, 0, out x, out y);
        return x;
    }
}
'@

$keyboard = @'
using System;
using System.Runtime.InteropServices;
public static class GateKeys {
    [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Explicit, Size = 40)] public struct IN { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KI ki; }
    [DllImport("user32.dll")] static extern uint SendInput(uint n, IN[] i, int size);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint c, uint t);
    static readonly ushort[] Extended = { 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E, 0x5B };
    static IN Key(ushort vk, bool up) {
        var i = new IN(); i.type = 1; i.ki.vk = vk; i.ki.scan = (ushort)MapVirtualKey(vk, 0);
        i.ki.flags = (Array.IndexOf(Extended, vk) >= 0 ? 1u : 0u) | (up ? 2u : 0u);
        return i;
    }
    // Every key of the chord down in order, then up in reverse order.
    public static uint Chord(ushort[] vks) {
        var a = new IN[vks.Length * 2];
        for (int k = 0; k < vks.Length; k++) { a[k] = Key(vks[k], false); a[a.Length - 1 - k] = Key(vks[k], true); }
        return SendInput((uint)a.Length, a, Marshal.SizeOf(typeof(IN)));
    }
}
'@
$vkNames = @{ 'alt' = 0x12; 'ctrl' = 0x11; 'shift' = 0x10; 'down' = 0x28; 'up' = 0x26; 'left' = 0x25; 'right' = 0x27
    'return' = 0x0D; 'escape' = 0x1B; 'tab' = 0x09; 'space' = 0x20; 'f4' = 0x73; 'f6' = 0x75; 'f11' = 0x7A; 'end' = 0x23
    'home' = 0x24; 'delete' = 0x2E; 'win' = 0x5B }

switch ($Action) {
    'keys' {
        Add-Type -TypeDefinition $keyboard
        $delay = if ($env:GATE_KEYS_DELAY) { [double]$env:GATE_KEYS_DELAY } else { 0.35 }
        foreach ($item in @($env:GATE_KEYS | ConvertFrom-Json | ForEach-Object { $_ })) {
            if ($item -is [string]) {
                $vks = [uint16[]]@($item.ToLower().Split('+') | ForEach-Object {
                        if ($vkNames.ContainsKey($_)) { $vkNames[$_] } elseif ($_.Length -eq 1) { [int][char]$_.ToUpper() } else { throw "unknown key $_" } })
                $sent = [GateKeys]::Chord($vks)
                Write-Output "chord $item sent $sent"
                Start-Sleep -Milliseconds ([int]($delay * 1000))
            } else { Start-Sleep -Milliseconds ([int]([double]$item * 1000)) }
        }
    }
    'prepare' {
        $exe = Get-ChildItem -Recurse -Filter hikarisub.exe $build -ErrorAction SilentlyContinue |
            Sort-Object { $_.FullName -notlike '*\src\app\*' } | Select-Object -First 1
        $docs = Join-Path $scratch 'docs'
        New-Item -ItemType Directory -Force $docs | Out-Null
        $head = "[Script Info]`r`nScriptType: v4.00+`r`n"
        $styles = "[V4+ Styles]`r`nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding`r`n"
        $events = "[Events]`r`nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text`r`n"
        # The same documents as the Linux gate (gate.py episode() and step_video()).
        $episode = Join-Path $docs 'episode.ass'
        [IO.File]::WriteAllText($episode, "$head`r`n$styles" +
            "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1`r`n`r`n$events" +
            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first`r`nDialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second`r`n", $utf8)
        $fixture = Get-ChildItem -Recurse -Filter cfr.mkv $build -ErrorAction SilentlyContinue | Select-Object -First 1
        $ep1 = $null
        if ($fixture) {
            Copy-Item $fixture.FullName (Join-Path $docs 'ep1.mkv') -Force
            $ep1 = Join-Path $docs 'ep1.ass'
            [IO.File]::WriteAllText($ep1, "$head" + "PlayResX: 320`r`nPlayResY: 240`r`nVideo File: ep1.mkv`r`n`r`n$styles" +
                "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1`r`n`r`n$events" +
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,overlay`r`n", $utf8)
        }
        $nvda = Get-ChildItem (Join-Path $scratch 'nvda') -Directory -Filter 'portable-*' -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName 'nvda.exe') } | Select-Object -First 1
        $os = Get-CimInstance Win32_OperatingSystem
        $layout = [ordered]@{
            root = $root; scratch = $scratch; app = $(if ($exe) { $exe.FullName } else { $null }); qt_bin = $qtBin
            qt_bin_exists = (Test-Path $qtBin); launcher = Join-Path $PSScriptRoot 'launch.ps1'
            episode = $episode; ep1_ass = $ep1; fixture = $(if ($fixture) { $fixture.FullName } else { $null })
            nvda_exe = $(if ($nvda) { Join-Path $nvda.FullName 'nvda.exe' } else { $null })
            nvda_config = Join-Path $scratch 'nvda\config'; nvda_dir = Join-Path $scratch 'nvda'
            profile = $profileDirs; windows = "$($os.Caption) $($os.Version) build $($os.BuildNumber)"
        }
        Write-Output ('GATE_LAYOUT ' + ($layout | ConvertTo-Json -Compress))
    }
    'fresh' {
        StopApp
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
        foreach ($k in $profileDirs.Keys) {
            $dir = $profileDirs[$k]
            if (Test-Path $dir) {
                $dest = Join-Path $scratch "old-profiles\$stamp\$k"
                New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
                # The ended process can hold its files for a moment.
                foreach ($try in 1..20) {
                    try { Move-Item $dir $dest -ErrorAction Stop; break }
                    catch { if ($try -eq 20) { throw }; Start-Sleep -Milliseconds 500 }
                }
                Write-Output "set aside $dir -> $dest"
            }
        }
    }
    'kill' { StopApp }
    'status' {
        $procs = AppProcesses
        Write-Output "ALIVE $(if ($procs.Count) { 'yes' } else { 'no' })"
        $procs | ForEach-Object { Write-Output "process $($_.Id) responding=$($_.Responding) $($_.MainWindowTitle)" }
        Write-Output '== layout files'
        Get-ChildItem $profileDirs.local -Filter 'layout.json*' -ErrorAction SilentlyContinue |
            ForEach-Object { Write-Output "$($_.FullName) $($_.Length) $($_.LastWriteTime.ToString('s'))" }
        Write-Output '== app log'
        $logFile = Get-ChildItem $scratch -Filter 'app-*.err.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($logFile) { Write-Output $logFile.Name; Get-Content $logFile.FullName -Tail 40 }
        Write-Output '== end'
    }
    'fullscreen' {
        Add-Type -TypeDefinition $win32
        $pids = [uint32[]]@((AppProcesses) | ForEach-Object { $_.Id })
        $h = [GateWin]::FindMain($pids)
        if ($h -eq [IntPtr]::Zero) { Write-Output 'FULLSCREEN_RESULT {"error":"no main window"}'; exit 0 }
        $stateFile = Join-Path $scratch 'fullscreen-state.json'
        $GWL_STYLE = -16
        $WS_CAPTION = 0x00C00000; $WS_THICKFRAME = 0x00040000
        $SWP_FRAMECHANGED = 0x0020; $SWP_SHOWWINDOW = 0x0040; $SWP_NOZORDER = 0x0004
        if ($env:GATE_FULLSCREEN -eq 'on') {
            $style = [GateWin]::GetWindowLongPtr($h, $GWL_STYLE).ToInt64()
            $r = New-Object GateWin+RECT
            [void][GateWin]::GetWindowRect($h, [ref]$r)
            @{ style = $style; x = $r.Left; y = $r.Top; w = $r.Right - $r.Left; h = $r.Bottom - $r.Top } |
                ConvertTo-Json -Compress | Set-Content -Encoding UTF8 $stateFile
            # What a "borderless fullscreen" tool (or a game) does: no caption or
            # frame, the whole monitor including the taskbar's area.
            $mi = New-Object GateWin+MONITORINFO
            $mi.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($mi)
            [void][GateWin]::GetMonitorInfo([GateWin]::MonitorFromWindow($h, 2), [ref]$mi)
            [void][GateWin]::SetWindowLongPtr($h, $GWL_STYLE, [IntPtr]($style -band -bnot ($WS_CAPTION -bor $WS_THICKFRAME)))
            $m = $mi.rcMonitor
            [void][GateWin]::SetWindowPos($h, [IntPtr]::Zero, $m.Left, $m.Top, $m.Right - $m.Left, $m.Bottom - $m.Top, $SWP_FRAMECHANGED -bor $SWP_SHOWWINDOW)
        } elseif (Test-Path $stateFile) {
            $s = Get-Content -Raw $stateFile | ConvertFrom-Json
            [void][GateWin]::SetWindowLongPtr($h, $GWL_STYLE, [IntPtr][int64]$s.style)
            [void][GateWin]::SetWindowPos($h, [IntPtr]::Zero, $s.x, $s.y, $s.w, $s.h, $SWP_FRAMECHANGED -bor $SWP_SHOWWINDOW -bor $SWP_NOZORDER)
            Remove-Item $stateFile
        }
        Start-Sleep -Milliseconds 500
        $r = New-Object GateWin+RECT
        [void][GateWin]::GetWindowRect($h, [ref]$r)
        $now = [GateWin]::GetWindowLongPtr($h, $GWL_STYLE).ToInt64()
        Write-Output ('FULLSCREEN_RESULT ' + (@{ mode = $env:GATE_FULLSCREEN; x = $r.Left; y = $r.Top; w = $r.Right - $r.Left; h = $r.Bottom - $r.Top
                    caption = [bool]($now -band $WS_CAPTION) } | ConvertTo-Json -Compress))
    }
    'dpi' {
        # SPI_GETLOGICALDPIOVERRIDE / SPI_SETLOGICALDPIOVERRIDE (what Settings >
        # Display > Scale uses for the primary monitor): the value is a step
        # relative to the recommended scale and applies without signing out.
        Add-Type -TypeDefinition $win32
        $scales = 100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500
        $target = [int]$env:GATE_DPI_PERCENT
        $before = [int][Math]::Round([GateWin]::PrimaryDpi() * 100 / 96)
        $rel = 0
        $okGet = [GateWin]::SpiGetInt(0x009E, 0, [ref]$rel, 0)
        $result = [ordered]@{ target = $target; before = $before; relative_before = $rel; get_ok = $okGet }
        $cur = [Array]::IndexOf($scales, $before)
        $want = [Array]::IndexOf($scales, $target)
        if ($cur -lt 0 -or $want -lt 0 -or -not $okGet) {
            $result.error = 'current or target scale is not a Windows scale step, or the override cannot be read'
        } else {
            $recommended = $cur - $rel
            $result.recommended = $scales[[Math]::Max(0, [Math]::Min($scales.Count - 1, $recommended))]
            $result.relative_set = $want - $recommended
            $result.set_ok = [GateWin]::SpiSet(0x009F, $want - $recommended, [IntPtr]::Zero, 1)
            Start-Sleep -Seconds 3
        }
        $result.after = [int][Math]::Round([GateWin]::PrimaryDpi() * 100 / 96)
        $result.per_monitor_settings = @(Get-ChildItem 'HKCU:\Control Panel\Desktop\PerMonitorSettings' -ErrorAction SilentlyContinue |
                ForEach-Object { "$($_.PSChildName) DpiValue=$((Get-ItemProperty $_.PSPath).DpiValue)" })
        $result.screen = (Get-CimInstance Win32_VideoController | Select-Object -First 1 | ForEach-Object { "$($_.CurrentHorizontalResolution)x$($_.CurrentVerticalResolution)" })
        Write-Output ('DPI_RESULT ' + ($result | ConvertTo-Json -Compress))
    }
}
