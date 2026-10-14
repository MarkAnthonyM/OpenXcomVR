#include "VrGL.h" // first: GL extension headers
#include "VrFloat.h"
#include "VrRoom.h"
#include "../Engine/Game.h"
#include "../Engine/State.h"
#include "../Engine/Surface.h"
#include "../Engine/Logger.h"
#include "../Interface/Window.h"
#include "../Battlescape/ActionMenuState.h"
#include "../Battlescape/BriefingState.h"
#include "../Battlescape/DebriefingState.h"
#include "../Battlescape/NextTurnState.h"
#include "../Battlescape/PromotionsState.h"
#include "../Battlescape/CommendationState.h"
#include "../Battlescape/CommendationLateState.h"
#include "../Battlescape/InfoboxState.h"
#include "../Battlescape/MiniMapState.h"
#include "../Geoscape/BuildNewBaseState.h"
#include "../Geoscape/SelectDestinationState.h"
#include "../Geoscape/MonthlyReportState.h"
#include "../Geoscape/GeoscapeState.h"
#include "../Geoscape/DogfightState.h"
#include "../Menu/LoadGameState.h"
#include "../Menu/SaveGameState.h"
#include <glm/gtc/matrix_transform.hpp>
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
const float T_LINE = 0.14f;   // opening: the line grows from the middle
const float T_UNFOLD = 0.42f; // then unfolds into the window
const float T_FOLD = 0.24f;   // closing: back into the line
const float T_SHRINK = 0.10f; // and the line shrinks away
const float LINE_H = 0.0025f;

float smooth01(float e0, float e1, float x)
{
	float t = std::min(1.f, std::max(0.f, (x - e0) / (e1 - e0)));
	return t * t * (3.f - 2.f * t);
}

/// Scale2x (EPX): doubles a pixel-art picture, rounding off staircase diagonals without blurring.
void scale2x(const std::vector<uint32_t> &in, int w, int h, std::vector<uint32_t> &out)
{
	out.resize((size_t)w * h * 4);
	const int W2 = w * 2;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			uint32_t P = in[(size_t)y * w + x];
			uint32_t A = y > 0 ? in[(size_t)(y - 1) * w + x] : P;
			uint32_t B = x < w - 1 ? in[(size_t)y * w + x + 1] : P;
			uint32_t C = x > 0 ? in[(size_t)y * w + x - 1] : P;
			uint32_t D = y < h - 1 ? in[(size_t)(y + 1) * w + x] : P;
			uint32_t e0 = P, e1 = P, e2 = P, e3 = P;
			if (C == A && C != D && A != B) e0 = A;
			if (A == B && A != C && B != D) e1 = B;
			if (D == C && D != B && C != A) e2 = C;
			if (B == D && B != A && D != C) e3 = D;
			size_t o = (size_t)(y * 2) * W2 + x * 2;
			out[o] = e0; out[o + 1] = e1; out[o + W2] = e2; out[o + W2 + 1] = e3;
		}
}
}

struct Spark
{
	glm::vec3 p, v;
	float life, age, size;
};

struct Win
{
	enum Phase { OPENING, OPEN, CLOSING, CLOSED } phase = OPENING;
	enum Kind { POPUP, BAR, DOGFIGHT } kind = POPUP;
	float t = 0.f;              // time in the current phase
	float shown = 0.f;          // time since it opened
	float minShow = 0.f;        // keep showing at least this long, even if the game closed it (info boxes)
	State *src = nullptr;       // only compared; dereferenced while the game still lists it
	bool alive = false;         // src is still up this frame
	glm::ivec4 rect{0};         // area on the game screen (x, y, w, h)
	Texture tex;
	std::vector<uint32_t> px, px2, px4;
	glm::vec3 O{0.f}, right{1, 0, 0}, up{0, 1, 0}, n{0, 0, 1}; // bottom centre and axes
	float W = 0.f, H = 0.f;
	float lean = -1.f;          // radians back from vertical, fixed when it opens (faces the eyes)
	bool armed[2] = {true, true};
	float cool[2] = {0.f, 0.f};
	std::string name;

	void shape(float &fold, float &width) const
	{
		switch (phase)
		{
		case OPENING: width = smooth01(0.f, T_LINE, t); fold = smooth01(T_LINE, T_LINE + T_UNFOLD, t); break;
		case OPEN: width = 1.f; fold = 1.f; break;
		case CLOSING: fold = 1.f - smooth01(0.f, T_FOLD, t); width = 1.f - smooth01(T_FOLD, T_FOLD + T_SHRINK, t); break;
		default: width = 0.f; fold = 0.f; break;
		}
	}
	glm::vec3 at(float u, float v, float lift = 0.f) const { return O + right * u + up * v + n * lift; }
	glm::mat4 frame(glm::vec2 c, glm::vec2 half, float lift) const
	{
		glm::mat4 m(1.f);
		m[0] = glm::vec4(right * (half.x * 2.f), 0.f);
		m[1] = glm::vec4(up * (half.y * 2.f), 0.f);
		m[2] = glm::vec4(n, 0.f);
		m[3] = glm::vec4(at(c.x, c.y, lift), 1.f);
		return m;
	}
	bool interactive() const { return phase == OPEN && alive && W > 0.f; }
	/// Window plane coordinates of a world point: u across, v up from the bottom, d in front.
	glm::vec3 local(const glm::vec3 &p) const { glm::vec3 d = p - O; return {glm::dot(d, right), glm::dot(d, up), glm::dot(d, n)}; }
	glm::ivec2 pixelAt(float u, float v) const
	{
		return {(int)((u / W + 0.5f) * rect.z) + rect.x, (int)((1.f - v / H) * rect.w) + rect.y};
	}
};

struct FloatWindow::Impl
{
	std::vector<std::unique_ptr<Win>> wins;
	std::vector<Spark> sparks;
	uint32_t seed = 12345u;
	double time = 0.0;
	int surfW = 320, surfH = 200;
	bool sharp = true;
	Mesh quad, box;
	glm::ivec2 lastHover{-1, -1};
	std::string lastEvent;

	float rnd() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.f; }
	float rnd(float a, float b) { return a + (b - a) * rnd(); }
	void emit(const Win &w, int count, float vOff, float dirV)
	{
		// sparks leave an edge of the band (vOff from the middle), flying away from it
		for (int i = 0; i < count && sparks.size() < 600; ++i)
		{
			Spark s;
			float u = rnd(-0.5f, 0.5f) * w.W;
			s.p = w.at(u, w.H * 0.5f + vOff, rnd(0.f, 0.004f));
			s.v = w.up * (dirV * rnd(0.05f, 0.35f)) + w.right * (rnd(-0.12f, 0.12f) + (u / std::max(w.W, 0.01f)) * 0.2f) + w.n * rnd(0.02f, 0.18f);
			s.life = rnd(0.35f, 0.75f);
			s.age = 0.f;
			s.size = rnd(0.0028f, 0.006f);
			sparks.push_back(s);
		}
	}
	void emitEnds(const Win &w, int count)
	{
		for (int i = 0; i < count && sparks.size() < 600; ++i)
		{
			Spark s;
			float side = rnd() < 0.5f ? -1.f : 1.f;
			s.p = w.at(side * w.W * 0.5f * rnd(0.85f, 1.f), w.H * 0.5f, 0.002f);
			s.v = w.right * (side * rnd(0.08f, 0.3f)) + w.up * rnd(-0.1f, 0.1f) + w.n * rnd(0.02f, 0.12f);
			s.life = rnd(0.3f, 0.6f);
			s.age = 0.f;
			s.size = rnd(0.0028f, 0.005f);
			sparks.push_back(s);
		}
	}
	void place(Win &w, const FloatContext &ctx, int index, int count);
	void copyPicture(Win &w, const FloatContext &ctx);
};

FloatWindow::FloatWindow() : _p(new Impl) {}
FloatWindow::~FloatWindow() {}

void FloatWindow::init()
{
	MeshData q;
	q.addQuad({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}, glm::vec4(1), MAT_PLAIN, {0, 1}, {1, 1}, {1, 0}, {0, 0});
	_p->quad.upload(q);
	MeshData b;
	b.addBox({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, glm::vec4(1), MAT_PLAIN);
	_p->box.upload(b);
}

// ------------------------------------------------------------------ which windows

/// Full screens that are really messages: they float too.
static bool messageScreen(State *s)
{
	return dynamic_cast<BriefingState*>(s) || dynamic_cast<DebriefingState*>(s) || dynamic_cast<NextTurnState*>(s)
		|| dynamic_cast<PromotionsState*>(s) || dynamic_cast<CommendationState*>(s) || dynamic_cast<CommendationLateState*>(s)
		|| dynamic_cast<MonthlyReportState*>(s) || dynamic_cast<MiniMapState*>(s);
}

struct Cand { State *s; Win::Kind kind; };

static std::vector<Cand> candidates(Game *game)
{
	std::vector<Cand> out;
	if (!game || game->getStates().empty()) return out;
	State *top = game->getStates().back();
	if (!top) return out;
	if (GeoscapeState *geo = dynamic_cast<GeoscapeState*>(top))
	{
		// dogfights are drawn by the geoscape itself; each open one floats
		for (DogfightState *d : geo->vrDogfights())
			if (d && !d->isMinimized()) out.push_back({d, Win::DOGFIGHT});
		return out;
	}
	if (dynamic_cast<BuildNewBaseState*>(top) || dynamic_cast<SelectDestinationState*>(top))
	{
		out.push_back({top, Win::BAR}); // a bar beyond the globe, which stays free for picking
		return out;
	}
	if (dynamic_cast<ActionMenuState*>(top)) return out;                               // keys on the console
	if (dynamic_cast<LoadGameState*>(top) || dynamic_cast<SaveGameState*>(top)) return out; // "loading..." flashes
	if (!top->isScreen() || messageScreen(top)) out.push_back({top, Win::POPUP});
	return out;
}

static glm::ivec4 areaOf(State *s, int W, int H)
{
	int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
	bool dogfight = dynamic_cast<DogfightState*>(s) != nullptr;
	// picking on the globe: only the bar with the title and Cancel (the globe's own rotate and
	// zoom buttons in the corner are the dome's job)
	bool bar = dynamic_cast<BuildNewBaseState*>(s) || dynamic_cast<SelectDestinationState*>(s);
	for (Surface *sf : s->vrSurfaces())
	{
		if (bar && !dynamic_cast<Window*>(sf)) continue;
		if (!sf || !sf->getVisible() || sf->getWidth() <= 0 || sf->getHeight() <= 0) continue;
		if (dogfight && x1 > x0) break; // a dogfight is its first surface, the window (a label sticks out)
		x0 = std::min(x0, sf->getX()); y0 = std::min(y0, sf->getY());
		x1 = std::max(x1, sf->getX() + sf->getWidth()); y1 = std::max(y1, sf->getY() + sf->getHeight());
	}
	x0 = std::max(0, x0); y0 = std::max(0, y0);
	x1 = std::min(W, x1); y1 = std::min(H, y1);
	if (x1 - x0 < 8 || y1 - y0 < 8) return glm::ivec4(0);
	return {x0, y0, x1 - x0, y1 - y0};
}

/// The game grows some windows open; the projection takes over once they're done.
static bool ready(State *s)
{
	for (Surface *sf : s->vrSurfaces())
		if (Window *w = dynamic_cast<Window*>(sf))
			if (!w->vrPopupDone()) return false;
	return true;
}

// ------------------------------------------------------------------ layout

void FloatWindow::Impl::place(Win &w, const FloatContext &ctx, int index, int count)
{
	const float cx = (ctx.table.x + ctx.table.z) * 0.5f;
	float K, maxW;
	glm::vec3 O;
	switch (w.kind)
	{
	case Win::BAR:
		// a long bar standing beyond the globe, high enough to be seen over it
		K = std::min(0.003f, std::min(0.9f / std::max(1, w.rect.z), 0.12f / std::max(1, w.rect.w)));
		O = glm::vec3(cx, ctx.tableY + 0.20f, ctx.table.y + 0.25f);
		break;
	case Win::DOGFIGHT:
		maxW = std::min(0.62f, 1.3f / count - 0.03f);
		K = std::min(0.0026f, std::min(maxW / std::max(1, w.rect.z), 0.34f / std::max(1, w.rect.w)));
		O = glm::vec3(cx + (index - (count - 1) * 0.5f) * (maxW + 0.03f), ctx.tableY + 0.04f, ctx.table.w - 0.24f);
		break;
	default:
		// big enough that a game pixel covers a few headset pixels (sharper text), within reach
		K = std::min(0.0026f, std::min(0.66f / std::max(1, w.rect.z), (ctx.keyboard ? 0.30f : 0.42f) / std::max(1, w.rect.w)));
		O = glm::vec3(cx, ctx.tableY + (ctx.keyboard ? 0.11f : 0.04f), ctx.table.w - (ctx.keyboard ? 0.26f : 0.24f));
		break;
	}
	float W = w.rect.z * K, H = w.rect.w * K;
	if (w.lean < 0.f)
	{
		// lean back so the window faces the eyes (decided once, so it doesn't wobble with the head)
		float lean = glm::radians(18.f);
		for (int i = 0; i < 3; ++i)
		{
			glm::vec3 C = O + glm::vec3(0.f, std::cos(lean), -std::sin(lean)) * (H * 0.5f);
			glm::vec3 d = ctx.eye - C;
			float horiz = std::max(0.05f, d.z);
			lean = std::atan2(d.y, horiz); // the normal points at the eye: lean = elevation of the eye
		}
		w.lean = glm::clamp(lean, glm::radians(5.f), glm::radians(50.f));
	}
	// ease into place (the keyboard rising, another dogfight joining)
	float k = w.W <= 0.f ? 1.f : 0.15f;
	w.O = w.W <= 0.f ? O : glm::mix(w.O, O, k);
	w.W = w.W <= 0.f ? W : glm::mix(w.W, W, k);
	w.H = w.H <= 0.f ? H : glm::mix(w.H, H, k);
	w.right = glm::vec3(1, 0, 0);
	w.up = glm::vec3(0, std::cos(w.lean), -std::sin(w.lean));
	w.n = glm::vec3(0, std::sin(w.lean), std::cos(w.lean));
}

void FloatWindow::Impl::copyPicture(Win &w, const FloatContext &ctx)
{
	const glm::ivec4 &R = w.rect;
	w.px.resize((size_t)R.z * R.w);
	for (int y = 0; y < R.w; ++y)
		std::copy_n(&(*ctx.pixels)[(size_t)(R.y + y) * ctx.surfW + R.x], R.z, &w.px[(size_t)y * R.z]);
	const std::vector<uint32_t> *src = &w.px;
	int tw = R.z, th = R.w;
	if (sharp)
	{
		// 4x pixel-art upscale: smoother letters and diagonals at the headset's resolution
		scale2x(w.px, R.z, R.w, w.px2);
		scale2x(w.px2, R.z * 2, R.w * 2, w.px4);
		src = &w.px4;
		tw *= 4; th *= 4;
	}
	if (!w.tex.valid() || w.tex.width() != tw || w.tex.height() != th) w.tex.create(tw, th, false, true);
	w.tex.update(src->data(), tw, th);
}

// ------------------------------------------------------------------ per frame

void FloatWindow::update(const FloatContext &ctx, const Hands &hands, float dt)
{
	Impl &p = *_p;
	p.time += dt;
	p.surfW = ctx.surfW;
	p.surfH = ctx.surfH;
	p.sharp = ctx.sharp;
	std::vector<Cand> cands = candidates(ctx.game);
	bool havePixels = ctx.pixels && !ctx.pixels->empty() && ctx.surfW > 0;

	// windows whose pop-up went away fold up (info boxes stay readable for a moment first)
	for (auto &wp : p.wins)
	{
		Win &w = *wp;
		w.t += dt;
		w.shown += dt;
		w.alive = false;
		if (w.src)
			for (const Cand &c : cands) if (c.s == w.src) w.alive = true;
		if (!w.alive) w.src = nullptr;
		if (w.phase == Win::OPENING && w.t >= T_LINE + T_UNFOLD) { w.phase = Win::OPEN; w.t = 0.f; }
		if ((w.phase == Win::OPENING || w.phase == Win::OPEN) && !w.alive && (w.phase == Win::OPEN && w.shown >= w.minShow))
		{
			w.phase = Win::CLOSING;
			w.t = 0.f;
			p.lastEvent = "close " + w.name;
		}
		if (w.phase == Win::CLOSING && w.t >= T_FOLD + T_SHRINK) w.phase = Win::CLOSED;
	}
	p.wins.erase(std::remove_if(p.wins.begin(), p.wins.end(), [](const std::unique_ptr<Win> &w) { return w->phase == Win::CLOSED; }), p.wins.end());

	// new pop-ups unfold (a pop-up waits for the one before it to finish folding away)
	bool popupFolding = false;
	for (auto &wp : p.wins) if (wp->kind != Win::DOGFIGHT && !wp->alive) popupFolding = true;
	for (const Cand &c : cands)
	{
		bool have = false;
		for (auto &wp : p.wins) if (wp->src == c.s) have = true;
		if (have || !havePixels || !ready(c.s)) continue;
		if (c.kind != Win::DOGFIGHT && popupFolding) continue;
		glm::ivec4 r = areaOf(c.s, ctx.surfW, ctx.surfH);
		if (r.z <= 0) continue;
		std::unique_ptr<Win> w(new Win);
		w->src = c.s;
		w->alive = true;
		w->kind = c.kind;
		w->rect = r;
		w->name = typeid(*c.s).name();
		if (dynamic_cast<InfoboxState*>(c.s)) w->minShow = 2.5f;
		p.lastEvent = "open " + w->name;
		Log(LOG_INFO) << "[VR] float: open " << w->name << " " << r.x << "," << r.y << " " << r.z << "x" << r.w;
		p.wins.push_back(std::move(w));
	}

	// live pictures and places
	int dogCount = 0, dogIndex = 0;
	for (auto &wp : p.wins) if (wp->kind == Win::DOGFIGHT && wp->alive) ++dogCount;
	for (auto &wp : p.wins)
	{
		Win &w = *wp;
		if (w.alive && havePixels)
		{
			glm::ivec4 r = areaOf(w.src, ctx.surfW, ctx.surfH);
			if (r.z > 0) w.rect = r;
			p.copyPicture(w, ctx);
		}
		if (w.alive || w.W <= 0.f)
		{
			int idx = 0, cnt = 1;
			if (w.kind == Win::DOGFIGHT) { idx = dogIndex++; cnt = std::max(1, dogCount); }
			p.place(w, ctx, idx, cnt);
		}
	}

	// sparks off the moving edges
	for (auto &wp : p.wins)
	{
		const Win &w = *wp;
		float fold = 0.f, width = 0.f;
		w.shape(fold, width);
		if (w.phase == Win::OPENING)
		{
			if (w.t < T_LINE) p.emitEnds(w, 4);
			else { p.emit(w, 6, w.H * 0.5f * fold, 1.f); p.emit(w, 6, -w.H * 0.5f * fold, -1.f); }
		}
		else if (w.phase == Win::CLOSING)
		{
			if (w.t < T_FOLD) { p.emit(w, 3, w.H * 0.5f * fold, 1.f); p.emit(w, 3, -w.H * 0.5f * fold, -1.f); }
			else p.emitEnds(w, 4);
		}
	}
	for (Spark &s : p.sparks)
	{
		s.age += dt;
		s.v.y -= 0.35f * dt;
		s.v *= std::max(0.f, 1.f - 1.5f * dt);
		s.p += s.v * dt;
	}
	p.sparks.erase(std::remove_if(p.sparks.begin(), p.sparks.end(), [](const Spark &s) { return s.age >= s.life; }), p.sparks.end());

	// fingertips: hovering in front moves the game's mouse there, touching the glass clicks
	int hoverHand = -1;
	float hoverDepth = 1e9f;
	glm::ivec2 hoverPx(-1);
	for (auto &wp : p.wins)
	{
		Win &w = *wp;
		for (int h = 0; h < 2; ++h) w.cool[h] = std::max(0.f, w.cool[h] - dt);
		if (!w.interactive()) { w.armed[0] = w.armed[1] = true; continue; }
		for (int h = 0; h < 2; ++h)
		{
			const HandPose &P = hands.pose[h];
			if (!P.valid || P.ghost) { w.armed[h] = true; continue; }
			float r = P.radius[fingerTip(F_INDEX)];
			glm::vec3 l = w.local(P.raw[fingerTip(F_INDEX)]);
			float depth = l.z - r;
			bool inside = std::fabs(l.x) <= w.W * 0.5f && l.y >= 0.f && l.y <= w.H;
			glm::ivec2 g = w.pixelAt(l.x, l.y);
			if (!inside || depth > 0.012f) w.armed[h] = true;
			if (!inside || depth < -0.05f) continue;
			if (depth < 0.04f && depth < hoverDepth) { hoverDepth = depth; hoverHand = h; hoverPx = g; }
			if (depth <= 0.003f && w.armed[h] && w.cool[h] <= 0.f)
			{
				w.armed[h] = false;
				w.cool[h] = 0.25f;
				if (ctx.clickScreen) ctx.clickScreen(g.x, g.y, 1);
				if (ctx.haptic) ctx.haptic(h, 0.6f, 0.02f);
				std::ostringstream ss;
				ss << "touch " << g.x << "," << g.y;
				p.lastEvent = ss.str();
				Log(LOG_INFO) << "[VR] float: " << p.lastEvent;
			}
		}
	}
	if (hoverHand >= 0 && hoverPx != p.lastHover && ctx.hover) ctx.hover(hoverPx.x, hoverPx.y);
	p.lastHover = hoverHand >= 0 ? hoverPx : glm::ivec2(-1);
}

// ------------------------------------------------------------------ drawing

void FloatWindow::draw(const Shader &sh) const
{
	const Impl &p = *_p;
	if (p.wins.empty() && p.sparks.empty()) return;
	glEnable(GL_BLEND);
	for (const auto &wp : p.wins)
	{
		const Win &w = *wp;
		float fold = 0.f, width = 0.f;
		w.shape(fold, width);
		glm::vec2 mid(0.f, w.H * 0.5f);
		float bandH = std::max(LINE_H, w.H * fold);
		float bandW = w.W * width;
		if (bandW <= 0.001f) continue;
		// flicker while it materializes, a faint shimmer once it's steady
		float settle = w.phase == Win::OPEN ? 1.f : fold;
		float flick = 1.f - (1.f - settle) * (0.35f + 0.35f * std::sin((float)p.time * 71.f) * std::sin((float)p.time * 23.f));
		flick *= 0.98f + 0.02f * std::sin((float)p.time * 9.f);

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		// the backing covers whatever stands on the table there (always on top of it, and writes
		// depth so hands in front of the window still show)
		glDepthFunc(GL_ALWAYS);
		sh.set("uMode", 2);
		sh.set("uTint", glm::vec4(0.15f, 0.9f, 1.1f, 0.55f * flick));
		sh.set("uModel", w.frame(mid, glm::vec2(bandW * 0.5f + 0.004f, bandH * 0.5f + 0.004f), -0.0015f));
		p.quad.draw();
		sh.set("uTint", glm::vec4(0.01f, 0.02f, 0.03f, 0.96f));
		sh.set("uModel", w.frame(mid, glm::vec2(bandW * 0.5f, bandH * 0.5f), -0.001f));
		p.quad.draw();
		glDepthFunc(GL_LEQUAL);
		// the picture: the middle band of the pop-up, as wide as the line and as tall as the fold
		if (fold > 0.01f && w.tex.valid())
		{
			sh.set("uMode", 1);
			sh.set("uScan", 0.f); // no screen scanlines: they soften small text
			sh.set("uTint", glm::vec4(flick, flick, flick, 1.f));
			sh.set("uTexSize", glm::vec2((float)w.tex.width(), (float)w.tex.height()));
			sh.set("uUVRect", glm::vec4(0.5f - 0.5f * width, 0.5f - 0.5f * fold, width, fold));
			sh.set("uModel", w.frame(mid, glm::vec2(bandW * 0.5f, w.H * fold * 0.5f), 0.f));
			w.tex.bind(0);
			p.quad.draw();
			sh.set("uUVRect", glm::vec4(0.f, 0.f, 1.f, 1.f));
			sh.set("uScan", 1.f);
		}
		// scanlines rolling down the picture while it materializes, then one slow sweep now and then
		sh.set("uMode", 2);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		if (w.phase != Win::OPEN && fold > 0.02f)
			for (int i = 0; i < 7; ++i)
			{
				float f = std::fmod((float)p.time * 1.7f + i / 7.f, 1.f);
				float v = (0.5f - f) * bandH;
				sh.set("uTint", glm::vec4(0.3f, 0.9f, 1.f, 0.18f * (1.f - settle * 0.6f)));
				sh.set("uModel", w.frame(mid + glm::vec2(0.f, v), glm::vec2(bandW * 0.5f, 0.0015f), 0.0006f));
				p.quad.draw();
			}
		else if (w.phase == Win::OPEN)
		{
			float f = std::fmod((float)p.time / 3.5f, 1.f);
			if (f < 0.4f)
			{
				float v = (0.5f - f / 0.4f) * w.H;
				sh.set("uTint", glm::vec4(0.3f, 0.9f, 1.f, 0.05f));
				sh.set("uModel", w.frame(mid + glm::vec2(0.f, v), glm::vec2(w.W * 0.5f, 0.004f), 0.0006f));
				p.quad.draw();
			}
		}
		// the projection line: a bright bar while it's a line, then the two edges of the band
		float glowA = w.phase == Win::OPEN ? 0.35f : 1.f;
		sh.set("uTint", glm::vec4(0.7f, 1.6f, 1.9f, glowA * flick));
		if (fold < 0.02f)
		{
			sh.set("uModel", w.frame(mid, glm::vec2(bandW * 0.5f, LINE_H), 0.001f));
			p.quad.draw();
		}
		else
			for (float s : {-1.f, 1.f})
			{
				sh.set("uModel", w.frame(mid + glm::vec2(0.f, s * bandH * 0.5f), glm::vec2(bandW * 0.5f + 0.003f, LINE_H * 0.6f), 0.001f));
				p.quad.draw();
			}
	}
	// voxel sparks
	if (!p.sparks.empty())
	{
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glDepthMask(GL_FALSE);
		sh.set("uMode", 2);
		for (const Spark &s : p.sparks)
		{
			float k = 1.f - s.age / s.life;
			sh.set("uTint", glm::vec4(0.5f + 0.5f * k, 1.2f, 1.4f, k));
			sh.set("uModel", glm::translate(glm::mat4(1.f), s.p) * glm::scale(glm::mat4(1.f), glm::vec3(s.size * (0.5f + 0.5f * k))));
			p.box.draw();
		}
		glDepthMask(GL_TRUE);
	}
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	sh.set("uTint", glm::vec4(1.f));
}

// ------------------------------------------------------------------ queries

bool FloatWindow::raycast(const glm::vec3 &o, const glm::vec3 &d, float &t, glm::vec2 &gameUV) const
{
	const Impl &p = *_p;
	bool any = false;
	t = 1e9f;
	for (const auto &wp : p.wins)
	{
		const Win &w = *wp;
		if (!w.interactive()) continue;
		float dn = glm::dot(d, w.n);
		if (std::fabs(dn) < 1e-6f) continue;
		float tt = glm::dot(w.O - o, w.n) / dn;
		if (tt <= 0.f || tt >= t) continue;
		glm::vec3 l = w.local(o + d * tt);
		if (std::fabs(l.x) > w.W * 0.5f || l.y < 0.f || l.y > w.H) continue;
		// window -> game pixel -> uv over the whole game frame (what the wall screen's laser uses)
		float gx = w.rect.x + (l.x / w.W + 0.5f) * w.rect.z;
		float gy = w.rect.y + (1.f - l.y / w.H) * w.rect.w;
		gameUV = glm::vec2(gx / std::max(1, p.surfW), gy / std::max(1, p.surfH));
		t = tt;
		any = true;
	}
	return any;
}

bool FloatWindow::fingerNear(const glm::vec3 &pt) const
{
	for (const auto &wp : _p->wins)
	{
		const Win &w = *wp;
		if (!w.interactive()) continue;
		glm::vec3 l = w.local(pt);
		if (std::fabs(l.x) <= w.W * 0.5f + 0.05f && l.y >= -0.05f && l.y <= w.H + 0.05f && l.z > -0.06f && l.z < 0.15f) return true;
	}
	return false;
}

bool FloatWindow::isOpen() const
{
	for (const auto &wp : _p->wins) if (wp->interactive()) return true;
	return false;
}

bool FloatWindow::isClosed() const { return _p->wins.empty(); }

bool FloatWindow::pointAt(float gx, float gy, glm::vec3 &world, glm::vec3 &normal) const
{
	for (const auto &wp : _p->wins)
	{
		const Win &w = *wp;
		if (!w.interactive() || w.rect.z <= 0) continue;
		if (gx < w.rect.x || gy < w.rect.y || gx >= w.rect.x + w.rect.z || gy >= w.rect.y + w.rect.w) continue;
		float u = ((gx - w.rect.x + 0.5f) / w.rect.z - 0.5f) * w.W;
		float v = (1.f - (gy - w.rect.y + 0.5f) / w.rect.w) * w.H;
		world = w.at(u, v);
		normal = w.n;
		return true;
	}
	return false;
}

std::string FloatWindow::status() const
{
	const Impl &p = *_p;
	static const char *phases[] = {"opening", "open", "closing", "closed"};
	std::ostringstream ss;
	ss << "floats=" << p.wins.size();
	for (const auto &wp : p.wins)
	{
		const Win &w = *wp;
		ss << " [" << phases[w.phase] << " " << w.name << " " << w.rect.x << "," << w.rect.y << " " << w.rect.z << "x" << w.rect.w
			<< " " << w.W << "x" << w.H << " lean " << glm::degrees(w.lean) << "]";
	}
	ss << " sparks=" << p.sparks.size() << " last='" << p.lastEvent << "'";
	return ss.str();
}

}
}
