[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InputBin,

    [Parameter(Mandatory = $true)]
    [string]$OutputT88,

    [string]$OutputCmt = "",

    [UInt16]$LoadAddress = 0xC000
)

$ErrorActionPreference = "Stop"

function Add-UInt16LE {
    param(
        [System.Collections.Generic.List[byte]]$Buffer,
        [UInt16]$Value
    )

    $Buffer.Add([byte]($Value -band 0xFF))
    $Buffer.Add([byte](($Value -shr 8) -band 0xFF))
}

function Add-UInt32LE {
    param(
        [System.Collections.Generic.List[byte]]$Buffer,
        [UInt32]$Value
    )

    for ($shift = 0; $shift -lt 32; $shift += 8) {
        $Buffer.Add([byte](($Value -shr $shift) -band 0xFF))
    }
}

function Add-TimedTag {
    param(
        [System.Collections.Generic.List[byte]]$Buffer,
        [UInt16]$Id,
        [UInt32]$StartTick,
        [UInt32]$DurationTick
    )

    Add-UInt16LE $Buffer $Id
    Add-UInt16LE $Buffer 8
    Add-UInt32LE $Buffer $StartTick
    Add-UInt32LE $Buffer $DurationTick
}

$InputBin = [System.IO.Path]::GetFullPath($InputBin)
$OutputT88 = [System.IO.Path]::GetFullPath($OutputT88)
if (-not $OutputCmt) {
    $OutputCmt = [System.IO.Path]::ChangeExtension($OutputT88, ".cmt")
}
$OutputCmt = [System.IO.Path]::GetFullPath($OutputCmt)
if ($OutputCmt -eq $OutputT88) {
    throw "CMT and T88 output paths must be different."
}
if (-not (Test-Path -LiteralPath $InputBin -PathType Leaf)) {
    throw "Input BIN not found: $InputBin"
}

$binary = [System.IO.File]::ReadAllBytes($InputBin)
if ($binary.Length -eq 0) {
    throw "Input BIN is empty: $InputBin"
}
if ([UInt32]$LoadAddress + [UInt32]$binary.Length -gt 0x10000) {
    throw "BIN does not fit in the 16-bit address space at the requested load address."
}

# PC-8001 monitor machine-code cassette stream. Each checksum is the two's
# complement of the preceding fields, so the complete record sums to zero.
$cmt = [System.Collections.Generic.List[byte]]::new()
$cmt.Add(0x3A)
$addressHigh = [byte](($LoadAddress -shr 8) -band 0xFF)
$addressLow = [byte]($LoadAddress -band 0xFF)
$cmt.Add($addressHigh)
$cmt.Add($addressLow)
$cmt.Add([byte]((-$addressHigh - $addressLow) -band 0xFF))

$offset = 0
while ($offset -lt $binary.Length) {
    $count = [Math]::Min(255, $binary.Length - $offset)
    $cmt.Add(0x3A)
    $cmt.Add([byte]$count)
    $sum = $count
    for ($index = 0; $index -lt $count; $index++) {
        $value = $binary[$offset + $index]
        $cmt.Add($value)
        $sum += $value
    }
    $cmt.Add([byte]((-$sum) -band 0xFF))
    $offset += $count
}

# A zero-length record is the machine-code stream terminator.
$cmt.Add(0x3A)
$cmt.Add(0x00)
$cmt.Add(0x00)

$ticksPerByte = 88              # 600 baud, 8 data bits, no parity, 2 stop bits
$leaderTicks = 2 * 4800         # two-second MARK carrier
$trailerTicks = 2 * 4800
$dataTicks = [UInt32]($cmt.Count * $ticksPerByte)
if ($cmt.Count -gt 32768) {
    throw "CMT stream exceeds the maximum size of one T88 data tag."
}

$t88 = [System.Collections.Generic.List[byte]]::new()
$header = [System.Text.Encoding]::ASCII.GetBytes("PC-8801 Tape Image(T88)`0")
if ($header.Length -ne 24) { throw "Internal T88 header length error." }
$t88.AddRange($header)

# Version 1.00 must be the first tag.
Add-UInt16LE $t88 0x0001
Add-UInt16LE $t88 0x0002
Add-UInt16LE $t88 0x0100

Add-TimedTag $t88 0x0103 0 $leaderTicks

$dataTagLength = 12 + $cmt.Count
Add-UInt16LE $t88 0x0101
Add-UInt16LE $t88 ([UInt16]$dataTagLength)
Add-UInt32LE $t88 $leaderTicks
Add-UInt32LE $t88 $dataTicks
Add-UInt16LE $t88 ([UInt16]$cmt.Count)
Add-UInt16LE $t88 0x00CC
$t88.AddRange($cmt)

$trailerStart = [UInt32]($leaderTicks + $dataTicks)
Add-TimedTag $t88 0x0103 $trailerStart $trailerTicks

Add-UInt16LE $t88 0x0000
Add-UInt16LE $t88 0x0000

foreach ($outputPath in @($OutputCmt, $OutputT88)) {
    $outputDirectory = Split-Path -Parent $outputPath
    if ($outputDirectory) {
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    }
}
[System.IO.File]::WriteAllBytes($OutputCmt, $cmt.ToArray())
[System.IO.File]::WriteAllBytes($OutputT88, $t88.ToArray())

[pscustomobject]@{
    Input = $InputBin
    Output = $OutputT88
    CmtOutput = $OutputCmt
    LoadAddress = ("{0:X4}H" -f $LoadAddress)
    BinaryBytes = $binary.Length
    CmtBytes = $cmt.Count
    T88Bytes = $t88.Count
    Baud = 600
}
