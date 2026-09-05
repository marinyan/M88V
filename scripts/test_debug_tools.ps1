# SPDX-License-Identifier: BSD-2-Clause
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RomDirectory,
    [string[]]$Modes=@('N802','N80V2','N','N88V1','N88V1H','N88V2'))
$ErrorActionPreference='Stop'
$out=Join-Path (Split-Path -Parent $PSScriptRoot) 'build/debug-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$ctl=Join-Path $PSScriptRoot 'm88ctl.ps1';$dev=Join-Path $PSScriptRoot 'm88dev.ps1'
foreach ($mode in $Modes) {
    $connection=Join-Path $out "$mode-connection.json"
    & (Join-Path $PSScriptRoot 'start_headless.ps1') -RomDirectory $RomDirectory -BasicMode $mode -Port 0 -ConnectionFile $connection | Out-Null
    try {
        & $ctl run -Frames 180 -ConnectionFile $connection | Out-Null
        $start=if($mode-in @('N802','N80V2')){0xC000}else{0xB000}
        [byte[]]$code=@(0xF3,0x01,0x34,0x12,0x3E,0x5A,0x32,0x00,0xE0,0x3E,0xA5,0x32,0x01,0xE0,0x76)
        $bin=Join-Path $out "$mode.bin";[IO.File]::WriteAllBytes($bin,$code)
        $sym=Join-Path $out "$mode.sym"
        [IO.File]::WriteAllText($sym,('START {0:X4}' -f $start)+"`n"+('DONE EQU ${0:X4}' -f ($start+14)))
        & $ctl load -Bin $bin -Address ('{0:X4}H' -f $start) -ConnectionFile $connection | Out-Null
        & $dev symbols -Path $sym -ConnectionFile $connection | Out-Null
        & $dev configure -Profile $true -History 64 -Writes $true -ConnectionFile $connection | Out-Null
        & $dev region -Name body -Begin START -End DONE -ConnectionFile $connection | Out-Null
        & $dev watch -Address E000H -ConnectionFile $connection | Out-Null
        $before=(& $ctl memory -Address E001H -Length 1 -ConnectionFile $connection).hex
        $hit=& $ctl run -Frames 1 -ConnectionFile $connection
        if (-not $hit.debug.stopped -or $hit.registers.pc-ne ($start+9) -or $hit.debug.hit.pc-ne ($start+6)) {throw "$mode watch did not stop at the writing instruction"}
        if ((& $ctl memory -Address E001H -Length 1 -ConnectionFile $connection).hex-ne $before) {throw "$mode executed past the watchpoint"}
        $writer=(& $dev writer -Address E000H -Space ram -ConnectionFile $connection).writer
        if ($writer.pc-ne ($start+6) -or $writer.value-ne 0x5A) {throw "$mode last-writer mismatch"}
        $trace=(& $dev trace -ConnectionFile $connection).trace
        if ($trace[-1].before.pc-ne ($start+6) -or $trace[-1].after.pc-ne ($start+9)) {throw "$mode trace mismatch"}
        $map=& $dev map -ConnectionFile $connection
        $region=$map.regions | Where-Object { $_.start-le $start -and ($_.start+$_.length)-gt $start }
        if ($region.read-ne 'ram' -or $region.write-ne 'ram') {throw "$mode CPU memory map mismatch"}
        & $dev resume -ConnectionFile $connection | Out-Null
        & $ctl run -Frames 1 -ConnectionFile $connection | Out-Null
        $profile=& $dev profile -ConnectionFile $connection
        $body=$profile.regions | Where-Object name -eq body
        if ($body.hits-ne 1 -or ($body.tstates-$body.wait_tstates-$body.idle_tstates)-ne 54) {throw "$mode marker cycle count mismatch: $($body|ConvertTo-Json -Compress)"}
        if ((& $ctl memory -Address E001H -Length 1 -ConnectionFile $connection).hex-ne 'A5') {throw "$mode resume failed"}
        $low=$map.regions|Where-Object start -eq 0|Select-Object -First 1
        if($low.read-notlike 'rom-*' -or $low.write-ne 'ram'){throw "$mode ROM/RAM asymmetric map failed"}
        & $dev watch-clear -ConnectionFile $connection | Out-Null
        & $ctl reset -ConnectionFile $connection | Out-Null
        & $ctl run -Frames 180 -ConnectionFile $connection | Out-Null
        $high=if($mode-in @('N802','N80V2')){0x80}else{0xC0}
        $port=if($mode-eq 'N802'){0x5C}else{0x5E}
        $space=if($mode-eq 'N802'){'gvram-b'}else{'gvram-g'}
        [byte[]]$pushCode=@(0xF3,0x31,0x02,$high,0x01,0x5A,0xA5,0xAF,0xD3,$port,0xC5,0x76)
        [IO.File]::WriteAllBytes($bin,$pushCode)
        & $ctl load -Bin $bin -Address ('{0:X4}H' -f $start) -ConnectionFile $connection | Out-Null
        & $dev watch -Address 0 -Length 2 -Space $space -ConnectionFile $connection | Out-Null
        $hit=& $ctl run -Frames 1 -ConnectionFile $connection
        if(-not $hit.debug.stopped -or $hit.registers.pc-ne ($start+11) -or $hit.registers.sp-ne ($high*256)){throw "$mode GVRAM PUSH stop failed"}
        $mapped=& $dev map -ConnectionFile $connection
        $region=$mapped.regions|Where-Object { $_.start-le ($high*256) -and ($_.start+$_.length)-gt ($high*256) }
        if($region.read-ne $space -or $region.write-ne $space -or $mapped.iff1){throw "$mode switched plane map failed"}
        if((& $ctl memory -Space $space -Address 0 -Length 2 -ConnectionFile $connection).hex-ne '5AA5'){throw "$mode PUSH byte writes incomplete"}
        foreach($offset in @(0,1)) {
            $writer=(& $dev writer -Space $space -Address $offset -ConnectionFile $connection).writer
            if($writer.pc-ne ($start+10)){throw "$mode physical last writer failed"}
        }
        $partial=Join-Path $out ($mode+'-'+[guid]::NewGuid().ToString('N')+'.m88vstate')
        & $dev state-save -Path $partial -ConnectionFile $connection | Out-Null
        & $dev watch-clear -ConnectionFile $connection | Out-Null
        & $ctl run -Frames 1 -ConnectionFile $connection | Out-Null
        $expected=& $ctl registers -ConnectionFile $connection
        & $dev state-load -Path $partial -ConnectionFile $connection | Out-Null
        & $ctl run -Frames 1 -ConnectionFile $connection | Out-Null
        $actual=& $ctl registers -ConnectionFile $connection
        if(($actual|ConvertTo-Json -Compress)-ne ($expected|ConvertTo-Json -Compress)){throw "$mode partial-frame restore failed"}
        Write-Host "$mode symbols/profile/watch/history/map: PASS"
    } finally { & $ctl shutdown -ConnectionFile $connection | Out-Null }
}
