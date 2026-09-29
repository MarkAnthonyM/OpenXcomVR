/*
 * OXCE VR tabletop - the war table's contents.
 *
 * Battlescape diorama
 *   Terrain is rebuilt in 3D from the game's own line-of-fire voxel data
 *   (LOFTEMPS: 16x16x24 voxels per tile part). Every exposed voxel face is
 *   colored by projecting it back into the part's isometric sprite, then
 *   coplanar faces are merged ("greedy meshing") and textured from an atlas,
 *   so a wall is a handful of quads instead of hundreds of cubes.
 *   Parts without voxels (grass tufts, small props) become crossed cards.
 *   Units are cardboard standees drawn with the game's own UnitSprite code,
 *   re-oriented for the angle you look at them from.
 *
 * Geoscape globe
 *   The globe's land polygons on a real sphere, colored from the game's
 *   globe textures, with bases, craft, UFOs and sites as markers.
 */
#include "VrGL.h" // first: GL extension headers
#include "VrBoard.h"
#include "VrRoom.h"
#include "../Engine/Game.h"
#include "../Engine/State.h"
#include "../Engine/Surface.h"
#include "../Engine/SurfaceSet.h"
#include "../Engine/Palette.h"
#include "../Engine/Logger.h"
#include "../Engine/Options.h"
#include "../Engine/GraphSubset.h"
#include "../Mod/Mod.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Armor.h"
#include "../Mod/RuleGlobe.h"
#include "../Mod/Polygon.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Base.h"
#include "../Savegame/Craft.h"
#include "../Savegame/Ufo.h"
#include "../Savegame/AlienBase.h"
#include "../Savegame/MissionSite.h"
#include "../Savegame/GameTime.h"
#include "../Battlescape/BattlescapeState.h"
#include "../Battlescape/Map.h"
#include "../Battlescape/Camera.h"
#include "../Battlescape/UnitSprite.h"
#include "../Battlescape/Projectile.h"
#include "../Battlescape/Explosion.h"
#include "../Battlescape/Position.h"
#include "../Geoscape/GeoscapeState.h"
#include "../Geoscape/Globe.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <map>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <functional>

namespace OpenXcom
{
namespace VR
{

static const int TILE_W = 16;   // voxels per tile, horizontally
static const int TILE_H = 24;   // voxels per tile, vertically
static const int CHUNK = 8;     // tiles per chunk side
static const int ATLAS = 2048;
static const float MAT_BOARD = 8.f;

static inline uint32_t rgba(const SDL_Color &c, uint8_t a = 255)
{
	return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)a << 24);
}

// ------------------------------------------------------------------ atlas

struct Atlas
{
	std::vector<uint32_t> px;
	int x = 0, y = 0, rowH = 0;
	bool dirty = false, full = false;
	VR::Texture tex;

	void reset()
	{
		px.assign((size_t)ATLAS * ATLAS, 0u);
		x = y = rowH = 0;
		dirty = true;
		full = false;
	}
	bool alloc(int w, int h, int &ox, int &oy)
	{
		if (x + w > ATLAS) { x = 0; y += rowH; rowH = 0; }
		if (y + h > ATLAS) { full = true; return false; }
		ox = x; oy = y;
		x += w;
		rowH = std::max(rowH, h);
		dirty = true;
		return true;
	}
	void put(int ax, int ay, uint32_t c) { px[(size_t)ay * ATLAS + ax] = c; }
	void upload()
	{
		if (!dirty) return;
		if (!tex.valid())
		{
			tex.create(ATLAS, ATLAS, true, false);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
		}
		tex.update(px.data(), ATLAS, ATLAS);
		dirty = false;
	}
};

// ------------------------------------------------------------------ data

/// Local geometry of one terrain part, in voxel units (X east, Y up, Z south).
struct Proto
{
	std::vector<Vertex> verts;
	std::vector<uint32_t> idx;
	bool card = false; // no voxels: drawn as crossed cards
};

struct Chunk
{
	uint64_t hash = ~0ull;
	std::unique_ptr<Mesh> mesh;
	bool empty = true;
};

struct UnitCard
{
	std::unique_ptr<VR::Texture> tex;
	int w = 32, h = 40, anchorY = 32;
	std::vector<uint32_t> px;
};

struct Board::Impl
{
	RoomLayout layout;
	glm::vec3 viewer{0.f, 1.65f, 0.f};
	Game *game = nullptr;
	bool hasBattle = false, hasGlobe = false;
	double time = 0.0;

	// ---- battlescape
	SavedBattleGame *battle = nullptr;
	BattlescapeState *bs = nullptr; // only valid while it is on the game's state stack
	const Tile *firstTile = nullptr;
	int mx = 0, my = 0, mz = 0, cxN = 0, cyN = 0;
	std::map<const MapData*, Proto> protos;
	std::vector<Chunk> chunks;
	Atlas atlas;
	int fogX = -1, fogY = -1;
	const std::vector<Uint16> *voxels = nullptr;
	SDL_Color pal[256];
	glm::vec3 focus{0.f}, focusTarget{0.f};
	bool focusInit = false;
	float tileSize = 0.055f;
	float yaw = 0.f;
	int viewLevel = 0;
	std::unordered_map<const BattleUnit*, UnitCard> cards;
	std::unique_ptr<Surface> spriteSurf, spriteSurf2;
	double animTime = 0.0;

	// hover
	bool hoverValid = false, hoverGlobe = false;
	Position hoverTile;
	glm::vec3 hoverPoint{0.f};
	double hoverLon = 0.0, hoverLat = 0.0;

	// meshes
	Mesh quad, disk, ring, cursor, dot, nightCap;

	// grabbing
	bool held[2] = {false, false};
	glm::vec3 lastHand[2];

	// ---- geoscape
	Mesh globeLand, globeOcean;
	bool globeBuilt = false;
	float globeRadius = 0.30f;
	glm::vec3 globeCenter{0.f};
	double cenLon = 0.0, cenLat = 0.0;
	Globe *globe = nullptr;
	GeoscapeState *geo = nullptr;

	float surfaceY() const { return layout.tableCenter.y - 0.02f; }
	glm::mat4 boardMatrix() const
	{
		float k = tileSize / TILE_W;
		return glm::translate(glm::mat4(1.f), {layout.tableCenter.x, surfaceY(), layout.tableCenter.z})
			* glm::rotate(glm::mat4(1.f), yaw, {0, 1, 0})
			* glm::scale(glm::mat4(1.f), glm::vec3(k))
			* glm::translate(glm::mat4(1.f), {-focus.x, 0.f, -focus.z});
	}
	glm::vec4 clipRect() const
	{
		glm::vec3 c = layout.tableCenter;
		float hx = layout.tableSize.x * 0.5f, hz = layout.tableSize.y * 0.5f;
		return {c.x - hx, c.z - hz, c.x + hx, c.z + hz};
	}
	glm::mat4 globeMatrix() const
	{
		// rotate so the game's globe center faces the player (+Z)
		return glm::translate(glm::mat4(1.f), globeCenter)
			* glm::rotate(glm::mat4(1.f), (float)-cenLat, {1, 0, 0})
			* glm::rotate(glm::mat4(1.f), (float)-cenLon, {0, 1, 0})
			* glm::scale(glm::mat4(1.f), glm::vec3(globeRadius));
	}

	void resetBattle();
	void bindBattle(SavedBattleGame *save);
	uint32_t sample(const Surface *spr, uint32_t fallback, float px, float py);
	const Proto &proto(const MapData *md);
	void buildProto(const MapData *md, Proto &p);
	uint64_t chunkHash(int cx, int cy, int z);
	void buildChunk(int cx, int cy, int z, Chunk &ch);
	void updateBattle(float dt);
	void drawBattle(const Shader &sh);
	void drawUnits(const Shader &sh);
	glm::vec3 unitVoxelPos(const BattleUnit *u);
	void renderUnitCard(BattleUnit *u, UnitCard &card, int apparentOffset);
	void buildGlobe();
	void drawGlobe(const Shader &sh);
};

// ================================================================== battlescape

void Board::Impl::resetBattle()
{
	protos.clear();
	chunks.clear();
	cards.clear();
	atlas.reset();
	fogX = fogY = -1;
	battle = nullptr;
	firstTile = nullptr;
	focusInit = false;
	hoverValid = false;
}

void Board::Impl::bindBattle(SavedBattleGame *save)
{
	resetBattle();
	battle = save;
	firstTile = save->getTile(Position(0, 0, 0));
	mx = save->getMapSizeX();
	my = save->getMapSizeY();
	mz = save->getMapSizeZ();
	cxN = (mx + CHUNK - 1) / CHUNK;
	cyN = (my + CHUNK - 1) / CHUNK;
	chunks.resize((size_t)cxN * cyN * mz);
	voxels = game->getMod()->getVoxelData();
	// the palette the battlescape is drawn with (TFTD depth palettes included)
	SDL_Color *p = bs ? bs->getPalette() : nullptr;
	if (!p)
	{
		Palette *bp = game->getMod()->getPalette("PAL_BATTLESCAPE", false);
		p = bp ? bp->getColors() : nullptr;
	}
	if (p) std::copy(p, p + 256, pal);
	// fog-of-war swatch
	int ax, ay;
	if (atlas.alloc(18, 18, ax, ay))
	{
		for (int j = 0; j < 18; ++j)
			for (int i = 0; i < 18; ++i)
			{
				bool edge = i <= 1 || j <= 1 || i >= 16 || j >= 16;
				atlas.put(ax + i, ay + j, edge ? 0xFF4A3F37u : 0xFF3A302Au);
			}
		fogX = ax + 1; fogY = ay + 1;
	}
	spriteSurf.reset(new Surface(32, 40));
	spriteSurf2.reset(new Surface(64, 56));
	Log(LOG_INFO) << "[VR] battle diorama bound: map " << mx << "x" << my << "x" << mz;
}

uint32_t Board::Impl::sample(const Surface *spr, uint32_t fallback, float px, float py)
{
	int x = (int)std::floor(px), y = (int)std::floor(py);
	static const int search[][2] = {{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}, {0, -2}, {0, 2}, {-1, -1}, {1, 1}, {0, -3}, {0, 3}, {0, -4}};
	for (auto &o : search)
	{
		int sx = x + o[0], sy = y + o[1];
		if (sx < 0 || sy < 0 || sx >= spr->getWidth() || sy >= spr->getHeight()) continue;
		Uint8 c = spr->getPixel(sx, sy);
		if (c) return rgba(pal[c]);
	}
	return fallback;
}

void Board::Impl::buildProto(const MapData *md, Proto &p)
{
	const Surface *spr = nullptr;
	if (md->getDataset() && md->getDataset()->getSurfaceset())
		spr = static_cast<const SurfaceSet*>(md->getDataset()->getSurfaceset())->getFrame(md->getSprite(0));
	if (!spr) return;
	const int yoff = md->getYOffset();
	const bool isFloor = md->getObjectType() == O_FLOOR;

	// average color, for faces whose projection falls on transparent pixels
	uint64_t sr = 0, sg = 0, sb = 0, n = 0;
	for (int y = 0; y < spr->getHeight(); ++y)
		for (int x = 0; x < spr->getWidth(); ++x)
			if (Uint8 c = spr->getPixel(x, y)) { sr += pal[c].r; sg += pal[c].g; sb += pal[c].b; ++n; }
	if (!n) return; // fully transparent sprite: nothing to show
	uint32_t avg = (uint32_t)(sr / n) | ((uint32_t)(sg / n) << 8) | ((uint32_t)(sb / n) << 16) | 0xFF000000u;

	// voxel occupancy
	static bool solid[TILE_W][TILE_W][TILE_H];
	int count = 0;
	for (int z = 0; z < TILE_H; ++z)
	{
		int loft = md->getLoftID(z / 2);
		for (int y = 0; y < TILE_W; ++y)
		{
			size_t idx = (size_t)loft * 16 + y;
			Uint16 row = (loft && voxels && idx < voxels->size()) ? (*voxels)[idx] : 0;
			for (int x = 0; x < TILE_W; ++x)
			{
				solid[x][y][z] = (row & (1 << (15 - x))) != 0;
				count += solid[x][y][z];
			}
		}
	}
	auto S = [&](int x, int y, int z) -> bool
	{
		if (x < 0 || y < 0 || z < 0 || x >= TILE_W || y >= TILE_W || z >= TILE_H) return false;
		return solid[x][y][z];
	};

	if (count == 0)
	{
		// decorative part without collision: two crossed cards showing the sprite
		int w = spr->getWidth(), h = spr->getHeight(), ax, ay;
		if (!atlas.alloc(w + 2, h + 2, ax, ay)) return;
		for (int j = -1; j <= h; ++j)
			for (int i = -1; i <= w; ++i)
			{
				int sx = glm::clamp(i, 0, w - 1), sy = glm::clamp(j, 0, h - 1);
				Uint8 c = spr->getPixel(sx, sy);
				atlas.put(ax + 1 + i, ay + 1 + j, c ? rgba(pal[c]) : 0u);
			}
		float u0 = (ax + 1) / (float)ATLAS, v0 = (ay + 1) / (float)ATLAS;
		float u1 = (ax + 1 + w) / (float)ATLAS, v1 = (ay + 1 + h) / (float)ATLAS;
		// sprite row 32 sits on the floor at the tile center; 1 px ~ 1 voxel
		float top = 32.f + yoff, bottom = top - h;
		float half = w * 0.35f;
		glm::vec3 c(8.f, 0.f, 8.f);
		glm::vec3 dirs[2] = {glm::normalize(glm::vec3(1, 0, -1)), glm::normalize(glm::vec3(1, 0, 1))};
		for (auto &d : dirs)
		{
			uint32_t b = (uint32_t)p.verts.size();
			glm::vec3 nrm = glm::normalize(glm::cross(d, glm::vec3(0, 1, 0)));
			glm::vec4 white(1.f);
			p.verts.push_back({c - d * half + glm::vec3(0, bottom, 0), nrm, {u0, v1}, white, MAT_BOARD});
			p.verts.push_back({c + d * half + glm::vec3(0, bottom, 0), nrm, {u1, v1}, white, MAT_BOARD});
			p.verts.push_back({c + d * half + glm::vec3(0, top, 0), nrm, {u1, v0}, white, MAT_BOARD});
			p.verts.push_back({c - d * half + glm::vec3(0, top, 0), nrm, {u0, v0}, white, MAT_BOARD});
			p.idx.insert(p.idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
		}
		p.card = true;
		return;
	}

	// ---- greedy meshing of exposed faces, per direction and slice
	struct Dir { int axis, sign; };
	const Dir dirs[5] = {{0, 1}, {0, -1}, {1, 1}, {1, -1}, {2, 1}}; // no bottom faces: never seen from above
	const int dims[3] = {TILE_W, TILE_W, TILE_H};
	static uint32_t maskCol[TILE_H][TILE_H];
	static bool mask[TILE_H][TILE_H], used[TILE_H][TILE_H];
	for (const Dir &d : dirs)
	{
		// (u, v) axes spanning the face plane
		int ua = d.axis == 0 ? 1 : 0;
		int va = d.axis == 2 ? 1 : 2;
		int nu = dims[ua], nv = dims[va];
		for (int k = 0; k < dims[d.axis]; ++k)
		{
			bool any = false;
			for (int v = 0; v < nv; ++v)
				for (int u = 0; u < nu; ++u)
				{
					int c[3];
					c[d.axis] = k; c[ua] = u; c[va] = v;
					int nb[3] = {c[0], c[1], c[2]};
					nb[d.axis] += d.sign;
					bool m = S(c[0], c[1], c[2]) && !S(nb[0], nb[1], nb[2]);
					mask[v][u] = m;
					used[v][u] = false;
					if (!m) continue;
					any = true;
					// face center in voxel coords (x east, y south, z up), projected into the iso sprite
					float f[3] = {c[0] + 0.5f, c[1] + 0.5f, c[2] + 0.5f};
					f[d.axis] += 0.5f * d.sign;
					float fz = f[2];
					if (isFloor && d.axis == 2) fz = 0.f; // floor sprites are drawn at height 0
					float px = 16.f + (f[0] - f[1]);
					float py = 24.f + (f[0] + f[1]) * 0.5f - fz + yoff;
					maskCol[v][u] = sample(spr, avg, px, py);
				}
			if (!any) continue;
			for (int v = 0; v < nv; ++v)
				for (int u = 0; u < nu; ++u)
				{
					if (!mask[v][u] || used[v][u]) continue;
					int w = 1;
					while (u + w < nu && mask[v][u + w] && !used[v][u + w]) ++w;
					int h = 1;
					bool ok = true;
					while (v + h < nv && ok)
					{
						for (int i = 0; i < w; ++i)
							if (!mask[v + h][u + i] || used[v + h][u + i]) { ok = false; break; }
						if (ok) ++h;
					}
					for (int j = 0; j < h; ++j)
						for (int i = 0; i < w; ++i) used[v + j][u + i] = true;
					int ax, ay;
					if (!atlas.alloc(w + 2, h + 2, ax, ay)) return;
					for (int j = -1; j <= h; ++j)
						for (int i = -1; i <= w; ++i)
						{
							int si = glm::clamp(i, 0, w - 1), sj = glm::clamp(j, 0, h - 1);
							atlas.put(ax + 1 + i, ay + 1 + j, maskCol[v + sj][u + si]);
						}
					float plane = (float)k + (d.sign > 0 ? 1.f : 0.f);
					auto corner = [&](float uu, float vv) -> glm::vec3
					{
						float cc[3];
						cc[d.axis] = plane; cc[ua] = uu; cc[va] = vv;
						return glm::vec3(cc[0], cc[2], cc[1]); // to (X east, Y up, Z south)
					};
					float nn[3] = {0, 0, 0};
					nn[d.axis] = (float)d.sign;
					glm::vec3 nrm(nn[0], nn[2], nn[1]);
					float tu0 = (ax + 1) / (float)ATLAS, tv0 = (ay + 1) / (float)ATLAS;
					float tu1 = (ax + 1 + w) / (float)ATLAS, tv1 = (ay + 1 + h) / (float)ATLAS;
					glm::vec4 white(1.f);
					uint32_t b = (uint32_t)p.verts.size();
					p.verts.push_back({corner((float)u, (float)v), nrm, {tu0, tv0}, white, MAT_BOARD});
					p.verts.push_back({corner((float)(u + w), (float)v), nrm, {tu1, tv0}, white, MAT_BOARD});
					p.verts.push_back({corner((float)(u + w), (float)(v + h)), nrm, {tu1, tv1}, white, MAT_BOARD});
					p.verts.push_back({corner((float)u, (float)(v + h)), nrm, {tu0, tv1}, white, MAT_BOARD});
					p.idx.insert(p.idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
				}
		}
	}
}

const Proto &Board::Impl::proto(const MapData *md)
{
	auto it = protos.find(md);
	if (it != protos.end()) return it->second;
	Proto &p = protos[md];
	buildProto(md, p);
	return p;
}

uint64_t Board::Impl::chunkHash(int cx, int cy, int z)
{
	uint64_t h = 1469598103934665603ull;
	auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
	for (int y = cy * CHUNK; y < std::min(my, (cy + 1) * CHUNK); ++y)
		for (int x = cx * CHUNK; x < std::min(mx, (cx + 1) * CHUNK); ++x)
		{
			Tile *t = battle->getTile(Position(x, y, z));
			if (!t) continue;
			for (int part = 0; part < O_MAX; ++part)
			{
				mix((uint64_t)(uintptr_t)t->getMapData((TilePart)part));
				mix(t->isDiscovered((TilePart)part));
				mix(t->isUfoDoorOpen((TilePart)part));
			}
			mix((uint64_t)t->getShade());
		}
	return h;
}

void Board::Impl::buildChunk(int cx, int cy, int z, Chunk &ch)
{
	MeshData md;
	for (int y = cy * CHUNK; y < std::min(my, (cy + 1) * CHUNK); ++y)
		for (int x = cx * CHUNK; x < std::min(mx, (cx + 1) * CHUNK); ++x)
		{
			Tile *t = battle->getTile(Position(x, y, z));
			if (!t) continue;
			glm::vec3 off((float)(x * TILE_W), (float)(z * TILE_H), (float)(y * TILE_W));
			if (!t->isDiscovered(O_FLOOR))
			{
				// unexplored ground: a dark slab so the board keeps its shape
				if (z == 0 && fogX >= 0)
				{
					float u0 = fogX / (float)ATLAS, v0 = fogY / (float)ATLAS, u1 = (fogX + 16) / (float)ATLAS, v1 = (fogY + 16) / (float)ATLAS;
					md.addQuad(off + glm::vec3(0, 0.5f, TILE_W), off + glm::vec3(TILE_W, 0.5f, TILE_W), off + glm::vec3(TILE_W, 0.5f, 0), off + glm::vec3(0, 0.5f, 0), glm::vec4(1.f), MAT_BOARD, {u0, v1}, {u1, v1}, {u1, v0}, {u0, v0});
				}
				continue;
			}
			// shade 0 (bright) .. 15 (dark), like the flat map
			float b = 1.f - glm::clamp(t->getShade(), 0, 15) / 15.f * 0.82f;
			glm::vec4 tint(b, b, b, 1.f);
			for (int part = 0; part < O_MAX; ++part)
			{
				TilePart tp = (TilePart)part;
				const MapData *d = t->getMapData(tp);
				if (!d) continue;
				if ((tp == O_WESTWALL || tp == O_NORTHWALL) && !t->isDiscovered(tp)) continue;
				if (t->isUfoDoorOpen(tp)) continue;
				const Proto &p = proto(d);
				if (p.verts.empty()) continue;
				uint32_t base = (uint32_t)md.verts.size();
				for (const Vertex &v : p.verts)
				{
					Vertex w = v;
					w.pos += off;
					w.color = tint;
					md.verts.push_back(w);
				}
				for (uint32_t i : p.idx) md.indices.push_back(base + i);
			}
		}
	ch.empty = md.indices.empty();
	if (!ch.mesh) ch.mesh.reset(new Mesh());
	ch.mesh->upload(md);
}

glm::vec3 Board::Impl::unitVoxelPos(const BattleUnit *u)
{
	int size = u->getArmor()->getSize();
	auto level = [&](Position p) -> float
	{
		float lvl = 0.f;
		for (int x = 0; x < size; ++x)
			for (int y = 0; y < size; ++y)
			{
				Tile *t = battle->getTile(p + Position(x, y, 0));
				if (t) lvl = std::min(lvl, (float)t->getTerrainLevel());
			}
		return lvl;
	};
	auto at = [&](Position p) -> glm::vec3
	{
		return glm::vec3(p.x * TILE_W + TILE_W * size * 0.5f, p.z * TILE_H - level(p), p.y * TILE_W + TILE_W * size * 0.5f);
	};
	UnitStatus st = u->getStatus();
	if (st == STATUS_WALKING || st == STATUS_FLYING)
	{
		int dir = u->getDirection();
		int phase = u->getWalkingPhase() + u->getDiagonalWalkingPhase();
		int midphase = 4 + 4 * (dir % 2), endphase = 8 + 8 * (dir % 2);
		if (u->getVerticalDirection()) { midphase = 4; endphase = 8; }
		Position start = phase < midphase ? u->getPosition() : u->getLastPosition();
		Position end = u->getDestination();
		float t = glm::clamp((float)phase / endphase, 0.f, 1.f);
		return glm::mix(at(start), at(end), t);
	}
	return at(u->getPosition());
}

void Board::Impl::renderUnitCard(BattleUnit *u, UnitCard &card, int apparentOffset)
{
	int size = u->getArmor()->getSize();
	Surface *surf = size > 1 ? spriteSurf2.get() : spriteSurf.get();
	surf->setPalette(pal);
	surf->clear();
	int frame = (int)(animTime * 8.0) % 8;
	BattleUnit::FacingSnapshot saved = u->getFacingSnapshot();
	BattleUnit::FacingSnapshot view = saved;
	auto rot = [&](int d) { return ((d + apparentOffset) % 8 + 8) % 8; };
	view.direction = rot(saved.direction);
	view.toDirection = rot(saved.toDirection);
	view.turret = rot(saved.turret);
	view.toTurret = rot(saved.toTurret);
	view.face = saved.face >= 0 ? rot(saved.face) : saved.face;
	u->setFacingSnapshot(view);
	{
		UnitSprite us(surf, game->getMod(), battle, frame, battle->getDepth() != 0, 0, 0);
		GraphSubset all(surf->getWidth(), surf->getHeight());
		if (size == 1)
		{
			us.draw(u, 0, 0, 0, 0, all, false);
		}
		else
		{
			// the four parts of a 2x2 unit, laid out like the flat map does
			const int ox[4] = {16, 32, 0, 16}, oy[4] = {0, 8, 8, 16};
			for (int part = 0; part < 4; ++part)
				us.draw(u, part, ox[part], oy[part], 0, all, false);
		}
	}
	u->setFacingSnapshot(saved);

	card.w = surf->getWidth();
	card.h = surf->getHeight();
	card.anchorY = size > 1 ? 40 : 32;
	card.px.resize((size_t)card.w * card.h);
	for (int y = 0; y < card.h; ++y)
		for (int x = 0; x < card.w; ++x)
		{
			Uint8 c = surf->getPixel(x, y);
			card.px[(size_t)y * card.w + x] = c ? rgba(pal[c]) : 0u;
		}
	if (!card.tex) { card.tex.reset(new VR::Texture()); card.tex->create(card.w, card.h, false, false); }
	card.tex->update(card.px.data(), card.w, card.h);
}

void Board::Impl::updateBattle(float dt)
{
	animTime += dt;
	Camera *cam = (bs && bs->getMap()) ? bs->getMap()->getCamera() : nullptr;
	if (cam)
	{
		viewLevel = cam->getViewLevel();
		Position c = cam->getCenterPosition();
		focusTarget = glm::vec3(c.x * TILE_W + TILE_W * 0.5f, 0.f, c.y * TILE_W + TILE_W * 0.5f);
	}
	else
	{
		viewLevel = mz - 1;
	}
	if (!focusInit) { focus = focusTarget; focusInit = true; }
	if (!held[0] && !held[1])
	{
		float a = 1.f - std::exp(-dt * 5.f);
		focus += (focusTarget - focus) * a;
	}

	// rebuild changed chunks; after the first build only a few per frame, to avoid hitches
	int budget = 6;
	for (int z = 0; z < mz; ++z)
		for (int cy = 0; cy < cyN; ++cy)
			for (int cx = 0; cx < cxN; ++cx)
			{
				Chunk &ch = chunks[((size_t)z * cyN + cy) * cxN + cx];
				uint64_t h = chunkHash(cx, cy, z);
				if (h == ch.hash) continue;
				if (ch.mesh && budget-- <= 0) continue;
				buildChunk(cx, cy, z, ch);
				ch.hash = h;
			}
	if (atlas.full)
	{
		Log(LOG_WARNING) << "[VR] terrain atlas full; rebuilding";
		SavedBattleGame *b = battle;
		bindBattle(b);
		return;
	}
	atlas.upload();

	// unit cards, drawn for the angle the viewer sees each unit from
	glm::mat4 inv = glm::inverse(boardMatrix());
	glm::vec3 viewerVox = glm::vec3(inv * glm::vec4(viewer, 1.f));
	for (auto it = cards.begin(); it != cards.end();)
	{
		bool alive = false;
		for (BattleUnit *u : *battle->getUnits()) if (u == it->first) { alive = true; break; }
		it = alive ? std::next(it) : cards.erase(it);
	}
	for (BattleUnit *u : *battle->getUnits())
	{
		if (u->isOut()) continue;
		if (!(u->getFaction() == FACTION_PLAYER || u->getVisible() || battle->getDebugMode())) continue;
		glm::vec3 p = unitVoxelPos(u);
		glm::vec2 toViewer(viewerVox.x - p.x, viewerVox.z - p.z);
		// sector the viewer stands in, seen from the unit (0 = north, clockwise)
		float ang = std::atan2(toViewer.x, -toViewer.y);
		int sector = ((int)std::lround(ang / (3.14159265f / 4.f)) % 8 + 8) % 8;
		// the flat map's camera sits to the south-east (sector 3)
		renderUnitCard(u, cards[u], 3 - sector);
	}
}

void Board::Impl::drawUnits(const Shader &sh)
{
	glm::mat4 M = boardMatrix();
	glm::mat4 inv = glm::inverse(M);
	glm::vec3 viewerVox = glm::vec3(inv * glm::vec4(viewer, 1.f));
	BattleUnit *sel = battle->getSelectedUnit();
	for (BattleUnit *u : *battle->getUnits())
	{
		auto it = cards.find(u);
		if (it == cards.end() || !it->second.tex) continue;
		if (u->isOut()) continue;
		if (!(u->getFaction() == FACTION_PLAYER || u->getVisible() || battle->getDebugMode())) continue;
		if (u->getPosition().z > viewLevel) continue;
		const UnitCard &card = it->second;
		glm::vec3 p = unitVoxelPos(u);
		int size = u->getArmor()->getSize();

		// miniature base in the faction's color
		glm::vec4 fc = u->getFaction() == FACTION_PLAYER ? glm::vec4(0.15f, 0.45f, 1.f, 1.f)
			: u->getFaction() == FACTION_HOSTILE ? glm::vec4(0.95f, 0.18f, 0.12f, 1.f) : glm::vec4(0.2f, 0.85f, 0.3f, 1.f);
		sh.set("uMode", 2);
		float r = 5.5f * size;
		sh.set("uModel", M * glm::translate(glm::mat4(1.f), p + glm::vec3(0, 0.1f, 0)) * glm::scale(glm::mat4(1.f), {r, 1.2f, r}));
		sh.set("uTint", fc * glm::vec4(0.6f, 0.6f, 0.6f, 1.f));
		disk.draw();
		if (u == sel)
		{
			float pulse = 0.75f + 0.25f * (float)std::sin(time * 5.0);
			sh.set("uModel", M * glm::translate(glm::mat4(1.f), p + glm::vec3(0, 1.4f, 0)) * glm::scale(glm::mat4(1.f), glm::vec3(r * 1.3f, 1.f, r * 1.3f)));
			sh.set("uTint", glm::vec4(1.f, 0.9f, 0.3f, 1.f) * pulse);
			ring.draw();
		}

		// the standee, turned to face the viewer
		glm::vec2 tv(viewerVox.x - p.x, viewerVox.z - p.z);
		float face = std::atan2(tv.x, tv.y);
		float w = (float)card.w, h = (float)card.h;
		float lift = 1.3f;
		glm::mat4 m = M * glm::translate(glm::mat4(1.f), p + glm::vec3(0, lift, 0))
			* glm::rotate(glm::mat4(1.f), face, {0, 1, 0})
			* glm::translate(glm::mat4(1.f), {0.f, card.anchorY - h * 0.5f, 0.f})
			* glm::scale(glm::mat4(1.f), {w, h, 1.f});
		sh.set("uMode", 3);
		sh.set("uTint", glm::vec4(1.f));
		sh.set("uModel", m);
		card.tex->bind(0);
		quad.draw();
	}
	sh.set("uTint", glm::vec4(1.f));
}

void Board::Impl::drawBattle(const Shader &sh)
{
	glm::mat4 M = boardMatrix();
	sh.set("uClip", clipRect());

	// terrain
	sh.set("uMode", 3);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", M);
	atlas.tex.bind(0);
	for (int z = 0; z <= std::min(viewLevel, mz - 1); ++z)
		for (int cy = 0; cy < cyN; ++cy)
			for (int cx = 0; cx < cxN; ++cx)
			{
				const Chunk &ch = chunks[((size_t)z * cyN + cy) * cxN + cx];
				if (ch.mesh && !ch.empty) ch.mesh->draw();
			}

	drawUnits(sh);

	// path preview markers
	sh.set("uMode", 2);
	for (int z = 0; z <= std::min(viewLevel, mz - 1); ++z)
		for (int y = 0; y < my; ++y)
			for (int x = 0; x < mx; ++x)
			{
				Tile *t = battle->getTile(Position(x, y, z));
				if (!t || t->getPreview() == -1 || !t->isDiscovered(O_FLOOR)) continue;
				glm::vec3 c(x * TILE_W + 8.f, z * TILE_H - t->getTerrainLevel() + 1.2f, y * TILE_W + 8.f);
				glm::vec4 col = t->getTUMarker() >= 0 ? glm::vec4(0.3f, 1.f, 0.5f, 1.f) : glm::vec4(1.f, 0.35f, 0.2f, 1.f);
				sh.set("uModel", M * glm::translate(glm::mat4(1.f), c) * glm::scale(glm::mat4(1.f), glm::vec3(1.6f, 0.6f, 1.6f)));
				sh.set("uTint", col);
				dot.draw();
			}

	if (bs && bs->getMap())
	{
		Map *map = bs->getMap();
		// projectile in flight
		if (Projectile *pr = map->getProjectile())
		{
			Position v = pr->getPosition(0);
			sh.set("uModel", M * glm::translate(glm::mat4(1.f), glm::vec3(v.x, v.z, v.y)) * glm::scale(glm::mat4(1.f), glm::vec3(1.4f)));
			sh.set("uTint", glm::vec4(3.f, 2.8f, 1.5f, 1.f));
			dot.draw();
		}
		// explosions: expanding glowing balls
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glDepthMask(GL_FALSE);
		for (Explosion *e : *map->getExplosions())
		{
			Position v = e->getPosition();
			float f = (float)std::max(0, e->getCurrentFrame());
			float r = e->isBig() ? 6.f + f * 3.f : 2.f + f * 1.2f;
			float a = glm::clamp(1.f - f / (e->isBig() ? 8.f : 5.f), 0.1f, 1.f);
			sh.set("uModel", M * glm::translate(glm::mat4(1.f), glm::vec3(v.x, v.z, v.y)) * glm::scale(glm::mat4(1.f), glm::vec3(r)));
			sh.set("uTint", glm::vec4(1.f, 0.55f, 0.15f, a));
			dot.draw();
		}
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}

	// hover cursor
	if (hoverValid && !hoverGlobe)
	{
		Tile *t = battle->getTile(hoverTile);
		float lvl = t ? (float)t->getTerrainLevel() : 0.f;
		glm::vec3 c(hoverTile.x * TILE_W, hoverTile.z * TILE_H - lvl + 0.6f, hoverTile.y * TILE_W);
		CursorType ct = (bs && bs->getMap()) ? bs->getMap()->getCursorType() : CT_NORMAL;
		bool aim = ct == CT_AIM || ct == CT_PSI || ct == CT_THROW || ct == CT_WAYPOINT;
		glm::vec4 col = aim ? glm::vec4(2.5f, 0.5f, 0.4f, 1.f) : glm::vec4(2.2f, 2.0f, 0.6f, 1.f);
		sh.set("uMode", 2);
		sh.set("uModel", M * glm::translate(glm::mat4(1.f), c));
		sh.set("uTint", col);
		cursor.draw();
	}
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
}

// ================================================================== geoscape globe

static glm::vec3 lonLatToUnit(double lon, double lat)
{
	// latitude is positive towards the south in OpenXcom
	return glm::vec3((float)(std::cos(lat) * std::sin(lon)), (float)(-std::sin(lat)), (float)(std::cos(lat) * std::cos(lon)));
}

void Board::Impl::buildGlobe()
{
	Mod *mod = game->getMod();
	SurfaceSet *tex = mod->getSurfaceSet("TEXTURE.DAT", false);
	// the palette the geoscape screen is actually drawn with
	SDL_Color *gpal = geo ? geo->getPalette() : nullptr;
	Palette *gp = mod->getPalette("PAL_GEOSCAPE", false);
	if (!gpal) gpal = gp ? gp->getColors() : pal;
	auto texColor = [&](int id) -> glm::vec4
	{
		glm::vec4 def(0.25f, 0.45f, 0.2f, 1.f);
		if (!tex) return def;
		const Surface *s = tex->getFrame(id);
		if (!s) return def;
		uint64_t r = 0, g = 0, b = 0, n = 0;
		for (int y = 0; y < s->getHeight(); ++y)
			for (int x = 0; x < s->getWidth(); ++x)
				if (Uint8 c = s->getPixel(x, y)) { r += gpal[c].r; g += gpal[c].g; b += gpal[c].b; ++n; }
		if (!n) return def;
		return glm::vec4(r / (255.f * n), g / (255.f * n), b / (255.f * n), 1.f);
	};
	std::map<int, glm::vec4> colors;
	MeshData land;
	for (Polygon *p : *mod->getGlobe()->getPolygons())
	{
		int t = p->getTexture();
		if (!colors.count(t)) colors[t] = texColor(t);
		glm::vec4 c = colors[t];
		int n = p->getPoints();
		if (n < 3) continue;
		// fan triangulation from the centroid, then recursive subdivision so every
		// triangle hugs the sphere (large polygons would otherwise sink into the ocean)
		glm::vec3 center(0.f);
		for (int i = 0; i < n; ++i) center += lonLatToUnit(p->getLongitude(i), p->getLatitude(i));
		center = glm::normalize(center);
		std::function<void(glm::vec3, glm::vec3, glm::vec3, int)> tri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c2, int depth)
		{
			float maxEdge = std::max(glm::length(a - b), std::max(glm::length(b - c2), glm::length(c2 - a)));
			if (maxEdge > 0.035f && depth < 6)
			{
				glm::vec3 ab = glm::normalize(a + b), bc = glm::normalize(b + c2), ca = glm::normalize(c2 + a);
				tri(a, ab, ca, depth + 1);
				tri(ab, b, bc, depth + 1);
				tri(ca, bc, c2, depth + 1);
				tri(ab, bc, ca, depth + 1);
				return;
			}
			uint32_t base = (uint32_t)land.verts.size();
			for (const glm::vec3 &u : {a, b, c2}) land.verts.push_back(Vertex{u * 1.002f, u, {0, 0}, c, 0});
			land.indices.insert(land.indices.end(), {base, base + 1, base + 2});
		};
		// ear clipping in the tangent plane at the centroid (polygons can be concave)
		std::vector<glm::vec3> pts;
		for (int i = 0; i < n; ++i) pts.push_back(lonLatToUnit(p->getLongitude(i), p->getLatitude(i)));
		glm::vec3 ex = glm::normalize(glm::cross(std::fabs(center.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), center));
		glm::vec3 ey = glm::cross(center, ex);
		std::vector<glm::vec2> q;
		for (auto &v : pts) q.push_back(glm::vec2(glm::dot(v, ex), glm::dot(v, ey)));
		float area = 0.f;
		for (size_t i = 0; i < q.size(); ++i) { const glm::vec2 &a = q[i], &b = q[(i + 1) % q.size()]; area += a.x * b.y - b.x * a.y; }
		std::vector<int> idx;
		for (int i = 0; i < n; ++i) idx.push_back(i);
		if (area < 0.f) std::reverse(idx.begin(), idx.end());
		auto cross2 = [](glm::vec2 a, glm::vec2 b, glm::vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
		int guard = 0;
		while (idx.size() > 3 && guard++ < 64)
		{
			bool clipped = false;
			for (size_t i = 0; i < idx.size(); ++i)
			{
				int ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
				if (cross2(q[ia], q[ib], q[ic]) <= 0.f) continue; // reflex corner
				bool inside = false;
				for (int j : idx)
				{
					if (j == ia || j == ib || j == ic) continue;
					if (cross2(q[ia], q[ib], q[j]) > 0.f && cross2(q[ib], q[ic], q[j]) > 0.f && cross2(q[ic], q[ia], q[j]) > 0.f) { inside = true; break; }
				}
				if (inside) continue;
				tri(pts[ia], pts[ib], pts[ic], 0);
				idx.erase(idx.begin() + i);
				clipped = true;
				break;
			}
			if (!clipped) break;
		}
		if (idx.size() >= 3)
		{
			for (size_t i = 1; i + 1 < idx.size(); ++i) tri(pts[idx[0]], pts[idx[i]], pts[idx[i + 1]], 0);
		}
	}
	globeLand.upload(land);
	MeshData ocean;
	ocean.addSphere({0, 0, 0}, 0.998f, 96, 48, glm::vec4(0.06f, 0.18f, 0.40f, 1.f), 0);
	globeOcean.upload(ocean);
	globeBuilt = true;
	Log(LOG_INFO) << "[VR] globe built: " << land.verts.size() << " vertices";
}

void Board::Impl::drawGlobe(const Shader &sh)
{
	glm::mat4 G = globeMatrix();
	SavedGame *sg = game->getSavedGame();
	glm::vec3 sun(0, 0, 1);
	if (sg && sg->getTime())
	{
		// the sun is overhead at longitude 0 at noon GMT
		double hours = sg->getTime()->getHour() + sg->getTime()->getMinute() / 60.0;
		sun = lonLatToUnit((12.0 - hours) / 24.0 * 6.283185307, 0.0);
	}
	glm::vec3 sunWorld = glm::normalize(glm::vec3(G * glm::vec4(sun, 0.f)));
	sh.set("uMode", 5);
	sh.set("uSun", sunWorld);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", G);
	globeOcean.draw();
	// land polygons overlap like layers of paint: draw them in the game's order without
	// writing depth (the ocean's depth still hides the far side)
	glDepthMask(GL_FALSE);
	globeLand.draw();
	glDepthMask(GL_TRUE);

	if (!sg) return;
	auto marker = [&](double lon, double lat, glm::vec4 col, float size)
	{
		glm::vec3 u = lonLatToUnit(lon, lat);
		sh.set("uModel", G * glm::translate(glm::mat4(1.f), u * 1.015f) * glm::scale(glm::mat4(1.f), glm::vec3(size)));
		sh.set("uTint", col);
		dot.draw();
	};
	float pulse = 0.7f + 0.3f * (float)std::sin(time * 6.0);
	for (Base *b : *sg->getBases())
	{
		marker(b->getLongitude(), b->getLatitude(), glm::vec4(0.3f, 0.6f, 1.f, 1.f), 0.025f);
		for (Craft *c : *b->getCrafts())
			if (c->getStatus() == "STR_OUT")
				marker(c->getLongitude(), c->getLatitude(), glm::vec4(0.3f, 1.f, 0.4f, 1.f), 0.018f);
	}
	for (Ufo *u : *sg->getUfos())
		if (u->getDetected() && u->getStatus() != Ufo::DESTROYED)
			marker(u->getLongitude(), u->getLatitude(), glm::vec4(1.f, 0.2f, 0.15f, 1.f) * pulse, 0.02f);
	for (AlienBase *a : *sg->getAlienBases())
		if (a->isDiscovered())
			marker(a->getLongitude(), a->getLatitude(), glm::vec4(0.8f, 0.3f, 1.f, 1.f), 0.022f);
	for (MissionSite *m : *sg->getMissionSites())
		if (m->getDetected())
			marker(m->getLongitude(), m->getLatitude(), glm::vec4(1.f, 0.6f, 0.1f, 1.f) * pulse, 0.02f);
	if (hoverValid && hoverGlobe)
	{
		sh.set("uModel", glm::translate(glm::mat4(1.f), hoverPoint) * glm::scale(glm::mat4(1.f), glm::vec3(0.008f)));
		sh.set("uTint", glm::vec4(1.f, 1.f, 0.4f, 1.f));
		dot.draw();
	}
	sh.set("uTint", glm::vec4(1.f));
}

// ================================================================== Board API

static void revalidate(Board::Impl &p);

Board::Board() : _p(new Impl) {}
Board::~Board() {}

void Board::init(const RoomLayout &layout)
{
	_p->layout = layout;
	MeshData q;
	q.addQuad({-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}, glm::vec4(1), MAT_BOARD, {0, 1}, {1, 1}, {1, 0}, {0, 0});
	_p->quad.upload(q);
	MeshData d;
	d.addCylinder({0, 0, 0}, 1.f, 1.f, 24, glm::vec4(1), 0);
	_p->disk.upload(d);
	MeshData r;
	for (int i = 0; i < 32; ++i)
	{
		float a0 = i / 32.f * 6.2831853f, a1 = (i + 1) / 32.f * 6.2831853f;
		glm::vec3 p0(std::cos(a0), 0, std::sin(a0)), p1(std::cos(a1), 0, std::sin(a1));
		r.addQuad(p0 * 0.8f, p1 * 0.8f, p1, p0, glm::vec4(1), 0);
	}
	_p->ring.upload(r);
	MeshData c;
	const float t = 1.6f, L = (float)TILE_W, H = 1.2f;
	c.addBox({0, 0, 0}, {L, H, t}, glm::vec4(1), 0);
	c.addBox({0, 0, L - t}, {L, H, L}, glm::vec4(1), 0);
	c.addBox({0, 0, 0}, {t, H, L}, glm::vec4(1), 0);
	c.addBox({L - t, 0, 0}, {L, H, L}, glm::vec4(1), 0);
	// corner posts, so the cursor reads from low angles too
	for (float px : {0.f, L - t})
		for (float pz : {0.f, L - t})
			c.addBox({px, 0, pz}, {px + t, 5.f, pz + t}, glm::vec4(1), 0);
	_p->cursor.upload(c);
	MeshData s;
	s.addSphere({0, 0, 0}, 1.f, 12, 8, glm::vec4(1), 0);
	_p->dot.upload(s);
	// night cap: the hemisphere facing -Z (rotated away from the sun when drawn)
	MeshData hemi;
	auto P = [](int ii, int jj)
	{
		float th = ii / 48.f * 6.2831853f, ph = 1.5707963f + jj / 12.f * 1.5707963f;
		return glm::vec3(std::sin(ph) * std::cos(th), std::sin(ph) * std::sin(th), std::cos(ph));
	};
	for (int j = 0; j < 12; ++j)
		for (int i = 0; i < 48; ++i)
			hemi.addQuad(P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1), glm::vec4(1), 0);
	_p->nightCap.upload(hemi);
}

void Board::setViewer(const glm::vec3 &head) { _p->viewer = head; }
bool Board::hasContent() const { return _p->hasBattle || _p->hasGlobe; }

void Board::update(Game *game, float dt)
{
	Impl &p = *_p;
	p.game = game;
	p.time += dt;
	SavedGame *sg = game->getSavedGame();
	SavedBattleGame *sb = sg ? sg->getSavedBattle() : nullptr;
	// the battlescape screen must really be alive (the saved battle keeps a stale pointer after it closes)
	BattlescapeState *live = nullptr;
	if (sb)
		for (State *s : game->getStates())
			if (auto *b = dynamic_cast<BattlescapeState*>(s)) { live = b; break; }
	bool battleReady = sb && live && live == sb->getBattleState() && sb->getMapSizeX() > 0;
	p.bs = battleReady ? live : nullptr;
	if (!battleReady)
	{
		if (p.battle) p.resetBattle();
		p.hasBattle = false;
	}
	else
	{
		if (sb != p.battle || sb->getTile(Position(0, 0, 0)) != p.firstTile) p.bindBattle(sb);
		p.hasBattle = true;
		p.updateBattle(dt);
	}

	// geoscape: find the GeoscapeState on the stack
	p.geo = nullptr;
	p.globe = nullptr;
	if (!p.hasBattle && sg)
	{
		for (State *s : game->getStates())
			if (auto *g = dynamic_cast<GeoscapeState*>(s)) { p.geo = g; p.globe = g->getGlobe(); break; }
	}
	p.hasGlobe = p.globe != nullptr;
	if (p.hasGlobe)
	{
		if (!p.globeBuilt) p.buildGlobe();
		p.globeCenter = p.layout.tableCenter + glm::vec3(0.f, 0.36f, 0.f);
		p.globe->getCenter(&p.cenLon, &p.cenLat);
	}
}

void Board::draw(const Shader &sh, const glm::mat4 &, const glm::vec3 &, double)
{
	if (_p->hasBattle && _p->atlas.tex.valid()) _p->drawBattle(sh);
	else if (_p->hasGlobe) _p->drawGlobe(sh);
}

bool Board::raycast(const glm::vec3 &o, const glm::vec3 &d, BoardHit &hit) const
{
	Impl &p = *_p;
	revalidate(p);
	if (p.hasBattle && (!p.game->getSavedGame() || p.game->getSavedGame()->getSavedBattle() != p.battle)) p.hasBattle = false;
	if (p.hasBattle)
	{
		glm::vec4 clip = p.clipRect();
		glm::mat4 inv = glm::inverse(p.boardMatrix());
		glm::vec3 lo = glm::vec3(inv * glm::vec4(o, 1.f));
		glm::vec3 ld = glm::vec3(inv * glm::vec4(d, 0.f));
		if (std::fabs(ld.y) < 1e-9f) return false;
		for (int z = std::min(p.viewLevel, p.mz - 1); z >= 0; --z)
		{
			float t = ((float)(z * TILE_H) - lo.y) / ld.y; // same t as along the world ray
			if (t <= 0.f) continue;
			glm::vec3 v = lo + ld * t;
			int tx = (int)std::floor(v.x / TILE_W), ty = (int)std::floor(v.z / TILE_W);
			if (tx < 0 || ty < 0 || tx >= p.mx || ty >= p.my) continue;
			Tile *tile = p.battle->getTile(Position(tx, ty, z));
			bool standable = tile && tile->isDiscovered(O_FLOOR) && (tile->getMapData(O_FLOOR) || tile->getMapData(O_OBJECT) || tile->getUnit());
			if (!standable && z > 0) continue;
			glm::vec3 w = o + d * t;
			if (w.x < clip.x || w.x > clip.z || w.z < clip.y || w.z > clip.w) return false;
			hit.t = t;
			hit.point = w;
			hit.tileX = tx; hit.tileY = ty; hit.tileZ = z;
			hit.globe = false;
			return true;
		}
		return false;
	}
	if (p.hasGlobe)
	{
		glm::vec3 oc = o - p.globeCenter;
		float b = glm::dot(oc, d), c = glm::dot(oc, oc) - p.globeRadius * p.globeRadius;
		float disc = b * b - c;
		if (disc < 0.f) return false;
		float t = -b - std::sqrt(disc);
		if (t <= 0.f) return false;
		glm::vec3 w = o + d * t;
		glm::mat4 inv = glm::inverse(p.globeMatrix());
		glm::vec3 u = glm::normalize(glm::vec3(inv * glm::vec4(w, 1.f)));
		hit.t = t;
		hit.point = w;
		hit.lat = -std::asin(glm::clamp(u.y, -1.f, 1.f));
		hit.lon = std::atan2(u.x, u.z);
		hit.globe = true;
		return true;
	}
	return false;
}

void Board::hover(const BoardHit &hit)
{
	Impl &p = *_p;
	p.hoverValid = true;
	p.hoverGlobe = hit.globe;
	p.hoverTile = Position(hit.tileX, hit.tileY, hit.tileZ);
	p.hoverPoint = hit.point;
	p.hoverLon = hit.lon;
	p.hoverLat = hit.lat;
}

void Board::hoverNone() { _p->hoverValid = false; }

/// Input can arrive after a screen closed but before the next update(): re-check cached pointers.
static void revalidate(Board::Impl &p)
{
	if (!p.game) { p.bs = nullptr; p.globe = nullptr; p.geo = nullptr; return; }
	bool bsAlive = false, geoAlive = false;
	for (State *s : p.game->getStates())
	{
		if (s == p.bs) bsAlive = true;
		if (s == p.geo) geoAlive = true;
	}
	if (!bsAlive) { p.bs = nullptr; p.hasBattle = false; }
	if (!geoAlive) { p.geo = nullptr; p.globe = nullptr; p.hasGlobe = false; }
}

void Board::click(int button)
{
	Impl &p = *_p;
	revalidate(p);
	if (!p.hoverValid) return;
	if (p.hasBattle && !p.hoverGlobe)
	{
		if (p.bs) p.bs->vrTileClick(p.hoverTile, button == SDL_BUTTON_RIGHT);
		return;
	}
	if (p.hasGlobe && p.hoverGlobe && p.globe && clickScreen)
	{
		// click the same spot on the flat globe; recenter first if it is near the edge there
		double lon = p.hoverLon, lat = p.hoverLat, cl, ca, x = 0, y = 0;
		p.globe->getCenter(&cl, &ca);
		double facing = std::cos(ca) * std::cos(lat) * std::cos(lon - cl) + std::sin(ca) * std::sin(lat);
		if (facing < 0.5) p.globe->center(lon, lat);
		p.globe->polarToCart(lon, lat, &x, &y);
		clickScreen(p.globe->getX() + (int)x, p.globe->getY() + (int)y, button);
	}
}

void Board::wheel(int dir)
{
	Impl &p = *_p;
	revalidate(p);
	if (p.hasBattle)
	{
		if (!p.bs) return;
		if (dir > 0) p.bs->btnMapUpClick(nullptr); else p.bs->btnMapDownClick(nullptr);
	}
	else if (p.hasGlobe)
	{
		p.globeRadius = glm::clamp(p.globeRadius * (dir > 0 ? 1.1f : 0.9f), 0.18f, 0.45f);
	}
}

bool Board::canGrab(const glm::vec3 &hand) const
{
	const Impl &p = *_p;
	if (p.hasBattle)
	{
		glm::vec4 c = p.clipRect();
		return hand.x > c.x - 0.1f && hand.x < c.z + 0.1f && hand.z > c.y - 0.1f && hand.z < c.w + 0.1f
			&& hand.y < p.surfaceY() + 0.45f && hand.y > p.surfaceY() - 0.15f;
	}
	if (p.hasGlobe) return glm::length(hand - p.globeCenter) < p.globeRadius + 0.15f;
	return false;
}

void Board::beginGrab(int hand, const glm::vec3 &pos)
{
	_p->held[hand] = true;
	_p->lastHand[hand] = pos;
}

void Board::updateGrab(int hand, const glm::vec3 &pos)
{
	Impl &p = *_p;
	if (!p.held[hand]) return;
	revalidate(p);
	int other = 1 - hand;
	if (p.hasBattle)
	{
		if (p.held[other])
		{
			// two hands: pinch to zoom, twist to turn the board
			glm::vec3 o = p.lastHand[other];
			glm::vec2 v0(p.lastHand[hand].x - o.x, p.lastHand[hand].z - o.z), v1(pos.x - o.x, pos.z - o.z);
			float l0 = glm::length(v0), l1 = glm::length(v1);
			if (l0 > 0.05f && l1 > 0.05f)
			{
				p.tileSize = glm::clamp(p.tileSize * (l1 / l0), 0.02f, 0.14f);
				p.yaw -= std::atan2(v1.y, v1.x) - std::atan2(v0.y, v0.x);
			}
		}
		else
		{
			// one hand: slide the map across the table
			glm::vec3 dw = pos - p.lastHand[hand];
			float k = p.tileSize / TILE_W;
			glm::vec3 dl = glm::vec3(glm::rotate(glm::mat4(1.f), -p.yaw, {0, 1, 0}) * glm::vec4(dw, 0.f)) / k;
			p.focus.x = glm::clamp(p.focus.x - dl.x, 0.f, (float)(p.mx * TILE_W));
			p.focus.z = glm::clamp(p.focus.z - dl.z, 0.f, (float)(p.my * TILE_W));
			if (p.bs && p.bs->getMap())
			{
				Position c((int)(p.focus.x / TILE_W), (int)(p.focus.z / TILE_W), p.viewLevel);
				p.bs->getMap()->getCamera()->centerOnPosition(c, true);
				p.focusTarget = p.focus;
			}
		}
	}
	else if (p.hasGlobe && p.globe && !p.held[other])
	{
		// spin the globe with the hand
		glm::vec3 dw = pos - p.lastHand[hand];
		double cl, ca;
		p.globe->getCenter(&cl, &ca);
		cl -= dw.x / p.globeRadius;
		ca = glm::clamp(ca - (double)dw.y / p.globeRadius, -1.5, 1.5);
		p.globe->center(cl, ca);
	}
	p.lastHand[hand] = pos;
}

void Board::endGrab(int hand)
{
	_p->held[hand] = false;
	if (_p->hasBattle) _p->focusTarget = _p->focus;
}

}
}
