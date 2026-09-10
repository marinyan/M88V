param (
    [string]$Action = "",
    [string]$Architecture = "x64"
)

# M88V Windows Build Script (PowerShell)
# Requirement: Visual Studio 2022 or later, and CMake

$BuildDir = "build"
$Config = "RelWithDebInfo"

if ($Action -eq "clean") {
    Write-Host "Cleaning build directory..."
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
    exit 0
}

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Write-Host "Configuring project..."
# Let CMake auto-detect the installed Visual Studio generator (VS 2022 / VS 2026 / etc.).
# GitHub's windows-2025 runner now ships VS 2026, so a hardcoded "Visual Studio 17 2022"
# generator fails with "could not find any instance of Visual Studio".
cmake -S . -B $BuildDir -A $Architecture -DM88V_GUI_FRONTEND=win32
if ($LASTEXITCODE -ne 0) {
    Write-Error "Configuration failed."
    exit $LASTEXITCODE
}

Write-Host "Building project..."
cmake --build $BuildDir --config $Config --target m88_win32
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed."
    exit $LASTEXITCODE
}

Write-Host "`nBuild successful!"
Write-Host "Executable is located at: $BuildDir\$Config\m88v.exe`n"

if ($Action -eq "run") {
    Write-Host "Starting m88v.exe..."
    Push-Location "$BuildDir\$Config"
    try {
        .\m88v.exe
    } finally {
        Pop-Location
    }
}
