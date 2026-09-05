[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RomDirectory,
    [Parameter(Mandatory)][string]$Bin,
    [ValidateSet("N802","N80V2","N","N88V1","N88V1H","N88V2")]
    [string]$BasicMode = "N802",
    [string]$N80Rom = "",
    [string]$Executable = "",
    [UInt16]$Address = 0xC000,
    [switch]$Wait
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
if (-not $Executable) { $Executable = Join-Path $repository "dist/windows-x64/m88m.exe" }
foreach ($path in @($Bin,$Executable)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "File not found: $path" }
}
if (-not (Test-Path -LiteralPath $RomDirectory -PathType Container)) { throw "ROM directory not found: $RomDirectory" }
if ($N80Rom -and $BasicMode -notin @('N802','N80V2')) { throw "-N80Rom applies only to N802/N80V2" }

# The GUI and headless frontends share ROM validation and temporary aliasing.
# This explicitly requested visible emulator is not a background helper.
$names=@('M88M_ROM_DIR','M88M_LOAD_BIN','M88M_LOAD_ADDRESS','M88V_BASIC_MODE','M88V_N80_ROM')
$previous=@{}
foreach ($name in $names) { $previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
try {
    $env:M88M_ROM_DIR = (Resolve-Path -LiteralPath $RomDirectory).Path
    $env:M88M_LOAD_BIN = (Resolve-Path -LiteralPath $Bin).Path
    $env:M88M_LOAD_ADDRESS = ("0x{0:X4}" -f $Address)
    $env:M88V_BASIC_MODE = $BasicMode
    $env:M88V_N80_ROM = $N80Rom
    $process = Start-Process -FilePath (Resolve-Path -LiteralPath $Executable).Path `
        -WorkingDirectory $repository -PassThru
    if ($Wait) {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "m88m.exe exited with code $($process.ExitCode)" }
    }
    $process
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name,$previous[$name],'Process') }
}
