# Windows Forms clipboard access needs an STA thread: powershell -STA -File clipboard-image.ps1 <out.png>
param([Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$data = $null
foreach ($i in 1..20) {
    # OpenClipboard fails while another process holds the clipboard.
    try { $data = [System.Windows.Forms.Clipboard]::GetDataObject(); break } catch { Start-Sleep -Milliseconds 250 }
}
if (-not $data) { Write-Output 'no clipboard data'; exit 3 }
Write-Output "formats: $($data.GetFormats() -join ', ')"
$image = [System.Windows.Forms.Clipboard]::GetImage()
if (-not $image) { Write-Output 'no image on the clipboard'; exit 4 }
$image.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "image $($image.Width)x$($image.Height) $($image.PixelFormat) -> $Out"
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
