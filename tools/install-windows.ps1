<#
.SYNOPSIS
  Instala / desinstala OBS SoundGrid Input para el usuario actual de Windows (sin admin).

.DESCRIPTION
  Destino (formato de plugins de OBS 33): %ProgramData%\obs-studio\plugins\obs-soundgrid-input\
     obs-soundgrid-input.dll
     data\locale\*.ini
  Con -Legacy usa el formato anterior (bin\64bit\ + data\), que OBS 33 aún carga como "Legacy".
  No modifica la instalación de OBS, ni escenas, ni perfiles.

  Uso:
    tools\install-windows.ps1 [-Source build\src\obs\Release] [-Legacy]
    tools\install-windows.ps1 -Uninstall
#>
param(
    [string]$Source = (Join-Path $PSScriptRoot '..\build\src\obs\Release'),
    [switch]$Legacy,
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$name = 'obs-soundgrid-input'
$root = Join-Path $env:ProgramData "obs-studio\plugins\$name"

if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    Write-Warning 'OBS está abierto: ciérralo antes de instalar o desinstalar (la DLL estaría bloqueada).'
    exit 1
}

if ($Uninstall) {
    if (Test-Path $root) { Remove-Item -Recurse -Force $root; Write-Host "Desinstalado: $root" }
    else { Write-Host 'No estaba instalado.' }
    Write-Host 'La configuración del plugin (alerts.jsonl, diagnostics.json) queda en %APPDATA%\obs-studio\plugin_config\obs-soundgrid-input.'
    exit 0
}

$dll = Join-Path $Source "$name.dll"
if (-not (Test-Path $dll)) { throw "No se encontró $dll (compila en Release primero)" }
if (Test-Path $root) { Remove-Item -Recurse -Force $root }
$binDir = if ($Legacy) { Join-Path $root 'bin\64bit' } else { $root }
New-Item -ItemType Directory -Force -Path $binDir | Out-Null
Copy-Item $dll $binDir
$pdb = Join-Path $Source "$name.pdb"
if (Test-Path $pdb) { Copy-Item $pdb $binDir }
Copy-Item -Recurse (Join-Path $Source 'data') (Join-Path $root 'data')
$hash = (Get-FileHash (Join-Path $binDir "$name.dll") -Algorithm SHA256).Hash
Write-Host "Instalado en $root ($(if ($Legacy) {'formato legacy'} else {'formato OBS 33'}))"
Write-Host "SHA256 $name.dll: $hash"
