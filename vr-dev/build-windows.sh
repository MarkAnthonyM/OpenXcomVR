#!/bin/bash
# Cross-compiles the Windows build (static, 64-bit) with llvm-mingw.
#   vr-dev/build-windows.sh          -> $VR_WIN_BUILD/bin/openxcom.exe
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"
vr_win_env
if [ ! -f "$VR_WIN_BUILD/build.ninja" ]; then
	cmake -S "$VR_REPO" -B "$VR_WIN_BUILD" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$VRDEV/win/toolchain.cmake" \
		-DCMAKE_BUILD_TYPE=Release -DOXCE_VR=ON -DDEPS_DIR=/nonexistent -DCMAKE_PREFIX_PATH="$VR_WP" \
		-DCMAKE_CXX_FLAGS="-I$VR_WP/include" \
		-DCMAKE_EXE_LINKER_FLAGS="-L$VR_WP/lib" -DCMAKE_SHARED_LINKER_FLAGS="-L$VR_WP/lib" -DCMAKE_MODULE_LINKER_FLAGS="-L$VR_WP/lib" \
		-DCMAKE_CXX_STANDARD_LIBRARIES="-lpng16 -lz -lvorbisfile -lvorbis -logg -lwinmm -lgdi32 -luser32 -lopengl32 -lole32 -loleaut32 -limm32 -lversion -luuid" \
		-DOPENGL_gl_LIBRARY=opengl32 -DOPENGL_glu_LIBRARY=glu32
fi
cmake --build "$VR_WIN_BUILD" -j"$(nproc)"
echo "windows: $VR_WIN_BUILD/bin/openxcom.exe"
