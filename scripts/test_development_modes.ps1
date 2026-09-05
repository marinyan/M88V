# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, marinyan
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RomDirectory,
    [ValidateSet('N802','N80V2','N','N88V1','N88V1H','N88V2')]
    [string[]]$Modes=@('N802','N80V2','N','N88V1','N88V1H','N88V2'),
    [string]$BuildDirectory='',
    [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $repository 'build/mode-tests' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$client=Join-Path $PSScriptRoot 'm88ctl.ps1'
$results=@()
foreach ($mode in $Modes) {
    $connectionPath=Join-Path $OutputDirectory "$mode-connection.json"
    $start=@{RomDirectory=$RomDirectory; BasicMode=$mode; Port=0; ConnectionFile=$connectionPath}
    if ($BuildDirectory) { $start.BuildDirectory=$BuildDirectory }
    $connection=& (Join-Path $PSScriptRoot 'start_headless.ps1') @start
    $headers=@{'X-M88-Token'=[string]$connection.token}
    $url=[string]$connection.url
    try {
        $status=& $client status -ConnectionFile $connectionPath
        $expectedMachine=if ($mode-eq 'N802') {'PC-8001mkII'} elseif ($mode-eq 'N80V2') {'PC-8001mkIISR'} else {'PC-8801'}
        if ($status.mode-ne $mode -or $status.machine-ne $expectedMachine -or $status.frames-ne 0) { throw "$mode identity mismatch" }
        $health=Invoke-RestMethod -Uri "$url/health"
        if (-not $health.ok) { throw "$mode health failed" }
        $denied=Invoke-WebRequest -Uri "$url/v1/status" -SkipHttpErrorCheck
        if ([int]$denied.StatusCode-ne 401) { throw "$mode accepted an unauthenticated API request" }
        & $client run -Frames 180 -ConnectionFile $connectionPath | Out-Null
        # N88 BASIC asks how many files; a blank Enter accepts the default.
        & (Join-Path $PSScriptRoot 'type_nbasic.ps1') -ConnectionFile $connectionPath -Line '' | Out-Null
        & $client run -Frames 90 -ConnectionFile $connectionPath | Out-Null
        & (Join-Path $PSScriptRoot 'type_nbasic.ps1') -ConnectionFile $connectionPath -Line 'PRINT "M88V":POKE &HE010,90' | Out-Null
        $poke=& $client memory -Address E010H -Length 1 -ConnectionFile $connectionPath
        & $client capture -Output (Join-Path $OutputDirectory "$mode-basic.png") -ConnectionFile $connectionPath | Out-Null
        if ($poke.hex-ne '5A') { throw "$mode BASIC keyboard/POKE test failed ($($poke.hex))" }

        & $client reset -ConnectionFile $connectionPath | Out-Null
        & $client run -Frames 180 -ConnectionFile $connectionPath | Out-Null
        $isPC80=$mode-in @('N802','N80V2')
        $codeAddress=if ($isPC80) {0xC000} else {0xB000}
        $gvramHigh=if ($isPC80) {0x80} else {0xC0}
        # Keep executing outside the selected GVRAM window; no stack accesses
        # while any graphics plane is mapped. Test both CPU and physical views.
        [byte[]]$probe=@(
            0xF3,0x3E,0x11,0x32,0x00,0xE0,
            0xDB,0x00,0x32,0x01,0xE0,
            0x21,0x00,$gvramHigh,
            0xAF,0xD3,0x5C,0x36,0x55,
            0xAF,0xD3,0x5D,0x36,0x66,
            0xAF,0xD3,0x5E,0x36,0x77,
            0xAF,0xD3,0x5F,
            0x3E,0x12,0x32,0x00,0xE0,
            0x01,0x34,0x12,0x11,0x78,0x56,0x21,0xBC,0x9A,
            0x76,0x18,0xFD
        )
        $probePath=Join-Path $OutputDirectory "$mode-probe.bin"
        [IO.File]::WriteAllBytes($probePath,$probe)
        & $client key -Key numpad2 -Down $true -ConnectionFile $connectionPath | Out-Null
        & $client load -Bin $probePath -Address ('{0:X4}H' -f $codeAddress) -ConnectionFile $connectionPath | Out-Null
        $run=& $client run -Frames 2 -ConnectionFile $connectionPath
        $bytes=& $client memory -Address E000H -Length 2 -ConnectionFile $connectionPath
        if ($bytes.hex-ne '12FB') { throw "$mode BIN execution or keyboard matrix failed ($($bytes.hex))" }
        $reg=(& $client registers -ConnectionFile $connectionPath).registers
        if ($reg.bc-ne 0x1234 -or $reg.de-ne 0x5678 -or $reg.hl-ne 0x9ABC) { throw "$mode register inspection failed" }
        # N802 has one packed graphics bank: selecting 5D/5E is ignored,
        # so all three writes land in the B bank. SR/88 have three planes.
        $planes=@(@('gvram-b','55'),@('gvram-r','66'),@('gvram-g','77'))
        if ($mode-eq 'N802') { $planes=,@('gvram-b','77') }
        foreach ($plane in $planes) {
            $pixel=& $client memory -Space $plane[0] -Address 0 -Length 1 -ConnectionFile $connectionPath
            if ($pixel.hex-ne $plane[1]) { throw "$mode $($plane[0]) failed ($($pixel.hex))" }
        }
        $zero=& $client run -Frames 0 -ConnectionFile $connectionPath
        if ($zero.frames-ne $run.frames) { throw "$mode advanced without a requested frame" }
        $invalid=Invoke-WebRequest -Method Post -Headers $headers -Uri "$url/v1/run?frames=100001" -SkipHttpErrorCheck
        if ([int]$invalid.StatusCode-ne 400) { throw "$mode accepted an invalid frame count" }
        $dumpPath=Join-Path $OutputDirectory "$mode.m88dump"
        & $client dump -Output $dumpPath -ConnectionFile $connectionPath | Out-Null
        [byte[]]$dump=[IO.File]::ReadAllBytes($dumpPath)
        if ($dump.Length-ne 118832 -or [Text.Encoding]::ASCII.GetString($dump,0,7)-ne 'M88DMP1') { throw "$mode dump format failed" }
        foreach ($region in @(@('ram',48,65536),@('tvram',65584,4096),@('gvram-b',69680,16384),@('gvram-r',86064,16384),@('gvram-g',102448,16384))) {
            $memory=& $client memory -Space $region[0] -Address 0 -Length $region[2] -ConnectionFile $connectionPath
            $hex=[Convert]::ToHexString($dump,[int]$region[1],[int]$region[2])
            if ($hex-ne $memory.hex) { throw "$mode dump differs from $($region[0]) inspection" }
        }
        $tapePath=Join-Path $OutputDirectory "$mode-probe.t88"
        & (Join-Path $PSScriptRoot 'bin_to_t88.ps1') -InputBin $probePath -OutputT88 $tapePath -LoadAddress $codeAddress | Out-Null
        $tape=& $client tape -Tape $tapePath -ConnectionFile $connectionPath
        if (-not $tape.ok) { throw "$mode T88 open failed" }
        $reset=& $client reset -ConnectionFile $connectionPath
        if ($reset.mode-ne $mode -or $reset.frames-ne 0) { throw "$mode reset lost its selected profile" }
        $results += [pscustomobject]@{Mode=$mode;Machine=$expectedMachine;Basic='PASS';Bin='PASS';Keyboard='PASS';Registers='PASS';Gvram='PASS';Dump='PASS';TapeOpen='PASS';Auth='PASS'}
        Write-Host "$mode development API: PASS"
    } finally {
        & $client shutdown -ConnectionFile $connectionPath | Out-Null
    }
}
$results | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'results.csv') -NoTypeInformation
$results
