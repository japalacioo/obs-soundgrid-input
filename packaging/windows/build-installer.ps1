<#
.SYNOPSIS
  Genera el instalador de Windows: dist\obs-soundgrid-input-<ver>-windows-x64-setup.exe (+ .sha256).
  Requiere Inno Setup 6 (ISCC.exe) y el plugin compilado en Release.
  Uso: packaging\windows\build-installer.ps1 [-BuildDir build] [-Config Release] [-Iscc ruta\ISCC.exe]
#>
param([string]$BuildDir = 'build', [string]$Config = 'Release', [string]$Iscc = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$ver = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(obs-soundgrid-input VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$bin = Join-Path $root "$BuildDir\src\obs\$Config"
if (-not (Test-Path (Join-Path $bin 'obs-soundgrid-input.dll'))) { throw "Falta $bin\obs-soundgrid-input.dll (compila en $Config primero)" }

if (-not $Iscc) {
    $candidates = @(
        (Get-Command iscc -ErrorAction SilentlyContinue).Source,
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) }
    $Iscc = $candidates | Select-Object -First 1
}
if (-not $Iscc) { throw 'No se encontró ISCC.exe (Inno Setup 6). Instálalo o pásalo con -Iscc.' }

& $Iscc /Qp "/DAppVersion=$ver" "/DRootDir=$root" "/DBinDir=$bin" (Join-Path $PSScriptRoot 'obs-soundgrid-input.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC falló ($LASTEXITCODE)" }

$exe = Join-Path $root "dist\obs-soundgrid-input-$ver-windows-x64-setup.exe"
$hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLower()
"$hash  $(Split-Path $exe -Leaf)" | Set-Content -Encoding ascii "$exe.sha256"
Write-Host "Instalador: $exe ($([math]::Round((Get-Item $exe).Length / 1KB)) KB)"
Write-Host "SHA256: $hash"
Write-Host 'Sin firma digital: Windows SmartScreen puede pedir "Más información > Ejecutar de todas formas".'
