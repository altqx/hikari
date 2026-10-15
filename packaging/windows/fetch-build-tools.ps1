# Puts the tools build-tools.lock pins on PATH for the Windows build:
#
#   fetch-build-tools.ps1 -Downloads <dir> -Tools <dir> -Name NASM,YASM
#
# Each tool's file is taken from -Downloads when it is there with the pinned
# SHA-256 (the CI job caches that folder), and downloaded from its release
# URL otherwise; a file whose SHA-256 differs is never used. It is unpacked
# under -Tools, and the folder holding the executable is added to PATH, and
# to GITHUB_PATH for the job's later steps.
param(
    [Parameter(Mandatory)] [string] $Downloads,
    [Parameter(Mandatory)] [string] $Tools,
    [Parameter(Mandatory)] [ValidateSet('NASM', 'YASM', 'GETTEXT')] [string[]] $Name
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$lock = @{}
foreach ($line in Get-Content (Join-Path $PSScriptRoot 'build-tools.lock')) {
    if ($line -match '^([A-Z0-9_]+)=(.*)$') { $lock[$Matches[1]] = $Matches[2] }
}

function Get-Pinned([string] $tool) {
    $url = $lock["${tool}_URL"]
    $sha = $lock["${tool}_SHA256"]
    if (-not $url -or -not $sha) { throw "build-tools.lock has no ${tool}_URL or ${tool}_SHA256" }
    New-Item -ItemType Directory -Force -Path $Downloads | Out-Null
    $file = Join-Path $Downloads ([IO.Path]::GetFileName(([Uri] $url).AbsolutePath))
    if (Test-Path -LiteralPath $file) {
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash -eq $sha) {
            Write-Host "${tool}: $file from the cache, SHA-256 $sha"
            return $file
        }
        Write-Host "::warning::${tool}: the cached $file does not have the pinned SHA-256; downloading it again"
        Remove-Item -LiteralPath $file
    }
    $part = "$file.part"
    for ($attempt = 1; ; $attempt++) {
        try {
            Invoke-WebRequest -Uri $url -OutFile $part -UseBasicParsing
            break
        }
        catch {
            if ($attempt -ge 5) { throw "${tool}: cannot download ${url}: $_" }
            Write-Host "${tool}: download attempt $attempt of $url failed ($_); trying again"
            Start-Sleep -Seconds (15 * $attempt)
        }
    }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $part).Hash
    if ($hash -ne $sha) {
        Remove-Item -LiteralPath $part
        throw "${tool}: $url has SHA-256 $hash, but build-tools.lock pins $sha"
    }
    Move-Item -LiteralPath $part $file
    Write-Host "${tool}: $file downloaded from $url, SHA-256 $sha"
    return $file
}

foreach ($tool in $Name) {
    $file = Get-Pinned $tool
    $dir = Join-Path $Tools $tool.ToLowerInvariant()
    Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    switch ($tool) {
        'NASM' {
            Expand-Archive -LiteralPath $file -DestinationPath $dir
            $bin = Join-Path $dir "nasm-$($lock['NASM_VERSION'])"
            $exe = 'nasm.exe'
        }
        'YASM' {
            # the build calls it as yasm
            Copy-Item -LiteralPath $file (Join-Path $dir 'yasm.exe')
            $bin = $dir
            $exe = 'yasm.exe'
        }
        'GETTEXT' {
            Expand-Archive -LiteralPath $file -DestinationPath $dir
            $bin = Join-Path $dir 'bin'
            $exe = 'msgfmt.exe'
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $bin $exe))) {
        throw "${tool}: $exe is not in $bin after unpacking $file"
    }
    $env:PATH = "$bin;$env:PATH"
    if ($env:GITHUB_PATH) { $bin | Out-File -Append -Encoding utf8 $env:GITHUB_PATH }
    Write-Host "${tool} $($lock["${tool}_VERSION"]): $(Join-Path $bin $exe)"
}
