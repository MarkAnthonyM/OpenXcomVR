# Source this: paths shared by the VR prototype's build and test scripts.
#   . vr-dev/env.sh
# Everything that is built or downloaded lives in $VRDEV (default ~/vrdev), outside the repo.

VR_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export VR_REPO
export VRDEV="${VRDEV:-$HOME/vrdev}"

export VR_SRC="$VRDEV/src"            # dependency sources (pinned commits, see setup.sh)
export VR_LP="$VRDEV/linux/prefix"    # Linux libraries for the desktop preview build
export VR_WP="$VRDEV/win/prefix"      # static Windows libraries
export VR_TC="$VRDEV/llvm-mingw-20250924-ucrt-ubuntu-22.04-x86_64"
export VR_RUN="$VRDEV/run"            # test harness: game data, saves, config, screenshots
export VR_PREVIEW_BUILD="$VRDEV/build/preview"
export VR_WIN_BUILD="$VRDEV/build/windows"
export VR_DIST="$VRDEV/dist"

# Windows cross-compiling environment (used inside a subshell by the scripts)
vr_win_env() {
	export PATH="$VR_TC/bin:$PATH"
	export HOSTT=x86_64-w64-mingw32
	export CC=$HOSTT-clang CXX=$HOSTT-clang++ AR=$HOSTT-ar RANLIB=$HOSTT-ranlib WINDRES=$HOSTT-windres STRIP=$HOSTT-strip
	export PKG_CONFIG_LIBDIR="$VR_WP/lib/pkgconfig" PKG_CONFIG_PATH=
	export CPPFLAGS="-I$VR_WP/include" LDFLAGS="-L$VR_WP/lib"
	export ACLOCAL_PATH="$VR_WP/share/aclocal" # sdl.m4, for autoreconf
}

# Linux preview environment
vr_linux_env() {
	export PKG_CONFIG_PATH="$VR_LP/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
	export LD_LIBRARY_PATH="$VR_LP/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
	export PATH="$VR_LP/bin:$PATH"
	export CPPFLAGS="-I$VR_LP/include" LDFLAGS="-L$VR_LP/lib"
	export ACLOCAL_PATH="$VR_LP/share/aclocal" # sdl.m4, for autoreconf
}

export -f vr_win_env vr_linux_env 2>/dev/null || true
