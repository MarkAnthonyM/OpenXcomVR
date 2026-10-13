#!/bin/bash
# Prepares the test harness in $VR_RUN from your X-COM: UFO Defense data (not in the repo: copyrighted).
#   vr-dev/test/setup-data.sh /path/to/XCOM          (the folder with GEODATA, MAPS, UFOGRAPH, ...)
#   vr-dev/test/setup-data.sh /path/to/xcom.zip      (a zip of that folder)
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/../env.sh"
SRC="${1:?usage: setup-data.sh <XCOM folder or zip>}"
mkdir -p "$VR_RUN"/{data,user/xcom1,user/mods,cfg,shots}
if [ -f "$SRC" ]; then
	rm -rf "$VRDEV/xcomdata"; mkdir -p "$VRDEV/xcomdata"
	unzip -q "$SRC" -d "$VRDEV/xcomdata"
	SRC="$VRDEV/xcomdata"
fi
# find the folder that holds GEODATA (any case)
G=$(find "$SRC" -maxdepth 4 -type d -iname geodata | head -1)
[ -n "$G" ] || { echo "no GEODATA folder found under $SRC"; exit 1; }
UFO="$(cd "$(dirname "$G")" && pwd)"
ln -sfn "$UFO" "$VR_RUN/data/UFO"
ln -sfn "$VR_REPO/bin/common" "$VR_RUN/data/common"
ln -sfn "$VR_REPO/bin/standard" "$VR_RUN/data/standard"
cp "$VR_REPO/vr-dev/test/options.cfg" "$VR_RUN/cfg/options.cfg"
cp "$VR_REPO"/vr-dev/test/saves/*.asav "$VR_RUN/user/xcom1/"
echo "test harness ready in $VR_RUN (game data: $UFO)"
