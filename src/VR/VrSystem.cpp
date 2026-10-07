/*
 * OXCE VR tabletop - main VR driver.
 *
 * Owns the OpenXR runtime (or the desktop preview camera), the command
 * center scene, the floating game screen and the controller -> mouse bridge.
 * Scene content that depends on game state (battlescape diorama, geoscape
 * globe) lives in VrBoard and is updated here every frame.
 */
#include "VrGL.h" // must come first (GL extension headers)
#include "VrXr.h"
#include "VrShaders.h"
#include "VrRoom.h"
#include "VrBoard.h"
#include "VrHands.h"
#include "VrTable.h"
#include "VrApi.h"
#include "../Engine/Game.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Tile.h"
#include "../Battlescape/Position.h"
#include "../Engine/Screen.h"
#include "../Engine/Options.h"
#include "../Battlescape/Camera.h"
#include "../Battlescape/Map.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Engine/Logger.h"
#include "../Engine/Surface.h"
#include "../Engine/Font.h"
#include "../Interface/Text.h"
#include "../Mod/Mod.h"
#include "../Menu/StartState.h"
#include "../lodepng.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>
#include <deque>
#include <algorithm>

namespace OpenXcom
{
namespace VR
{

static const Uint8 SYNTH_TAG = 0xB7; // "which" value marking events we injected

struct Panel
{
	glm::vec3 pos{0.f};
	glm::quat rot{1.f, 0.f, 0.f, 0.f};
	float width = 1.7f;
	float aspect = 0.625f; // height / width
	float height() const { return width * aspect; }
	glm::mat4 matrix() const { return glm::translate(glm::mat4(1.f), pos) * glm::mat4_cast(rot); }
	/// Ray test in world space; returns distance or -1. uv is (0,0) top-left.
	float hit(const glm::vec3 &o, const glm::vec3 &d, glm::vec2 &uv) const
	{
		glm::mat4 inv = glm::inverse(matrix());
		glm::vec3 lo = glm::vec3(inv * glm::vec4(o, 1.f));
		glm::vec3 ld = glm::vec3(inv * glm::vec4(d, 0.f));
		if (ld.z >= -1e-5f || lo.z <= 0.f) return -1.f; // only from the front
		float t = -lo.z / ld.z;
		glm::vec3 p = lo + ld * t;
		float hw = width * 0.5f, hh = height() * 0.5f;
		if (std::fabs(p.x) > hw || std::fabs(p.y) > hh) return -1.f;
		uv = glm::vec2(p.x / width + 0.5f, 0.5f - p.y / height());
		return t;
	}
};

struct Grab
{
	enum Kind { NONE, WORLD, BOARD } kind = NONE;
	glm::vec3 lastTracking{0.f}; // WORLD: hand position in tracking space last frame
};

struct ScriptCmd
{
	std::string op;
	std::vector<std::string> args;
};

struct State
{
	bool on = false;
	Mode mode = MODE_OFF;
	Game *game = nullptr;
	void *glContext = nullptr;
	XrRuntime xr;
	bool xrReady = false;

	// resources
	Shader shader;
	Mesh roomMesh, panelFrame, controllerMesh[2], laserMesh, dotMesh, quadMesh;
	Texture gameTex;
	Texture placardTex;
	bool placardTried = false;
	RenderTarget eyeTarget[2];
	RenderTarget previewTarget;
	ShadowMap shadow;
	bool shadowFailed = false, shadowOn = false;
	glm::mat4 shadowVP{1.f};
	// lights for this frame (0 = overhead key light with shadows)
	int lightCount = 0;
	glm::vec3 lightPos[12], lightCol[12];
	glm::vec3 gameGlow{0.f};
	float alertLevel = 0.f, night = 0.f;
	RoomLayout layout;
	Board board;
	Hands handsVis;
	Table table;
	std::vector<Collider> colliders;
	float grabHold[2] = {0.f, 0.f};
	bool laserOn[2] = {true, true};
	// desktop preview: a scripted right hand for testing
	bool simHand = false;
	Pose simGrip;
	glm::vec3 simTarget{0.f}, simStep{0.f};
	int simFrames = 0;
	float simIndex = 0.f, simOthers = 0.f;
	bool simThumb = false;

	// game frame
	std::vector<uint32_t> pixels;
	int surfW = 0, surfH = 0;
	bool pixelsDirty = false;

	// tracking
	glm::mat4 rig{1.f};          // tracking space -> world
	bool recentered = false;
	EyeView views[2];
	HandState hands[2];
	Pose head;
	Panel panel;
	int pointerHand = 1;
	Grab grab[2];
	bool twoHandWorld = false;
	float bHold = 0.f;              // Left B held (recenter / seat calibration)
	bool bCalibrated = false;
	bool padWasTouched[2] = {false, false};
	float padLast[2] = {0.f, 0.f};
	Uint32 wheelNext = 0;

	// virtual mouse (window coordinates the game understands)
	int vmX = 0, vmY = 0;
	Uint8 vmButtons = 0;
	bool leftSent = false, rightSent = false;
	bool pointerOnPanel = false;
	bool pointerOnBoard = false;
	glm::vec3 laserEnd[2];
	bool laserHit[2] = {false, false};

	// desktop preview
	glm::vec3 camPos{0.f, 1.65f, 0.25f};
	float camYaw = 0.f, camPitch = -15.f;
	bool camDrag = false;
	int realMouseX = 0, realMouseY = 0;
	Uint32 lastPreviewFrame = 0;
	int previewW = 0, previewH = 0;

	// automation (desktop preview only)
	std::deque<ScriptCmd> script;
	struct PendingButton { int frames; Uint8 button; bool down; };
	std::vector<PendingButton> pendingButtons;
	Uint32 scriptWaitUntil = 0;
	std::string pendingShot;
	int frameCount = 0;
	double time = 0.0;
};

static State *S = nullptr;

// ------------------------------------------------------------------ helpers

static glm::vec3 xfPoint(const glm::mat4 &m, const glm::vec3 &p) { return glm::vec3(m * glm::vec4(p, 1.f)); }
static glm::vec3 xfDir(const glm::mat4 &m, const glm::vec3 &d) { return glm::normalize(glm::vec3(m * glm::vec4(d, 0.f))); }

static void gameToWindow(const glm::vec2 &uv, int &wx, int &wy)
{
	Screen *scr = S->game->getScreen();
	double gx = std::floor(glm::clamp(uv.x, 0.f, 0.9999f) * S->surfW) + 0.5;
	double gy = std::floor(glm::clamp(uv.y, 0.f, 0.9999f) * S->surfH) + 0.5;
	wx = (int)(gx * scr->getXScale()) + scr->getCursorLeftBlackBand();
	wy = (int)(gy * scr->getYScale()) + scr->getCursorTopBlackBand();
}

static void pushMotion(int x, int y)
{
	SDL_Event ev{};
	ev.type = SDL_MOUSEMOTION;
	ev.motion.which = SYNTH_TAG;
	ev.motion.state = S->vmButtons;
	ev.motion.x = (Uint16)std::max(0, x);
	ev.motion.y = (Uint16)std::max(0, y);
	ev.motion.xrel = (Sint16)(x - S->vmX);
	ev.motion.yrel = (Sint16)(y - S->vmY);
	S->vmX = x;
	S->vmY = y;
	SDL_PushEvent(&ev);
}

static void pushButton(Uint8 button, bool down)
{
	SDL_Event ev{};
	ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	ev.button.which = SYNTH_TAG;
	ev.button.button = button;
	ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	ev.button.x = (Uint16)std::max(0, S->vmX);
	ev.button.y = (Uint16)std::max(0, S->vmY);
	if (button <= 3)
	{
		if (down) S->vmButtons |= SDL_BUTTON(button);
		else S->vmButtons &= ~SDL_BUTTON(button);
	}
	SDL_PushEvent(&ev);
}

static void pushKey(SDLKey key, Uint16 unicode)
{
	for (int down = 1; down >= 0; --down)
	{
		SDL_Event ev{};
		ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
		ev.key.which = SYNTH_TAG;
		ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
		ev.key.keysym.sym = key;
		ev.key.keysym.unicode = down ? unicode : 0;
		SDL_PushEvent(&ev);
	}
}

// ------------------------------------------------------------------ setup

static void resetPanel()
{
	S->panel.pos = S->layout.panelPos;
	S->panel.rot = glm::angleAxis(glm::radians(-S->layout.panelTiltDeg), glm::vec3(1, 0, 0));
	S->panel.width = S->layout.panelWidth;
}

static bool createResources()
{
	if (!loadGL()) return false;
	if (!S->shader.build("scene", kSceneVS, kSceneFS)) return false;

	MeshData room;
	{
		RoomLayout fresh; // resources can be rebuilt; scale from the default size each time
		S->layout.tableCenter = fresh.tableCenter;
		S->layout.tableSize = fresh.tableSize;
		S->layout.scaleTable(glm::clamp(Options::vrTableScale, 60, 150) / 100.f);
	}
	buildCommandCenter(room, S->layout);
	S->roomMesh.upload(room);

	for (int h = 0; h < 2; ++h)
	{
		MeshData c;
		buildController(c, h == 0);
		S->controllerMesh[h].upload(c);
	}
	MeshData laser;
	laser.addBox({-0.0012f, -0.0012f, -1.f}, {0.0012f, 0.0012f, 0.f}, glm::vec4(1), MAT_PLAIN);
	S->laserMesh.upload(laser);
	MeshData dot;
	dot.addSphere({0, 0, 0}, 1.f, 12, 8, glm::vec4(1), MAT_PLAIN);
	S->dotMesh.upload(dot);
	MeshData quad;
	quad.addQuad({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}, glm::vec4(1), MAT_PLAIN, {0, 1}, {1, 1}, {1, 0}, {0, 0});
	S->quadMesh.upload(quad);
	MeshData frame;
	frame.addBox({-0.5f, -0.5f, -0.04f}, {0.5f, 0.5f, -0.003f}, glm::vec4(0.06f, 0.065f, 0.075f, 1), MAT_GLOSSY);
	S->panelFrame.upload(frame);

	S->gameTex.create(320, 200, true);
	S->board.init(S->layout);
	S->handsVis.init();
	S->table.init(S->layout);
	S->board.setMapRect(S->table.mapRect());
	S->board.clickScreen = [](int gx, int gy, int button)
	{
		glm::vec2 uv((gx + 0.5f) / std::max(1, S->surfW), (gy + 0.5f) / std::max(1, S->surfH));
		int wx, wy;
		gameToWindow(uv, wx, wy);
		pushMotion(wx, wy);
		// let the game see the hover first: some dialogs ignore a press that arrives with the move
		S->pendingButtons.push_back({2, (Uint8)button, true});
		S->pendingButtons.push_back({3, (Uint8)button, false});
	};
	resetPanel();
	logGLErrors("createResources");
	return true;
}

static void loadScript()
{
	const char *path = std::getenv("OXCE_VR_SCRIPT");
	if (!path) return;
	std::ifstream f(path);
	std::string line;
	while (std::getline(f, line))
	{
		std::istringstream ss(line);
		ScriptCmd c;
		if (!(ss >> c.op) || c.op[0] == '#') continue;
		std::string a;
		while (ss >> a) c.args.push_back(a);
		S->script.push_back(c);
	}
	Log(LOG_INFO) << "[VR] loaded " << S->script.size() << " automation commands from " << path;
}

void configureOptions()
{
	if (Options::vrMode == MODE_OFF) return;
	// VR composes the game screen into a texture: that needs the OpenGL output path.
	Options::useOpenGL = true;
	Options::vSyncForOpenGL = false; // the headset paces the loop, not the monitor
	Options::allowResize = false;    // resizing would recreate the GL context under OpenXR
	Options::fullscreen = false;
	if (Options::pauseMode > 0) Options::pauseMode = 0; // keep running while the desktop window is unfocused
}

void startup(Game *game)
{
	if (Options::vrMode == MODE_OFF) return;
	S = new State();
	S->game = game;
	S->mode = (Mode)Options::vrMode;
	S->glContext = XrRuntime::currentGLContext();
	if (!S->glContext)
	{
		Log(LOG_ERROR) << "[VR] no OpenGL context - is useOpenGL disabled? Continuing without VR.";
		delete S; S = nullptr;
		return;
	}
	if (!createResources())
	{
		Log(LOG_ERROR) << "[VR] failed to create GL resources. Continuing without VR.";
		delete S; S = nullptr;
		return;
	}
	if (S->mode == MODE_HEADSET)
	{
		S->xrReady = S->xr.init("OpenXcom Extended VR Tabletop");
		if (!S->xrReady)
		{
			Log(LOG_ERROR) << "[VR] headset mode unavailable. Start with -vrMode 2 for the desktop preview, or -vrMode 0 for the normal game.";
			S->xr.shutdown();
			delete S; S = nullptr;
			return;
		}
		int w = S->xr.eyeWidth(), h = S->xr.eyeHeight();
		for (auto &t : S->eyeTarget)
			if (!t.create(w, h, 4)) { delete S; S = nullptr; return; }
		if (!S->xr.floorLevel())
			S->rig = glm::translate(glm::mat4(1.f), {0.f, 1.25f, 0.f}); // seated: put the eyes at seated height
	}
	else
	{
		loadScript();
		if (const char *cam = std::getenv("OXCE_VR_CAMERA"))
		{
			std::sscanf(cam, "%f,%f,%f,%f,%f", &S->camPos.x, &S->camPos.y, &S->camPos.z, &S->camYaw, &S->camPitch);
		}
	}
	S->on = true;
	SDL_GetMouseState(&S->vmX, &S->vmY);
	Log(LOG_INFO) << "[VR] started in " << (S->mode == MODE_HEADSET ? "headset" : "desktop preview") << " mode";
}

void shutdown()
{
	if (!S) return;
	S->xr.shutdown();
	delete S;
	S = nullptr;
}

bool active() { return S && S->on; }
bool preview() { return S && S->on && S->mode == MODE_PREVIEW; }
bool pacesLoop() { return S && S->on && S->mode == MODE_HEADSET && S->xr.isRunning(); }

// ------------------------------------------------------------------ controls placard

/// A small plate on the near edge of the table listing the controls, written with the game's own font.
static void buildPlacard()
{
	// wait until the mod (and its fonts) finished loading
	for (OpenXcom::State *st : S->game->getStates())
		if (dynamic_cast<StartState*>(st)) return;
	S->placardTried = true;
	Mod *mod = S->game->getMod();
	if (!mod || !S->game->getLanguage()) return;
	Font *big = mod->getFont("FONT_BIG", false), *small = mod->getFont("FONT_SMALL", false);
	if (!big || !small) return;
	const int W = 520, H = 44;
	Text text(W, H, 0, 0);
	text.initText(big, small, S->game->getLanguage());
	text.setSmall();
	text.setColor(1);
	text.setWordWrap(false);
	if (S->mode == MODE_HEADSET)
		text.setText(
			"POKE buttons, TAP a head to select, TAP a tile to move   RIGHT WRIST: minimap\n"
			"PINCH (finger on trigger + thumb on A) an item in the tray to move it\n"
			"LEFT STICK: slide map   RIGHT STICK: turn map   TRACKPAD SWIPE: zoom\n"
			"TRIGGER: laser click   A: right click   B: back   LEFT B: recenter (hold: seat height)");
	else
		text.setText(
			"DESKTOP PREVIEW - mouse aims from your eyes\n"
			"LEFT CLICK: click, pick a tile, press a table button, pick up / drop an item\n"
			"WHEEL over the table: map level / globe zoom\n"
			"hold MIDDLE BUTTON and move the mouse to look around");
	text.draw();
	std::vector<uint32_t> px((size_t)W * H);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			// glyph body = color+1..+2, the font's dark outline = higher offsets
			int v = text.getPixel(x, y);
			px[(size_t)y * W + x] = v == 0 ? 0x00000000u : v <= 2 ? 0xFFF4EDB8u : v <= 3 ? 0xFFC8C090u : 0xC0100C08u;
		}
	if (const char *dump = std::getenv("OXCE_VR_DUMP_PLACARD"))
	{
		std::vector<unsigned char> b((const unsigned char*)px.data(), (const unsigned char*)(px.data() + px.size()));
		lodepng::encode(dump, b, W, H);
	}
	S->placardTex.create(W, H, true);
	S->placardTex.update(px.data(), W, H);
}

// ------------------------------------------------------------------ game frame

bool onScreenFlip(SDL_Surface *s)
{
	if (!active() || !s) return false;
	S->surfW = s->w;
	S->surfH = s->h;
	S->pixels.resize((size_t)s->w * s->h);
	SDL_LockSurface(s);
	if (s->format->BitsPerPixel == 8 && s->format->palette)
	{
		uint32_t lut[256];
		SDL_Color *c = s->format->palette->colors;
		int n = s->format->palette->ncolors;
		for (int i = 0; i < 256; ++i)
		{
			SDL_Color k = i < n ? c[i] : SDL_Color{0, 0, 0, 0};
			lut[i] = (uint32_t)k.r | ((uint32_t)k.g << 8) | ((uint32_t)k.b << 16) | 0xFF000000u;
		}
		for (int y = 0; y < s->h; ++y)
		{
			const Uint8 *row = (const Uint8*)s->pixels + y * s->pitch;
			uint32_t *out = &S->pixels[(size_t)y * s->w];
			for (int x = 0; x < s->w; ++x) out[x] = lut[row[x]];
		}
	}
	else
	{
		for (int y = 0; y < s->h; ++y)
			for (int x = 0; x < s->w; ++x)
			{
				Uint32 px = 0;
				const Uint8 *p = (const Uint8*)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
				std::memcpy(&px, p, s->format->BytesPerPixel);
				Uint8 r, g, b;
				SDL_GetRGB(px, s->format, &r, &g, &b);
				S->pixels[(size_t)y * s->w + x] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;
			}
	}
	SDL_UnlockSurface(s);
	S->pixelsDirty = true;
	S->panel.aspect = (float)s->h / (float)s->w;
	return S->mode == MODE_PREVIEW;
}

// ------------------------------------------------------------------ mouse API

Uint8 getMouseState(int *x, int *y)
{
	if (!active()) return SDL_GetMouseState(x, y);
	if (x) *x = S->vmX;
	if (y) *y = S->vmY;
	return S->vmButtons;
}

void warpMouse(Uint16 x, Uint16 y)
{
	if (!active()) { SDL_WarpMouse(x, y); return; }
	pushMotion(x, y);
}

// ------------------------------------------------------------------ interaction

struct RayHit
{
	enum Target { NONE, PANEL, BOARD, TABLE } target = NONE;
	float t = 1e9f;
	glm::vec2 uv{0.f};
	BoardHit board;
	TableHit table;
};

static RayHit castRay(const glm::vec3 &o, const glm::vec3 &d)
{
	RayHit best;
	glm::vec2 uv;
	float t = S->panel.hit(o, d, uv);
	if (t > 0.f && t < best.t) { best.target = RayHit::PANEL; best.t = t; best.uv = uv; }
	BoardHit bh;
	if (S->board.raycast(o, d, bh) && bh.t < best.t)
	{
		best.target = RayHit::BOARD;
		best.t = bh.t;
		best.board = bh;
	}
	TableHit tbl;
	tbl.t = best.t;
	if (S->table.raycast(o, d, tbl) && tbl.t <= best.t)
	{
		best.target = RayHit::TABLE;
		best.t = tbl.t;
		best.table = tbl;
	}
	return best;
}

static void recenter()
{
	glm::vec3 H = xfPoint(S->rig, S->head.pos);
	glm::vec3 f = xfDir(S->rig, S->head.forward());
	float yaw = std::atan2(-f.x, -f.z);
	glm::vec3 target(0.f, H.y, 0.f);
	if (!S->xr.floorLevel()) target.y = 1.25f;
	target.y += Options::vrSeatOffset / 1000.f; // seat height calibration
	glm::mat4 M = glm::translate(glm::mat4(1.f), target) * glm::rotate(glm::mat4(1.f), -yaw, {0, 1, 0}) * glm::translate(glm::mat4(1.f), -H);
	S->rig = M * S->rig;
	S->recentered = true;
}

static void pointerTo(const RayHit &hit)
{
	S->pointerOnPanel = hit.target == RayHit::PANEL;
	S->pointerOnBoard = hit.target == RayHit::BOARD;
	S->table.pointerMove(hit.target == RayHit::TABLE ? &hit.table : nullptr);
	if (S->pointerOnPanel)
	{
		int wx, wy;
		gameToWindow(hit.uv, wx, wy);
		if (wx != S->vmX || wy != S->vmY) pushMotion(wx, wy);
	}
	else if (S->pointerOnBoard)
	{
		S->board.hover(hit.board);
	}
	if (!S->pointerOnBoard) S->board.hoverNone();
}

static TableContext tableContext()
{
	TableContext ctx;
	ctx.game = S->game;
	ctx.board = &S->board;
	ctx.pixels = &S->pixels;
	ctx.surfW = S->surfW;
	ctx.surfH = S->surfH;
	ctx.clickScreen = S->board.clickScreen;
	ctx.haptic = [](int hand, float amp, float sec) { if (S->mode == MODE_HEADSET) S->xr.haptic(hand, amp, sec); };
	for (int h = 0; h < 2; ++h) ctx.handBusy[h] = S->grab[h].kind != Grab::NONE;
	ctx.eye = S->mode == MODE_HEADSET ? xfPoint(S->rig, S->head.pos) : S->head.pos;
	ctx.pressKey = [](int key) { pushKey((SDLKey)key, (Uint16)(key < 128 ? key : 0)); };
	return ctx;
}

/// Hands (after input), then the table: button physics, pinching, taps. Colliders feed the next frame.
static void updateHandsAndTable(float dt, bool simulated)
{
	if (simulated)
	{
		S->handsVis.hide(0);
		if (S->simHand)
		{
			if (S->simFrames > 0) { S->simGrip.pos += S->simStep; --S->simFrames; }
			S->handsVis.simulate(1, S->simGrip, S->simIndex, S->simOthers, S->simThumb, dt, S->colliders);
		}
		else S->handsVis.hide(1);
	}
	else
	{
		S->handsVis.update(S->hands, S->rig, dt, S->colliders);
	}
	TableContext ctx = tableContext();
	S->table.update(ctx, S->handsVis, dt);
	S->colliders.clear();
	S->table.colliders(S->colliders);
}

/// Seat height: hold Left B with both hands resting where the tabletop should be. The player is moved up
/// or down so the table surface meets the palms; the offset is kept in the options.
static void calibrateSeat()
{
	int n = 0;
	float y = 0.f;
	for (int h = 0; h < 2; ++h)
		if (S->hands[h].active) { y += xfPoint(S->rig, S->hands[h].grip.pos).y; ++n; }
	if (!n) return;
	y /= n;
	const float palmBelowGrip = 0.035f;
	float delta = S->table.surfaceY() - (y - palmBelowGrip);
	S->rig = glm::translate(glm::mat4(1.f), {0.f, delta, 0.f}) * S->rig;
	Options::vrSeatOffset = (int)std::lround(Options::vrSeatOffset + delta * 1000.f);
	Options::save();
	for (int h = 0; h < 2; ++h) S->xr.haptic(h, 0.6f, 0.08f);
	Log(LOG_INFO) << "[VR] seat height calibrated: offset " << Options::vrSeatOffset << " mm";
}

static void updateHeadsetInput(float dt)
{
	HandState *H = S->hands;

	// who points: the last hand that pulled its trigger
	for (int h = 0; h < 2; ++h)
		if (H[h].active && H[h].triggerBtn.pressed) S->pointerHand = h;

	// grabbing must be deliberate: on Index the grip has to be squeezed (force sensor), elsewhere
	// pulled most of the way, and held for a moment. Resting fingers on the grip does nothing.
	for (int h = 0; h < 2; ++h)
	{
		HandState &hs = H[h];
		float v = hs.hasForce ? hs.squeezeForce : hs.squeeze;
		float on = hs.hasForce ? 0.32f : 0.88f, off = hs.hasForce ? 0.12f : 0.55f;
		bool above = hs.grabBtn.down ? v > off : v > on;
		S->grabHold[h] = above ? S->grabHold[h] + dt : 0.f;
		hs.grabBtn.update(hs.grabBtn.down ? above : S->grabHold[h] >= 0.12f);
	}
	const glm::vec4 tableRect(S->layout.tableCenter.x - S->layout.tableSize.x * 0.5f - 0.15f, S->layout.tableCenter.z - S->layout.tableSize.y * 0.5f - 0.15f,
		S->layout.tableCenter.x + S->layout.tableSize.x * 0.5f + 0.15f, S->layout.tableCenter.z + S->layout.tableSize.y * 0.5f + 0.2f);
	auto overTable = [&](const glm::vec3 &p)
	{
		return p.x > tableRect.x && p.x < tableRect.z && p.z > tableRect.y && p.z < tableRect.w && p.y < S->table.surfaceY() + 0.3f;
	};
	// grip: the map (fallback to the sticks), or the room
	for (int h = 0; h < 2; ++h)
	{
		Grab &g = S->grab[h];
		if (!H[h].active) { g.kind = Grab::NONE; continue; }
		glm::vec3 gripW = xfPoint(S->rig, H[h].grip.pos);
		if (H[h].grabBtn.pressed)
		{
			if (S->board.canGrab(gripW))
			{
				g.kind = Grab::BOARD;
				S->board.beginGrab(h, gripW);
			}
			else if (!overTable(gripW))
			{
				g.kind = Grab::WORLD;
				g.lastTracking = H[h].grip.pos;
			}
			if (g.kind != Grab::NONE) S->xr.haptic(h, 0.3f, 0.02f);
		}
		else if (H[h].grabBtn.released)
		{
			if (g.kind == Grab::BOARD) S->board.endGrab(h);
			g.kind = Grab::NONE;
		}
	}
	bool worldL = S->grab[0].kind == Grab::WORLD, worldR = S->grab[1].kind == Grab::WORLD;
	if (worldL && worldR)
	{
		glm::vec3 l0 = S->grab[0].lastTracking, r0 = S->grab[1].lastTracking;
		glm::vec3 l1 = H[0].grip.pos, r1 = H[1].grip.pos;
		glm::vec2 v0(r0.x - l0.x, r0.z - l0.z), v1(r1.x - l1.x, r1.z - l1.z);
		float delta = std::atan2(v1.y, v1.x) - std::atan2(v0.y, v0.x);
		glm::vec3 mid = (l1 + r1) * 0.5f, midPrev = (l0 + r0) * 0.5f;
		S->rig = S->rig * glm::translate(glm::mat4(1.f), midPrev) * glm::rotate(glm::mat4(1.f), delta, {0, 1, 0}) * glm::translate(glm::mat4(1.f), -mid);
		S->grab[0].lastTracking = l1;
		S->grab[1].lastTracking = r1;
	}
	else
	{
		for (int h = 0; h < 2; ++h)
		{
			Grab &g = S->grab[h];
			if (g.kind != Grab::WORLD) continue;
			glm::vec3 now = H[h].grip.pos;
			S->rig = S->rig * glm::translate(glm::mat4(1.f), g.lastTracking - now);
			g.lastTracking = now;
		}
	}
	for (int h = 0; h < 2; ++h)
		if (S->grab[h].kind == Grab::BOARD) S->board.updateGrab(h, xfPoint(S->rig, H[h].grip.pos));

	// Left B: tap = recenter, hold for a second = seat height from where the hands rest
	if (H[0].b.down)
	{
		S->bHold += dt;
		if (S->bHold >= 1.0f && !S->bCalibrated) { calibrateSeat(); S->bCalibrated = true; }
	}
	else if (H[0].b.released)
	{
		if (!S->bCalibrated) recenter();
		S->bHold = 0.f;
		S->bCalibrated = false;
	}

	// ---- map movement: left stick slides the view across the map (relative to where you look),
	// right stick left/right turns it, a swipe on either trackpad zooms
	{
		glm::vec3 f = xfDir(S->rig, S->head.forward());
		glm::vec2 fwd(f.x, f.z);
		if (glm::length(fwd) < 1e-3f) fwd = glm::vec2(0.f, -1.f);
		fwd = glm::normalize(fwd);
		glm::vec2 right(-fwd.y, fwd.x);
		glm::vec2 st = H[0].stick;
		float m = glm::length(st);
		if (H[0].active && m > 0.15f)
		{
			float speed = 0.45f * std::pow((m - 0.15f) / 0.85f, 1.5f); // metres of table per second
			glm::vec2 dir = (right * st.x + fwd * st.y) / m;
			S->board.pan(dir * speed * dt);
		}
		float rx = H[1].stick.x;
		if (H[1].active && std::fabs(rx) > 0.2f && !S->pointerOnPanel)
		{
			float r = (std::fabs(rx) - 0.2f) / 0.8f;
			S->board.rotate((rx > 0 ? 1.f : -1.f) * glm::radians(90.f) * r * r * dt); // like turning your view to the right
		}
		for (int h = 0; h < 2; ++h)
		{
			if (H[h].trackpadTouch && S->padWasTouched[h])
			{
				float dy = H[h].trackpad.y - S->padLast[h];
				if (std::fabs(dy) < 0.5f) S->board.zoom(std::exp(dy * 0.9f)); // swipe up = closer
			}
			S->padWasTouched[h] = H[h].trackpadTouch;
			S->padLast[h] = H[h].trackpad.y;
		}
	}

	// lasers: off while a hand works on the table, so pinching and poking never click from afar
	for (int h = 0; h < 2; ++h)
	{
		S->laserHit[h] = false;
		if (!H[h].active) { S->laserOn[h] = false; continue; }
		glm::vec3 o = xfPoint(S->rig, H[h].aim.pos), d = xfDir(S->rig, H[h].aim.forward());
		RayHit hit = castRay(o, d);
		glm::vec3 handPos = S->handsVis.pose[h].valid ? S->handsVis.pose[h].tip(F_INDEX) : o;
		S->laserOn[h] = !overTable(handPos) || hit.target == RayHit::PANEL;
		if (!S->laserOn[h])
		{
			if (h == S->pointerHand) { S->pointerOnPanel = false; S->pointerOnBoard = false; S->board.hoverNone(); S->table.pointerMove(nullptr); }
			continue;
		}
		S->laserHit[h] = hit.target != RayHit::NONE;
		S->laserEnd[h] = o + d * (S->laserHit[h] ? hit.t : 4.f);
		if (h == S->pointerHand && S->grab[h].kind == Grab::NONE)
		{
			bool wasOnPanel = S->pointerOnPanel;
			pointerTo(hit);
			if (S->pointerOnPanel && !wasOnPanel) S->xr.haptic(h, 0.15f, 0.01f);
		}
	}

	// buttons of the pointing hand
	HandState &P = H[S->pointerHand];
	if (P.triggerBtn.pressed && S->laserOn[S->pointerHand])
	{
		glm::vec3 o = xfPoint(S->rig, P.aim.pos), d = xfDir(S->rig, P.aim.forward());
		RayHit hit = castRay(o, d);
		if (S->pointerOnPanel) { pushButton(SDL_BUTTON_LEFT, true); S->leftSent = true; }
		else if (hit.target == RayHit::TABLE) { TableContext ctx = tableContext(); S->table.pointerClick(hit.table, SDL_BUTTON_LEFT, ctx); }
		else if (S->pointerOnBoard) S->board.click(SDL_BUTTON_LEFT);
		S->xr.haptic(S->pointerHand, 0.4f, 0.015f);
	}
	if (P.triggerBtn.released && S->leftSent) { pushButton(SDL_BUTTON_LEFT, false); S->leftSent = false; }
	// A clicks only with the laser out: over the table the thumb rests on A to pinch
	if (P.a.pressed && S->pointerHand == 1 && S->laserOn[1])
	{
		if (S->pointerOnPanel) { pushButton(SDL_BUTTON_RIGHT, true); S->rightSent = true; }
		else if (S->pointerOnBoard) S->board.click(SDL_BUTTON_RIGHT);
	}
	if (P.a.released && S->rightSent) { pushButton(SDL_BUTTON_RIGHT, false); S->rightSent = false; }
	if (H[1].b.pressed) pushKey(SDLK_ESCAPE, 27);

	// scroll wheel: right stick up/down while pointing at the screen
	float sy = H[1].stick.y;
	Uint32 now = SDL_GetTicks();
	if (S->pointerOnPanel && std::fabs(sy) > 0.55f && now >= S->wheelNext)
	{
		Uint8 b = sy > 0 ? SDL_BUTTON_WHEELUP : SDL_BUTTON_WHEELDOWN;
		pushButton(b, true);
		pushButton(b, false);
		S->wheelNext = now + (std::fabs(sy) > 0.9f ? 90 : 180);
	}
}

// ------------------------------------------------------------------ rendering

static void setCommonUniforms(const glm::mat4 &viewProj, const glm::vec3 &eye, bool shadowPass = false)
{
	Shader &sh = S->shader;
	sh.use();
	sh.set("uViewProj", viewProj);
	sh.set("uEye", eye);
	sh.set("uTime", (float)S->time);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
	sh.set("uUVRect", glm::vec4(0.f, 0.f, 1.f, 1.f));
	sh.set("uHoleCount", 0);
	sh.set("uTex", 0);
	sh.set("uLightCount", S->lightCount);
	for (int i = 0; i < S->lightCount; ++i)
	{
		std::string p = "uLightPos[" + std::to_string(i) + "]";
		std::string c = "uLightCol[" + std::to_string(i) + "]";
		sh.set(p.c_str(), S->lightPos[i]);
		sh.set(c.c_str(), S->lightCol[i]);
	}
	const RoomLayout &L = S->layout;
	sh.set("uAmbient", 1.f - 0.5f * S->night);
	sh.set("uAlert", S->alertLevel);
	sh.set("uRoomMin", glm::vec3(L.roomX0, 0.f, L.roomZ0));
	sh.set("uRoomMax", glm::vec3(L.roomX1, L.roomH, L.roomZ1));
	glm::vec3 T = L.tableCenter;
	float px = L.tableSize.x * 0.5f * 0.6f, pz = L.tableSize.y * 0.5f * 0.5f;
	sh.set("uPedestal", glm::vec4(T.x - px, T.z - pz, T.x + px, T.z + pz));
	float hx = L.tableSize.x * 0.5f, hz = L.tableSize.y * 0.5f;
	sh.set("uTableRect", glm::vec4(T.x - hx, T.z - hz, T.x + hx, T.z + hz));
	sh.set("uTableY", S->table.surfaceY());
	sh.set("uTableGlow", glm::vec3(0.12f, 0.55f, 0.65f) * (1.f + 0.3f * S->night));
	sh.set("uShadowPass", 0);
	bool useShadow = S->shadowOn && !shadowPass;
	sh.set("uShadowOn", useShadow ? 1 : 0);
	sh.set("uShadowMap", 1);
	sh.set("uShadowVP", S->shadowVP);
	sh.set("uShadowTexel", S->shadow.size() > 0 ? 1.f / S->shadow.size() : 0.f);
	gl.ActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, useShadow ? S->shadow.depthTex() : 0);
	gl.ActiveTexture(GL_TEXTURE0);
}

/// This frame's lights: the overhead key light (shadows), the table's own glow from below, dimmed
/// room fills, the screens' colours spilling into the room, a red alert beacon and battle flashes.
static void computeLights(float dt)
{
	const RoomLayout &L = S->layout;
	glm::vec3 T = L.tableCenter;
	// smoothed battle state
	float alertT = S->board.alert() ? 1.f : 0.f, nightT = S->board.nightLevel();
	S->alertLevel += (alertT - S->alertLevel) * (1.f - std::exp(-dt * 2.5f));
	S->night += (nightT - S->night) * (1.f - std::exp(-dt * 1.f));
	// the game screen's average colour, every few frames
	if (S->frameCount % 5 == 0 && !S->pixels.empty())
	{
		glm::vec3 sum(0.f);
		int n = 0;
		for (size_t i = 0; i < S->pixels.size(); i += 13, ++n)
		{
			uint32_t c = S->pixels[i];
			sum += glm::vec3((c & 255) / 255.f, ((c >> 8) & 255) / 255.f, ((c >> 16) & 255) / 255.f);
		}
		S->gameGlow = n ? sum / (float)n : glm::vec3(0.f);
	}
	int n = 0;
	auto add = [&](const glm::vec3 &p, const glm::vec3 &c) { if (n < 12) { S->lightPos[n] = p; S->lightCol[n] = c; ++n; } };
	float dim = 1.f - 0.45f * S->night;
	// 0: over the table, a little behind and to the left so shadows fall toward the player
	add({T.x - 0.5f, T.y + 1.3f, T.z - 0.45f}, glm::vec3(1.6f, 1.65f, 1.8f) * dim);
	float fill = 0.5f * (1.f - 0.5f * S->night) * (1.f - 0.4f * S->alertLevel);
	add(L.lightPos[1], L.lightCol[1] * fill);  // (1-3: room fills)
	add(L.lightPos[2], L.lightCol[2] * fill);
	add(L.lightPos[3], L.lightCol[3] * fill);
	add(S->panel.pos + glm::vec3(0.f, 0.f, 0.6f), S->gameGlow * 0.9f);          // the game screen's light
	std::vector<std::pair<glm::vec3, glm::vec3>> extra;
	S->table.screenGlow(extra);
	for (auto &e : extra) add(e.first, e.second * 0.55f);
	if (S->alertLevel > 0.02f)
	{
		// a red beacon sweeping round under the ceiling
		float a = (float)S->time * 1.7f;
		float rx = (L.roomX1 - L.roomX0) * 0.38f, rz = (L.roomZ1 - L.roomZ0) * 0.38f;
		glm::vec3 c((L.roomX0 + L.roomX1) * 0.5f, L.roomH - 0.45f, (L.roomZ0 + L.roomZ1) * 0.5f);
		add(c + glm::vec3(std::cos(a) * rx, 0.f, std::sin(a) * rz), glm::vec3(1.6f, 0.07f, 0.03f) * S->alertLevel);
	}
	// muzzle flashes, explosions, fires on the table: the brightest few
	extra.clear();
	S->board.lights(extra);
	std::sort(extra.begin(), extra.end(), [](const std::pair<glm::vec3, glm::vec3> &a, const std::pair<glm::vec3, glm::vec3> &b)
		{ return a.second.r + a.second.g + a.second.b > b.second.r + b.second.g + b.second.b; });
	for (auto &e : extra) add(e.first, e.second * 0.25f);
	S->lightCount = n;
}

/// Depth from the overhead light: figures, terrain and hands cast shadows onto the table.
static void renderShadows()
{
	S->shadowOn = false;
	if (!Options::vrShadows) return;
	if (!S->shadow.valid())
	{
		if (S->shadowFailed) return;
		if (!S->shadow.create(2048)) { S->shadowFailed = true; return; }
	}
	const RoomLayout &L = S->layout;
	glm::vec3 T = L.tableCenter, lp = S->lightPos[0];
	float hx = L.tableSize.x * 0.5f + 0.5f, hz = L.tableSize.y * 0.5f + 0.6f;
	glm::mat4 view = glm::lookAt(lp, T, glm::vec3(0.f, 0.f, -1.f));
	glm::mat4 proj = glm::ortho(-hx, hx, -hz, hz, 0.2f, 3.0f);
	S->shadowVP = proj * view;
	S->shadow.bind();
	glDisable(GL_FRAMEBUFFER_SRGB);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glClear(GL_DEPTH_BUFFER_BIT);
	Shader &sh = S->shader;
	setCommonUniforms(S->shadowVP, lp, true);
	sh.set("uShadowPass", 1);
	S->board.setShadowPass(true, lp);
	S->board.draw(sh, S->shadowVP, lp, S->time);
	S->board.setShadowPass(false);
	sh.use();
	sh.set("uShadowPass", 1);
	S->handsVis.draw(sh);
	sh.set("uShadowPass", 0);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	S->shadowOn = true;
}

static void drawScene(const glm::mat4 &view, const glm::mat4 &proj, const glm::vec3 &eye, bool drawHands)
{
	glm::mat4 vp = proj * view;
	glEnable(GL_FRAMEBUFFER_SRGB);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glClearColor(0.01f, 0.012f, 0.015f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	Shader &sh = S->shader;
	setCommonUniforms(vp, eye);
	sh.set("uMode", 0);
	sh.set("uModel", glm::mat4(1.f));
	// the table glass is cut open where hatches are open
	const std::vector<glm::vec4> &holes = S->table.holes();
	sh.set("uHoleCount", (int)std::min<size_t>(holes.size(), 40));
	for (size_t i = 0; i < holes.size() && i < 40; ++i) sh.set(("uHoles[" + std::to_string(i) + "]").c_str(), holes[i]);
	sh.set("uHoleY", glm::vec2(S->table.surfaceY() - 0.002f, S->table.surfaceY() + 0.003f));
	S->roomMesh.draw();
	sh.set("uHoleCount", 0);

	// game content on the table (diorama / globe), the table's own controls, live wall screens
	S->board.draw(sh, vp, eye, S->time);
	sh.use();
	S->table.draw(sh);
	S->table.drawWalls(sh);

	// game screen
	glm::mat4 pm = S->panel.matrix() * glm::scale(glm::mat4(1.f), {S->panel.width, S->panel.height(), 1.f});
	glm::mat4 fm = S->panel.matrix() * glm::scale(glm::mat4(1.f), {S->panel.width + 0.06f, S->panel.height() + 0.06f, 1.f});
	sh.set("uMode", 0);
	sh.set("uModel", fm);
	S->panelFrame.draw();
	sh.set("uMode", 1);
	sh.set("uModel", pm);
	sh.set("uTexSize", glm::vec2((float)S->gameTex.width(), (float)S->gameTex.height()));
	S->gameTex.bind(0);
	S->quadMesh.draw();
	// controls placard on the near rim of the table
	if (S->placardTex.valid())
	{
		const RoomLayout &L = S->layout;
		// on the front face of the table rim, facing the player
		glm::vec3 c(L.tableCenter.x, L.tableCenter.y - 0.055f, L.tableCenter.z + L.tableSize.y * 0.5f + L.tableRim + 0.005f);
		glm::mat4 m = glm::translate(glm::mat4(1.f), c) * glm::rotate(glm::mat4(1.f), glm::radians(-8.f), {1, 0, 0});
		sh.set("uMode", 0);
		sh.set("uModel", m * glm::scale(glm::mat4(1.f), {0.80f, 0.10f, 1.f}));
		S->panelFrame.draw();
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		sh.set("uMode", 4);
		sh.set("uModel", m * glm::translate(glm::mat4(1.f), {0.f, 0.f, 0.002f}) * glm::scale(glm::mat4(1.f), {0.76f, 0.0697f, 1.f}));
		sh.set("uTint", glm::vec4(1.f));
		S->placardTex.bind(0);
		S->quadMesh.draw();
		glDisable(GL_BLEND);
	}

	S->handsVis.draw(sh);
	if (!drawHands) return;
	// controllers, only when there is no hand to show
	sh.set("uMode", 0);
	for (int h = 0; h < 2; ++h)
	{
		if (!S->hands[h].active || S->handsVis.pose[h].valid) continue;
		sh.set("uModel", S->rig * S->hands[h].grip.matrix());
		S->controllerMesh[h].draw();
	}
	// lasers
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	sh.set("uMode", 2);
	for (int h = 0; h < 2; ++h)
	{
		if (!S->hands[h].active) continue;
		if (!S->laserOn[h]) continue;
		glm::vec3 o = xfPoint(S->rig, S->hands[h].aim.pos);
		glm::vec3 d = xfDir(S->rig, S->hands[h].aim.forward());
		float len = glm::length(S->laserEnd[h] - o);
		glm::quat q = glm::rotation(glm::vec3(0, 0, -1), d);
		sh.set("uModel", glm::translate(glm::mat4(1.f), o) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.f), {1.f, 1.f, len}));
		bool main = h == S->pointerHand;
		glm::vec4 tint = S->laserHit[h] ? glm::vec4(0.25f, 0.95f, 1.f, main ? 0.8f : 0.35f) : glm::vec4(0.6f, 0.6f, 0.65f, main ? 0.35f : 0.15f);
		sh.set("uTint", tint);
		S->laserMesh.draw();
		if (S->laserHit[h])
		{
			sh.set("uModel", glm::translate(glm::mat4(1.f), S->laserEnd[h]) * glm::scale(glm::mat4(1.f), glm::vec3(0.006f)));
			sh.set("uTint", glm::vec4(0.6f, 1.f, 1.f, 0.9f));
			S->dotMesh.draw();
		}
	}
	sh.set("uTint", glm::vec4(1.f));
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

static glm::mat4 viewFromPose(const Pose &eyeWorldPose)
{
	return glm::inverse(eyeWorldPose.matrix());
}

// ------------------------------------------------------------------ preview mode

static Pose previewHead()
{
	Pose p;
	p.pos = S->camPos;
	p.rot = glm::angleAxis(glm::radians(S->camYaw), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::radians(S->camPitch), glm::vec3(1, 0, 0));
	return p;
}

static void previewRay(int mx, int my, glm::vec3 &o, glm::vec3 &d)
{
	SDL_Surface *vs = SDL_GetVideoSurface();
	int w = vs ? vs->w : 640, h = vs ? vs->h : 400;
	float fovY = glm::radians(62.f);
	float aspect = (float)w / (float)h;
	float nx = ((mx + 0.5f) / w) * 2.f - 1.f;
	float ny = 1.f - ((my + 0.5f) / h) * 2.f;
	float th = std::tan(fovY * 0.5f);
	Pose head = previewHead();
	o = head.pos;
	d = glm::normalize(head.rot * glm::vec3(nx * th * aspect, ny * th, -1.f));
}

static void runScript()
{
	Uint32 now = SDL_GetTicks();
	while (!S->script.empty() && now >= S->scriptWaitUntil)
	{
		ScriptCmd c = S->script.front();
		S->script.pop_front();
		auto num = [&](size_t i, double def) { return i < c.args.size() ? std::atof(c.args[i].c_str()) : def; };
		if (c.op == "wait") { S->scriptWaitUntil = now + (Uint32)num(0, 0); }
		else if (c.op == "waitframes") { S->scriptWaitUntil = now + 1; if (num(0, 0) > 1) { c.args[0] = std::to_string((int)num(0, 0) - 1); S->script.push_front(c); } return; }
		else if (c.op == "move" || c.op == "click" || c.op == "rclick")
		{
			glm::vec2 uv((float)(num(0, 0) + 0.5) / std::max(1, S->surfW), (float)(num(1, 0) + 0.5) / std::max(1, S->surfH));
			int wx, wy;
			gameToWindow(uv, wx, wy);
			pushMotion(wx, wy);
			if (c.op != "move")
			{
				Uint8 b = c.op == "click" ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
				pushButton(b, true);
				pushButton(b, false);
			}
		}
		else if (c.op == "key")
		{
			int k = (int)num(0, 0);
			pushKey((SDLKey)k, (Uint16)(k < 128 ? k : 0));
		}
		else if (c.op == "cam")
		{
			S->camPos = glm::vec3(num(0, 0), num(1, 1.65), num(2, 0.25));
			S->camYaw = (float)num(3, 0);
			S->camPitch = (float)num(4, -24);
		}
		else if (c.op == "pointer")
		{
			// aim the preview "hand" at a window pixel: board hover/click tests
			S->realMouseX = (int)num(0, 0);
			S->realMouseY = (int)num(1, 0);
		}
		else if (c.op == "boardclick")
		{
			glm::vec3 o, d;
			previewRay(S->realMouseX, S->realMouseY, o, d);
			BoardHit bh;
			if (S->board.raycast(o, d, bh)) { S->board.hover(bh); S->board.click(c.args.size() > 0 && c.args[0] == "right" ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT); }
			else Log(LOG_INFO) << "[VR] script: boardclick missed the board";
		}
		else if (c.op == "wheel") { S->board.wheel((int)num(0, 1)); }
		else if (c.op == "boardpan") { S->board.pan(glm::vec2((float)num(0, 0), (float)num(1, 0))); }
		else if (c.op == "boardrotate") { S->board.rotate(glm::radians((float)num(0, 0))); }
		else if (c.op == "boardzoom") { S->board.zoom((float)num(0, 1)); }
		else if (c.op == "camunit")
		{
			// test helper: look at the selected soldier from behind and above (back, up in metres)
			if (SavedBattleGame *sb = S->board.battle())
				for (auto &m : S->board.unitMarkers())
					if (m.unit == sb->getSelectedUnit())
					{
						glm::vec3 eye = m.head + glm::vec3(0.f, (float)num(1, 0.25), (float)num(0, 0.35));
						glm::vec3 d = glm::normalize(m.head - eye);
						S->camPos = eye;
						S->camYaw = glm::degrees(std::atan2(-d.x, -d.z));
						S->camPitch = glm::degrees(std::asin(d.y));
					}
		}
		else if (c.op == "testburst")
		{
			if (SavedBattleGame *sb = S->board.battle())
				if (BattleUnit *sel = sb->getSelectedUnit())
				{
					Position p = sel->getPosition();
					S->board.testBurst(p.x + (int)num(0, 0), p.y + (int)num(1, 0), p.z, num(2, 1) > 0.5);
					if (Tile *t = sb->getTile(Position(p.x + (int)num(0, 0) + 1, p.y + (int)num(1, 0), p.z))) { t->addSmoke(10); t->setFire(4); }
					if (Tile *t = sb->getTile(Position(p.x + (int)num(0, 0), p.y + (int)num(1, 0) + 1, p.z))) t->addSmoke(12);
				}
		}
		else if (c.op == "clicktile")
		{
			// test helper: click a tile relative to the selected unit, as a tap would
			if (SavedBattleGame *sb = S->board.battle())
				if (BattleUnit *sel = sb->getSelectedUnit())
				{
					Position p = sel->getPosition();
					S->board.clickTile(p.x + (int)num(0, 0), p.y + (int)num(1, 0), std::max(0, p.z + (int)num(2, 0)), false);
				}
		}
		else if (c.op == "spotaliens")
		{
			// test helper: the selected soldier "sees" the first few aliens, so the game shows its red indicators
			if (BattlescapeState *bs = S->board.battleState())
				if (SavedBattleGame *sb = S->board.battle())
					if (BattleUnit *sel = sb->getSelectedUnit())
					{
						int n = 0;
						for (BattleUnit *u : *sb->getUnits())
							if (u->getFaction() == FACTION_HOSTILE && !u->isOut() && n < (int)num(0, 3)) { sel->addToVisibleUnits(u); u->setVisible(true); ++n; }
						bs->updateSoldierInfo(false);
					}
		}
		else if (c.op == "minimaptap") { S->board.centerOnTile(S->board.minimapTile(glm::vec2((float)num(0, 0), (float)num(1, 0)), true)); }
		else if (c.op == "gamecam")
		{
			// gamecam x y z : centre the flat game camera on a tile (tests of off-screen behaviour)
			if (BattlescapeState *bs = S->board.battleState())
				bs->getMap()->getCamera()->centerOnPosition(Position((int)num(0, 0), (int)num(1, 0), (int)num(2, 0)));
		}
		else if (c.op == "hand")
		{
			// hand x y z yaw pitch roll indexCurl othersCurl thumb : a scripted right hand (grip pose, degrees)
			S->simHand = true;
			S->simGrip.pos = glm::vec3(num(0, 0), num(1, 1), num(2, -0.4));
			S->simGrip.rot = glm::angleAxis(glm::radians((float)num(3, 0)), glm::vec3(0, 1, 0))
				* glm::angleAxis(glm::radians((float)num(4, 0)), glm::vec3(1, 0, 0))
				* glm::angleAxis(glm::radians((float)num(5, 0)), glm::vec3(0, 0, 1));
			S->simIndex = (float)num(6, 0);
			S->simOthers = (float)num(7, 0.8);
			S->simThumb = num(8, 1) > 0.5;
			S->simFrames = 0;
		}
		else if (c.op == "handmove")
		{
			// handmove x y z frames : glide the scripted hand
			glm::vec3 to((float)num(0, 0), (float)num(1, 1), (float)num(2, -0.4));
			int n = std::max(1, (int)num(3, 10));
			S->simStep = (to - S->simGrip.pos) / (float)n;
			S->simFrames = n;
		}
		else if (c.op == "poketile" || c.op == "pokenear" || c.op == "pokeunit" || c.op == "pokebutton" || c.op == "pokemenu" || c.op == "pinchitem" || c.op == "pinchto")
		{
			// test macros that steer the scripted hand: poke a point with the index finger, or pinch
			Hands probe;
			Pose at;
			bool pinchPose = c.op == "pinchitem" || c.op == "pinchto";
			probe.simulate(1, at, pinchPose ? 1.f : 0.f, pinchPose ? 0.8f : 1.f, true, 0.f, {});
			glm::vec3 off = pinchPose ? probe.pose[1].pinchPoint : probe.pose[1].tip(F_INDEX);
			glm::vec3 target(0.f);
			bool ok = false;
			if (c.op == "poketile") { target = S->board.tileCenter((int)num(0, 0), (int)num(1, 0), (int)num(2, 0)); ok = S->board.battle() != nullptr; }
			else if (c.op == "pokenear" && S->board.battle() && S->board.battle()->getSelectedUnit())
			{
				Position p = S->board.battle()->getSelectedUnit()->getPosition();
				target = S->board.tileCenter(p.x + (int)num(0, 0), p.y + (int)num(1, 0), std::max(0, p.z + (int)num(2, 0)));
				Log(LOG_INFO) << "[VR] script: unit at " << p.x << "," << p.y << "," << p.z;
				ok = true;
			}
			else if (c.op == "pokeunit")
			{
				auto marks = S->board.unitMarkers();
				int n = (int)num(0, 0), k = 0;
				for (auto &m : marks)
					if (m.ours && m.unit != S->board.battle()->getSelectedUnit() && k++ == n) { target = m.head; ok = true; break; }
			}
			else if (c.op == "pokebutton") ok = !c.args.empty() && S->table.buttonCenter(c.args[0], target);
			glm::vec3 pushDir(0.f, -1.f, 0.f);
			if (c.op == "pokemenu") { glm::vec3 n; ok = S->table.menuKeyCenter((int)num(0, 0), target, n); pushDir = -n; }
			else if (c.op == "pinchitem") ok = S->table.itemCenter((int)num(0, 0), target);
			else if (c.op == "pinchto") ok = S->table.cellCenter(c.args.empty() ? "STR_BACK_PACK" : c.args[0], (float)num(1, 0.5), (float)num(2, 0.5), target);
			if (!ok) { Log(LOG_WARNING) << "[VR] script: " << c.op << " has no target"; continue; }
			S->simHand = true;
			S->simGrip.rot = glm::quat(1.f, 0.f, 0.f, 0.f);
			glm::vec3 grip = target - off;
			std::deque<ScriptCmd> seq;
			auto add = [&](const std::string &op, std::vector<std::string> args) { seq.push_back({op, args}); };
			auto f = [](float v) { return std::to_string(v); };
			if (c.op == "pinchto")
			{
				// glide there while still pinching, then open the fingers
				add("handmove", {f(grip.x), f(grip.y + 0.01f), f(grip.z), "30"});
				add("waitframes", {"36"});
				add("handcurl", {"0", "0.8", "0"});
				add("waitframes", {"8"});
				add("status", {});
			}
			else if (pinchPose)
			{
				S->simGrip.pos = grip + glm::vec3(0.f, 0.01f, 0.f);
				S->simIndex = 0.f; S->simOthers = 0.8f; S->simThumb = false;
				add("waitframes", {"6"});
				add("handcurl", {"1", "0.8", "1"});
				add("waitframes", {"10"});
				add("status", {});
			}
			else
			{
				// start a little in front of the target, push through it, come back
				glm::vec3 from = grip - pushDir * 0.05f, to = grip + pushDir * 0.02f, back = grip - pushDir * 0.06f;
				S->simGrip.pos = from;
				S->simIndex = 0.f; S->simOthers = 1.f; S->simThumb = true;
				add("waitframes", {"4"});
				add("handmove", {f(to.x), f(to.y), f(to.z), "18"});
				add("waitframes", {"24"});
				add("status", {});
				add("handmove", {f(back.x), f(back.y), f(back.z), "8"});
				add("waitframes", {"10"});
			}
			for (auto it = seq.rbegin(); it != seq.rend(); ++it) S->script.push_front(*it);
			continue;
		}
		else if (c.op == "handcurl") { S->simIndex = (float)num(0, 0); S->simOthers = (float)num(1, 0.8); S->simThumb = num(2, 1) > 0.5; }
		else if (c.op == "handoff") { S->simHand = false; }
		else if (c.op == "status")
		{
			const HandPose &hp = S->handsVis.pose[1];
			glm::vec3 t = hp.tip(F_INDEX);
			Log(LOG_INFO) << "[VR] status: " << S->table.status() << " | tip " << t.x << "," << t.y << "," << t.z
				<< " contact=" << hp.contact[fingerTip(F_INDEX)] << " pinch=" << hp.pinch;
		}
		else if (c.op == "shot") { S->pendingShot = c.args.empty() ? "vr_shot.png" : c.args[0]; return; }
		else if (c.op == "gameshot" && !S->pixels.empty())
		{
			std::vector<unsigned char> px((const unsigned char*)S->pixels.data(), (const unsigned char*)(S->pixels.data() + S->pixels.size()));
			lodepng::encode(c.args.empty() ? "vr_game.png" : c.args[0], px, S->surfW, S->surfH);
			Log(LOG_INFO) << "[VR] game frame " << S->surfW << "x" << S->surfH << " saved";
		}
		else if (c.op == "log") { std::string m; for (auto &a : c.args) m += a + " "; Log(LOG_INFO) << "[VR] script: " << m; }
		else if (c.op == "quit") { S->game->quit(); }
		else Log(LOG_WARNING) << "[VR] unknown script command " << c.op;
	}
}

static void saveShot(const std::string &path, int w, int h)
{
	std::vector<unsigned char> px((size_t)w * h * 4), flipped((size_t)w * h * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
	for (int y = 0; y < h; ++y)
		std::memcpy(&flipped[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
	for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
	unsigned err = lodepng::encode(path, flipped, w, h);
	Log(LOG_INFO) << "[VR] screenshot " << path << (err ? " FAILED" : " saved");
}

static void previewFrame()
{
	runScript();
	Uint32 now = SDL_GetTicks();
	if (now - S->lastPreviewFrame < 14 && S->pendingShot.empty()) return;
	S->lastPreviewFrame = now;

	SDL_Surface *vs = SDL_GetVideoSurface();
	int w = vs ? vs->w : 640, h = vs ? vs->h : 400;
	if (w != S->previewW || h != S->previewH)
	{
		S->previewTarget.~RenderTarget();
		new (&S->previewTarget) RenderTarget();
		S->previewTarget.create(w, h, 4);
		S->previewW = w; S->previewH = h;
	}
	S->head = previewHead();
	S->board.setViewer(S->head.pos);
	{
		GLStateGuard guard;
		static Uint32 lastSim = now;
		updateHandsAndTable(std::min(0.1f, (now - lastSim) / 1000.f), true);
		lastSim = now;
	}
	// the preview "right hand" is a ray from the eye through the mouse cursor
	glm::vec3 o, d;
	previewRay(S->realMouseX, S->realMouseY, o, d);
	RayHit hit = castRay(o, d);
	S->laserHit[1] = hit.target != RayHit::NONE;
	S->laserEnd[1] = o + d * (S->laserHit[1] ? hit.t : 4.f);
	if (hit.target == RayHit::BOARD) S->board.hover(hit.board);
	else S->board.hoverNone();
	S->table.pointerMove(hit.target == RayHit::TABLE ? &hit.table : nullptr);

	GLStateGuard guard;
	computeLights(std::min(0.1f, (now - S->lastPreviewFrame) / 1000.f + 0.016f));
	renderShadows();
	S->previewTarget.bind();
	glm::mat4 proj = glm::perspective(glm::radians(62.f), (float)w / (float)h, 0.03f, 60.f);
	drawScene(viewFromPose(S->head), proj, S->head.pos, false);
	// cursor dot where the mouse ray lands
	if (S->laserHit[1])
	{
		glDisable(GL_DEPTH_TEST);
		S->shader.use();
		S->shader.set("uMode", 2);
		S->shader.set("uModel", glm::translate(glm::mat4(1.f), S->laserEnd[1]) * glm::scale(glm::mat4(1.f), glm::vec3(0.005f)));
		S->shader.set("uTint", glm::vec4(0.4f, 1.f, 1.f, 1.f));
		S->dotMesh.draw();
	}
	glDisable(GL_FRAMEBUFFER_SRGB);
	S->previewTarget.resolveToDefault(w, h);
	if (!S->pendingShot.empty())
	{
		gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
		saveShot(S->pendingShot, w, h);
		S->pendingShot.clear();
	}
	SDL_GL_SwapBuffers();
}

// ------------------------------------------------------------------ headset mode

static void headsetFrame(float dt)
{
	if (!S->xr.pollEvents())
	{
		Log(LOG_INFO) << "[VR] runtime asked the app to exit";
		S->game->quit();
		S->on = false;
		return;
	}
	if (!S->xr.isRunning()) { SDL_Delay(10); return; }
	bool render = false;
	if (!S->xr.beginFrame(S->views, S->hands, render)) return;

	// head = midpoint of the eyes
	S->head.pos = (S->views[0].pose.pos + S->views[1].pose.pos) * 0.5f;
	S->head.rot = S->views[0].pose.rot;
	if (!S->recentered && render) recenter();
	S->board.setViewer(xfPoint(S->rig, S->head.pos));
	updateHeadsetInput(dt);
	{
		GLStateGuard guard;
		updateHandsAndTable(dt, false);
	}

	if (render)
	{
		GLStateGuard guard;
		computeLights(dt);
		renderShadows();
		for (int e = 0; e < 2; ++e)
		{
			GLuint tex = S->xr.acquireEye(e);
			if (!tex) continue;
			Pose eyeWorld;
			glm::mat4 m = S->rig * S->views[e].pose.matrix();
			glm::vec3 eyePos = glm::vec3(m[3]);
			S->eyeTarget[e].bind();
			drawScene(glm::inverse(m), S->views[e].projection(0.03f, 60.f), eyePos, true);
			glDisable(GL_FRAMEBUFFER_SRGB);
			S->eyeTarget[e].resolveTo(tex);
			S->xr.releaseEye(e);
		}
		glFlush();
	}
	S->xr.endFrame(render);
}

void frame()
{
	if (!active()) return;
	if (XrRuntime::currentGLContext() != S->glContext)
	{
		Log(LOG_ERROR) << "[VR] the OpenGL context was re-created (video options changed?). VR stopped; restart the game.";
		S->on = false;
		return;
	}
	static Uint32 last = SDL_GetTicks();
	Uint32 now = SDL_GetTicks();
	float dt = std::min(0.1f, (now - last) / 1000.f);
	last = now;
	S->time += dt;
	S->frameCount++;
	for (auto it = S->pendingButtons.begin(); it != S->pendingButtons.end();)
	{
		if (--it->frames <= 0) { pushButton(it->button, it->down); it = S->pendingButtons.erase(it); }
		else ++it;
	}

	if (!S->placardTried && S->game->getMod() && S->surfW > 0 && S->frameCount % 30 == 0)
	{
		GLStateGuard guard;
		buildPlacard();
	}
	if (S->pixelsDirty && S->surfW > 0)
	{
		GLStateGuard guard;
		S->gameTex.update(S->pixels.data(), S->surfW, S->surfH);
		S->pixelsDirty = false;
	}
	{
		GLStateGuard guard;
		S->board.update(S->game, dt);
	}

	if (S->mode == MODE_HEADSET) headsetFrame(dt);
	else previewFrame();
}

// ------------------------------------------------------------------ desktop events

bool filterEvent(SDL_Event &ev)
{
	if (!active()) return true;
	bool mouse = ev.type == SDL_MOUSEMOTION || ev.type == SDL_MOUSEBUTTONDOWN || ev.type == SDL_MOUSEBUTTONUP;
	if (!mouse) return true;
	Uint8 which = ev.type == SDL_MOUSEMOTION ? ev.motion.which : ev.button.which;
	if (which == SYNTH_TAG) return true; // ours, already consistent with the virtual mouse

	if (S->mode == MODE_HEADSET)
	{
		// a real mouse on the desktop still works; keep the virtual state in sync
		if (ev.type == SDL_MOUSEMOTION) { S->vmX = ev.motion.x; S->vmY = ev.motion.y; }
		else if (ev.button.button <= 3)
		{
			if (ev.type == SDL_MOUSEBUTTONDOWN) S->vmButtons |= SDL_BUTTON(ev.button.button);
			else S->vmButtons &= ~SDL_BUTTON(ev.button.button);
		}
		return true;
	}

	// desktop preview: the mouse aims a ray from the camera
	if (ev.type == SDL_MOUSEMOTION)
	{
		if (S->camDrag)
		{
			S->camYaw -= ev.motion.xrel * 0.25f;
			S->camPitch = glm::clamp(S->camPitch - ev.motion.yrel * 0.25f, -85.f, 85.f);
			SDL_WarpMouse(S->realMouseX, S->realMouseY);
			return false;
		}
		S->realMouseX = ev.motion.x;
		S->realMouseY = ev.motion.y;
	}
	else
	{
		if (ev.button.button == SDL_BUTTON_MIDDLE)
		{
			S->camDrag = ev.type == SDL_MOUSEBUTTONDOWN;
			return false;
		}
	}
	glm::vec3 o, d;
	int mx = ev.type == SDL_MOUSEMOTION ? ev.motion.x : ev.button.x;
	int my = ev.type == SDL_MOUSEMOTION ? ev.motion.y : ev.button.y;
	previewRay(mx, my, o, d);
	RayHit hit = castRay(o, d);
	bool held = S->vmButtons != 0;
	if (hit.target == RayHit::PANEL || (held && ev.type != SDL_MOUSEBUTTONDOWN))
	{
		int wx = S->vmX, wy = S->vmY;
		if (hit.target == RayHit::PANEL) gameToWindow(hit.uv, wx, wy);
		S->pointerOnPanel = hit.target == RayHit::PANEL;
		if (ev.type == SDL_MOUSEMOTION)
		{
			ev.motion.xrel = (Sint16)(wx - S->vmX);
			ev.motion.yrel = (Sint16)(wy - S->vmY);
			ev.motion.x = (Uint16)wx;
			ev.motion.y = (Uint16)wy;
			ev.motion.state = S->vmButtons;
		}
		else
		{
			ev.button.x = (Uint16)wx;
			ev.button.y = (Uint16)wy;
			if (ev.button.button <= 3)
			{
				if (ev.type == SDL_MOUSEBUTTONDOWN) S->vmButtons |= SDL_BUTTON(ev.button.button);
				else S->vmButtons &= ~SDL_BUTTON(ev.button.button);
			}
		}
		S->vmX = wx;
		S->vmY = wy;
		return true;
	}
	if (hit.target == RayHit::TABLE)
	{
		S->table.pointerMove(&hit.table);
		if (ev.type == SDL_MOUSEBUTTONDOWN)
		{
			TableContext ctx = tableContext();
			S->table.pointerClick(hit.table, ev.button.button, ctx);
		}
		return false;
	}
	if (hit.target == RayHit::BOARD)
	{
		S->board.hover(hit.board);
		if (ev.type == SDL_MOUSEBUTTONDOWN)
		{
			if (ev.button.button == SDL_BUTTON_WHEELUP) S->board.wheel(1);
			else if (ev.button.button == SDL_BUTTON_WHEELDOWN) S->board.wheel(-1);
			else S->board.click(ev.button.button);
		}
	}
	return false;
}

}
}
