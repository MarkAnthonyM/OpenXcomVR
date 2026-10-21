/*
 * OXCE VR tabletop - the motion scanner in your hand (see VrScanner.h).
 */
#include "VrScanner.h"
#include "VrRoom.h"
#include "VrAudio.h"
#include "../Engine/Game.h"
#include "../Engine/State.h"
#include "../Engine/Surface.h"
#include "../Engine/Palette.h"
#include "../Engine/Font.h"
#include "../Engine/Language.h"
#include "../Engine/Logger.h"
#include "../Interface/Text.h"
#include "../Mod/Mod.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Tile.h"
#include "../Battlescape/Position.h"
#include "../Battlescape/TileEngine.h"
#include "../Battlescape/ScannerState.h"
#include "../Battlescape/ScannerView.h"
#include "../lodepng.h"
#include <glm/gtc/matrix_transform.hpp>
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace OpenXcom
{
namespace VR
{

namespace
{
const float PX = 0.0008f;                // one pixel of the scanner screen's art (metres): about 21 x 14 cm
const float DEPTH = 0.014f;              // the device's thickness
const float F = DEPTH * 0.5f;            // its front (device z; the back is at -F)
const float BTN_UP = 0.0018f;            // the buttons stand out of the front...
const float PANEL_IN = 0.0012f;          // ...the side panel's displays are set into it...
const float SCREEN_IN = 0.002f;          // ...and the big screen a little deeper
struct Rect { int x0, y0, x1, y1; int w() const { return x1 - x0 + 1; } int h() const { return y1 - y0 + 1; } };
// the art (DETBORD.PCK, 320 x 200; inclusive pixel rects)
const Rect FEED{56, 24, 207, 175};       // the radar screen (where the game puts its ScannerView): the camera
const Rect BAR{217, 29, 263, 39};        // the long readout at the top of the side panel: the compass
const Rect SIDE{216, 48, 264, 128};      // the tall display: the game's radar and a readout
const Rect BTN[4] = {{216, 136, 232, 152}, {240, 136, 256, 152}, {216, 160, 232, 176}, {240, 160, 256, 176}};
enum { B_BLUE, B_YELLOW, B_GREEN, B_RED };
const char *BTN_NAME[4] = {"blue", "yellow", "green", "red"};
// where the fist is (art pixels): just off the right edge, around the ribbed grip (right hand), or off
// the left edge (left hand), so the fingers wrap the edge and keep off the side panel
const glm::vec2 GRIP_R{298.f, 150.f};
const glm::vec2 GRIP_L{22.f, 150.f};
const float TILT = 35.f;                 // the camera looks this far below where the hand points (degrees),
                                         // so the wrist needn't bend down to aim at the table; the screen
                                         // tips up toward the eyes
const int FEED_N = 152;                  // the camera's picture (pixels): the size of the game's radar
const int TS = 4;                        // texels per art pixel on the compass and the side display
const float RANGE = 9.5f;                // tiles from the soldier to the edge of the scan (19 x 19)
const float CORNER = 13.6f;              // ...to its corners
const float PING_SPEED = CORNER / 1.2f;  // tiles per second: the wave reaches the corners in 1.2 s
const float FILL = 0.3f;                 // the picture fills in behind the wave over this long (scattered points)...
const float LIFE = 1.8f, LIFE_JITTER = 0.4f; // ...each point stays this long, then drops out over the next 0.4 s
// the next ping starts just as the edge of the range begins to go out, so every spot is dark for about the
// time the wave takes to cross the range (about a second) before the next wave lights it again
const float PING_CYCLE = CORNER / PING_SPEED + LIFE; // 3 s
// coming out: the device forms behind a scan line rising from the hand, then the screens turn on like a
// tube (the power-on sound); then the first ping
const float BUILD_END = 0.5f, OPEN_T = 0.85f;
// going away: the screens collapse to a line and a dot (the power-off sound), the device dissolves downward
const float CRT_OFF = 0.25f, CLOSE_T = 0.65f;
const glm::vec3 HOLO(0.35f, 1.f, 0.5f);
const float FOVS[] = {25.f, 35.f, 45.f, 60.f, 75.f};
const int FOV_N = 5, FOV_DEFAULT = 2;
const char *DIRS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

inline uint32_t rgba(int r, int g, int b, int a = 255)
{
	return (uint32_t)std::min(255, std::max(0, r)) | ((uint32_t)std::min(255, std::max(0, g)) << 8)
		| ((uint32_t)std::min(255, std::max(0, b)) << 16) | ((uint32_t)a << 24);
}
inline int R(uint32_t c) { return c & 255; }
inline int G(uint32_t c) { return (c >> 8) & 255; }
inline int B(uint32_t c) { return (c >> 16) & 255; }
inline uint32_t mixc(uint32_t a, uint32_t b, float t)
{
	t = std::min(1.f, std::max(0.f, t));
	return rgba((int)(R(a) + (R(b) - R(a)) * t), (int)(G(a) + (G(b) - G(a)) * t), (int)(B(a) + (B(b) - B(a)) * t));
}
inline uint32_t scalec(uint32_t a, float k) { return rgba((int)(R(a) * k), (int)(G(a) * k), (int)(B(a) * k)); }
float smooth(float e0, float e1, float x) { float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0))); return t * t * (3.f - 2.f * t); }
/// The 8-point direction of a map offset (x east, y south).
int dir8(float dx, float dy)
{
	float a = std::atan2(dx, -dy) * 57.29578f;
	return ((int)std::lround(a / 45.f) % 8 + 8) % 8;
}
}

// ---- the scanner's sounds, made in code (the game has none that fit)
enum ScanSound { SND_ON, SND_OFF, SND_PING, SND_CLICK, SND_CLICK_RED, SND_BLIP0, SND_BLIPS = 8, SND_COUNT = SND_BLIP0 + SND_BLIPS };
const char *SND_NAME[SND_COUNT] = {"on", "off", "ping", "click", "click-red", "blip-1", "blip-2", "blip-3", "blip-4", "blip-5", "blip-6", "blip-7", "blip-8"};

/// The scanner's sounds share one dark little space: the highs rolled off, then two short filtered echoes
/// feeding back (a tail of `tail` seconds is added).
static std::vector<float> darkSpace(const std::vector<float> &x, int freq, float tail)
{
	const float k = 44100.f / (float)freq;          // the filters are tuned at 44.1 kHz
	std::vector<float> y(x.size() + (size_t)(tail * freq), 0.f);
	const size_t d1 = (size_t)(0.083f * freq), d2 = (size_t)(0.131f * freq);
	float hc = 0.f, l1 = 0.f, l2 = 0.f;
	for (size_t n = 0; n < y.size(); ++n)
	{
		float v = n < x.size() ? x[n] : 0.f;
		hc += (v - hc) * std::min(1.f, 0.28f * k);      // highs off (about 2 kHz)
		if (n >= d1) l1 += (y[n - d1] - l1) * std::min(1.f, 0.18f * k);
		if (n >= d2) l2 += (y[n - d2] - l2) * std::min(1.f, 0.12f * k);
		y[n] = hc + 0.36f * l1 + 0.26f * l2;
	}
	return y;
}

/// One of the scanner's sounds (mono, -1..1) at a sample rate: low, slow and uneasy. Blips: 0 = a contact
/// right by the soldier (louder, higher, harsher), 7 = at the far corner of the range (a faint low thump).
std::vector<float> synthScan(int which, int freq)
{
	const float TAU = 6.2831853f;
	auto make = [&](float len) { return std::vector<float>((size_t)(len * freq), 0.f); };
	auto normalize = [](std::vector<float> &v, float peak) {
		float m = 1e-6f;
		for (float x : v) m = std::max(m, std::fabs(x));
		for (float &x : v) x *= peak / m;
	};
	uint32_t seed = 0x51c4u + (uint32_t)which * 977u;
	auto noise = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed >> 8) / 8388608.f - 1.f; };
	const float k44 = 44100.f / (float)freq;
	std::vector<float> v;
	if (which == SND_PING)
	{
		// a low sonar boom: two tones a little apart (they throb), an octave under them, a breath of noise;
		// a slow swell and a long fall
		v = make(2.2f);
		float p1 = 0.f, p2 = 0.f, p3 = 0.f, br = 0.f;
		for (size_t i = 0; i < v.size(); ++i)
		{
			float t = (float)i / freq;
			float f = 165.f - 18.f * std::min(t / 1.6f, 1.f);
			p1 += TAU * f / freq; p2 += TAU * (f + 2.3f) / freq; p3 += TAU * f * 0.5f / freq;
			float env = std::min(t / 0.06f, 1.f) * std::exp(-t / 0.65f);
			br += (noise() - br) * 0.02f * k44;
			float breath = br * 6.f * std::exp(-t / 0.25f);
			v[i] = (std::sin(p1) + 0.8f * std::sin(p2) + 0.45f * std::sin(p3) + 0.12f * std::sin(3.f * p1)) * env + breath * 0.3f;
		}
		v = darkSpace(v, freq, 1.0f);
		normalize(v, 0.75f);
	}
	else if (which >= SND_BLIP0)
	{
		// a contact: a muffled heartbeat thump (and a softer second one); the nearer, the louder, the higher,
		// and the more a sour overtone and some grit come in
		int k = which - SND_BLIP0;
		float nearK = 1.f - k / (float)(SND_BLIPS - 1);   // 1 right by the soldier .. 0 at the far corner
		float f0 = 58.f * std::pow(2.f, nearK);            // 58 .. 116 Hz
		v = make(0.5f);
		for (int beat = 0; beat < 2; ++beat)
		{
			float at = beat == 0 ? 0.f : 0.16f, g = beat == 0 ? 1.f : 0.55f;
			float ph = 0.f, ph2 = 0.f;
			for (size_t i = (size_t)(at * freq); i < v.size(); ++i)
			{
				float t = (float)i / freq - at;
				float f = f0 * (1.f + 0.8f * std::exp(-t / 0.025f));
				ph += TAU * f / freq;
				ph2 += TAU * f * 2.83f / freq;              // a tritone over the octave: wrong-sounding
				float env = std::min(t / 0.004f, 1.f) * std::exp(-t / 0.09f);
				float x = std::sin(ph) + 0.35f * nearK * std::sin(ph2) * std::exp(-t / 0.05f);
				x = std::tanh(x * (1.f + 2.5f * nearK)) / std::tanh(1.f + 2.5f * nearK);
				v[i] += x * env * g;
			}
		}
		v = darkSpace(v, freq, 0.5f);
		normalize(v, 0.45f + 0.4f * nearK);
	}
	else if (which == SND_ON)
	{
		// power: a low electrical swell with a little crackle
		v = make(0.9f);
		float ph = 0.f, lp = 0.f;
		for (size_t i = 0; i < v.size(); ++i)
		{
			float t = (float)i / freq;
			ph += TAU * (50.f + 12.f * t) / freq;
			float saw = std::fmod(ph / TAU, 1.f) * 2.f - 1.f;
			float cut = (0.004f + 0.05f * std::min(t / 0.7f, 1.f)) * k44;   // it opens up as it swells
			lp += (saw - lp) * std::min(1.f, cut);
			float env = glm::smoothstep(0.f, 0.55f, t) * std::min((0.9f - t) / 0.2f, 1.f);
			float crackle = (noise() > 0.995f ? noise() : 0.f) * std::min(t / 0.5f, 1.f);
			v[i] = lp * env * 2.f + crackle * 0.6f;
		}
		v = darkSpace(v, freq, 0.4f);
		normalize(v, 0.55f);
	}
	else if (which == SND_OFF)
	{
		// power off: a falling sub drop, with static fading out
		v = make(1.0f);
		float ph = 0.f, st = 0.f;
		for (size_t i = 0; i < v.size(); ++i)
		{
			float t = (float)i / freq;
			ph += TAU * (90.f * std::pow(30.f / 90.f, std::min(t / 0.6f, 1.f))) / freq;
			st += (noise() - st) * 0.08f * k44;
			float env = std::min(t / 0.01f, 1.f) * std::exp(-t / 0.35f);
			v[i] = std::sin(ph) * env + st * 0.45f * std::exp(-t / 0.3f);
		}
		v = darkSpace(v, freq, 0.4f);
		normalize(v, 0.55f);
	}
	else
	{
		// a button: a heavy relay clunk (red: lower and heavier, with a second clack)
		bool red = which == SND_CLICK_RED;
		v = make(red ? 0.25f : 0.15f);
		float lp = 0.f;
		for (int hit = 0; hit < (red ? 2 : 1); ++hit)
		{
			float at = hit == 0 ? 0.f : 0.07f, g = hit == 0 ? 1.f : 0.6f;
			float f = red ? 62.f : 90.f;
			for (size_t i = (size_t)(at * freq); i < v.size(); ++i)
			{
				float t = (float)i / freq - at;
				lp += (noise() - lp) * 0.07f * k44;
				v[i] += g * (std::sin(TAU * f * t) * std::exp(-t / 0.04f) + lp * 3.f * std::exp(-t / 0.006f)
					+ 0.3f * std::sin(TAU * (red ? 310.f : 420.f) * t) * std::exp(-t / 0.03f));
			}
		}
		v = darkSpace(v, freq, 0.3f);
		normalize(v, 0.6f);
	}
	return v;
}

/// A line of the game's small font: 1 where a glyph pixel is.
struct TextMask
{
	int w = 0, h = 0;
	std::vector<uint8_t> px;
};

struct HandScanner::Impl
{
	enum Phase { CLOSED, OPENING, OPEN, CLOSING };
	Phase phase = CLOSED;
	float t = 0.f;                  // seconds in the phase
	float armed = 0.f;              // the buttons work after a moment (no press on the way in)
	int hand = 1;                   // the hand holding it (0 left, 1 right)
	State *src = nullptr;           // the game's scanner screen
	Game *game = nullptr;
	bool built = false, artOk = false;
	// the device
	std::vector<uint8_t> cls;       // per art pixel: what it is (see build)
	std::vector<uint32_t> art;      // the art's colours
	Texture bodyTex, feedTex, barTex, sideTex;
	Mesh frontMesh, wallMesh, feedMesh, barMesh, sideMesh, btnTop[4], btnWall[4];
	mutable Mesh arrow;             // the camera picture's arrows to contacts out of view
	float freezeT = -1.f;
	RenderTarget feedRT;
	bool feedOk = false, feedFailed = false, feedFresh = false;
	int feedSkip = 0;
	glm::mat4 xf{1.f};              // device -> world
	bool xfKnown = false;
	glm::vec2 grip = GRIP_R;
	float heading = 0.f;            // where the camera points: degrees from the map's north, clockwise
	int fovIdx = FOV_DEFAULT;
	// the views: commander (the camera in the hand) or soldier (the camera at the soldier's eyes, turned by
	// pointing the device; yellow / blue raise and lower it a floor at a time)
	bool soldierView = false;
	int eyeStep = 0;                // 0 the soldier's eye height, 1 and 2 floors above
	BattleUnit *me = nullptr;       // the soldier who used the scanner
	glm::vec3 eyeWorld{0.f};
	bool haveEye = false;
	// this frame's camera (world) and its footprint on the soldier's floor (tiles from the soldier), for the
	// side radar
	glm::vec3 camPos{0.f}, camDir{0.f, 0.f, -1.f}, camUp{0.f, 1.f, 0.f};
	glm::vec2 foot[4], camTile{0.f};
	bool footOk = false;
	float radarRot = 0.f;           // the side radar's up: degrees clockwise from the map's north
	void camera(const ScannerContext &ctx);
	glm::mat4 feedVP{1.f};
	// buttons
	float pressT[4] = {9.f, 9.f, 9.f, 9.f};
	bool latched = false;           // a fingertip is still on a button
	int pending = -1;               // a press to act on (tests, or a finger)
	int presses = 0;
	std::string last = "-";
	// the scan: the game's own picture, and the contacts around the soldier
	std::vector<uint32_t> radar;
	int radarW = 0, radarH = 0;
	struct Contact { int dx, dy, size; float dist; glm::vec3 world; };
	std::vector<Contact> contacts;
	glm::vec3 origin{0.f}, eastT{0.f}, southT{0.f}; // the soldier's tile (world), one tile east / south over its length squared
	bool haveScan = false;
	float pingT = 0.f, lastTc = 0.f;
	int lastCycle = -1;
	// sounds: made the first time each is needed; they come from the device, wherever it is around the head
	Mix_Chunk *snd[SND_COUNT] = {};
	bool sndTried[SND_COUNT] = {};
	int pings = 0, blips = 0;
	bool soundOn = false;           // the power-on chirp, once the device has its place
	void sound(int which, AudioSlot slot, float volume, const ScannerContext &ctx);
	// the screens' pictures
	std::vector<uint32_t> bar, side;
	TextMask dirMask[8];
	std::map<std::string, TextMask> textCache;

	void build(Game *g);
	void place(const Hands &hands, float dt);
	void cropRadar(const ScannerContext &ctx);
	void scan(const ScannerContext &ctx);
	void composeBar();
	void composeSide();
	const TextMask &text(const std::string &s);
	void blit(std::vector<uint32_t> &dst, int dw, int dh, const TextMask &m, float xPx, int yPx, uint32_t col, float alpha) const;
	float tc() const { return std::fmod(pingT, PING_CYCLE); }
	int cycle() const { return (int)(pingT / PING_CYCLE); }
	/// A contact's light (0 = not shown): from when the wave reaches it (a flash first) until its part of the
	/// picture drops out (the shader's own timing, without the per-pixel scatter).
	float contactLight(const Contact &c) const
	{
		const float life = LIFE + LIFE_JITTER * 0.5f;
		float a = tc() - (c.dist / PING_SPEED + FILL * 0.5f);
		if (a >= 0.f && a < life) return 1.f + 0.9f * std::exp(-a * 7.f);
		if (a < 0.f && cycle() > 0 && a + PING_CYCLE < life) return 1.f;
		return 0.f;
	}
	glm::vec3 artToDevice(float x, float y, float z) const { return glm::vec3((x - grip.x) * PX, (grip.y - y) * PX, z); }
	float frontZ(uint8_t c) const;
};

enum : uint8_t { C_OUT = 0, C_BODY, C_HOLE, C_BAR, C_SIDE, C_BTN = 10 };

HandScanner::HandScanner() : _p(new Impl()) {}
HandScanner::~HandScanner() = default;

bool HandScanner::isOpen() const { return _p->phase == Impl::OPENING || _p->phase == Impl::OPEN; }

float HandScanner::Impl::frontZ(uint8_t c) const
{
	switch (c)
	{
	case C_OUT: return -F;
	case C_HOLE: return F - SCREEN_IN;
	case C_BAR: case C_SIDE: return F - PANEL_IN;
	case C_BODY: return F;
	default: return F + BTN_UP;
	}
}

const TextMask &HandScanner::Impl::text(const std::string &s)
{
	auto it = textCache.find(s);
	if (it != textCache.end()) return it->second;
	TextMask &m = textCache[s];
	Mod *mod = game ? game->getMod() : nullptr;
	Font *big = mod ? mod->getFont("FONT_BIG", false) : nullptr, *small = mod ? mod->getFont("FONT_SMALL", false) : nullptr;
	if (!big || !small) return m;
	const int W = 120, H = 9;
	Text t(W, H, 0, 0);
	t.initText(big, small, game->getLanguage());
	if (Palette *pal = mod->getPalette("PAL_BATTLESCAPE", false)) t.setPalette(pal->getColors());
	t.setSmall();
	t.setColor(1);
	t.setText(s);
	t.draw();
	m.w = std::min(W, t.getTextWidth());
	m.h = H;
	m.px.assign((size_t)m.w * H, 0);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < m.w; ++x) { int v = t.getPixel(x, y); m.px[(size_t)y * m.w + x] = (v == 1 || v == 2) ? 1 : 0; } // the font's own shading (3-5) left out
	return m;
}

/// A text mask onto a TS-scaled picture: xPx, yPx in art pixels (x may fall between pixels).
void HandScanner::Impl::blit(std::vector<uint32_t> &dst, int dw, int dh, const TextMask &m, float xPx, int yPx, uint32_t col, float alpha) const
{
	if (alpha <= 0.01f) return;
	int ox = (int)std::lround(xPx * TS), oy = yPx * TS;
	for (int y = 0; y < m.h; ++y)
		for (int x = 0; x < m.w; ++x)
		{
			if (!m.px[(size_t)y * m.w + x]) continue;
			for (int sy = 0; sy < TS; ++sy)
				for (int sx = 0; sx < TS; ++sx)
				{
					int tx = ox + x * TS + sx, ty = oy + y * TS + sy;
					if (tx < 0 || ty < 0 || tx >= dw || ty >= dh) continue;
					uint32_t &o = dst[(size_t)ty * dw + tx];
					o = mixc(o, col, alpha);
				}
		}
}

/// The device: the scanner screen's art made solid. The front is one picture (the frame), with the
/// screens and buttons cut out of it; walls stand where the surface steps (the outline, the screens'
/// edges, the buttons' sides); the back is flat.
void HandScanner::Impl::build(Game *g)
{
	built = true;
	Mod *mod = g ? g->getMod() : nullptr;
	Surface *s = mod ? mod->getSurface("DETBORD.PCK", false) : nullptr;
	Palette *pal = mod ? mod->getPalette("PAL_BATTLESCAPE", false) : nullptr;
	if (!s || !pal || s->getWidth() < 300 || s->getHeight() < 190)
	{
		Log(LOG_ERROR) << "[VR] scanner: no DETBORD.PCK art";
		return;
	}
	const int W = 320, H = 200;
	SDL_Color *c = pal->getColors();
	art.assign((size_t)W * H, 0);
	cls.assign((size_t)W * H, C_BODY);
	std::vector<uint8_t> idx((size_t)W * H);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			Uint8 i = s->getPixel(x, y);
			idx[(size_t)y * W + x] = i;
			art[(size_t)y * W + x] = rgba(c[i].r, c[i].g, c[i].b);
		}
	// outside: the empty pixels reached from the picture's border; the other empty ones are the screen
	std::vector<int> stack;
	auto push = [&](int x, int y) {
		if (x < 0 || y < 0 || x >= W || y >= H) return;
		size_t k = (size_t)y * W + x;
		if (idx[k] != 0 || cls[k] == C_OUT) return;
		cls[k] = C_OUT;
		stack.push_back((int)k);
	};
	for (int x = 0; x < W; ++x) { push(x, 0); push(x, H - 1); }
	for (int y = 0; y < H; ++y) { push(0, y); push(W - 1, y); }
	while (!stack.empty())
	{
		int k = stack.back(); stack.pop_back();
		int x = k % W, y = k / W;
		push(x + 1, y); push(x - 1, y); push(x, y + 1); push(x, y - 1);
	}
	for (size_t k = 0; k < cls.size(); ++k) if (idx[k] == 0 && cls[k] != C_OUT) cls[k] = C_HOLE;
	auto fill = [&](const Rect &r, uint8_t v) {
		for (int y = r.y0; y <= r.y1; ++y) for (int x = r.x0; x <= r.x1; ++x) cls[(size_t)y * W + x] = v;
	};
	fill(BAR, C_BAR);
	fill(SIDE, C_SIDE);
	for (int b = 0; b < 4; ++b) fill(BTN[b], (uint8_t)(C_BTN + b));
	int x0 = W, y0 = H, x1 = -1, y1 = -1;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			if (cls[(size_t)y * W + x] != C_OUT) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
	if (x1 < x0) return;

	// the frame's picture: solid where the frame and the buttons are
	std::vector<uint32_t> tex((size_t)W * H);
	for (size_t k = 0; k < tex.size(); ++k)
	{
		bool solid = cls[k] == C_BODY || cls[k] >= C_BTN;
		tex[k] = solid ? art[k] : (art[k] & 0x00FFFFFFu);
	}
	bodyTex.create(W, H, true, false);
	bodyTex.update(tex.data(), W, H);

	auto uv = [&](float x, float y) { return glm::vec2(x / W, y / H); };
	auto quadXY = [&](MeshData &m, float ax0, float ay0, float ax1, float ay1, float z, const glm::vec4 &col, float mat) {
		// art rect (pixel edges) as a quad facing the front, textured with the art's own uv
		m.addQuad(artToDevice(ax0, ay1, z), artToDevice(ax1, ay1, z), artToDevice(ax1, ay0, z), artToDevice(ax0, ay0, z), col, mat,
			uv(ax0, ay1), uv(ax1, ay1), uv(ax1, ay0), uv(ax0, ay0));
	};
	MeshData front, walls, bt[4], bw[4];
	quadXY(front, (float)x0, (float)y0, (float)x1 + 1.f, (float)y1 + 1.f, F, glm::vec4(1.f), 0.f);
	for (int b = 0; b < 4; ++b)
		quadXY(bt[b], (float)BTN[b].x0, (float)BTN[b].y0, (float)BTN[b].x1 + 1.f, (float)BTN[b].y1 + 1.f, F + BTN_UP, glm::vec4(1.f), 0.f);
	// walls where the surface steps down from a pixel to its neighbour
	auto lin = [](uint32_t col, float k) { return glm::vec4(R(col) / 255.f * k, G(col) / 255.f * k, B(col) / 255.f * k, 1.f); };
	const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
	for (int y = y0; y <= y1; ++y)
		for (int x = x0; x <= x1; ++x)
		{
			uint8_t a = cls[(size_t)y * W + x];
			if (a == C_OUT) continue;
			float za = frontZ(a);
			for (int d = 0; d < 4; ++d)
			{
				int nx = x + DX[d], ny = y + DY[d];
				uint8_t b = (nx < 0 || ny < 0 || nx >= W || ny >= H) ? C_OUT : cls[(size_t)ny * W + nx];
				float zb = frontZ(b);
				if (za <= zb + 1e-6f) continue;
				// the shared edge (art coordinates)
				float ex0, ey0, ex1, ey1;
				if (DX[d] == 1) { ex0 = ex1 = (float)x + 1.f; ey0 = (float)y; ey1 = (float)y + 1.f; }
				else if (DX[d] == -1) { ex0 = ex1 = (float)x; ey0 = (float)y; ey1 = (float)y + 1.f; }
				else if (DY[d] == 1) { ey0 = ey1 = (float)y + 1.f; ex0 = (float)x; ex1 = (float)x + 1.f; }
				else { ey0 = ey1 = (float)y; ex0 = (float)x; ex1 = (float)x + 1.f; }
				uint32_t ac = art[(size_t)y * W + x];
				bool button = a >= C_BTN;
				glm::vec4 col = b == C_OUT ? lin(ac, 0.8f) : button ? lin(ac, 0.6f) : glm::vec4(0.05f, 0.07f, 0.05f, 1.f);
				MeshData &m = button ? bw[a - C_BTN] : walls;
				m.addQuad(artToDevice(ex0, ey0, zb), artToDevice(ex1, ey1, zb), artToDevice(ex1, ey1, za), artToDevice(ex0, ey0, za),
					col, (float)MAT_GLOSSY);
			}
		}
	// the back: plain dark metal, one strip per row
	for (int y = y0; y <= y1; ++y)
	{
		int x = x0;
		while (x <= x1)
		{
			while (x <= x1 && cls[(size_t)y * W + x] == C_OUT) ++x;
			if (x > x1) break;
			int s0 = x;
			while (x <= x1 && cls[(size_t)y * W + x] != C_OUT) ++x;
			walls.addQuad(artToDevice((float)s0, (float)y, -F), artToDevice((float)x, (float)y, -F),
				artToDevice((float)x, (float)y + 1.f, -F), artToDevice((float)s0, (float)y + 1.f, -F),
				glm::vec4(0.16f, 0.15f, 0.15f, 1.f), (float)MAT_GLOSSY);
		}
	}
	frontMesh.upload(front);
	wallMesh.upload(walls);
	for (int b = 0; b < 4; ++b) { btnTop[b].upload(bt[b]); btnWall[b].upload(bw[b]); }
	// the screens: the camera (GL picture: rows bottom-up), the compass and the side display
	auto screenQuad = [&](Mesh &mesh, const Rect &r, float z, bool flipV) {
		MeshData m;
		glm::vec2 bl = flipV ? glm::vec2(0.f, 0.f) : glm::vec2(0.f, 1.f), br = flipV ? glm::vec2(1.f, 0.f) : glm::vec2(1.f, 1.f);
		glm::vec2 tr = flipV ? glm::vec2(1.f, 1.f) : glm::vec2(1.f, 0.f), tl = flipV ? glm::vec2(0.f, 1.f) : glm::vec2(0.f, 0.f);
		m.addQuad(artToDevice((float)r.x0, (float)r.y1 + 1.f, z), artToDevice((float)r.x1 + 1.f, (float)r.y1 + 1.f, z),
			artToDevice((float)r.x1 + 1.f, (float)r.y0, z), artToDevice((float)r.x0, (float)r.y0, z), glm::vec4(1.f), 0.f, bl, br, tr, tl);
		mesh.upload(m);
	};
	screenQuad(feedMesh, FEED, F - SCREEN_IN + 0.0001f, true);
	screenQuad(barMesh, BAR, F - PANEL_IN + 0.0001f, false);
	screenQuad(sideMesh, SIDE, F - PANEL_IN + 0.0001f, false);
	feedTex.create(FEED_N, FEED_N, false, true);
	std::vector<uint32_t> black((size_t)FEED_N * FEED_N, rgba(2, 10, 4));
	feedTex.update(black.data(), FEED_N, FEED_N);
	// the compass labels
	for (int i = 0; i < 8; ++i) dirMask[i] = text(DIRS[i]);
	artOk = true;
	Log(LOG_INFO) << "[VR] scanner: built from DETBORD.PCK, " << (x1 - x0 + 1) << " x " << (y1 - y0 + 1) << " pixels";
}

/// Held by its grip in the hand: the edge in the fist, the screen facing back toward the player, the
/// camera looking out of the back a little below where the hand points (TILT).
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
	glm::vec3 right = glm::normalize(glm::cross(up, -fwd));
	// tipped about its horizontal axis: the camera (out of the back) looks TILT below the hand's forward
	const float ct = std::cos(glm::radians(TILT)), st = std::sin(glm::radians(TILT));
	glm::vec3 cam = fwd * ct - up * st;
	up = up * ct + fwd * st;
	glm::vec3 toUser = -cam;                          // the screen faces back toward the player
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
}

/// The game's scanner picture, put together from the two surfaces the game draws it from: the rings
/// (DETBORD2.PCK, where the ScannerView sits on it) and the ScannerView's own blips and arrow. Not cut out of
/// the finished frame, so the game's mouse cursor (moved by the laser over the wall screen) never shows.
void HandScanner::Impl::cropRadar(const ScannerContext &ctx)
{
	if (!src || !ctx.game) return;
	Surface *rings = ctx.game->getMod()->getSurface("DETBORD2.PCK", false);
	for (Surface *sf : src->vrSurfaces())
	{
		ScannerView *v = dynamic_cast<ScannerView*>(sf);
		if (!v) continue;
		int w = v->getWidth(), h = v->getHeight();
		SDL_Color *pal = v->getPalette();
		if (w <= 0 || h <= 0 || !pal) return;
		radarW = w; radarH = h;
		radar.assign((size_t)w * h, 0xFF000000u);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				Uint8 i = v->getPixel(x, y);
				if (!i && rings) i = rings->getPixel(FEED.x0 + x, FEED.y0 + y); // the ScannerView's spot on the rings
				radar[(size_t)y * w + x] = rgba(pal[i].r, pal[i].g, pal[i].b);
			}
		return;
	}
}

/// The contacts, exactly as the game's scanner finds them: units within 9 tiles (any floor) that moved,
/// sized by how much (ScannerView::draw).
void HandScanner::Impl::scan(const ScannerContext &ctx)
{
	contacts.clear();
	haveScan = false;
	SavedBattleGame *battle = (ctx.game && ctx.game->getSavedGame()) ? ctx.game->getSavedGame()->getSavedBattle() : nullptr;
	me = battle ? battle->getSelectedUnit() : nullptr;
	if (!me || !ctx.tileCenter) return;
	Position p = me->getPosition();
	origin = ctx.tileCenter(p.x, p.y, p.z);
	glm::vec3 e = ctx.tileCenter(p.x + 1, p.y, p.z) - origin, s = ctx.tileCenter(p.x, p.y + 1, p.z) - origin;
	if (glm::dot(e, e) < 1e-10f || glm::dot(s, s) < 1e-10f) return;
	eastT = e / glm::dot(e, e);
	southT = s / glm::dot(s, s);
	haveScan = true;
	for (int y = -9; y < 10; ++y)
		for (int x = -9; x < 10; ++x)
		{
			int size = -1;
			for (int z = 0; z < battle->getMapSizeZ(); ++z)
			{
				Tile *t = battle->getTile(Position(p.x + x, p.y + y, z));
				// (the soldier with the scanner isn't a contact)
				if (t && t->getUnit() && t->getUnit() != me && t->getUnit()->getMotionPoints())
					size = std::max(size, std::min(5, t->getUnit()->getMotionPoints() / 5));
			}
			if (size < 0) continue;
			Contact c;
			c.dx = x; c.dy = y; c.size = size;
			c.dist = std::sqrt((float)(x * x + y * y));
			c.world = ctx.tileCenter(p.x + x, p.y + y, p.z);
			contacts.push_back(c);
		}
}

/// The compass strip: the map's 8 directions sliding past a mark in the middle.
void HandScanner::Impl::composeBar()
{
	const int W = BAR.w() * TS, H = BAR.h() * TS;
	bar.assign((size_t)W * H, 0);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			bar[(size_t)y * W + x] = scalec(art[(size_t)(BAR.y0 + y / TS) * 320 + BAR.x0 + x / TS], 0.55f);
	const float mid = BAR.w() * 0.5f, perDeg = 18.f / 45.f; // 45 degrees = 18 art pixels
	const uint32_t lit = rgba(200, 245, 120), dim = rgba(90, 160, 50), mark = rgba(255, 196, 70);
	for (int a = 0; a < 360; a += 15)
	{
		float off = std::remainder((float)a - heading, 360.f);
		float x = mid + off * perDeg;
		float fade = 1.f - smooth(mid - 6.f, mid - 1.f, std::fabs(x - mid));
		if (a % 45 == 0)
		{
			const TextMask &m = dirMask[a / 45];
			blit(bar, W, H, m, x - m.w * 0.5f, 1, lit, fade);
		}
		else
		{
			// a tick between the labels
			int tx = (int)std::lround(x * TS);
			for (int ty = 8 * TS; ty < 10 * TS; ++ty)
				for (int k = -1; k <= 1; ++k)
					if (tx + k >= 0 && tx + k < W) { uint32_t &o = bar[(size_t)ty * W + tx + k]; o = mixc(o, dim, fade); }
		}
	}
	// the middle mark, top and bottom
	int cx = (int)(mid * TS);
	for (int y = 0; y < TS; ++y)
		for (int x = -TS / 2; x < TS / 2; ++x) { bar[(size_t)y * W + cx + x] = mark; bar[(size_t)(H - 1 - y) * W + cx + x] = mark; }
}

/// The side display: the game's own radar picture (north up, or turning with the device) with the
/// camera's view on it, and a readout of the contacts.
void HandScanner::Impl::composeSide()
{
	const int W = SIDE.w() * TS, H = SIDE.h() * TS;
	side.assign((size_t)W * H, 0);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			side[(size_t)y * W + x] = scalec(art[(size_t)(SIDE.y0 + y / TS) * 320 + SIDE.x0 + x / TS], 0.45f);
	// the radar: a circle of 45 art pixels near the top, the game's picture turned (radarRot up), the whole
	// square kept in it
	const int S0 = 2 * TS, SN = 45 * TS;
	const float cx = S0 + SN * 0.5f, cy = S0 + SN * 0.5f, Rr = SN * 0.5f;
	const float fov = FOVS[fovIdx];
	const float th = glm::radians(radarRot);
	const glm::vec2 rr(std::cos(th), std::sin(th)), uu(std::sin(th), -std::cos(th));
	const float k = (float)radarW / (SN / std::sqrt(2.f));
	const float pxPerTile = radarW / 19.f;
	// tiles from the soldier -> a texel of the side display
	auto toTex = [&](const glm::vec2 &tiles) { glm::vec2 g = tiles * pxPerTile / k; return glm::vec2(cx + glm::dot(g, rr), cy - glm::dot(g, uu)); };
	glm::vec2 q[4];
	if (footOk) for (int i = 0; i < 4; ++i) q[i] = toTex(foot[i]);
	auto inQuad = [&](const glm::vec2 &pt) {
		int sgn = 0;
		for (int i = 0; i < 4; ++i)
		{
			glm::vec2 a = q[i], b = q[(i + 1) % 4];
			float c = (b.x - a.x) * (pt.y - a.y) - (b.y - a.y) * (pt.x - a.x);
			int sc = c > 0.f ? 1 : -1;
			if (sgn == 0) sgn = sc; else if (sc != sgn) return false;
		}
		return true;
	};
	auto segDist = [](const glm::vec2 &pt, const glm::vec2 &a, const glm::vec2 &b) {
		glm::vec2 ab = b - a;
		float t = glm::clamp(glm::dot(pt - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.f, 1.f);
		return glm::length(pt - (a + ab * t));
	};
	auto sample = [&](float fx, float fy) -> uint32_t {
		// bilinear from the game's picture
		if (radar.empty() || fx < 0.f || fy < 0.f || fx > radarW - 1.f || fy > radarH - 1.f) return rgba(3, 14, 4);
		int x0 = (int)fx, y0 = (int)fy, x1 = std::min(x0 + 1, radarW - 1), y1 = std::min(y0 + 1, radarH - 1);
		float ax = fx - x0, ay = fy - y0;
		uint32_t a = mixc(radar[(size_t)y0 * radarW + x0], radar[(size_t)y0 * radarW + x1], ax);
		uint32_t b = mixc(radar[(size_t)y1 * radarW + x0], radar[(size_t)y1 * radarW + x1], ax);
		return mixc(a, b, ay);
	};
	glm::vec2 camT = toTex(camTile);
	const uint32_t lit = rgba(190, 255, 150);
	for (int y = S0; y < S0 + SN; ++y)
		for (int x = S0; x < S0 + SN; ++x)
		{
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			uint32_t &o = side[(size_t)y * W + x];
			float d2 = dx * dx + dy * dy;
			if (d2 > Rr * Rr) { if (d2 < (Rr + 3.f) * (Rr + 3.f)) o = rgba(42, 90, 52); continue; }
			glm::vec2 g = rr * dx - uu * dy;
			o = sample(radarW * 0.5f + g.x * k - 0.5f, radarH * 0.5f + g.y * k - 0.5f);
			glm::vec2 pt(x + 0.5f, y + 0.5f);
			if (soldierView)
			{
				// the soldier's view: a cone up from the middle
				float r = std::sqrt(d2);
				if (r > 3.f)
				{
					float rel = std::fabs(std::atan2(dx, -dy) * 57.29578f);
					if (rel <= fov * 0.5f) o = mixc(o, rgba(170, 255, 140), 0.16f);
					if (std::fabs(rel - fov * 0.5f) * 0.01745f * r < 1.6f) o = mixc(o, lit, 0.55f);
				}
			}
			else if (footOk)
			{
				// the commander's camera: the patch of map it films, and where the scanner itself is
				if (inQuad(pt)) o = mixc(o, rgba(170, 255, 140), 0.16f);
				float e = 1e9f;
				for (int i = 0; i < 4; ++i) e = std::min(e, segDist(pt, q[i], q[(i + 1) % 4]));
				if (e < 1.6f) o = mixc(o, lit, 0.55f);
				float rc = glm::length(pt - camT);
				if (std::fabs(rc - 7.f) < 1.6f) o = mixc(o, rgba(255, 200, 80), 0.8f);
			}
		}
	// the readout
	std::string l1, l2, l3;
	if (contacts.empty()) l1 = "NO MOTION";
	else
	{
		std::ostringstream a, c;
		a << "CONTACTS " << contacts.size();
		l1 = a.str();
		if (text(l1).w > SIDE.w() - 2) { std::ostringstream b; b << "MOTION " << contacts.size(); l1 = b.str(); }
		const Contact *n = &contacts[0];
		for (const Contact &q : contacts) if (q.dist < n->dist) n = &q;
		l2 = "NEAREST";
		if (n->dx == 0 && n->dy == 0) c << "HERE";
		else c << (int)std::lround(n->dist) << " " << DIRS[dir8((float)n->dx, (float)n->dy)];
		l3 = c.str();
	}
	const uint32_t tc = rgba(200, 235, 110);
	int yl = 50;
	for (const std::string *l : {&l1, &l2, &l3})
	{
		if (!l->empty()) { const TextMask &m = text(*l); blit(side, W, H, m, (SIDE.w() - m.w) * 0.5f, yl, tc, 1.f); }
		yl += 10;
	}
}

void HandScanner::freeze(float t) { _p->freezeT = t; }

/// A screen turning on (k 0..1): a line across first, then it opens to the full picture; returns the
/// shader's uCrt (open vertically, open across, glow).
static glm::vec3 crtOn(float k)
{
	k = glm::clamp(k, 0.f, 1.f);
	if (k <= 0.f) return glm::vec3(0.f);
	if (k < 0.3f) return glm::vec3(0.f, k / 0.3f, 1.f);
	float v = glm::smoothstep(0.f, 1.f, (k - 0.3f) / 0.7f);
	return glm::vec3(v, 1.f, 1.f - v);
}
/// ...and off (k 0..1): it closes to a line, then to a dot.
static glm::vec3 crtOff(float k)
{
	k = glm::clamp(k, 0.f, 1.f);
	if (k >= 1.f) return glm::vec3(0.f);
	if (k < 0.6f) { float v = 1.f - glm::smoothstep(0.f, 1.f, k / 0.6f); return glm::vec3(v, 1.f, 1.f - v); }
	return glm::vec3(0.f, 1.f - (k - 0.6f) / 0.4f, 1.f);
}

void HandScanner::Impl::sound(int which, AudioSlot slot, float volume, const ScannerContext &ctx)
{
	if (which == SND_PING) ++pings;
	if (which >= SND_BLIP0) ++blips;
	int freq = audioFreq();
	if (!freq) return;
	if (!sndTried[which]) { sndTried[which] = true; snd[which] = makeChunk(synthScan(which, freq)); }
	if (!snd[which]) return;
	// from the device, as the head hears it: degrees clockwise from where it faces
	glm::vec3 d = glm::vec3(xf[3]) - ctx.eye;
	glm::vec2 f(ctx.headFwd.x, ctx.headFwd.z), dv(d.x, d.z);
	float angle = 0.f;
	if (glm::length(f) > 1e-4f && glm::length(dv) > 1e-4f)
	{
		f = glm::normalize(f);
		glm::vec2 r(-f.y, f.x);
		angle = std::atan2(glm::dot(dv, r), glm::dot(dv, f)) * 57.29578f;
	}
	playAt(slot, snd[which], volume, angle);
}

void HandScanner::update(const ScannerContext &ctx, const Hands &hands, float dt)
{
	Impl &p = *_p;
	p.t += dt;
	p.game = ctx.game;
	// is the game's scanner screen up?
	State *top = (ctx.game && !ctx.game->getStates().empty()) ? ctx.game->getStates().back() : nullptr;
	State *scanState = dynamic_cast<ScannerState*>(top) ? top : nullptr;
	if (scanState && (p.phase == Impl::CLOSED || p.phase == Impl::CLOSING || p.src != scanState))
	{
		// in the right hand unless only the left one is there
		p.hand = hands.pose[1].valid ? 1 : (hands.pose[0].valid ? 0 : 1);
		glm::vec2 grip = p.hand == 1 ? GRIP_R : GRIP_L;
		if (!p.built || grip != p.grip) { p.grip = grip; p.build(ctx.game); }
		p.src = scanState;
		p.phase = Impl::OPENING;
		p.t = 0.f;
		p.armed = 0.f;
		p.xfKnown = false;
		p.pingT = 0.f;
		p.lastTc = 0.f;
		p.lastCycle = -1;
		p.feedFresh = false;
		p.soldierView = false;      // it always comes out in the commander view
		p.eyeStep = 0;
		p.last = "open";
		p.soundOn = true;
		if (ctx.haptic) ctx.haptic(p.hand, 0.35f, 0.08f);
		Log(LOG_INFO) << "[VR] scanner: open in the " << (p.hand == 1 ? "right" : "left") << " hand";
	}
	if (!scanState && (p.phase == Impl::OPENING || p.phase == Impl::OPEN))
	{
		p.phase = Impl::CLOSING;
		p.t = 0.f;
		p.src = nullptr;
		p.sound(SND_OFF, AUDIO_SCANNER_UI, 0.45f, ctx);
		Log(LOG_INFO) << "[VR] scanner: closed";
	}
	if (p.freezeT >= 0.f && (p.phase == Impl::OPENING || p.phase == Impl::CLOSING)) p.t = p.freezeT;
	if (p.phase == Impl::OPENING && p.t > OPEN_T) { p.phase = Impl::OPEN; p.t = 0.f; }
	if (p.phase == Impl::CLOSING && p.t > CLOSE_T) { p.phase = Impl::CLOSED; p.t = 0.f; }
	if (p.phase == Impl::CLOSED || !p.artOk) return;

	p.place(hands, dt);
	// the screens turn on: the chirp
	if (p.soundOn && (p.phase != Impl::OPENING || p.t >= BUILD_END)) { p.soundOn = false; p.sound(SND_ON, AUDIO_SCANNER_UI, 0.45f, ctx); }
	// where the camera points, on the map: out of the back of the device, laid flat (pointed straight
	// down, the top of the screen decides)
	glm::vec3 fwd = -glm::vec3(p.xf[2]), up = glm::vec3(p.xf[1]);
	glm::vec2 h(fwd.x + up.x, fwd.z + up.z);
	if (glm::length(h) > 1e-3f)
	{
		h = glm::normalize(h);
		float target = glm::degrees(std::atan2(glm::dot(h, ctx.east), glm::dot(h, ctx.north)));
		float diff = std::remainder(target - p.heading, 360.f);
		p.heading = std::fmod(p.heading + diff * (1.f - std::exp(-dt * 20.f)) + 360.f, 360.f);
	}
	if (p.src) { p.cropRadar(ctx); p.scan(ctx); }
	p.camera(ctx);

	// the ping (once the screens are on): a tick in the holding hand as the wave passes each contact
	if (p.phase == Impl::OPEN)
	{
		p.pingT += dt;
		float now = p.tc();
		if (p.cycle() != p.lastCycle)
		{
			p.lastCycle = p.cycle();
			p.lastTc = -1.f;
			p.sound(SND_PING, AUDIO_SCANNER_PING, 0.55f, ctx);
		}
		// contacts the wave reached this frame: one blip (and tick) for them all, pitched by the nearest
		float nearest = -1.f;
		for (const Impl::Contact &c : p.contacts)
		{
			float t0 = c.dist / PING_SPEED;
			if (t0 > p.lastTc && t0 <= now && (nearest < 0.f || c.dist < nearest)) nearest = c.dist;
		}
		if (nearest >= 0.f)
		{
			if (ctx.haptic) ctx.haptic(p.hand, 0.18f, 0.015f);
			int k = std::min(SND_BLIPS - 1, (int)(nearest / CORNER * SND_BLIPS));
			p.sound(SND_BLIP0 + k, AUDIO_SCANNER_BLIP, 0.5f, ctx);
		}
		p.lastTc = now;
	}
	p.composeBar();
	p.composeSide();
	if (!p.barTex.valid()) p.barTex.create(BAR.w() * TS, BAR.h() * TS, false, true);
	p.barTex.update(p.bar.data(), BAR.w() * TS, BAR.h() * TS);
	if (!p.sideTex.valid()) p.sideTex.create(SIDE.w() * TS, SIDE.h() * TS, false, true);
	p.sideTex.update(p.side.data(), SIDE.w() * TS, SIDE.h() * TS);

	// the buttons, pressed with the other hand's index fingertip
	for (float &t : p.pressT) t += dt;
	if (p.phase == Impl::OPEN) p.armed += dt;
	const HandPose &other = hands.pose[1 - p.hand];
	if (p.phase == Impl::OPEN && other.valid && !other.ghost)
	{
		glm::vec3 tip = other.tip(F_INDEX);
		float best = 1e9f;
		int which = -1;
		for (int b = 0; b < 4; ++b)
		{
			const Rect &r = BTN[b];
			glm::vec3 c = glm::vec3(p.xf * glm::vec4(p.artToDevice((r.x0 + r.x1 + 1) * 0.5f, (r.y0 + r.y1 + 1) * 0.5f, F + BTN_UP), 1.f));
			float d = glm::length(tip - c);
			if (d < best) { best = d; which = b; }
		}
		if (p.latched && best > 0.025f) p.latched = false;
		if (!p.latched && p.armed > 0.3f && best < 0.011f) { p.latched = true; p.pending = which; }
	}
	if (p.pending >= 0 && p.phase == Impl::OPEN)
	{
		int b = p.pending;
		p.pending = -1;
		p.pressT[b] = 0.f;
		++p.presses;
		p.sound(b == B_RED ? SND_CLICK_RED : SND_CLICK, AUDIO_SCANNER_UI, 0.45f, ctx);
		if (ctx.haptic) { ctx.haptic(1 - p.hand, 0.5f, 0.04f); ctx.haptic(p.hand, 0.25f, 0.04f); }
		std::string what;
		if (b == B_RED)
		{
			// the game's scanner closes on a right-click anywhere
			if (ctx.clickScreen) ctx.clickScreen(ctx.surfW / 2, ctx.surfH / 2, SDL_BUTTON_RIGHT);
			p.armed = -1e9f;
			what = "close";
		}
		else if (b == B_BLUE && p.soldierView) { p.eyeStep = std::max(0, p.eyeStep - 1); what = "view lower"; }
		else if (b == B_YELLOW && p.soldierView) { p.eyeStep = std::min(2, p.eyeStep + 1); what = "view higher"; }
		else if (b == B_BLUE) { p.fovIdx = std::max(0, p.fovIdx - 1); what = "zoom in"; }
		else if (b == B_YELLOW) { p.fovIdx = std::min(FOV_N - 1, p.fovIdx + 1); what = "zoom out"; }
		else { p.soldierView = !p.soldierView; what = p.soldierView ? "soldier view" : "commander view"; }
		p.last = BTN_NAME[b];
		Log(LOG_INFO) << "[VR] scanner: " << BTN_NAME[b] << " button (" << what << ") fov=" << (int)FOVS[p.fovIdx] << " step=" << p.eyeStep;
	}
	else p.pending = -1;
}

bool HandScanner::testPress(const std::string &button)
{
	Impl &p = *_p;
	if (p.phase != Impl::OPEN) return false;
	for (int b = 0; b < 4; ++b)
		if (button == BTN_NAME[b]) { p.pending = b; return true; }
	return false;
}

bool HandScanner::feedCamera(glm::mat4 &view, glm::mat4 &proj, glm::vec3 &eye)
{
	Impl &p = *_p;
	if (!p.artOk || !p.xfKnown || !p.haveScan || (p.phase != Impl::OPEN && p.phase != Impl::OPENING)) return false;
	// half the frame rate is plenty for the little screen
	if (p.feedFresh && (++p.feedSkip & 1)) return false;
	view = glm::lookAt(p.camPos, p.camPos + p.camDir, p.camUp);
	// the soldier view is down among the figures: a much nearer near plane
	proj = glm::perspective(glm::radians(FOVS[p.fovIdx]), 1.f, p.soldierView ? 0.0008f : 0.01f, 20.f);
	eye = p.camPos;
	p.feedVP = proj * view;
	return true;
}

BattleUnit *HandScanner::feedHides() const
{
	const Impl &p = *_p;
	return p.soldierView && isOpen() ? p.me : nullptr;
}

/// This frame's camera. Commander view: out of the back of the device in the hand. Soldier view: at the
/// soldier's eyes on the table (a floor higher per step), looking where the hand points, the horizon level.
/// Also what the side radar shows: its turn, and the camera's footprint on the soldier's floor.
void HandScanner::Impl::camera(const ScannerContext &ctx)
{
	glm::vec3 dev = -glm::vec3(xf[2]), devUp = glm::vec3(xf[1]);
	haveEye = false;
	if (soldierView && me && ctx.voxelToWorld && ctx.game && ctx.game->getSavedGame()->getSavedBattle())
	{
		Position e = ctx.game->getSavedGame()->getSavedBattle()->getTileEngine()->getSightOriginVoxel(me);
		eyeWorld = ctx.voxelToWorld(glm::vec3((float)e.x, (float)e.y, (float)e.z + 24.f * eyeStep));
		haveEye = true;
	}
	if (soldierView && haveEye)
	{
		// the compass heading of the commander's camera, and its pitch raised by the device's tilt (so a
		// level hand looks level), the horizon kept level
		camPos = eyeWorld;
		glm::vec2 hz(dev.x + devUp.x * 0.f, dev.z);
		if (glm::length(hz) < 0.15f) hz += glm::vec2(devUp.x, devUp.z); // pointed straight down: the screen's top
		hz = glm::length(hz) > 1e-4f ? glm::normalize(hz) : glm::vec2(0.f, -1.f);
		float pitch = glm::clamp(std::asin(glm::clamp(dev.y, -1.f, 1.f)) + glm::radians(TILT), glm::radians(-80.f), glm::radians(80.f));
		camDir = glm::vec3(hz.x * std::cos(pitch), std::sin(pitch), hz.y * std::cos(pitch));
		camUp = glm::vec3(0.f, 1.f, 0.f);
	}
	else
	{
		camPos = glm::vec3(xf * glm::vec4(artToDevice((FEED.x0 + FEED.x1 + 1) * 0.5f, (FEED.y0 + FEED.y1 + 1) * 0.5f, -F - 0.001f), 1.f));
		camDir = dev;
		camUp = devUp;
	}
	// the side radar: in the soldier view its up is where the soldier looks; in the commander view, the
	// table's far side (the radar lies as the table does in front of you)
	if (soldierView) radarRot = heading;
	else
	{
		glm::vec2 away(0.f, -1.f);
		radarRot = glm::degrees(std::atan2(glm::dot(away, ctx.east), glm::dot(away, ctx.north)));
	}
	// where the camera's view meets the soldier's floor (the corner rays; ones above the horizon are cut
	// off 30 tiles out)
	footOk = false;
	if (!haveScan) return;
	auto toTiles = [&](const glm::vec3 &w) { glm::vec3 r = w - origin; return glm::vec2(glm::dot(r, eastT), glm::dot(r, southT)); };
	glm::vec3 right = glm::normalize(glm::cross(camDir, camUp)), up2 = glm::cross(right, camDir);
	float tn = std::tan(glm::radians(FOVS[fovIdx]) * 0.5f);
	const float farDist = 30.f / std::max(1e-4f, std::sqrt(glm::dot(eastT, eastT))); // 30 tiles (world)
	const float cx[4] = {-1.f, 1.f, 1.f, -1.f}, cy[4] = {-1.f, -1.f, 1.f, 1.f};
	for (int i = 0; i < 4; ++i)
	{
		glm::vec3 d = glm::normalize(camDir + right * (tn * cx[i]) + up2 * (tn * cy[i]));
		glm::vec3 hit;
		float hor = std::sqrt(d.x * d.x + d.z * d.z);
		if (d.y < -1e-4f && (origin.y - camPos.y) / d.y * hor < farDist) hit = camPos + d * ((origin.y - camPos.y) / d.y);
		else hit = camPos + glm::vec3(d.x, 0.f, d.z) / std::max(hor, 1e-4f) * farDist;
		foot[i] = toTiles(hit);
	}
	camTile = toTiles(camPos);
	footOk = true;
}

bool HandScanner::beginFeed()
{
	Impl &p = *_p;
	if (p.feedFailed) return false;
	if (!p.feedOk)
	{
		if (!p.feedRT.create(FEED_N, FEED_N, 0)) { p.feedFailed = true; Log(LOG_ERROR) << "[VR] scanner: no camera picture"; return false; }
		p.feedOk = true;
	}
	p.feedRT.bind();
	return true;
}

void HandScanner::endFeed()
{
	Impl &p = *_p;
	p.feedRT.resolveTo(p.feedTex.id());
	p.feedFresh = true;
}

void HandScanner::feedUniforms(const Shader &sh) const
{
	const Impl &p = *_p;
	sh.set("uFeed", 1);
	sh.set("uFeedO", p.origin);
	sh.set("uFeedE", p.eastT);
	sh.set("uFeedS", p.southT);
	sh.set("uFeedClock", glm::vec4(p.tc(), (float)p.cycle(), PING_SPEED, PING_CYCLE));
	sh.set("uFeedFill", glm::vec3(FILL, LIFE, LIFE_JITTER));
	int n = 0;
	for (const Impl::Contact &c : p.contacts)
	{
		if (n >= 24) break;
		float k = std::max(1.f, p.contactLight(c)); // where its part of the picture shows, with the flash
		std::string name = "uFeedC[" + std::to_string(n) + "]";
		sh.set(name.c_str(), glm::vec4((float)c.dx, (float)c.dy, 0.2f + 0.06f * c.size, k));
		++n;
	}
	sh.set("uFeedN", n);
}

/// Contacts out of the camera's view: an arrow at the edge of the picture pointing their way.
void HandScanner::feedOverlay(const Shader &sh) const
{
	const Impl &p = *_p;
	sh.set("uFeed", 0);
	Mesh &arrow = p.arrow;
	if (arrow.empty())
	{
		MeshData m;
		Vertex a{}, b{}, c{};
		a.pos = {1.f, 0.f, 0.f}; b.pos = {-0.7f, 0.75f, 0.f}; c.pos = {-0.7f, -0.75f, 0.f};
		for (Vertex *v : {&a, &b, &c}) { v->normal = {0.f, 0.f, 1.f}; v->color = glm::vec4(1.f); }
		m.indices = {m.addVertex(a), m.addVertex(b), m.addVertex(c)};
		arrow.upload(m);
	}
	glDisable(GL_DEPTH_TEST);
	sh.set("uViewProj", glm::mat4(1.f));
	sh.set("uMode", 2);
	for (const Impl::Contact &c : p.contacts)
	{
		float k = p.contactLight(c);
		if (k <= 0.01f) continue;
		glm::vec4 q = p.feedVP * glm::vec4(c.world, 1.f);
		glm::vec2 v;
		if (q.w > 1e-4f)
		{
			v = glm::vec2(q) / q.w;
			if (std::fabs(v.x) < 0.95f && std::fabs(v.y) < 0.95f) continue;
		}
		else v = -glm::vec2(q); // behind the camera: the other way
		float m = std::max(std::fabs(v.x), std::fabs(v.y));
		if (m < 1e-5f) continue;
		glm::vec2 e = v / m * 0.86f;
		float ang = std::atan2(v.y, v.x);
		glm::mat4 M = glm::translate(glm::mat4(1.f), glm::vec3(e, 0.f)) * glm::rotate(glm::mat4(1.f), ang, glm::vec3(0.f, 0.f, 1.f))
			* glm::scale(glm::mat4(1.f), glm::vec3(0.075f));
		sh.set("uModel", M);
		sh.set("uTint", glm::vec4(glm::vec3(1.f, 0.62f, 0.1f) * std::min(1.6f, k * 1.3f), 1.f));
		arrow.draw();
	}
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", glm::mat4(1.f));
	glEnable(GL_DEPTH_TEST);
}

void HandScanner::draw(const Shader &sh) const
{
	const Impl &p = *_p;
	if (p.phase == Impl::CLOSED || !p.xfKnown || !p.artOk) return;
	const bool opening = p.phase == Impl::OPENING, closing = p.phase == Impl::CLOSING;
	const float t = p.t;
	// how much of the device is there (0..1, from the bottom up)
	float built = 1.f;
	if (opening) built = glm::smoothstep(0.f, 1.f, t / BUILD_END);
	else if (closing) built = 1.f - glm::smoothstep(0.f, 1.f, (t - CRT_OFF) / (CLOSE_T - CRT_OFF));
	const glm::mat4 M = p.xf;
	if (built > 0.001f)
	{
		bool partial = built < 0.999f;
		if (partial)
		{
			// a plane across the device, rising from its bottom edge (the hand) to its top
			glm::vec3 U = glm::normalize(glm::vec3(p.xf[1]));
			float y0 = (p.grip.y - 189.f) * PX - 0.004f, y1 = (p.grip.y - 12.f) * PX + 0.004f;
			glm::vec3 P = glm::vec3(p.xf * glm::vec4(0.f, y0 + (y1 - y0) * built, 0.f, 1.f));
			sh.set("uBuild", 1);
			sh.set("uBuildPlane", glm::vec4(U, -glm::dot(U, P)));
			sh.set("uBuildGlow", HOLO * 2.5f);
		}
		sh.set("uMapShade", 0.f); // plain lighting, also over the map (the caller sets it back)
		sh.set("uModel", M);
		sh.set("uTint", glm::vec4(1.f));
		sh.set("uMode", 0);
		p.wallMesh.draw();
		sh.set("uMode", 3);
		p.bodyTex.bind(0);
		p.frontMesh.draw();
		for (int b = 0; b < 4; ++b)
		{
			// a pressed button goes in and lights up for a moment
			float pt = p.pressT[b];
			float in = pt < 0.18f ? std::sin(pt / 0.18f * 3.14159f) : 0.f;
			glm::mat4 Mb = M * glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, -BTN_UP * 0.8f * in));
			sh.set("uModel", Mb);
			sh.set("uMode", 0);
			sh.set("uTint", glm::vec4(1.f));
			p.btnWall[b].draw();
			sh.set("uMode", 3);
			sh.set("uTint", glm::vec4(glm::vec3(1.f + 1.2f * in), 1.f));
			p.bodyTex.bind(0);
			p.btnTop[b].draw();
		}
		// the screens: tubes, faintly lined and darker at the edges; they turn on once the device is there
		auto crt = [&](float start, float len) {
			if (opening) return crtOn((t - start) / len);
			if (closing) return crtOff(t / CRT_OFF);
			return glm::vec3(1.f, 1.f, 0.f);
		};
		sh.set("uModel", M);
		sh.set("uMode", 1);
		sh.set("uTint", glm::vec4(1.f));
		sh.set("uScanK", 0.3f);
		sh.set("uVignette", 0.4f);
		sh.set("uCrt", crt(BUILD_END, OPEN_T - BUILD_END));
		sh.set("uTexSize", glm::vec2((float)FEED_N, (float)FEED_N));
		sh.set("uScan", 1.f);
		p.feedTex.bind(0);
		p.feedMesh.draw();
		if (p.barTex.valid())
		{
			sh.set("uCrt", crt(BUILD_END + 0.07f, 0.23f));
			sh.set("uTexSize", glm::vec2((float)p.barTex.width(), (float)p.barTex.height()));
			sh.set("uScan", (float)TS);
			p.barTex.bind(0);
			p.barMesh.draw();
		}
		if (p.sideTex.valid())
		{
			sh.set("uCrt", crt(BUILD_END + 0.1f, 0.25f));
			sh.set("uTexSize", glm::vec2((float)p.sideTex.width(), (float)p.sideTex.height()));
			sh.set("uScan", (float)TS);
			p.sideTex.bind(0);
			p.sideMesh.draw();
		}
		sh.set("uCrt", glm::vec3(1.f, 1.f, 0.f));
		sh.set("uScanK", 0.12f);
		sh.set("uVignette", 0.f);
		sh.set("uBuild", 0);
	}
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", glm::mat4(1.f));
	sh.set("uMode", 0);
}

bool writeScannerSounds(const std::string &prefix)
{
	for (int i = 0; i < SND_COUNT; ++i)
		if (!writeWav(prefix + SND_NAME[i] + ".wav", synthScan(i, 44100), 44100)) return false;
	Log(LOG_INFO) << "[VR] scanner sounds written to " << prefix << "*.wav";
	return true;
}

bool HandScanner::dump(const std::string &path) const
{
	const Impl &p = *_p;
	if (!p.artOk || p.bar.empty() || p.side.empty()) return false;
	const int SW = SIDE.w() * TS, SH = SIDE.h() * TS, BW = BAR.w() * TS, BH = BAR.h() * TS;
	const int W = FEED_N * 2 + 8 + std::max(SW, BW), H = std::max(FEED_N * 2, BH + 8 + SH);
	std::vector<unsigned char> img((size_t)W * H * 4, 0);
	for (size_t i = 3; i < img.size(); i += 4) img[i] = 255;
	auto put = [&](int x, int y, uint32_t c) {
		if (x < 0 || y < 0 || x >= W || y >= H) return;
		unsigned char *o = &img[((size_t)y * W + x) * 4];
		o[0] = (unsigned char)R(c); o[1] = (unsigned char)G(c); o[2] = (unsigned char)B(c); o[3] = 255;
	};
	std::vector<uint32_t> feed((size_t)FEED_N * FEED_N, 0);
	glBindTexture(GL_TEXTURE_2D, p.feedTex.id());
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, feed.data());
	for (int y = 0; y < FEED_N * 2; ++y)
		for (int x = 0; x < FEED_N * 2; ++x) put(x, y, feed[(size_t)(FEED_N - 1 - y / 2) * FEED_N + x / 2]);
	int ox = FEED_N * 2 + 8;
	for (int y = 0; y < BH; ++y) for (int x = 0; x < BW; ++x) put(ox + x, y, p.bar[(size_t)y * BW + x]);
	for (int y = 0; y < SH; ++y) for (int x = 0; x < SW; ++x) put(ox + x, BH + 8 + y, p.side[(size_t)y * SW + x]);
	unsigned err = lodepng::encode(path, img, W, H);
	Log(LOG_INFO) << "[VR] scanner screens " << path << (err ? " FAILED" : " saved");
	return !err;
}

std::string HandScanner::status() const
{
	const Impl &p = *_p;
	std::ostringstream o;
	const char *ph[] = {"closed", "opening", "open", "closing"};
	o << "scanner=" << ph[p.phase];
	if (p.phase != Impl::CLOSED)
	{
		o << " hand=" << (p.hand == 1 ? "right" : "left") << " heading=" << (int)std::lround(p.heading)
			<< " " << DIRS[((int)std::lround(p.heading / 45.f)) % 8] << " fov=" << (int)FOVS[p.fovIdx]
			<< " view=" << (p.soldierView ? "soldier" : "commander") << " step=" << p.eyeStep << " radar=" << (int)std::lround(p.radarRot) << " contacts=" << p.contacts.size();
		if (!p.contacts.empty())
		{
			const Impl::Contact *n = &p.contacts[0];
			for (const Impl::Contact &q : p.contacts) if (q.dist < n->dist) n = &q;
			o << " nearest=" << (int)std::lround(n->dist) << "," << DIRS[dir8((float)n->dx, (float)n->dy)];
		}
		o << " feed=" << (p.feedFresh ? "on" : "off") << " pings=" << p.pings << " blips=" << p.blips
			<< " look=" << (int)std::lround(glm::degrees(std::asin(glm::clamp(p.camDir.y, -1.f, 1.f)))) << (p.haveEye ? " eye" : "");
	}
	o << " presses=" << p.presses << " last=" << p.last;
	return o.str();
}

}
}
