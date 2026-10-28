# Reads the image on the Windows clipboard from a process of its own and
# saves it as PNG: what another program pasting it gets. Used by
# hikari_ui_shell_tests videoSnapshotsReachTheSystemClipboard (V4, winix task
# v4-clipboard-window). Prints the clipboard's formats and the image's size.
#   powershell -NoProfile -STA -File clipboard-image.ps1 <out.png>
param([Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$data = $null
foreach ($i in 1..20) {
    # The owner may still be setting it; OpenClipboard fails while another process holds it.
    try { $data = [System.Windows.Forms.Clipboard]::GetDataObject(); break } catch { Start-Sleep -Milliseconds 250 }
}
if (-not $data) { Write-Output 'no clipboard data'; exit 3 }
Write-Output "formats: $($data.GetFormats() -join ', ')"
$image = [System.Windows.Forms.Clipboard]::GetImage()
if (-not $image) { Write-Output 'no image on the clipboard'; exit 4 }
$image.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "image $($image.Width)x$($image.Height) $($image.PixelFormat) -> $Out"
# The other renditions the owner offered, beside it for a comparison: the PNG
# format's bytes as they are (<out>.PNG.png) and the raw CF_DIBV5 / CF_DIB
# headers (their first 124 / 40 bytes, hex).
foreach ($f in 'PNG', 'image/png') {
    if ($data.GetDataPresent($f)) {
        $s = $data.GetData($f)
        if ($s -is [IO.MemoryStream]) { [IO.File]::WriteAllBytes("$Out.PNG.png", $s.ToArray()); Write-Output "format $f $($s.Length) bytes -> $Out.PNG.png"; break }
    }
}
foreach ($f in 'Format17', 'DeviceIndependentBitmap') {
    if ($data.GetDataPresent($f)) {
        $s = $data.GetData($f)
        if ($s -is [IO.MemoryStream]) {
            $b = $s.ToArray(); $n = [Math]::Min($b.Length, 124)
            Write-Output "format $f $($b.Length) bytes, header $(($b[0..($n - 1)] | ForEach-Object { $_.ToString('x2') }) -join '')"
        }
    }
}
exit 0
