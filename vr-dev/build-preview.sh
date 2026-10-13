#!/bin/bash
# Builds the Linux desktop preview (the program the tests run: vrMode 2, mouse instead of a headset).
#   vr-dev/build-preview.sh          -> $VR_PREVIEW_BUILD/bin/openxcom
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"
vr_linux_env
if [ ! -f "$VR_PREVIEW_BUILD/build.ninja" ]; then
	cmake -S "$VR_REPO" -B "$VR_PREVIEW_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOXCE_VR=ON \
		-DCMAKE_PREFIX_PATH="$VR_LP" -DCMAKE_BUILD_RPATH="$VR_LP/lib"
fi
cmake --build "$VR_PREVIEW_BUILD" -j"$(nproc)"
echo "preview: $VR_PREVIEW_BUILD/bin/openxcom"
