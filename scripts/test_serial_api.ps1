# SPDX-License-Identifier: BSD-2-Clause
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RomDirectory,
      [string]$BuildDirectory="")
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent $PSScriptRoot
$out=Join-Path $repository ('build/serial-api-tests/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$connection=Join-Path $out 'connection.json'
$ctl=Join-Path $PSScriptRoot 'm88ctl.ps1'
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
    Request GET '/v1/serial/status' 401 $false | Out-Null
    Request GET '/v1/serial/open' 405 | Out-Null
    Request POST '/v1/serial/read' 409 | Out-Null
    $ports=Request GET '/v1/serial/ports'
    Write-Output ("Available COM ports: "+($ports.ports -join ', '))
    foreach($query in @('port=C%3A%5Ctest','port=COM0','port=COM1&baud=0','port=COM1&data_bits=9','port=COM1&parity=bad','port=COM1&stop_bits=1.5&data_bits=8','port=COM99999')) {
        Request POST "/v1/serial/open?$query" 400 | Out-Null
        Check (!(Request GET '/v1/serial/status').serial.connected) 'failed COM open preserves closed state'
    }
    & $ctl serial-open -ConnectionFile $connection | Out-Null
    Request POST '/v1/serial/open?port=COM1' 409 | Out-Null
    Request POST '/v1/tape/rewind' 409 | Out-Null
    Request POST ('/v1/state/save?path='+[Uri]::EscapeDataString((Join-Path $out 'serial.state'))) 400 | Out-Null
    foreach($hex in @('','0','zz','01xz')) { Request POST "/v1/serial/write?hex=$hex" 400 | Out-Null }
    Request POST '/v1/serial/read?max=0' 400 | Out-Null
    $chunk='a5'*4096
    1..16 | ForEach-Object { Request POST "/v1/serial/write?hex=$chunk" | Out-Null }
    Request POST '/v1/serial/write?hex=01' 409 | Out-Null
    Check ((Request GET '/v1/serial/status').serial.rx_pending -eq 65536) 'bounded receive queue'
    & $ctl serial-clear -ConnectionFile $connection | Out-Null
    Check ((Request GET '/v1/serial/status').serial.rx_pending -eq 0) 'clear queue'
    # Reset from any 8251 mode, select 8N1, enable RX/TX, poll and echo.
    [byte[]]$code=0xf3,0xaf,0xd3,0x21,0xd3,0x21,0xd3,0x21,0x3e,0x40,0xd3,0x21,0x3e,0x4e,0xd3,0x21,0x3e,0x05,0xd3,0x21,0xdb,0x21,0xe6,0x02,0x28,0xfa,0xdb,0x20,0xd3,0x20,0xc3,0x14,0xb0
    $bin=Join-Path $out 'echo.bin';[IO.File]::WriteAllBytes($bin,$code)
    & $ctl load -Bin $bin -Address B000H -ConnectionFile $connection | Out-Null
    & $ctl run -Frames 2 -ConnectionFile $connection | Out-Null
    $state=(Request GET '/v1/serial/status').serial
    Check ($state.rx_enabled -and $state.tx_enabled) 'guest enabled 8251'
    $hex=-join (0..255 | ForEach-Object { $_.ToString('x2') })
    & $ctl serial-write -Hex $hex -ConnectionFile $connection | Out-Null
    & $ctl run -Frames 2 -ConnectionFile $connection | Out-Null
    $first=& $ctl serial-read -MaxBytes 17 -ConnectionFile $connection
    $rest=& $ctl serial-read -ConnectionFile $connection
    Check (($first.hex+$rest.hex) -ceq $hex) 'guest binary echo all 256 values and partial read'
    Check ((& $ctl serial-read -ConnectionFile $connection).length -eq 0) 'read consumes output'
    & $ctl serial-close -ConnectionFile $connection | Out-Null
    & $ctl serial-close -ConnectionFile $connection | Out-Null
    Request POST '/v1/replay/record/start' | Out-Null
    Request POST '/v1/serial/open' 409 | Out-Null
    Request POST ('/v1/replay/record/stop?path='+[Uri]::EscapeDataString((Join-Path $out 'input.json'))) | Out-Null
    Write-Output 'Serial API: guest binary echo, queues, auth, validation and exclusivity passed.'
} finally {
    try { & $ctl shutdown -ConnectionFile $connection | Out-Null } catch {}
}
