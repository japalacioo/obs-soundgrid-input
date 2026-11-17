#!/usr/bin/env bash
# Empaqueta el plugin macOS (bundle .plugin) en un .pkg que instala en
# ~/Library/Application Support/obs-studio/plugins/ y en un .zip.
# ESTADO: escrito, NO ejecutado (requiere macOS + Xcode). Firma y notarización solo si hay
# credenciales: export SG_CODESIGN_ID="Developer ID Application: ..." y SG_NOTARY_PROFILE=...
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build-macos}"
CONFIG="${2:-Release}"
VER="$(sed -nE 's/^project\(obs-soundgrid-input VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt")"
BUNDLE="$BUILD/src/obs/$CONFIG/obs-soundgrid-input.plugin"
[ -d "$BUNDLE" ] || { echo "Falta $BUNDLE"; exit 1; }

STAGE="$ROOT/dist/stage-macos"
rm -rf "$STAGE" && mkdir -p "$STAGE/obs-soundgrid-input.plugin/Contents/Resources"
cp -R "$BUNDLE/" "$STAGE/obs-soundgrid-input.plugin/"
cp -R "$ROOT/data/" "$STAGE/obs-soundgrid-input.plugin/Contents/Resources/"

if [ -n "${SG_CODESIGN_ID:-}" ]; then
  codesign --force --options runtime --timestamp --sign "$SG_CODESIGN_ID" "$STAGE/obs-soundgrid-input.plugin"
fi

mkdir -p "$ROOT/dist"
ZIP="$ROOT/dist/obs-soundgrid-input-$VER-macos-universal.zip"
(cd "$STAGE" && ditto -c -k --keepParent obs-soundgrid-input.plugin "$ZIP")
shasum -a 256 "$ZIP" > "$ZIP.sha256"

PKG="$ROOT/dist/obs-soundgrid-input-$VER-macos-universal.pkg"
pkgbuild --root "$STAGE" --identifier com.obs-soundgrid-input.plugin --version "$VER" \
  --install-location "/Library/Application Support/obs-studio/plugins" "$PKG"
if [ -n "${SG_NOTARY_PROFILE:-}" ]; then
  xcrun notarytool submit "$PKG" --keychain-profile "$SG_NOTARY_PROFILE" --wait
  xcrun stapler staple "$PKG"
fi
shasum -a 256 "$PKG" > "$PKG.sha256"
echo "Paquetes: $ZIP $PKG"
echo "Recuerda: binario universal != driver Waves compatible en cada CPU (ADR-007)."
