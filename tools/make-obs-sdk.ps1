<#
.SYNOPSIS
  Prepara un "SDK" mínimo de libobs a partir de una instalación de OBS Studio en Windows.

.DESCRIPTION
  OBS Studio no distribuye cabeceras ni bibliotecas de importación. Este script:
    1. Clona (sparse, solo lectura) las cabeceras de libobs y de obs-frontend-api del
       repositorio oficial https://github.com/obsproject/obs-studio en la etiqueta que
       coincide con la versión instalada.
    2. Genera obs.lib y obs-frontend-api.lib a partir de las DLL instaladas
       (dumpbin /exports + lib /def), sin modificar la instalación.
  Resultado: <OutDir>\include\{libobs,frontend-api} y <OutDir>\lib\*.lib

  Uso: powershell -ExecutionPolicy Bypass -File tools\make-obs-sdk.ps1 [-ObsDir "C:\Program Files\obs-studio"] [-OutDir .deps\obs-sdk]
#>
param(
    [string]$ObsDir = "$env:ProgramFiles\obs-studio",
    [string]$OutDir = (Join-Path $PSScriptRoot '..\.deps\obs-sdk'),
    [string]$Tag = ''
)
$ErrorActionPreference = 'Stop'

$bin = Join-Path $ObsDir 'bin\64bit'
$obsExe = Join-Path $bin 'obs64.exe'
if (-not (Test-Path $obsExe)) { throw "No se encontró OBS en $bin" }
if (-not $Tag) {
    $v = (Get-Item $obsExe).VersionInfo.ProductVersion
    $Tag = ($v -split '[\s-]')[0]
}
Write-Host "OBS instalado: $Tag"

$deps = Split-Path $OutDir -Parent
New-Item -ItemType Directory -Force -Path $deps, "$OutDir\include", "$OutDir\lib" | Out-Null
$src = Join-Path $deps "obs-studio"
if (-not (Test-Path (Join-Path $src '.git'))) {
    git clone -q --depth 1 --branch $Tag --filter=blob:none --sparse https://github.com/obsproject/obs-studio.git $src
}
Push-Location $src
git sparse-checkout set libobs frontend/api UI/obs-frontend-api 2>$null
Pop-Location

# Copiar cabeceras (solo .h / .hpp)
$incObs = Join-Path $OutDir 'include\libobs'
if (Test-Path $incObs) { Remove-Item -Recurse -Force $incObs }
robocopy (Join-Path $src 'libobs') $incObs *.h *.hpp /S /NFL /NDL /NJH /NJS /NP | Out-Null
$fe = @('frontend\api', 'UI\obs-frontend-api') | ForEach-Object { Join-Path $src $_ } | Where-Object { Test-Path (Join-Path $_ 'obs-frontend-api.h') } | Select-Object -First 1
if ($fe) {
    New-Item -ItemType Directory -Force -Path "$OutDir\include\frontend-api" | Out-Null
    Copy-Item (Join-Path $fe 'obs-frontend-api.h') "$OutDir\include\frontend-api\" -Force
}

# Herramientas de MSVC
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "No se encontró vcvars64.bat" }

function New-ImportLib([string]$dll, [string]$name) {
    $def = Join-Path $OutDir "lib\$name.def"
    $lib = Join-Path $OutDir "lib\$name.lib"
    $dump = cmd /c "`"$vcvars`" >nul && dumpbin /nologo /exports `"$dll`""
    $names = foreach ($line in $dump) {
        if ($line -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)') { $Matches[1] }
    }
    if (-not $names) { throw "No se leyeron exportaciones de $dll" }
    @("LIBRARY $name", 'EXPORTS') + ($names | ForEach-Object { "    $_" }) | Set-Content -Encoding ascii $def
    cmd /c "`"$vcvars`" >nul && lib /nologo /def:`"$def`" /machine:x64 /out:`"$lib`"" | Out-Null
    if (-not (Test-Path $lib)) { throw "No se generó $lib" }
    Write-Host "  $name.lib: $($names.Count) exportaciones"
}
New-ImportLib (Join-Path $bin 'obs.dll') 'obs'
if (Test-Path (Join-Path $bin 'obs-frontend-api.dll')) { New-ImportLib (Join-Path $bin 'obs-frontend-api.dll') 'obs-frontend-api' }

Set-Content -Encoding ascii (Join-Path $OutDir 'VERSION.txt') "obs-studio $Tag (cabeceras de github.com/obsproject/obs-studio; import libs generadas de $bin)"
Write-Host "SDK listo en $OutDir"
