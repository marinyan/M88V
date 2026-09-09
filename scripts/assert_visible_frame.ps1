# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, marinyan
# Inspect the RGB8 / filter-0 PNG emitted by HeadlessDraw. A successful capture
# or two identical PNG hashes alone must not let black-versus-black tests pass.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Path, [int]$MinimumLitPixels=200)
$ErrorActionPreference='Stop'
[byte[]]$png=[IO.File]::ReadAllBytes($Path)
function ReadU32([int]$offset) {
    [uint32]($png[$offset]*16777216L+$png[$offset+1]*65536L+$png[$offset+2]*256L+$png[$offset+3])
}
if ($png.Length-lt 33 -or [Convert]::ToHexString($png,0,8)-ne '89504E470D0A1A0A' -or
    [Text.Encoding]::ASCII.GetString($png,12,4)-ne 'IHDR' -or (ReadU32 16)-ne 640 -or (ReadU32 20)-ne 400 -or
    $png[24]-ne 8 -or $png[25]-ne 2 -or $png[26]-ne 0 -or $png[27]-ne 0 -or $png[28]-ne 0) {
    throw "Expected a 640x400 noninterlaced RGB8 headless PNG: $Path"
}
$compressed=[IO.MemoryStream]::new()
$raw=[IO.MemoryStream]::new()
try {
    for ($offset=8; $offset+12-le $png.Length;) {
        $size=ReadU32 $offset
        if ([long]$offset+$size+12-gt $png.Length) { throw "Truncated PNG: $Path" }
        if ([Text.Encoding]::ASCII.GetString($png,$offset+4,4)-eq 'IDAT') {
            $compressed.Write($png,$offset+8,$size)
        }
        $offset+=12+$size
    }
    $compressed.Position=0
    $inflate=[IO.Compression.ZLibStream]::new($compressed,[IO.Compression.CompressionMode]::Decompress,$true)
    try { $inflate.CopyTo($raw) } finally { $inflate.Dispose() }
    [byte[]]$pixels=$raw.ToArray()
    if ($pixels.Length-ne 1921*400) { throw "Unexpected decoded PNG length: $Path" }
    $lit=0
    for ($y=0;$y-lt 400;$y++) {
        $row=$y*1921
        if ($pixels[$row]-ne 0) { throw "Expected headless PNG filter 0: $Path" }
        for ($x=0;$x-lt 640;$x++) {
            $p=$row+1+$x*3
            if ($pixels[$p]-or $pixels[$p+1]-or $pixels[$p+2]) { $lit++ }
        }
    }
    if ($lit-lt $MinimumLitPixels) { throw "Blank/sparse framebuffer ($lit lit pixels): $Path" }
    $lit
} finally { $raw.Dispose(); $compressed.Dispose() }
