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
#include "../Mod/RuleDamageType.h"
#include "../Battlescape/Position.h"
#include "../Battlescape/Pathfinding.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Interface/Text.h"
#include "../Engine/Font.h"
#include "../Engine/Language.h"
#include "../Mod/RuleInterface.h"
#include "../Mod/RuleItem.h"
#include "../Geoscape/GeoscapeState.h"
#include "../Geoscape/Globe.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <map>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <sstream>

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
	uint32_t avg = 0;  // average sprite colour (minimap)
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
	int topRow = 4;            // first opaque row: where the head is
	std::vector<uint32_t> px;
	glm::vec3 vis{0.f};        // smoothed position on the table (voxels)
	glm::vec3 visVel{0.f};     // its velocity (voxels/s)
	bool visInit = false;
	uint64_t seenFrame = 0;    // last update that drew this unit (to spot units coming back into view)
	int lastKey = -1;
	double lastRender = -1.0;
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
	std::vector<Chunk> fogChunks;  // fog of war volume, per chunk column (all levels)
	int fogTop(int x, int y) const; // fogged levels in a column (0 = explored)
	uint64_t fogHash(int cx, int cy);
	void buildFog(int cx, int cy, Chunk &ch);
	Atlas atlas;
	int fogX = -1, fogY = -1;
	const std::vector<Uint16> *voxels = nullptr;
	SDL_Color pal[256];
	glm::vec3 focus{0.f}, focusTarget{0.f};
	Position camSet;             // camera centre last seen (or set by the board)
	bool camSetValid = false;
	BattleUnit *lastSelected = nullptr;
	bool walkedLast = false;
	BattleUnit *walkedUnit = nullptr;
	bool projectileSeen = false;
	float sinceProjectile = 10.f;
	bool hidden = false;         // the game is showing "Hidden Movement"
	float curtain = 0.f;         // 0 open .. 1 the table fully covered
	std::vector<uint32_t> hiddenText; // "HIDDEN MOVEMENT" in the big font (RGBA), for banner and minimaps
	int hiddenTextW = 0, hiddenTextH = 0;
	Texture hiddenTex;
	Mesh curtainMesh;
	glm::vec4 curtainRect{0.f};
	void buildHiddenText();
	void drawCurtain(const Shader &sh);
	bool inView(const glm::vec3 &vox, float frac) const
	{
		float k = tileSize / TILE_W;
		glm::vec3 ow = glm::vec3(glm::rotate(glm::mat4(1.f), yaw, {0, 1, 0}) * glm::vec4((vox.x - focus.x) * k, 0.f, (vox.z - focus.z) * k, 0.f));
		return std::fabs(ow.x) < (mapRect.z - mapRect.x) * 0.5f * frac && std::fabs(ow.z) < (mapRect.w - mapRect.y) * 0.5f * frac;
	}
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
	Mesh wire[2], arrow, pointer, goal;
	Mesh cube;                         // unit cube for voxel effects
	bool shadowPass = false;           // drawing into the shadow map: terrain and figures only

	// ---- voxel effects: particles (board voxel space), what spawned them, and lights for the room
	struct Particle
	{
		glm::vec3 p, v;
		float life, maxLife, size, spin;
		glm::vec4 c0, c1;              // colour at birth and at death (alpha too)
		bool glow, gravity;
		bool flicker = false;          // crackling static: blinks on and off
		uint32_t seed = 0;
	};
	std::vector<Particle> particles;
	std::vector<const void*> seenExplosions;
	const Projectile *lastProjectile = nullptr;
	glm::vec3 lastProjPos{0.f};
	int projStyle = 0;                 // 0 bullet, 1 laser, 2 plasma, 3 rocket, 4 thrown
	float projPower = 0.5f;            // 0 weakest .. 1 strongest weapon of its class (sizes the effect)
	std::map<int, glm::vec2> powerRange; // per damage type: weakest and strongest firearm/ammo power in the mod
	float strength(ItemDamageType dt, int power);
	glm::vec3 projColor{1.f};
	glm::vec3 projOrigin{0.f};
	float beamFade = 0.f;
	glm::vec3 beamA{0.f}, beamB{0.f}, beamColor{1.f};
	struct Flash { glm::vec3 p; glm::vec3 col; };
	std::vector<Flash> flashes;        // world-space lights for the room this frame
	std::map<int, glm::vec3> spriteColors;
	glm::vec3 bulletColor(int sprite);
	void updateEffects(float dt);
	void spawnBurst(const glm::vec3 &at, bool big, bool hit);
	void drawEffects(const Shader &sh, const glm::mat4 &M); // HUD: wireframe boxes (1 and 2 tiles), path arrow, selected-unit arrow, path end

	// HUD: the game-style cursor, the selected unit marker, path preview, text readouts
	Position cursorTile;
	bool cursorSet = false;
	bool turnPreview = false;          // a finger is dragging away from the soldier to turn it
	glm::vec2 turnDir{0.f};            // last tile picked or hovered (laser, finger, tap)
	Position fingerTile;
	int fingerFrames = 0;              // >0 while a fingertip hovers over the map
	struct Label { std::unique_ptr<Texture> tex; int w = 0, h = 0; double used = 0.0; };
	std::map<std::string, Label> labels;
	const Label &label(const std::string &text, Uint8 color);
	glm::vec4 palColor(int index, float glow) const
	{
		const SDL_Color &c = pal[index & 255];
		return glm::vec4(c.r / 255.f * glow, c.g / 255.f * glow, c.b / 255.f * glow, 1.f);
	}
	void drawWire(const Shader &sh, const glm::mat4 &M, const glm::vec3 &at, int size, const glm::vec4 &col, float xray) const;
	void drawLabel(const Shader &sh, const glm::mat4 &M, const glm::vec3 &at, const glm::vec3 &viewerVox, const Label &l, float pxSize) const;
	void drawHud(const Shader &sh, const glm::mat4 &M);

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

	glm::vec4 mapRect{0.f};    // world xz rect of the table area that shows the map
	uint64_t unitFrame = 0;
	float surfaceY() const { return layout.tableCenter.y - 0.02f; }
	glm::mat4 boardMatrix() const
	{
		float k = tileSize / TILE_W;
		return glm::translate(glm::mat4(1.f), {(mapRect.x + mapRect.z) * 0.5f, surfaceY(), (mapRect.y + mapRect.w) * 0.5f})
			* glm::rotate(glm::mat4(1.f), yaw, {0, 1, 0})
			* glm::scale(glm::mat4(1.f), glm::vec3(k))
			* glm::translate(glm::mat4(1.f), {-focus.x, 0.f, -focus.z});
	}
	glm::vec4 clipRect() const { return mapRect; }
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
	fogChunks.clear();
	cards.clear();
	atlas.reset();
	fogX = fogY = -1;
	battle = nullptr;
	firstTile = nullptr;
	focusInit = false;
	hoverValid = false;
	cursorSet = false;
	fingerFrames = 0;
	labels.clear();
	particles.clear();
	seenExplosions.clear();
	lastProjectile = nullptr;
	beamFade = 0.f;
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
	fogChunks.clear();
	fogChunks.resize((size_t)cxN * cyN);
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
	p.avg = avg;

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

// ------------------------------------------------------------------ fog of war

/// How many levels of this column are fog: the unexplored ground and every unexplored level
/// straight above it, up to the level being viewed. 0 when the ground there has been seen.
int Board::Impl::fogTop(int x, int y) const
{
	if (x < 0 || y < 0 || x >= mx || y >= my || battle->getDebugMode()) return 0;
	int n = 0;
	for (int z = 0; z <= std::min(viewLevel, mz - 1); ++z)
	{
		Tile *t = battle->getTile(Position(x, y, z));
		if (!t || t->isDiscovered(O_FLOOR)) break;
		++n;
	}
	return n;
}

uint64_t Board::Impl::fogHash(int cx, int cy)
{
	uint64_t h = 1469598103934665603ull ^ (uint64_t)viewLevel;
	for (int y = cy * CHUNK - 1; y <= (cy + 1) * CHUNK; ++y)
		for (int x = cx * CHUNK - 1; x <= (cx + 1) * CHUNK; ++x)
		{
			h ^= (uint64_t)fogTop(x, y) + 1;
			h *= 1099511628211ull;
		}
	return h;
}

/// The fog volume: each fogged tile is a block of 4 x 4 little voxel columns with uneven tops, so
/// the unexplored map reads as a bank of cloud. Only faces that can be seen are made.
void Board::Impl::buildFog(int cx, int cy, Chunk &ch)
{
	const int SUB = 4;                 // columns per tile side
	const float CW = TILE_W / (float)SUB;
	auto hash = [](int a, int b) { uint32_t h = (uint32_t)(a * 73856093) ^ (uint32_t)(b * 19349663); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (h & 1023) / 1023.f; };
	// height of a sub-column (voxels), 0 where there is no fog
	auto colH = [&](int sx, int sy) -> float
	{
		int tx = sx >= 0 ? sx / SUB : -1, ty = sy >= 0 ? sy / SUB : -1;
		int n = fogTop(tx, ty);
		if (!n) return 0.f;
		float top = (n - 1) * TILE_H + 15.f;
		return top - std::floor(hash(sx, sy) * 6.f);
	};
	MeshData md;
	for (int sy = cy * CHUNK * SUB; sy < std::min(my, (cy + 1) * CHUNK) * SUB; ++sy)
		for (int sx = cx * CHUNK * SUB; sx < std::min(mx, (cx + 1) * CHUNK) * SUB; ++sx)
		{
			float h = colH(sx, sy);
			if (h <= 0.f) continue;
			float shade = 0.92f + 0.16f * hash(sx * 7 + 3, sy * 5 + 1);
			glm::vec4 topC = glm::vec4(0.31f, 0.35f, 0.44f, 1.f) * shade, sideC = glm::vec4(0.22f, 0.25f, 0.33f, 1.f) * shade;
			topC.a = sideC.a = 1.f;
			float x0 = sx * CW, x1 = x0 + CW, z0 = sy * CW, z1 = z0 + CW;
			const float y0 = 0.4f;
			// top (uv.x = 1: these vertices drift)
			md.addQuad({x0, h, z1}, {x1, h, z1}, {x1, h, z0}, {x0, h, z0}, topC, MAT_FOG, {1, 0}, {1, 0}, {1, 0}, {1, 0});
			// sides facing lower neighbours
			struct N { int dx, dy; };
			for (N nb : {N{1, 0}, N{-1, 0}, N{0, 1}, N{0, -1}})
			{
				float hn = colH(sx + nb.dx, sy + nb.dy);
				if (hn >= h) continue;
				float lo = std::max(y0, hn);
				glm::vec3 a, b; // edge on the ground plane, counter-clockwise seen from outside
				if (nb.dx == 1) { a = {x1, 0, z1}; b = {x1, 0, z0}; }
				else if (nb.dx == -1) { a = {x0, 0, z0}; b = {x0, 0, z1}; }
				else if (nb.dy == 1) { a = {x0, 0, z1}; b = {x1, 0, z1}; }
				else { a = {x1, 0, z0}; b = {x0, 0, z0}; }
				glm::vec2 top = hn > 0.f ? glm::vec2(1, 0) : glm::vec2(1, 0);
				md.addQuad(a + glm::vec3(0, lo, 0), b + glm::vec3(0, lo, 0), b + glm::vec3(0, h, 0), a + glm::vec3(0, h, 0), sideC, MAT_FOG,
					{0, 0}, {0, 0}, top, top);
			}
		}
	ch.empty = md.indices.empty();
	if (!ch.mesh) ch.mesh.reset(new Mesh());
	ch.mesh->upload(md);
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
				// unexplored ground: a dark slab so the board keeps its shape (the fog volume covers it)
				if (z == 0 && fogX >= 0)
				{
					float u0 = fogX / (float)ATLAS, v0 = fogY / (float)ATLAS, u1 = (fogX + 16) / (float)ATLAS, v1 = (fogY + 16) / (float)ATLAS;
					md.addQuad(off + glm::vec3(0, 0.5f, TILE_W), off + glm::vec3(TILE_W, 0.5f, TILE_W), off + glm::vec3(TILE_W, 0.5f, 0), off + glm::vec3(0, 0.5f, 0), glm::vec4(1.f), MAT_BOARD, {u0, v1}, {u1, v1}, {u1, v0}, {u0, v0});
				}
				// walls already seen from outside stay standing (the flat map shows them too)
				for (TilePart tp : {O_WESTWALL, O_NORTHWALL})
				{
					const MapData *d = t->getMapData(tp);
					if (!d || !t->isDiscovered(tp) || t->isUfoDoorOpen(tp)) continue;
					const Proto &p = proto(d);
					uint32_t base = (uint32_t)md.verts.size();
					float b = 1.f - glm::clamp(t->getShade(), 0, 15) / 15.f * 0.82f;
					for (const Vertex &v : p.verts) { Vertex w = v; w.pos += off; w.color = glm::vec4(b, b, b, 1.f); md.verts.push_back(w); }
					for (uint32_t i : p.idx) md.indices.push_back(base + i);
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
	card.topRow = -1;
	for (int y = 0; y < card.h; ++y)
		for (int x = 0; x < card.w; ++x)
		{
			Uint8 c = surf->getPixel(x, y);
			card.px[(size_t)y * card.w + x] = c ? rgba(pal[c]) : 0u;
			if (c && card.topRow < 0) card.topRow = y;
		}
	if (card.topRow < 0) card.topRow = 4;
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
		BattleUnit *sel = battle->getSelectedUnit();
		bool playerTurn = battle->getSide() == FACTION_PLAYER;
		hidden = bs->getMap()->vrHiddenMovement() && !playerTurn && !battle->getDebugMode();
		if (bs->getMap()->getProjectile()) sinceProjectile = 0.f; else sinceProjectile += dt;

		// The table keeps its own view. The game's camera is only followed when the game moves it on
		// purpose: another soldier selected, an alien button pressed, the alien turn's visible action.
		// Its automatic "centre on the soldier" after every move, and its bullet-chasing, are ignored.
		if (!camSetValid || c.x != camSet.x || c.y != camSet.y)
		{
			bool follow = true;
			if (playerTurn)
			{
				if (sinceProjectile < 0.6f) follow = false;
				else if (sel && sel == lastSelected && std::abs(sel->getPosition().x - c.x) <= 1 && std::abs(sel->getPosition().y - c.y) <= 1)
					follow = false; // just re-centring on the soldier we already have
			}
			else if (hidden) follow = false; // nothing may be revealed while the movement is hidden
			if (follow) focusTarget = glm::vec3(c.x * TILE_W + TILE_W * 0.5f, 0.f, c.y * TILE_W + TILE_W * 0.5f);
			camSet = c;
			camSetValid = true;
		}
		if (sel != lastSelected)
		{
			// a newly selected soldier off the table's view: bring it in
			lastSelected = sel;
			if (sel && playerTurn && !sel->isOut() && !inView(unitVoxelPos(sel), 0.8f))
			{
				glm::vec3 up = unitVoxelPos(sel);
				focusTarget = glm::vec3(up.x, 0.f, up.z);
			}
		}

		// keep a walking unit in view: glide just enough to bring it back inside the middle of the table
		if (!hidden && sel && !sel->isOut() && (sel->getStatus() == STATUS_WALKING || sel->getStatus() == STATUS_FLYING)
			&& (playerTurn || sel->getVisible()) && !held[0] && !held[1])
		{
			auto it = cards.find(sel);
			glm::vec3 up = (it != cards.end() && it->second.visInit) ? it->second.vis : unitVoxelPos(sel);
			float k = tileSize / TILE_W;
			// unit offset from the focus target, in table space (metres)
			glm::vec3 ow = glm::vec3(glm::rotate(glm::mat4(1.f), yaw, {0, 1, 0}) * glm::vec4((up.x - focusTarget.x) * k, 0.f, (up.z - focusTarget.z) * k, 0.f));
			float hw = (mapRect.z - mapRect.x) * 0.5f, hd = (mapRect.w - mapRect.y) * 0.5f;
			glm::vec3 shift(0.f);
			float bx = hw * 0.35f, bz = hd * 0.3f; // the unit stays inside the middle of the table
			if (ow.x > bx) shift.x = ow.x - bx; else if (ow.x < -bx) shift.x = ow.x + bx;
			if (ow.z > bz) shift.z = ow.z - bz; else if (ow.z < -bz) shift.z = ow.z + bz;
			if (shift.x != 0.f || shift.z != 0.f)
			{
				glm::vec3 sl = glm::vec3(glm::rotate(glm::mat4(1.f), -yaw, {0, 1, 0}) * glm::vec4(shift, 0.f)) / k;
				focusTarget.x = glm::clamp(focusTarget.x + sl.x, 0.f, (float)(mx * TILE_W));
				focusTarget.z = glm::clamp(focusTarget.z + sl.z, 0.f, (float)(my * TILE_W));
				// the flat screen follows the table
				Position fc((int)(focusTarget.x / TILE_W), (int)(focusTarget.z / TILE_W), viewLevel);
				cam->centerOnPosition(fc, false);
				camSet = cam->getCenterPosition();
				camSetValid = true;
			}
		}
		// when the walk ends, settle the view on the unit if it ended up off-centre
		bool walkingNow = sel && (sel->getStatus() == STATUS_WALKING || sel->getStatus() == STATUS_FLYING);
		if (walkedLast && !walkingNow && sel == walkedUnit && sel && !sel->isOut() && !hidden && (playerTurn || sel->getVisible()))
		{
			glm::vec3 up = unitVoxelPos(sel);
			glm::vec3 saveFocus = focus;
			focus = focusTarget;
			bool central = inView(up, 0.3f);
			focus = saveFocus;
			if (!central)
			{
				focusTarget = glm::vec3(up.x, 0.f, up.z);
				Position fc((int)(up.x / TILE_W), (int)(up.z / TILE_W), viewLevel);
				cam->centerOnPosition(fc, false);
				camSet = cam->getCenterPosition();
				camSetValid = true;
			}
		}
		walkedLast = walkingNow;
		walkedUnit = sel;
		// the hidden movement curtain
		float ct = hidden ? 1.f : 0.f;
		curtain = hidden ? std::min(1.f, curtain + dt / 0.5f) : std::max(0.f, curtain - dt / 0.3f);
		(void)ct;
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
	// fog volume, a few columns per frame after the first build
	int fogBudget = 4;
	for (int cy = 0; cy < cyN; ++cy)
		for (int cx = 0; cx < cxN; ++cx)
		{
			Chunk &fc = fogChunks[(size_t)cy * cxN + cx];
			uint64_t h = fogHash(cx, cy);
			if (h == fc.hash) continue;
			if (fc.mesh && fogBudget-- <= 0) continue;
			buildFog(cx, cy, fc);
			fc.hash = h;
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
	if (fingerFrames > 0) --fingerFrames;
	updateEffects(dt);
	++unitFrame;
	for (BattleUnit *u : *battle->getUnits())
	{
		if (u->isOut()) continue;
		if (!(u->getFaction() == FACTION_PLAYER || u->getVisible() || battle->getDebugMode())) continue;
		UnitCard &card = cards[u];
		bool reappeared = card.seenFrame + 1 != unitFrame;
		card.seenFrame = unitFrame;

		// smooth movement (see below); units coming back into view and jumps of more than
		// 3 tiles (loading, teleports) snap instead of gliding from where they were last seen
		glm::vec3 target = unitVoxelPos(u);
		if (!card.visInit || reappeared || glm::length(target - card.vis) > TILE_W * 3.f)
		{
			card.vis = target;
			card.visVel = glm::vec3(0.f);
			card.visInit = true;
		}
		else
		{
			int stepMs = std::max(1, u->getFaction() == FACTION_PLAYER ? Options::battleXcomSpeed : Options::battleAlienSpeed);
			bool moving = u->getStatus() == STATUS_WALKING || u->getStatus() == STATUS_FLYING;
			// The game moves figures in 2-voxel steps (~33 a second) on a timer that bunches up after slow
			// frames, while the headset draws 90-144 frames a second. Follow it with a critically damped
			// spring (time constant ~3 game steps) so figures glide instead of ticking and hopping.
			float stepS = stepMs / 1000.f;
			float T = glm::clamp(3.f * stepS, 0.06f, 0.2f);
			float w = 2.f / T;
			glm::vec3 x0 = card.vis - target;
			glm::vec3 tmp = (card.visVel + w * x0) * dt;
			float e = std::exp(-w * dt);
			glm::vec3 vel = (card.visVel - w * tmp) * e;
			glm::vec3 before = card.vis;
			card.vis = target + (x0 + tmp) * e;
			// cap at 1.6x walking pace: evens out catch-up bursts without lagging behind a unit that walks normally
			float maxSpeed = 2.f * 1.6f / stepS * (moving ? 1.f : 1.5f);
			float sp = glm::length(vel);
			if (sp > maxSpeed) vel *= maxSpeed / sp;
			card.visVel = vel;
			static const bool trace = std::getenv("OXCE_VR_TRACE_WALK") != nullptr;
			if (trace && moving && dt > 0.f)
				Log(LOG_INFO) << "[VR] walk " << (glm::length(card.vis - before) / dt) << " vox/s, game " << glm::length(target - before) << " ahead, dt " << dt;
		}

		glm::vec3 p = card.vis;
		glm::vec2 toViewer(viewerVox.x - p.x, viewerVox.z - p.z);
		// sector the viewer stands in, seen from the unit (0 = north, clockwise)
		float ang = std::atan2(toViewer.x, -toViewer.y);
		int sector = ((int)std::lround(ang / (3.14159265f / 4.f)) % 8 + 8) % 8;
		// the flat map's camera sits to the south-east (sector 3); redraw on a change or ~15 times a second
		int key = (3 - sector + 8) % 8 + 8 * u->getDirection() + 64 * (int)u->getStatus();
		if (key != card.lastKey || animTime - card.lastRender > 1.0 / 15.0 || !card.tex)
		{
			renderUnitCard(u, card, 3 - sector);
			card.lastKey = key;
			card.lastRender = animTime;
		}
	}
}

void Board::Impl::drawUnits(const Shader &sh)
{
	glm::mat4 M = boardMatrix();
	glm::mat4 inv = glm::inverse(M);
	glm::vec3 viewerVox = glm::vec3(inv * glm::vec4(viewer, 1.f));
	for (BattleUnit *u : *battle->getUnits())
	{
		auto it = cards.find(u);
		if (it == cards.end() || !it->second.tex) continue;
		if (u->isOut()) continue;
		if (!(u->getFaction() == FACTION_PLAYER || u->getVisible() || battle->getDebugMode())) continue;
		if (u->getPosition().z > viewLevel) continue;
		const UnitCard &card = it->second;
		glm::vec3 p = card.visInit ? card.vis : unitVoxelPos(u);
		int size = u->getArmor()->getSize();

		// miniature base in the faction's color
		glm::vec4 fc = u->getFaction() == FACTION_PLAYER ? glm::vec4(0.15f, 0.45f, 1.f, 1.f)
			: u->getFaction() == FACTION_HOSTILE ? glm::vec4(0.95f, 0.18f, 0.12f, 1.f) : glm::vec4(0.2f, 0.85f, 0.3f, 1.f);
		sh.set("uMode", 2);
		float r = 5.5f * size;
		sh.set("uModel", M * glm::translate(glm::mat4(1.f), p + glm::vec3(0, 0.1f, 0)) * glm::scale(glm::mat4(1.f), {r, 1.2f, r}));
		sh.set("uTint", fc * glm::vec4(0.6f, 0.6f, 0.6f, 1.f));
		disk.draw();

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

void Board::Impl::buildHiddenText()
{
	if (!hiddenText.empty() || !game) return;
	Mod *mod = game->getMod();
	Font *big = mod->getFont("FONT_BIG", false), *small = mod->getFont("FONT_SMALL", false);
	if (!big || !small) return;
	const int W = 200, H = 18;
	Text t(W, H, 0, 0);
	SDL_Color sp[256];
	for (int i = 0; i < 256; ++i) sp[i] = pal[i];
	t.setPalette(sp);
	t.initText(big, small, game->getLanguage());
	t.setBig();
	t.setColor(1);
	t.setText(game->getLanguage()->getString("STR_HIDDEN_MOVEMENT"));
	t.draw();
	int w = std::max(1, std::min(W, t.getTextWidth() + 2)), h = std::max(1, std::min(H, t.getTextHeight() + 1));
	hiddenText.assign((size_t)w * h, 0u);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			int v = t.getPixel(x, y);
			if (v) hiddenText[(size_t)y * w + x] = v <= 2 ? 0xFF3040FFu : v <= 4 ? 0xFF1020C0u : 0xFF000010u;
		}
	hiddenTextW = w;
	hiddenTextH = h;
	hiddenTex.create(w, h, true, true);
	hiddenTex.update(hiddenText.data(), w, h);
}

/// The hidden movement curtain: a bank of voxel fog rising out of the table over the whole map.
void Board::Impl::drawCurtain(const Shader &sh)
{
	if (curtain <= 0.f) return;
	if (curtainRect != mapRect || curtainMesh.empty())
	{
		curtainRect = mapRect;
		const float C = 0.02f;
		int nx = (int)std::ceil((mapRect.z - mapRect.x) / C), nz = (int)std::ceil((mapRect.w - mapRect.y) / C);
		auto hash = [](int a, int b) { uint32_t h = (uint32_t)(a * 73856093) ^ (uint32_t)(b * 19349663); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (h & 1023) / 1023.f; };
		auto hgt = [&](int i, int j) { return (i < 0 || j < 0 || i >= nx || j >= nz) ? 0.f : 0.7f + 0.3f * hash(i, j); };
		MeshData md;
		for (int j = 0; j < nz; ++j)
			for (int i = 0; i < nx; ++i)
			{
				float h = hgt(i, j);
				float shade = 0.9f + 0.2f * hash(i * 3 + 1, j * 7 + 2);
				glm::vec4 top = glm::vec4(0.26f, 0.28f, 0.36f, 1.f) * shade, side = glm::vec4(0.18f, 0.2f, 0.27f, 1.f) * shade;
				top.a = side.a = 1.f;
				float x0 = mapRect.x + i * C, x1 = std::min(mapRect.z, x0 + C), z0 = mapRect.y + j * C, z1 = std::min(mapRect.w, z0 + C);
				md.addQuad({x0, h, z1}, {x1, h, z1}, {x1, h, z0}, {x0, h, z0}, top, MAT_FOG, {1, 0}, {1, 0}, {1, 0}, {1, 0});
				struct N { int di, dj; };
				for (N nb : {N{1, 0}, N{-1, 0}, N{0, 1}, N{0, -1}})
				{
					float hn = hgt(i + nb.di, j + nb.dj);
					if (hn >= h) continue;
					glm::vec3 a, b;
					if (nb.di == 1) { a = {x1, 0, z1}; b = {x1, 0, z0}; }
					else if (nb.di == -1) { a = {x0, 0, z0}; b = {x0, 0, z1}; }
					else if (nb.dj == 1) { a = {x0, 0, z1}; b = {x1, 0, z1}; }
					else { a = {x1, 0, z0}; b = {x0, 0, z0}; }
					md.addQuad(a + glm::vec3(0, hn, 0), b + glm::vec3(0, hn, 0), b + glm::vec3(0, h, 0), a + glm::vec3(0, h, 0), side, MAT_FOG, {0, 0}, {0, 0}, {1, 0}, {1, 0});
				}
			}
		curtainMesh.upload(md);
	}
	float e = curtain * curtain * (3.f - 2.f * curtain);
	const float H = 0.07f;
	sh.set("uMode", 0);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", glm::translate(glm::mat4(1.f), {0.f, surfaceY() - 0.002f, 0.f}) * glm::scale(glm::mat4(1.f), {1.f, std::max(0.002f, H * e), 1.f}));
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	curtainMesh.draw();
	// the banner over the middle of the table, facing the player
	buildHiddenText();
	if (hiddenTex.valid() && e > 0.3f)
	{
		glm::vec3 c((mapRect.x + mapRect.z) * 0.5f, surfaceY() + H + 0.16f, (mapRect.y + mapRect.w) * 0.5f);
		glm::vec3 toV = viewer - c;
		toV.y = 0.f;
		toV = glm::length(toV) > 1e-3f ? glm::normalize(toV) : glm::vec3(0, 0, 1);
		glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), toV));
		float w = 0.95f, h = w * hiddenTextH / (float)hiddenTextW;
		glm::mat4 B(1.f);
		B[0] = glm::vec4(right * w, 0.f);
		B[1] = glm::vec4(0.f, h, 0.f, 0.f);
		B[2] = glm::vec4(toV, 0.f);
		B[3] = glm::vec4(c, 1.f);
		float pulse = 0.8f + 0.2f * std::sin((float)time * 2.5f);
		sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
		sh.set("uMode", 4);
		sh.set("uTint", glm::vec4(glm::vec3(1.6f * pulse), (e - 0.3f) / 0.7f));
		sh.set("uModel", B);
		hiddenTex.bind(0);
		glDisable(GL_DEPTH_TEST);
		quad.draw();
		glEnable(GL_DEPTH_TEST);
		sh.set("uClip", clipRect());
	}
	glDisable(GL_BLEND);
	sh.set("uTint", glm::vec4(1.f));
}

void Board::Impl::drawBattle(const Shader &sh)
{
	glm::mat4 M = boardMatrix();
	sh.set("uClip", clipRect());
	if (curtain >= 0.999f)
	{
		// fully covered: nothing of the map may show through
		if (!shadowPass) drawCurtain(sh);
		sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
		return;
	}

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

	if (shadowPass) { sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f)); return; } // only solid things cast shadows

	// fog of war over the unexplored map
	sh.set("uMode", 0);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", M);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	for (const Chunk &fc : fogChunks)
		if (fc.mesh && !fc.empty) fc.mesh->draw();
	glDisable(GL_BLEND);

	drawEffects(sh, M);

	drawHud(sh, M);
	drawCurtain(sh);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
}

// ------------------------------------------------------------------ voxel effects

static float frand(uint32_t &st) { st = st * 1664525u + 1013904223u; return (st >> 8) / 16777216.f; }

/// The colour of a weapon's bullet sprite (so mods' projectiles keep their colours on the table).
glm::vec3 Board::Impl::bulletColor(int sprite)
{
	auto it = spriteColors.find(sprite);
	if (it != spriteColors.end()) return it->second;
	glm::vec3 c(1.f, 0.8f, 0.4f);
	SurfaceSet *set = game ? game->getMod()->getSurfaceSet("Projectiles", false) : nullptr;
	if (set && sprite >= 0)
		if (Surface *f = set->getFrame(sprite))
		{
			glm::vec3 sum(0.f);
			float n = 0.f;
			for (int y = 0; y < f->getHeight(); ++y)
				for (int x = 0; x < f->getWidth(); ++x)
					if (Uint8 v = f->getPixel(x, y))
					{
						// weight bright pixels: the core of the bullet
						glm::vec3 k(pal[v].r / 255.f, pal[v].g / 255.f, pal[v].b / 255.f);
						float w = 0.2f + glm::dot(k, glm::vec3(0.3f, 0.6f, 0.1f));
						sum += k * w; n += w;
					}
			if (n > 0.f) c = sum / n;
			float m = std::max(c.r, std::max(c.g, c.b));
			if (m > 0.01f) c /= m; // full brightness, keep the hue
		}
	spriteColors[sprite] = c;
	return c;
}

/// How strong a weapon is within its class (its damage type): 0 the mod's weakest, 1 its strongest.
float Board::Impl::strength(ItemDamageType dt, int power)
{
	auto it = powerRange.find((int)dt);
	if (it == powerRange.end())
	{
		glm::vec2 r(1e9f, -1e9f);
		Mod *mod = game->getMod();
		for (const std::string &id : mod->getItemsList())
		{
			const RuleItem *ri = mod->getItem(id);
			if (!ri || !ri->getDamageType() || ri->getDamageType()->ResistType != dt || ri->getPower() <= 0) continue;
			if (ri->getBattleType() != BT_FIREARM && ri->getBattleType() != BT_AMMO) continue;
			r.x = std::min(r.x, (float)ri->getPower());
			r.y = std::max(r.y, (float)ri->getPower());
		}
		it = powerRange.emplace((int)dt, r).first;
	}
	glm::vec2 r = it->second;
	if (power <= 0 || r.y <= r.x) return 0.5f;
	return glm::clamp((power - r.x) / (r.y - r.x), 0.f, 1.f);
}

/// An explosion (or a bullet hit): a burst of glowing voxels, embers and rising smoke.
void Board::Impl::spawnBurst(const glm::vec3 &at, bool big, bool hit)
{
	static uint32_t st = 12345u;
	int fire = hit ? 10 : big ? 70 : 28, smoke = hit ? 3 : big ? 26 : 10, debris = hit ? 4 : big ? 18 : 8;
	float speed = hit ? 18.f : big ? 55.f : 30.f;
	for (int i = 0; i < fire; ++i)
	{
		glm::vec3 d = glm::normalize(glm::vec3(frand(st) - 0.5f, frand(st) * 0.9f - 0.2f, frand(st) - 0.5f) + glm::vec3(0, 0.05f, 0));
		Particle p;
		p.p = at + d * (frand(st) * 2.f);
		p.v = d * speed * (0.4f + 0.6f * frand(st));
		p.maxLife = p.life = (hit ? 0.25f : 0.45f) + frand(st) * (big ? 0.6f : 0.35f);
		p.size = (hit ? 1.2f : big ? 3.f : 2.f) * (0.6f + frand(st) * 0.8f);
		p.spin = frand(st) * 6.f;
		p.c0 = glm::vec4(2.6f, 1.45f, 0.35f, 1.f);
		p.c1 = glm::vec4(1.3f, 0.22f, 0.03f, 0.f);
		p.glow = true; p.gravity = false;
		particles.push_back(p);
	}
	// the fireball core: a few big voxels that swell and fade
	for (int i = 0; hit ? false : i < (big ? 7 : 3); ++i)
	{
		Particle p;
		p.p = at + glm::vec3(frand(st) - 0.5f, frand(st) - 0.3f, frand(st) - 0.5f) * (big ? 10.f : 5.f);
		p.v = glm::vec3(0.f, 4.f, 0.f);
		p.maxLife = p.life = (big ? 0.45f : 0.3f) + frand(st) * 0.15f;
		p.size = (big ? 9.f : 5.f) * (0.7f + frand(st) * 0.5f);
		p.spin = frand(st) * 6.f;
		p.c0 = glm::vec4(2.4f, 1.6f, 0.5f, 0.9f);
		p.c1 = glm::vec4(1.5f, 0.3f, 0.04f, 0.f);
		p.glow = true; p.gravity = false;
		particles.push_back(p);
	}
	for (int i = 0; i < smoke; ++i)
	{
		Particle p;
		p.p = at + glm::vec3(frand(st) - 0.5f, frand(st) * 0.5f, frand(st) - 0.5f) * (big ? 16.f : 7.f);
		p.v = glm::vec3((frand(st) - 0.5f) * 6.f, 6.f + frand(st) * 8.f, (frand(st) - 0.5f) * 6.f);
		p.maxLife = p.life = 1.6f + frand(st) * (big ? 2.4f : 1.2f);
		p.size = (big ? 4.5f : 3.f) * (0.7f + frand(st) * 0.6f);
		p.spin = frand(st) * 6.f;
		float g = 0.18f + frand(st) * 0.1f;
		p.c0 = glm::vec4(g, g, g * 1.05f, 0.85f);
		p.c1 = glm::vec4(g * 1.6f, g * 1.6f, g * 1.7f, 0.f);
		p.glow = false; p.gravity = false;
		particles.push_back(p);
	}
	for (int i = 0; i < debris; ++i)
	{
		Particle p;
		glm::vec3 d = glm::normalize(glm::vec3(frand(st) - 0.5f, 0.6f + frand(st), frand(st) - 0.5f));
		p.p = at;
		p.v = d * speed * 0.9f;
		p.maxLife = p.life = 0.8f + frand(st) * 0.6f;
		p.size = 0.9f + frand(st) * 0.9f;
		p.spin = frand(st) * 6.f;
		p.c0 = glm::vec4(0.25f, 0.2f, 0.16f, 1.f);
		p.c1 = glm::vec4(0.1f, 0.08f, 0.06f, 1.f);
		p.glow = false; p.gravity = true;
		particles.push_back(p);
	}
}

void Board::Impl::updateEffects(float dt)
{
	flashes.clear();
	glm::mat4 M = boardMatrix();
	auto toWorld = [&](const glm::vec3 &v) { return glm::vec3(M * glm::vec4(v, 1.f)); };
	static uint32_t st = 777u;
	Map *map = (bs && bs->getMap()) ? bs->getMap() : nullptr;

	// projectile: pick a style from what was fired; trails leave particles behind
	const Projectile *pr = map ? map->getProjectile() : nullptr;
	projectileSeen = pr != nullptr;
	if (pr != lastProjectile)
	{
		lastProjectile = pr;
		if (pr)
		{
			const BattleAction &a = pr->vrAction();
			const BattleItem *ammo = pr->vrAmmo();
			const RuleItem *ar = ammo ? ammo->getRules() : (a.weapon ? a.weapon->getRules() : nullptr);
			ItemDamageType dt2 = DT_AP;
			if (ar && ar->getDamageType()) dt2 = ar->getDamageType()->ResistType;
			else if (a.weapon && a.weapon->getRules()->getDamageType()) dt2 = a.weapon->getRules()->getDamageType()->ResistType;
			// rockets / bombs: launched, or explosive ammo that comes one round at a time
			bool rocket = a.type == BA_LAUNCH || ((dt2 == DT_HE || dt2 == DT_IN || dt2 == DT_STUN || dt2 == DT_SMOKE) && ar && ar->getBattleType() == BT_AMMO && ar->getClipSize() <= 1);
			projStyle = a.type == BA_THROW ? 4 : rocket ? 3 : dt2 == DT_LASER ? 1 : (dt2 == DT_PLASMA || dt2 == DT_ACID || dt2 == DT_STUN) ? 2 : 0;
			projPower = strength(dt2, ar ? ar->getPower() : 0);
			float k = projPower;
			// colours are emissive: kept saturated (no channel far above 1) so they stay red / green and don't burn to white
			if (projStyle == 1) projColor = glm::mix(glm::vec3(1.f, 0.10f, 0.06f), glm::vec3(1.f, 0.03f, 0.10f), k);       // laser: red, deeper when heavy
			else if (projStyle == 2) projColor = dt2 == DT_STUN ? glm::vec3(0.45f, 0.5f, 1.f) : glm::mix(glm::vec3(0.25f, 1.f, 0.25f), glm::vec3(0.1f, 1.f, 0.35f), k); // plasma: green
			else if (projStyle == 3) projColor = glm::vec3(1.f, 0.6f, 0.2f);
			else if (projStyle == 4) projColor = glm::vec3(0.7f, 0.75f, 0.6f);
			else projColor = (dt2 == DT_HE || dt2 == DT_IN) ? glm::vec3(1.f, 0.55f, 0.15f) : (pr->vrBulletSprite() >= 0 ? bulletColor(pr->vrBulletSprite()) : glm::vec3(1.f, 0.85f, 0.35f));
			Position o = pr->getOrigin();
			Position v0 = pr->getPosition(0);
			projOrigin = glm::vec3(v0.x, v0.z, v0.y);
			(void)o;
			lastProjPos = projOrigin;
			// muzzle flash
			for (int i = 0; i < (projStyle == 4 ? 0 : 4 + (int)(8 * k)); ++i)
			{
				Particle p;
				glm::vec3 d = glm::normalize(glm::vec3(frand(st) - 0.5f, frand(st) - 0.5f, frand(st) - 0.5f));
				p.p = projOrigin; p.v = d * (10.f + 12.f * k);
				p.maxLife = p.life = 0.12f + frand(st) * 0.08f;
				p.size = 0.9f + 1.0f * k; p.spin = 0.f;
				p.c0 = glm::vec4(projColor * 3.f, 1.f); p.c1 = glm::vec4(projColor, 0.f);
				p.glow = true; p.gravity = false;
				particles.push_back(p);
			}
		}
		else if (projStyle == 1)
		{
			beamFade = 0.12f + 0.1f * projPower; // the laser beam lingers for a moment after the shot lands
		}
	}
	if (pr)
	{
		Position v = pr->getPosition(0);
		glm::vec3 now(v.x, v.z, v.y);
		glm::vec3 seg = now - lastProjPos;
		float len = glm::length(seg);
		float k = projPower;
		int n = (int)std::min(16.f, len / (projStyle == 1 ? 1.6f : 3.f));
		for (int i = 0; i < n; ++i)
		{
			glm::vec3 at = lastProjPos + seg * ((i + frand(st)) / std::max(1, n));
			Particle p;
			p.p = at;
			p.spin = frand(st) * 6.f;
			p.gravity = false;
			p.seed = (uint32_t)(frand(st) * 1e6f);
			if (projStyle == 3)
			{
				// rocket: a smoke trail, thicker for bigger rockets
				p.v = glm::vec3((frand(st) - 0.5f) * 3.f, 2.f + frand(st) * 2.f, (frand(st) - 0.5f) * 3.f);
				p.maxLife = p.life = 0.9f + frand(st) * 0.8f + k * 0.6f;
				p.size = (1.6f + 1.6f * k) + frand(st) * 1.5f;
				p.c0 = glm::vec4(0.35f, 0.33f, 0.31f, 0.8f); p.c1 = glm::vec4(0.6f, 0.6f, 0.62f, 0.f);
				p.glow = false;
			}
			else if (projStyle == 2)
			{
				// plasma: green sparks that drift off, more and longer-lived for heavier guns
				for (int m = 0; m < 1 + (int)(2 * k); ++m)
				{
					Particle q = p;
					q.p = at + glm::vec3(frand(st) - 0.5f, frand(st) - 0.5f, frand(st) - 0.5f) * (1.f + 2.f * k);
					q.v = glm::vec3(frand(st) - 0.5f, frand(st) - 0.5f, frand(st) - 0.5f) * (8.f + 8.f * k);
					q.maxLife = q.life = 0.2f + frand(st) * 0.25f + 0.25f * k;
					q.size = 0.6f + 0.6f * k + frand(st) * 0.6f;
					q.c0 = glm::vec4(projColor * 1.4f, 1.f); q.c1 = glm::vec4(projColor * 0.5f, 0.f);
					q.glow = true;
					particles.push_back(q);
				}
				continue;
			}
			else if (projStyle == 1)
			{
				// laser: crackling red static left hanging in the air, fading in about half a second
				for (int m = 0; m < 1 + (int)(2 * k); ++m)
				{
					Particle q = p;
					float spread = 0.8f + 1.8f * k;
					q.p = at + glm::vec3(frand(st) - 0.5f, frand(st) - 0.5f, frand(st) - 0.5f) * spread;
					q.v = glm::vec3(frand(st) - 0.5f, frand(st) - 0.5f, frand(st) - 0.5f) * 1.5f;
					q.maxLife = q.life = 0.3f + 0.3f * k + frand(st) * 0.15f;
					q.size = 0.6f + 0.7f * k + frand(st) * 0.4f;
					q.c0 = glm::vec4(projColor * (1.2f + 0.4f * k), 1.f); q.c1 = glm::vec4(projColor * 0.5f, 0.f);
					q.glow = true;
					q.flicker = true;
					q.seed = (uint32_t)(frand(st) * 1e6f);
					particles.push_back(q);
				}
				continue;
			}
			else if (projStyle == 0)
			{
				// tracer: a short streak of fading voxels
				p.v = glm::vec3(0.f);
				p.maxLife = p.life = 0.06f + 0.08f * k;
				p.size = 0.6f + 0.6f * k;
				p.c0 = glm::vec4(projColor * (1.8f + 1.2f * k), 0.9f); p.c1 = glm::vec4(projColor, 0.f);
				p.glow = true;
			}
			else continue;
			particles.push_back(p);
		}
		if (projStyle == 1) { beamA = projOrigin; beamB = now; beamColor = projColor; }
		lastProjPos = now;
		if (projStyle != 4) flashes.push_back({toWorld(now), projColor * (projStyle == 1 ? 0.6f : 0.35f) * (0.6f + 0.8f * projPower)});
	}
	if (beamFade > 0.f) beamFade -= dt;

	// explosions: a burst the first time each one shows up
	if (map)
	{
		std::vector<const void*> now;
		for (Explosion *e : *map->getExplosions())
		{
			now.push_back(e);
			Position v = e->getPosition();
			glm::vec3 at(v.x, v.z, v.y);
			if (std::find(seenExplosions.begin(), seenExplosions.end(), (const void*)e) == seenExplosions.end())
				spawnBurst(at, e->isBig(), e->isHit());
			float f = (float)std::max(0, e->getCurrentFrame());
			float k = glm::clamp(1.f - f / (e->isBig() ? 8.f : 5.f), 0.f, 1.f);
			if (!e->isHit()) flashes.push_back({toWorld(at + glm::vec3(0, 6, 0)), glm::vec3(1.f, 0.55f, 0.18f) * (e->isBig() ? 2.5f : 1.2f) * k});
		}
		seenExplosions.swap(now);
	}

	// particles
	for (Particle &p : particles)
	{
		p.life -= dt;
		if (p.gravity) p.v.y -= 120.f * dt;
		else if (!p.glow) p.v *= std::exp(-dt * 0.8f);
		else p.v *= std::exp(-dt * 3.f);
		p.p += p.v * dt;
		if (p.gravity && p.p.y < 0.5f) { p.p.y = 0.5f; p.v = glm::vec3(0.f); }
		p.spin += dt * 2.f;
	}
	particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle &p) { return p.life <= 0.f; }), particles.end());
	if (particles.size() > 3000) particles.erase(particles.begin(), particles.begin() + (particles.size() - 3000));
}

void Board::Impl::drawEffects(const Shader &sh, const glm::mat4 &M)
{
	Map *map = (bs && bs->getMap()) ? bs->getMap() : nullptr;
	auto cubeAt = [&](const glm::vec3 &p, float size, float spin, const glm::vec4 &c)
	{
		sh.set("uTint", c);
		sh.set("uModel", M * glm::translate(glm::mat4(1.f), p) * glm::rotate(glm::mat4(1.f), spin, {0.3f, 1.f, 0.2f}) * glm::scale(glm::mat4(1.f), glm::vec3(size)));
		cube.draw();
	};
	auto boxAlong = [&](const glm::vec3 &a, const glm::vec3 &b, float thick, const glm::vec4 &c)
	{
		glm::vec3 d = b - a;
		float l = glm::length(d);
		if (l < 1e-3f) return;
		glm::quat q = glm::rotation(glm::vec3(0, 0, 1), d / l);
		sh.set("uTint", c);
		sh.set("uModel", M * glm::translate(glm::mat4(1.f), (a + b) * 0.5f) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.f), {thick, thick, l}));
		cube.draw();
	};
	double t = animTime;

	// smoke and fire lingering on tiles: drifting voxel clouds and flickering voxel flames
	if (battle)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		for (int z = 0; z <= std::min(viewLevel, mz - 1); ++z)
			for (int y = 0; y < my; ++y)
				for (int x = 0; x < mx; ++x)
				{
					Tile *tl = battle->getTile(Position(x, y, z));
					if (!tl || (!tl->isDiscovered(O_FLOOR) && !battle->getDebugMode())) continue;
					int smoke = tl->getSmoke(), fire = tl->getFire();
					if (!smoke && !fire) continue;
					uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663) ^ (uint32_t)(z * 83492791);
					glm::vec3 base(x * TILE_W + 8.f, z * TILE_H - tl->getTerrainLevel(), y * TILE_W + 8.f);
					int puffs = smoke ? std::min(6, 1 + smoke / 3) : 0;
					sh.set("uMode", 0);
					for (int i = 0; i < puffs; ++i)
					{
						uint32_t r = h + i * 2654435761u;
						float a = frand(r), b = frand(r), c = frand(r), sp = 0.08f + 0.06f * frand(r);
						float ph = (float)std::fmod(t * sp + a, 1.0);
						glm::vec3 p = base + glm::vec3((b - 0.5f) * 12.f + std::sin((float)t * 0.7f + c * 6.f) * 2.f, 2.f + ph * 26.f, (c - 0.5f) * 12.f + std::cos((float)t * 0.6f + b * 6.f) * 2.f);
						float g = 0.30f + 0.12f * a;
						float alpha = std::min(1.f, smoke / 10.f + 0.3f) * std::sin(ph * 3.14159f) * 0.75f;
						cubeAt(p, 4.f + 3.f * c + ph * 3.f, b * 3.f + (float)t * 0.2f, glm::vec4(g, g, g * 1.04f, alpha));
					}
					if (fire)
					{
						sh.set("uMode", 2);
						for (int i = 0; i < 5; ++i)
						{
							uint32_t r = h + i * 40503u + 99u;
							float a = frand(r), b = frand(r), sp = 0.9f + 0.6f * frand(r);
							float ph = (float)std::fmod(t * sp + a, 1.0);
							glm::vec3 p = base + glm::vec3((a - 0.5f) * 10.f, 1.f + ph * 12.f, (b - 0.5f) * 10.f);
							glm::vec3 col = glm::mix(glm::vec3(3.f, 2.2f, 0.6f), glm::vec3(2.2f, 0.4f, 0.05f), ph);
							cubeAt(p, (2.6f - ph * 1.8f) * (0.8f + 0.4f * b), ph * 4.f, glm::vec4(col, 1.f - ph));
						}
						if (fire && ((x + y) & 3) == 0) flashes.push_back({glm::vec3(M * glm::vec4(base + glm::vec3(0, 6, 0), 1.f)), glm::vec3(1.f, 0.45f, 0.12f) * 0.25f});
					}
				}
		glDisable(GL_BLEND);
	}

	// particles: glowing ones additive, smoke and debris normal
	glEnable(GL_BLEND);
	glDepthMask(GL_FALSE);
	for (int pass = 0; pass < 2; ++pass)
	{
		if (pass == 0) { glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); sh.set("uMode", 0); }
		else { glBlendFunc(GL_SRC_ALPHA, GL_ONE); sh.set("uMode", 2); }
		uint32_t frameNo = (uint32_t)(animTime * 40.0);
		for (const Particle &p : particles)
		{
			if (p.glow != (pass == 1)) continue;
			if (p.flicker)
			{
				uint32_t h = (p.seed + frameNo * 2654435761u) ^ (p.seed >> 7);
				h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
				if ((h & 255) < 90) continue; // static: about a third of the voxels blink out each moment
			}
			float k = 1.f - glm::clamp(p.life / p.maxLife, 0.f, 1.f);
			glm::vec4 c = glm::mix(p.c0, p.c1, k);
			float size = p.glow ? p.size * (1.f - 0.5f * k) : p.size * (1.f + 1.2f * k);
			cubeAt(p.p, size, p.spin, c);
		}
	}

	// the projectile itself
	if (const Projectile *pr = map ? map->getProjectile() : nullptr)
	{
		Position v = pr->getPosition(0), v2 = pr->getPosition(-2);
		glm::vec3 now(v.x, v.z, v.y), prev(v2.x, v2.z, v2.y);
		glm::vec3 dir = now - prev;
		if (glm::length(dir) < 1e-3f) dir = now - projOrigin;
		dir = glm::length(dir) > 1e-3f ? glm::normalize(dir) : glm::vec3(1, 0, 0);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		sh.set("uMode", 2);
		float k = projPower;
		switch (projStyle)
		{
		case 0: // bullet: a short bright streak, longer and thicker for bigger calibres
			boxAlong(now - dir * (4.f + 5.f * k), now, 0.6f + 0.8f * k, glm::vec4(projColor * (1.4f + 0.5f * k), 1.f));
			break;
		case 1: // laser: a bolt with a glowing sheath and a beam back to the muzzle
		{
			float jit = 0.3f * std::sin((float)t * 90.f);
			boxAlong(projOrigin, now, (0.3f + 1.1f * k) * (1.f + 0.15f * jit), glm::vec4(projColor * (1.0f + 0.4f * k), 0.9f));
			boxAlong(projOrigin, now, 1.0f + 3.4f * k, glm::vec4(projColor * 0.7f, 0.1f + 0.14f * k));
			boxAlong(now - dir * (5.f + 6.f * k), now, 1.0f + 1.2f * k, glm::vec4(1.3f, 0.22f, 0.16f, 1.f));
			break;
		}
		case 2: // plasma: a tumbling cluster of glowing green voxels, bigger and denser for heavier guns
		{
			int n = 5 + (int)(8 * k);
			float rad = 1.0f + 1.4f * k;
			for (int i = 0; i < n; ++i)
			{
				float a1 = i * 2.39996f, z1 = 1.f - 2.f * (i + 0.5f) / n, rr = std::sqrt(std::max(0.f, 1.f - z1 * z1));
				glm::vec3 o(std::cos(a1) * rr, z1, std::sin(a1) * rr);
				glm::vec3 r = glm::vec3(glm::rotate(glm::mat4(1.f), (float)t * 9.f, glm::vec3(0.4f, 1.f, 0.3f)) * glm::vec4(o * rad, 0.f));
				cubeAt(now + r, 0.8f + 0.6f * k, (float)t * 5.f, glm::vec4(projColor * (1.0f + 0.3f * k), 0.9f));
			}
			cubeAt(now, 1.4f + 1.6f * k, (float)t * 7.f, glm::vec4(0.45f, 1.4f, 0.5f, 1.f));
			break;
		}
		case 3: // rocket: body, fins and a flame
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			sh.set("uMode", 0);
			float sz = 0.8f + 0.5f * k;
			boxAlong(now - dir * 7.f * sz, now, 1.6f * sz, glm::vec4(0.55f, 0.57f, 0.6f, 1.f));
			boxAlong(now - dir * 1.5f * sz, now + dir * 0.5f, 1.2f * sz, glm::vec4(0.8f, 0.25f, 0.15f, 1.f));
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			sh.set("uMode", 2);
			float fl = 0.7f + 0.3f * std::sin((float)t * 60.f);
			boxAlong(now - dir * (10.f + 2.f * fl) * sz, now - dir * 7.f * sz, 1.3f * fl * sz, glm::vec4(3.f, 1.8f, 0.4f, 1.f));
			break;
		}
		default: // thrown item: a tumbling block
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			sh.set("uMode", 0);
			cubeAt(now, 2.6f, (float)t * 8.f, glm::vec4(0.35f, 0.42f, 0.3f, 1.f));
			break;
		}
	}
	// a laser beam fades out after it lands
	if (beamFade > 0.f && projStyle == 1 && !(map && map->getProjectile()))
	{
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		sh.set("uMode", 2);
		float k = glm::clamp(beamFade / (0.12f + 0.1f * projPower), 0.f, 1.f);
		boxAlong(beamA, beamB, (0.35f + 0.7f * projPower) * k, glm::vec4(beamColor * 1.2f, k));
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	sh.set("uTint", glm::vec4(1.f));
}

// ------------------------------------------------------------------ HUD: cursor, selection, path preview

/// A text readout in the game's small font and palette colour, cached by content.
const Board::Impl::Label &Board::Impl::label(const std::string &text, Uint8 color)
{
	std::string key = text + '\x01' + std::to_string((int)color);
	auto it = labels.find(key);
	if (it != labels.end()) { it->second.used = time; return it->second; }
	if (labels.size() > 200)
	{
		for (auto i = labels.begin(); i != labels.end();)
			i = (time - i->second.used > 5.0) ? labels.erase(i) : std::next(i);
	}
	Label &l = labels[key];
	l.used = time;
	Mod *mod = game->getMod();
	Font *big = mod->getFont("FONT_BIG", false), *small = mod->getFont("FONT_SMALL", false);
	if (!big || !small) return l;
	const int W = 128, H = 20;
	Text t(W, H, 0, 0);
	SDL_Color sp[256];
	for (int i = 0; i < 256; ++i) sp[i] = pal[i];
	t.setPalette(sp);
	t.initText(big, small, game->getLanguage());
	t.setSmall();
	t.setHighContrast(true);
	t.setColor(color);
	t.setText(text);
	t.draw();
	int w = std::max(1, std::min(W, t.getTextWidth() + 1)), h = std::max(1, std::min(H, t.getTextHeight() + 1));
	std::vector<uint32_t> px((size_t)w * h, 0u);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			Uint8 c = t.getPixel(x, y);
			if (c) { const SDL_Color &k = pal[c]; px[(size_t)y * w + x] = 0xFF000000u | ((uint32_t)k.b << 16) | ((uint32_t)k.g << 8) | k.r; }
		}
	l.tex = std::make_unique<Texture>();
	l.tex->create(w, h, true, true);
	l.tex->update(px.data(), w, h);
	l.w = w;
	l.h = h;
	return l;
}

/// A wireframe box over a tile, like the game's 3D cursor. With xray > 0 a faint copy shows through walls.
void Board::Impl::drawWire(const Shader &sh, const glm::mat4 &M, const glm::vec3 &at, int size, const glm::vec4 &col, float xray) const
{
	const Mesh &m = wire[size > 1 ? 1 : 0];
	sh.set("uMode", 2);
	sh.set("uModel", M * glm::translate(glm::mat4(1.f), at));
	if (xray > 0.f)
	{
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		sh.set("uTint", glm::vec4(glm::vec3(col), xray));
		m.draw();
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
	}
	sh.set("uTint", col);
	m.draw();
}

/// A readout floating at a point (board voxels), turned to face the player, drawn over everything.
void Board::Impl::drawLabel(const Shader &sh, const glm::mat4 &M, const glm::vec3 &at, const glm::vec3 &viewerVox, const Label &l, float pxSize) const
{
	if (!l.tex) return;
	glm::vec3 toV = viewerVox - at;
	if (glm::length(toV) < 1e-3f) return;
	toV = glm::normalize(toV);
	glm::vec3 right = glm::cross(glm::vec3(0, 1, 0), toV);
	if (glm::length(right) < 1e-3f) right = glm::vec3(1, 0, 0);
	right = glm::normalize(right);
	glm::vec3 up = glm::normalize(glm::cross(toV, right));
	glm::mat4 B(1.f);
	B[0] = glm::vec4(right * (l.w * pxSize), 0.f);
	B[1] = glm::vec4(up * (l.h * pxSize), 0.f);
	B[2] = glm::vec4(toV, 0.f);
	B[3] = glm::vec4(at + up * (l.h * pxSize * 0.5f), 1.f);
	sh.set("uMode", 4);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", M * B);
	l.tex->bind(0);
	quad.draw();
}

void Board::Impl::drawHud(const Shader &sh, const glm::mat4 &M)
{
	if (!bs || !bs->getMap()) return;
	Map *map = bs->getMap();
	glm::vec3 viewerVox = glm::vec3(glm::inverse(M) * glm::vec4(viewer, 1.f));
	const float PX = 0.75f; // label pixel size in voxels (about 2.5 mm on the table at the default zoom)
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	auto floorAt = [&](const Position &p) -> float
	{
		Tile *t = battle->getTile(p);
		return p.z * TILE_H - (t ? (float)t->getTerrainLevel() : 0.f);
	};
	auto markerIndex = [](int marker) -> int { return Palette::blockOffset(marker - 1) - 1; };
	// a HUD mesh, plus a faint copy that shows through walls and roofs
	auto xrayDraw = [&](const Mesh &mesh, const glm::mat4 &model, const glm::vec4 &col)
	{
		sh.set("uMode", 2);
		sh.set("uModel", model);
		glDisable(GL_DEPTH_TEST);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		sh.set("uTint", glm::vec4(glm::vec3(col), 0.3f));
		mesh.draw();
		glEnable(GL_DEPTH_TEST);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		sh.set("uTint", col);
		mesh.draw();
	};
	std::vector<std::pair<glm::vec3, const Label*>> texts;

	// ---- path preview, as the flat map shows it (Options: battleNewPreviewPath)
	PathPreview ps = Options::traceAI ? PATH_ARROW_TU : Options::battleNewPreviewPath;
	bool arrowsOn = ps & PATH_ARROWS, tuOn = ps & PATH_TU_COST, enOn = ps & PATH_ENERGY_COST;
	if (battle->getPathfinding() && battle->getPathfinding()->isPathPreviewed())
	{
		int msg = 0;
		if (RuleInterface *ri = game->getMod()->getInterface("battlescape", false))
			if (const Element *el = ri->getElement("messageWindows")) msg = el->color + 1;
		for (int z = 0; z <= std::min(viewLevel, mz - 1); ++z)
			for (int y = 0; y < my; ++y)
				for (int x = 0; x < mx; ++x)
				{
					Tile *t = battle->getTile(Position(x, y, z));
					if (!t || t->getPreview() == -1 || !t->isDiscovered(O_FLOOR)) continue;
					int ci = markerIndex(t->getMarkerColor());   // text colour (the font adds its shades on top)
					glm::vec4 col = palColor(Palette::blockOffset(t->getMarkerColor() - 1) + 3, 1.0f); // a bright shade of that colour
					glm::vec3 c(x * TILE_W + 8.f, floorAt(Position(x, y, z)) + 0.9f, y * TILE_W + 8.f);
					int pv = t->getPreview();
					if (arrowsOn)
					{
						glm::mat4 m = M * glm::translate(glm::mat4(1.f), c);
						if (pv >= 0 && pv < 8) m = m * glm::rotate(glm::mat4(1.f), glm::radians(-45.f * pv), {0, 1, 0});
						else if (pv == Pathfinding::DIR_UP) m = glm::translate(m, {0, 7, 0}) * glm::rotate(glm::mat4(1.f), glm::radians(90.f), {1, 0, 0});
						else if (pv == Pathfinding::DIR_DOWN) m = glm::translate(m, {0, 7, 0}) * glm::rotate(glm::mat4(1.f), glm::radians(-90.f), {1, 0, 0});
						if (pv == 10) xrayDraw(ring, m * glm::scale(glm::mat4(1.f), glm::vec3(6.f, 1.f, 6.f)), col);
						else xrayDraw(arrow, m, col);
					}
					if ((tuOn || enOn) && (t->getTUMarker() > -1 || t->getEnergyMarker() > -1))
					{
						std::string txt;
						if (tuOn) txt = std::to_string(std::max(0, t->getTUMarker()));
						if (enOn) txt += (txt.empty() ? "" : "\n") + std::to_string(std::max(0, t->getEnergyMarker()));
						const Label &l = label(txt, (Uint8)(arrowsOn ? msg : ci));
						texts.push_back({c + glm::vec3(0.f, arrowsOn ? 3.5f : 1.5f, 0.f), &l});
					}
				}
	}

	CursorType ct = map->getCursorType();
	bool show = ct != CT_NONE && (battle->getSide() == FACTION_PLAYER || battle->getDebugMode());

	// ---- the selected soldier: a yellow wireframe box and the game's bobbing arrow over its head
	BattleUnit *sel = battle->getSelectedUnit();
	if (show && sel && !sel->isOut() && sel->getPosition().z <= viewLevel)
	{
		auto it = cards.find(sel);
		glm::vec3 p = (it != cards.end() && it->second.visInit) ? it->second.vis : unitVoxelPos(sel);
		int size = sel->getArmor()->getSize();
		glm::vec3 corner(p.x - TILE_W * size * 0.5f, p.y, p.z - TILE_W * size * 0.5f);
		drawWire(sh, M, corner, size, glm::vec4(2.0f, 1.7f, 0.2f, 1.f), 0.35f);
		float head = (float)(sel->getHeight() + sel->getFloatHeight());
		float bob = 1.2f * (float)std::sin(time * 4.0);
		xrayDraw(pointer, M * glm::translate(glm::mat4(1.f), p + glm::vec3(0.f, head + 9.f + bob, 0.f)), glm::vec4(2.0f, 1.7f, 0.2f, 1.f));
		if (turnPreview && glm::length(turnDir) > 1e-3f)
		{
			// the direction the soldier will face: a long arrow on the ground from its feet
			float ang = std::atan2(turnDir.x, -turnDir.y); // arrow mesh points -Z
			for (int k = 0; k < 3; ++k)
			{
				glm::vec3 at = p + glm::vec3(turnDir.x, 0.f, turnDir.y) / glm::length(turnDir) * (9.f + 9.f * k) + glm::vec3(0.f, 1.f, 0.f);
				xrayDraw(arrow, M * glm::translate(glm::mat4(1.f), at) * glm::rotate(glm::mat4(1.f), -ang, {0, 1, 0}) * glm::scale(glm::mat4(1.f), glm::vec3(1.2f)),
					glm::vec4(2.0f, 1.7f, 0.2f, 1.f - 0.25f * k));
			}
		}
	}

	// ---- the cursor: last tile hovered (laser or finger) or picked
	// (a hovering fingertip wins over the laser / mouse)
	if (fingerFrames > 0) { cursorTile = fingerTile; cursorSet = true; }
	else if (hoverValid && !hoverGlobe) { cursorTile = hoverTile; cursorSet = true; }
	if (show && cursorSet && battle->getTile(cursorTile))
	{
		Tile *t = battle->getTile(cursorTile);
		BattleUnit *u = t->getUnit();
		bool unitThere = u && (u->getVisible() || battle->getDebugMode());
		bool flash = std::fmod(time * 4.0, 1.0) < 0.5;
		glm::vec4 red(2.2f, 0.12f, 0.08f, 1.f), yellow(2.0f, 1.7f, 0.2f, 1.f), blue(0.4f, 0.8f, 2.4f, 1.f);
		glm::vec4 col = cursorTile.z < viewLevel ? blue : unitThere ? (flash ? yellow : red) : red;
		int size = std::max(1, map->getCursorSize());
		glm::vec3 corner(cursorTile.x * TILE_W, floorAt(cursorTile) + 0.3f, cursorTile.y * TILE_W);
		drawWire(sh, M, corner, size, col, 0.3f);
		if (ct != CT_NORMAL)
		{
			// aiming / throwing / psi / waypoint: a crosshair ring over the box, spinning when on a target
			glm::vec4 rc = ct == CT_AIM ? (unitThere ? yellow : red)
				: ct == CT_THROW ? glm::vec4(2.4f, 1.4f, 0.3f, 1.f)
				: ct == CT_PSI ? glm::vec4(1.6f, 0.6f, 2.4f, 1.f) : glm::vec4(0.5f, 2.3f, 0.8f, 1.f);
			float spin = unitThere ? (float)time * 2.f : 0.f;
			float half = TILE_W * size * 0.5f;
			xrayDraw(ring, M * glm::translate(glm::mat4(1.f), corner + glm::vec3(half, TILE_H + 1.f, half))
				* glm::rotate(glm::mat4(1.f), spin, {0, 1, 0}) * glm::scale(glm::mat4(1.f), glm::vec3(half * 0.8f, 1.f, half * 0.8f)), rc);
		}
		// the readout the flat map prints next to the cursor (hit chance, damage with Alt, out of range)
		std::string info;
		Uint8 infoColor = 0;
		bool hasInfo = map->getCursorInfo(cursorTile, info, infoColor);
		if (!hasInfo && !map->isAltPressed() && (ct == CT_PSI || ct == CT_WAYPOINT))
		{
			BattleAction *action = bs->getBattleGame()->getCurrentAction();
			if (action && action->actor && action->weapon
				&& action->weapon->getRules()->isOutOfRange(action->actor->distance3dToPositionSq(cursorTile)))
			{
				info = "0%";
				infoColor = (Uint8)markerIndex(Pathfinding::red);
				hasInfo = true;
			}
		}
		if (hasInfo && !info.empty())
		{
			// beside the box on the player's right, so the pointing hand does not cover it
			const Label &l = label(info, infoColor);
			float half = TILE_W * size * 0.5f;
			glm::vec3 centre = corner + glm::vec3(half, 0.f, half);
			glm::vec3 toV = viewerVox - centre;
			toV.y = 0.f;
			glm::vec3 right = glm::length(toV) > 1e-3f ? glm::normalize(glm::cross(glm::vec3(0, 1, 0), toV)) : glm::vec3(1, 0, 0);
			texts.push_back({centre + right * (half * 1.45f + l.w * PX * 0.5f) + glm::vec3(0.f, TILE_H * 0.45f, 0.f), &l});
		}
	}

	// waypoints set for a guided weapon, numbered like on the flat map
	if (show)
		if (std::vector<Position> *wps = map->getWaypoints())
			for (size_t i = 0; i < wps->size(); ++i)
			{
				const Position &w = (*wps)[i];
				glm::vec3 corner(w.x * TILE_W, floorAt(w) + 0.3f, w.y * TILE_W);
				drawWire(sh, M, corner, 1, glm::vec4(2.0f, 1.7f, 0.2f, 1.f), 0.3f);
				texts.push_back({corner + glm::vec3(8.f, TILE_H + 2.f, 8.f), &label(std::to_string(i + 1), (Uint8)markerIndex(Pathfinding::yellow))});
			}

	// readouts last, over everything, so they stay legible
	glDisable(GL_DEPTH_TEST);
	for (auto &tx : texts) drawLabel(sh, M, tx.first, viewerVox, *tx.second, PX);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
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
	glm::vec3 tc = layout.tableCenter;
	_p->mapRect = glm::vec4(tc.x - layout.tableSize.x * 0.5f, tc.z - layout.tableSize.y * 0.5f, tc.x + layout.tableSize.x * 0.5f, tc.z + layout.tableSize.y * 0.5f);
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
	{
		MeshData cb;
		cb.addBox({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, glm::vec4(1), 0);
		_p->cube.upload(cb);
	}
	// HUD meshes: wireframe boxes for one and two tiles (thin bars along the 12 edges)
	for (int n = 0; n < 2; ++n)
	{
		MeshData w;
		const float E = 0.7f, X = TILE_W * (n + 1.f), Y = (float)TILE_H, Z = TILE_W * (n + 1.f);
		for (float yy : {0.f, Y - E})
			for (float zz : {0.f, Z - E}) w.addBox({0, yy, zz}, {X, yy + E, zz + E}, glm::vec4(1), 0);
		for (float yy : {0.f, Y - E})
			for (float xx : {0.f, X - E}) w.addBox({xx, yy, 0}, {xx + E, yy + E, Z}, glm::vec4(1), 0);
		for (float xx : {0.f, X - E})
			for (float zz : {0.f, Z - E}) w.addBox({xx, 0, zz}, {xx + E, Y, zz + E}, glm::vec4(1), 0);
		_p->wire[n].upload(w);
	}
	{
		// path arrow lying on the floor, pointing -Z (map north)
		MeshData a;
		const float T = 0.8f;
		a.addBox({-1.2f, 0, -1.f}, {1.2f, T, 5.f}, glm::vec4(1), 0);
		glm::vec3 l(-3.6f, 0, -1.f), r(3.6f, 0, -1.f), tip(0, 0, -6.5f), up(0, T, 0);
		a.addQuad(l + up, r + up, tip + up, tip + up, glm::vec4(1), 0);
		a.addQuad(l, tip, tip, r, glm::vec4(1), 0);
		a.addQuad(l, l + up, tip + up, tip, glm::vec4(1), 0);
		a.addQuad(tip, tip + up, r + up, r, glm::vec4(1), 0);
		a.addQuad(r, r + up, l + up, l, glm::vec4(1), 0);
		_p->arrow.upload(a);
		// selected-unit arrow: a downward pointer
		MeshData d;
		glm::vec3 apex(0, 0, 0);
		float s = 2.6f, h = 4.5f;
		glm::vec3 c0(-s, h, -s), c1(s, h, -s), c2(s, h, s), c3(-s, h, s);
		d.addQuad(apex, apex, c1, c0, glm::vec4(1), 0);
		d.addQuad(apex, apex, c2, c1, glm::vec4(1), 0);
		d.addQuad(apex, apex, c3, c2, glm::vec4(1), 0);
		d.addQuad(apex, apex, c0, c3, glm::vec4(1), 0);
		d.addQuad(c0, c1, c2, c3, glm::vec4(1), 0);
		d.addBox({-1.f, h, -1.f}, {1.f, h + 3.5f, 1.f}, glm::vec4(1), 0);
		_p->pointer.upload(d);
	}
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
void Board::setMapRect(const glm::vec4 &rect) { _p->mapRect = rect; }
BattlescapeState *Board::battleState() const { return _p->hasBattle ? _p->bs : nullptr; }
SavedBattleGame *Board::battle() const { return _p->hasBattle ? _p->battle : nullptr; }

std::vector<Board::UnitMarker> Board::unitMarkers() const
{
	std::vector<UnitMarker> out;
	const Impl &p = *_p;
	if (!p.hasBattle || !p.battle) return out;
	glm::mat4 M = p.boardMatrix();
	float k = p.tileSize / TILE_W;
	for (BattleUnit *u : *p.battle->getUnits())
	{
		auto it = p.cards.find(u);
		if (it == p.cards.end() || !it->second.visInit || u->isOut()) continue;
		if (!(u->getFaction() == FACTION_PLAYER || u->getVisible() || p.battle->getDebugMode())) continue;
		if (u->getPosition().z > p.viewLevel) continue;
		const UnitCard &c = it->second;
		// same placement as the standee in drawUnits: pixel row r sits at anchorY - r above the base
		glm::vec3 local = c.vis + glm::vec3(0.f, 1.3f + c.anchorY - c.topRow - 5.f, 0.f);
		glm::vec3 w = glm::vec3(M * glm::vec4(local, 1.f));
		glm::vec4 clip = p.clipRect();
		if (w.x < clip.x || w.x > clip.z || w.z < clip.y || w.z > clip.w) continue;
		out.push_back({u, w, std::max(0.016f, 6.f * k), u->getFaction() == p.battle->getSide()});
	}
	return out;
}

bool Board::tileUnder(const glm::vec3 &world, int &tx, int &ty, int &tz, float &floorY) const
{
	const Impl &p = *_p;
	if (!p.hasBattle || !p.battle) return false;
	glm::vec4 clip = p.clipRect();
	if (world.x < clip.x || world.x > clip.z || world.z < clip.y || world.z > clip.w) return false;
	glm::mat4 M = p.boardMatrix();
	glm::vec3 l = glm::vec3(glm::inverse(M) * glm::vec4(world, 1.f));
	int x = (int)std::floor(l.x / TILE_W), y = (int)std::floor(l.z / TILE_W);
	if (x < 0 || y < 0 || x >= p.mx || y >= p.my) return false;
	// fog: the top of the fog bank is the tile at the highest fogged level (as on the flat map,
	// clicking into the dark picks the tile at the level being viewed)
	if (int f = p.fogTop(x, y))
	{
		tx = x; ty = y; tz = f - 1;
		floorY = glm::vec3(M * glm::vec4(l.x, (float)((f - 1) * TILE_H + 13), l.z, 1.f)).y;
		return true;
	}
	for (int z = std::min(p.viewLevel, p.mz - 1); z >= 0; --z)
	{
		Tile *t = p.battle->getTile(Position(x, y, z));
		if (!t) continue;
		bool floor = z == 0 || (t->isDiscovered(O_FLOOR) && (t->getMapData(O_FLOOR) || t->getMapData(O_OBJECT)));
		if (!floor) continue;
		float fy = (float)(z * TILE_H - t->getTerrainLevel());
		if (l.y < fy - 3.f && z > 0) continue; // below this floor: look further down
		tx = x; ty = y; tz = z;
		floorY = glm::vec3(M * glm::vec4(l.x, fy, l.z, 1.f)).y;
		return true;
	}
	return false;
}

glm::vec3 Board::tileCenter(int tx, int ty, int tz) const
{
	const Impl &p = *_p;
	float lvl = 0.f;
	if (p.battle)
		if (Tile *t = p.battle->getTile(Position(tx, ty, tz))) lvl = (float)t->getTerrainLevel();
	return glm::vec3(p.boardMatrix() * glm::vec4(tx * TILE_W + 8.f, tz * TILE_H - lvl, ty * TILE_W + 8.f, 1.f));
}

std::string Board::debugInfo() const
{
	const Impl &p = *_p;
	std::ostringstream ss;
	ss << "focus " << p.focus.x << "," << p.focus.z << " target " << p.focusTarget.x << "," << p.focusTarget.z << " tileSize " << p.tileSize << " yaw " << p.yaw;
	if (p.battle && p.battle->getSelectedUnit())
	{
		BattleUnit *u = p.battle->getSelectedUnit();
		auto it = p.cards.find(u);
		glm::vec3 v = (it != p.cards.end() && it->second.visInit) ? it->second.vis : glm::vec3(-1.f);
		ss << " unit " << v.x << "," << v.z << " inView30 " << p.inView(v, 0.3f) << " inView60 " << p.inView(v, 0.6f);
	}
	return ss.str();
}

bool Board::tileCoords(const glm::vec3 &world, glm::vec2 &tile) const
{
	const Impl &p = *_p;
	if (!p.hasBattle || !p.battle) return false;
	glm::vec3 l = glm::vec3(glm::inverse(p.boardMatrix()) * glm::vec4(world, 1.f));
	tile = glm::vec2(l.x, l.z) / (float)TILE_W;
	return true;
}

void Board::setTurnPreview(bool on, const glm::vec2 &dir)
{
	_p->turnPreview = on;
	_p->turnDir = dir;
}

void Board::fingerHover(int tx, int ty, int tz)
{
	_p->fingerTile = Position(tx, ty, tz);
	_p->fingerFrames = 2;
}

void Board::clickTile(int tx, int ty, int tz, bool right)
{
	Impl &p = *_p;
	revalidate(p);
	p.cursorTile = Position(tx, ty, tz);
	p.cursorSet = true;
	if (p.bs) p.bs->vrTileClick(Position(tx, ty, tz), right);
}

bool Board::selectUnit(BattleUnit *unit)
{
	Impl &p = *_p;
	revalidate(p);
	return p.bs ? p.bs->vrSelectUnit(unit) : false;
}
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

void Board::setShadowPass(bool on, const glm::vec3 &light)
{
	// standees turn toward whoever looks at them: in the shadow pass that is the light, so they
	// cast their full silhouette
	static glm::vec3 saved;
	if (on && !_p->shadowPass) { saved = _p->viewer; _p->viewer = light; }
	else if (!on && _p->shadowPass) _p->viewer = saved;
	_p->shadowPass = on;
}

void Board::lights(std::vector<std::pair<glm::vec3, glm::vec3>> &out) const
{
	for (const auto &f : _p->flashes) out.push_back({f.p, f.col});
}

bool Board::alert() const
{
	const Impl &p = *_p;
	if (!p.hasBattle || !p.battle) return false;
	if (p.battle->getSide() != FACTION_PLAYER) return true;
	for (BattleUnit *u : *p.battle->getUnits())
		if (u->getFaction() == FACTION_HOSTILE && !u->isOut() && u->getVisible()) return true;
	return false;
}

float Board::nightLevel() const
{
	const Impl &p = *_p;
	if (!p.hasBattle || !p.battle) return 0.f;
	// the game's darkness runs 0 (day) .. 15; night missions sit around 10 and up
	return glm::clamp((p.battle->getGlobalShade() - 2) / 8.f, 0.f, 1.f);
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
			if (p.fogTop(tx, ty) == z + 1) standable = true; // the top of the fog bank
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

/// Points the game's camera at the board's focus, and remembers that the board did it, so the
/// next update does not snap the focus back to the camera's whole-tile centre.
static void syncCamera(Board::Impl &p)
{
	if (!p.bs || !p.bs->getMap()) return;
	Position c((int)(p.focus.x / TILE_W), (int)(p.focus.z / TILE_W), p.viewLevel);
	p.bs->getMap()->getCamera()->centerOnPosition(c, true);
	p.camSet = p.bs->getMap()->getCamera()->getCenterPosition();
	p.camSetValid = true;
	p.focusTarget = p.focus;
}

/// During the alien turn the game decides where the table looks (and while the movement is hidden,
/// the table is covered): the player can't move the map then.
static bool viewLocked(const Board::Impl &p)
{
	return p.hasBattle && p.battle && p.battle->getSide() != FACTION_PLAYER && !p.battle->getDebugMode();
}

bool Board::viewLocked() const { return OpenXcom::VR::viewLocked(*_p); }
float Board::curtain() const { return _p->hasBattle ? _p->curtain : 0.f; }

void Board::pan(const glm::vec2 &d)
{
	Impl &p = *_p;
	revalidate(p);
	if (p.hasBattle)
	{
		if (OpenXcom::VR::viewLocked(p)) return;
		float k = p.tileSize / TILE_W;
		glm::vec3 dl = glm::vec3(glm::rotate(glm::mat4(1.f), -p.yaw, {0, 1, 0}) * glm::vec4(d.x, 0.f, d.y, 0.f)) / k;
		p.focus.x = glm::clamp(p.focus.x + dl.x, 0.f, (float)(p.mx * TILE_W));
		p.focus.z = glm::clamp(p.focus.z + dl.z, 0.f, (float)(p.my * TILE_W));
		syncCamera(p);
	}
	else if (p.hasGlobe && p.globe)
	{
		double cl, ca;
		p.globe->getCenter(&cl, &ca);
		cl += d.x / p.globeRadius;
		ca = glm::clamp(ca + (double)d.y / p.globeRadius, -1.5, 1.5);
		p.globe->center(cl, ca);
	}
}

void Board::rotate(float r)
{
	Impl &p = *_p;
	if (p.hasBattle && p.hidden) return;
	if (p.hasBattle) p.yaw += r;
	else if (p.hasGlobe && p.globe)
	{
		double cl, ca;
		p.globe->getCenter(&cl, &ca);
		p.globe->center(cl - r, ca);
	}
}

void Board::zoom(float f)
{
	Impl &p = *_p;
	if (p.hasBattle && p.hidden) return;
	if (p.hasBattle) p.tileSize = glm::clamp(p.tileSize * f, 0.02f, 0.14f);
	else if (p.hasGlobe) p.globeRadius = glm::clamp(p.globeRadius * f, 0.18f, 0.45f);
}

// ------------------------------------------------------------------ minimap (wrist pad, wall screen)

/// The minimap's square: the whole map, turned like the table when align is set (the table's far
/// side is up). Returns centre (tiles), right and up directions (tiles per unit of the square).
static void minimapFrame(const Board::Impl &p, bool align, glm::vec2 &centre, glm::vec2 &right, glm::vec2 &up)
{
	float yaw = align ? p.yaw : 0.f;
	float c = std::cos(yaw), sn = std::sin(yaw);
	// board-local directions (tile x, tile y) of world +x and world -z: the board matrix turns local
	// into world by yaw about +Y, so these are world +x / -z turned back by -yaw
	glm::vec2 r(c, sn), u(sn, -c);
	float ext = (float)std::max(p.mx, p.my) * (std::fabs(c) + std::fabs(sn)) * 1.04f;
	centre = glm::vec2(p.mx * 0.5f, p.my * 0.5f);
	right = r * ext;
	up = u * ext;
}

bool Board::renderMinimap(std::vector<uint32_t> &px, int S, bool align)
{
	Impl &p = *_p;
	revalidate(p);
	if (!p.hasBattle || !p.battle || S <= 0) return false;
	if (p.curtain > 0.5f)
	{
		// the movement is hidden: no map, just the game's message
		px.assign((size_t)S * S, 0xFF140806u);
		p.buildHiddenText();
		if (p.hiddenText.empty() || p.hiddenTextW <= 0) return true;
		int sc = std::max(1, std::min(2, (S - 12) / p.hiddenTextW));
		int x0 = (S - p.hiddenTextW * sc) / 2, y0 = (S - p.hiddenTextH * sc) / 2;
		for (int y = 0; y < p.hiddenTextH * sc; ++y)
			for (int x = 0; x < p.hiddenTextW * sc; ++x)
			{
				uint32_t c = p.hiddenText[(size_t)(y / sc) * p.hiddenTextW + x / sc];
				int xx = x0 + x, yy = y0 + y;
				if ((c >> 24) && xx >= 0 && yy >= 0 && xx < S && yy < S) px[(size_t)yy * S + xx] = c;
			}
		return true;
	}
	glm::vec2 c, r, u;
	minimapFrame(p, align, c, r, u);
	const uint32_t fog = 0xFF2A2018u, outside = 0xFF0C0806u;
	px.assign((size_t)S * S, outside);
	int top = std::min(p.viewLevel, p.mz - 1);
	for (int j = 0; j < S; ++j)
		for (int i = 0; i < S; ++i)
		{
			float uu = (i + 0.5f) / S - 0.5f, vv = 0.5f - (j + 0.5f) / S;
			glm::vec2 t = c + r * uu + u * vv;
			int tx = (int)std::floor(t.x), ty = (int)std::floor(t.y);
			if (tx < 0 || ty < 0 || tx >= p.mx || ty >= p.my) continue;
			uint32_t col = fog;
			bool seen = false;
			for (int z = top; z >= 0; --z)
			{
				Tile *tl = p.battle->getTile(Position(tx, ty, z));
				if (!tl) continue;
				if (!tl->isDiscovered(O_FLOOR) && !p.battle->getDebugMode()) continue;
				seen = true;
				const MapData *md = nullptr;
				for (TilePart part : {O_OBJECT, O_NORTHWALL, O_WESTWALL, O_FLOOR})
					if ((md = tl->getMapData(part))) break;
				if (!md) continue;
				uint32_t a = p.proto(md).avg;
				if (!a) continue;
				// lower levels a little darker, so roofs read above the ground
				float k = 0.55f + 0.45f * (float)(z + 1) / (float)(top + 1);
				uint32_t rr = (uint32_t)((a & 255) * k), gg = (uint32_t)(((a >> 8) & 255) * k), bb = (uint32_t)(((a >> 16) & 255) * k);
				col = rr | (gg << 8) | (bb << 16) | 0xFF000000u;
				break;
			}
			if (!seen) col = fog;
			px[(size_t)j * S + i] = col;
		}
	// tile -> pixel
	glm::vec2 rn = r / glm::dot(r, r), un = u / glm::dot(u, u);
	auto toPx = [&](glm::vec2 t) { glm::vec2 d = t - c; return glm::vec2((glm::dot(d, rn) + 0.5f) * S, (0.5f - glm::dot(d, un)) * S); };
	auto dot = [&](glm::vec2 q, float rad, uint32_t col)
	{
		for (int y = (int)(q.y - rad - 1); y <= (int)(q.y + rad + 1); ++y)
			for (int x = (int)(q.x - rad - 1); x <= (int)(q.x + rad + 1); ++x)
				if (x >= 0 && y >= 0 && x < S && y < S && (x + 0.5f - q.x) * (x + 0.5f - q.x) + (y + 0.5f - q.y) * (y + 0.5f - q.y) <= rad * rad)
					px[(size_t)y * S + x] = col;
	};
	auto line = [&](glm::vec2 a, glm::vec2 b, uint32_t col)
	{
		int n = (int)std::max(std::fabs(b.x - a.x), std::fabs(b.y - a.y)) + 1;
		for (int k = 0; k <= n; ++k)
		{
			glm::vec2 q = glm::mix(a, b, (float)k / n);
			int x = (int)q.x, y = (int)q.y;
			if (x >= 0 && y >= 0 && x < S && y < S) px[(size_t)y * S + x] = col;
		}
	};
	// the part of the map the table shows
	{
		glm::mat4 inv = glm::inverse(p.boardMatrix());
		glm::vec4 m = p.mapRect;
		glm::vec3 cw[4] = {{m.x, p.surfaceY(), m.y}, {m.z, p.surfaceY(), m.y}, {m.z, p.surfaceY(), m.w}, {m.x, p.surfaceY(), m.w}};
		glm::vec2 q[4];
		for (int k = 0; k < 4; ++k) { glm::vec3 l = glm::vec3(inv * glm::vec4(cw[k], 1.f)); q[k] = toPx(glm::vec2(l.x, l.z) / (float)TILE_W); }
		for (int k = 0; k < 4; ++k) line(q[k], q[(k + 1) % 4], 0xFFE0D040u);
	}
	// units
	float rad = std::max(1.2f, S / 90.f);
	BattleUnit *sel = p.battle->getSelectedUnit();
	for (BattleUnit *un2 : *p.battle->getUnits())
	{
		if (un2->isOut()) continue;
		bool ours = un2->getFaction() == FACTION_PLAYER;
		if (!ours && !un2->getVisible() && !p.battle->getDebugMode()) continue;
		auto it = p.cards.find(un2);
		glm::vec3 v = (it != p.cards.end() && it->second.visInit) ? it->second.vis : p.unitVoxelPos(un2);
		glm::vec2 q = toPx(glm::vec2(v.x, v.z) / (float)TILE_W);
		uint32_t col = un2 == sel ? 0xFF30E8FFu : ours ? 0xFFFF9030u : un2->getFaction() == FACTION_HOSTILE ? 0xFF3030FFu : 0xFF40E040u;
		dot(q, un2 == sel ? rad * 1.6f : rad, 0xFF000000u);
		dot(q, (un2 == sel ? rad * 1.6f : rad) - 0.8f, col);
	}
	return true;
}

glm::vec2 Board::minimapTile(const glm::vec2 &uv, bool align) const
{
	glm::vec2 c, r, u;
	minimapFrame(*_p, align, c, r, u);
	return c + r * uv.x + u * uv.y;
}

void Board::testBurst(int tx, int ty, int tz, bool big)
{
	_p->spawnBurst(glm::vec3(tx * TILE_W + 8.f, tz * TILE_H + 6.f, ty * TILE_W + 8.f), big, false);
}

void Board::centerOnTile(const glm::vec2 &tile)
{
	Impl &p = *_p;
	revalidate(p);
	if (!p.hasBattle || OpenXcom::VR::viewLocked(p)) return;
	p.focus = glm::vec3(glm::clamp(tile.x, 0.f, (float)p.mx) * TILE_W, 0.f, glm::clamp(tile.y, 0.f, (float)p.my) * TILE_W);
	syncCamera(p);
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
	if (OpenXcom::VR::viewLocked(*_p)) return;
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
			syncCamera(p);
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
