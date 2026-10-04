# D1 native gate on Windows (winix task gate-win-uia): the UI Automation view
# of the HikariSub under test, the Windows counterpart of atspi_tool.py json.
# `winix ui tree/find` only reach the first top-level window of a process;
# this walks every top-level window of the app (main window, floating panels,
# popups) and also reports the focused element, the foreground window and
# the cursor. One output line: UIA_JSON_B64 <base64 of UTF-8 JSON>.
#
# GATE_UIA_DO (optional, JSON list) runs actions first, then waits
# GATE_UIA_SETTLE_MS (default 1000) before the view is taken:
#   {"action": "invoke"|"focus"|"select", "name": "...", "type": "Button",
#    "window": "<top-level title>", "all": true, "repeat": 3}
#   {"action": "closewindow", "window": "Move panel"}
# invoke uses Invoke, else Toggle, else SelectionItem. Only elements that
# are not off screen match; "all" acts on every match (else the first).
$ErrorActionPreference = 'Stop'
trap { Write-Output "UIA_ERROR $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber): $($_.InvocationInfo.Line.Trim()))"; exit 1 }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class GateUia {
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
}
'@
# Per-monitor DPI aware (v2): bounds and the cursor in physical pixels on
# every monitor, whatever its scale.
[void][GateUia]::SetProcessDpiAwarenessContext([IntPtr]-4)
[void][GateUia]::SetThreadDpiAwarenessContext([IntPtr]-4)
$A = [System.Windows.Automation.AutomationElement]
$NS = $A::NotSupported
$build = Join-Path $PWD.Path 'out\build\windows-x64-release'
$appPids = @(Get-Process hikarisub -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -and $_.Path.StartsWith($build, [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object { $_.Id })
$maxElements = 5000

$cr = New-Object System.Windows.Automation.CacheRequest
# Properties per element; the tree is walked with the control-view walker.
# (A Subtree cache request fails with "Index was outside the bounds of the
# array" once the main window owns another window, e.g. a floating panel.)
$cr.TreeScope = [System.Windows.Automation.TreeScope]::Element
$walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
foreach ($p in @($A::RuntimeIdProperty, $A::NameProperty, $A::ControlTypeProperty, $A::AutomationIdProperty, $A::BoundingRectangleProperty,
        $A::HasKeyboardFocusProperty, $A::IsKeyboardFocusableProperty, $A::IsEnabledProperty, $A::IsOffscreenProperty,
        $A::IsInvokePatternAvailableProperty, $A::IsValuePatternAvailableProperty, $A::IsSelectionItemPatternAvailableProperty,
        $A::IsTextPatternAvailableProperty, $A::IsTogglePatternAvailableProperty, $A::IsExpandCollapsePatternAvailableProperty,
        $A::NativeWindowHandleProperty,
        [System.Windows.Automation.ValuePattern]::ValueProperty,
        [System.Windows.Automation.SelectionItemPattern]::IsSelectedProperty,
        [System.Windows.Automation.TogglePattern]::ToggleStateProperty,
        [System.Windows.Automation.ExpandCollapsePattern]::ExpandCollapseStateProperty)) { $cr.Add($p) }

function Cached($e, $p) {
    $v = $e.GetCachedPropertyValue($p, $true)
    if ([object]::ReferenceEquals($v, $NS)) { return $null }
    return $v
}

function TypeName($ct) { if ($ct) { $ct.ProgrammaticName -replace '^ControlType\.', '' } else { '' } }

function TopWindows {
    $all = $A::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
    @($all | Where-Object { $appPids -contains $_.Current.ProcessId })
}

function Visit($e, [int]$parent, [int]$depth, [string]$win, $list) {
    if ($list.Count -ge $maxElements) { return }
    # The walker can lead back to an element already seen (an owned window
    # under the main window, the main window's content under it again).
    $rid = ''
    try { $rid = ($e.GetCachedPropertyValue($A::RuntimeIdProperty) -join '.') } catch { }
    if ($rid -and -not $seen.Add($rid)) { return }
    $r = Cached $e $A::BoundingRectangleProperty
    $box = if ($r -and -not $r.IsEmpty) { @([int]$r.X, [int]$r.Y, [int]$r.Width, [int]$r.Height) } else { @(0, 0, 0, 0) }
    $val = Cached $e ([System.Windows.Automation.ValuePattern]::ValueProperty)
    $sel = Cached $e ([System.Windows.Automation.SelectionItemPattern]::IsSelectedProperty)
    $tog = Cached $e ([System.Windows.Automation.TogglePattern]::ToggleStateProperty)
    $exp = Cached $e ([System.Windows.Automation.ExpandCollapsePattern]::ExpandCollapseStateProperty)
    $ctype = TypeName (Cached $e $A::ControlTypeProperty)
    $name = [string](Cached $e $A::NameProperty)
    # Floating panels, the placement window and dialogs are windows owned by
    # the main window: UI Automation puts them under it, not on the desktop.
    if ($ctype -eq 'Window' -and $depth -gt 0 -and $name) { $win = $name }
    $hwv = [int64](Cached $e $A::NativeWindowHandleProperty)
    $data = [ordered]@{
        i = $list.Count; p = $parent; d = $depth; win = $win; hw = $hwv
        t = $ctype; n = $name
        id = [string](Cached $e $A::AutomationIdProperty)
        x = $box[0]; y = $box[1]; w = $box[2]; h = $box[3]
        fo = [bool](Cached $e $A::HasKeyboardFocusProperty); fa = [bool](Cached $e $A::IsKeyboardFocusableProperty)
        en = [bool](Cached $e $A::IsEnabledProperty); off = [bool](Cached $e $A::IsOffscreenProperty)
        inv = [bool](Cached $e $A::IsInvokePatternAvailableProperty)
        val = $(if ($null -ne $val) { [string]$val } else { $null })
        sel = $(if ($null -ne $sel) { [bool]$sel } else { $null })
        tog = $(if ($null -ne $tog) { [string]$tog } else { $null })
        exp = $(if ($null -ne $exp) { [string]$exp } else { $null })
        txt = $null
        dpi = $(if ($ctype -eq 'Window' -and $hwv) { [int][GateUia]::GetDpiForWindow([IntPtr]$hwv) } else { $null })
    }
    [void]$list.Add(@{ el = $e; data = $data; rid = $rid; textAvail = [bool](Cached $e $A::IsTextPatternAvailableProperty) })
    # Qt reports the window's own HWND on Grid rows (DataItem), and UI
    # Automation then lists that window's content under the row again: such
    # an element is kept but not walked into.
    if ($data.hw) {
        if ($ctype -eq 'Window') { [void]$hwnds.Add($data.hw) }
        elseif ($hwnds.Contains($data.hw)) { $data.loop = $true; return }
    }
    $ch = $walker.GetFirstChild($e, $cr)
    while ($ch) {
        Visit $ch $data.i ($depth + 1) $win $list
        $ch = $walker.GetNextSibling($ch, $cr)
    }
}

function Snapshot {
    $list = New-Object System.Collections.ArrayList
    $script:seen = New-Object 'System.Collections.Generic.HashSet[string]'
    $script:hwnds = New-Object 'System.Collections.Generic.HashSet[long]'
    foreach ($w in TopWindows) {
        # A window can go away while it is walked (a closing popup).
        try { $c = $w.GetUpdatedCache($cr); Visit $c -1 0 ([string]$w.Current.Name) $list }
        catch { $script:snapErrors += "$($_.Exception.Message) at $($_.InvocationInfo.ScriptLineNumber)" }
    }
    # Static texts with an accessible name of their own ("Video times") carry
    # their text in the Text pattern; editable texts without Value, too.
    $n = 0
    foreach ($item in $list) {
        $d = $item.data
        if ($n -ge 300 -or $d.off -or -not $item.textAvail -or ($d.t -ne 'Text' -and -not ($d.t -eq 'Edit' -and $null -eq $d.val))) { continue }
        try { $d.txt = $item.el.GetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern).DocumentRange.GetText(4000); $n++ } catch { }
    }
    return $list
}

function DoAction($e, [string]$what) {
    switch ($what) {
        'invoke' {
            if ($e.GetCachedPropertyValue($A::IsInvokePatternAvailableProperty)) {
                $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); return 'Invoke'
            }
            if ($e.GetCachedPropertyValue($A::IsTogglePatternAvailableProperty)) {
                $e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern).Toggle(); return 'Toggle'
            }
            if ($e.GetCachedPropertyValue($A::IsSelectionItemPatternAvailableProperty)) {
                $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); return 'Select'
            }
            throw 'no Invoke, Toggle or SelectionItem pattern'
        }
        'focus' { $e.SetFocus(); return 'SetFocus' }
        'select' { $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); return 'Select' }
        default { throw "unknown action $what" }
    }
}

$snapErrors = @()
$actions = @()
if ($env:GATE_UIA_DO) {
    $doList = @($env:GATE_UIA_DO | ConvertFrom-Json | ForEach-Object { $_ })
    $snap = Snapshot
    foreach ($act in $doList) {
        $res = [ordered]@{ action = $act.action; name = $act.name; type = $act.type; window = $act.window; matches = 0; done = @(); errors = @() }
        if ($act.action -eq 'closewindow') {
            foreach ($h in @($snap | Where-Object { $_.data.t -eq 'Window' -and $_.data.n -eq $act.window })) {
                $res.matches++
                try { $h.el.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern).Close(); $res.done += 'Close' }
                catch { $res.errors += $_.Exception.Message }
            }
        } else {
            $hits = @($snap | Where-Object { $_.data.n -eq $act.name -and -not $_.data.off -and
                    (-not $act.type -or $_.data.t -eq $act.type) -and (-not $act.window -or $_.data.win -eq $act.window) })
            $res.matches = $hits.Count
            if (-not $act.all) { $hits = @($hits | Select-Object -First 1) }
            $repeat = if ($act.repeat) { [int]$act.repeat } else { 1 }
            for ($k = 0; $k -lt $repeat; $k++) {
                foreach ($h in $hits) {
                    try { $res.done += (DoAction $h.el $act.action) } catch { $res.errors += $_.Exception.Message }
                }
                if ($k -lt $repeat - 1) { Start-Sleep -Milliseconds 500 }
            }
        }
        $actions += $res
    }
    $settle = if ($env:GATE_UIA_SETTLE_MS) { [int]$env:GATE_UIA_SETTLE_MS } else { 1000 }
    Start-Sleep -Milliseconds $settle
}

$fg = [GateUia]::GetForegroundWindow()
$fgPid = [uint32]0
[void][GateUia]::GetWindowThreadProcessId($fg, [ref]$fgPid)
$sb = New-Object System.Text.StringBuilder 512
[void][GateUia]::GetWindowText($fg, $sb, 512)
$cursor = New-Object GateUia+POINT
[void][GateUia]::GetCursorPos([ref]$cursor)

$windows = @(foreach ($w in TopWindows) {
        try {
            $c = $w.Current
            $r = $c.BoundingRectangle
            [ordered]@{ name = $c.Name; type = (TypeName $c.ControlType); className = $c.ClassName; pid = $c.ProcessId
                hwnd = $c.NativeWindowHandle; active = ([int64]$c.NativeWindowHandle -eq $fg.ToInt64())
                x = [int]$r.X; y = [int]$r.Y; w = [int]$r.Width; h = [int]$r.Height }
        } catch { }
    })

$list = Snapshot
$focus = $null
try {
    $fe = $A::FocusedElement
    $frid = ($fe.GetRuntimeId() -join '.')
    $match = $list | Where-Object { $_.rid -eq $frid } | Select-Object -First 1
    if ($match) { $focus = [ordered]@{ index = $match.data.i } }
    else {
        $chain = @()
        $x = $fe
        while ($x -and $chain.Count -lt 30 -and -not $x.Equals($A::RootElement)) {
            $chain = @("$(TypeName $x.Current.ControlType):'$($x.Current.Name)'") + $chain
            $x = $walker.GetParent($x)
        }
        $focus = [ordered]@{ index = -1; pid = $fe.Current.ProcessId; path = ($chain -join ' > ') }
    }
} catch { $focus = [ordered]@{ index = -1; error = $_.Exception.Message } }

$out = [ordered]@{
    pids = $appPids
    foreground = [ordered]@{ hwnd = $fg.ToInt64(); pid = $fgPid; title = $sb.ToString() }
    cursor = [ordered]@{ x = $cursor.X; y = $cursor.Y }
    windows = $windows
    elements = @($list | ForEach-Object { $_.data })
    truncated = ($list.Count -ge $maxElements)
    focus = $focus
    actions = $actions
    errors = $snapErrors
}
$json = $out | ConvertTo-Json -Depth 6 -Compress
Write-Output ('UIA_JSON_B64 ' + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json)))
