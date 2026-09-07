#!/bin/bash
# Package the built SpaghettiKart.app as a compressed .dmg for a fork release.
# Usage: cmake/macos/make-dmg.sh [build-dir] [out-dir]
# The image holds the app and an Applications shortcut, the same layout the
# other HarbourMasters macOS ports ship.
set -euo pipefail

BUILD_DIR="${1:-build-cmake}"
OUT_DIR="${2:-$BUILD_DIR}"
APP="$BUILD_DIR/SpaghettiKart.app"

if [ ! -d "$APP" ]; then
    echo "make-dmg: no app bundle at $APP (build first)" >&2
    exit 1
fi

VERSION=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$APP/Contents/Info.plist")
ARCH=$(uname -m)
DMG="$OUT_DIR/SpaghettiKart-v${VERSION}-macOS-${ARCH}.dmg"

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ditto "$APP" "$STAGE/SpaghettiKart.app"
ln -s /Applications "$STAGE/Applications"

rm -f "$DMG"
hdiutil create -volname "SpaghettiKart" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
echo "$DMG"
