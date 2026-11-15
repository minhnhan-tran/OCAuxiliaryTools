#!/bin/sh
# Package the built macOS app into an installable, ad-hoc signed disk image.
#
#   scripts/package-macos.sh <built .app> <output .dmg>
#
# What the in-app updater (src/selfupdate.cpp) needs from the image, and why each step exists:
#   - CFBundleShortVersionString == CurVersion  -> the updater refuses an app whose version differs from the tag
#   - the .app passes `codesign --verify --deep --strict` -> hence ad-hoc signing at the end
#   - Database/ lives in Contents/Resources and is symlinked from Contents/MacOS: kext bundles directly under
#     Contents/MacOS make codesign fail ("code object is not signed at all" / "In subcomponent: ...kext")
#   - no Qt rpath pointing at the build machine -> macdeployqt bundles Qt, local rpaths are removed first
set -eu

APP_IN=${1:?usage: package-macos.sh <built .app> <output .dmg>}
DMG_OUT=${2:?usage: package-macos.sh <built .app> <output .dmg>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NAME=$(basename "$APP_IN" .app)
# the executable is not necessarily named like the folder
EXE=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$APP_IN/Contents/Info.plist" 2>/dev/null || true)
[ -n "$EXE" ] && [ -x "$APP_IN/Contents/MacOS/$EXE" ] || { echo "no executable named by $APP_IN/Contents/Info.plist"; exit 1; }

VER=$(grep -o 'CurVersion = "[^"]*"' "$ROOT/src/aboutdialog.cpp" | cut -d'"' -f2)
[ -n "$VER" ] || { echo "cannot read CurVersion"; exit 1; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/ocat-pkg.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
APP="$WORK/$NAME.app"
cp -R "$APP_IN" "$APP"

# Info.plist from the repo, stamped with the real version
cp -f "$ROOT/Info.plist" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VER" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $VER" "$APP/Contents/Info.plist"

# drop rpaths that point at the build machine's Qt (macdeployqt adds the bundled one)
otool -l "$APP/Contents/MacOS/$EXE" | awk '/LC_RPATH/{f=1} f&&/ path /{print $2; f=0}' | while read -r rp; do
  case "$rp" in @*) ;; *) install_name_tool -delete_rpath "$rp" "$APP/Contents/MacOS/$EXE" ;; esac
done

macdeployqt "$APP" -verbose=1

# data + helpers
cp -R "$ROOT/Database" "$APP/Contents/Resources/Database"
cp -f "$ROOT/Database/mac/aria2c" "$APP/Contents/MacOS/aria2c"
rm -f "$APP/Contents/Resources/Database/mac/aria2c"
ln -sfn ../Resources/Database "$APP/Contents/MacOS/Database"
D="$APP/Contents/Resources/Database/mac"
chmod +x "$APP/Contents/MacOS/aria2c" "$D/macserial" "$D/ocvalidate" "$D/ocpasswordgen" \
  "$D/CreateVault/create_vault.sh" "$D/CreateVault/RsaTool" "$D/CreateVault/sign.command"
xattr -cr "$APP"

# sign inside-out, then the bundle (no --deep: it trips over the kexts in Database)
for f in "$D/macserial" "$D/ocvalidate" "$D/ocpasswordgen" "$D/CreateVault/RsaTool"; do codesign --force --sign - "$f"; done
find "$APP/Contents/PlugIns" -type f -name '*.dylib' -exec codesign --force --sign - {} \;
for fw in "$APP/Contents/Frameworks/"*.framework; do codesign --force --sign - "$fw"; done
codesign --force --sign - "$APP/Contents/MacOS/aria2c"
codesign --force --sign - "$APP/Contents/MacOS/$EXE"
codesign --force --sign - "$APP"
codesign --verify --deep --strict --verbose=1 "$APP"

# HFS+ so older macOS can mount it
ROOTDIR="$WORK/dmgroot"
mkdir -p "$ROOTDIR"
ditto "$APP" "$ROOTDIR/$NAME.app"
ln -s /Applications "$ROOTDIR/Applications"
mkdir -p "$(dirname "$DMG_OUT")"
rm -f "$DMG_OUT"
hdiutil create -volname "$NAME" -srcfolder "$ROOTDIR" -fs HFS+ -ov -format UDZO "$DMG_OUT"
echo "packaged $NAME $VER -> $DMG_OUT"
