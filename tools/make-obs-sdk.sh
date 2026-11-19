#!/usr/bin/env bash
# Prepara un "SDK" mínimo de libobs para macOS a partir de OBS.app (equivalente a make-obs-sdk.ps1).
#   1. Clona (sparse, solo lectura) las cabeceras de libobs y obs-frontend-api del repositorio
#      oficial https://github.com/obsproject/obs-studio en la etiqueta de la versión instalada.
#   2. Genera stubs de enlace (.tbd) para libobs y obs-frontend-api a partir de los binarios
#      instalados, declarados para arm64 y x86_64: así se enlaza un plugin universal aunque
#      OBS.app sea de una sola arquitectura. No modifica la instalación.
# Resultado: <OutDir>/include/{libobs,frontend-api} y <OutDir>/lib/*.tbd
# Uso: tools/make-obs-sdk.sh [/Applications/OBS.app] [.deps/obs-sdk]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OBS_APP="${1:-/Applications/OBS.app}"
OUT="${2:-$ROOT/.deps/obs-sdk}"
TAG="${SG_OBS_TAG:-$(defaults read "$OBS_APP/Contents/Info.plist" CFBundleShortVersionString)}"
FW="$OBS_APP/Contents/Frameworks"
[ -f "$FW/libobs.framework/libobs" ] || { echo "No se encontró libobs en $FW"; exit 1; }
echo "OBS instalado: $TAG"

SRC="$(dirname "$OUT")/obs-studio"
if [ ! -d "$SRC/.git" ]; then
  git clone -q --depth 1 --branch "$TAG" --filter=blob:none --sparse https://github.com/obsproject/obs-studio.git "$SRC"
fi
git -C "$SRC" sparse-checkout set libobs frontend/api UI/obs-frontend-api 2>/dev/null

rm -rf "$OUT/include/libobs" "$OUT/include/frontend-api" "$OUT/lib"
mkdir -p "$OUT/include/libobs" "$OUT/include/frontend-api" "$OUT/lib"
(cd "$SRC/libobs" && find . \( -name '*.h' -o -name '*.hpp' \) -exec rsync -R {} "$OUT/include/libobs/" \;)
cat > "$OUT/include/libobs/obsconfig.h" <<'EOF'
#pragma once
/* Generado por tools/make-obs-sdk.sh para compilar plugins contra una release de OBS. */
#define OBS_DATA_PATH "../../data"
#define OBS_PLUGIN_PATH "../../obs-plugins"
#define OBS_PLUGIN_DESTINATION "obs-plugins"
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
EOF
# libobs/util/sse-intrin.h incluye <simde/x86/sse2.h> en macOS (SIMDe, MIT, solo cabeceras).
SIMDE="$(dirname "$OUT")/simde"
if [ ! -d "$SIMDE/.git" ]; then
  git clone -q --depth 1 --branch "${SG_SIMDE_TAG:-v0.8.2}" https://github.com/simd-everywhere/simde.git "$SIMDE"
fi
rm -rf "$OUT/include/simde" && cp -R "$SIMDE/simde" "$OUT/include/simde"
for d in frontend/api UI/obs-frontend-api; do
  if [ -f "$SRC/$d/obs-frontend-api.h" ]; then cp "$SRC/$d/obs-frontend-api.h" "$OUT/include/frontend-api/"; break; fi
done

# Stub .tbd (formato v4) con los símbolos exportados del binario instalado.
make_tbd() { # <binario> <salida.tbd>
  local bin="$1" out="$2" install_name syms
  install_name="$(otool -D "$bin" | tail -1)"
  syms="$(nm -gUj "$bin" | grep -v '^$' | sort -u | sed "s/.*/'&'/" | paste -sd, - | sed 's/,/, /g')"
  [ -n "$syms" ] || { echo "Sin exportaciones en $bin"; exit 1; }
  cat > "$out" <<EOF
--- !tapi-tbd
tbd-version:     4
targets:         [ x86_64-macos, arm64-macos ]
install-name:    '$install_name'
exports:
  - targets:     [ x86_64-macos, arm64-macos ]
    symbols:     [ $syms ]
...
EOF
  echo "  $(basename "$out"): $(nm -gUj "$bin" | grep -vc '^$') exportaciones"
}
make_tbd "$FW/libobs.framework/libobs" "$OUT/lib/libobs.tbd"
make_tbd "$FW/obs-frontend-api.dylib" "$OUT/lib/obs-frontend-api.tbd"
echo "SDK listo en $OUT"
