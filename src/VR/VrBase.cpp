#include "VrGL.h" // first: GL extension headers
#include "VrBase.h"
#include "VrRoom.h"
#include "../Engine/Game.h"
#include "../Engine/State.h"
#include "../Engine/Surface.h"
#include "../Engine/SurfaceSet.h"
#include "../Engine/Font.h"
#include "../Engine/Language.h"
#include "../Engine/Logger.h"
#include "../Interface/Text.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleBaseFacility.h"
#include "../Mod/Texture.h"
#include "../Savegame/Base.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BaseFacility.h"
#include "../Savegame/Craft.h"
#include "../Basescape/BasescapeState.h"
#include "../Basescape/PlaceFacilityState.h"
#include "../Basescape/BaseView.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <cstdlib>

namespace OpenXcom
{
namespace VR
{

namespace
{
const int PX = 192, CELL = 32, CELLS = 6;   // the game's base view: 6 x 6 cells of 32 pixels
const float SIZE = 0.62f;                    // the base on the table (metres)
const float HOLD = 1.0f, TAP = 0.6f;         // seconds: hold to dismantle, longest tap

inline float lin(Uint8 c) { return std::pow(c / 255.f, 2.2f); }
}

struct BaseDiorama::Impl
{
	glm::vec4 map{0.f};
	float sy = 0.875f;
	glm::vec2 origin{0.f};       // world xz of the base's top-left corner
	bool placed = false;
	float S = SIZE / PX;         // metres per base pixel

	Game *game = nullptr;
	BasescapeState *bs = nullptr;
	PlaceFacilityState *place = nullptr;
	State *top = nullptr;
	Base *base = nullptr;
	BaseView *view = nullptr;
	bool on = false;
	double time = 0.0;

	// the relief
	std::string sig;
	Mesh relief;
	std::vector<float> height = std::vector<float>(PX * PX, 0.f);
	struct CraftModel { const Surface *frame; glm::vec2 at; };
	std::vector<CraftModel> crafts;
	std::map<const Surface*, std::unique_ptr<Mesh>> craftMeshes;
	struct Site { glm::ivec4 cells; int days; };
	std::vector<Site> sites;      // facilities under construction
	Mesh box, quad;
	mutable std::map<std::string, std::unique_ptr<Texture>> labels;

	// pointing and touching
	glm::ivec2 laserCell{-1}, fingerCell{-1};
	struct Touch { bool down = false; float held = 0.f; glm::ivec2 cell{-1}; bool fired = false; float cool = 0.f; };
	Touch touch[2];
	glm::ivec2 buildCell{-1};     // the empty spot tapped last (where a new facility starts out)
	glm::ivec2 hoverSent{-1};
	std::string lastEvent;

	glm::ivec2 shownCell() const { return fingerCell.x >= 0 ? fingerCell : laserCell; }
	glm::vec3 world(float px, float py, float h) const { return {origin.x + px * S, sy + h, origin.y + py * S}; }
	float heightAt(int x, int y) const { return (x < 0 || y < 0 || x >= PX || y >= PX) ? 0.f : height[(size_t)y * PX + x]; }
	BaseFacility *facilityAt(glm::ivec2 c) const
	{
		if (!base) return nullptr;
		for (BaseFacility *f : *base->getFacilities())
			if (c.x >= f->getX() && c.x < f->getX() + f->getRules()->getSizeX() && c.y >= f->getY() && c.y < f->getY() + f->getRules()->getSizeY())
				return f;
		return nullptr;
	}
	glm::ivec2 viewPixel(glm::ivec2 c) const
	{
		return view ? glm::ivec2(view->getX() + c.x * CELL + CELL / 2, view->getY() + c.y * CELL + CELL / 2) : glm::ivec2(-1);
	}
	void rebuild();
	const Mesh *craftMesh(const Surface *frame);
	const Texture *label(const std::string &s) const;
	void tap(glm::ivec2 c, const BaseInput &in);
	void holdAction(glm::ivec2 c, const BaseInput &in);
};

BaseDiorama::BaseDiorama() : _p(new Impl) {}
BaseDiorama::~BaseDiorama() {}

void BaseDiorama::init(const glm::vec4 &mapRect, float surfaceY)
{
	Impl &p = *_p;
	p.map = mapRect;
	p.sy = surfaceY;
	// centred left to right, close to the player (cells near the far edge are still in reach)
	p.origin = glm::vec2((mapRect.x + mapRect.z) * 0.5f - SIZE * 0.5f, mapRect.w - 0.03f - SIZE);
	MeshData b;
	b.addBox({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, glm::vec4(1), MAT_PLAIN);
	p.box.upload(b);
	MeshData q;
	q.addQuad({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}, glm::vec4(1), MAT_PLAIN, {0, 1}, {1, 1}, {1, 0}, {0, 0});
	p.quad.upload(q);
}

bool BaseDiorama::active() const { return _p->on; }

// ------------------------------------------------------------------ the relief

/// The base view's picture rebuilt layer by layer (ground, facility outlines, corridors, facility
/// art), each layer standing at its own height, brighter pixels a little higher: a voxel relief.
void BaseDiorama::Impl::rebuild()
{
	SurfaceSet *bits = game->getMod()->getSurfaceSet("BASEBITS.PCK", false);
	SDL_Color *pal = top ? top->getPalette() : nullptr;
	if (!bits || !pal || !base) return;
	std::vector<Uint8> idx(PX * PX, 0);
	std::vector<Uint8> layer(PX * PX, 0); // 1 ground, 2 corridor, 3 outline, 4 facility art
	std::vector<Uint8> building(PX * PX, 0);
	auto blit = [&](int frameNo, int fx, int fy, int L, bool constr)
	{
		Surface *f = bits->getFrame(frameNo);
		if (!f) return;
		for (int y = 0; y < f->getHeight(); ++y)
			for (int x = 0; x < f->getWidth(); ++x)
			{
				Uint8 c = f->getPixel(x, y);
				int X = fx + x, Y = fy + y;
				if (!c || X < 0 || Y < 0 || X >= PX || Y >= PX) continue;
				idx[(size_t)Y * PX + X] = c;
				layer[(size_t)Y * PX + X] = (Uint8)L;
				building[(size_t)Y * PX + X] = constr ? 1 : 0;
			}
	};
	int ground = base->getGlobeTexture() ? base->getGlobeTexture()->getBaseGridSprite() : 0;
	for (int y = 0; y < CELLS; ++y)
		for (int x = 0; x < CELLS; ++x) blit(ground, x * CELL, y * CELL, 1, false);
	BaseFacility *grid[CELLS][CELLS] = {};
	for (BaseFacility *f : *base->getFacilities())
		for (int y = f->getY(); y < f->getY() + f->getRules()->getSizeY(); ++y)
			for (int x = f->getX(); x < f->getX() + f->getRules()->getSizeX(); ++x)
				if (x >= 0 && y >= 0 && x < CELLS && y < CELLS) grid[x][y] = f;
	for (BaseFacility *f : *base->getFacilities())
	{
		const RuleBaseFacility *r = f->getRules();
		int num = 0;
		int outline = r->isSmall() ? 3 : r->getSizeX() * r->getSizeY();
		for (int y = f->getY(); y < f->getY() + r->getSizeY(); ++y)
			for (int x = f->getX(); x < f->getX() + r->getSizeX(); ++x, ++num)
				blit(r->getSpriteShape() + num + (f->getBuildTime() == 0 ? 0 : outline), x * CELL, y * CELL, 3, f->getBuildTime() > 0);
	}
	// corridors between finished facilities (as the game draws them)
	for (BaseFacility *f : *base->getFacilities())
	{
		const RuleBaseFacility *r = f->getRules();
		if (!f->isBuiltOrHadPreviousFacility() || r->connectorsDisabled()) continue;
		auto joins = [&](int x, int y) { BaseFacility *o = (x < CELLS && y < CELLS) ? grid[x][y] : nullptr; return o && o->isBuiltOrHadPreviousFacility() && !o->getRules()->connectorsDisabled(); };
		int x = f->getX() + r->getSizeX();
		if (x < CELLS)
			for (int y = f->getY(); y < f->getY() + r->getSizeY(); ++y)
				if (joins(x, y)) blit(7, x * CELL - CELL / 2, y * CELL, 2, false);
		int y = f->getY() + r->getSizeY();
		if (y < CELLS)
			for (int sx = f->getX(); sx < f->getX() + r->getSizeX(); ++sx)
				if (joins(sx, y)) blit(8, sx * CELL, y * CELL - CELL / 2, 2, false);
	}
	crafts.clear();
	sites.clear();
	auto craftIt = base->getCrafts()->begin();
	for (BaseFacility *f : *base->getFacilities())
	{
		const RuleBaseFacility *r = f->getRules();
		int num = 0;
		for (int y = f->getY(); y < f->getY() + r->getSizeY(); ++y)
			for (int x = f->getX(); x < f->getX() + r->getSizeX(); ++x, ++num)
				if (r->getSpriteEnabled()) blit(r->getSpriteFacility() + num, x * CELL, y * CELL, 4, f->getBuildTime() > 0);
		if (f->getBuildTime() > 0) sites.push_back({{f->getX(), f->getY(), r->getSizeX(), r->getSizeY()}, f->getBuildTime()});
		// craft parked in hangars, in the same order the game assigns them
		if (f->getBuildTime() == 0 && r->getCrafts() > 0 && craftIt != base->getCrafts()->end())
		{
			if ((*craftIt)->getStatus() != "STR_OUT")
			{
				Surface *fr = bits->getFrame((*craftIt)->getSkinSprite() + 33);
				if (fr) crafts.push_back({fr, glm::vec2(f->getX() * CELL + (r->getSizeX() - 1) * CELL / 2 + 2, f->getY() * CELL + (r->getSizeY() - 1) * CELL / 2 - 4)});
			}
			++craftIt;
		}
	}

	// heights (metres): ground low, corridors, outline walls and machinery higher, by brightness
	for (int i = 0; i < PX * PX; ++i)
	{
		if (!layer[i]) { height[i] = 0.f; continue; }
		SDL_Color k = pal[idx[i]];
		float l = (k.r * 0.3f + k.g * 0.59f + k.b * 0.11f) / 255.f;
		float h = 0.f;
		switch (layer[i])
		{
		case 1: h = 0.0015f + 0.0015f * l; break;
		case 2: h = 0.006f + 0.004f * l; break;
		case 3: h = 0.008f + 0.008f * l; break;
		default: h = 0.006f + 0.014f * l; break;
		}
		if (building[i]) h = std::min(h, 0.0035f); // foundations only, until it's built
		height[i] = h;
	}
	MeshData m;
	for (int y = 0; y < PX; ++y)
		for (int x = 0; x < PX; ++x)
		{
			int i = y * PX + x;
			if (!layer[i]) continue;
			SDL_Color k = pal[idx[i]];
			glm::vec4 col(lin(k.r), lin(k.g), lin(k.b), 1.f);
			if (building[i]) col *= glm::vec4(0.55f, 0.55f, 0.6f, 1.f);
			glm::vec4 side = col * glm::vec4(0.6f, 0.6f, 0.6f, 1.f);
			float h = height[i];
			float X0 = (float)x, X1 = x + 1.f, Z0 = (float)y, Z1 = y + 1.f;
			m.addQuad({X0, h, Z1}, {X1, h, Z1}, {X1, h, Z0}, {X0, h, Z0}, col, MAT_PLAIN);
			float n;
			if ((n = heightAt(x, y + 1)) < h) m.addQuad({X0, n, Z1}, {X1, n, Z1}, {X1, h, Z1}, {X0, h, Z1}, side, MAT_PLAIN);
			if ((n = heightAt(x, y - 1)) < h) m.addQuad({X1, n, Z0}, {X0, n, Z0}, {X0, h, Z0}, {X1, h, Z0}, side, MAT_PLAIN);
			if ((n = heightAt(x + 1, y)) < h) m.addQuad({X1, n, Z1}, {X1, n, Z0}, {X1, h, Z0}, {X1, h, Z1}, side, MAT_PLAIN);
			if ((n = heightAt(x - 1, y)) < h) m.addQuad({X0, n, Z0}, {X0, n, Z1}, {X0, h, Z1}, {X0, h, Z0}, side, MAT_PLAIN);
		}
	relief.upload(m);
	Log(LOG_INFO) << "[VR] base relief built: " << m.verts.size() << " vertices, " << crafts.size() << " craft, " << sites.size() << " under construction";
}

/// A craft sprite as a little voxel model: thicker toward the middle of its silhouette.
const Mesh *BaseDiorama::Impl::craftMesh(const Surface *frame)
{
	auto it = craftMeshes.find(frame);
	if (it != craftMeshes.end()) return it->second.get();
	SDL_Color *pal = top ? top->getPalette() : nullptr;
	if (!pal) return nullptr;
	int W = frame->getWidth(), H = frame->getHeight();
	std::vector<int> d(W * H, 0);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x) d[y * W + x] = const_cast<Surface*>(frame)->getPixel(x, y) ? 99 : 0;
	for (int pass = 0; pass < 2; ++pass)
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x)
			{
				int xx = pass ? W - 1 - x : x, yy = pass ? H - 1 - y : y;
				int &v = d[yy * W + xx];
				if (!v) continue;
				int best = v;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						int nx = xx + dx, ny = yy + dy;
						int n = (nx < 0 || ny < 0 || nx >= W || ny >= H) ? 0 : d[ny * W + nx];
						best = std::min(best, n + 1);
					}
				v = best;
			}
	auto ht = [&](int x, int y) -> float
	{
		if (x < 0 || y < 0 || x >= W || y >= H) return 0.f;
		int v = d[y * W + x];
		return v ? 0.002f + 0.0022f * (float)std::min(v - 1, 4) : 0.f;
	};
	MeshData m;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			Uint8 c = const_cast<Surface*>(frame)->getPixel(x, y);
			if (!c) continue;
			SDL_Color k = pal[c];
			glm::vec4 col(lin(k.r), lin(k.g), lin(k.b), 1.f);
			glm::vec4 side = col * glm::vec4(0.65f, 0.65f, 0.65f, 1.f);
			float h = ht(x, y), n;
			float X0 = (float)x, X1 = x + 1.f, Z0 = (float)y, Z1 = y + 1.f;
			// symmetric above and below the middle, like a hull
			m.addQuad({X0, h, Z1}, {X1, h, Z1}, {X1, h, Z0}, {X0, h, Z0}, col, MAT_GLOSSY);
			m.addQuad({X0, -h, Z0}, {X1, -h, Z0}, {X1, -h, Z1}, {X0, -h, Z1}, side, MAT_GLOSSY);
			if ((n = ht(x, y + 1)) < h) m.addQuad({X0, -h, Z1}, {X1, -h, Z1}, {X1, h, Z1}, {X0, h, Z1}, side, MAT_GLOSSY);
			if ((n = ht(x, y - 1)) < h) m.addQuad({X1, -h, Z0}, {X0, -h, Z0}, {X0, h, Z0}, {X1, h, Z0}, side, MAT_GLOSSY);
			if ((n = ht(x + 1, y)) < h) m.addQuad({X1, -h, Z1}, {X1, -h, Z0}, {X1, h, Z0}, {X1, h, Z1}, side, MAT_GLOSSY);
			if ((n = ht(x - 1, y)) < h) m.addQuad({X0, -h, Z0}, {X0, -h, Z1}, {X0, h, Z1}, {X0, h, Z0}, side, MAT_GLOSSY);
		}
	auto mesh = std::make_unique<Mesh>();
	mesh->upload(m);
	const Mesh *ptr = mesh.get();
	craftMeshes[frame] = std::move(mesh);
	return ptr;
}

/// A line of text in the game's big font (white with a dark outline), for labels over the base.
const Texture *BaseDiorama::Impl::label(const std::string &s) const
{
	auto it = labels.find(s);
	if (it != labels.end()) return it->second.get();
	Font *big = game->getMod()->getFont("FONT_BIG", false), *small = game->getMod()->getFont("FONT_SMALL", false);
	if (!big || !small) return nullptr;
	const int W = 160, H = 18;
	Text t(W, H, 0, 0);
	t.initText(big, small, game->getLanguage());
	if (top) t.setPalette(top->getPalette());
	t.setBig();
	t.setAlign(ALIGN_CENTER);
	t.setVerticalAlign(ALIGN_MIDDLE);
	t.setColor(1);
	t.setText(s);
	t.draw();
	std::vector<uint32_t> px((size_t)W * H, 0u);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
		{
			int v = t.getPixel(x, y);
			if (v) px[(size_t)y * W + x] = v <= 2 ? 0xFFFFFFFFu : 0xC0000000u;
		}
	auto tex = std::make_unique<Texture>();
	tex->create(W, H, false, true);
	tex->update(px.data(), W, H);
	const Texture *ptr = tex.get();
	labels[s] = std::move(tex);
	return ptr;
}

// ------------------------------------------------------------------ following the game

bool BaseDiorama::update(Game *game, float dt)
{
	Impl &p = *_p;
	p.game = game;
	p.time += dt;
	p.bs = nullptr;
	p.place = nullptr;
	p.base = nullptr;
	p.view = nullptr;
	p.top = nullptr;
	// the topmost base screen or facility placement on the stack shows its base
	const auto &states = game->getStates();
	for (auto it = states.rbegin(); it != states.rend(); ++it)
	{
		if (auto *pf = dynamic_cast<PlaceFacilityState*>(*it)) { p.place = pf; p.base = pf->vrBase(); p.view = pf->vrView(); p.top = pf; break; }
		if (auto *b = dynamic_cast<BasescapeState*>(*it)) { p.bs = b; p.base = b->vrBase(); p.view = b->vrView(); p.top = b; break; }
	}
	p.on = p.base != nullptr && game->getSavedGame() && !game->getSavedGame()->getSavedBattle();
	if (!p.on) { p.sig.clear(); return false; }
	// rebuild when anything shown changed
	std::ostringstream ss;
	ss << (const void*)p.base;
	for (BaseFacility *f : *p.base->getFacilities())
		ss << '|' << f->getRules()->getType() << ',' << f->getX() << ',' << f->getY() << ',' << f->getBuildTime() << ',' << f->getDisabled();
	for (Craft *c : *p.base->getCrafts()) ss << '|' << c->getSkinSprite() << (c->getStatus() == "STR_OUT" ? 'o' : 'i');
	if (ss.str() != p.sig)
	{
		p.sig = ss.str();
		p.rebuild();
	}
	return true;
}

// ------------------------------------------------------------------ drawing

void BaseDiorama::draw(const Shader &sh, const glm::vec3 &eye) const
{
	const Impl &p = *_p;
	if (!p.on) return;
	glm::mat4 M = glm::translate(glm::mat4(1.f), glm::vec3(p.origin.x, p.sy, p.origin.y)) * glm::scale(glm::mat4(1.f), glm::vec3(p.S, 1.f, p.S));
	sh.set("uMode", 0);
	sh.set("uTint", glm::vec4(1.15f, 1.15f, 1.15f, 1.f));
	sh.set("uModel", M);
	p.relief.draw();

	// craft in their hangars, hovering a little
	for (size_t i = 0; i < p.crafts.size(); ++i)
	{
		const Mesh *m = const_cast<Impl&>(p).craftMesh(p.crafts[i].frame);
		if (!m) continue;
		float bob = 0.003f * (float)std::sin(p.time * 1.6 + i * 2.1);
		glm::vec2 a = p.crafts[i].at;
		sh.set("uModel", glm::translate(glm::mat4(1.f), glm::vec3(p.origin.x + a.x * p.S, p.sy + 0.03f + bob, p.origin.y + a.y * p.S))
			* glm::scale(glm::mat4(1.f), glm::vec3(p.S, 1.f, p.S)));
		m->draw();
	}

	auto boxAt = [&](const glm::vec3 &mn, const glm::vec3 &mx)
	{
		sh.set("uModel", glm::translate(glm::mat4(1.f), (mn + mx) * 0.5f) * glm::scale(glm::mat4(1.f), glm::max(mx - mn, glm::vec3(0.0005f))));
		p.box.draw();
	};
	// scaffolding around facilities under construction
	const float POLE = 0.0028f, TOPH = 0.055f;
	for (const Impl::Site &s : p.sites)
	{
		sh.set("uMode", 0);
		sh.set("uTint", glm::vec4(0.95f, 0.62f, 0.12f, 1.f));
		float x0 = s.cells.x * CELL + 3.f, y0 = s.cells.y * CELL + 3.f;
		float x1 = (s.cells.x + s.cells.z) * CELL - 3.f, y1 = (s.cells.y + s.cells.w) * CELL - 3.f;
		for (int i = 0; i <= s.cells.z * 2; ++i)
			for (float yy : {y0, y1})
			{
				float xx = x0 + (x1 - x0) * i / (s.cells.z * 2);
				glm::vec3 b = p.world(xx, yy, 0.f);
				boxAt(b - glm::vec3(POLE * 0.5f, 0.f, POLE * 0.5f), b + glm::vec3(POLE * 0.5f, TOPH, POLE * 0.5f));
			}
		for (int i = 1; i < s.cells.w * 2; ++i)
			for (float xx : {x0, x1})
			{
				float yy = y0 + (y1 - y0) * i / (s.cells.w * 2);
				glm::vec3 b = p.world(xx, yy, 0.f);
				boxAt(b - glm::vec3(POLE * 0.5f, 0.f, POLE * 0.5f), b + glm::vec3(POLE * 0.5f, TOPH, POLE * 0.5f));
			}
		for (float h : {TOPH * 0.5f, TOPH})
		{
			glm::vec3 a = p.world(x0, y0, h), b = p.world(x1, y1, h);
			boxAt(glm::vec3(a.x, h + p.sy - POLE * 0.5f, a.z - POLE * 0.5f), glm::vec3(b.x, h + p.sy + POLE * 0.5f, a.z + POLE * 0.5f));
			boxAt(glm::vec3(a.x, h + p.sy - POLE * 0.5f, b.z - POLE * 0.5f), glm::vec3(b.x, h + p.sy + POLE * 0.5f, b.z + POLE * 0.5f));
			boxAt(glm::vec3(a.x - POLE * 0.5f, h + p.sy - POLE * 0.5f, a.z), glm::vec3(a.x + POLE * 0.5f, h + p.sy + POLE * 0.5f, b.z));
			boxAt(glm::vec3(b.x - POLE * 0.5f, h + p.sy - POLE * 0.5f, a.z), glm::vec3(b.x + POLE * 0.5f, h + p.sy + POLE * 0.5f, b.z));
		}
	}

	// labels facing the eyes
	auto billboard = [&](const std::string &s, const glm::vec3 &at, float h, const glm::vec4 &tint)
	{
		const Texture *t = p.label(s);
		if (!t) return;
		glm::vec3 f = eye - at;
		if (glm::length(f) < 1e-4f) return;
		f = glm::normalize(f);
		glm::vec3 r = glm::normalize(glm::cross(glm::vec3(0, 1, 0), f));
		glm::vec3 u = glm::cross(f, r);
		float w = h * 160.f / 18.f;
		glm::mat4 m(1.f);
		m[0] = glm::vec4(r * w, 0.f);
		m[1] = glm::vec4(u * h, 0.f);
		m[2] = glm::vec4(f, 0.f);
		m[3] = glm::vec4(at, 1.f);
		sh.set("uMode", 4);
		sh.set("uTint", tint);
		sh.set("uModel", m);
		t->bind(0);
		p.quad.draw();
	};
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	for (const Impl::Site &s : p.sites)
	{
		glm::vec3 c = p.world((s.cells.x + s.cells.z * 0.5f) * CELL, (s.cells.y + s.cells.w * 0.5f) * CELL, TOPH + 0.022f);
		billboard(std::to_string(s.days) + (s.days == 1 ? " DAY" : " DAYS"), c, 0.03f, glm::vec4(1.f, 0.85f, 0.4f, 1.f));
	}

	// what the finger or laser points at: an outline and its name; a facility being placed: its footprint
	glm::ivec2 c = p.shownCell();
	if (c.x >= 0)
	{
		glm::ivec4 r(c.x, c.y, 1, 1);
		glm::vec4 col(0.3f, 1.f, 1.f, 0.9f);
		std::string name;
		if (p.place && p.place->vrRule())
		{
			r.z = p.place->vrRule()->getSizeX();
			r.w = p.place->vrRule()->getSizeY();
			bool fits = r.x + r.z <= CELLS && r.y + r.w <= CELLS;
			if (fits && p.view && p.view->getGridX() == c.x && p.view->getGridY() == c.y)
				fits = !p.view->getPlacementError(p.place->vrRule(), p.place->vrMoving());
			col = fits ? glm::vec4(0.3f, 1.f, 0.4f, 0.9f) : glm::vec4(1.f, 0.3f, 0.25f, 0.9f);
			name = p.game->getLanguage()->getString(p.place->vrRule()->getType());
			// the footprint as a translucent block
			sh.set("uMode", 2);
			sh.set("uTint", glm::vec4(col.r, col.g, col.b, 0.3f));
			boxAt(p.world(r.x * CELL + 1.f, r.y * CELL + 1.f, 0.f), p.world((r.x + r.z) * CELL - 1.f, (r.y + r.w) * CELL - 1.f, 0.035f));
		}
		else if (BaseFacility *f = p.facilityAt(c))
		{
			r = {f->getX(), f->getY(), f->getRules()->getSizeX(), f->getRules()->getSizeY()};
			name = p.game->getLanguage()->getString(f->getRules()->getType());
		}
		else name = p.game->getLanguage()->getString("STR_BUILD_FACILITIES");
		// hold to dismantle: the outline fills red
		float hold = 0.f;
		for (const Impl::Touch &t : p.touch)
			if (t.down && !t.fired && t.cell == c && !p.place && p.facilityAt(c)) hold = std::max(hold, glm::clamp((t.held - TAP) / (HOLD - TAP), 0.f, 1.f));
		if (hold > 0.f)
		{
			sh.set("uMode", 2);
			sh.set("uTint", glm::vec4(1.f, 0.2f, 0.15f, 0.25f + 0.35f * hold));
			boxAt(p.world(r.x * CELL + 1.f, r.y * CELL + 1.f, 0.f), p.world((r.x + r.z) * CELL - 1.f, (r.y + r.w) * CELL - 1.f, 0.004f + 0.04f * hold));
			col = glm::mix(col, glm::vec4(1.f, 0.25f, 0.2f, 1.f), hold);
		}
		sh.set("uMode", 2);
		sh.set("uTint", col);
		const float T = 0.0025f, H = 0.026f;
		glm::vec3 a = p.world(r.x * CELL, r.y * CELL, 0.f), b = p.world((r.x + r.z) * CELL, (r.y + r.w) * CELL, 0.f);
		float y0 = p.sy + 0.002f, y1 = p.sy + H;
		boxAt({a.x, y1 - T, a.z - T * 0.5f}, {b.x, y1, a.z + T * 0.5f});
		boxAt({a.x, y1 - T, b.z - T * 0.5f}, {b.x, y1, b.z + T * 0.5f});
		boxAt({a.x - T * 0.5f, y1 - T, a.z}, {a.x + T * 0.5f, y1, b.z});
		boxAt({b.x - T * 0.5f, y1 - T, a.z}, {b.x + T * 0.5f, y1, b.z});
		for (float x : {a.x, b.x})
			for (float z : {a.z, b.z}) boxAt({x - T * 0.5f, y0, z - T * 0.5f}, {x + T * 0.5f, y1, z + T * 0.5f});
		if (!name.empty())
			billboard(name, p.world((r.x + r.z * 0.5f) * CELL, (r.y + r.w * 0.5f) * CELL, H + 0.03f), 0.026f, glm::vec4(0.75f, 1.f, 1.1f, 1.f));
	}
	glDisable(GL_BLEND);
	sh.set("uTint", glm::vec4(1.f));
}

// ------------------------------------------------------------------ input

bool BaseDiorama::raycast(const glm::vec3 &o, const glm::vec3 &d, float &t, glm::vec3 &point, int &cx, int &cy) const
{
	const Impl &p = *_p;
	if (!p.on || std::fabs(d.y) < 1e-6f) return false;
	t = (p.sy + 0.01f - o.y) / d.y;
	if (t <= 0.f) return false;
	point = o + d * t;
	float px = (point.x - p.origin.x) / p.S, py = (point.z - p.origin.y) / p.S;
	if (px < 0.f || py < 0.f || px >= PX || py >= PX) return false;
	cx = (int)px / CELL;
	cy = (int)py / CELL;
	return true;
}

void BaseDiorama::hover(int cx, int cy) { _p->laserCell = {cx, cy}; }
void BaseDiorama::hoverNone() { _p->laserCell = glm::ivec2(-1); }

/// A tap: a facility opens its screen (the game's right click), an empty spot opens the build list.
void BaseDiorama::Impl::tap(glm::ivec2 c, const BaseInput &in)
{
	if (!bs || game->getStates().back() != (State*)bs) return; // something else is open
	if (BaseFacility *f = facilityAt(c))
	{
		glm::ivec2 g = viewPixel(c);
		if (in.clickScreen) in.clickScreen(g.x, g.y, SDL_BUTTON_RIGHT);
		lastEvent = "tap " + f->getRules()->getType();
	}
	else
	{
		buildCell = c;
		bs->vrBuild();
		lastEvent = "build at " + std::to_string(c.x) + "," + std::to_string(c.y);
	}
	Log(LOG_INFO) << "[VR] base: " << lastEvent;
}

/// A long press: the game's left click on a facility, which asks to dismantle it.
void BaseDiorama::Impl::holdAction(glm::ivec2 c, const BaseInput &in)
{
	if (!bs || game->getStates().back() != (State*)bs) return;
	BaseFacility *f = facilityAt(c);
	if (!f || f->getRules()->isLift()) return; // (the game's left click on the lift starts a base defense preview)
	glm::ivec2 g = viewPixel(c);
	if (in.clickScreen) in.clickScreen(g.x, g.y, SDL_BUTTON_LEFT);
	lastEvent = "hold " + f->getRules()->getType();
	Log(LOG_INFO) << "[VR] base: " << lastEvent;
}

void BaseDiorama::click(int button, const BaseInput &in)
{
	Impl &p = *_p;
	glm::ivec2 c = p.laserCell;
	if (!p.on || c.x < 0) return;
	if (p.place && p.game->getStates().back() == (State*)p.place)
	{
		glm::ivec2 g = p.viewPixel(c);
		if (button == SDL_BUTTON_LEFT && in.clickScreen) in.clickScreen(g.x, g.y, SDL_BUTTON_LEFT);
		return;
	}
	if (button == SDL_BUTTON_LEFT) p.tap(c, in);
	else p.holdAction(c, in);
}

void BaseDiorama::fingers(const Hands &hands, const BaseInput &in, float dt)
{
	Impl &p = *_p;
	p.fingerCell = glm::ivec2(-1);
	if (!p.on) return;
	bool placing = p.place && p.game->getStates().back() == (State*)p.place;
	bool baseTop = p.bs && p.game->getStates().back() == (State*)p.bs;
	for (int h = 0; h < 2; ++h)
	{
		Impl::Touch &t = p.touch[h];
		t.cool = std::max(0.f, t.cool - dt);
		const HandPose &P = hands.pose[h];
		bool usable = P.valid && !P.ghost && P.indexExtended && !P.pinch && !in.handBusy[h] && (placing || baseTop);
		glm::vec3 tip = usable ? P.tip(F_INDEX) : glm::vec3(0.f);
		float r = usable ? P.radius[fingerTip(F_INDEX)] : 0.f;
		float px = (tip.x - p.origin.x) / p.S, py = (tip.z - p.origin.y) / p.S;
		bool inside = usable && px >= 0.f && py >= 0.f && px < PX && py < PX;
		glm::ivec2 cell = inside ? glm::ivec2((int)px / CELL, (int)py / CELL) : glm::ivec2(-1);
		float top = inside ? p.heightAt((int)px, (int)py) : 0.f;
		bool hovering = inside && tip.y - r < p.sy + top + 0.09f;
		// touch at the surface, let go a little above it (no flicker while the finger rests there)
		bool touching = inside && tip.y - r <= p.sy + top + (t.down ? 0.012f : 0.004f);
		if (hovering && p.fingerCell.x < 0) p.fingerCell = cell;
		if (touching && !t.down && t.cool <= 0.f)
		{
			t.down = true;
			t.held = 0.f;
			t.cell = cell;
			t.fired = false;
			if (placing)
			{
				// place it where the finger landed
				glm::ivec2 g = p.viewPixel(cell);
				if (in.clickScreen) in.clickScreen(g.x, g.y, SDL_BUTTON_LEFT);
				t.fired = true;
				p.lastEvent = "place at " + std::to_string(cell.x) + "," + std::to_string(cell.y);
				Log(LOG_INFO) << "[VR] base: " << p.lastEvent;
			}
			if (in.haptic) in.haptic(h, 0.4f, 0.02f);
		}
		if (!t.down) continue;
		t.held += dt;
		if (!touching)
		{
			if (!t.fired && t.held < TAP) p.tap(t.cell, in);
			t.down = false;
			t.cool = 0.3f;
		}
		else if (!t.fired && t.held >= HOLD)
		{
			p.holdAction(t.cell, in);
			t.fired = true;
			if (in.haptic) in.haptic(h, 0.8f, 0.06f);
		}
	}
	// while placing, the game's own selector follows the finger (it also tells whether the spot fits)
	glm::ivec2 c = p.shownCell();
	if (placing && c.x >= 0 && c != p.hoverSent && in.hover)
	{
		glm::ivec2 g = p.viewPixel(c);
		in.hover(g.x - CELL / 2 + 2, g.y - CELL / 2 + 2);
		p.hoverSent = c;
	}
	if (!placing) p.hoverSent = glm::ivec2(-1);
}

void BaseDiorama::pan(const glm::vec2 &d)
{
	Impl &p = *_p;
	p.origin += d;
	p.origin.x = glm::clamp(p.origin.x, p.map.x, p.map.z - SIZE);
	p.origin.y = glm::clamp(p.origin.y, p.map.y, p.map.w - SIZE);
}

bool BaseDiorama::contains(const glm::vec3 &w, float margin) const
{
	const Impl &p = *_p;
	return p.on && w.x > p.origin.x - margin && w.x < p.origin.x + SIZE + margin && w.z > p.origin.y - margin && w.z < p.origin.y + SIZE + margin
		&& w.y < p.sy + 0.4f && w.y > p.sy - 0.1f;
}

bool BaseDiorama::cellTop(int cx, int cy, glm::vec3 &w) const
{
	const Impl &p = *_p;
	if (!p.on || cx < 0 || cy < 0 || cx >= CELLS || cy >= CELLS) return false;
	float px = cx * CELL + CELL * 0.5f, py = cy * CELL + CELL * 0.5f;
	w = p.world(px, py, p.heightAt((int)px, (int)py));
	return true;
}

std::string BaseDiorama::status() const
{
	const Impl &p = *_p;
	std::ostringstream ss;
	ss << "base=" << (p.on ? 1 : 0);
	if (p.on)
		ss << " facilities=" << p.base->getFacilities()->size() << " craft=" << p.crafts.size() << " sites=" << p.sites.size()
			<< " placing=" << (p.place ? 1 : 0) << " last='" << p.lastEvent << "'";
	return ss.str();
}

}
}
