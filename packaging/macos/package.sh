#!/usr/bin/env bash
# Empaqueta el plugin macOS (bundle .plugin universal) en un instalador .pkg y en un .zip.
#
# OBS 30+ en macOS solo carga bundles .plugin desde la carpeta del USUARIO:
#   ~/Library/Application Support/obs-studio/plugins/<plugin>.plugin
# (/Library/Application Support solo se usa para el formato antiguo bin/ + data/). Por eso el
# .pkg se instala en el dominio "Solo para mí" (enable_currentUserHome) y no pide contraseña.
#
# Firma y notarización solo si hay credenciales:
#   export SG_CODESIGN_ID="Developer ID Application: ..."     (firma del bundle)
#   export SG_INSTALLER_ID="Developer ID Installer: ..."      (firma del .pkg)
#   export SG_NOTARY_PROFILE=<perfil de notarytool>
# Sin ellas el bundle lleva firma ad hoc y el .pkg queda sin firmar (Gatekeeper avisará).
#
# Uso: packaging/macos/package.sh [build-macos] [Release]
set -euo pipefail
export COPYFILE_DISABLE=1 # sin ficheros ._ (AppleDouble) en el .pkg ni en el .zip
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build-macos}"
CONFIG="${2:-Release}"
VER="$(sed -nE 's/^project\(obs-soundgrid-input VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt")"
ID="com.obs-soundgrid-input.plugin"
NAME="obs-soundgrid-input"

# Xcode (multi-config) deja el bundle en src/obs/<Config>/; Makefiles/Ninja en src/obs/.
BUNDLE="$BUILD/src/obs/$CONFIG/$NAME.plugin"
[ -d "$BUNDLE" ] || BUNDLE="$BUILD/src/obs/$NAME.plugin"
[ -d "$BUNDLE" ] || { echo "Falta $NAME.plugin en $BUILD/src/obs (compila primero)"; exit 1; }

DIST="$ROOT/dist"
WORK="$DIST/work-macos"
STAGE="$WORK/root"
rm -rf "$WORK" && mkdir -p "$STAGE" "$WORK/resources"
ditto "$BUNDLE" "$STAGE/$NAME.plugin"
# Los datos (locale/) van en Contents/Resources; se copian por si el build no lo hizo.
mkdir -p "$STAGE/$NAME.plugin/Contents/Resources"
ditto "$ROOT/data" "$STAGE/$NAME.plugin/Contents/Resources"
rm -rf "$STAGE/$NAME.plugin/Contents/MacOS/data"
xattr -cr "$STAGE/$NAME.plugin"

ARCHS="$(lipo -archs "$STAGE/$NAME.plugin/Contents/MacOS/$NAME")"
echo "Arquitecturas: $ARCHS"

if [ -n "${SG_CODESIGN_ID:-}" ]; then
  codesign --force --options runtime --timestamp --sign "$SG_CODESIGN_ID" "$STAGE/$NAME.plugin"
else
  codesign --force --sign - "$STAGE/$NAME.plugin" # firma ad hoc (obligatoria en Apple Silicon)
fi
codesign --verify --strict "$STAGE/$NAME.plugin"
xattr -cr "$STAGE/$NAME.plugin" 2>/dev/null || true

mkdir -p "$DIST"
SUFFIX="macos-universal"
[ "$ARCHS" = "arm64" ] && SUFFIX="macos-arm64"
[ "$ARCHS" = "x86_64" ] && SUFFIX="macos-x86_64"

ZIP="$DIST/$NAME-$VER-$SUFFIX.zip"
rm -f "$ZIP"
(cd "$STAGE" && ditto -c -k --norsrc --noextattr --keepParent "$NAME.plugin" "$ZIP")
(cd "$DIST" && shasum -a 256 "$(basename "$ZIP")" > "$(basename "$ZIP").sha256")

# ---- paquete de componente: el bundle no es reubicable (si no, Installer podría "seguir"
# una copia del mismo identificador en otra carpeta y actualizarla allí).
pkgbuild --analyze --root "$STAGE" "$WORK/component.plist" >/dev/null
plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/component.plist"
plutil -replace 0.BundleOverwriteAction -string upgrade "$WORK/component.plist"
pkgbuild --root "$STAGE" --component-plist "$WORK/component.plist" \
  --identifier "$ID" --version "$VER" \
  --install-location "/Library/Application Support/obs-studio/plugins" \
  "$WORK/$NAME.pkg" >/dev/null

# ---- textos del asistente
cp "$ROOT/LICENSE" "$WORK/resources/LICENSE.txt"
cat > "$WORK/resources/welcome.html" <<EOF
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h3>OBS SoundGrid Input $VER</h3>
<p>Plugin para OBS Studio que recibe el audio de una consola Waves eMotion LV1 Classic V17
a través del Waves SoundGrid Driver V17.</p>
<p>Se instala en <code>~/Library/Application Support/obs-studio/plugins/</code> (solo para tu
usuario). <b>Cierra OBS antes de continuar.</b></p>
<p>Requisitos: macOS 11 o posterior, OBS Studio 30 o posterior (probado con 33.0.1) y el
SoundGrid Driver de Waves instalado con Waves Central.</p>
<p style="color:#a00"><b>Estado en macOS: experimental.</b> El plugin carga en OBS, pero no se
ha probado con hardware Waves en Mac y la barra roja de aviso todavía no existe en macOS
(las alertas aparecen en el panel de la fuente y en el log de OBS).</p>
<p>Proyecto independiente, sin relación con Waves Audio Ltd. Licencia GPLv3.</p>
</body></html>
EOF
cat > "$WORK/resources/conclusion.html" <<EOF
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h3>Instalación completada</h3>
<p>Abre OBS y añade <b>Fuentes &gt; + &gt; SoundGrid Input (Waves)</b>.</p>
<p>La primera vez, macOS pedirá permiso de <b>micrófono</b> para OBS: acéptalo o el driver
no entregará audio (Ajustes del Sistema &gt; Privacidad y seguridad &gt; Micrófono).</p>
<p>Para desinstalar, borra la carpeta
<code>~/Library/Application Support/obs-studio/plugins/$NAME.plugin</code>.</p>
</body></html>
EOF

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>OBS SoundGrid Input $VER</title>
  <welcome file="welcome.html" mime-type="text/html"/>
  <license file="LICENSE.txt"/>
  <conclusion file="conclusion.html" mime-type="text/html"/>
  <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
  <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>
  <choices-outline><line choice="default"/></choices-outline>
  <choice id="default" title="OBS SoundGrid Input">
    <pkg-ref id="$ID"/>
  </choice>
  <pkg-ref id="$ID" version="$VER" onConclusion="none">$NAME.pkg</pkg-ref>
</installer-gui-script>
EOF

PKG="$DIST/$NAME-$VER-$SUFFIX.pkg"
SIGN_ARGS=()
[ -n "${SG_INSTALLER_ID:-}" ] && SIGN_ARGS=(--sign "$SG_INSTALLER_ID")
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
  --package-path "$WORK" "${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"}" "$PKG"
if [ -n "${SG_NOTARY_PROFILE:-}" ]; then
  xcrun notarytool submit "$PKG" --keychain-profile "$SG_NOTARY_PROFILE" --wait
  xcrun stapler staple "$PKG"
fi
(cd "$DIST" && shasum -a 256 "$(basename "$PKG")" > "$(basename "$PKG").sha256")
rm -rf "$WORK"
echo "Paquetes:"
echo "  $PKG"
echo "  $ZIP"
echo "Recuerda: binario universal != driver Waves compatible en cada CPU (ADR-007)."
