[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory)]
    [ValidateSet("status", "registers", "reset", "run", "load", "tape", "disks", "disk-mount", "disk-unmount", "disk-select", "serial", "serial-open", "serial-ports", "serial-close", "serial-write", "serial-read", "serial-clear", "key", "release", "capture", "memory", "dump", "shutdown")]
    [string]$Command,
    [string]$ConnectionFile = "",
    [int]$Frames = 1,
    [string]$Bin = "",
    [string]$Tape = "",
    [string]$Disk = "",
    [ValidateRange(1,2)][int]$Drive = 1,
    [ValidateRange(0,63)][int]$Index = 0,
    [bool]$ReadOnly = $true,
    [string]$ComPort = "",
    [ValidateRange(1,4000000)][int]$Baud = 9600,
    [ValidateRange(5,8)][int]$DataBits = 8,
    [ValidateSet("none","odd","even")][string]$Parity = "none",
    [ValidateSet("1","1.5","2")][string]$StopBits = "1",
    [ValidateSet("none","rtscts")][string]$Flow = "none",
    [string]$Hex = "",
    [ValidateRange(1,4096)][int]$MaxBytes = 4096,
    [string]$Address = "C000H",
    [string]$Key = "",
    [bool]$Down = $true,
    [string]$Output = "",
    [ValidateSet("ram", "tvram", "gvram-b", "gvram-r", "gvram-g")]
    [string]$Space = "ram",
    [int]$Length = 256
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
if (-not $ConnectionFile) { $ConnectionFile = Join-Path $repository ".m88-headless\connection.json" }
if (-not (Test-Path -LiteralPath $ConnectionFile -PathType Leaf)) {
    throw "Connection file not found: $ConnectionFile"
}
$connection = Get-Content -LiteralPath $ConnectionFile -Raw | ConvertFrom-Json
$headers = @{ "X-M88-Token" = [string]$connection.token }
$base = ([string]$connection.url).TrimEnd('/')
function Encode([string]$value) { [Uri]::EscapeDataString($value) }
function Invoke-M88([string]$method, [string]$path) {
    Invoke-RestMethod -Method $method -Uri ($base + $path) -Headers $headers
}

switch ($Command) {
    "status"    { Invoke-M88 GET "/v1/status" }
    "registers" { Invoke-M88 GET "/v1/registers" }
    "reset"     { Invoke-M88 POST "/v1/reset" }
    "run"       { Invoke-M88 POST ("/v1/run?frames=" + $Frames) }
    "load" {
        if (-not $Bin) { throw "-Bin is required for load" }
        $absolute = (Resolve-Path -LiteralPath $Bin).Path
        Invoke-M88 POST ("/v1/load-bin?path=" + (Encode $absolute) + "&address=" + (Encode $Address))
    }
    "tape" {
        if (-not $Tape) { throw "-Tape is required for tape" }
        $absolute = (Resolve-Path -LiteralPath $Tape).Path
        Invoke-M88 POST ("/v1/tape/open?path=" + (Encode $absolute))
    }
    "disks" { Invoke-M88 GET "/v1/disks" }
    "serial" { Invoke-M88 GET "/v1/serial/status" }
    "serial-ports" { Invoke-M88 GET "/v1/serial/ports" }
    "serial-open" {
        $query = if ($ComPort) { "?port=$(Encode $ComPort)&baud=$Baud&data_bits=$DataBits&parity=$Parity&stop_bits=$StopBits&flow=$Flow" } else { "" }
        Invoke-M88 POST ("/v1/serial/open"+$query)
    }
    "serial-close" { Invoke-M88 POST "/v1/serial/close" }
    "serial-clear" { Invoke-M88 POST "/v1/serial/clear" }
    "serial-read" { Invoke-M88 POST "/v1/serial/read?max=$MaxBytes" }
    "serial-write" {
        if ($Hex -notmatch '^(?:[0-9a-fA-F]{2}){1,4096}$') { throw '-Hex must contain 1 to 4096 complete hexadecimal bytes' }
        Invoke-M88 POST ("/v1/serial/write?hex="+(Encode $Hex))
    }
    "disk-mount" {
        if (-not $Disk) { throw "-Disk is required for disk-mount" }
        $absolute = (Resolve-Path -LiteralPath $Disk).Path
        $ro = if ($ReadOnly) { 1 } else { 0 }
        Invoke-M88 POST ("/v1/disk/mount?drive=$Drive&index=$Index&readonly=$ro&path=" + (Encode $absolute))
    }
    "disk-unmount" { Invoke-M88 POST "/v1/disk/unmount?drive=$Drive" }
    "disk-select" {
        if (-not $PSBoundParameters.ContainsKey('Index')) { throw "-Index is required for disk-select" }
        Invoke-M88 POST "/v1/disk/select?drive=$Drive&index=$Index"
    }
    "key" {
        if (-not $Key) { throw "-Key is required for key" }
        $gameKeys = @{
            "numpad2" = @(0, 2); "kp2" = @(0, 2)
            "numpad4" = @(0, 4); "kp4" = @(0, 4)
            "numpad6" = @(0, 6); "kp6" = @(0, 6)
            "numpad8" = @(1, 0); "kp8" = @(1, 0)
            "leftshift" = @(8, 6); "left_shift" = @(8, 6)
            "insert" = @(12, 6); "delete" = @(12, 7)
        }
        $normalizedKey = $Key.ToLowerInvariant()
        $downValue = if ($Down) { "1" } else { "0" }
        if ($gameKeys.ContainsKey($normalizedKey)) {
            $position = $gameKeys[$normalizedKey]
            Invoke-M88 POST ("/v1/key?row=" + $position[0] + "&bit=" + $position[1] + "&down=" + $downValue)
        } else {
            Invoke-M88 POST ("/v1/key?name=" + (Encode $Key) + "&down=" + $downValue)
        }
    }
    "release" { Invoke-M88 POST "/v1/keys/release" }
    "capture" {
        if (-not $Output) { $Output = Join-Path $repository ".m88-headless\capture.png" }
        Invoke-WebRequest -Method GET -Uri ($base + "/v1/frame.png") -Headers $headers -OutFile $Output | Out-Null
        Get-Item -LiteralPath $Output
    }
    "memory" {
        Invoke-M88 GET ("/v1/memory?space=" + (Encode $Space) + "&address=" + (Encode $Address) + "&length=" + $Length)
    }
    "dump" {
        if (-not $Output) { $Output = Join-Path $repository ".m88-headless\machine.m88dump" }
        Invoke-WebRequest -Method GET -Uri ($base + "/v1/dump") -Headers $headers -OutFile $Output | Out-Null
        Get-Item -LiteralPath $Output
    }
    "shutdown" { Invoke-M88 POST "/v1/shutdown" }
}
