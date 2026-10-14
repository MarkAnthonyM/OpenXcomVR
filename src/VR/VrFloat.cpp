#include "VrGL.h" // first: GL extension headers
#include "VrFloat.h"
#include "VrRoom.h"
#include "../Engine/Game.h"
#include "../Engine/State.h"
#include "../Engine/Surface.h"
#include "../Engine/Logger.h"
#include "../Interface/Window.h"
#include "../Battlescape/ActionMenuState.h"
#include "../Geoscape/BuildNewBaseState.h"
#include "../Geoscape/SelectDestinationState.h"
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
}

struct Spark
{
	glm::vec3 p, v;
	float life, age, size;
};

struct FloatWindow::Impl
{
	enum Phase { CLOSED, OPENING, OPEN, CLOSING } phase = CLOSED;
	float t = 0.f;              // time in the current phase
	State *src = nullptr;       // the pop-up shown (only compared, dereferenced while it's the top state)
	glm::ivec4 rect{0};         // its area on the game screen (x, y, w, h)
	int surfW = 320, surfH = 200;
	Texture tex;
	std::vector<uint32_t> px;
	glm::vec3 O{0.f}, right{1, 0, 0}, up{0, 1, 0}, n{0, 0, 1}; // window bottom centre and axes
	float W = 0.f, H = 0.f;     // window size (m)
	float yLift = 0.f;          // eases up when the keyboard rises
	std::vector<Spark> sparks;
	uint32_t seed = 12345u;
	double time = 0.0;
	Mesh quad, box;
	// fingertips
	bool armed[2] = {true, true};
	float cool[2] = {0.f, 0.f};
	glm::ivec2 lastHover{-1, -1};
	std::string lastEvent;

	float rnd() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.f; }
	float rnd(float a, float b) { return a + (b - a) * rnd(); }
	/// How much of the window shows (0 = the line, 1 = open) and how wide the line is.
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
	void emit(int count, float vOff, float dirV)
	{
		// sparks leave an edge of the band (vOff from the middle), flying away from it
		for (int i = 0; i < count && sparks.size() < 400; ++i)
		{
			Spark s;
			float u = rnd(-0.5f, 0.5f) * W;
			s.p = at(u, H * 0.5f + vOff, rnd(0.f, 0.004f));
			s.v = up * (dirV * rnd(0.05f, 0.35f)) + right * (rnd(-0.12f, 0.12f) + (u / std::max(W, 0.01f)) * 0.2f) + n * rnd(0.02f, 0.18f);
			s.life = rnd(0.35f, 0.75f);
			s.age = 0.f;
			s.size = rnd(0.0028f, 0.006f);
			sparks.push_back(s);
		}
	}
	void emitEnds(int count, float vOff)
	{
		for (int i = 0; i < count && sparks.size() < 400; ++i)
		{
			Spark s;
			float side = rnd() < 0.5f ? -1.f : 1.f;
			s.p = at(side * W * 0.5f * rnd(0.85f, 1.f), H * 0.5f + vOff, 0.002f);
			s.v = right * (side * rnd(0.08f, 0.3f)) + up * rnd(-0.1f, 0.1f) + n * rnd(0.02f, 0.12f);
			s.life = rnd(0.3f, 0.6f);
			s.age = 0.f;
			s.size = rnd(0.002f, 0.004f);
			sparks.push_back(s);
		}
	}
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

/// The pop-up to show: the top state when it isn't a full screen, except the few pop-ups that
/// have their own place on the table or need the globe under them.
static State *popupOf(Game *game)
{
	if (!game || game->getStates().empty()) return nullptr;
	State *top = game->getStates().back();
	if (!top || top->isScreen()) return nullptr;
	if (dynamic_cast<ActionMenuState*>(top)) return nullptr;      // keys on the console
	if (dynamic_cast<BuildNewBaseState*>(top)) return nullptr;    // pick a site on the globe
	if (dynamic_cast<SelectDestinationState*>(top)) return nullptr; // pick a target on the globe
	return top;
}

static glm::ivec4 areaOf(State *s, int W, int H)
{
	int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
	for (Surface *sf : s->vrSurfaces())
	{
		if (!sf || !sf->getVisible() || sf->getWidth() <= 0 || sf->getHeight() <= 0) continue;
		x0 = std::min(x0, sf->getX()); y0 = std::min(y0, sf->getY());
		x1 = std::max(x1, sf->getX() + sf->getWidth()); y1 = std::max(y1, sf->getY() + sf->getHeight());
	}
	x0 = std::max(0, x0); y0 = std::max(0, y0);
	x1 = std::min(W, x1); y1 = std::min(H, y1);
	if (x1 - x0 < 8 || y1 - y0 < 8) return glm::ivec4(0);
	return {x0, y0, x1 - x0, y1 - y0};
}

void FloatWindow::update(const FloatContext &ctx, const Hands &hands, float dt)
{
	Impl &p = *_p;
	p.time += dt;
	p.t += dt;
	p.surfW = ctx.surfW;
	p.surfH = ctx.surfH;
	State *want = popupOf(ctx.game);

	// phases: a different pop-up folds the current window away first, then the next one unfolds
	// the projection replaces the game's own pop-up animation: wait until its windows have grown
	bool ready = want != nullptr;
	if (want)
		for (Surface *sf : want->vrSurfaces())
			if (Window *w = dynamic_cast<Window*>(sf))
				if (!w->vrPopupDone()) ready = false;
	if (p.phase == Impl::CLOSED && want && ready)
	{
		glm::ivec4 r = ctx.pixels ? areaOf(want, ctx.surfW, ctx.surfH) : glm::ivec4(0);
		if (r.z > 0)
		{
			p.src = want;
			p.rect = r;
			p.phase = Impl::OPENING;
			p.t = 0.f;
			p.lastEvent = std::string("open ") + typeid(*want).name();
			Log(LOG_INFO) << "[VR] float: " << p.lastEvent << " " << r.x << "," << r.y << " " << r.z << "x" << r.w;
		}
	}
	else if ((p.phase == Impl::OPENING || p.phase == Impl::OPEN) && want != p.src)
	{
		p.phase = Impl::CLOSING;
		p.t = 0.f;
		p.src = nullptr; // may be gone already
		p.lastEvent = "close";
	}
	if (p.phase == Impl::OPENING && p.t >= T_LINE + T_UNFOLD) { p.phase = Impl::OPEN; p.t = 0.f; }
	if (p.phase == Impl::CLOSING && p.t >= T_FOLD + T_SHRINK) { p.phase = Impl::CLOSED; p.t = 0.f; }

	// live picture while the pop-up is up; a closing window keeps its last picture
	if (p.src && (p.phase == Impl::OPENING || p.phase == Impl::OPEN) && ctx.pixels && !ctx.pixels->empty())
	{
		glm::ivec4 r = areaOf(p.src, ctx.surfW, ctx.surfH);
		if (r.z > 0) p.rect = r;
		const glm::ivec4 &R = p.rect;
		p.px.resize((size_t)R.z * R.w);
		for (int y = 0; y < R.w; ++y)
			std::copy_n(&(*ctx.pixels)[(size_t)(R.y + y) * ctx.surfW + R.x], R.z, &p.px[(size_t)y * R.z]);
		if (!p.tex.valid() || p.tex.width() != R.z || p.tex.height() != R.w) p.tex.create(R.z, R.w, false, true);
		p.tex.update(p.px.data(), R.z, R.w);
	}

	// placement: over the near half of the map, leaning back, sized to the pop-up
	if (p.phase != Impl::CLOSED)
	{
		float maxH = ctx.keyboard ? 0.26f : 0.32f;
		float K = std::min(0.0019f, std::min(0.50f / std::max(1, p.rect.z), maxH / std::max(1, p.rect.w)));
		p.W = p.rect.z * K;
		p.H = p.rect.w * K;
		float lift = ctx.keyboard ? 0.11f : 0.04f;
		p.yLift += (lift - p.yLift) * std::min(1.f, dt * 6.f);
		const float tilt = glm::radians(18.f);
		p.right = glm::vec3(1, 0, 0);
		p.up = glm::vec3(0, std::cos(tilt), -std::sin(tilt));
		p.n = glm::vec3(0, std::sin(tilt), std::cos(tilt));
		p.O = glm::vec3((ctx.table.x + ctx.table.z) * 0.5f, ctx.tableY + p.yLift, ctx.table.w - (ctx.keyboard ? 0.26f : 0.24f));
	}
	else p.yLift = ctx.keyboard ? 0.11f : 0.04f;

	// sparks off the moving edges
	float fold = 0.f, width = 0.f;
	p.shape(fold, width);
	if (p.phase == Impl::OPENING)
	{
		if (p.t < T_LINE) p.emitEnds(4, 0.f);
		else { p.emit(6, p.H * 0.5f * fold, 1.f); p.emit(6, -p.H * 0.5f * fold, -1.f); }
	}
	else if (p.phase == Impl::CLOSING)
	{
		if (p.t < T_FOLD) { p.emit(3, p.H * 0.5f * fold, 1.f); p.emit(3, -p.H * 0.5f * fold, -1.f); }
		else p.emitEnds(4, 0.f);
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
	for (int h = 0; h < 2; ++h) p.cool[h] = std::max(0.f, p.cool[h] - dt);
	if (p.phase != Impl::OPEN || !p.src) { p.armed[0] = p.armed[1] = true; return; }
	int hoverHand = -1;
	float hoverDepth = 1e9f;
	glm::ivec2 hoverPx(-1);
	for (int h = 0; h < 2; ++h)
	{
		const HandPose &P = hands.pose[h];
		if (!P.valid || P.ghost) { p.armed[h] = true; continue; }
		glm::vec3 tip = P.raw[fingerTip(F_INDEX)];
		float r = P.radius[fingerTip(F_INDEX)];
		glm::vec3 d = tip - p.O;
		float u = glm::dot(d, p.right), v = glm::dot(d, p.up), depth = glm::dot(d, p.n) - r;
		bool inside = std::fabs(u) <= p.W * 0.5f && v >= 0.f && v <= p.H;
		glm::ivec2 g((int)((u / p.W + 0.5f) * p.rect.z) + p.rect.x, (int)((1.f - v / p.H) * p.rect.w) + p.rect.y);
		if (!inside || depth > 0.012f) p.armed[h] = true;
		if (!inside || depth < -0.05f) continue;
		if (depth < 0.04f && depth < hoverDepth) { hoverDepth = depth; hoverHand = h; hoverPx = g; }
		if (depth <= 0.003f && p.armed[h] && p.cool[h] <= 0.f)
		{
			p.armed[h] = false;
			p.cool[h] = 0.25f;
			if (ctx.clickScreen) ctx.clickScreen(g.x, g.y, 1);
			if (ctx.haptic) ctx.haptic(h, 0.6f, 0.02f);
			std::ostringstream ss;
			ss << "touch " << g.x << "," << g.y;
			p.lastEvent = ss.str();
			Log(LOG_INFO) << "[VR] float: " << p.lastEvent;
		}
	}
	if (hoverHand >= 0 && hoverPx != p.lastHover && ctx.hover) ctx.hover(hoverPx.x, hoverPx.y);
	p.lastHover = hoverHand >= 0 ? hoverPx : glm::ivec2(-1);
}

void FloatWindow::draw(const Shader &sh) const
{
	const Impl &p = *_p;
	if (p.phase == Impl::CLOSED && p.sparks.empty()) return;
	float fold = 0.f, width = 0.f;
	p.shape(fold, width);
	glm::vec2 mid(0.f, p.H * 0.5f);
	float bandH = std::max(LINE_H, p.H * fold);
	float bandW = p.W * width;
	// flicker while it materializes, a faint shimmer once it's steady
	float settle = p.phase == Impl::OPEN ? 1.f : fold;
	float flick = 1.f - (1.f - settle) * (0.35f + 0.35f * std::sin((float)p.time * 71.f) * std::sin((float)p.time * 23.f));
	flick *= 0.97f + 0.03f * std::sin((float)p.time * 9.f);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	if (p.phase != Impl::CLOSED && bandW > 0.001f)
	{
		// the backing covers whatever stands on the table there (always on top of it, and writes
		// depth so hands in front of the window still show)
		glDepthFunc(GL_ALWAYS);
		sh.set("uMode", 2);
		sh.set("uTint", glm::vec4(0.15f, 0.9f, 1.1f, 0.55f * flick));
		sh.set("uModel", p.frame(mid, glm::vec2(bandW * 0.5f + 0.004f, bandH * 0.5f + 0.004f), -0.0015f));
		p.quad.draw();
		sh.set("uTint", glm::vec4(0.01f, 0.02f, 0.03f, 0.96f));
		sh.set("uModel", p.frame(mid, glm::vec2(bandW * 0.5f, bandH * 0.5f), -0.001f));
		p.quad.draw();
		glDepthFunc(GL_LEQUAL);
		// the picture: the middle band of the pop-up, as wide as the line and as tall as the fold
		if (fold > 0.01f && p.tex.valid())
		{
			sh.set("uMode", 1);
			sh.set("uTint", glm::vec4(flick, flick, flick, 1.f));
			sh.set("uTexSize", glm::vec2((float)p.tex.width(), (float)p.tex.height()));
			sh.set("uUVRect", glm::vec4(0.5f - 0.5f * width, 0.5f - 0.5f * fold, width, fold));
			sh.set("uModel", p.frame(mid, glm::vec2(bandW * 0.5f, p.H * fold * 0.5f), 0.f));
			p.tex.bind(0);
			p.quad.draw();
			sh.set("uUVRect", glm::vec4(0.f, 0.f, 1.f, 1.f));
		}
		// scanlines rolling down the picture while it materializes, then one slow sweep now and then
		sh.set("uMode", 2);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		if (p.phase != Impl::OPEN && fold > 0.02f)
			for (int i = 0; i < 7; ++i)
			{
				float f = std::fmod((float)p.time * 1.7f + i / 7.f, 1.f);
				float v = (0.5f - f) * bandH;
				sh.set("uTint", glm::vec4(0.3f, 0.9f, 1.f, 0.18f * (1.f - settle * 0.6f)));
				sh.set("uModel", p.frame(mid + glm::vec2(0.f, v), glm::vec2(bandW * 0.5f, 0.0015f), 0.0006f));
				p.quad.draw();
			}
		else if (p.phase == Impl::OPEN)
		{
			float f = std::fmod((float)p.time / 3.5f, 1.f);
			if (f < 0.4f)
			{
				float v = (0.5f - f / 0.4f) * p.H;
				sh.set("uTint", glm::vec4(0.3f, 0.9f, 1.f, 0.07f));
				sh.set("uModel", p.frame(mid + glm::vec2(0.f, v), glm::vec2(p.W * 0.5f, 0.004f), 0.0006f));
				p.quad.draw();
			}
		}
		// the projection line: a bright bar while it's a line, then the two edges of the band
		float glowA = p.phase == Impl::OPEN ? 0.35f : 1.f;
		sh.set("uTint", glm::vec4(0.7f, 1.6f, 1.9f, glowA * flick));
		if (fold < 0.02f)
		{
			sh.set("uModel", p.frame(mid, glm::vec2(bandW * 0.5f, LINE_H), 0.001f));
			p.quad.draw();
		}
		else
			for (float s : {-1.f, 1.f})
			{
				sh.set("uModel", p.frame(mid + glm::vec2(0.f, s * bandH * 0.5f), glm::vec2(bandW * 0.5f + 0.003f, LINE_H * 0.6f), 0.001f));
				p.quad.draw();
			}
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	glDisable(GL_BLEND);
	sh.set("uTint", glm::vec4(1.f));
}

bool FloatWindow::raycast(const glm::vec3 &o, const glm::vec3 &d, float &t, glm::vec2 &gameUV) const
{
	const Impl &p = *_p;
	if (p.phase != Impl::OPEN || !p.src || p.W <= 0.f) return false;
	float dn = glm::dot(d, p.n);
	if (std::fabs(dn) < 1e-6f) return false;
	t = glm::dot(p.O - o, p.n) / dn;
	if (t <= 0.f) return false;
	glm::vec3 rel = o + d * t - p.O;
	float u = glm::dot(rel, p.right), v = glm::dot(rel, p.up);
	if (std::fabs(u) > p.W * 0.5f || v < 0.f || v > p.H) return false;
	// window uv -> game pixel -> uv over the whole game frame (what the wall screen's laser uses)
	float gx = p.rect.x + (u / p.W + 0.5f) * p.rect.z;
	float gy = p.rect.y + (1.f - v / p.H) * p.rect.w;
	gameUV = glm::vec2(gx / std::max(1, p.surfW), gy / std::max(1, p.surfH));
	return true;
}

bool FloatWindow::isOpen() const { return _p->phase == Impl::OPEN && _p->src; }
bool FloatWindow::isClosed() const { return _p->phase == Impl::CLOSED; }

bool FloatWindow::pointAt(float gx, float gy, glm::vec3 &w, glm::vec3 &n) const
{
	const Impl &p = *_p;
	if (!isOpen() || p.rect.z <= 0) return false;
	float u = ((gx - p.rect.x + 0.5f) / p.rect.z - 0.5f) * p.W;
	float v = (1.f - (gy - p.rect.y + 0.5f) / p.rect.w) * p.H;
	w = p.at(u, v);
	n = p.n;
	return true;
}

std::string FloatWindow::status() const
{
	const Impl &p = *_p;
	static const char *names[] = {"closed", "opening", "open", "closing"};
	std::ostringstream ss;
	ss << "float=" << names[p.phase] << " rect=" << p.rect.x << "," << p.rect.y << " " << p.rect.z << "x" << p.rect.w
		<< " size=" << p.W << "x" << p.H << " sparks=" << p.sparks.size() << " last='" << p.lastEvent << "'";
	return ss.str();
}

}
}
