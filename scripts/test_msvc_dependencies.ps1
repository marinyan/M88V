# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, marinyan
[CmdletBinding()]
param([Parameter(Mandatory)][string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cache = Join-Path $build 'CMakeCache.txt'
$ninja = (Select-String -LiteralPath $cache -Pattern '^CMAKE_MAKE_PROGRAM:FILEPATH=').Line.Split('=', 2)[1]
$object = 'CMakeFiles/m88_win32.dir/src/win32/main.cpp.obj'
$dependencies = (& $ninja -C $build -t deps $object) -join "`n"
if ($LASTEXITCODE -ne 0 -or $dependencies -notmatch 'src/win32/ui\.h' -or
    $dependencies -notmatch 'src/common/critsect\.h') {
    throw 'MSVC header dependencies are missing; do not distribute this incremental build.'
}
$header = Join-Path (Split-Path -Parent $PSScriptRoot) 'src/win32/ui.h'
$stamp = (Get-Item -LiteralPath $header).LastWriteTimeUtc
try {
    # Touch only the timestamp and dry-run the build; never change the source.
    (Get-Item -LiteralPath $header).LastWriteTimeUtc = [DateTime]::UtcNow.AddSeconds(2)
    $plan = (& $ninja -C $build -n m88_win32) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $plan -notmatch 'main\.cpp\.obj') {
        throw "Changing ui.h did not schedule main.cpp for recompilation. Ninja plan:`n$plan"
    }
} finally {
    (Get-Item -LiteralPath $header).LastWriteTimeUtc = $stamp
}
'MSVC header tracking and incremental rebuild: PASS'
