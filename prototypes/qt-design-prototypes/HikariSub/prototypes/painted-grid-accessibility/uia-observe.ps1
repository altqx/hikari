param([Parameter(Mandatory=$true)][int]$TargetProcessId, [Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $TargetProcessId)
$window = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)
if ($null -eq $window) { throw 'Study window not found for the explicitly supplied process.' }
$items = [System.Collections.Generic.List[object]]::new()
function Read-StudyElement($element, [int]$depth) {
    if ($depth -gt 4 -or $items.Count -ge 36) { return }
    $patterns = @($element.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName })
    $items.Add([ordered]@{depth=$depth; name=$element.Current.Name; controlType=$element.Current.ControlType.ProgrammaticName; automationId=$element.Current.AutomationId; keyboardFocusable=$element.Current.IsKeyboardFocusable; hasKeyboardFocus=$element.Current.HasKeyboardFocus; patterns=$patterns})
    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    $child = $walker.GetFirstChild($element)
    while ($null -ne $child -and $items.Count -lt 36) {
        Read-StudyElement $child ($depth + 1)
        $child = $walker.GetNextSibling($child)
    }
}
Read-StudyElement $window 0
[ordered]@{observer='Windows .NET UIAutomationClient / ControlViewWalker'; targetProcessId=$TargetProcessId; boundedDepth=4; boundedElementCount=36; elements=$items.ToArray()} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $OutputPath -Encoding utf8
