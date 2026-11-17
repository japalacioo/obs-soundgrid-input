<#
.SYNOPSIS
  Compila con MSVC y muestra solo errores/avisos únicos (útil en CI local y para agentes).
  Uso: tools\build.ps1 [-Config Debug|Release] [-Target <target>] [-Configure] [-ObsSdk <dir>]
#>
param(
    [string]$Config = 'Debug',
    [string]$Target = 'ALL_BUILD',
    [switch]$Configure,
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build'),
    [string]$ObsSdk = (Join-Path $PSScriptRoot '..\.deps\obs-sdk')
)
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
if ($Configure -or -not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
    $args = @('-S', $root, '-B', $BuildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64')
    if (Test-Path (Join-Path $ObsSdk 'lib\obs.lib')) { $args += "-DSG_OBS_SDK_DIR=$((Resolve-Path $ObsSdk).Path)" }
    & cmake @args | Select-Object -Last 3
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$out = & cmake --build $BuildDir --config $Config --target $Target -- /v:q /nologo /clp:NoSummary 2>&1
$code = $LASTEXITCODE
$out | Select-String -Pattern ': (error|warning) ' | ForEach-Object { ($_.Line -replace '\s*\[[A-Z]:\\.*$', '') -replace '^.*\\(src|tests)\\', '$1\' } | Select-Object -Unique -First 60
if ($code -eq 0) { Write-Host "BUILD OK ($Config, $Target)" } else { Write-Host "BUILD FAILED ($code)" }
exit $code
