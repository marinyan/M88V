# SPDX-License-Identifier: BSD-2-Clause
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RomDirectory,
    [string[]]$Modes=@('N802','N80V2','N','N88V1','N88V1H','N88V2'))
$ErrorActionPreference='Stop'
$out=Join-Path (Split-Path -Parent $PSScriptRoot) ('build/checkpoint-tests/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$ctl=Join-Path $PSScriptRoot 'm88ctl.ps1';$dev=Join-Path $PSScriptRoot 'm88dev.ps1'
function Digest([string]$tag) {
    $dump=Join-Path $out ($mode+'-'+$tag+'.m88dump')
    & $ctl dump -Output $dump -ConnectionFile $connection | Out-Null
    $png=Join-Path $out ($mode+'-'+$tag+'.png')
    & $ctl capture -Output $png -ConnectionFile $connection | Out-Null
    $registers=& $ctl registers -ConnectionFile $connection
    [IO.File]::WriteAllText((Join-Path $out ($mode+'-'+$tag+'.registers.json')),($registers|ConvertTo-Json -Depth 5))
    return (Get-FileHash -LiteralPath $dump).Hash+(Get-FileHash -LiteralPath $png).Hash+($registers|ConvertTo-Json -Compress -Depth 5)
}
function Inputs {
    & $ctl key -Key numpad4 -ConnectionFile $connection | Out-Null
    & $ctl run -Frames 7 -ConnectionFile $connection | Out-Null
    & $ctl release -ConnectionFile $connection | Out-Null
    & $ctl run -Frames 11 -ConnectionFile $connection | Out-Null
}
foreach ($mode in $Modes) {
    $connection=Join-Path $out "$mode-connection.json"
    & (Join-Path $PSScriptRoot 'start_headless.ps1') -RomDirectory $RomDirectory -BasicMode $mode -Port 0 -ConnectionFile $connection | Out-Null
    try {
        & $ctl run -Frames 180 -ConnectionFile $connection | Out-Null
        # BASIC exercises interrupts and timers; observing must not alter results.
        $boot=Join-Path $out "$mode-boot.m88vstate"
        & $dev state-save -Path $boot -ConnectionFile $connection | Out-Null
        $bootDigest=Digest boot
        & $ctl run -Frames 5 -ConnectionFile $connection | Out-Null
        $unobserved=Digest unobserved
        & $dev state-load -Path $boot -ConnectionFile $connection | Out-Null
        if((Digest bootloaded)-ne $bootDigest){throw "$mode boot restore mismatch"}
        & $dev state-save -Path (Join-Path $out "$mode-boot-loaded.m88vstate") -ConnectionFile $connection | Out-Null
        & $ctl run -Frames 5 -ConnectionFile $connection | Out-Null
        if((Digest bootrepeated)-ne $unobserved){throw "$mode boot continuation mismatch"}
        & $dev state-load -Path $boot -ConnectionFile $connection | Out-Null
        & $dev configure -Profile $true -History 64 -Writes $true -ConnectionFile $connection | Out-Null
        & $ctl run -Frames 5 -ConnectionFile $connection | Out-Null
        if ((Digest observed)-ne $unobserved) {throw "$mode instrumentation changed execution"}
        & $dev configure -ConnectionFile $connection | Out-Null
        $start=if($mode-in @('N802','N80V2')){0xC000}else{0xB000}
        # DI; LD HL,E000; INC (HL); IN A,(0); LD (E001),A; JP loop.
        [byte[]]$code=@(0xF3,0x21,0x00,0xE0,0x34,0xDB,0x00,0x32,0x01,0xE0,0xC3,(($start+4)-band 255),($start-shr 8))
        $bin=Join-Path $out "$mode.bin";[IO.File]::WriteAllBytes($bin,$code)
        & $ctl load -Bin $bin -Address ('{0:X4}H' -f $start) -ConnectionFile $connection | Out-Null
        & $ctl run -Frames 3 -ConnectionFile $connection | Out-Null
        $state=Join-Path $out "$mode.m88vstate"
        & $dev state-save -Path $state -ConnectionFile $connection | Out-Null
        $saved=Digest saved
        Inputs
        $expected=Digest expected
        & $dev state-load -Path $state -ConnectionFile $connection | Out-Null
        & $dev state-save -Path (Join-Path $out "$mode-restored.m88vstate") -ConnectionFile $connection | Out-Null
        if ((Digest restored)-ne $saved) {throw "$mode immediate state restore mismatch"}
        Inputs
        if ((Digest repeated)-ne $expected) {throw "$mode continuation mismatch"}
        & $dev record-start -ConnectionFile $connection | Out-Null
        Inputs
        $recorded=Digest recorded
        $replay=Join-Path $out "$mode.m88replay"
        & $dev record-stop -Path $replay -ConnectionFile $connection | Out-Null
        & $ctl run -Frames 9 -ConnectionFile $connection | Out-Null
        & $dev replay -Path $replay -ConnectionFile $connection | Out-Null
        if ((Digest replayed)-ne $recorded) {throw "$mode recorded input replay mismatch"}
        # A damaged file must fail before touching the running machine.
        $bad=Join-Path $out "$mode-bad.m88vstate"
        $bytes=[IO.File]::ReadAllBytes($state);$bytes[100]=$bytes[100]-bxor 1;[IO.File]::WriteAllBytes($bad,$bytes)
        $rejected=$false
        try { & $dev state-load -Path $bad -ConnectionFile $connection | Out-Null } catch { $rejected=$true }
        if (-not $rejected -or (Digest rejected)-ne $recorded) {throw "$mode corrupt-state handling failed"}
        $wrong=Join-Path $out "$mode-wrong-configuration.m88vstate"
        $bytes=[IO.File]::ReadAllBytes($state);$bytes[24]=$bytes[24]-bxor 1;[IO.File]::WriteAllBytes($wrong,$bytes)
        $rejected=$false
        try { & $dev state-load -Path $wrong -ConnectionFile $connection | Out-Null } catch { $rejected=$true }
        if (-not $rejected -or (Digest mismatch)-ne $recorded) {throw "$mode mismatched-ROM handling failed"}
        & $ctl shutdown -ConnectionFile $connection | Out-Null
        & (Join-Path $PSScriptRoot 'start_headless.ps1') -RomDirectory $RomDirectory -BasicMode $mode -Port 0 -ConnectionFile $connection | Out-Null
        & $dev state-load -Path $state -ConnectionFile $connection | Out-Null
        Inputs
        if ((Digest newprocess)-ne $expected) {throw "$mode cross-process restore mismatch"}
        Write-Host "$mode checkpoint/continuation/replay/corruption/cross-process: PASS"
    } finally { & $ctl shutdown -ConnectionFile $connection | Out-Null }
}
