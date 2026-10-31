# sinks.ps1 media | pick | remove <device id> | heard <device id>, for the winix audio-removal task.
# remove waits for tools/winix/hotplug.sh to unplug the USB device on its marker; heard cannot listen.
param([string]$Command, [string]$Id)
$usb = Get-PnpDevice -Class AudioEndpoint -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object FriendlyName -Match '\((\d+- )?Audio Device\)' | Select-Object -First 1
switch ($Command) {
    'media' { $env:AUDIO_REMOVAL_MEDIA }
    'pick' { if (-not $usb) { exit 1 }; $usb.InstanceId -replace '^SWD\\MMDEVAPI\\', '' }
    'remove' {
        [Console]::Error.WriteLine('HOTPLUG-READY')
        for ($i = 0; $i -lt 120; $i++) {
            if (-not (Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object InstanceId -Like "*$Id*")) { exit 0 }
            Start-Sleep -Milliseconds 500
        }
        exit 1
    }
    'heard' { exit 0 }
    default { exit 2 }
}
