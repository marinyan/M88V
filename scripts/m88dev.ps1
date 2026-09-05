# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, marinyan
[CmdletBinding()]
param(
    [Parameter(Mandatory,Position=0)]
    [ValidateSet('symbols','configure','clear','profile','region','watch','watch-clear','resume','trace','writer','map','state-save','state-load','record-start','record-stop','replay')]
    [string]$Command,
    [string]$ConnectionFile='', [string]$Path='', [string]$Address='',
    [string]$Begin='', [string]$End='', [string]$Name='', [string]$Space='cpu',
    [int]$Length=1, [int]$History=0, [int]$Last=32, [int]$Top=50,
    [bool]$Profile=$false, [bool]$Writes=$false
)
$ErrorActionPreference='Stop'
if (-not $ConnectionFile) { $ConnectionFile=Join-Path (Split-Path -Parent $PSScriptRoot) '.m88-headless/connection.json' }
$connection=Get-Content -LiteralPath $ConnectionFile -Raw | ConvertFrom-Json
$headers=@{'X-M88-Token'=[string]$connection.token}
$method='POST'; $query=@{}
if($Command-in @('state-save','state-load','record-stop','replay') -and -not $Path) {throw '-Path is required'}
switch ($Command) {
    'symbols' { $endpoint='/v1/symbols'; if ($Path) {$query.path=(Resolve-Path -LiteralPath $Path).Path} else {$method='GET'} }
    'configure' { $endpoint='/v1/debug/config';$query=@{profile=[int]$Profile;history=$History;writes=[int]$Writes} }
    'clear' { $endpoint='/v1/debug/clear' }
    'profile' { $method='GET';$endpoint='/v1/profile';$query.top=$Top }
    'region' { $endpoint='/v1/profile/region';$query=@{name=$Name;begin=$Begin;end=$End} }
    'watch' { $endpoint='/v1/debug/watch';$query=@{address=$Address;length=$Length;space=$Space} }
    'watch-clear' { $endpoint='/v1/debug/watch/clear' }
    'resume' { $endpoint='/v1/debug/resume' }
    'trace' { $method='GET';$endpoint='/v1/debug/trace';$query.last=$Last }
    'writer' { $method='GET';$endpoint='/v1/debug/writer';$query=@{address=$Address;space=$Space} }
    'map' { $method='GET';$endpoint='/v1/map' }
    'state-save' { $endpoint='/v1/state/save';$query.path=[IO.Path]::GetFullPath($Path) }
    'state-load' { $endpoint='/v1/state/load';$query.path=(Resolve-Path -LiteralPath $Path).Path }
    'record-start' { $endpoint='/v1/replay/record/start' }
    'record-stop' { $endpoint='/v1/replay/record/stop';$query.path=[IO.Path]::GetFullPath($Path) }
    'replay' { $endpoint='/v1/replay/play';$query.path=(Resolve-Path -LiteralPath $Path).Path }
}
$encoded=($query.GetEnumerator() | ForEach-Object { [Uri]::EscapeDataString([string]$_.Key)+'='+[Uri]::EscapeDataString([string]$_.Value) }) -join '&'
$uri=([string]$connection.url).TrimEnd('/')+$endpoint
if ($encoded) { $uri+='?'+$encoded }
Invoke-RestMethod -Method $method -Uri $uri -Headers $headers
