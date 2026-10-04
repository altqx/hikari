# D1 native gate on Windows (winix tasks display-layout and gate-win-display):
# the guest's desktop over the VM's two QXL monitors, in the user session,
# without admin rights. One output line: DISPLAY_RESULT <JSON>.
#   GATE_DISPLAY=extend   SetDisplayConfig(SDC_TOPOLOGY_EXTEND | SDC_APPLY), then every
#                         monitor at GATE_WIDTH x GATE_HEIGHT (default 1280x800), the
#                         second to the right of the primary (ChangeDisplaySettingsEx)
#   GATE_DISPLAY=detach   only the primary (SDC_TOPOLOGY_INTERNAL; else the second
#                         display detached with a 0x0 mode)
#   GATE_DISPLAY=status   nothing changed
#   GATE_SCALE2=PERCENT   (optional, after the above) the second monitor's scale through
#                         DisplayConfigSetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_SET_DPI_SCALE),
#                         the per-monitor call Settings > Display uses; applies live
# The result lists every monitor (EnumDisplayMonitors/GetMonitorInfo, physical
# pixels: the thread is per-monitor DPI aware) with its effective DPI.
$ErrorActionPreference = 'Stop'
trap { Write-Output ('DISPLAY_RESULT ' + (@{ error = "$($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))" } | ConvertTo-Json -Compress)); exit 1 }
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class GateDisplay {
    [StructLayout(LayoutKind.Sequential)] public struct LUID { public uint Low; public int High; }
    [StructLayout(LayoutKind.Sequential)] public struct PATH_SOURCE { public LUID adapterId; public uint id; public uint modeInfoIdx; public uint statusFlags; }
    [StructLayout(LayoutKind.Sequential)] public struct PATH_TARGET { public LUID adapterId; public uint id; public uint modeInfoIdx; public uint outputTechnology; public uint rotation; public uint scaling; public uint refreshNum; public uint refreshDen; public uint scanLineOrdering; public int targetAvailable; public uint statusFlags; }
    [StructLayout(LayoutKind.Sequential)] public struct PATH_INFO { public PATH_SOURCE source; public PATH_TARGET target; public uint flags; }
    [StructLayout(LayoutKind.Sequential, Size = 64)] public struct MODE_INFO { public uint infoType; public uint id; public LUID adapterId; public uint width; public uint height; public uint pixelFormat; public int x; public int y; }
    [StructLayout(LayoutKind.Sequential)] public struct HEADER { public int type; public uint size; public LUID adapterId; public uint id; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] public struct SOURCE_NAME { public HEADER header; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string gdiName; }
    [StructLayout(LayoutKind.Sequential)] public struct DPI_GET { public HEADER header; public int minRel; public int curRel; public int maxRel; }
    [StructLayout(LayoutKind.Sequential)] public struct DPI_SET { public HEADER header; public int rel; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] public struct MONITORINFOEX { public int cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szDevice; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] public struct DEVMODE {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
        public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra; public int dmFields;
        public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
        public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
        public short dmLogPixels; public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
        public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] public struct DISPLAY_DEVICE { public int cb; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceString; public int StateFlags; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceID; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceKey; }
    delegate bool MonitorEnum(IntPtr m, IntPtr dc, ref RECT r, IntPtr d);

    [DllImport("user32.dll")] static extern int GetDisplayConfigBufferSizes(uint flags, out uint np, out uint nm);
    [DllImport("user32.dll")] static extern int QueryDisplayConfig(uint flags, ref uint np, [Out] PATH_INFO[] p, ref uint nm, [Out] MODE_INFO[] m, IntPtr topo);
    [DllImport("user32.dll")] public static extern int SetDisplayConfig(uint np, IntPtr p, uint nm, IntPtr m, uint flags);
    [DllImport("user32.dll")] static extern int DisplayConfigGetDeviceInfo(ref SOURCE_NAME r);
    [DllImport("user32.dll")] static extern int DisplayConfigGetDeviceInfo(ref DPI_GET r);
    [DllImport("user32.dll")] static extern int DisplayConfigSetDeviceInfo(ref DPI_SET r);
    [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorEnum cb, IntPtr d);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool GetMonitorInfo(IntPtr m, ref MONITORINFOEX mi);
    [DllImport("shcore.dll")] static extern int GetDpiForMonitor(IntPtr m, int type, out uint x, out uint y);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool EnumDisplayDevices(string dev, uint i, ref DISPLAY_DEVICE dd, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool EnumDisplaySettings(string dev, int mode, ref DEVMODE dm);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int ChangeDisplaySettingsEx(string dev, ref DEVMODE dm, IntPtr hwnd, uint flags, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int ChangeDisplaySettingsEx(string dev, IntPtr dm, IntPtr hwnd, uint flags, IntPtr l);

    public static List<string> Monitors() {
        SetThreadDpiAwarenessContext(new IntPtr(-4));
        var list = new List<string>();
        EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (IntPtr m, IntPtr dc, ref RECT r, IntPtr d) => {
            var mi = new MONITORINFOEX(); mi.cbSize = Marshal.SizeOf(typeof(MONITORINFOEX));
            GetMonitorInfo(m, ref mi);
            uint dx, dy; GetDpiForMonitor(m, 0, out dx, out dy);
            list.Add(string.Format("{{\"device\":\"{0}\",\"primary\":{1},\"x\":{2},\"y\":{3},\"w\":{4},\"h\":{5},\"dpi\":{6}}}",
                mi.szDevice.Replace("\\", "\\\\"), (mi.dwFlags & 1) != 0 ? "true" : "false", mi.rcMonitor.Left, mi.rcMonitor.Top,
                mi.rcMonitor.Right - mi.rcMonitor.Left, mi.rcMonitor.Bottom - mi.rcMonitor.Top, dx));
            return true;
        }, IntPtr.Zero);
        return list;
    }
    // Display devices attached to the desktop or not (\\.\DISPLAYn of each adapter output).
    public static List<string> Devices() {
        var list = new List<string>();
        for (uint i = 0; ; i++) {
            var dd = new DISPLAY_DEVICE(); dd.cb = Marshal.SizeOf(typeof(DISPLAY_DEVICE));
            if (!EnumDisplayDevices(null, i, ref dd, 0)) break;
            list.Add(dd.DeviceName + "|" + dd.StateFlags + "|" + dd.DeviceString);
        }
        return list;
    }
    public static int SetMode(string dev, int w, int h, int x, int y) {
        var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
        EnumDisplaySettings(dev, -2, ref dm); // registry settings (also for a detached display)
        dm.dmPelsWidth = w; dm.dmPelsHeight = h; dm.dmPositionX = x; dm.dmPositionY = y;
        dm.dmFields = 0x80000 | 0x100000 | 0x20;
        return ChangeDisplaySettingsEx(dev, ref dm, IntPtr.Zero, 0x1 | 0x10000000, IntPtr.Zero);
    }
    public static int ApplyModes() { return ChangeDisplaySettingsEx(null, IntPtr.Zero, IntPtr.Zero, 0, IntPtr.Zero); }
    public static List<string> Modes(string dev) {
        var list = new List<string>();
        var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
        for (int i = 0; EnumDisplaySettings(dev, i, ref dm); i++) { var s = dm.dmPelsWidth + "x" + dm.dmPelsHeight; if (!list.Contains(s)) list.Add(s); }
        return list;
    }
    // Active paths: source GDI name, source position, adapter and source id.
    public static List<object[]> Sources() {
        uint np, nm; GetDisplayConfigBufferSizes(2, out np, out nm);
        var p = new PATH_INFO[np]; var m = new MODE_INFO[nm];
        int rc = QueryDisplayConfig(2, ref np, p, ref nm, m, IntPtr.Zero);
        if (rc != 0) throw new Exception("QueryDisplayConfig " + rc);
        var list = new List<object[]>();
        for (int i = 0; i < np; i++) {
            var n = new SOURCE_NAME(); n.header.type = 1; n.header.size = (uint)Marshal.SizeOf(typeof(SOURCE_NAME));
            n.header.adapterId = p[i].source.adapterId; n.header.id = p[i].source.id;
            DisplayConfigGetDeviceInfo(ref n);
            int x = 0, y = 0; uint mi = p[i].source.modeInfoIdx;
            if (mi < nm && m[mi].infoType == 1) { x = m[mi].x; y = m[mi].y; }
            list.Add(new object[] { n.gdiName, x, y, p[i].source.adapterId.Low, p[i].source.adapterId.High, p[i].source.id });
        }
        return list;
    }
    // Every available path made active (the QXL secondary's path stays
    // inactive under SDC_TOPOLOGY_EXTEND, which fails with no extend topology
    // in the database); Windows picks the modes, then the database keeps it.
    [DllImport("user32.dll")] static extern int SetDisplayConfig(uint np, [In] PATH_INFO[] p, uint nm, [In] MODE_INFO[] m, uint flags);
    public static string ActivateAll() {
        uint np, nm; GetDisplayConfigBufferSizes(1, out np, out nm);
        var p = new PATH_INFO[np]; var m = new MODE_INFO[nm];
        int rc = QueryDisplayConfig(1, ref np, p, ref nm, m, IntPtr.Zero);
        if (rc != 0) return "QueryDisplayConfig(ALL) " + rc;
        var active = new List<PATH_INFO>(); var seenTargets = new List<string>();
        for (int i = 0; i < np; i++) {
            string t = p[i].target.adapterId.Low + ":" + p[i].target.id;
            if (p[i].target.targetAvailable == 0 || seenTargets.Contains(t)) continue;
            bool already = (p[i].flags & 1) != 0;
            if (!already) {
                // One source per target on its own adapter; no source shared with an active path.
                bool clash = false;
                foreach (var a in active) if (a.source.adapterId.Low == p[i].source.adapterId.Low && a.source.id == p[i].source.id) clash = true;
                if (clash) continue;
                p[i].flags |= 1; p[i].source.modeInfoIdx = 0xffffffff; p[i].target.modeInfoIdx = 0xffffffff;
            }
            seenTargets.Add(t); active.Add(p[i]);
        }
        var arr = active.ToArray();
        rc = SetDisplayConfig((uint)arr.Length, arr, nm, m, 0x80 | 0x20 | 0x400 | 0x200);
        return "SetDisplayConfig(supplied, " + arr.Length + " paths) " + rc;
    }
    static HEADER Header(int type, Type t, uint low, int high, uint id) {
        var h = new HEADER(); h.type = type; h.size = (uint)Marshal.SizeOf(t); h.adapterId.Low = low; h.adapterId.High = high; h.id = id; return h;
    }
    public static int[] GetScale(uint low, int high, uint id) {
        var g = new DPI_GET(); g.header = Header(-3, typeof(DPI_GET), low, high, id);
        int rc = DisplayConfigGetDeviceInfo(ref g);
        return new int[] { rc, g.minRel, g.curRel, g.maxRel };
    }
    public static int SetScale(uint low, int high, uint id, int rel) {
        var s = new DPI_SET(); s.header = Header(-4, typeof(DPI_SET), low, high, id); s.rel = rel;
        return DisplayConfigSetDeviceInfo(ref s);
    }
}
'@
$scales = 100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500
$width = if ($env:GATE_WIDTH) { [int]$env:GATE_WIDTH } else { 1280 }
$height = if ($env:GATE_HEIGHT) { [int]$env:GATE_HEIGHT } else { 800 }
# The second monitor's mode (default the same): Windows offers 150 % only
# from about 1920x1200 (at 1280x800 the most is 125 %).
$width2 = if ($env:GATE_WIDTH2) { [int]$env:GATE_WIDTH2 } else { $width }
$height2 = if ($env:GATE_HEIGHT2) { [int]$env:GATE_HEIGHT2 } else { $height }
$mode = if ($env:GATE_DISPLAY) { $env:GATE_DISPLAY } else { 'status' }
$result = [ordered]@{ mode = $mode; steps = @() }
$result.before = @([GateDisplay]::Monitors() | ForEach-Object { $_ | ConvertFrom-Json })

function Outputs { @([GateDisplay]::Devices() | Where-Object { $_ -like '*QXL*' } | ForEach-Object { ($_ -split '\|')[0] }) }

if ($mode -eq 'extend') {
    $result.steps += "SetDisplayConfig(EXTEND|APPLY) = $([GateDisplay]::SetDisplayConfig(0, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0x4 -bor 0x80))"
    Start-Sleep -Seconds 3
    if (@([GateDisplay]::Monitors()).Count -lt 2) {
        $result.steps += [GateDisplay]::ActivateAll()
        Start-Sleep -Seconds 3
    }
    $outs = Outputs
    $result.outputs = $outs
    $result.modes = [ordered]@{}
    $primary = ($result.before | Where-Object { $_.primary } | Select-Object -First 1).device
    if (-not $primary) { $primary = $outs[0] }
    $x = 0
    foreach ($dev in @($primary) + @($outs | Where-Object { $_ -ne $primary })) {
        $w, $h = if ($dev -eq $primary) { $width, $height } else { $width2, $height2 }
        $result.modes[$dev] = ([GateDisplay]::Modes($dev) -join ' ')
        $result.steps += "SetMode $dev ${w}x$h at $x,0 = $([GateDisplay]::SetMode($dev, $w, $h, $x, 0))"
        $x += $w
    }
    $result.steps += "apply = $([GateDisplay]::ApplyModes())"
    Start-Sleep -Seconds 3
} elseif ($mode -eq 'detach') {
    $result.steps += "SetDisplayConfig(INTERNAL|APPLY) = $([GateDisplay]::SetDisplayConfig(0, [IntPtr]::Zero, 0, [IntPtr]::Zero, 0x1 -bor 0x80))"
    Start-Sleep -Seconds 3
    if (@([GateDisplay]::Monitors()).Count -gt 1) {
        $second = ([GateDisplay]::Monitors() | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object { -not $_.primary } | Select-Object -First 1).device
        $result.steps += "SetMode $second 0x0 = $([GateDisplay]::SetMode($second, 0, 0, 0, 0))"
        $result.steps += "apply = $([GateDisplay]::ApplyModes())"
        Start-Sleep -Seconds 3
    }
}

if ($env:GATE_SCALE2) {
    $target = [int]$env:GATE_SCALE2
    $src = [GateDisplay]::Sources() | Where-Object { $_[1] -ne 0 -or $_[2] -ne 0 } | Select-Object -First 1
    if (-not $src) { $result.scale2_error = 'no second monitor on the desktop' }
    else {
        $g = [GateDisplay]::GetScale([uint32]$src[3], [int]$src[4], [uint32]$src[5])
        # Relative steps count from the recommended scale; the minimum is 100 %.
        $recommended = -$g[1]
        $want = [Array]::IndexOf($scales, $target)
        $rc = [GateDisplay]::SetScale([uint32]$src[3], [int]$src[4], [uint32]$src[5], $want - $recommended)
        Start-Sleep -Seconds 3
        $after = [GateDisplay]::GetScale([uint32]$src[3], [int]$src[4], [uint32]$src[5])
        $result.scale2 = [ordered]@{ device = $src[0]; target = $target; get_rc = $g[0]; min = $g[1]; cur = $g[2]; max = $g[3]
            recommended = $scales[[Math]::Max(0, $recommended)]; set_rel = $want - $recommended; set_rc = $rc; cur_after = $after[2] }
    }
}
$result.sources = @([GateDisplay]::Sources() | ForEach-Object { "$($_[0]) at $($_[1]),$($_[2]) source $($_[5])" })
$result.devices = @([GateDisplay]::Devices())
$result.monitors = @([GateDisplay]::Monitors() | ForEach-Object { $_ | ConvertFrom-Json })
Write-Output ('DISPLAY_RESULT ' + ($result | ConvertTo-Json -Depth 5 -Compress))
