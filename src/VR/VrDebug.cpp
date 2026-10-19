/*
 * OXCE VR tabletop - the sky debug panel on the left wrist (see VrDebug.h).
 */
#include "VrDebug.h"
#include "VrBoard.h"
#include "VrDash.h"
#include "VrWeather.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/Logger.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>

namespace OpenXcom
{
namespace VR
{

namespace
{
const int W = 240, H = 380;                 // the panel's picture (pixels)
const float PW = 0.125f;                    // its width on the wrist (metres)
const float PH = PW * H / W;

struct PanelButton
{
	std::string id, label;
	int x, y, w, h;
};

std::string fmt(const char *f, double a = 0, double b = 0, double c = 0)
{
	char buf[128];
	snprintf(buf, sizeof(buf), f, a, b, c);
	return buf;
}

std::string compass(float az)
{
	static const char *n[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
	int i = (int)std::floor(std::fmod(az + 11.25f + 720.f, 360.f) / 22.5f) & 15;
	return n[i];
}

std::string band(int shade)
{
	if (shade <= 3) return "day: high white sun";
	if (shade <= 8) return "low gold sun";
	if (shade <= 11) return "deep orange sun";
	return "night: moonlight";
}

uint32_t toPx(const glm::vec3 &c)
{
	// a light's colour as a swatch (display gamma, scaled to its brightest channel)
	float m = std::max(0.001f, std::max(c.r, std::max(c.g, c.b)));
	glm::vec3 k = glm::pow(glm::clamp(c / m, 0.f, 1.f), glm::vec3(1.f / 2.2f)) * 255.f;
	return Dash::C((int)k.r, (int)k.g, (int)k.b);
}
}

struct WristDebug::Impl
{
	float show = 0.f;
	glm::vec3 c{0.f}, right{1, 0, 0}, up{0, 1, 0}, n{0, 0, 1};
	bool pinned = false;
	int page = 0;                  // 0 info, 1 controls
	Texture tex;
	Mesh quad;
	std::vector<uint32_t> px;
	float timer = 0.f;
	bool armed = false;
	float cooldown = 0.f;
	std::string lastPress;
	float pressFlash = 0.f;
	float frameMs = 0.f;
	Game *game = nullptr;
	Board *board = nullptr;

	std::vector<PanelButton> buttons() const
	{
		std::vector<PanelButton> b;
		b.push_back({"INFO", "INFO", 2, 2, W / 2 - 3, 20});
		b.push_back({"CONTROLS", "CONTROLS", W / 2 + 1, 2, W / 2 - 3, 20});
		if (page != 1) return b;
		const char *wx[8] = {"AUTO", "CLEAR", "RAIN", "SNOW", "DUST", "SAND", "MIST", "CLOUDS"};
		for (int i = 0; i < 8; ++i) b.push_back({wx[i], wx[i], 4 + (i % 4) * 59, 40 + (i / 4) * 29, 55, 25});
		b.push_back({"DARKER", "DARKER", 4, 118, 76, 25});
		b.push_back({"LIGHTER", "LIGHTER", 82, 118, 76, 25});
		b.push_back({"GAME", "GAME", 160, 118, 76, 25});
		b.push_back({"TURNL", "TURN -15", 4, 170, 76, 25});
		b.push_back({"TURNR", "TURN +15", 82, 170, 76, 25});
		b.push_back({"REAL", "REAL", 160, 170, 76, 25});
		const char *ck[6][2] = {{"HOURM", "-1H"}, {"HOURP", "+1H"}, {"HALFDAY", "+12H"}, {"PHASEM", "-MOON"}, {"PHASEP", "+MOON"}, {"NOW", "GAME"}};
		for (int i = 0; i < 6; ++i) b.push_back({ck[i][0], ck[i][1], 4 + i * 39, 234, 37, 25});
		b.push_back({"STRIKE", "STRIKE LIGHTNING", 4, 286, 115, 25});
		b.push_back({"VANILLA", Options::vrVanillaSky ? "VANILLA: ON" : "VANILLA: OFF", 121, 286, 115, 25});
		return b;
	}

	bool press(const std::string &id)
	{
		if (!board) return false;
		std::string w = id;
		std::transform(w.begin(), w.end(), w.begin(), ::tolower);
		if (id == "INFO") page = 0;
		else if (id == "CONTROLS") page = 1;
		else if (id == "AUTO" || id == "CLEAR" || id == "RAIN" || id == "SNOW" || id == "DUST" || id == "SAND" || id == "MIST" || id == "CLOUDS")
			board->forceWeather(w);
		else if (id == "DARKER") board->setShadeOffset(std::min(15, board->shadeOffset() + 1));
		else if (id == "LIGHTER") board->setShadeOffset(std::max(-15, board->shadeOffset() - 1));
		else if (id == "GAME") board->setShadeOffset(0);
		else if (id == "TURNL") board->rotateSun(-15.f);
		else if (id == "TURNR") board->rotateSun(15.f);
		else if (id == "REAL") board->realSun();
		else if (id == "STRIKE") board->testStrike();
		else if (id == "HOURM") board->shiftSkyClock(-1.0 / 24.0);
		else if (id == "HOURP") board->shiftSkyClock(1.0 / 24.0);
		else if (id == "HALFDAY") board->shiftSkyClock(0.5);
		else if (id == "PHASEM") board->shiftSkyClock(-4.0);   // whole days keep the time of day; four is about an eighth of a moon
		else if (id == "PHASEP") board->shiftSkyClock(4.0);
		else if (id == "NOW") board->shiftSkyClock(0.0, true);
		else if (id == "VANILLA") { Options::vrVanillaSky = !Options::vrVanillaSky; board->skyChanged(); }
		else return false;
		lastPress = id;
		pressFlash = 0.25f;
		timer = 0.f; // redraw now
		Log(LOG_INFO) << "[VR] debug panel: " << id;
		return true;
	}

	void render();
};

void WristDebug::Impl::render()
{
	if (!game || !board) return;
	Dash::Canvas cv(game, px, W, H);
	if (!cv.ok()) return;
	using namespace Dash;
	SkyInfo I = board->skyInfo();
	// tabs
	for (const PanelButton &b : buttons())
	{
		bool on = (b.id == "INFO" && page == 0) || (b.id == "CONTROLS" && page == 1);
		std::string lw = b.id;
		std::transform(lw.begin(), lw.end(), lw.begin(), ::tolower);
		bool weatherOn = lw == I.forced || (b.id == "VANILLA" && Options::vrVanillaSky); // the weather button in force (AUTO when none)
		bool flash = pressFlash > 0.f && lastPress == b.id;
		uint32_t bg = flash ? C(90, 200, 230) : (on || weatherOn) ? C(40, 90, 150) : BAND;
		cv.rect(b.x, b.y, b.w, b.h, bg);
		cv.frame(b.x, b.y, b.w, b.h, (on || weatherOn) ? CYAN : LINE);
		cv.text(b.x, b.y + (b.h - 9) / 2, b.w, b.label, flash ? C(10, 20, 40) : (on || weatherOn) ? GOLD : TEXT, false, 1);
	}
	int y = 28;
	auto head = [&](const std::string &s) { cv.rect(0, y, W, 11, C(14, 24, 52)); cv.text(4, y + 1, W - 8, s, GOLD); y += 13; };
	auto line = [&](const std::string &s, uint32_t col = TEXT) { cv.text(6, y, W - 10, s, col); y += 10; };
	if (!I.active)
	{
		line("No battle on the table.", DIM);
		return;
	}
	if (page == 0)
	{
		head("MISSION");
		line(cv.tr(I.mission));
		line("Terrain: " + I.terrain);
		line("Sets: " + I.sets, DIM);
		if (I.vanilla) line("VANILLA: the pre-0.13 table (no sky)", AMBER);
		else line(I.outdoor ? "Outdoors" : "Indoors (" + I.indoorWhy + "): no sun, no weather", I.outdoor ? TEXT : AMBER);
		if (I.site)
			line(fmt("Site %.1f", std::fabs(I.lat)) + (I.lat >= 0 ? "N " : "S ") + fmt("%.1f", std::fabs(I.lon)) + (I.lon >= 0 ? "E" : "W"));
		else line("Site: unknown", DIM);
		{
			int lh = (int)I.localH, lm = (int)((I.localH - lh) * 60.f);
			line(I.skyClock.substr(0, 10) + fmt("  GMT %02.0f:%02.0f", I.gmtH, I.gmtM) + (I.site ? fmt("  local %02.0f:%02.0f", lh, lm) : std::string()),
				I.skyShift != 0.0 ? AMBER : TEXT);
		}
		y += 3;
		head("LIGHT");
		line(fmt("Darkness %.0f", I.shade) + (I.shade != I.shadeGame ? fmt(" (game %.0f)", I.shadeGame) : std::string()) + " - " + band(I.shade),
			I.shade != I.shadeGame ? AMBER : TEXT);
		line("Darkness from " + I.shadeFrom, DIM);
		if (I.outdoor)
		{
			bool isNight = I.sky.compare(0, 5, "night") == 0;
			line(std::string(isNight ? "Moon" : "Sun") + " from " + compass(I.sunAz) + fmt(" (%.0f), %.0f high", I.sunAz, I.sunElev));
			line(I.realKnown ? fmt("Real sun at the site: %.0f high", I.realElev) + (I.realElev < 0.f ? " (below)" : "") : std::string("Real sun: not known"), DIM);
			line("Direction: " + I.sunFrom, I.sunFrom == "set by hand" ? AMBER : DIM);
			if (I.sky.compare(0, 5, "night") == 0)
			{
				std::string ph = fmt("%.0f%% ", I.moonIllum * 100.f) + (I.moonWaxing ? "waxing" : "waning");
				if (I.moonMirrored) line("Moon " + ph + fmt(", %.0f below: mirrored up", -I.moonAlt), AMBER);
				else line("Moon " + ph + fmt(", %.0f high", I.moonAlt) + (I.moonKnown ? "" : " (rolled)"));
				line(fmt("Moonlight %.0f%% of a full moon's", I.moonLight * 100.f) + (I.moonLight > I.moonIllum + 0.005f ? " (the floor)" : ""), DIM);
			}
			cv.text(6, y, 40, "Sun", TEXT); cv.rect(34, y, 30, 8, toPx(I.sunCol)); cv.frame(34, y, 30, 8, LINE);
			cv.text(80, y, 40, "Sky", TEXT); cv.rect(104, y, 30, 8, toPx(I.skyCol)); cv.frame(104, y, 30, 8, LINE);
			cv.text(146, y, 90, fmt("x%.2f", std::max(I.sunCol.r, std::max(I.sunCol.g, I.sunCol.b))), DIM);
			y += 11;
		}
		y += 3;
		head("WEATHER");
		if (I.outdoor)
		{
			std::string now = I.weather;
			if (I.mist) now += " + mist";
			if (I.cloud > 0.f) now += fmt(", clouds %.0f%%", I.cloud * 100.f);
			line("Now: " + now + (I.forced != "auto" ? "  (FORCED " + I.forced + ")" : ""), I.forced != "auto" ? AMBER : TEXT);
			line(I.rollKind, DIM);
			if (!I.rollStrength.empty()) line("Strength: " + I.rollStrength, DIM);
			line(I.rollClouds, DIM);
			line(I.rollMist, DIM);
			line(fmt("Drawn %.0f   strikes %.0f", I.drawn, I.strikes) + (I.nextStrike >= 0.f ? fmt("   next %.0f s", I.nextStrike) : std::string()));
			char seed[32];
			snprintf(seed, sizeof(seed), "Roll #%08X", I.seed);
			line(seed, DIM);
		}
		else line("none indoors", DIM);
		y += 3;
		head("SETTINGS");
		line(std::string("Weather ") + (Options::vrWeather ? "on" : "OFF") + "   shadows " + (Options::vrShadows ? "on" : "OFF"));
		line(fmt("Frame %.1f ms (%.0f fps)", frameMs, frameMs > 0.f ? 1000.f / frameMs : 0.f));
	}
	else
	{
		cv.text(4, 28, W - 8, "WEATHER  (now: " + (I.forced == "auto" ? std::string("the mission's roll") : I.forced) + ")", GOLD);
		cv.text(4, 104, W - 8, fmt("DARKNESS  table %.0f, game %.0f", I.shade, I.shadeGame), GOLD);
		cv.text(4, 156, W - 8, std::string(I.sky.compare(0, 5, "night") == 0 ? (I.moonMirrored ? "MOON (mirrored)" : "MOON") : "SUN") + "  from " + compass(I.sunAz) + fmt(" (%.0f)", I.sunAz) + " - " + I.sunFrom, GOLD);
		{
			double sh = I.skyShift;
			int d = (int)(sh >= 0 ? std::floor(sh) : -std::floor(-sh)), h = (int)std::lround((sh - d) * 24.0);
			cv.text(4, 208, W - 8, "SKY CLOCK  " + I.skyClock, GOLD);
			cv.text(4, 220, W - 8, sh == 0.0 ? std::string("the game's own clock (sun and moon only)")
				: fmt("game clock %+.0f d %+.0f h (sun and moon only)", d, h), sh == 0.0 ? DIM : AMBER);
		}
		cv.text(4, 272, W - 8, fmt("LIGHTNING  strikes %.0f", I.strikes) + "      SKY", GOLD);
		y = 318;
		line("Table only: the game's darkness, sight and", DIM);
		line("flat screen stay as they are. Forced weather,", DIM);
		line("a turned sun and the clock carry on.", DIM);
		line("VANILLA is the vrVanillaSky setting (saved).", DIM);
		line("+12H swaps day and night; -/+MOON moves the", DIM);
		line("clock 4 days (about 1/8 of the moon's cycle).", DIM);
	}
}

WristDebug::WristDebug() : _p(new Impl()) {}
WristDebug::~WristDebug() = default;

void WristDebug::update(Game *game, Board *board, const Hands &hands, const glm::vec3 &eye,
	const std::function<void(int, float, float)> &haptic, float dt)
{
	Impl &p = *_p;
	p.game = game;
	p.board = board;
	if (dt > 0.f) p.frameMs += (dt * 1000.f - p.frameMs) * 0.05f;
	p.cooldown = std::max(0.f, p.cooldown - dt);
	p.pressFlash = std::max(0.f, p.pressFlash - dt);
	bool battle = board && board->battle() != nullptr;
	bool want = false;
	if (Options::vrDebugPanel && battle)
	{
		if (p.pinned) want = true;
		else
		{
			const HandPose &L = hands.pose[0];
			if (L.valid && !L.ghost)
			{
				// joints (XR_EXT_hand_tracking order): wrist 1, index knuckle 7, middle knuckle 12, little knuckle 22
				glm::vec3 wr = L.pos[1], ik = L.pos[7], mk = L.pos[12], lk = L.pos[22];
				glm::vec3 palm = -glm::cross(ik - wr, lk - wr); // the inside of the left hand
				if (glm::length(palm) > 1e-6f)
				{
					palm = glm::normalize(palm);
					glm::vec3 toEye = eye - wr;
					float dist = glm::length(toEye);
					float facing = dist > 1e-3f ? glm::dot(palm, toEye / dist) : 0.f;
					want = facing > (p.show > 0.5f ? 0.45f : 0.62f) && dist < 0.8f;
					glm::vec3 f = glm::normalize(mk - wr);
					glm::vec3 up = glm::vec3(0, 1, 0) - palm * palm.y;
					if (glm::length(up) < 0.2f) up = -f;
					if (glm::length(up) > 1e-6f)
					{
						up = glm::normalize(up);
						p.n = palm; p.up = up; p.right = glm::normalize(glm::cross(up, palm));
						// along the inside of the forearm, just short of the hand
						p.c = wr + palm * 0.03f - f * (PH * 0.5f + 0.02f);
					}
				}
			}
		}
	}
	float a = 1.f - std::exp(-dt * 10.f);
	p.show += ((want ? 1.f : 0.f) - p.show) * (dt > 0.f ? a : 1.f);
	if (!want && p.show < 0.02f) { p.armed = false; return; }

	p.timer -= dt;
	if (p.timer <= 0.f)
	{
		p.timer = 0.25f;
		p.render();
		if (!p.px.empty())
		{
			if (!p.tex.valid()) p.tex.create(W, H, true, true);
			p.tex.update(p.px.data(), W, H);
		}
	}

	// the right index fingertip presses the panel's tabs and buttons
	const HandPose &R = hands.pose[1];
	if (!p.pinned && R.valid && !R.ghost && p.show > 0.6f)
	{
		glm::vec3 tip = R.tip(F_INDEX);
		glm::vec3 d = tip - p.c;
		float depth = glm::dot(d, p.n);
		glm::vec2 uv(glm::dot(d, p.right) / PW, glm::dot(d, p.up) / PH);
		bool inside = std::fabs(uv.x) < 0.5f && std::fabs(uv.y) < 0.5f;
		bool touching = inside && depth < 0.006f && depth > -0.03f;
		if (touching && p.armed && p.cooldown <= 0.f)
		{
			int gx = (int)((uv.x + 0.5f) * W), gy = (int)((0.5f - uv.y) * H);
			for (const PanelButton &b : p.buttons())
				if (gx >= b.x && gx < b.x + b.w && gy >= b.y && gy < b.y + b.h)
				{
					if (p.press(b.id))
					{
						if (haptic) { haptic(1, 0.45f, 0.02f); haptic(0, 0.25f, 0.02f); }
						p.cooldown = 0.3f;
					}
					break;
				}
		}
		p.armed = inside && depth > 0.006f && depth < 0.06f ? true : (touching ? false : p.armed && inside);
	}
}

void WristDebug::draw(const Shader &sh) const
{
	const Impl &p = *_p;
	if (p.show < 0.02f || !p.tex.valid()) return;
	if (p.quad.empty())
	{
		MeshData md;
		md.addQuad({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}, glm::vec4(1), 0.f, {0, 1}, {1, 1}, {1, 0}, {0, 0});
		const_cast<Mesh&>(p.quad).upload(md);
	}
	glm::mat4 m(1.f);
	m[0] = glm::vec4(p.right, 0.f);
	m[1] = glm::vec4(p.up, 0.f);
	m[2] = glm::vec4(p.n, 0.f);
	m[3] = glm::vec4(p.c, 1.f);
	float k = 0.6f + 0.4f * p.show;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
	sh.set("uUVRect", glm::vec4(0.f, 0.f, 1.f, 1.f));
	sh.set("uMode", 2);
	sh.set("uTint", glm::vec4(1.2f, 0.75f, 0.25f, 0.8f * p.show)); // an amber edge: a debug tool, not part of the game
	sh.set("uModel", m * glm::translate(glm::mat4(1.f), {0.f, 0.f, -0.001f}) * glm::scale(glm::mat4(1.f), {PW * k * 1.04f, PH * k * 1.03f, 1.f}));
	p.quad.draw();
	sh.set("uMode", 4);
	sh.set("uTint", glm::vec4(1.15f, 1.15f, 1.15f, p.show));
	sh.set("uModel", m * glm::scale(glm::mat4(1.f), {PW * k, PH * k, 1.f}));
	p.tex.bind(0);
	p.quad.draw();
	glDisable(GL_BLEND);
	sh.set("uTint", glm::vec4(1.f));
}

std::string WristDebug::status() const
{
	const Impl &p = *_p;
	char b[96];
	snprintf(b, sizeof(b), "dbg=%s page=%d last=%s", p.show > 0.5f ? "shown" : "hidden", p.page, p.lastPress.empty() ? "-" : p.lastPress.c_str());
	return b;
}

void WristDebug::testPin(bool pin, const glm::vec3 &center, const glm::vec3 &normal)
{
	Impl &p = *_p;
	p.pinned = pin;
	if (!pin) return;
	p.n = glm::normalize(normal);
	glm::vec3 up = glm::vec3(0, 1, 0) - p.n * p.n.y;
	p.up = glm::length(up) > 1e-3f ? glm::normalize(up) : glm::vec3(0, 0, -1);
	p.right = glm::normalize(glm::cross(p.up, p.n));
	p.c = center;
	p.timer = 0.f;
}

void WristDebug::testPage(int page) { _p->page = page; _p->timer = 0.f; }

bool WristDebug::testPress(const std::string &id) { return _p->press(id); }

}
}
