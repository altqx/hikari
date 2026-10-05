# D1 native gate on Windows (winix task gate-win-uia): what `winix ui` does
# not report. Everything else (windows, trees, focus, clicks) goes through
# `winix ui`. One output line: UIA_JSON_B64 <base64 of UTF-8 JSON>.
#   GATE_UIA_DO   (optional, JSON list) actions on named elements of the
#                 HikariSub under test, through UI Automation patterns only
#                 (`winix ui click` falls back to a mouse click, so it cannot
#                 show that a screen reader can press a control):
#                 {"action": "invoke", "name": "...", "type": "Button", "window": "<title>", "all": true, "repeat": 3}
#                 invoke uses Invoke, else Toggle, else SelectionItem and reports which.
#                 Elements off screen do not match; "all" acts on every match (else the first).
#   GATE_UIA_SETTLE_MS  wait after the actions (default 1000)
# It also reports GetDpiForWindow for each window of the app.
$ErrorActionPreference = 'Stop'
trap { Write-Output "UIA_ERROR $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber): $($_.InvocationInfo.Line.Trim()))"; exit 1 }
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class GateUia {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
}
'@
[void][GateUia]::SetProcessDpiAwarenessContext([IntPtr]-4)
$A = [System.Windows.Automation.AutomationElement]
$build = Join-Path $PWD.Path 'out\build\windows-x64-release'
$appPids = @(Get-Process hikarisub -ErrorAction SilentlyContinue | Where-Object {
        $_.Path -and $_.Path.StartsWith($build, [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object { $_.Id })
$tops = @($A::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition) |
        Where-Object { $appPids -contains $_.Current.ProcessId })

# Floating panels and dialogs are owned by the main window, so UI Automation
# nests them under it: windows are looked for below the top-level ones too.
function Windows {
    $cond = New-Object System.Windows.Automation.PropertyCondition($A::ControlTypeProperty, [System.Windows.Automation.ControlType]::Window)
    @($tops) + @($tops | ForEach-Object { $_.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond) })
}

function Matches($act) {
    $conds = @(New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, [string]$act.name))
    if ($act.type) {
        $conds += New-Object System.Windows.Automation.PropertyCondition($A::ControlTypeProperty,
            [System.Windows.Automation.ControlType].GetField($act.type).GetValue($null))
    }
    $conds += New-Object System.Windows.Automation.PropertyCondition($A::IsOffscreenProperty, $false)
    $cond = New-Object System.Windows.Automation.AndCondition([System.Windows.Automation.Condition[]]$conds)
    $roots = if ($act.window) { @(Windows | Where-Object { $_.Current.Name -eq $act.window }) } else { $tops }
    $seen = @{}
    foreach ($r in $roots) {
        foreach ($e in $r.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)) {
            $id = $e.GetRuntimeId() -join '.'
            if (-not $seen.ContainsKey($id)) { $seen[$id] = $true; $e }
        }
    }
}

function Press($e) {
    $p = $null
    if ($e.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$p)) { $p.Invoke(); return 'Invoke' }
    if ($e.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$p)) { $p.Toggle(); return 'Toggle' }
    if ($e.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$p)) { $p.Select(); return 'Select' }
    throw 'no Invoke, Toggle or SelectionItem pattern'
}

$actions = @()
if ($env:GATE_UIA_DO) {
    foreach ($act in @($env:GATE_UIA_DO | ConvertFrom-Json | ForEach-Object { $_ })) {
        $res = [ordered]@{ action = $act.action; name = $act.name; type = $act.type; window = $act.window; matches = 0; done = @(); errors = @() }
        $hits = @(Matches $act)
        $res.matches = $hits.Count
        if (-not $act.all) { $hits = @($hits | Select-Object -First 1) }
        $repeat = if ($act.repeat) { [int]$act.repeat } else { 1 }
        for ($k = 0; $k -lt $repeat; $k++) {
            foreach ($h in $hits) { try { $res.done += (Press $h) } catch { $res.errors += $_.Exception.Message } }
            if ($k -lt $repeat - 1) { Start-Sleep -Milliseconds 500 }
        }
        $actions += $res
    }
    $settle = if ($env:GATE_UIA_SETTLE_MS) { [int]$env:GATE_UIA_SETTLE_MS } else { 1000 }
    Start-Sleep -Milliseconds $settle
}

$dpi = @(Windows | ForEach-Object {
        $h = [int64]$_.Current.NativeWindowHandle
        if ($h) { [ordered]@{ hwnd = $h; name = $_.Current.Name; dpi = [int][GateUia]::GetDpiForWindow([IntPtr]$h) } }
    })
$json = [ordered]@{ pids = $appPids; actions = $actions; windowDpi = $dpi } | ConvertTo-Json -Depth 5 -Compress
Write-Output ('UIA_JSON_B64 ' + [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json)))
