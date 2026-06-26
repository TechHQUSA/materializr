#!/usr/bin/env bash
# Build Materializr.app and a distributable .dmg on macOS (Apple Silicon).
#
# Prereqs:
#   - a Release build in $BUILD_DIR (default: build/) — see BUILD.md (macOS)
#   - brew install dylibbundler
#
# The result is self-contained: every Homebrew/OpenCASCADE dylib the binary
# needs is copied into the .app and its install name rewritten, so the app runs
# on a Mac that has never seen Homebrew. It is ad-hoc signed (not notarized), so
# first launch needs right-click -> Open (or `xattr -dr com.apple.quarantine`).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build}"
BIN="$BUILD_DIR/materializr"
APP="$BUILD_DIR/Materializr.app"
VERSION="$(grep -m1 'project(Materializr VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"
DMG="$BUILD_DIR/Materializr-${VERSION}-arm64.dmg"
BREW="$(brew --prefix)"

[ -x "$BIN" ] || { echo "error: $BIN not found — build first: cmake --build $BUILD_DIR"; exit 1; }
command -v dylibbundler >/dev/null || { echo "error: dylibbundler not found — brew install dylibbundler"; exit 1; }

echo "==> Assembling $APP (v$VERSION)"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/assets" "$APP/Contents/Frameworks"

cp "$BIN" "$APP/Contents/MacOS/materializr"

# Bundled fonts — resolveBundledFont() looks in <exe>/../Resources/assets/fonts.
cp -R assets/fonts "$APP/Contents/Resources/assets/"

# Icon: icon.png -> Materializr.icns (build a full iconset so Finder/Dock scale).
ICONSET="$(mktemp -d)/Materializr.iconset"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
  sips -z "$s"        "$s"        icon.png --out "$ICONSET/icon_${s}x${s}.png"    >/dev/null
  sips -z "$((s*2))"  "$((s*2))"  icon.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/Materializr.icns"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Materializr</string>
  <key>CFBundleDisplayName</key><string>Materializr</string>
  <key>CFBundleExecutable</key><string>materializr</string>
  <key>CFBundleIdentifier</key><string>com.materializr.app</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleIconFile</key><string>Materializr</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>GPLv3</string>
</dict>
</plist>
PLIST

# Copy every non-system dylib into Frameworks/ and rewrite install names to
# @executable_path/../Frameworks. Search paths cover the Homebrew + OCCT kegs so
# dylibbundler never needs to prompt interactively.
echo "==> Bundling dylibs"
dylibbundler -b -cd -of \
  -x "$APP/Contents/MacOS/materializr" \
  -d "$APP/Contents/Frameworks/" \
  -p "@executable_path/../Frameworks/" \
  -s "$BREW/lib" \
  -s "$BREW/opt/opencascade/lib" </dev/null

# dylibbundler rewrites each original rpath to the same @executable_path/../
# Frameworks, leaving duplicate LC_RPATH entries that dyld refuses to load
# ("duplicate LC_RPATH"). This happens in the main binary AND in any bundled OCCT
# dylib that shipped with more than one rpath, so collapse duplicates everywhere.
# -delete_rpath removes one matching entry per call.
RP='@executable_path/../Frameworks/'
dedupe_rpath() {
  while [ "$(otool -l "$1" | grep -c "path $RP")" -gt 1 ]; do
    install_name_tool -delete_rpath "$RP" "$1"
  done
}
dedupe_rpath "$APP/Contents/MacOS/materializr"
for dylib in "$APP/Contents/Frameworks/"*.dylib; do
  dedupe_rpath "$dylib"
done

# Ad-hoc signature (lets Gatekeeper run it after the user approves it once).
echo "==> Ad-hoc signing"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP" && echo "    signature OK"

# .dmg with an /Applications symlink for drag-to-install.
echo "==> Building $DMG"
STAGE="$(mktemp -d)/dmg"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "Materializr ${VERSION}" -srcfolder "$STAGE" \
  -ov -format UDZO "$DMG" >/dev/null

echo "==> Done"
echo "    $APP"
echo "    $DMG  ($(du -h "$DMG" | cut -f1))"
