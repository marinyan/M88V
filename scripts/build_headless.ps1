[CmdletBinding()]
param(
    [string]$BuildDirectory = "",
    [ValidateSet("Debug", "RelWithDebInfo", "Release")]
    [string]$Configuration = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
$useMsvc = Test-Path -LiteralPath $vcvars -PathType Leaf
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repository $(if ($useMsvc) { "build\headless-msvc" } else { "build\headless" })
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
    # Some Windows sessions contain both Path and PATH. vcvars updates PATH,
    # while the later mixed-case entry can otherwise overwrite it again.
    $visualStudioPath = $environmentLines | Where-Object { $_ -match '^(?i:Path)=' } | Select-Object -First 1
    if (-not $visualStudioPath) { throw "vcvars64.bat did not return PATH" }
    $env:Path = $visualStudioPath.Substring(5)
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw "MSVC compiler was not added to PATH"
    }
}

$configure = @(
    "-S", $repository,
    "-B", $BuildDirectory,
    "-DM88M_BUILD_GUI=OFF",
    "-DM88M_BUILD_HEADLESS=ON",
    "-DBUILD_TESTING=ON",
    "-DCMAKE_BUILD_TYPE=$Configuration"
)
if ($useMsvc) { $configure += @("-G", "Ninja") }
& cmake @configure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
exit $LASTEXITCODE
