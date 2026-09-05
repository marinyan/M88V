# SPDX-License-Identifier: BSD-2-Clause
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RomDirectory,
    [Parameter(Mandatory)][string]$BaselineBuildDirectory,
    [string]$CurrentBuildDirectory='', [int]$Frames=3000)
$ErrorActionPreference='Stop'
$out=Join-Path (Split-Path -Parent $PSScriptRoot) ('build/overhead/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$ctl=Join-Path $PSScriptRoot 'm88ctl.ps1'
foreach($mode in @('N802','N88V2')) {
    $sessions=@{}
    try {
        foreach($variant in @('baseline','current')) {
            $connection=Join-Path $out "$mode-$variant-connection.json"
            $directory=if($variant-eq 'baseline'){$BaselineBuildDirectory}else{$CurrentBuildDirectory}
            & (Join-Path $PSScriptRoot 'start_headless.ps1') -RomDirectory $RomDirectory -BasicMode $mode -Port 0 -BuildDirectory $directory -ConnectionFile $connection | Out-Null
            $sessions[$variant]=$connection
            & $ctl run -Frames 180 -ConnectionFile $connection | Out-Null
            $start=if($mode-eq 'N802'){0xC000}else{0xB000}
            [byte[]]$code=@(0xF3,0x21,0x00,0xE0,0x34,0x7E,0x77,0xC3,(($start+4)-band 255),($start-shr 8))
            $bin=Join-Path $out "$mode-$variant.bin";[IO.File]::WriteAllBytes($bin,$code)
            & $ctl load -Bin $bin -Address ('{0:X4}H' -f $start) -ConnectionFile $connection | Out-Null
            & $ctl run -Frames 100 -ConnectionFile $connection | Out-Null
        }
        $samples=@{baseline=@();current=@()}
        for($i=0;$i-lt 5;$i++) {
            $order=if($i%2){@('current','baseline')}else{@('baseline','current')}
            foreach($variant in $order) {
                $timer=[Diagnostics.Stopwatch]::StartNew()
                & $ctl run -Frames $Frames -ConnectionFile $sessions[$variant] | Out-Null
                $timer.Stop();$samples[$variant]+=$timer.Elapsed.TotalMilliseconds
            }
        }
        $base=($samples.baseline|Sort-Object)[2];$current=($samples.current|Sort-Object)[2]
        [pscustomobject]@{mode=$mode;frames=$Frames;baseline_median_ms=[Math]::Round($base,2);current_median_ms=[Math]::Round($current,2);overhead_percent=[Math]::Round(($current/$base-1)*100,2)}
    } finally {foreach($connection in $sessions.Values){& $ctl shutdown -ConnectionFile $connection | Out-Null}}
}
