[CmdletBinding()]
param(
    [string]$BuildDirectory = "",
    [ValidateSet("Debug", "RelWithDebInfo", "Release")]
    [string]$Configuration = "RelWithDebInfo",
    [ValidateSet("win32", "raylib")]
    [string]$Frontend = "win32",
    [switch]$NoPublish,
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
    $BuildDirectory = Join-Path $repository $(if ($useMsvc) { "build\gui-$Frontend-msvc" } else { "build\gui-$Frontend" })
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
    "-DM88V_GUI_FRONTEND=$Frontend",
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

$guiTarget = if ($Frontend -eq 'win32') { 'm88_win32' } else { 'm88_raylib' }
& cmake --build $BuildDirectory --config $Configuration --parallel --target $guiTarget
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$executableName = if ($Frontend -eq 'win32') { 'm88v.exe' } else { 'm88v-raylib.exe' }
$builtExecutable = Join-Path $BuildDirectory $executableName
if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
    $builtExecutable = Join-Path $BuildDirectory "$Configuration\$executableName"
}
if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
    throw "Built $executableName was not found under $BuildDirectory"
}
if ($NoPublish) { Get-Item -LiteralPath $builtExecutable; return }

$distributionDirectory = Join-Path $repository "dist\windows-x64"
New-Item -ItemType Directory -Force -Path $distributionDirectory | Out-Null
$distributionExecutable = Join-Path $distributionDirectory $executableName
Copy-Item -LiteralPath $builtExecutable -Destination $distributionExecutable -Force
Get-Item -LiteralPath $distributionExecutable
