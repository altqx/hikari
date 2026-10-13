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
exit 0
