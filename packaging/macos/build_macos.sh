#!/usr/bin/env bash
# Builds Пси for macOS 11+ Intel x64 into build/Psi.dmg.
# Prerequisites: Xcode command line tools, CMake ≥ 3.21, Ninja, Qt 6 (Core Gui Widgets PrintSupport Svg) with macdeployqt on PATH
# or pointed to by $QT_ROOT (e.g. ~/Qt/6.7.3/macos).
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
VERSION="${PSI_VERSION:-$(sed -n 's/^project(PSI VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)}"
QT_ROOT="${QT_ROOT:-${QT_ROOT_DIR:-}}"
if [[ -n "$QT_ROOT" ]]; then export PATH="$QT_ROOT/bin:$PATH"; CMAKE_PREFIX="-DCMAKE_PREFIX_PATH=$QT_ROOT"; else CMAKE_PREFIX=""; fi
# Open CASCADE: через vcpkg (VCPKG_ROOT + оверлей-триплет x64-osx-11, см. .github/workflows/psi-macos.yml) или из CMAKE_PREFIX_PATH.
# PSI_WITH_OCCT=ON требует ядро, OFF собирает без «Модели», пусто — как найдёт CMake.
OCCT_ARGS=()
if [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
  OCCT_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET:-x64-osx-11}"
              "-DVCPKG_OVERLAY_TRIPLETS=${VCPKG_OVERLAY_TRIPLETS:-$ROOT/packaging/vcpkg-triplets}" "-DVCPKG_INSTALLED_DIR=$ROOT/vcpkg_installed")
fi
if [[ -n "${PSI_WITH_OCCT:-}" ]]; then OCCT_ARGS+=("-DPSI_WITH_OCCT=$PSI_WITH_OCCT"); fi

echo "== Пси $VERSION: configure"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DCMAKE_OSX_ARCHITECTURES=x86_64 -DPSI_BUILD_TESTS=ON $CMAKE_PREFIX "${OCCT_ARGS[@]}"
grep -q "PSI_WITH_OCCT:BOOL=ON" "$BUILD/CMakeCache.txt" && echo "== Open CASCADE: включено" || echo "== Open CASCADE: выключено (рабочее пространство «Модель» недоступно)"
echo "== build"
cmake --build "$BUILD" --parallel
echo "== tests"
(cd "$BUILD" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure --timeout 600)

APP="$BUILD/apps/psi-desktop/psi.app"
[[ -d "$APP" ]] || { echo "bundle not found: $APP"; exit 1; }

echo "== icon"
if [[ ! -f "$BUILD/Psi.icns" ]]; then
  python3 "$ROOT/packaging/macos/make_icon.py" "$BUILD/Psi.iconset" && iconutil -c icns "$BUILD/Psi.iconset" -o "$BUILD/Psi.icns" || echo "icon skipped"
fi
if [[ -f "$BUILD/Psi.icns" ]]; then
  mkdir -p "$APP/Contents/Resources"
  cp "$BUILD/Psi.icns" "$APP/Contents/Resources/Psi.icns"
  /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile Psi.icns" "$APP/Contents/Info.plist" 2>/dev/null || /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string Psi.icns" "$APP/Contents/Info.plist"
fi

echo "== macdeployqt"
macdeployqt "$APP" -verbose=1 -always-overwrite
# drop plugins we never use to keep the bundle small
rm -rf "$APP/Contents/PlugIns/sqldrivers" "$APP/Contents/PlugIns/networkinformation" "$APP/Contents/PlugIns/tls" "$APP/Contents/PlugIns/imageformats/libqgif.dylib" 2>/dev/null || true
codesign --force --deep --sign - "$APP"

echo "== minimum macOS check"
python3 "$ROOT/packaging/macos/check_min_macos.py" "$APP" 11.0

echo "== self-test of the bundled binary"
QT_QPA_PLATFORM=offscreen "$APP/Contents/MacOS/psi" --selftest

echo "== dmg"
STAGE="$BUILD/dmg"
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/Пси.app"
ln -s /Applications "$STAGE/Applications"
cp "$ROOT/packaging/macos/README-dmg.txt" "$STAGE/Прочти меня.txt"
DMG="$BUILD/Psi-$VERSION-macos-x64.dmg"
rm -f "$DMG"
for attempt in 1 2 3 4 5; do
  if hdiutil create -volname "Пси $VERSION" -srcfolder "$STAGE" -ov -format UDZO -fs HFS+ "$DMG"; then break; fi
  echo "hdiutil failed (attempt $attempt), retrying…"; sleep $((attempt * 5))
done
[[ -f "$DMG" ]] || { echo "dmg not created"; exit 1; }
ls -la "$DMG"
echo "== done: $DMG"
