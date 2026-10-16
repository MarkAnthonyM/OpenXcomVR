#!/bin/bash
# Packages the Windows build as OXCE-VR-Prototype-<version>-Windows.zip in $VR_DIST.
# The version comes from src/VR/VrVersion.h. Commit first: BUILD-INFO.txt names the commit.
#   vr-dev/package.sh
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"
vr_win_env
V=$(sed -n 's/.*OXCE_VR_PROTOTYPE_VERSION "\(.*\)".*/\1/p' "$VR_REPO/src/VR/VrVersion.h")
[ -n "$V" ] || { echo "no version in VrVersion.h"; exit 1; }
EXE="$VR_WIN_BUILD/bin/openxcom.exe"
[ -f "$EXE" ] || { echo "build first: vr-dev/build-windows.sh"; exit 1; }
# the exe must be the build of this version (a stale build once went out as 0.13.0)
grep -aq "VR prototype $V" "$EXE" || { echo "$EXE is not a $V build: run vr-dev/build-windows.sh"; exit 1; }
[ "$EXE" -nt "$VR_REPO/src/VR/VrVersion.h" ] || { echo "$EXE is older than the sources: run vr-dev/build-windows.sh"; exit 1; }
C=$(git -C "$VR_REPO" rev-parse HEAD)
git -C "$VR_REPO" diff --quiet HEAD -- src CMakeLists.txt || echo "WARNING: uncommitted source changes; BUILD-INFO.txt will name $C anyway"
N="OXCE-VR-Prototype-$V-Windows"
P="$VR_DIST/$N"
rm -rf "$P" "$VR_DIST/$N.zip"; mkdir -p "$P"
cp -a "$VR_REPO/vr-dev/dist-template/." "$P/"
cp -a "$VR_REPO/bin/common" "$VR_REPO/bin/standard" "$P/"
cp "$EXE" "$P/openxcom.exe" && $STRIP "$P/openxcom.exe"
cp "$VR_REPO/README-VR.md" "$P/"
cp "$VR_REPO/LICENSE.txt" "$P/LICENSE.txt"
cp "$VR_REPO/vr-dist/THIRD-PARTY-NOTICES.txt" "$P/"
cp -a "$VR_REPO/vr-dist/licenses" "$P/licenses"
OXV=$(sed -n 's/.*OPENXCOM_VERSION_SHORT "Extended \([^"]*\)".*/\1/p' "$VR_REPO/src/version.h" | head -1)
cat > "$P/BUILD-INFO.txt" <<INFO
OpenXcom Extended - VR Tabletop prototype $V (Windows, 64-bit)

Based on:  OpenXcom Extended ${OXV:-8.7.1}
Source:    https://github.com/MarkAnthonyM/OpenXcomVR/tree/proto-$V
Commit:    $C (branch vr-prototype)
Built:     $(date -u +%Y-%m-%d) with llvm-mingw 20250924 (x86_64-w64-mingw32), libraries linked statically

Licences: LICENSE.txt (GPL-3.0) and THIRD-PARTY-NOTICES.txt.
INFO
(cd "$VR_DIST" && zip -qr "$N.zip" "$N")
echo "package: $VR_DIST/$N.zip"
