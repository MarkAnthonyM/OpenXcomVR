#!/usr/bin/env python3
"""Regression run for the VR prototype's desktop preview.

Runs each test script with its save and checks the game's log for the expected outcome.
    python3 vr-dev/test/regress.py            # all
    python3 vr-dev/test/regress.py v3 i3      # some
The screenshots in $VRDEV/run/shots are worth a look too (listed at the end).
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
VRDEV = os.environ.get("VRDEV", os.path.expanduser("~/vrdev"))
LOG = os.path.join(VRDEV, "run", "user", "openxcom.log")

def boards(log):
    out = []
    for m in re.finditer(r"\[VR\] board: focus ([-\d.e]+),([-\d.e]+) target ([-\d.e]+),([-\d.e]+).*?onTable ([-\d.e]+),([-\d.e]+).*?inShot (\d)(?: level (\d+))?", log):
        f = [float(x) for x in m.groups()[:6]]
        out.append({"focus": f[0:2], "target": f[2:4], "onTable": f[4:6], "inShot": m.group(7) == "1",
                    "level": int(m.group(8)) if m.group(8) else None})
    return out

def near_spot(b):  # a focused soldier stands ~0.24 m toward the player from the map's middle
    return 0.18 <= b["onTable"][1] <= 0.28 and abs(b["onTable"][0]) < 0.06

def has(*texts):
    return lambda log: all(t in log for t in texts) or "missing one of: " + ", ".join(texts)

def check_v1(log):
    b = boards(log)
    if len(b) < 3: return "too few board readouts"
    if not near_spot(b[0]): return "soldier not brought close at load: %s" % b[0]["onTable"]
    if any(abs(x["focus"][0] - b[0]["focus"][0]) > 1 or abs(x["focus"][1] - b[0]["focus"][1]) > 1 for x in b[1:3]):
        return "tapping a soldier moved the map"
    return True

def check_last_near(log):
    b = boards(log)
    return bool(b) and near_spot(b[-1]) or "last readout not at the near spot: %s" % (b[-1]["onTable"] if b else None)

def check_v3(log):
    b = boards(log)
    if not any(x["inShot"] for x in b): return "the shot was never followed"
    if abs(b[-1]["target"][0] - b[0]["target"][0]) > 1 or abs(b[-1]["target"][1] - b[0]["target"][1]) > 1:
        return "the table did not go back after the shot"
    return True

def check_v4(log):
    lv = [x["level"] for x in boards(log)]
    return lv == [0, 1, 3, 3, 0] or "levels %s, expected [0, 1, 3, 3, 0]" % lv

def check_float(log):
    st = re.findall(r"\[VR\] status: .*", log)
    if "float: open N8OpenXcom10PauseStateE" not in log: return "the options pop-up never floated"
    if "float: touch 128,158" not in log: return "the finger touch on Cancel didn't click"
    return (bool(st) and "floats=0" in st[-1]) or "the window didn't close after Cancel"

def check_infobox(log):
    st = re.findall(r"\[VR\] status: .*", log)
    return (bool(st) and "InfoboxStateE" in st[-1]) or "the message was gone 3 s after it appeared"

def check_menu(log):
    if "float: open N8OpenXcom13MainMenuStateE" in log: return "the main menu (a full screen) floated"
    return "[VR] status:" in log or "the script didn't run"

def check_runs(log):
    return bool(boards(log)) or "no board readout"

TESTS = [  # script, save, check, what it covers
    ("v1", "outside.asav", check_v1, "first soldier brought close; tapping soldiers keeps the view"),
    ("v2", "outside.asav", check_last_near, "after a walk the soldier is brought close"),
    ("v3", "outside.asav", check_v3, "the table follows a shot and goes back"),
    ("v4", "outside.asav", check_v4, "left trackpad swipes step the view level"),
    ("v5", "outside.asav", check_last_near, "next-soldier button brings the soldier close"),
    ("a2", "battle_test.asav", has("last='button alert0'"), "red spotted-alien button"),
    ("d1", "battle_test.asav", has("turned soldier toward 3,1"), "drag away from a soldier to turn it"),
    ("t6", "battle_test.asav", has("swapped STR_SMOKE_GRENADE with STR_RIFLE"), "tray: swap with the item in a hand"),
    ("i3", "battle_test.asav", has("warning STR_WEAPON_IS_ALREADY_LOADED", "loaded STR_RIFLE_CLIP into STR_RIFLE"), "tray: reload rules"),
    ("u1", "battle_test.asav", has("unloaded STR_RIFLE", "warning STR_NO_AMMUNITION_LOADED"), "tray: unload pad"),
    ("g1", "battle_test.asav", has("ground page 2/"), "tray: ground pages"),
    ("h2", "battle_test.asav", check_runs, "alien turn / hidden movement (look at shots/h1_*.png)"),
    ("nt2", "night.asav", check_runs, "night darkness (look at shots/night_table.png)"),
    ("gl1", "outnight.asav", check_runs, "plasma glow at night (look at shots/gl_*.png)"),
    ("gk", "geo_test.asav", has("last='button geo1Hour'"), "geoscape keys on the console (dome: shots/gk_dome.png)"),
    ("fl", "geo_test.asav", check_float, "pop-up floats over the table, a finger touch clicks it (shots/fl_open.png)"),
    ("kb", "geo_test.asav", has("keyboard='Vr test'"), "table keyboard types a save name (shots/kb_keyboard.png)"),
    ("df", "geo_test.asav", has("float: open N8OpenXcom13DogfightStateE"), "a dogfight floats over the table (shots/df_dogfight.png)"),
    ("sd", "geo_test.asav", has("SelectDestinationStateE 0,0 256x28", "globe tap", "float: open N8OpenXcom23ConfirmDestinationStateE"),
     "destination bar beyond the globe; a fingertip on the dome picks the spot"),
    ("ib", "outside.asav", check_infobox, "a self-closing battle message stays readable (shots/ib_infobox.png)"),
    ("db", "geo_test.asav", has("screens=10111"), "geoscape dashboards on the back wall (shots/db_wall.png)"),
    ("bt", "geo_test.asav", has("tap STR_HANGAR", "float: open N8OpenXcom14CraftInfoStateE"), "base on the table; tapping a hangar opens its craft (shots/bt_base.png)"),
    ("bb", "geo_test.asav", has("build at 1,1", "place at 1,1", "sites=1"), "tap an empty spot, pick a laboratory, tap to place it (shots/bb_scaffold.png)"),
    ("bh", "geo_test.asav", has("hold STR_LABORATORY", "float: open N8OpenXcom17ErrorMessageStateE"), "holding a facility asks to dismantle it (here: refused, the game's rule)"),
    ("bd", "geo_test.asav", has("float: scrolled at"), "dragging a finger on a floating list scrolls it (shots/bd_scrolled.png)"),
    ("wx", "outside.asav", has("weather=rain", "weather=snow", "sky=dusk", "weather=none+mist", "sky=night", "strikes=1"),
     "sun and weather over the battle (shots/wx_*.png: rain, lightning, snow, dust, sand, clouds, mist, dusk, night)"),
    ("dbg", "outside.asav", has("debug panel: RAIN", "debug panel: TURNR", "dbg=shown page=1", "sky=day sun=137,50"),
     "the sky debug panel: info and controls pages (shots/dbg_*.png)"),
    ("sky", "outside.asav", has("moon=99%,70", "night (moon mirrored)", "clouds="),
     "low sun, cloud shadows, real full moon, the moon mirrored up when it is down (shots/sky_*.png)"),
    ("sky2", "outnight.asav", has("sky=vanilla", "moon=99%,70"),
     "night: the soldiers' light is its own lamp; vanilla mode (shots/sky2_*.png)"),
    ("moon", "outnight.asav", has("debug panel: HALFDAY", "moon=99%,70 ", "debug panel: NOW"),
     "the panel's sky clock: +12 h brings New Battle's full moon up; +moon steps its phase (shots/moon_*.png)"),
    ("mark", "outside.asav", has("mark=sun+90,moon-", "mark=sun-90,moon+7", "mark=sun+0,moon-1"),
     "debug panel on: the sun, the moon and the table light shown over the map (shots/skymark_*.png)"),
    ("pools", "outnight.asav", has("sky=night sun=359,70", "moon=99%,-69,mirrored", "sky=dusk", "sky=dawn"),
     "soldiers' light in the sky light's colour: full moon, mirrored moon, dusk, pink dawn (shots/pools_*.png)"),
    ("clouds", "outside.asav", has("sky=day sun=137,50 weather=none clouds=68", "sky=dusk"),
     "cloud shadows: separate clouds with crisp, cool edges, by day and at dusk (shots/clouds_*.png)"),
    ("storms", "outside.asav", has("weather=rain clouds=30", "strength=0", "clouds=75", "strikes=1", "weather=snow clouds=70", "strength=2"),
     "rain and snow strengths: drizzle, thunderstorm with a strike, flurries, blizzard (shots/storms_*.png)"),
    ("mm", None, check_menu, "the main menu at start doesn't flash a floating window"),
    ("br", None, has("float: open N8OpenXcom13BriefingStateE", "float: touch 160,172"), "the briefing floats; touching OK starts the battle"),
]

# tests that run another test's script with extra options: name -> (script, [args])
VARIANTS = {"dbg": ("dbg", ["-vrDebugPanel", "true"]), "mark": ("skymark", ["-vrDebugPanel", "true"])}

def main():
    want = set(sys.argv[1:])
    run = os.path.join(HERE, "run.sh")
    failed = []
    for name, save, check, what in TESTS:
        if want and name not in want: continue
        script, extra = VARIANTS.get(name, (name, []))
        subprocess.run([run, script] + (["-load", save] if save else []) + extra, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        log = open(LOG, errors="replace").read() if os.path.exists(LOG) else ""
        r = check(log) if "Segmentation" not in log else "crash"
        ok = r is True
        print("%-4s %-4s %s%s" % (name, "ok" if ok else "FAIL", what, "" if ok else "  -> " + str(r)))
        if not ok:
            failed.append(name)
            # keep the evidence: the next test overwrites the log
            if os.path.exists(LOG):
                import shutil
                shutil.copy(LOG, os.path.join(VRDEV, "run", "fail_%s.log" % name))
    print("\nscreenshots: %s" % os.path.join(VRDEV, "run", "shots"))
    print("all passed" if not failed else "FAILED: " + " ".join(failed))
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
