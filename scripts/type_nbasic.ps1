[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [AllowEmptyString()]
    [string[]]$Line,
    [string]$ConnectionFile = "",
    [ValidateRange(1,60)]
    [int]$KeyFrames = 6
)

$ErrorActionPreference = "Stop"
$repository = Split-Path -Parent $PSScriptRoot
if (-not $ConnectionFile) {
    $ConnectionFile = Join-Path $repository ".m88-headless\connection.json"
}
if (-not (Test-Path -LiteralPath $ConnectionFile -PathType Leaf)) {
    throw "Connection file not found: $ConnectionFile"
}
$connection = Get-Content -LiteralPath $ConnectionFile -Raw | ConvertFrom-Json
$headers = @{ "X-M88-Token" = [string]$connection.token }
$base = ([string]$connection.url).TrimEnd('/')

$plain = @{
    ' ' = @(9,6); ':' = @(7,2); ';' = @(7,3); ',' = @(7,4)
    '.' = @(7,5); '/' = @(7,6); '_' = @(7,7); '-' = @(5,7)
    '^' = @(5,6); '[' = @(5,3); '\' = @(5,4); ']' = @(5,5)
    '@' = @(2,0); '*' = @(1,2); '+' = @(1,3); '=' = @(1,4)
}
$shifted = @{
    '!' = @(6,1); '"' = @(6,2); '#' = @(6,3); '$' = @(6,4)
    '%' = @(6,5); '&' = @(6,6); "'" = @(6,7); '(' = @(7,0)
    ')' = @(7,1); '<' = @(7,4); '>' = @(7,5); '?' = @(7,6)
}

function Invoke-Key([int]$Row,[int]$Bit,[bool]$Down) {
    $value = if ($Down) { 1 } else { 0 }
    Invoke-RestMethod -Method Post -Uri "$base/v1/key?row=$Row&bit=$Bit&down=$value" -Headers $headers | Out-Null
}

function Run-Frames([int]$Frames = $KeyFrames) {
    Invoke-RestMethod -Method Post -Uri "$base/v1/run?frames=$Frames" -Headers $headers | Out-Null
}

function Send-MatrixKey([int]$Row,[int]$Bit,[bool]$Shift) {
    if ($Shift) {
        Invoke-Key 8 6 $true
        Run-Frames
    }
    Invoke-Key $Row $Bit $true
    Run-Frames
    Invoke-Key $Row $Bit $false
    Run-Frames
    if ($Shift) {
        Invoke-Key 8 6 $false
        Run-Frames
    }
}

function Send-Character([char]$Character) {
    $text = [string]$Character
    $letterShift = ($Character -ge 'A' -and $Character -le 'Z')
    if ($Character -ge 'a' -and $Character -le 'z') {
        $Character = [char]::ToUpperInvariant($Character)
        $text = [string]$Character
    }
    if ($Character -ge 'A' -and $Character -le 'G') {
        Send-MatrixKey 2 (([int]$Character)-([int][char]'A')+1) $letterShift
    } elseif ($Character -ge 'H' -and $Character -le 'O') {
        Send-MatrixKey 3 (([int]$Character)-([int][char]'H')) $letterShift
    } elseif ($Character -ge 'P' -and $Character -le 'W') {
        Send-MatrixKey 4 (([int]$Character)-([int][char]'P')) $letterShift
    } elseif ($Character -ge 'X' -and $Character -le 'Z') {
        Send-MatrixKey 5 (([int]$Character)-([int][char]'X')) $letterShift
    } elseif ($Character -ge '0' -and $Character -le '7') {
        Send-MatrixKey 6 (([int]$Character)-([int][char]'0')) $false
    } elseif ($Character -eq '8' -or $Character -eq '9') {
        Send-MatrixKey 7 (([int]$Character)-([int][char]'8')) $false
    } elseif ($plain.ContainsKey($text)) {
        $position = $plain[$text]
        Send-MatrixKey $position[0] $position[1] $false
    } elseif ($shifted.ContainsKey($text)) {
        $position = $shifted[$text]
        Send-MatrixKey $position[0] $position[1] $true
    } else {
        throw "Unsupported N-BASIC input character: $text"
    }
}

foreach ($sourceLine in $Line) {
    Run-Frames
    foreach ($character in $sourceLine.ToCharArray()) { Send-Character $character }
    Send-MatrixKey 1 7 $false
}

[pscustomobject]@{
    LinesEntered = $Line.Count
    CharactersEntered = ($Line | ForEach-Object Length | Measure-Object -Sum).Sum
}
