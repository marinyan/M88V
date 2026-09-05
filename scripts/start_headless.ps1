[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$RomDirectory,
    [string]$N80Rom = "",
    [ValidateSet("N802","N80V2","N","N88V1","N88V1H","N88V2")]
    [string]$BasicMode = "N802",
    [int]$Port = 8802,
    [string]$BuildDirectory = "",
    [string]$ConnectionFile = ""
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
if (-not $ConnectionFile) { $ConnectionFile = Join-Path $repository ".m88-headless\connection.json" }

$buildDirectories = if ($BuildDirectory) {
    @($BuildDirectory)
} else {
    @(
        (Join-Path $repository "build\headless-msvc"),
        (Join-Path $repository "build\msvc-ninja"),
        (Join-Path $repository "build\headless")
    )
}
$executables = foreach ($directory in $buildDirectories) {
    Join-Path $directory "m88-headless.exe"
    Join-Path $directory "RelWithDebInfo\m88-headless.exe"
    Join-Path $directory "Release\m88-headless.exe"
    Join-Path $directory "Debug\m88-headless.exe"
}
$executable = $executables | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $executable) {
    throw "m88-headless.exe was not found. Run scripts\build_headless.ps1 first."
}
if (-not (Test-Path -LiteralPath $RomDirectory -PathType Container)) {
    throw "ROM directory does not exist: $RomDirectory"
}

$stateDirectory = Split-Path -Parent $ConnectionFile
New-Item -ItemType Directory -Force -Path $stateDirectory | Out-Null
if (Test-Path -LiteralPath $ConnectionFile) { Remove-Item -LiteralPath $ConnectionFile -Force }
$stdout = Join-Path $stateDirectory "server.stdout.log"
$stderr = Join-Path $stateDirectory "server.stderr.log"

$arguments = @(
    "--rom-dir", ('"' + (Resolve-Path -LiteralPath $RomDirectory).Path + '"'),
    "--port", $Port,
    "--connection-file", ('"' + [IO.Path]::GetFullPath($ConnectionFile) + '"')
)
if ($N80Rom) { $arguments += @("--n80-rom", ('"' + $N80Rom + '"')) }
$arguments += @("--basic-mode", $BasicMode.ToLowerInvariant())
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $repository `
    -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru

$deadline = [DateTime]::UtcNow.AddSeconds(10)
while ([DateTime]::UtcNow -lt $deadline -and -not (Test-Path -LiteralPath $ConnectionFile)) {
    if ($process.HasExited) {
        $detail = if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Raw } else { "" }
        throw "m88-headless exited with code $($process.ExitCode). $detail"
    }
    Start-Sleep -Milliseconds 100
}
if (-not (Test-Path -LiteralPath $ConnectionFile)) {
    throw "m88-headless did not create its connection file within 10 seconds."
}

Get-Content -LiteralPath $ConnectionFile -Raw | ConvertFrom-Json
