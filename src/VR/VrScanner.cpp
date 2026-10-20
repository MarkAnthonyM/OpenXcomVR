/*
 * OXCE VR tabletop - the motion scanner in your hand (see VrScanner.h).
 */
#include "VrScanner.h"
#include "VrRoom.h"
#include "../Engine/Game.h"
#include "../Engine/State.h"
#include "../Engine/Surface.h"
#include "../Engine/SurfaceSet.h"
#include "../Engine/Palette.h"
#include "../Engine/Logger.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleItem.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Battlescape/ScannerState.h"
#include "../Battlescape/ScannerView.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <typeinfo>

namespace OpenXcom
{
namespace VR
{

namespace
{
const float PX = 0.015f;            // one pixel of the item art (metres): the device is about 17 x 24 cm
const int SCREEN_W = 288, SCREEN_H = 128; // the screen's picture (texels), the art's 9 x 4 pixel screen
// the art's screen (pixels, inclusive) and the close button (the orange knob on the left of the head)
const int SX0 = 4, SX1 = 12, SY0 = 3, SY1 = 6;
const int BX = 2, BY0 = 4, BY1 = 5;
}

struct HandScanner::Impl
{
	enum Phase { CLOSED, OPENING, OPEN, CLOSING };
	Phase phase = CLOSED;
	float t = 0.f;                  // seconds in the phase
	float armed = 0.f;              // the button works after a moment (no press on the way in)
	int hand = 1;                   // the hand holding it (0 left, 1 right)
	State *src = nullptr;           // the game's scanner screen
	bool built = false;
	MeshData body;                  // the voxel model, device space (x right, y up, z toward the user; metres)
	Mesh bodyMesh, screenMesh, buttonMesh;
	Texture screenTex;
	std::vector<uint32_t> radar;    // the game's scanner picture, as cropped from the frame
	int radarW = 0, radarH = 0;
	std::vector<uint32_t> screen;   // what the device's screen shows (turned)
	glm::mat4 xf{1.f};              // device -> world
	bool xfKnown = false;
	float heading = 0.f;            // where it points: degrees from the map's north, clockwise
	glm::vec3 buttonC{0.f};         // the close button (world)
	int presses = 0;
	std::string last = "-";
	float depthHead = 3.f, depthHandle = 2.f;
	glm::vec2 grip{7.5f, 12.5f};    // the art pixel held in the palm

	void build(Game *game);
	void place(const Hands &hands, float dt);
	void cropRadar(const ScannerContext &ctx);
	void composeScreen(const ScannerContext &ctx);
	glm::vec3 artToDevice(float x, float y, float z) const { return glm::vec3((x - grip.x) * PX, (grip.y - y) * PX, z * PX); }
};

HandScanner::HandScanner() : _p(new Impl()) {}
HandScanner::~HandScanner() = default;

bool HandScanner::isOpen() const { return _p->phase == Impl::OPENING || _p->phase == Impl::OPEN; }

/// The voxel model: each pixel of the game's scanner item art becomes a block, the head deeper than the
/// grip; the screen's pixels are left for the live picture and the orange knob on the left is the button.
void HandScanner::Impl::build(Game *game)
{
	built = true;
	body.clear();
	Mod *mod = game ? game->getMod() : nullptr;
	if (!mod) return;
	// the scanner the soldier used (in a hand), otherwise the first scanner item in the rules
	const RuleItem *rule = nullptr;
	SavedBattleGame *battle = game->getSavedGame() ? game->getSavedGame()->getSavedBattle() : nullptr;
	if (battle && battle->getSelectedUnit())
		for (BattleItem *it : {battle->getSelectedUnit()->getRightHandWeapon(), battle->getSelectedUnit()->getLeftHandWeapon()})
			if (it && it->getRules()->getBattleType() == BT_SCANNER) { rule = it->getRules(); break; }
	if (!rule) rule = mod->getItem("STR_MOTION_SCANNER", false);
	SurfaceSet *set = mod->getSurfaceSet("BIGOBS.PCK", false);
	Surface *art = (rule && set) ? set->getFrame(rule->getBigSprite()) : nullptr;
	Palette *pal = mod->getPalette("PAL_BATTLESCAPE", false);
	if (!art || !pal) return;
	SDL_Color *c = pal->getColors();
	int w = art->getWidth(), h = art->getHeight();
	// where the grip is: the middle of the lowest rows of solid pixels
	int ymax = 0, gx0 = w, gx1 = -1;
	for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) if (art->getPixel(x, y)) ymax = std::max(ymax, y);
	for (int y = std::max(0, ymax - 4); y <= ymax; ++y) for (int x = 0; x < w; ++x) if (art->getPixel(x, y)) { gx0 = std::min(gx0, x); gx1 = std::max(gx1, x); }
	if (gx1 >= gx0) grip = glm::vec2((gx0 + gx1 + 1) * 0.5f, ymax - 2.5f);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			Uint8 i = art->getPixel(x, y);
			if (!i) continue;
			if (x == BX && y >= BY0 && y <= BY1) continue; // the button is its own part
			glm::vec4 col(c[i].r / 255.f, c[i].g / 255.f, c[i].b / 255.f, 1.f);
			bool head = y < ymax - 5;
			float d = head ? depthHead : depthHandle;
			bool screenPx = x >= SX0 && x <= SX1 && y >= SY0 && y <= SY1;
			// the screen sits a little into the head, behind its bezel
			float zFront = screenPx ? d * 0.5f - 0.35f : d * 0.5f;
			body.addBox(artToDevice((float)x, (float)y + 1.f, -d * 0.5f), artToDevice((float)x + 1.f, (float)y, zFront),
				screenPx ? glm::vec4(0.02f, 0.05f, 0.03f, 1.f) : col, (float)MAT_GLOSSY);
		}
	bodyMesh.upload(body);
	// the screen: the picture over the art's screen pixels
	MeshData sm;
	float z = depthHead * 0.5f - 0.33f;
	sm.addQuad(artToDevice((float)SX0, (float)SY1 + 1.f, z), artToDevice((float)SX1 + 1.f, (float)SY1 + 1.f, z),
		artToDevice((float)SX1 + 1.f, (float)SY0, z), artToDevice((float)SX0, (float)SY0, z), glm::vec4(1.f), 0.f,
		{0.f, 1.f}, {1.f, 1.f}, {1.f, 0.f}, {0.f, 0.f});
	screenMesh.upload(sm);
	// the close button: the knob, standing out from the head's left side
	MeshData bm;
	bm.addBox(artToDevice((float)BX - 0.6f, (float)BY1 + 1.f, -0.9f), artToDevice((float)BX + 1.f, (float)BY0, 0.9f), glm::vec4(1.f), (float)MAT_GLOSSY);
	buttonMesh.upload(bm);
	Log(LOG_INFO) << "[VR] scanner: built from " << (rule ? rule->getType() : std::string("?")) << " art " << w << "x" << h << ", grip at " << grip.x << "," << grip.y;
}

/// Held by its grip in the hand: the handle across the palm, the head above the fist, the screen facing
/// back along the forearm (toward the player).
void HandScanner::Impl::place(const Hands &hands, float dt)
{
	const HandPose &H = hands.pose[hand];
	if (!H.valid) return; // keeps its last place while the hand isn't tracked
	// joints (XR_EXT_hand_tracking order): palm 0, wrist 1, index knuckle 7, middle knuckle 12, little knuckle 22
	glm::vec3 wr = H.pos[1], ik = H.pos[7], mk = H.pos[12], lk = H.pos[22], palm = H.pos[0];
	glm::vec3 fwd = mk - wr;
	if (glm::length(fwd) < 1e-4f) return;
	fwd = glm::normalize(fwd);
	glm::vec3 down = lk - ik;
	down -= fwd * glm::dot(down, fwd);
	if (glm::length(down) < 1e-4f) return;
	down = glm::normalize(down);
	glm::vec3 up = -down;
	glm::vec3 toUser = -fwd;                          // the screen faces back toward the player
	glm::vec3 right = glm::normalize(glm::cross(up, toUser));
	glm::vec3 inward = glm::cross(ik - wr, lk - wr);  // into the fist
	if (glm::length(inward) > 1e-6f) inward = glm::normalize(inward) * (hand == 1 ? 1.f : -1.f);
	glm::vec3 anchor = palm + inward * 0.022f;
	glm::mat4 m(1.f);
	m[0] = glm::vec4(right, 0.f);
	m[1] = glm::vec4(up, 0.f);
	m[2] = glm::vec4(toUser, 0.f);
	m[3] = glm::vec4(anchor, 1.f);
	if (!xfKnown) { xf = m; xfKnown = true; }
	else
	{
		// a little smoothing so tracking jitter doesn't shake the picture
		float a = 1.f - std::exp(-dt * 30.f);
		for (int i = 0; i < 4; ++i) xf[i] += (m[i] - xf[i]) * a;
		glm::vec3 r = glm::normalize(glm::vec3(xf[0])), u = glm::vec3(xf[1]);
		u = glm::normalize(u - r * glm::dot(u, r));
		xf[0] = glm::vec4(r, 0.f); xf[1] = glm::vec4(u, 0.f); xf[2] = glm::vec4(glm::cross(r, u), 0.f);
	}
	buttonC = glm::vec3(xf * glm::vec4(artToDevice((float)BX - 0.1f, (float)BY0 + 1.f, 0.f), 1.f));
}

/// The game's scanner picture: the scanner view's own area of the game frame.
void HandScanner::Impl::cropRadar(const ScannerContext &ctx)
{
	if (!src || !ctx.pixels || ctx.pixels->empty() || ctx.surfW <= 0) return;
	for (Surface *sf : src->vrSurfaces())
	{
		ScannerView *v = dynamic_cast<ScannerView*>(sf);
		if (!v) continue;
		int x0 = v->getX(), y0 = v->getY(), w = v->getWidth(), h = v->getHeight();
		if (w <= 0 || h <= 0) return;
		radarW = w; radarH = h;
		radar.assign((size_t)w * h, 0xFF000000u);
		for (int y = 0; y < h; ++y)
		{
			int sy = y0 + y;
			if (sy < 0 || sy >= ctx.surfH) continue;
			for (int x = 0; x < w; ++x)
			{
				int sx = x0 + x;
				if (sx < 0 || sx >= ctx.surfW) continue;
				radar[(size_t)y * w + x] = (*ctx.pixels)[(size_t)sy * ctx.surfW + sx];
			}
		}
		return;
	}
}

/// The device's screen: the game's picture turned so that where the device points is at the top, on a
/// round screen just big enough to keep the whole square in view at any angle.
void HandScanner::Impl::composeScreen(const ScannerContext &ctx)
{
	screen.assign((size_t)SCREEN_W * SCREEN_H, 0xFF050805u);
	if (radar.empty()) return;
	const float cx = SCREEN_W * 0.5f, cy = SCREEN_H * 0.5f, R = SCREEN_H * 0.5f - 2.f;
	// the square's half diagonal fits the circle
	const float side = R * 2.f / std::sqrt(2.f);
	const float k = (float)radarW / side;           // radar pixels per screen texel
	float th = glm::radians(heading);
	// screen right and screen up as directions on the map grid (x east, y south)
	glm::vec2 r(std::cos(th), std::sin(th)), u(std::sin(th), -std::cos(th));
	for (int y = 0; y < SCREEN_H; ++y)
		for (int x = 0; x < SCREEN_W; ++x)
		{
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			float d2 = dx * dx + dy * dy;
			uint32_t &o = screen[(size_t)y * SCREEN_W + x];
			if (d2 > R * R)
			{
				if (d2 < (R + 1.5f) * (R + 1.5f)) o = 0xFF2A5A34u; // the screen's rim
				continue;
			}
			glm::vec2 g = r * dx - u * dy;                // map grid offset from the soldier (texels)
			float rx = radarW * 0.5f + g.x * k, ry = radarH * 0.5f + g.y * k;
			if (rx < 0.f || ry < 0.f || rx >= radarW || ry >= radarH) { o = 0xFF0C140Du; continue; }
			o = radar[(size_t)ry * radarW + (size_t)rx];
		}
	// a mark at the top: the way the device points
	for (int y = 2; y < 9; ++y)
		for (int x = -1; x <= 1; ++x) screen[(size_t)y * SCREEN_W + (size_t)(cx + x)] = 0xFF6AE08Au;
	(void)ctx;
}

void HandScanner::update(const ScannerContext &ctx, const Hands &hands, float dt)
{
	Impl &p = *_p;
	p.t += dt;
	// is the game's scanner screen up?
	State *top = (ctx.game && !ctx.game->getStates().empty()) ? ctx.game->getStates().back() : nullptr;
	State *scan = dynamic_cast<ScannerState*>(top) ? top : nullptr;
	if (scan && (p.phase == Impl::CLOSED || p.phase == Impl::CLOSING || p.src != scan))
	{
		if (!p.built) p.build(ctx.game);
		p.src = scan;
		p.phase = Impl::OPENING;
		p.t = 0.f;
		p.armed = 0.f;
		// in the right hand unless only the left one is there
		p.hand = hands.pose[1].valid ? 1 : (hands.pose[0].valid ? 0 : 1);
		p.xfKnown = false;
		p.last = "open";
		if (ctx.haptic) ctx.haptic(p.hand, 0.35f, 0.08f);
		Log(LOG_INFO) << "[VR] scanner: open in the " << (p.hand == 1 ? "right" : "left") << " hand";
	}
	if (!scan && (p.phase == Impl::OPENING || p.phase == Impl::OPEN))
	{
		p.phase = Impl::CLOSING;
		p.t = 0.f;
		p.src = nullptr;
		Log(LOG_INFO) << "[VR] scanner: closed";
	}
	if (p.phase == Impl::OPENING && p.t > 0.25f) { p.phase = Impl::OPEN; p.t = 0.f; }
	if (p.phase == Impl::CLOSING && p.t > 0.25f) { p.phase = Impl::CLOSED; p.t = 0.f; }
	if (p.phase == Impl::CLOSED) return;

	p.place(hands, dt);
	// where it points, on the map: the device's forward and the top of its screen, both laid flat
	// (held upright it points with its nose, held flat like a map with the top of its screen)
	glm::vec3 fwd = -glm::vec3(p.xf[2]), up = glm::vec3(p.xf[1]);
	glm::vec2 h(fwd.x + up.x, fwd.z + up.z);
	if (glm::length(h) > 1e-3f)
	{
		h = glm::normalize(h);
		float target = glm::degrees(std::atan2(glm::dot(h, ctx.east), glm::dot(h, ctx.north)));
		float diff = std::remainder(target - p.heading, 360.f);
		p.heading = std::fmod(p.heading + diff * (1.f - std::exp(-dt * 20.f)) + 360.f, 360.f);
	}
	if (p.src) p.cropRadar(ctx);
	p.composeScreen(ctx);
	if (!p.screenTex.valid()) p.screenTex.create(SCREEN_W, SCREEN_H, true, true);
	p.screenTex.update(p.screen.data(), SCREEN_W, SCREEN_H);

	// the close button, pressed with the other hand's index fingertip
	if (p.phase == Impl::OPEN) p.armed += dt;
	const HandPose &other = hands.pose[1 - p.hand];
	if (p.phase == Impl::OPEN && p.armed > 0.6f && other.valid && !other.ghost
		&& glm::length(other.tip(F_INDEX) - p.buttonC) < 0.022f)
		testPress();
	(void)ctx;
	if (p.last == "press" && ctx.clickScreen)
	{
		// the game's scanner closes on a right-click anywhere
		ctx.clickScreen(ctx.surfW / 2, ctx.surfH / 2, SDL_BUTTON_RIGHT);
		if (ctx.haptic) { ctx.haptic(1 - p.hand, 0.6f, 0.05f); ctx.haptic(p.hand, 0.3f, 0.05f); }
		p.last = "pressed";
	}
}

bool HandScanner::testPress()
{
	Impl &p = *_p;
	if (p.phase != Impl::OPEN) return false;
	p.armed = -1e9f;
	++p.presses;
	p.last = "press";
	Log(LOG_INFO) << "[VR] scanner: close button";
	return true;
}

void HandScanner::draw(const Shader &sh) const
{
	const Impl &p = *_p;
	if (p.phase == Impl::CLOSED || !p.xfKnown || p.bodyMesh.empty()) return;
	// it grows out of the hand, and shrinks back into it
	float s = 1.f;
	if (p.phase == Impl::OPENING) s = glm::smoothstep(0.f, 1.f, p.t / 0.25f);
	if (p.phase == Impl::CLOSING) s = 1.f - glm::smoothstep(0.f, 1.f, p.t / 0.25f);
	if (s <= 0.01f) return;
	glm::mat4 M = p.xf * glm::scale(glm::mat4(1.f), glm::vec3(s));
	sh.set("uModel", M);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uMode", 0);
	p.bodyMesh.draw();
	// the button glows red-orange once it works
	float glow = p.phase == Impl::OPEN && p.armed > 0.6f ? 1.f : 0.5f;
	sh.set("uMode", 2);
	sh.set("uTint", glm::vec4(1.25f * glow, 0.38f * glow, 0.12f * glow, 1.f));
	p.buttonMesh.draw();
	if (p.screenTex.valid())
	{
		sh.set("uMode", 1);
		sh.set("uTint", glm::vec4(1.f));
		sh.set("uTexSize", glm::vec2((float)SCREEN_W, (float)SCREEN_H));
		sh.set("uScan", 0.f);
		p.screenTex.bind(0);
		p.screenMesh.draw();
	}
	sh.set("uTint", glm::vec4(1.f));
}

std::string HandScanner::status() const
{
	const Impl &p = *_p;
	std::ostringstream o;
	const char *ph[] = {"closed", "opening", "open", "closing"};
	o << "scanner=" << ph[p.phase];
	if (p.phase != Impl::CLOSED)
		o << " hand=" << (p.hand == 1 ? "right" : "left") << " heading=" << (int)std::lround(p.heading) << " radar=" << p.radarW << "x" << p.radarH;
	o << " presses=" << p.presses;
	return o.str();
}

}
}
