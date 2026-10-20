#!/bin/bash
# Runs the desktop preview with a test script (see HANDOFF.md, "Testing").
#   vr-dev/test/run.sh v1 -load outside.asav       (script name from vr-dev/test/scripts, or a path)
# Screenshots go to $VR_RUN/shots; the game's log is $VR_RUN/user/openxcom.log.
set -uo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/../env.sh"
vr_linux_env
S="$1"; shift
[ -f "$S" ] || S="$VR_REPO/vr-dev/test/scripts/${S%.txt}.txt"
[ -f "$S" ] || { echo "no script $S"; exit 1; }
# every run starts from the test settings: the game saves options given on the command line (such as
# -vrDebugPanel true) into its config, which would carry them into the next tests
cp "$VR_REPO/vr-dev/test/options.cfg" "$VR_RUN/cfg/options.cfg"
pgrep -x Xvfb >/dev/null || { (setsid nohup Xvfb :99 -screen 0 1280x800x24 >/dev/null 2>&1 &); sleep 2; }
cd "$VR_RUN" && DISPLAY=:99 OXCE_VR_SCRIPT="$S" timeout 200 "$VR_PREVIEW_BUILD/bin/openxcom" \
	-data "$VR_RUN/data/" -user "$VR_RUN/user/" -cfg "$VR_RUN/cfg/" -vrMode 2 \
	-displayWidth 1280 -displayHeight 800 -playIntro false "$@" > "$VR_RUN/last.log" 2>&1
rc=$?
echo "exit $rc"
grep "\[VR\]\|ERROR" "$VR_RUN/user/openxcom.log" | grep -v "No available audio device" | tail -12
exit $rc
