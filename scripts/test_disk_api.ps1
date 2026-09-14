# SPDX-License-Identifier: BSD-2-Clause
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RomDirectory,
      [string]$BuildDirectory="")
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent $PSScriptRoot
$out=Join-Path $repository ('build/disk-api-tests/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$connection=Join-Path $out 'connection.json'
$ctl=Join-Path $PSScriptRoot 'm88ctl.ps1'
function Image([string]$name,[byte]$protect=0) {
    $bytes=New-Object byte[] 688
    [Text.Encoding]::ASCII.GetBytes($name).CopyTo($bytes,0)
    $bytes[26]=$protect
    [BitConverter]::GetBytes([uint32]688).CopyTo($bytes,28)
    return ,$bytes
}
$multi=Join-Path $out '複数枚.d88'
[byte[]]$data=(Image 'DISK A')+(Image 'DISK B')
[IO.File]::WriteAllBytes($multi,$data)
$protected=Join-Path $out 'protected.d88'
[IO.File]::WriteAllBytes($protected,(Image 'LOCKED' 16))
$bad=Join-Path $out 'bad.d88'
[IO.File]::WriteAllBytes($bad,[byte[]](1,2,3))
$originalHash=(Get-FileHash -LiteralPath $multi).Hash
function Check($condition,[string]$message) { if (!$condition) { throw $message } }
function Request([string]$method,[string]$path,[int]$expected=200,[bool]$authorize=$true) {
    $actual=200; $response=$null
    $requestHeaders=if($authorize){$headers}else{@{}}
    try { $response=Invoke-RestMethod -Method $method -Uri ($url+$path) -Headers $requestHeaders }
    catch {
        if (!$_.Exception.Response) { throw }
        $actual=[int]$_.Exception.Response.StatusCode
    }
    Check ($actual -eq $expected) "$method $path returned $actual; expected $expected"
    return $response
}
& (Join-Path $PSScriptRoot 'start_headless.ps1') -RomDirectory $RomDirectory -BasicMode N88V1H -Port 0 -BuildDirectory $BuildDirectory -ConnectionFile $connection | Out-Null
$info=Get-Content -LiteralPath $connection -Raw | ConvertFrom-Json
$url=$info.url.TrimEnd('/');$headers=@{'X-M88-Token'=$info.token}
try {
    $state=& $ctl disks -ConnectionFile $connection
    Check ($state.disks.Count -eq 2 -and !$state.disks[0].mounted -and !$state.disks[1].mounted) 'initial drives'
    Request GET '/v1/disks' 401 $false | Out-Null
    Request GET '/v1/disk/mount?drive=1' 405 | Out-Null
    Request POST '/v1/disks' 405 | Out-Null
    $path=[Uri]::EscapeDataString($multi)
    foreach($query in @('drive=0','drive=3','drive=-1','drive=1&index=64','drive=1&readonly=2','drive=1&readonly=yes')) {
        Request POST "/v1/disk/mount?$query&path=$path" 400 | Out-Null
    }
    $state=& $ctl disk-mount -Drive 1 -Disk $multi -ConnectionFile $connection
    Check ($state.disks[0].mounted -and $state.disks[0].readonly -and $state.disks[0].image_count -eq 2 -and $state.disks[0].index -eq 0) 'mount defaults and multi image count'
    Check ($state.disks[0].path -eq $multi) 'UTF-8 image path'
    Check ((Request GET '/v1/status').disks[0].mounted) 'general status includes disks'
    Request POST '/v1/disk/mount?drive=1&path=missing.d88' 400 | Out-Null
    Request POST ("/v1/disk/mount?drive=1&path="+[Uri]::EscapeDataString($bad)) 400 | Out-Null
    Request POST "/v1/disk/mount?drive=1&index=2&path=$path" 400 | Out-Null
    Request POST '/v1/disk/select?drive=1' 400 | Out-Null
    Check ((Request GET '/v1/disks').disks[0].index -eq 0) 'failed operations retain media'
    Request POST "/v1/disk/mount?drive=2&index=0&path=$path" 400 | Out-Null
    Request POST "/v1/disk/mount?drive=2&index=1&readonly=0&path=$path" 400 | Out-Null
    $state=& $ctl disk-mount -Drive 2 -Disk $multi -Index 1 -ConnectionFile $connection
    Check ($state.disks[0].mounted -and $state.disks[1].mounted -and $state.disks[1].index -eq 1) 'shared container with distinct images'
    Request POST '/v1/disk/select?drive=1&index=1' 400 | Out-Null
    & $ctl disk-unmount -Drive 2 -ConnectionFile $connection | Out-Null
    $state=& $ctl disk-select -Drive 1 -Index 1 -ConnectionFile $connection
    Check ($state.disks[0].index -eq 1 -and $state.disks[0].readonly) 'select keeps readonly'
    Request POST ('/v1/state/save?path='+[Uri]::EscapeDataString((Join-Path $out 'mounted.state'))) 400 | Out-Null
    & $ctl disk-unmount -Drive 1 -ConnectionFile $connection | Out-Null
    & $ctl disk-unmount -Drive 1 -ConnectionFile $connection | Out-Null
    Request POST '/v1/disk/select?drive=1&index=0' 400 | Out-Null
    $state=& $ctl disk-mount -Drive 1 -Disk $multi -ReadOnly $false -ConnectionFile $connection
    Check (!$state.disks[0].readonly -and !$state.disks[0].requested_readonly) 'explicit writable mount'
    & $ctl disk-unmount -Drive 1 -ConnectionFile $connection | Out-Null
    $state=& $ctl disk-mount -Drive 1 -Disk $protected -ReadOnly $false -ConnectionFile $connection
    Check ($state.disks[0].readonly -and !$state.disks[0].requested_readonly) 'D88 header protection overrides writable request'
    & $ctl disk-unmount -Drive 1 -ConnectionFile $connection | Out-Null
    Request POST '/v1/replay/record/start' | Out-Null
    Request GET '/v1/disks' | Out-Null
    Request POST "/v1/disk/mount?drive=1&path=$path" 409 | Out-Null
    Request POST '/v1/disk/unmount?drive=1' 409 | Out-Null
    Request POST ('/v1/replay/record/stop?path='+[Uri]::EscapeDataString((Join-Path $out 'input.json'))) | Out-Null
    Check ((Get-FileHash -LiteralPath $multi).Hash -eq $originalHash) 'API switching did not alter image bytes'
    Write-Output 'Disk API: two drives, selection, UTF-8 paths, protection, invalid input, auth and recording guards passed.'
} finally { Request POST '/v1/shutdown' | Out-Null }
