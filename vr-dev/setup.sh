#!/bin/bash
# Sets up everything needed to build and test the VR prototype, from scratch:
#   - system packages (Ubuntu 24.04; apt is used when something is missing)
#   - the llvm-mingw toolchain for the Windows cross build
#   - the dependency sources at the exact commits used for the releases
#   - static Windows libraries in $VR_WP and shared Linux libraries (desktop preview) in $VR_LP
# Re-running is safe: finished steps are skipped (stamp files in $VRDEV/stamps).
#
#   vr-dev/setup.sh            # everything
#   vr-dev/setup.sh linux      # only what the desktop preview needs
#   vr-dev/setup.sh windows    # only what the Windows build needs
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"
WHAT="${1:-all}"
JOBS="$(nproc)"
mkdir -p "$VRDEV/stamps" "$VR_SRC" "$VR_LP" "$VR_WP" "$VRDEV/build/deps-linux" "$VRDEV/build/deps-win"
log() { echo; echo "==== $*"; }
done_() { touch "$VRDEV/stamps/$1"; }
is_done() { [ -f "$VRDEV/stamps/$1" ]; }

# ---------------------------------------------------------------- system packages
PKGS="build-essential cmake ninja-build autoconf automake libtool pkg-config git curl xz-utils zip unzip python3
      xvfb libx11-dev libxext-dev libxrender-dev libpng-dev zlib1g-dev libgl1 libglx-mesa0 libgl1-mesa-dri libegl1"
missing=""
for p in $PKGS; do dpkg -s "$p" >/dev/null 2>&1 || missing="$missing $p"; done
if [ -n "$missing" ]; then
	log "installing system packages:$missing"
	if command -v apt-get >/dev/null; then
		SUDO=""; [ "$(id -u)" = 0 ] || SUDO=sudo
		$SUDO apt-get update -qq && $SUDO apt-get install -y -qq $missing
	else
		echo "Please install:$missing"; exit 1
	fi
fi

# ---------------------------------------------------------------- sources (pinned)
# name | git url | commit (version)
SOURCES="
SDL-1.2|https://github.com/libsdl-org/SDL-1.2.git|9c868c5ecc3e183615a4e3e5a2445f858026dec5
SDL_image|https://github.com/libsdl-org/SDL_image.git|2ffb2e3e1eba037897164e3ac6c67570d8bccd79
SDL_mixer|https://github.com/libsdl-org/SDL_mixer.git|eb3e457a8850d4c9c4f52e8818eaf32fee7ce9c1
SDL_gfx|https://github.com/ferzkopp/SDL_gfx.git|4cc9485e8b36fab6126ee390c5746bbe16dcd3e6
OpenXR-SDK|https://github.com/KhronosGroup/OpenXR-SDK.git|f2448a8797c85814aa892efc1ab8707900fbcc78
glm|https://github.com/g-truc/glm.git|6f14f4792a0cde5d0cf2c910506724d61cb95834
libglvnd|https://github.com/NVIDIA/libglvnd.git|606f6627cf481ee6dcb32387edc010c502cdf38b
libpng|https://github.com/pnggroup/libpng.git|cd952f49f95bb27154ae77dbb103032d95f6e580
ogg|https://github.com/xiph/ogg.git|06a5e0262cdc28aa4ae6797627a783b5010440f0
vorbis|https://github.com/xiph/vorbis.git|1b75110b5a2754ba1931d82dd83cb822b266a21d
zlib|https://github.com/madler/zlib.git|da607da739fa6047df13e66a2af6b8bec7c2a498
"
# SDL 1.2.16, SDL_image 1.2.13, SDL_mixer 1.2.13, SDL_gfx 2.0.25, OpenXR 1.1.63, libpng 1.6.59,
# libogg 1.3.6, libvorbis 1.3.7, zlib 1.3.2 (see vr-dist/THIRD-PARTY-NOTICES.txt)
fetch() {
	local name=$1 url=$2 sha=$3 dir="$VR_SRC/$1"
	if [ -d "$dir/.git" ] && [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" = "$sha" ]; then return; fi
	log "fetching $name"
	rm -rf "$dir"; mkdir -p "$dir"
	git -C "$dir" init -q
	git -C "$dir" remote add origin "$url"
	git -C "$dir" fetch -q --depth 1 origin "$sha"
	git -C "$dir" checkout -q FETCH_HEAD
}
echo "$SOURCES" | while IFS="|" read -r n u s; do if [ -n "$n" ]; then fetch "$n" "$u" "$s"; fi; done

# fresh copy of a source tree for an in-tree (autotools) build
copy_src() { rm -rf "$2"; cp -a "$VR_SRC/$1" "$2"; rm -rf "$2/.git"; }

# ---------------------------------------------------------------- Linux (desktop preview)
build_linux() {
	(
	vr_linux_env
	B="$VRDEV/build/deps-linux"
	if ! is_done linux-headers; then
		# first: SDL's configure only builds its OpenGL support when it finds these headers
		log "Linux: glm and OpenGL headers"
		rm -rf "$VR_LP/include/glm"; cp -a "$VR_SRC/glm/glm" "$VR_LP/include/glm"
		# no OpenGL -dev package needed: headers from libglvnd, link names for the runtime libraries
		for d in GL KHR EGL GLES2; do [ -d "$VR_SRC/libglvnd/include/$d" ] && { rm -rf "$VR_LP/include/$d"; cp -a "$VR_SRC/libglvnd/include/$d" "$VR_LP/include/$d"; }; done
		for l in GL EGL; do
			sys=$(ls /usr/lib/x86_64-linux-gnu/lib$l.so.1 2>/dev/null || true)
			[ -n "$sys" ] && ln -sf "$sys" "$VR_LP/lib/lib$l.so"
		done
		# SDL 1.2's SDL_opengl.h includes GL/glu.h, which libglvnd doesn't have; the game calls no GLU
		# function, so without the system's (libglu1-mesa-dev) a stand-in that only includes gl.h will do
		if [ ! -f /usr/include/GL/glu.h ]; then
			printf '%s\n' '/* Stand-in for GL/glu.h (libglu1-mesa-dev), desktop preview only: SDL_opengl.h includes it,' \
				' * but the game never calls a GLU function. Written by vr-dev/setup.sh. */' \
				'#ifndef OXCE_VR_GLU_STUB' '#define OXCE_VR_GLU_STUB' '#include <GL/gl.h>' '#endif' > "$VR_LP/include/GL/glu.h"
		fi
		done_ linux-headers
	fi
	if ! is_done linux-sdl; then
		log "Linux: SDL 1.2"
		copy_src SDL-1.2 "$B/SDL-1.2"; cd "$B/SDL-1.2"
		./configure -q --prefix="$VR_LP" --disable-static --enable-video-x11 --enable-video-opengl
		make -j"$JOBS" >/dev/null && make install >/dev/null
		done_ linux-sdl
	fi
	if ! is_done linux-sdl-image; then
		log "Linux: SDL_image"
		copy_src SDL_image "$B/SDL_image"; cd "$B/SDL_image"
		./configure -q --prefix="$VR_LP" --disable-static --with-sdl-prefix="$VR_LP" --disable-jpg --disable-tif --disable-webp
		make -j"$JOBS" >/dev/null && make install >/dev/null
		done_ linux-sdl-image
	fi
	if ! is_done linux-sdl-mixer; then
		log "Linux: SDL_mixer"
		copy_src SDL_mixer "$B/SDL_mixer"; cd "$B/SDL_mixer"
		./configure -q --prefix="$VR_LP" --disable-static --with-sdl-prefix="$VR_LP" \
			--disable-music-mp3 --disable-music-flac --disable-music-ogg --disable-music-mod
		make -j"$JOBS" >/dev/null && make install >/dev/null
		done_ linux-sdl-mixer
	fi
	if ! is_done linux-sdl-gfx; then
		log "Linux: SDL_gfx"
		copy_src SDL_gfx "$B/SDL_gfx"; cd "$B/SDL_gfx"
		autoreconf -fi >/dev/null 2>&1 || { echo "autoreconf failed in SDL_gfx"; exit 1; } # the shipped configure script is too old for this system
		./configure -q --prefix="$VR_LP" --disable-static --with-sdl-prefix="$VR_LP" --disable-mmx
		make -j"$JOBS" >/dev/null && make install >/dev/null
		done_ linux-sdl-gfx
	fi
	if ! is_done linux-openxr; then
		log "Linux: OpenXR loader"
		cmake -S "$VR_SRC/OpenXR-SDK" -B "$B/openxr" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$VR_LP" \
			-DDYNAMIC_LOADER=ON -DBUILD_TESTS=OFF -DPRESENTATION_BACKEND=xlib -DBUILD_WITH_XLIB_HEADERS=ON >/dev/null
		cmake --build "$B/openxr" -j"$JOBS" >/dev/null && cmake --install "$B/openxr" >/dev/null
		done_ linux-openxr
	fi
	)
}

# ---------------------------------------------------------------- Windows (static, cross)
build_windows() {
	if [ ! -x "$VR_TC/bin/x86_64-w64-mingw32-clang" ]; then
		log "downloading llvm-mingw 20250924"
		curl -fsSL -o "$VRDEV/llvm-mingw.tar.xz" \
			https://github.com/mstorsjo/llvm-mingw/releases/download/20250924/llvm-mingw-20250924-ucrt-ubuntu-22.04-x86_64.tar.xz
		tar -C "$VRDEV" -xf "$VRDEV/llvm-mingw.tar.xz" && rm -f "$VRDEV/llvm-mingw.tar.xz"
	fi
	cat > "$VRDEV/win/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(TC $VR_TC)
set(CMAKE_C_COMPILER \${TC}/bin/x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER \${TC}/bin/x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER \${TC}/bin/x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH $VR_WP \${TC}/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
	(
	vr_win_env
	B="$VRDEV/build/deps-win"
	TCF="$VRDEV/win/toolchain.cmake"
	wcmake() { # name src options...
		local n=$1 s=$2; shift 2
		cmake -S "$s" -B "$B/$n" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TCF" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$VR_WP" "$@" >/dev/null
		cmake --build "$B/$n" -j"$JOBS" >/dev/null && cmake --install "$B/$n" >/dev/null
	}
	is_done win-zlib || { log "Windows: zlib"; wcmake zlib "$VR_SRC/zlib" -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=OFF -DZLIB_BUILD_MINIZIP=OFF
		cp "$VR_WP/lib/libzs.a" "$VR_WP/lib/libz.a"; done_ win-zlib; } # zlib 1.3.2 names its static library libzs.a
	is_done win-libpng || { log "Windows: libpng"; wcmake libpng "$VR_SRC/libpng" -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF \
		-DZLIB_ROOT="$VR_WP" -DZLIB_LIBRARY="$VR_WP/lib/libz.a" -DZLIB_INCLUDE_DIR="$VR_WP/include"; done_ win-libpng; }
	is_done win-ogg || { log "Windows: libogg"; wcmake ogg "$VR_SRC/ogg" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DINSTALL_DOCS=OFF; done_ win-ogg; }
	is_done win-vorbis || { log "Windows: libvorbis"; wcmake vorbis "$VR_SRC/vorbis" -DBUILD_SHARED_LIBS=OFF -DOGG_ROOT="$VR_WP"; done_ win-vorbis; }
	is_done win-openxr || { log "Windows: OpenXR loader (static)"; wcmake openxr "$VR_SRC/OpenXR-SDK" -DDYNAMIC_LOADER=OFF -DBUILD_TESTS=OFF \
		-DCMAKE_C_FLAGS=-DWINAPI_PARTITION_SYSTEM=0 -DCMAKE_CXX_FLAGS=-DWINAPI_PARTITION_SYSTEM=0; done_ win-openxr; }
	is_done win-glm || { rm -rf "$VR_WP/include/glm"; cp -a "$VR_SRC/glm/glm" "$VR_WP/include/glm"; done_ win-glm; }
	# rapidyaml includes <alloca.h>, which mingw doesn't have (it's in malloc.h there)
	[ -f "$VR_WP/include/alloca.h" ] || printf '#pragma once\n#include <malloc.h>\n' > "$VR_WP/include/alloca.h"
	acfg() { ./configure -q --host=$HOSTT --prefix="$VR_WP" --disable-shared --enable-static "$@"; make -j"$JOBS" >/dev/null && make install >/dev/null; }
	if ! is_done win-sdl; then
		log "Windows: SDL 1.2"; copy_src SDL-1.2 "$B/SDL-1.2"; cd "$B/SDL-1.2"
		acfg --disable-directx --enable-video-opengl; done_ win-sdl
	fi
	if ! is_done win-sdl-image; then
		log "Windows: SDL_image"; copy_src SDL_image "$B/SDL_image"; cd "$B/SDL_image"
		autoreconf -fi >/dev/null 2>&1 || { echo "autoreconf failed in SDL_image"; exit 1; } # needed for a static mingw build
		# only the library: the showimage demo doesn't link statically (libpng needs zlib after it)
		./configure -q --host=$HOSTT --prefix="$VR_WP" --disable-shared --enable-static --with-sdl-prefix="$VR_WP" \
			--disable-jpg --disable-tif --disable-webp --disable-png-shared
		make -j"$JOBS" libSDL_image.la >/dev/null
		make install-libLTLIBRARIES install-libSDL_imageincludeHEADERS install-pkgconfigDATA >/dev/null
		done_ win-sdl-image
	fi
	if ! is_done win-sdl-mixer; then
		log "Windows: SDL_mixer"; copy_src SDL_mixer "$B/SDL_mixer"; cd "$B/SDL_mixer"
		# only the library: the playwave/playmus demos don't link statically
		./configure -q --host=$HOSTT --prefix="$VR_WP" --disable-shared --enable-static --with-sdl-prefix="$VR_WP" \
			--disable-music-mp3 --disable-music-flac --disable-music-mod --enable-music-ogg --disable-music-ogg-shared --enable-music-midi
		make -j"$JOBS" build/libSDL_mixer.la >/dev/null
		make install-hdrs install-lib >/dev/null
		done_ win-sdl-mixer
	fi
	if ! is_done win-sdl-gfx; then
		log "Windows: SDL_gfx"; copy_src SDL_gfx "$B/SDL_gfx"; cd "$B/SDL_gfx"
		acfg --with-sdl-prefix="$VR_WP" --disable-mmx; done_ win-sdl-gfx
	fi
	)
}

case "$WHAT" in
	linux) build_linux ;;
	windows) build_windows ;;
	all) build_linux; build_windows ;;
	*) echo "usage: $0 [all|linux|windows]"; exit 1 ;;
esac
log "setup done ($WHAT). Next: vr-dev/build-preview.sh, vr-dev/build-windows.sh"
