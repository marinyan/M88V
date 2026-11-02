# SPDX-License-Identifier: BSD-2-Clause
[CmdletBinding()]
param([Parameter(Mandatory)][string]$RomDirectory,
      [string]$BuildDirectory="", [string]$FFmpeg="", [string]$FFprobe="")
$ErrorActionPreference='Stop'
$repository=Split-Path -Parent $PSScriptRoot
$out=Join-Path $repository ('build/media-api-tests/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$connection=Join-Path $out 'connection.json'
$ctl=Join-Path $PSScriptRoot 'm88ctl.ps1'
function Check($condition,[string]$message) { if (!$condition) { throw $message } }
function Request([string]$method,[string]$path,[int]$expected=200,[bool]$authorize=$true) {
    $actual=200; $response=$null
    $requestHeaders=if($authorize){$headers}else{@{}}
    try { $response=Invoke-RestMethod -Method $method -Uri ($url+$path) -Headers $requestHeaders -TimeoutSec 60 }
    catch { if (!$_.Exception.Response) { throw }; $actual=[int]$_.Exception.Response.StatusCode }
    Check ($actual -eq $expected) "$method $path returned $actual; expected $expected"
    return $response
}
function Encode([string]$path) { [Uri]::EscapeDataString($path) }
function Hash([byte[]]$data) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try { [Convert]::ToBase64String($sha.ComputeHash($data)) } finally { $sha.Dispose() }
}
& (Join-Path $PSScriptRoot 'start_headless.ps1') -RomDirectory $RomDirectory -BasicMode N88V1H -Port 0 -BuildDirectory $BuildDirectory -ConnectionFile $connection | Out-Null
$info=Get-Content -LiteralPath $connection -Raw | ConvertFrom-Json
$url=$info.url.TrimEnd('/');$headers=@{'X-M88-Token'=$info.token};$shutdown=$false
try {
    $caps=& $ctl capabilities -ConnectionFile $connection
    Check ($caps.features -contains 'disk-mount' -and $caps.features -contains 'video-recording') 'capabilities'
    Request GET '/v1/capabilities' 401 $false | Out-Null
    Request POST '/v1/capabilities' 405 | Out-Null
    if($env:OS -eq 'Windows_NT') { Request POST '/v1/audio/start?path=%FF.wav' 400 | Out-Null }
    foreach($kind in 'audio','video') {
        Request GET "/v1/$kind/status" 401 $false | Out-Null
        Request POST "/v1/$kind/status" 405 | Out-Null
        Request GET "/v1/$kind/start" 405 | Out-Null
        Request POST "/v1/$kind/start" 400 | Out-Null
        Request POST "/v1/$kind/stop" 409 | Out-Null
    }
    & $ctl run -Frames 180 -ConnectionFile $connection | Out-Null
    # Executed by the emulated CPU: SSG tone, BEEP and HALT. Keep the ROM's screen.
    $program=New-Object 'Collections.Generic.List[byte]'
    $program.Add(0xf3)
    function Port([byte]$p,[byte]$v) { $program.AddRange([byte[]](0x3e,$v,0xd3,$p)) }
    function Reg([byte]$r,[byte]$v) { Port 0x44 $r; Port 0x45 $v }
    Reg 0 0x40; Reg 1 1; Reg 7 0x3e; Reg 8 12; Port 0x40 0x20; $program.Add(0x76)
    $bin=Join-Path $out 'tone.bin';[IO.File]::WriteAllBytes($bin,$program.ToArray())
    & $ctl load -Bin $bin -Address C000H -ConnectionFile $connection | Out-Null
    & $ctl run -Frames 2 -ConnectionFile $connection | Out-Null
    $state=Join-Path $out 'start.m88vstate'
    Request POST ('/v1/state/save?path='+(Encode $state)) | Out-Null
    $wav=Join-Path $out '音声.wav';$avi=Join-Path $out '映像.avi'
    & $ctl audio-start -Output $wav -ConnectionFile $connection | Out-Null
    & $ctl video-start -Output $avi -ConnectionFile $connection | Out-Null
    foreach($kind in 'audio','video') { Request POST "/v1/$kind/start" 409 | Out-Null }
    foreach($path in '/v1/reset','/v1/state/load','/v1/replay/play') { Request POST $path 409 | Out-Null }
    $flags=(Request GET '/v1/status').media_recording
    Check ($flags.audio -and $flags.video) 'general status'
    Start-Sleep -Milliseconds 150
    Check ((Request GET '/v1/audio/status').samples -eq 0) 'wall clock must not advance recording'
    # Recording is compatible with disks in both drives (checkpoints are separate).
    foreach($drive in 1,2) {
        $disk=Join-Path $out "disk$drive.d88";$data=New-Object byte[] 688
        [BitConverter]::GetBytes([uint32]688).CopyTo($data,28);[IO.File]::WriteAllBytes($disk,$data)
        & $ctl disk-mount -Drive $drive -Disk $disk -ConnectionFile $connection | Out-Null
    }
    & $ctl run -Frames 90 -ConnectionFile $connection | Out-Null
    & $ctl capture -Output (Join-Path $out 'frame.png') -ConnectionFile $connection | Out-Null
    $audio=& $ctl audio-stop -ConnectionFile $connection
    $video=& $ctl video-stop -ConnectionFile $connection
    Check (!$audio.active -and !$video.active -and $audio.samples -eq $video.samples) 'stop and matching audio duration'
    Check ($video.frames -eq [Math]::Ceiling($audio.samples/800)) 'video frame count'
    Check ($audio.path -eq $wav -and $video.path -eq $avi) 'UTF-8 paths'
    $bytes=[IO.File]::ReadAllBytes($wav);$pcm=[byte[]]$bytes[44..($bytes.Length-1)]
    Check ($bytes.Length -eq 44+4*$audio.samples -and [BitConverter]::ToUInt32($bytes,24) -eq 48000) 'PCM WAV size/rate'
    Check (($pcm | Where-Object {$_ -ne 0} | Select-Object -First 1) -ne $null) 'audible signal'
    $wavHash=(Get-FileHash -LiteralPath $wav).Hash
    Request POST ('/v1/audio/start?path='+(Encode $wav)) 400 | Out-Null
    Check ((Get-FileHash -LiteralPath $wav).Hash -eq $wavHash) 'existing WAV not overwritten'
    Request POST ('/v1/video/start?path='+(Encode (Join-Path $out 'bad.mp4'))) 400 | Out-Null
    Request POST ('/v1/audio/start?path='+(Encode (Join-Path $out 'missing/out.wav'))) 400 | Out-Null
    foreach($drive in 1,2) { & $ctl disk-unmount -Drive $drive -ConnectionFile $connection | Out-Null }
    Request POST ('/v1/state/load?path='+(Encode $state)) | Out-Null
    $second=Join-Path $out 'split.wav'
    & $ctl audio-start -Output $second -ConnectionFile $connection | Out-Null
    foreach($i in 1..9) { & $ctl run -Frames 10 -ConnectionFile $connection | Out-Null }
    & $ctl audio-stop -ConnectionFile $connection | Out-Null
    Check ((Get-FileHash -LiteralPath $second).Hash -eq $wavHash) 'checkpoint restore / request batching changed PCM'
    & $ctl reset -ConnectionFile $connection | Out-Null
    $empty=Join-Path $out 'empty.wav'
    & $ctl audio-start -Output $empty -ConnectionFile $connection | Out-Null
    & $ctl audio-stop -ConnectionFile $connection | Out-Null
    Check ((Get-Item -LiteralPath $empty).Length -eq 44) 'empty capture'
    $final=Join-Path $out 'shutdown.avi'
    & $ctl video-start -Output $final -ConnectionFile $connection | Out-Null
    & $ctl run -Frames 2 -ConnectionFile $connection | Out-Null
    & $ctl shutdown -ConnectionFile $connection | Out-Null;$shutdown=$true
    $data=[IO.File]::ReadAllBytes($final)
    Check ([BitConverter]::ToUInt32($data,4)+8 -eq $data.Length) 'shutdown finalizes RIFF'
    if($FFprobe) {
        $probe=(& $FFprobe -v error -show_streams -of json $avi | Out-String | ConvertFrom-Json)
        Check ($LASTEXITCODE -eq 0) 'ffprobe failed'
        $v=$probe.streams | Where-Object codec_type -eq video
        $a=$probe.streams | Where-Object codec_type -eq audio
        Check ($v.codec_name -eq 'png' -and $v.width -eq 640 -and $v.height -eq 400 -and $v.r_frame_rate -eq '60/1') 'external video metadata'
        Check ($a.codec_name -eq 'pcm_s16le' -and $a.sample_rate -eq 48000 -and $a.channels -eq 2) 'external audio metadata'
    }
    if($FFmpeg) {
        $raw=Join-Path $out 'decoded.pcm'
        & $FFmpeg -v error -i $avi -map 0:a:0 -f s16le $raw
        Check ($LASTEXITCODE -eq 0 -and (Hash ([IO.File]::ReadAllBytes($raw))) -eq (Hash $pcm)) 'decoded AVI audio differs from WAV'
        & $FFmpeg -v error -i $avi -f null -
        Check ($LASTEXITCODE -eq 0) 'AVI full decode'
        $frame=Join-Path $out 'last.rgb';$reference=Join-Path $out 'reference.rgb'
        & $FFmpeg -v error -i $avi -vf "select=eq(n\,$($video.frames-1))" -frames:v 1 -pix_fmt rgb24 -f rawvideo $frame
        Check ($LASTEXITCODE -eq 0) 'last frame decode'
        & $FFmpeg -v error -i (Join-Path $out 'frame.png') -frames:v 1 -pix_fmt rgb24 -f rawvideo $reference
        Check ($LASTEXITCODE -eq 0 -and (Get-FileHash -LiteralPath $frame).Hash -eq (Get-FileHash -LiteralPath $reference).Hash) 'AVI framebuffer pixels'
    }
    Write-Output "Media API passed: auth, lifecycle, WAV/AVI, emulated time, disks, replayable PCM, UTF-8 paths, no overwrite, shutdown. Outputs: $out"
} finally { if(!$shutdown) { Request POST '/v1/shutdown' | Out-Null } }
