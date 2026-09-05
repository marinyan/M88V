[CmdletBinding()]
param(
    [string]$BuildDirectory = "",
    [ValidateSet("Debug", "RelWithDebInfo", "Release")]
    [string]$Configuration = "RelWithDebInfo",
    [string[]]$CMakeArguments = @()
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$cmakeCaBundle = "C:/Program Files/Git/mingw64/etc/ssl/certs/ca-bundle.crt"
if (-not $env:CMAKE_TLS_CAINFO -and (Test-Path -LiteralPath $cmakeCaBundle -PathType Leaf)) {
    $env:CMAKE_TLS_CAINFO = $cmakeCaBundle
}
$useMsvc = Test-Path -LiteralPath $vcvars -PathType Leaf
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repository $(if ($useMsvc) { "build\gui-msvc" } else { "build\gui" })
}

if ($useMsvc) {
    $environmentLines = & cmd.exe /d /s /c ('call "' + $vcvars + '" >nul && set')
    if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed with exit code $LASTEXITCODE" }
    foreach ($line in $environmentLines) {
        $equals = $line.IndexOf('=')
        if ($equals -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0, $equals), $line.Substring($equals + 1), 'Process')
        }
    }
    $visualStudioPath = $environmentLines | Where-Object { $_ -match '^(?i:PATH)=' } | Select-Object -First 1
    if (-not $visualStudioPath) { throw "vcvars64.bat did not return PATH" }
    $env:Path = $visualStudioPath.Substring(5)
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "MSVC compiler was not added to PATH"
    }
}

$configure = @(
    "-S", $repository,
    "-B", $BuildDirectory,
    "-DM88M_BUILD_GUI=ON",
    "-DM88M_BUILD_HEADLESS=OFF",
    "-DBUILD_TESTING=OFF",
    "-DCMAKE_BUILD_TYPE=$Configuration"
)
if (Test-Path -LiteralPath $cmakeCaBundle -PathType Leaf) {
    $configure += @("-DCMAKE_TLS_CAINFO=$cmakeCaBundle")
}
if ($useMsvc) { $configure += @("-G", "Ninja") }
$configure += $CMakeArguments
& cmake @configure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build $BuildDirectory --config $Configuration --parallel --target m88_raylib
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$builtExecutable = Join-Path $BuildDirectory "m88m.exe"
if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
    $builtExecutable = Join-Path $BuildDirectory "$Configuration\m88m.exe"
}
if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
    throw "Built m88m.exe was not found under $BuildDirectory"
}

$distributionDirectory = Join-Path $repository "dist\windows-x64"
New-Item -ItemType Directory -Force -Path $distributionDirectory | Out-Null
$distributionExecutable = Join-Path $distributionDirectory "m88m.exe"
Copy-Item -LiteralPath $builtExecutable -Destination $distributionExecutable -Force
Get-Item -LiteralPath $distributionExecutable
