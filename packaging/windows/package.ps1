<#
.SYNOPSIS
  Genera el paquete Windows x64: dist\obs-soundgrid-input-<ver>-windows-x64.zip (+ .sha256).
  Contenido: obs-soundgrid-input\{obs-soundgrid-input.dll, data\}, install.ps1, uninstall.ps1,
  LICENSE, README/INSTALL/LIMITATIONS. No incluye drivers ni SDK de Waves/Steinberg.
  Uso: packaging\windows\package.ps1 [-BuildDir build] [-Config Release]
#>
param([string]$BuildDir = 'build', [string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$ver = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(obs-soundgrid-input VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$bin = Join-Path $root "$BuildDir\src\obs\$Config"
$dll = Join-Path $bin 'obs-soundgrid-input.dll'
if (-not (Test-Path $dll)) { throw "Falta $dll" }

$stage = Join-Path $root "dist\stage\obs-soundgrid-input-$ver-windows-x64"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$plug = Join-Path $stage 'obs-soundgrid-input'
New-Item -ItemType Directory -Force -Path $plug | Out-Null
Copy-Item $dll $plug
Copy-Item -Recurse (Join-Path $bin 'data') (Join-Path $plug 'data')
Copy-Item (Join-Path $root 'LICENSE') $stage
foreach ($d in 'README.md', 'docs\INSTALL.md', 'docs\LIMITATIONS.md', 'docs\TROUBLESHOOTING.md', 'docs\ALERTS.md', 'docs\RELEASE_NOTES.md') {
    $p = Join-Path $root $d
    if (Test-Path $p) { Copy-Item $p $stage }
}
# Instalador / desinstalador por usuario (sin admin) incluidos en el ZIP.
@'
# Instala OBS SoundGrid Input en %ProgramData%\obs-studio\plugins (formato OBS 33). Cierra OBS antes.
$ErrorActionPreference = 'Stop'
if (Get-Process obs64 -ErrorAction SilentlyContinue) { Write-Warning 'Cierra OBS antes de instalar.'; exit 1 }
$dst = Join-Path $env:ProgramData 'obs-studio\plugins\obs-soundgrid-input'
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
Copy-Item -Recurse (Join-Path $PSScriptRoot 'obs-soundgrid-input') $dst
Write-Host "Instalado en $dst. Abre OBS y añade la fuente 'SoundGrid Input (Waves)'."
'@ | Set-Content -Encoding utf8 (Join-Path $stage 'install.ps1')
@'
# Desinstala OBS SoundGrid Input (no toca escenas ni la configuración de OBS).
$ErrorActionPreference = 'Stop'
if (Get-Process obs64 -ErrorAction SilentlyContinue) { Write-Warning 'Cierra OBS antes de desinstalar.'; exit 1 }
$dst = Join-Path $env:ProgramData 'obs-studio\plugins\obs-soundgrid-input'
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst; Write-Host "Eliminado $dst" } else { Write-Host 'No estaba instalado.' }
'@ | Set-Content -Encoding utf8 (Join-Path $stage 'uninstall.ps1')

New-Item -ItemType Directory -Force -Path (Join-Path $root 'dist') | Out-Null
$zip = Join-Path $root "dist\obs-soundgrid-input-$ver-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path $zip -Leaf)" | Set-Content -Encoding ascii "$zip.sha256"
$dllHash = (Get-FileHash $dll -Algorithm SHA256).Hash.ToLower()
Write-Host "Paquete: $zip"
Write-Host "SHA256 zip: $hash"
Write-Host "SHA256 dll: $dllHash"
