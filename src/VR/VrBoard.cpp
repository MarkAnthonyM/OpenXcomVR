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
#include "VrBase.h"
#include "VrRoom.h"
#include "VrWeather.h"
#include "VrLab.h"
#include "VrRelief.h"
#include "VrDash.h"
#include "VrCompare.h"
#include "VrModels.h"
#include "VrVersion.h"
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
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleTerrain.h"
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
#include "../Battlescape/TileEngine.h"
#include "../Interface/Text.h"
#include "../Engine/Font.h"
#include "../Engine/Language.h"
#include "../Mod/RuleInterface.h"
#include "../Mod/RuleItem.h"
#include "../Geoscape/GeoscapeState.h"
#include "../Geoscape/Globe.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <array>
#include <map>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include <functional>
#include <sstream>

namespace OpenXcom
{
namespace VR
{

static const int TILE_W = 16;   // voxels per tile, horizontally
static const int TILE_H = 24;   // voxels per tile, vertically
static const int SHAPE_H = TILE_H + 16; // a tile's display shape may rise into the level above (a tree's crown)
static const int CHUNK = 8;     // tiles per chunk side
static const int ATLAS = 2048;
static const float MAT_BOARD = 8.f;
// hand-built lights (VrModels.h Model::light; 0.21.0-terrain.16): glow in their own colour, animated in step
// with the game's terrain frames (shader: uAnimFrame, uPulse, uFlip)
static const float MAT_GLOW_A = 10.f, MAT_GLOW_B = 11.f, MAT_GLOW_PULSE = 12.f, MAT_GLOW_FLIP = 13.f;
static const float MAT_PANEL = 14.f; // a hand-built model's face lit by its panel's direction (terrain.22): as MAT_BOARD, never turned to the eye

/// How much a tile's shade (0 bright .. 15 dark, the game's lighting) darkens it, in linear light.
/// Measured from the battlescape palette: each shade step moves a colour one step down its 16-colour
/// ramp, which is roughly a 20-30% drop per step (the median over the palette's ramps). The board
/// works in linear colour, so this is what makes a night tile as dark as on the flat map.
static float shadeFactor(int shade)
{
	static const float k[16] = {1.f, 0.804f, 0.639f, 0.506f, 0.397f, 0.306f, 0.232f, 0.172f,
		0.129f, 0.091f, 0.062f, 0.041f, 0.028f, 0.020f, 0.015f, 0.013f};
	return k[glm::clamp(shade, 0, 15)];
}

static inline uint32_t rgba(const SDL_Color &c, uint8_t a = 255)
{
	return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)a << 24);
}

static uint32_t scoreColor(int shape); // the lab's comparison (below)

// ------------------------------------------------------------------ atlas

struct Atlas
{
	std::vector<uint32_t> px;
	// shelves by height class (0.21.0-terrain.13): a face goes on a shelf of its height rounded up (4, 8,
	// 12, 16, then by 8), so the hand-built models' tall thin stalk faces and their tiny ear faces don't share
	// rows (one row as tall as its tallest face wasted most of the atlas with the wheat)
	struct Shelf { int h, x, y; };
	std::vector<Shelf> shelves;
	int nextY = 0;
	long texels = 0; // handed out
	bool dirty = false, full = false;
	VR::Texture tex;

	void reset()
	{
		px.assign((size_t)ATLAS * ATLAS, 0u);
		shelves.clear();
		nextY = 0;
		texels = 0;
		dirty = true;
		full = false;
	}
	int usedPercent() const { return (int)(texels * 100 / ((long)ATLAS * ATLAS)); }
	bool alloc(int w, int h, int &ox, int &oy)
	{
		if (w > ATLAS || h > ATLAS) { full = true; return false; }
		const int bh = h <= 4 ? 4 : h <= 8 ? 8 : h <= 12 ? 12 : h <= 16 ? 16 : (h + 7) / 8 * 8;
		Shelf *s = nullptr;
		for (Shelf &c : shelves)
			if (c.h == bh && c.x + w <= ATLAS) { s = &c; break; }
		if (!s)
		{
			if (nextY + bh > ATLAS) { full = true; return false; }
			shelves.push_back({bh, 0, nextY});
			nextY += bh;
			s = &shelves.back();
		}
		ox = s->x; oy = s->y;
		s->x += w;
		texels += (long)w * h;
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
	uint8_t stray = 0; // tile shapes: parts (bits) whose faces landing on empty art were left out
	uint8_t grew = 0;  // tile shapes: objects (bits) that grew display voxels into their art (crowns)
	uint8_t model = 0; // tile shapes: parts (bits) drawn from a hand-built model (VrModels.h)
	int8_t yoffs[O_MAX] = {0, 0, 0, 0}; // per part: sprite row = the shape's projected row + this (lab comparison)
};

struct Chunk
{
	uint64_t hash = ~0ull;
	uint64_t terrain = 0;    // its terrain alone (parts, UFO doors): when that changes, sun-reach traces are redone
	bool terrainKnown = false;
	std::unique_ptr<Mesh> mesh;
	bool empty = true;
	size_t tris = 0; // triangles in its mesh (boardinfo)
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
	BaseDiorama baseView;       // the base screen: the base on the table
	bool hasBase = false;
	// ---- the sky over an outdoor battle: sun, rain, snow, dust, mist (VrWeather)
	Weather weather;
	std::vector<float> stopCache;  // per tile column: where rain and snow stop (map voxels)
	std::vector<uint8_t> openCache; // per tile column: explored ground under the open sky
	float stopTimer = 0.f;
	int stopLevel = -1;
	float stopMax = 0.f;           // the highest roof or treetop in view
	std::map<const MapData*, float> partTops;
	float partTop(const MapData *d);
	void updateStops();
	std::vector<glm::vec3> handPts;
	// the debug panel's sky marker: where the sun, the moon and the table's light are
	Mesh skyMarkMesh;
	std::string skyMarkText; // what it showed last (tests)
	bool skyMarkerOn = false; // the debug panel's SUN MARK switch (owner: off by default)
	glm::vec3 shadowLight{0.f};  // where the shadow pass turned the standees toward (world)
	bool hasShadowLight = false;
	void drawSkyMarker(const Shader &sh);
	int shadeOffset = 0;            // the debug panel's darkness change (the table only, not the game)
	int tileShade(const Tile *t) const { return glm::clamp(t->getShade() + shadeOffset, 0, 15); }
	/// How much of a tile's own light is warm (fires, flares, lit items: 1) rather than the soldiers' (0),
	/// which takes the sky light's colour under the sun or moon.
	static float poolWarm(const Tile *t)
	{
		int u = t->getLight(LL_UNITS), o = std::max(t->getLight(LL_FIRE), t->getLight(LL_ITEMS));
		if (o <= 0 && u <= 0) return 1.f;
		return o > u ? 1.f : o == u ? 0.5f : 0.f;
	}
	/// Is there a ceiling over a tile (a floor on the level right above)? The sky can't light it then.
	/// Only the next level counts: something higher (a craft's wing, an overhang) leaves the ground under
	/// it open to a low sun, which the shadow map already handles.
	bool roofed(const Position &p) const
	{
		if (p.z + 1 >= battle->getMapSizeZ()) return false;
		Tile *t = battle->getTile(Position(p.x, p.y, p.z + 1));
		return t && t->getMapData(O_FLOOR);
	}
	/// Does the sun (or moon) reach the floor of a tile through the terrain (figures left out)? Four
	/// points of the floor are traced toward it through the game's own collision voxels (the tile's own
	/// object left out); at least two must get out. A low sun reaches under a roof from the side (a
	/// craft's open cabin, an overhang).
	bool sunReachesFloor(const Position &p) const
	{
		const SkyLight &s = weather.sky();
		if (!s.on || s.dir.y <= 0.02f) return false;
		TileEngine *te = battle->getTileEngine();
		if (!te) return false;
		// map voxels: x east, y up, z south -> the engine's x east, y south, z up
		glm::vec3 d(s.dir.x, s.dir.z, s.dir.y);
		d /= std::max(std::max(std::fabs(d.x), std::fabs(d.y)), std::fabs(d.z)); // about a voxel a step
		const float topZ = (float)(battle->getMapSizeZ() * 24);
		int out = 0;
		static const float off[4][2] = {{4.f, 4.f}, {12.f, 4.f}, {4.f, 12.f}, {12.f, 12.f}};
		for (auto &o : off)
		{
			glm::vec3 q(p.x * 16.f + o[0], p.y * 16.f + o[1], p.z * 24.f + 3.f);
			bool blocked = false;
			for (int i = 0; i < 2000; ++i)
			{
				q += d;
				if (q.z >= topZ) break;
				Position vp((int)q.x, (int)q.y, (int)q.z);
				VoxelType v = te->voxelCheck(vp, nullptr, true);
				if (v == V_OUTOFBOUNDS) break;
				// the tile's own furniture doesn't count: the trace starts just above the floor, which can be
				// inside an object standing there (a craft's fittings), and that object doesn't hide the sky
				if (v == V_OBJECT && vp.toTile() == p) continue;
				if (v != V_EMPTY) { blocked = true; break; }
			}
			if (!blocked) ++out;
		}
		te->voxelCheckFlush();
		return out >= 2;
	}
	/// Under a roof the sky can't reach: the soldiers' light stays a soft light of its own there. The
	/// traces are kept until the sun moves or the terrain changes anywhere (see the chunk loop).
	mutable std::unordered_map<int, bool> reachCache;
	mutable glm::vec3 reachSun{0.f};
	/// The landed craft's own map data sets (the Skyranger's PLANE), by name: the ground under the craft is
	/// open at the sides, so it is lit as outdoors (indoors()). Empty without a craft on the map.
	std::set<std::string> craftSets;
	void findCraftSets()
	{
		craftSets.clear();
		SavedGame *sg = game ? game->getSavedGame() : nullptr;
		if (!sg) return;
		for (Base *b : *sg->getBases())
			for (Craft *c : *b->getCrafts())
				if (c->isInBattlescape() && c->getRules()->getBattlescapeTerrainData())
					for (MapDataSet *ds : *c->getRules()->getBattlescapeTerrainData()->getMapDataSets())
						if (ds && ds->getName() != "BLANKS") craftSets.insert(ds->getName());
	}
	bool craftPart(const MapData *d) const { return d && d->getDataset() && craftSets.count(d->getDataset()->getName()); }
	/// The ground under a craft: the floor right above is the craft's, nothing on this tile is.
	bool underCraft(const Position &p) const
	{
		if (craftSets.empty() || p.z + 1 >= battle->getMapSizeZ()) return false;
		Tile *above = battle->getTile(Position(p.x, p.y, p.z + 1)), *t = battle->getTile(p);
		if (!above || !t || !craftPart(above->getMapData(O_FLOOR))) return false;
		for (int part = 0; part < O_MAX; ++part) if (craftPart(t->getMapData((TilePart)part))) return false;
		return true;
	}
	bool indoors(const Position &p) const
	{
		if (!roofed(p)) return false;
		// the ground under a landed craft is open at the sides: lit as outdoors, in the craft's own shadow
		// (owner's choice of the 0.17.0-shade test builds; inside buildings and the cabin stay indoors)
		if (underCraft(p)) return false;
		glm::vec3 sd = weather.sky().dir;
		if (sd != reachSun) { reachCache.clear(); reachSun = sd; }
		int key = (p.z * battle->getMapSizeY() + p.y) * battle->getMapSizeX() + p.x;
		auto it = reachCache.find(key);
		if (it != reachCache.end()) return it->second;
		bool in = !sunReachesFloor(p);
		reachCache[key] = in;
		return in;
	}
	uint64_t sunKey() const
	{
		glm::vec3 d = weather.sky().dir;
		return (uint64_t)(int)(d.x * 1000.f) * 1000003ull + (uint64_t)(int)(d.y * 1000.f) * 1009ull + (uint64_t)(int)(d.z * 1000.f);
	}
	int globalShade() const { return glm::clamp(battle->getGlobalShade() + shadeOffset, 0, 15); }
	RoomLayout layout;
	glm::vec3 viewer{0.f, 1.65f, 0.f};
	glm::vec3 listenerFwd{0.f, 0.f, -1.f}; // where the head faces (world)
	Game *game = nullptr;
	bool hasBattle = false, hasGlobe = false;
	double time = 0.0;

	// ---- battlescape
	SavedBattleGame *battle = nullptr;
	BattlescapeState *bs = nullptr; // only valid while it is on the game's state stack
	const Tile *firstTile = nullptr;
	int mx = 0, my = 0, mz = 0, cxN = 0, cyN = 0;
	/// A tile's parts (null where it has none, or a part not drawn: an open UFO door, an unseen wall).
	struct TileKey
	{
		const MapData *m[O_MAX] = {nullptr, nullptr, nullptr, nullptr};
		int join = 0; // floor relief carrying on into the neighbours (VR::reliefField)
		int edges = 0; // sides (1 west, 2 east, 4 north, 8 south) where the floor's thickness shows (no floor beside)
		int variant = 0; // which version of a hand-built object (VrModels: modelVariants), by the tile's position
		bool operator<(const TileKey &o) const
		{
			if (join != o.join) return join < o.join;
			if (variant != o.variant) return variant < o.variant;
			if (edges != o.edges) return edges < o.edges;
			return std::lexicographical_compare(m, m + O_MAX, o.m, o.m + O_MAX);
		}
	};
	std::map<TileKey, Proto> tileProtos;   // one shape per combination of parts (VrBoard: buildTile)
	const Proto &tileProto(const TileKey &k);
	void buildTile(const TileKey &k, Proto &p);
	void addCard(const MapData *md, Proto &p);
	void addRelief(const MapData *floor, int join, Proto &p);
	void addFloor(const Surface *spr, int yoff, uint32_t avg, bool relief, int edges, const uint8_t own[TILE_W][TILE_W][SHAPE_H], Proto &p);
	void addModel(const VR::Model &m, const Surface *spr, int yoff, const Surface *floorSpr, Proto &p, const MapData *md = nullptr);
	/// The part's hand-built model (VrModels.h), or null; `variant` picks one of its versions (wraps round).
	const VR::Model *modelOf(const MapData *md, int variant = 0);
	/// How many versions of a hand-built model the part has (0: none).
	int modelVariantsOf(const MapData *md);
	bool growToArt(const MapData *md, const Surface *spr, int yoff, uint8_t own[TILE_W][TILE_W][SHAPE_H], int part, int &rowShift);
	TileKey tileKey(Tile *t, int x, int y, int z);
	/// The terrain lab's inspect panel for a tile: its parts as the game draws them, each on its own, and
	/// what the table did with them.
	bool inspectPicture(const Position &pos, std::vector<uint32_t> &px, int &W, int &H);
	/// The lab's comparison of a shape with its art (VrCompare.h), each picture CMP_W x CMP_H: the art as the
	/// game draws it, the table's shape from the game's angle, where they differ, the shape from behind.
	struct Comparison
	{
		std::vector<uint32_t> art, table, diff, look, above;
		VR::Compare::Score score;
		uint8_t grew = 0, stray = 0, model = 0;
		bool card = false, empty = true;
	};
	/// scratchBuild: build the shape afresh in the scratch atlas (parts that aren't on the map); else the
	/// table's own cached shape.
	void compareKey(const TileKey &k, bool scratchBuild, Comparison &c);
	bool comparePicture(const Position &pos, std::vector<uint32_t> &px, int &W, int &H);
	bool partPicture(const MapDataSet *ds, int id, std::vector<uint32_t> &px, int &W, int &H);
	bool comparisonPicture(const Comparison &c, const std::string &title, std::vector<uint32_t> &px, int &W, int &H);
	bool partSheet(const MapDataSet *ds, std::vector<uint32_t> &px, int &W, int &H, std::string &report);
	uint64_t inspectKey = ~0ull;
	/// Per part: its sprite's average colour (minimap, fallbacks) and how high it reaches (rain).
	struct PartInfo { uint32_t avg = 0; float top = 0.f; };
	std::map<const MapData*, PartInfo> partInfos;
	const PartInfo &partInfo(const MapData *md);
	std::vector<Chunk> chunks;
	std::vector<Chunk> fogChunks;  // fog of war volume, per chunk column (all levels)
	int fogTop(int x, int y) const; // fogged levels in a column (0 = explored)
	uint64_t fogHash(int cx, int cy);
	void buildFog(int cx, int cy, Chunk &ch);
	Atlas atlas;
	Atlas scratch;          // the lab's comparisons build shapes here, so the table's atlas stays as it is
	Atlas *build = &atlas;   // where buildTile, addCard and addRelief put their pictures
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
	// a shot in progress: the table follows the game's camera as it chases the projectile, then goes
	// back to where it was when the camera returns
	bool inShot = false;
	glm::vec3 preShotFocus{0.f};
	Position preShotCam;
	float returnIn = -1.f;       // after a shot in our turn: seconds left before going back (-1 = none)
	float tapSelect = 0.f;       // seconds left in which a new selection came from a tap on the table
	// where a focused soldier stands on the table: this far from the middle toward the player,
	// as a fraction of half the map's depth (leaves most of the table in front of the soldier)
	static constexpr float NEAR_SPOT = 0.45f;
	void focusOnUnit(const BattleUnit *u, Camera *cam);
	bool hidden = false;         // the game is showing "Hidden Movement"
	float curtain = 0.f;         // (unused since 0.9.1: the alien turn's darkness below covers the table)
	// The alien turn (0.19.0-hidden): the table shows what the flat screen shows, through a window in the
	// darkness, turned and zoomed like the flat view, jumping where the flat camera goes; while the game shows
	// Hidden Movement it is all dark. The rest of the map and the table stay dark: nothing on the table gives
	// anything away. Turning and zooming are locked; the player's own view comes back for their turn.
	static constexpr float FLAT_YAW = -0.78539816f; // the flat screen's top (the map's north-west) away from the player
	bool alienView = false;
	bool savedView = false;
	float savedYaw = 0.f, savedTile = 0.055f;
	float alienZoom = 1.f;       // the player's zoom during the alien turn, against the flat view's (kept for the battle's later alien turns)
	glm::vec3 savedFocus{0.f};
	glm::vec4 win{0.f, 0.f, 1e6f, 1e6f}; // the flat screen's map area: centre and half sizes along its diagonals (tiles, u = x - y, v = x + y)
	float reveal = 1.f;          // 0 all dark .. 1 the window shown (dissolving in and out)
	float fallT = -1.f;          // seconds since the darkness began to fall, -1 when not falling
	bool winOn = false;          // the darkness and the window are in use
	static constexpr float REVEAL_IN = 0.06f, FALL_T = 0.35f;
	void followFlatScreen(Camera *cam);
	void applyWin(const Shader &sh, bool on) const;
	void drawCover(const Shader &sh);
	Mesh coverMesh;
	glm::vec4 coverRect{0.f};
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
	TerrainLabState *lab = nullptr; // the terrain lab's screen, while it shows its map
	unsigned labGen = 0, styleGen = 0;
	int labLevel = 0;                // the lab's level shown (no game camera there)
	bool pinned = false;             // the lab's inspect panel stays on pinTile (trigger, or a fingertip's touch)
	Position pinTile;
	float fingerAge = 0.f;           // the lab: a fingertip is over labFingerTile (seconds left)
	Position labFingerTile;
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
	bool feedPass = false;             // drawing for the scanner's camera: no markers
	bool capturePass = false;          // drawing a terrain lab shot: no markers, labels or panels
	Position captureTile = Position(-1, -1, -1); // the marked tile in that shot (corner ticks)
	Mesh ticks;                        // corner ticks around a tile, on the ground
	BattleUnit *hiddenUnit = nullptr;  // ...and in its soldier view, not the soldier's own figure

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
	glm::vec3 testFlashAt{0.f};        // tests: a burst's light, as an explosion's (board voxels)
	float testFlashT = 0.f;
	// light that glowing shots and explosions throw onto the map (board voxels, radius in voxels).
	// Table only: it brightens terrain and figures from their unlit colour, so it shows at night;
	// the game's own lighting (and the flat screen) is untouched.
	struct Glow { glm::vec3 vox; glm::vec3 col; float radius; };
	std::vector<Glow> glows;
	std::vector<uint8_t> smokeSnap, fireSnap; // tile smoke / fire as last shown (frozen while the movement is hidden)
	bool projGlow = false;             // the current shot gives off light (laser, plasma, rocket, incendiary)
	float projGlowR = 32.f;
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
	Label inspectPanel;             // the terrain lab's inspect panel (drawLab)
	std::map<std::string, Label> labPictures; // the lab's picture cards and flat views (drawLab), per map
	/// Tiles x0..x1, y0..y1, levels z0..z1 of the battle map as the flat game draws them (RGBA, on a dark
	/// backing): floors, walls and objects, back to front, level by level.
	void flatPicture(int x0, int y0, int z0, int x1, int y1, int z1, std::vector<uint32_t> &px, int &W, int &H);
	/// An upright picture turned about the vertical to face the viewer, its bottom middle at base (voxels).
	void drawCard(const Shader &sh, const glm::mat4 &M, const glm::vec3 &base, const glm::vec3 &viewerVox, const Label &l, float pxSize, float lean = 0.f) const;
	const Label &label(const std::string &text, Uint8 color);
	glm::vec4 palColor(int index, float glow) const
	{
		const SDL_Color &c = pal[index & 255];
		return glm::vec4(c.r / 255.f * glow, c.g / 255.f * glow, c.b / 255.f * glow, 1.f);
	}
	void drawWire(const Shader &sh, const glm::mat4 &M, const glm::vec3 &at, int size, const glm::vec4 &col, float xray) const;
	void drawLabel(const Shader &sh, const glm::mat4 &M, const glm::vec3 &at, const glm::vec3 &viewerVox, const Label &l, float pxSize) const;
	void drawLab(const Shader &sh, const glm::mat4 &M);
	void drawTicks(const Shader &sh, const glm::mat4 &M, const Position &t, const glm::vec4 &col) const;
	void drawHud(const Shader &sh, const glm::mat4 &M);

	// grabbing
	bool held[2] = {false, false};
	glm::vec3 lastHand[2];

	// ---- geoscape
	Mesh globeLand, globeOcean;
	bool globeBuilt = false;
	float globeRadius = 0.30f;
	glm::vec3 globeCenter{0.f};
	// The globe is a dome rising out of the table: its top stays this high above the surface (below
	// the line of sight to the back-wall screen) and zooming sinks it deeper instead of raising it.
	// It is tipped toward the player so the area in focus faces up and toward you.
	static constexpr float DOME_TOP = 0.22f;
	static constexpr float DOME_TILT = -0.61f; // -35 degrees about x
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
			* glm::translate(glm::mat4(1.f), {-focus.x, FLOOR_LIFT, -focus.z});
	}
	/// The map stands 2 voxels up (0.21.0-terrain.9): floors are surfaces at height 0 with their 2-voxel
	/// thickness hanging below, so the ground floor's top and edges sit where the old slab's did, and walls,
	/// objects, units and the game's own positions (shots, picks) all stand on it, being lifted alike.
	static constexpr float FLOOR_LIFT = 2.f;
	glm::vec4 clipRect() const { return mapRect; }
	glm::mat4 globeMatrix() const
	{
		// rotate so the game's globe center faces the player (+Z)
		return glm::translate(glm::mat4(1.f), globeCenter)
			* glm::rotate(glm::mat4(1.f), DOME_TILT, {1, 0, 0})
			* glm::rotate(glm::mat4(1.f), (float)-cenLat, {1, 0, 0})
			* glm::rotate(glm::mat4(1.f), (float)-cenLon, {0, 1, 0})
			* glm::scale(glm::mat4(1.f), glm::vec3(globeRadius));
	}

	void resetBattle();
	void bindBattle(SavedBattleGame *save);
	uint64_t chunkHash(int cx, int cy, int z, uint64_t *terrain = nullptr);
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
	weather.end();
	partTops.clear();
	stopCache.clear();
	openCache.clear();
	tileProtos.clear();
	partInfos.clear();
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
	labPictures.clear();
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
	weather.begin(game, save, shadeOffset);
	findCraftSets();
	alienZoom = 1.f;
	stopCache.assign((size_t)mx * my, 0.f);
	openCache.assign((size_t)mx * my, 0);
	stopTimer = 0.f;
	stopLevel = -1;
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


const Board::Impl::PartInfo &Board::Impl::partInfo(const MapData *md)
{
	auto it = partInfos.find(md);
	if (it != partInfos.end()) return it->second;
	PartInfo &pi = partInfos[md];
	const Surface *spr = nullptr;
	if (md->getDataset() && md->getDataset()->getSurfaceset())
		spr = static_cast<const SurfaceSet*>(md->getDataset()->getSurfaceset())->getFrame(md->getSprite(0));
	if (!spr) return pi;
	uint64_t sr = 0, sg = 0, sb = 0, n = 0;
	for (int y = 0; y < spr->getHeight(); ++y)
		for (int x = 0; x < spr->getWidth(); ++x)
			if (Uint8 c = spr->getPixel(x, y)) { sr += pal[c].r; sg += pal[c].g; sb += pal[c].b; ++n; }
	if (n) pi.avg = (uint32_t)(sr / n) | ((uint32_t)(sg / n) << 8) | ((uint32_t)(sb / n) << 16) | 0xFF000000u;
	// the highest voxel layer with anything in it; parts without voxels stand as cards up to the sprite's top
	int top = -1;
	for (int z = 0; z < TILE_H; ++z)
	{
		int loft = md->getLoftID(z / 2);
		if (!loft || !voxels) continue;
		for (int y = 0; y < TILE_W; ++y)
		{
			size_t idx = (size_t)loft * 16 + y;
			if (idx < voxels->size() && (*voxels)[idx]) { top = z; break; }
		}
	}
	pi.top = md->getObjectType() == O_FLOOR ? 0.f : top >= 0 ? (float)(top + 1) : (n ? 32.f + md->getYOffset() : 0.f); // a floor's top is the ground (0.21.0-terrain.9)
	return pi;
}

const Proto &Board::Impl::tileProto(const TileKey &k)
{
	auto it = tileProtos.find(k);
	if (it != tileProtos.end()) return it->second;
	Proto &p = tileProtos[k];
	buildTile(k, p);
	return p;
}

/// A decorative part without collision voxels: two crossed cards showing its sprite.
void Board::Impl::addCard(const MapData *md, Proto &p)
{
	const Surface *spr = nullptr;
	if (md->getDataset() && md->getDataset()->getSurfaceset())
		spr = static_cast<const SurfaceSet*>(md->getDataset()->getSurfaceset())->getFrame(md->getSprite(0));
	if (!spr) return;
	int w = spr->getWidth(), h = spr->getHeight(), ax, ay;
	bool any = false;
	for (int y = 0; y < h && !any; ++y) for (int x = 0; x < w; ++x) if (spr->getPixel(x, y)) { any = true; break; }
	if (!any || !build->alloc(w + 2, h + 2, ax, ay)) return;
	for (int j = -1; j <= h; ++j)
		for (int i = -1; i <= w; ++i)
		{
			int sx = glm::clamp(i, 0, w - 1), sy = glm::clamp(j, 0, h - 1);
			Uint8 c = spr->getPixel(sx, sy);
			build->put(ax + 1 + i, ay + 1 + j, c ? rgba(pal[c]) : 0u);
		}
	float u0 = (ax + 1) / (float)ATLAS, v0 = (ay + 1) / (float)ATLAS;
	float u1 = (ax + 1 + w) / (float)ATLAS, v1 = (ay + 1 + h) / (float)ATLAS;
	float top = 32.f + md->getYOffset(), bottom = top - h; // sprite row 32 sits on the floor at the tile centre
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
}

/// The relief of a listed floor part, as whole-voxel steps or a smooth surface (VR::reliefMode), over the
/// floor's top (2 voxels up); the painting seen from above is its texture.
void Board::Impl::addRelief(const MapData *floor, int join, Proto &p)
{
	const VR::ReliefSpec *spec = VR::reliefSpec(floor);
	if (!spec) return;
	const int N = VR::RELIEF_N;
	static VR::ReliefField F;
	VR::reliefField(floor, *spec, pal, join, F);
	int ax, ay;
	if (!build->alloc(N + 2, N + 2, ax, ay)) return;
	for (int j = -1; j <= N; ++j)
		for (int i = -1; i <= N; ++i)
			build->put(ax + 1 + i, ay + 1 + j, F.tex[glm::clamp(j, 0, N - 1)][glm::clamp(i, 0, N - 1)]);
	auto uv = [&](float i, float j) { return glm::vec2((ax + 1 + i) / (float)ATLAS, (ay + 1 + j) / (float)ATLAS); };
	const float BASE = 0.f, CELL = (float)TILE_W / N; // the floor's top (0.21.0-terrain.9: at height 0)
	const glm::vec4 white(1.f);
	auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n, glm::vec2 ta, glm::vec2 tb, glm::vec2 tc, glm::vec2 td)
	{
		uint32_t i0 = (uint32_t)p.verts.size();
		p.verts.push_back({a, n, ta, white, MAT_BOARD});
		p.verts.push_back({b, n, tb, white, MAT_BOARD});
		p.verts.push_back({c, n, tc, white, MAT_BOARD});
		p.verts.push_back({d, n, td, white, MAT_BOARD});
		p.idx.insert(p.idx.end(), {i0, i0 + 1, i0 + 2, i0, i0 + 2, i0 + 3});
	};
	if (VR::reliefMode() == VR::RELIEF_STEPS)
	{
		// whole voxels, like the rest of the table: each voxel column takes its 2 x 2 cells' mean height
		const int C = N / 2;
		int H[C][C];
		for (int y = 0; y < C; ++y)
			for (int x = 0; x < C; ++x)
				H[y][x] = (int)std::lround((F.h[2 * y][2 * x] + F.h[2 * y][2 * x + 1] + F.h[2 * y + 1][2 * x] + F.h[2 * y + 1][2 * x + 1]) * 0.25f);
		auto Hat = [&](int x, int y) -> int { return (x < 0 || y < 0 || x >= C || y >= C) ? 0 : H[y][x]; };
		for (int y = 0; y < C; ++y)
			for (int x = 0; x < C; ++x)
			{
				float top = BASE + H[y][x];
				float x0 = (float)x, x1 = x + 1.f, z0 = (float)y, z1 = y + 1.f;
				quad({x0, top, z1}, {x1, top, z1}, {x1, top, z0}, {x0, top, z0}, {0, 1, 0},
					uv(2.f * x, 2.f * y + 2), uv(2.f * x + 2, 2.f * y + 2), uv(2.f * x + 2, 2.f * y), uv(2.f * x, 2.f * y));
				// the steps down to each lower neighbour (outside the tile: the floor's level), in this column's colour
				const int nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
				for (int q = 0; q < 4; ++q)
				{
					int nh = Hat(nb[q][0], nb[q][1]);
					bool outside = nb[q][0] < 0 || nb[q][1] < 0 || nb[q][0] >= C || nb[q][1] >= C;
					float lo = BASE + std::min(nh, H[y][x]), hi = BASE + std::max(nh, H[y][x]);
					if (nh >= H[y][x] && !(outside && H[y][x] < 0)) continue;
					if (outside && H[y][x] < 0) { lo = BASE + H[y][x]; hi = BASE; } // a hole cut at the tile's edge
					glm::vec3 n = q == 0 ? glm::vec3(1, 0, 0) : q == 1 ? glm::vec3(-1, 0, 0) : q == 2 ? glm::vec3(0, 0, 1) : glm::vec3(0, 0, -1);
					if (outside && H[y][x] < 0) n = -n; // facing into the hole
					// the edge's two cells of this column
					float ex = q == 0 ? x1 : q == 1 ? x0 : -1.f, ez = q == 2 ? z1 : q == 3 ? z0 : -1.f;
					glm::vec2 t0, t1;
					if (q < 2) { float ti = 2.f * x + (q == 0 ? 1.5f : 0.5f); t0 = uv(ti, 2.f * y + 0.5f); t1 = uv(ti, 2.f * y + 1.5f); }
					else { float tj = 2.f * y + (q == 2 ? 1.5f : 0.5f); t0 = uv(2.f * x + 0.5f, tj); t1 = uv(2.f * x + 1.5f, tj); }
					if (q < 2) quad({ex, lo, z0}, {ex, lo, z1}, {ex, hi, z1}, {ex, hi, z0}, n, t0, t1, t1, t0);
					else quad({x0, lo, ez}, {x1, lo, ez}, {x1, hi, ez}, {x0, hi, ez}, n, t0, t1, t1, t0);
				}
			}
		return;
	}
	// smooth: a height field over the cells' corners, lit by its own slopes
	float Hc[N + 1][N + 1];
	for (int j = 0; j <= N; ++j)
		for (int i = 0; i <= N; ++i)
		{
			float sum = 0.f;
			int n = 0;
			for (int dj = -1; dj <= 0; ++dj)
				for (int di = -1; di <= 0; ++di)
				{
					int ci = i + di, cj = j + dj;
					if (ci < 0 || cj < 0 || ci >= N || cj >= N) continue;
					sum += F.h[cj][ci]; ++n;
				}
			bool edge = i == 0 || j == 0 || i == N || j == N;
			Hc[j][i] = edge ? 0.f : (n ? sum / n : 0.f);
		}
	uint32_t i0 = (uint32_t)p.verts.size();
	for (int j = 0; j <= N; ++j)
		for (int i = 0; i <= N; ++i)
		{
			float hx = (Hc[j][std::min(i + 1, N)] - Hc[j][std::max(i - 1, 0)]) / ((std::min(i + 1, N) - std::max(i - 1, 0)) * CELL);
			float hz = (Hc[std::min(j + 1, N)][i] - Hc[std::max(j - 1, 0)][i]) / ((std::min(j + 1, N) - std::max(j - 1, 0)) * CELL);
			glm::vec3 n = glm::normalize(glm::vec3(-hx, 1.f, -hz));
			p.verts.push_back({{i * CELL, BASE + Hc[j][i], j * CELL}, n, uv((float)i, (float)j), white, MAT_BOARD});
		}
	for (int j = 0; j < N; ++j)
		for (int i = 0; i < N; ++i)
		{
			uint32_t a = i0 + j * (N + 1) + i, b = a + 1, c = a + (N + 1) + 1, d = a + (N + 1);
			p.idx.insert(p.idx.end(), {a, d, c, a, c, b});
		}
}

/// Objects whose collision is much less than their art (a tree's trunk under a crown drawn around it, a
/// bush, a crop field, small props): the display shape is worked out from the art (0.21.0-terrain.3).
/// The object is taken as a stack of horizontal slices round about its axis, each a disc clipped to the
/// tile (a large one is a rounded square, the largest the whole square: a crop field). A slice at height z
/// is centred on the sprite row groundRow - z; each gets the largest radius whose outline stays inside the
/// art in every row it covers. Then:
///  - slices that add nothing to what the slices under them already show are dropped from the top: a flat
///    top face seen from above narrows toward its far corner, which read as a cone before;
///  - when the art includes a patch of ground under the object (trees), the patch's colour inside the
///    tile's diamond doesn't count as the object, so what stands on it is the trunk (no mound of grass);
///  - the result replaces the collision voxels (a tree's collision is often a full column).
/// If the slices don't explain the art well (under 60% overlap), the old rule adds voxels round about the axis instead.
bool Board::Impl::growToArt(const MapData *md, const Surface *spr, int yoff, uint8_t own[TILE_W][TILE_W][SHAPE_H], int part, int &rowShift)
{
	rowShift = 0;
	const int W = spr->getWidth(), H = spr->getHeight();
	auto loftSolid = [&](int x, int y, int z) -> bool
	{
		int loft = md->getLoftID(z / 2);
		if (!loft || !voxels) return false;
		size_t idx = (size_t)loft * 16 + y;
		return idx < voxels->size() && ((*voxels)[idx] & (1 << (15 - x)));
	};
	auto proj = [&](float x, float y, float z, int &px, int &py)
	{
		px = (int)std::floor(16.f + (x - y));
		py = (int)std::floor(24.f + (x + y) * 0.5f - z + yoff);
	};
	bool lowOnly = true; // the collision is only the bottom 2 layers
	for (int z = 2; z < TILE_H && lowOnly; ++z)
		for (int y = 0; y < TILE_W && lowOnly; ++y)
			for (int x = 0; x < TILE_W; ++x) if (loftSolid(x, y, z)) { lowOnly = false; break; }
	// how much of the art the collision voxels already cover; the axis is the middle of their footprint
	std::vector<uint8_t> covered((size_t)W * H, 0);
	float ax = 0.f, ay = 0.f;
	int nfoot = 0;
	for (int z = 0; z < TILE_H; ++z)
		for (int y = 0; y < TILE_W; ++y)
			for (int x = 0; x < TILE_W; ++x)
			{
				// (for a part that is only its bottom 2 layers, a copy of the floor's slab in its collision,
				// those don't count as covering the art: low grass or a bush is built from its art)
				// (what the part keeps: its slab copy is already gone from own[], see buildTile)
				if (own[x][y][z] != part + 1 || (z < 2 && lowOnly)) continue;
				if (z >= 2) { ax += x + 0.5f; ay += y + 0.5f; ++nfoot; }
				int px, py;
				proj(x + 0.5f, y + 0.5f, z + 1.f, px, py);
				for (int qy = py - 1; qy <= py + 2; ++qy)
					for (int qx = px - 1; qx <= px; ++qx)
						if (qx >= 0 && qy >= 0 && qx < W && qy < H) covered[(size_t)qy * W + qx] = 1;
			}
	int opaque = 0, cov = 0;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			if (spr->getPixel(x, y)) { ++opaque; cov += covered[(size_t)y * W + x]; }
	if (opaque < 12 || cov * 10 >= opaque * 7) return false; // the collision already matches (70%+ of the art)
	if (nfoot) { ax /= nfoot; ay /= nfoot; } else { ax = ay = 8.f; }
	const float axisPx = 16.f + (ax - ay), groundRow = 24.f + (ax + ay) * 0.5f + yoff;
	// the art, less a ground patch drawn under the object: the outline just above the middle row narrows
	// like the tile's diamond (a tree on a patch of grass; a crop field's sides go straight up instead).
	// Then pixels inside that diamond in the patch's colour family (green, brown or other) aren't the
	// object; what is left there is its trunk or stem.
	auto colourClass = [&](Uint8 c) -> int
	{
		const SDL_Color &k = pal[c];
		return k.g > k.r * 1.15f ? 0 : (k.r > k.g ? 1 : 2);
	};
	auto extent = [&](int y, int &l, int &r)
	{
		l = W; r = -1;
		if (y < 0 || y >= H) return;
		for (int x = 0; x < W; ++x) if (spr->getPixel(x, y)) { l = std::min(l, x); r = std::max(r, x); }
	};
	auto diamondHalf = [&](float row) { return 2.f * (8.f - std::fabs(row + 0.5f - groundRow)); };
	bool patch = true;
	for (int d = 1; d <= 4 && patch; ++d)
	{
		int y = (int)std::floor(groundRow - d), l, r;
		extent(y, l, r);
		if (r < 0 || (r + 1 - l) > 2.f * diamondHalf((float)y) + 2.f) patch = false;
	}
	for (int d = 2; d <= 5 && patch; ++d)
	{
		int y = (int)std::floor(groundRow + d), l, r;
		extent(y, l, r);
		if (r < 0 || (r + 1 - l) < 2.f * diamondHalf((float)y) * 0.8f) patch = false;
	}
	std::vector<uint8_t> art((size_t)W * H, 0);
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			art[(size_t)y * W + x] = spr->getPixel(x, y) ? 1 : 0;
	if (patch)
	{
		int votes[3] = {0, 0, 0};
		for (int d = 3; d <= 6; ++d)
		{
			int y = (int)std::floor(groundRow + d);
			if (y < 0 || y >= H) continue;
			for (int x = 0; x < W; ++x) if (Uint8 c = spr->getPixel(x, y)) ++votes[colourClass(c)];
		}
		int ground = (int)(std::max_element(votes, votes + 3) - votes);
		for (int y = 0; y < H; ++y)
		{
			float half = diamondHalf((float)y);
			if (half <= 0.f) continue;
			for (int x = 0; x < W; ++x)
			{
				Uint8 c = spr->getPixel(x, y);
				if (c && std::fabs(x + 0.5f - 16.f) <= half && colourClass(c) == ground) art[(size_t)y * W + x] = 0;
			}
		}
	}
	// trunk and branch colours: what is left standing on a ground patch near the axis. Trees are drawn with
	// the trunk reaching below the tile's middle row; that lowest trunk row is taken as the ground for the
	// heights (the crown sits that much higher, as drawn). rowShift moves the art's rows the same way when
	// the faces are coloured.
	std::set<Uint8> trunkCols;
	int trunkBottom = -1;
	if (patch)
		for (int y = 0; y < H; ++y)
		{
			float half = diamondHalf((float)y);
			if (half <= 0.f || y + 0.5f > groundRow + 8.f) continue;
			for (int x = 0; x < W; ++x)
				if (art[(size_t)y * W + x] && std::fabs(x + 0.5f - axisPx) <= 3.f)
				{
					trunkCols.insert(spr->getPixel(x, y));
					trunkBottom = std::max(trunkBottom, y);
				}
		}
	rowShift = trunkBottom >= 0 ? std::max(0, std::min(6, (int)std::floor(trunkBottom + 1.f - groundRow))) : 0;
	const float gv = groundRow + rowShift; // the row of height 0 for the shape
	int artCount = 0;
	for (uint8_t v : art) artCount += v;
	// the art's reach either side of the axis in each row (left and right averaged, as a round thing has)
	std::vector<float> reach(H, -1.f);
	for (int y = 0; y < H; ++y)
	{
		int l = W, r = -1;
		for (int x = 0; x < W; ++x) if (art[(size_t)y * W + x]) { l = std::min(l, x); r = std::max(r, x); }
		if (r < 0) continue;
		float left = axisPx - l, right = r + 1.f - axisPx;
		if (left <= -1.5f || right <= -1.5f) continue; // the row is off to one side of the axis
		reach[y] = std::max(0.5f, (left + right) * 0.5f); // a thin trunk a pixel off the axis still counts
	}
	// a slice: a disc of radius rad round the axis, clipped to the tile (so a large one is a rounded
	// square, and from 11.3 the whole square). Seen from the flat game's view, at row offset v from the
	// slice's middle row its points have x + y = 2v (relative to the axis) and the horizontal offset
	// u = x - y; the disc allows |u| <= sqrt(2 (rad^2 - 2 v^2)), the tile x0 <= x <= x1, y0 <= y <= y1.
	const float x0 = -ax, x1 = 16.f - ax, y0 = -ay, y1 = 16.f - ay;
	auto span = [&](float rad, float v, float &lo, float &hi) -> bool
	{
		float q = rad * rad - 2.f * v * v;
		if (q < 0.f) return false;
		float c = std::sqrt(2.f * q);
		lo = std::max({-c, 2.f * (x0 - v), 2.f * (v - y1)});
		hi = std::min({c, 2.f * (x1 - v), 2.f * (v - y0)});
		return lo <= hi;
	};
	auto fits = [&](float rad, float row) -> bool
	{
		bool any = false;
		for (int y = (int)std::floor(row - 12.f); y <= (int)std::floor(row + 12.f); ++y)
		{
			float lo, hi;
			if (!span(rad, y + 0.5f - row, lo, hi)) continue;
			if (y < 0 || y >= H || reach[y] < 0.f) return false;
			if (hi > reach[y] + 0.75f || -lo > reach[y] + 0.75f) return false;
			any = true;
		}
		return any;
	};
	// slices from the ground (z 0, the floor's top since 0.21.0-terrain.9) up, as high as the art goes (a tree's crown rises above its level)
	struct Fit { std::vector<float> rad; int top = -1; float iou = 0.f; std::vector<uint8_t> mask; };
	auto fitWith = [&](float cap) -> Fit
	{
		Fit f;
		f.rad.assign(SHAPE_H, 0.f);
		for (int z = 0; z < SHAPE_H; ++z)
		{
			float row = gv - (z + 0.5f);
			if (row < 0.f) break;
			for (float rad = cap; rad >= 0.5f; rad -= 0.25f)
				if (fits(rad, row)) { f.rad[z] = rad; break; }
		}
		// a gap between slices (a trunk hidden for a row or two) takes the narrower of its neighbours
		for (int z = 1; z < SHAPE_H - 1; ++z)
		{
			if (f.rad[z] > 0.f || f.rad[z - 1] <= 0.f) continue;
			int z2 = z;
			while (z2 < SHAPE_H && f.rad[z2] <= 0.f) ++z2;
			if (z2 >= SHAPE_H || z2 - z > 3) continue;
			float m = std::min(f.rad[z - 1], f.rad[z2]);
			for (int q = z; q < z2; ++q) f.rad[q] = m;
		}
		// what each slice shows, bottom up; drop slices from the top while the art they explain is (almost)
		// all explained by the slices under them (a flat top seen from above is not a cone)
		f.mask.assign((size_t)W * H, 0);
		std::vector<int> gained(SHAPE_H, 0);
		int total = 0, uni = 0;
		for (int z = 0; z < SHAPE_H; ++z)
		{
			if (f.rad[z] <= 0.f) continue;
			float row = gv - (z + 0.5f);
			for (int y = (int)std::floor(row - 12.f); y <= (int)std::floor(row + 12.f); ++y)
			{
				float lo, hi;
				if (y < 0 || y >= H || !span(f.rad[z], y + 0.5f - row, lo, hi)) continue;
				for (int x = (int)std::floor(axisPx + lo); x < (int)std::ceil(axisPx + hi); ++x)
				{
					if (x < 0 || x >= W) continue;
					uint8_t &m = f.mask[(size_t)y * W + x];
					if (m) continue;
					m = 1; ++uni;
					if (art[(size_t)y * W + x]) { ++gained[z]; ++total; }
				}
			}
		}
		if (!uni || !artCount) return f;
		f.iou = (float)total / (float)(uni + artCount - total);
		int keep = total, top = SHAPE_H - 1;
		while (top >= 0 && (f.rad[top] <= 0.f || keep - gained[top] >= total - std::max(3, total / 150)))
		{
			keep -= gained[top];
			f.rad[top] = 0.f;
			--top;
		}
		f.top = top;
		return f;
	};
	// round slices (a crown: at most a little wider than the tile's inscribed circle), unless square ones
	// (a crop field's block) explain the art clearly better
	Fit round = fitWith(8.75f), square = fitWith(11.4f);
	const bool boxy = square.iou > round.iou + 0.1f;
	Fit best = boxy ? square : round;
	// a crown's top is a dome: the flat view can't tell a flat top from a round one (the top face of a
	// flat-topped crown covers the same rows), and the art's crowns are drawn round. From where the crown
	// reaches most of its width, half an ellipsoid as wide as the crown, high enough that its outline
	// reaches the art's top near the axis.
	if (!boxy && best.top >= 0)
	{
		float rm = 0.f;
		for (int z = 0; z <= best.top; ++z) rm = std::max(rm, best.rad[z]);
		int zb = -1;
		for (int z = 0; z <= best.top && zb < 0; ++z) if (best.rad[z] >= 0.6f * rm) zb = z;
		int artTop = -1;
		for (int y = 0; y < H && artTop < 0; ++y)
			for (int x = (int)std::floor(axisPx - 2.f); x <= (int)std::floor(axisPx + 2.f); ++x)
				if (x >= 0 && x < W && art[(size_t)y * W + x]) { artTop = y; break; }
		float D = gv - zb - artTop;
		if (rm >= 3.f && zb >= 0 && artTop >= 0 && D > rm)
		{
			float c = (D * D - rm * rm * 0.5f) / (2.f * D), zc = zb + c;
			if (c > 2.f)
			{
				int top = std::min(SHAPE_H - 1, (int)std::floor(zc + c - 0.5f));
				for (int z = (int)std::ceil(zc); z < SHAPE_H; ++z)
				{
					float t = (z + 0.5f - zc) / c;
					best.rad[z] = (z <= top && t < 1.f) ? std::max(0.5f, rm * std::sqrt(1.f - t * t)) : 0.f;
				}
				best.top = top;
			}
		}
	}
	// a block whose top band in the art is ragged (a crop field's ears): stalks stand on a lower top
	bool ragged = false;
	if (boxy && best.top >= 0)
	{
		int band = 0, filled = 0;
		for (int x = 0; x < W; ++x)
		{
			int mtop = -1;
			for (int y = 0; y < H && mtop < 0; ++y) if (best.mask[(size_t)y * W + x]) mtop = y;
			if (mtop < 0) continue;
			for (int y = mtop; y < std::min(H, mtop + 7); ++y)
				if (best.mask[(size_t)y * W + x]) { ++band; filled += art[(size_t)y * W + x]; }
		}
		ragged = band > 0 && filled < band * 0.92f;
	}
	// a height from the hand-checked list (terrain-relief.txt, "tall"): the slices squeezed or stretched
	if (float want = VR::shapeHeight(md))
	{
		int newTop = std::max(0, std::min(SHAPE_H - 1, (int)std::lround(want) - 1));
		if (best.top >= 0 && newTop != best.top)
		{
			std::vector<float> r(SHAPE_H, 0.f);
			for (int z = 0; z <= newTop; ++z)
			{
				float src = newTop == 0 ? 0.f : z * (float)best.top / (float)newTop;
				r[z] = best.rad[std::max(0, std::min(best.top, (int)std::lround(src)))];
			}
			best.rad = r;
			best.top = newTop;
		}
	}
	float stalkH = 0.f, stalkFill = 0.f;
	if (ragged && best.top >= 6)
	{
		best.rad[best.top] = best.rad[best.top - 1] = 0.f;
		best.top -= 2;
		stalkH = 2.5f;
		stalkFill = 0.45f;
	}
	static const bool shapeLog = std::getenv("OXCE_VR_SHAPE_LOG") != nullptr;
	if (shapeLog)
	{
		std::ostringstream o;
		o << "[VR] shape " << (md->getDataset() ? md->getDataset()->getName() : std::string("?")) << " sprite "
		  << md->getSprite(0) << ": " << (boxy ? "square" : "round") << " iou " << best.iou << " (round " << round.iou
		  << ", square " << square.iou << ") collision " << (100 * cov / opaque) << "% top " << best.top
		  << (patch ? " patch" : "") << " stalks " << stalkH << "/" << stalkFill << " trunk colours " << trunkCols.size() << " radii";
		for (int z = 0; z <= best.top; ++z) o << " " << best.rad[z];
		Log(LOG_INFO) << o.str();
	}
	// natural terrain only: craft, UFO and base parts keep their collision with the old rule's additions
	bool natural = true;
	if (md->getDataset())
	{
		const std::string &n = md->getDataset()->getName();
		for (const char *pre : {"U_", "UFO", "PLANE", "XBASE", "UBASE", "BRAIN", "BLANKS"})
			if (n.compare(0, std::strlen(pre), pre) == 0) natural = false;
		if (craftSets.count(n)) natural = false;
	}
	bool added = false;
	if (natural && best.top >= 0 && best.iou >= 0.6f)
	{
		// the shape replaces the collision, from the ground (the floor's top) up
		for (int z = 0; z < SHAPE_H; ++z)
			for (int y = 0; y < TILE_W; ++y)
				for (int x = 0; x < TILE_W; ++x)
					if (own[x][y][z] == part + 1) own[x][y][z] = 0;
		for (int z = 0; z <= best.top; ++z)
		{
			float rad = best.rad[z];
			if (rad <= 0.f) continue;
			for (int y = 0; y < TILE_W; ++y)
				for (int x = 0; x < TILE_W; ++x)
				{
					if (own[x][y][z]) continue;
					float dx = x + 0.5f - ax, dy = y + 0.5f - ay;
					if (dx * dx + dy * dy <= (rad + 0.2f) * (rad + 0.2f)) { own[x][y][z] = (uint8_t)(part + 1); added = true; }
				}
		}
		// stalks on the top: single voxels standing on some of the top's columns, 1 to 4 high
		if (stalkH >= 1.f && stalkFill > 0.f)
			for (int y = 0; y < TILE_W; ++y)
				for (int x = 0; x < TILE_W; ++x)
				{
					int zt = -1;
					for (int z = SHAPE_H - 1; z >= 0; --z) if (own[x][y][z] == part + 1) { zt = z; break; }
					if (zt < 0) continue;
					uint32_t hsh = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u) ^ (uint32_t)(md->getSprite(0) * 83492791u);
					hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
					if ((hsh & 1023) / 1024.f >= stalkFill) continue;
					int hgt = std::max(1, (int)std::lround(stalkH * (0.5f + ((hsh >> 10) & 1023) / 1024.f)));
					for (int z = zt + 1; z <= std::min(SHAPE_H - 1, zt + hgt); ++z) own[x][y][z] = (uint8_t)(part + 1);
				}
		// gaps a crown's art shows: where the picture is empty the voxels along that line of sight go (light
		// through the leaves); where it shows the trunk's or a branch's colour, the leaves in front of the
		// voxel nearest the axis on that line go, so the branch shows in a gap
		if (!boxy && patch)
		{
			std::map<int, std::vector<std::array<int, 3>>> rays; // sprite pixel -> the part's voxels drawn there
			for (int z = 0; z < SHAPE_H; ++z)
				for (int y = 0; y < TILE_W; ++y)
					for (int x = 0; x < TILE_W; ++x)
					{
						if (own[x][y][z] != part + 1) continue;
						int px, py;
						proj(x + 0.5f, y + 0.5f, z + 0.5f, px, py);
						py += rowShift;
						rays[py * 1024 + px].push_back({x, y, z});
					}
			for (auto &r : rays)
			{
				int px = r.first % 1024, py = r.first / 1024;
				Uint8 c = (px >= 0 && py >= 0 && px < W && py < H) ? spr->getPixel(px, py) : 0;
				if (!c)
				{
					for (auto &v : r.second) own[v[0]][v[1]][v[2]] = 0;
					continue;
				}
				if (!trunkCols.count(c) || py + 0.5f > gv - 3.f) continue; // the trunk below the crown stays
				// the voxel nearest the axis; the ones in front of it (towards the flat view's eye) go
				size_t keep = 0;
				float bestD = 1e9f;
				for (size_t i = 0; i < r.second.size(); ++i)
				{
					float dx = r.second[i][0] + 0.5f - ax, dy = r.second[i][1] + 0.5f - ay, d = dx * dx + dy * dy;
					if (d < bestD) { bestD = d; keep = i; }
				}
				int sKeep = r.second[keep][0] + r.second[keep][1] + r.second[keep][2];
				for (auto &v : r.second)
					if (v[0] + v[1] + v[2] > sKeep) own[v[0]][v[1]][v[2]] = 0;
			}
		}
		return added;
	}
	// no slice shape fits the art: add voxels round about the axis where the art has something (the old rule)
	rowShift = 0;
	for (int z = 2; z < TILE_H; ++z)
	{
		int row = (int)std::floor(groundRow - z - 0.5f);
		if (row < 0 || row >= H || reach[row] < 0.f) continue;
		float rr = std::min(7.5f, reach[row] / 1.4142f);
		for (int y = 0; y < TILE_W; ++y)
			for (int x = 0; x < TILE_W; ++x)
			{
				if (own[x][y][z]) continue;
				float dx = x + 0.5f - ax, dy = y + 0.5f - ay;
				if (dx * dx + dy * dy > rr * rr) continue;
				int px, py;
				proj(x + 0.5f, y + 0.5f, z + 0.5f, px, py);
				bool art = false;
				for (int q = -1; q <= 1 && !art; ++q)
					if (px + q >= 0 && px + q < W && py >= 0 && py < H && spr->getPixel(px + q, py)) art = true;
				if (art) { own[x][y][z] = (uint8_t)(part + 1); added = true; }
			}
	}
	return added;
}

/// A floor (0.21.0-terrain.9): its top a surface at height 0, where walls, objects and units stand and the
/// art draws the ground, coloured from its sprite as the flat game shows it (left out under the walls' and
/// objects' feet, and where relief replaces it). Its 2-voxel thickness hangs below that, drawn only on the
/// sides `edges` says have no floor beside them (the map's edge, a hole, the edge of an upper floor).
void Board::Impl::addFloor(const Surface *spr, int yoff, uint32_t avg, bool relief, int edges, const uint8_t own[TILE_W][TILE_W][SHAPE_H], Proto &p)
{
	const int W = spr->getWidth(), H = spr->getHeight();
	// the colour of the floor's top over cell (x, y): its sprite pixel, or the nearest one
	uint32_t top[TILE_W][TILE_W];
	for (int y = 0; y < TILE_W; ++y)
		for (int x = 0; x < TILE_W; ++x)
		{
			int px = (int)std::floor(16.f + (x - y)), py = (int)std::floor(24.f + (x + y + 1) * 0.5f + yoff);
			uint32_t c = avg;
			bool found = false;
			for (int r = 0; r <= 6 && !found; ++r)
				for (int dy = -r; dy <= r && !found; ++dy)
					for (int dx = -r; dx <= r && !found; ++dx)
					{
						if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
						int sx = px + dx, sy = py + dy;
						if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
						if (Uint8 v = spr->getPixel(sx, sy)) { c = rgba(pal[v]); found = true; }
					}
			top[y][x] = c;
		}
	// a floor whose art fills only part of the tile (a hull's edge, half a deck): only where the art is
	bool cellArt[TILE_W][TILE_W];
	int withArt = 0;
	for (int y = 0; y < TILE_W; ++y)
		for (int x = 0; x < TILE_W; ++x)
		{
			int px = (int)std::floor(16.f + (x - y)), py = (int)std::floor(24.f + (x + y + 1) * 0.5f + yoff);
			bool a = false;
			for (int dy = -1; dy <= 1 && !a; ++dy)
				for (int dx = -1; dx <= 1 && !a; ++dx)
				{
					int sx = px + dx, sy = py + dy;
					a = sx >= 0 && sy >= 0 && sx < W && sy < H && spr->getPixel(sx, sy);
				}
			cellArt[y][x] = a;
			withArt += a;
		}
	const bool partial = withArt * 10 < TILE_W * TILE_W * 9;
	auto open = [&](int x, int y) { return !own[x][y][0] && (!partial || cellArt[y][x]); };
	auto quad = [&](const glm::vec3 c[4], const glm::vec3 &n, const std::vector<uint32_t> &tex, int w, int h)
	{
		int ax, ay;
		if (!build->alloc(w + 2, h + 2, ax, ay)) return;
		for (int j = -1; j <= h; ++j)
			for (int i = -1; i <= w; ++i)
				build->put(ax + 1 + i, ay + 1 + j, tex[(size_t)glm::clamp(j, 0, h - 1) * w + glm::clamp(i, 0, w - 1)]);
		float tu0 = (ax + 1) / (float)ATLAS, tv0 = (ay + 1) / (float)ATLAS, tu1 = (ax + 1 + w) / (float)ATLAS, tv1 = (ay + 1 + h) / (float)ATLAS;
		const glm::vec4 white(1.f);
		uint32_t b = (uint32_t)p.verts.size();
		p.verts.push_back({c[0], n, {tu0, tv0}, white, MAT_BOARD});
		p.verts.push_back({c[1], n, {tu1, tv0}, white, MAT_BOARD});
		p.verts.push_back({c[2], n, {tu1, tv1}, white, MAT_BOARD});
		p.verts.push_back({c[3], n, {tu0, tv1}, white, MAT_BOARD});
		p.idx.insert(p.idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
	};
	// the top, merged into rectangles of open cells (not under a wall's or object's foot)
	if (!relief)
	{
		bool used[TILE_W][TILE_W] = {};
		for (int v = 0; v < TILE_W; ++v)
			for (int u = 0; u < TILE_W; ++u)
			{
				if (used[v][u] || !open(u, v)) continue;
				int w = 1;
				while (u + w < TILE_W && !used[v][u + w] && open(u + w, v)) ++w;
				int h = 1;
				for (bool ok = true; v + h < TILE_W && ok;)
				{
					for (int i = 0; i < w; ++i)
						if (used[v + h][u + i] || !open(u + i, v + h)) { ok = false; break; }
					if (ok) ++h;
				}
				std::vector<uint32_t> tex((size_t)w * h);
				for (int j = 0; j < h; ++j)
					for (int i = 0; i < w; ++i) { used[v + j][u + i] = true; tex[(size_t)j * w + i] = top[v + j][u + i]; }
				const glm::vec3 c[4] = {{(float)u, 0.f, (float)v}, {(float)(u + w), 0.f, (float)v}, {(float)(u + w), 0.f, (float)(v + h)}, {(float)u, 0.f, (float)(v + h)}};
				quad(c, glm::vec3(0, 1, 0), tex, w, h);
			}
	}
	// the thickness, hanging below on the open sides: the top's colours along that side, darker
	auto shadeC = [](uint32_t c, float f) -> uint32_t
	{
		uint32_t r = (uint32_t)((c & 255) * f), g = (uint32_t)(((c >> 8) & 255) * f), b = (uint32_t)(((c >> 16) & 255) * f);
		return r | (g << 8) | (b << 16) | 0xFF000000u;
	};
	const float T = (float)TILE_W, D = -2.f;
	for (int side = 0; side < 4; ++side)
	{
		if (!(edges & (1 << side))) continue;
		std::vector<uint32_t> tex((size_t)TILE_W * 2);
		for (int i = 0; i < TILE_W; ++i)
		{
			uint32_t c = side == 0 ? top[i][0] : side == 1 ? top[i][TILE_W - 1] : side == 2 ? top[0][i] : top[TILE_W - 1][i];
			tex[i] = shadeC(c, 0.78f);
			tex[TILE_W + i] = shadeC(c, 0.62f);
		}
		if (side == 0) { const glm::vec3 c[4] = {{0, 0, 0}, {0, 0, T}, {0, D, T}, {0, D, 0}}; quad(c, glm::vec3(-1, 0, 0), tex, TILE_W, 2); }
		else if (side == 1) { const glm::vec3 c[4] = {{T, 0, 0}, {T, 0, T}, {T, D, T}, {T, D, 0}}; quad(c, glm::vec3(1, 0, 0), tex, TILE_W, 2); }
		else if (side == 2) { const glm::vec3 c[4] = {{0, 0, 0}, {T, 0, 0}, {T, D, 0}, {0, D, 0}}; quad(c, glm::vec3(0, 0, -1), tex, TILE_W, 2); }
		else { const glm::vec3 c[4] = {{0, 0, T}, {T, 0, T}, {T, D, T}, {0, D, T}}; quad(c, glm::vec3(0, 0, 1), tex, TILE_W, 2); }
	}
}

/// One tile's parts built together as one shape (terrain round, 0.21.0-terrain):
///  - every voxel has one owner, so a face exists only once (two parts' faces in the same place
///    flickered): walls, then the object, and the floor owns the bottom slab (its 2 voxel layers),
///    which most walls and objects repeat;
///  - faces the flat game shows (top, east, south) take the owner's sprite where it projects; where the
///    sprite is empty there the face isn't drawn (the art says nothing is there), except on floors;
///  - faces it never shows (west, north) take the colour of the opposite side of the same solid, straight
///    through it (a wall's far side looks like its near side), else the nearest colour in the sprite.
const VR::Model *Board::Impl::modelOf(const MapData *md, int variant)
{
	MapDataSet *ds = md ? md->getDataset() : nullptr;
	if (!ds) return nullptr;
	for (size_t i = 0; i < ds->getSize(); ++i)
		if (ds->getObject(i) == md)
		{
			const int n = VR::modelVariants(ds->getName(), (int)i);
			return n ? VR::terrainModel(ds->getName(), (int)i, variant % n) : nullptr;
		}
	return nullptr;
}

int Board::Impl::modelVariantsOf(const MapData *md)
{
	MapDataSet *ds = md ? md->getDataset() : nullptr;
	if (!ds) return 0;
	for (size_t i = 0; i < ds->getSize(); ++i)
		if (ds->getObject(i) == md) return VR::modelVariants(ds->getName(), (int)i);
	return 0;
}

/// A hand-built model (VrModels.h) for a part: its faces greedy-merged per slice, two model voxels per table
/// voxel, coloured from the part's sprite by kind. A face the flat game's view sees takes the sprite pixel
/// it lands on (or the nearest one of its kind); a face it never sees takes the colour of the matching
/// point on the other side of the model's axis; undersides are that colour darkened. The paint layer
/// (z = 0) lies flat on the floor's top.
void Board::Impl::addModel(const VR::Model &m, const Surface *spr, int yoff, const Surface *floorSpr, Proto &p, const MapData *md)
{
	if (!spr) return;
	const int W = spr->getWidth(), H = spr->getHeight();
	const float hx = m.sx * 0.5f, hy = m.sy * 0.5f;
	// the model's axis: the middle of its solid voxels
	double cx = 0, cy = 0;
	long n = 0;
	for (int z = 1; z < m.sz; ++z)
		for (int y = 0; y < m.sy; ++y)
			for (int x = 0; x < m.sx; ++x)
				if (m.at(x, y, z)) { cx += x + 0.5; cy += y + 0.5; ++n; }
	const float axX = n ? 8.f + ((float)(cx / n) - hx) / m.perX : 8.f, axZ = n ? 8.f + ((float)(cy / n) - hy) / m.perX : 8.f;
	auto toTable = [&](float fx, float fy, float fz) { return glm::vec3(8.f + (fx - hx) / m.perX, (fz - 1.f) / m.perZ + m.baseY, 8.f + (fy - hy) / m.perX); }; // z 1 stands on the floor's top (height 0), or baseY
	// colour references (VrModels.h: Model::ref): that pixel of that part of the set
	MapDataSet *refSet = md ? md->getDataset() : nullptr;
	auto refColour = [&](uint32_t r, uint32_t &out) -> bool
	{
		if (!refSet || !refSet->getSurfaceset()) return false;
		const int part = (int)((r >> 16) & 255), row = (int)((r >> 8) & 255), col = (int)(r & 255);
		if (part >= (int)refSet->getSize() || !refSet->getObject(part)) return false;
		const Surface *rs = static_cast<const SurfaceSet*>(refSet->getSurfaceset())->getFrame(refSet->getObject(part)->getSprite(0));
		if (!rs || col >= rs->getWidth() || row >= rs->getHeight()) return false;
		Uint8 c = rs->getPixel(col, row);
		if (!c) return false;
		out = rgba(pal[c]);
		return true;
	};
	auto classOf = [&](Uint8 c) -> int
	{
		const SDL_Color &k = pal[c];
		if (k.g > k.r * 1.15f && k.g >= k.b) return VR::MK_LEAF;
		if (k.r > k.g * 1.6f && k.r > 90) return VR::MK_FRUIT;
		return VR::MK_WOOD;
	};
	// the sprite pixel of kind `kind` nearest (px, py) within `reach`, else any pixel; false: none
	auto sample = [&](float px, float py, int kind, int reach, uint32_t &out) -> bool
	{
		int x0 = (int)std::floor(px), y0 = (int)std::floor(py);
		int want = kind == VR::MK_PATCH ? (int)VR::MK_LEAF : kind;
		for (int pass = 0; pass < 2; ++pass)
			for (int r = 0; r <= reach; ++r)
				for (int dy = -r; dy <= r; ++dy)
					for (int dx = -r; dx <= r; ++dx)
					{
						if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
						int sx = x0 + dx, sy = y0 + dy;
						if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
						Uint8 c = spr->getPixel(sx, sy);
						if (!c) continue;
						int cl = classOf(c);
						if (pass == 0 && want != VR::MK_PLAIN && cl != want && !(want == VR::MK_FRUIT && cl == VR::MK_LEAF && r > 1)) continue;
						out = rgba(pal[c]);
						return true;
					}
		return false;
	};
	auto colour = [&](glm::vec3 P, int axis, int sign, int kind) -> uint32_t
	{
		bool under = axis == 2 && sign < 0;
		bool shown = sign > 0;
		if (!shown && !under) { P.x = 2.f * axX - P.x; P.z = 2.f * axZ - P.z; } // the other side of the axis
		float px = 16.f + P.x - P.z, py = 24.f + (P.x + P.z) * 0.5f - P.y + yoff;
		if (kind == VR::MK_LEAF)
		{
			// leaves: a pixel up to two away, picked per voxel, so the art's rows don't streak across the
			// crown's sides (where many voxels along the view land on the same pixel)
			uint32_t hsh = (uint32_t)(std::floor(P.x * 2.f) * 73856093.f) ^ (uint32_t)(std::floor(P.y * 2.f) * 19349663.f) ^ (uint32_t)(std::floor(P.z * 2.f) * 83492791.f);
			hsh = hsh * 2654435761u;
			px += (float)((int)((hsh >> 8) % 5) - 2);
			py += (float)((int)((hsh >> 16) % 5) - 2);
		}
		uint32_t c = 0xFF404040u;
		sample(px, py, kind, kind == VR::MK_PATCH ? 8 : 5, c);
		if (under)
		{
			uint32_t r = (c & 255) * 6 / 10, g = ((c >> 8) & 255) * 6 / 10, b = ((c >> 16) & 255) * 6 / 10;
			c = r | (g << 8) | (b << 16) | 0xFF000000u;
		}
		return c;
	};
	auto solid = [&](int x, int y, int z) -> bool { return z >= 1 && m.at(x, y, z) != VR::MK_EMPTY && m.at(x, y, z) != VR::MK_PATCH; };
	const int dims[3] = {m.sx, m.sy, m.sz};
	std::vector<uint32_t> maskCol;
	// mask: 0 no face, 1 plain, 2 pulsing light, 3 flipping light, 4/5 rim light layers A/B; above that
	// (terrain.22) the face's panel << 3, so faces only merge within a panel
	std::vector<uint16_t> mask;
	std::vector<uint8_t> used;
	// a panel's direction in the table's axes (model x, y, z = table x, z, y)
	auto panelDir = [&](int pid) { const float *n = &m.panelNormal[(size_t)(pid - 1) * 3]; return glm::normalize(glm::vec3(n[0], n[2], n[1])); };
	struct Dir { int axis, sign; };
	const Dir dirs[6] = {{0, 1}, {0, -1}, {1, 1}, {1, -1}, {2, 1}, {2, -1}};
	float emitMat = MAT_BOARD, emitLift = 0.f; // the faces' material; how far out they stand (light layers)
	int emitPanel = 0;                         // the faces' panel (0: lit by their own direction)
	auto emit = [&](int axis, int sign, float plane, int u, int v, int w, int h, int ua, int va, int nu, bool paint)
	{
		int ax, ay;
		if (!build->alloc(w + 2, h + 2, ax, ay)) return;
		for (int j = -1; j <= h; ++j)
			for (int i = -1; i <= w; ++i)
			{
				int si = glm::clamp(i, 0, w - 1), sj = glm::clamp(j, 0, h - 1);
				build->put(ax + 1 + i, ay + 1 + j, maskCol[(size_t)(v + sj) * nu + u + si]);
			}
		auto corner = [&](float uu, float vv) -> glm::vec3
		{
			float cc[3];
			cc[axis] = plane; cc[ua] = uu; cc[va] = vv;
			glm::vec3 t = toTable(cc[0], cc[1], cc[2]);
			if (paint) t.y = 0.05f; // flat on the floor's top
			return t;
		};
		float nn[3] = {0, 0, 0};
		nn[axis] = (float)sign;
		glm::vec3 nrm(nn[0], nn[2], nn[1]);
		const glm::vec3 lift = nrm * emitLift;
		if (emitPanel) nrm = panelDir(emitPanel); // lit as its panel (the lift, light layers only, stays square)
		float tu0 = (ax + 1) / (float)ATLAS, tv0 = (ay + 1) / (float)ATLAS;
		float tu1 = (ax + 1 + w) / (float)ATLAS, tv1 = (ay + 1 + h) / (float)ATLAS;
		glm::vec4 white(1.f);
		uint32_t b = (uint32_t)p.verts.size();
		p.verts.push_back({corner((float)u, (float)v) + lift, nrm, {tu0, tv0}, white, emitMat});
		p.verts.push_back({corner((float)(u + w), (float)v) + lift, nrm, {tu1, tv0}, white, emitMat});
		p.verts.push_back({corner((float)(u + w), (float)(v + h)) + lift, nrm, {tu1, tv1}, white, emitMat});
		p.verts.push_back({corner((float)u, (float)(v + h)) + lift, nrm, {tu0, tv1}, white, emitMat});
		// keep the winding the same way round as the table's own faces
		p.idx.insert(p.idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
	};
	auto greedy = [&](int axis, int sign, float plane, int ua, int va, int nu, int nv, bool paint)
	{
		used.assign((size_t)nu * nv, 0);
		for (int v = 0; v < nv; ++v)
			for (int u = 0; u < nu; ++u)
			{
				size_t o = (size_t)v * nu + u;
				if (!mask[o] || used[o]) continue;
				const uint16_t mv = mask[o]; // faces of one kind (plain, or a light's) and one panel only
				int w = 1;
				while (u + w < nu && mask[o + w] == mv && !used[o + w]) ++w;
				int h = 1;
				for (bool ok = true; v + h < nv && ok;)
				{
					for (int i = 0; i < w; ++i)
						if (mask[(size_t)(v + h) * nu + u + i] != mv || used[(size_t)(v + h) * nu + u + i]) { ok = false; break; }
					if (ok) ++h;
				}
				for (int j = 0; j < h; ++j)
					for (int i = 0; i < w; ++i) used[(size_t)(v + j) * nu + u + i] = 1;
				const int kindv = mv & 7;
				emitPanel = mv >> 3;
				emitMat = kindv == 2 ? MAT_GLOW_PULSE : kindv == 3 ? MAT_GLOW_FLIP : kindv == 4 ? MAT_GLOW_A : kindv == 5 ? MAT_GLOW_B : emitPanel ? MAT_PANEL : MAT_BOARD;
				emitLift = kindv >= 4 ? 0.04f : 0.f;
				emit(axis, sign, plane, u, v, w, h, ua, va, nu, paint);
				emitMat = MAT_BOARD; emitLift = 0.f; emitPanel = 0;
			}
	};
	for (const Dir &d : dirs)
	{
		int ua = d.axis == 0 ? 1 : 0;
		int va = d.axis == 2 ? 1 : 2;
		int nu = dims[ua], nv = dims[va];
		mask.assign((size_t)nu * nv, 0);
		maskCol.assign((size_t)nu * nv, 0u);
		for (int k = (d.axis == 2 ? 1 : 0); k < dims[d.axis]; ++k)
		{
			bool any = false;
			for (int v = 0; v < nv; ++v)
				for (int u = 0; u < nu; ++u)
				{
					int c[3];
					c[d.axis] = k; c[ua] = u; c[va] = v;
					int nb[3] = {c[0], c[1], c[2]};
					nb[d.axis] += d.sign;
					size_t o = (size_t)v * nu + u;
					bool f = solid(c[0], c[1], c[2]) && !solid(nb[0], nb[1], nb[2]);
					if (f && d.axis == 2 && d.sign < 0 && k == 1 && m.baseY == 0.f) f = false; // stands on the floor
					const size_t vi = ((size_t)c[2] * m.sy + c[1]) * m.sx + c[0];
					const uint8_t lt = (f && !m.light.empty()) ? m.light[vi] : 0;
					mask[o] = !f ? 0 : lt == VR::Model::LIGHT_PULSE ? 2 : lt == VR::Model::LIGHT_FLIP ? 3 : 1;
					if (!f) continue;
					// its panel, unless the face turns away from it (a cut end keeps its own light)
					if (!m.panel.empty() && m.panel[vi])
					{
						const float *pn = &m.panelNormal[(size_t)(m.panel[vi] - 1) * 3];
						if (pn[d.axis] * d.sign > 0.05f) mask[o] |= (uint16_t)(m.panel[vi] << 3);
					}
					any = true;
					// one colour per voxel (from its centre), whichever face: faces of a stepped surface
					// coloured from different sprite rows striped
					const uint32_t r = m.ref.empty() ? 0u : m.ref[((size_t)c[2] * m.sy + c[1]) * m.sx + c[0]];
					uint32_t rc;
					if (r && refColour(r, rc))
					{
						if (d.axis == 2 && d.sign < 0) // undersides darker, as below
							rc = ((rc & 255) * 6 / 10) | ((((rc >> 8) & 255) * 6 / 10) << 8) | ((((rc >> 16) & 255) * 6 / 10) << 16) | 0xFF000000u;
						maskCol[o] = rc;
					}
					else maskCol[o] = colour(toTable(c[0] + 0.5f, c[1] + 0.5f, c[2] + 0.5f), d.axis, d.sign, m.at(c[0], c[1], c[2]));
				}
			if (any) greedy(d.axis, d.sign, (float)k + (d.sign > 0 ? 1.f : 0.f), ua, va, nu, nv, false);
			// the rim's lights (A, B): a thin layer just over those faces, in the lights' colour; the shader
			// shows it on its frames only, the voxel's own colour shows through between
			if (any && !m.light.empty())
			{
				uint32_t lc = 0xFFFFFFFFu;
				refColour(m.lightRef, lc);
				bool anyLight = false;
				for (int v = 0; v < nv; ++v)
					for (int u = 0; u < nu; ++u)
					{
						size_t o = (size_t)v * nu + u;
						if (!mask[o]) continue;
						int c[3];
						c[d.axis] = k; c[ua] = u; c[va] = v;
						const uint8_t lt = m.light[((size_t)c[2] * m.sy + c[1]) * m.sx + c[0]];
						mask[o] = lt == VR::Model::LIGHT_A ? 4 : lt == VR::Model::LIGHT_B ? 5 : 0;
						if (mask[o]) { maskCol[o] = lc; anyLight = true; }
					}
				if (anyLight) greedy(d.axis, d.sign, (float)k + (d.sign > 0 ? 1.f : 0.f), ua, va, nu, nv, false);
			}
		}
	}
	// the paint layer, flat on the floor (0.21.0-terrain.8): the ground as the sprite draws it (roots,
	// shadows, sticks, a patch of another colour), less the tree. Ground the tree hides in the art (behind
	// the trunk and crown seen from the flat game's angle) takes the nearest ground that shows. Only what
	// differs from the floor's own colours is painted, so the floor shows round it; the paint's edge is
	// ragged so it never reads as a square tile.
	{
		const int nu = m.sx, nv = m.sy;
		// sprite pixels the standing tree covers (voxels a table voxel or more above the floor)
		std::vector<uint8_t> hidden((size_t)W * H, 0);
		for (int z = 1 + (int)m.perZ; z < m.sz; ++z) // a table voxel or more above the floor
			for (int y = 0; y < m.sy; ++y)
				for (int x = 0; x < m.sx; ++x)
				{
					if (!solid(x, y, z)) continue;
					glm::vec3 P = toTable(x + 0.5f, y + 0.5f, z + 0.5f);
					int px = (int)std::floor(16.f + P.x - P.z), py = (int)std::floor(24.f + (P.x + P.z) * 0.5f - P.y + yoff);
					if (px >= 0 && py >= 0 && px < W && py < H) hidden[(size_t)py * W + px] = 1;
				}
		// the floor's own colours: palette entries making up 2% or more of its sprite
		std::vector<SDL_Color> floorCols;
		if (floorSpr)
		{
			int count[256] = {0}, total = 0;
			for (int y = 0; y < floorSpr->getHeight(); ++y)
				for (int x = 0; x < floorSpr->getWidth(); ++x)
					if (Uint8 c = floorSpr->getPixel(x, y)) { ++count[c]; ++total; }
			for (int c = 1; c < 256; ++c) if (total && count[c] * 50 >= total) floorCols.push_back(pal[c]);
		}
		// hue (degrees) and value of a colour
		auto hv = [](int r, int g, int b, float &h, int &v)
		{
			int mx = std::max({r, g, b}), mn = std::min({r, g, b});
			v = mx;
			if (mx == mn) { h = -1.f; return; }
			float d = (float)(mx - mn);
			h = mx == r ? 60.f * std::fmod((g - b) / d + 6.f, 6.f) : mx == g ? 60.f * ((b - r) / d + 2.f) : 60.f * ((r - g) / d + 4.f);
		};
		auto differs = [&](uint32_t c) -> bool
		{
			if (floorCols.empty()) return true;
			int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255, best = 1 << 30;
			for (const SDL_Color &f : floorCols)
				best = std::min(best, (r - f.r) * (r - f.r) + (g - f.g) * (g - f.g) + (b - f.b) * (b - f.b));
			if (best <= 34 * 34) return false;
			// a darker shade of the floor's own hue is a shadow the artist drew (under a crown): the table casts
			// real shadows, so it isn't painted (0.21.0-terrain.9)
			float h; int v;
			hv(r, g, b, h, v);
			if (h >= 0.f)
				for (const SDL_Color &f : floorCols)
				{
					float fh; int fv;
					hv(f.r, f.g, f.b, fh, fv);
					float dh = std::fabs(h - fh);
					dh = std::min(dh, 360.f - dh);
					if (fh >= 0.f && dh < 25.f && v < fv) return false;
				}
			return true;
		};
		// each cell's ground colour where the art shows it; the rest from the nearest that shows
		std::vector<uint32_t> col((size_t)nu * nv, 0u);
		std::vector<uint8_t> shows((size_t)nu * nv, 0); // the art shows this ground (not hidden by the tree)
		std::vector<int> queue;
		for (int y = 0; y < nv; ++y)
			for (int x = 0; x < nu; ++x)
			{
				if (!m.at(x, y, 0)) continue;
				glm::vec3 P = toTable(x + 0.5f, y + 0.5f, 1.f); // ground: Y 0
				int px = (int)std::floor(16.f + P.x - P.z), py = (int)std::floor(24.f + (P.x + P.z) * 0.5f + yoff);
				if (px < 0 || py < 0 || px >= W || py >= H || hidden[(size_t)py * W + px]) continue;
				Uint8 c = spr->getPixel(px, py);
				if (!c) continue;
				col[(size_t)y * nu + x] = rgba(pal[c]);
				shows[(size_t)y * nu + x] = 1;
				queue.push_back(y * nu + x);
			}
		for (size_t q = 0; q < queue.size(); ++q)
		{
			int x = queue[q] % nu, y = queue[q] / nu;
			const int nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
			for (auto &n : nb)
			{
				if (n[0] < 0 || n[1] < 0 || n[0] >= nu || n[1] >= nv || !m.at(n[0], n[1], 0)) continue;
				uint32_t &t = col[(size_t)n[1] * nu + n[0]];
				if (t) continue;
				t = col[(size_t)y * nu + x];
				queue.push_back(n[1] * nu + n[0]);
			}
		}
		// what to paint: what differs from the floor, with a ragged edge towards the tile's sides
		std::vector<uint8_t> paint((size_t)nu * nv, 0);
		int x0 = nu, y0 = nv, x1 = -1, y1 = -1;
		for (int y = 0; y < nv; ++y)
			for (int x = 0; x < nu; ++x)
				if (m.at(x, y, 0)) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
		for (int y = 0; y < nv; ++y)
			for (int x = 0; x < nu; ++x)
			{
				size_t o = (size_t)y * nu + x;
				// ground the tree hides in the art isn't painted (0.21.0-terrain.9: one nearby pixel's colour spread
				// over it made a dark blob); only a gap inside a painted patch is filled (below)
				if (!shows[o] || !col[o] || !differs(col[o])) continue;
				int edge = std::min(std::min(x - x0, x1 - x), std::min(y - y0, y1 - y)); // cells from the side
				uint32_t hsh = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u);
				hsh *= 2654435761u;
				float keep = edge >= 4 ? 1.f : 0.3f + 0.175f * edge;
				if ((hsh >> 8) % 1000 < keep * 1000) paint[o] = 1;
			}
		// tidy: lone cells go, holes inside fill
		std::vector<uint8_t> tidy = paint;
		for (int y = 0; y < nv; ++y)
			for (int x = 0; x < nu; ++x)
			{
				int n = 0;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
						if ((dx || dy) && x + dx >= 0 && y + dy >= 0 && x + dx < nu && y + dy < nv && paint[(size_t)(y + dy) * nu + x + dx]) ++n;
				size_t o = (size_t)y * nu + x;
				if (paint[o] && n < 2) tidy[o] = 0;
				else if (!paint[o] && n >= 6 && col[o]) tidy[o] = 1;
			}
		mask.assign((size_t)nu * nv, 0);
		maskCol.assign((size_t)nu * nv, 0u);
		bool any = false;
		for (size_t o = 0; o < mask.size(); ++o)
			if (tidy[o]) { mask[o] = 1; maskCol[o] = col[o]; any = true; }
		if (any) greedy(2, 1, 1.f, 0, 1, nu, nv, true);
	}
}

void Board::Impl::buildTile(const TileKey &k, Proto &p)
{
	struct Src { const Surface *spr = nullptr; int yoff = 0; bool floor = false; uint32_t avg = 0; };
	Src src[O_MAX];
	static uint8_t own[TILE_W][TILE_W][SHAPE_H]; // 0 empty, else part + 1
	std::memset(own, 0, sizeof(own));
	auto loftSolid = [&](const MapData *md, int x, int y, int z) -> bool
	{
		int loft = md->getLoftID(z / 2);
		if (!loft || !voxels) return false;
		size_t idx = (size_t)loft * 16 + y;
		return idx < voxels->size() && ((*voxels)[idx] & (1 << (15 - x)));
	};
	static const int ORDER[O_MAX] = {O_WESTWALL, O_NORTHWALL, O_OBJECT, O_FLOOR};
	bool any = false, floorSolid = false;
	const VR::Model *modelled[O_MAX] = {}; // hand-built parts (any slot since terrain.18: the Skyranger's fin and nose are walls)
	for (int part : ORDER)
	{
		const MapData *md = k.m[part];
		if (!md) continue;
		Src &sc = src[part];
		if (md->getDataset() && md->getDataset()->getSurfaceset())
			sc.spr = static_cast<const SurfaceSet*>(md->getDataset()->getSurfaceset())->getFrame(md->getSprite(0));
		if (!sc.spr) continue;
		sc.yoff = md->getYOffset();
		sc.floor = md->getObjectType() == O_FLOOR;
		sc.avg = partInfo(md).avg;
		if (!sc.avg) continue; // fully transparent sprite: nothing to show
		if (const VR::Model *mm = modelOf(md, k.variant)) { modelled[part] = mm; continue; } // hand-built: not its voxels
		int count = 0;
		for (int z = 0; z < TILE_H; ++z)
			for (int y = 0; y < TILE_W; ++y)
				for (int x = 0; x < TILE_W; ++x)
				{
					if (!loftSolid(md, x, y, z)) continue;
					++count;
					if (part == O_FLOOR && z < 2) continue; // the floor is a surface at height 0 (addFloor)
					uint8_t &o = own[x][y][z];
					if (!o) o = (uint8_t)(part + 1);
				}
		if (count == 0) { addCard(md, p); continue; }
		if (part == O_FLOOR)
		{
			// a raised floor (a deck, a ramp): what rises above the slab stays voxels, standing from the
			// ground in the columns that go on up; the rest is the surface
			floorSolid = true;
			bool raised = false;
			for (int z = 2; z < SHAPE_H && !raised; ++z)
				for (int y = 0; y < TILE_W && !raised; ++y)
					for (int x = 0; x < TILE_W; ++x) if (own[x][y][z] == part + 1) { raised = true; break; }
			if (!raised) continue;
			any = true;
			for (int y = 0; y < TILE_W; ++y)
				for (int x = 0; x < TILE_W; ++x)
					if ((own[x][y][2] == part + 1 || own[x][y][3] == part + 1) && loftSolid(md, x, y, 0))
						for (int z = 0; z < 2; ++z) if (!own[x][y][z]) own[x][y][z] = (uint8_t)(part + 1);
			continue;
		}
		any = true;
		// most walls and objects repeat the floor's 2-voxel slab at the bottom of their collision: that copy
		// goes, the part's own foot stays (its columns that go on up); a part that is only that slab keeps it
		{
			bool above = false;
			int low = 0; // columns solid in both bottom layers
			for (int z = 0; z < SHAPE_H; ++z)
				for (int y = 0; y < TILE_W; ++y)
					for (int x = 0; x < TILE_W; ++x)
						if (own[x][y][z] == part + 1 && z >= 2) above = true;
			for (int y = 0; y < TILE_W; ++y)
				for (int x = 0; x < TILE_W; ++x)
					if (own[x][y][0] == part + 1 && own[x][y][1] == part + 1) ++low;
			// only a layer over (nearly) the whole tile is the floor's copy; a smaller low shape is the
			// part's own (rubble, a step)
			if (low * 10 < TILE_W * TILE_W * 9) above = false;
			// and only if the art doesn't come down to the ground along the tile's front edges (a sand mound
			// or a crop field does; a wall only along its own line): sample the layer's south and east sides
			if (above && sc.spr)
			{
				int hits = 0, n = 0;
				auto artAt = [&](float fx, float fy, float fz) -> bool
				{
					int px = (int)std::floor(16.f + fx - fy), py = (int)std::floor(24.f + (fx + fy) * 0.5f - fz + sc.yoff);
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							int qx = px + dx, qy = py + dy;
							if (qx >= 0 && qy >= 0 && qx < sc.spr->getWidth() && qy < sc.spr->getHeight() && sc.spr->getPixel(qx, qy)) return true;
						}
					return false;
				};
				for (int i = 0; i < TILE_W; ++i)
				{
					n += 2;
					hits += artAt(i + 0.5f, (float)TILE_W, 1.f);  // south side
					hits += artAt((float)TILE_W, i + 0.5f, 1.f);  // east side
				}
				if (hits * 10 >= n * 7) above = false; // the art stands on the ground there: the part's own
			}
			for (int z = 0; z < 2; ++z)
				for (int y = 0; y < TILE_W; ++y)
					for (int x = 0; x < TILE_W; ++x)
					{
						uint8_t &o = own[x][y][z];
						if (o != part + 1) continue;
						if (above && own[x][y][2] != part + 1 && own[x][y][3] != part + 1) o = 0; // (a part with nothing above keeps it all)
					}
		}
		int rowShift = 0;
		if (part == O_OBJECT && growToArt(md, sc.spr, sc.yoff, own, part, rowShift))
		{
			p.grew |= (uint8_t)(1 << part);
			sc.yoff += rowShift; // the shape was raised: its faces take the art's rows the same way
		}
	}
	// where each part's art lies over the shape seen from the flat game's angle (the lab's comparison): art
	// height 0 is table height 0 for every part (0.21.0-terrain.9: the floor's top is there too)
	for (int part = 0; part < O_MAX; ++part)
		if (k.m[part]) p.yoffs[part] = (int8_t)(src[part].spr ? src[part].yoff : k.m[part]->getYOffset());
	// painted rocks, mounds and holes on a listed floor (VrRelief.h)
	bool relief = k.m[O_FLOOR] && src[O_FLOOR].spr && VR::reliefMode() != VR::RELIEF_FLAT && VR::reliefSpec(k.m[O_FLOOR]);
	if (relief) addRelief(k.m[O_FLOOR], k.join, p);
	for (int part : ORDER)
	{
		if (!modelled[part]) continue;
		// an object's model paints the ground under it from the floor's art (the farm trees, terrain.8)
		const Surface *floorSpr = nullptr;
		if (part == O_OBJECT)
			if (const MapData *f = k.m[O_FLOOR])
				if (f->getDataset() && f->getDataset()->getSurfaceset()) floorSpr = static_cast<const SurfaceSet*>(f->getDataset()->getSurfaceset())->getFrame(f->getSprite(0));
		addModel(*modelled[part], src[part].spr, src[part].yoff, floorSpr, p, k.m[part]);
		p.model |= (uint8_t)(1 << part);
	}
	if (floorSolid && src[O_FLOOR].spr) addFloor(src[O_FLOOR].spr, src[O_FLOOR].yoff, src[O_FLOOR].avg, relief, k.edges, own, p);
	if (!any) return;
	auto S = [&](int x, int y, int z) -> int
	{
		if (x < 0 || y < 0 || z < 0 || x >= TILE_W || y >= TILE_W || z >= SHAPE_H) return 0;
		return own[x][y][z];
	};
	// a sprite pixel at (px, py), or a near one (the projection is a little coarse at edges)
	auto hit = [&](const Surface *spr, float px, float py, int reach, uint32_t &out) -> bool
	{
		int x = (int)std::floor(px), y = (int)std::floor(py);
		for (int r = 0; r <= reach; ++r)
			for (int dy = -r; dy <= r; ++dy)
				for (int dx = -r; dx <= r; ++dx)
				{
					if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
					int sx = x + dx, sy = y + dy;
					if (sx < 0 || sy < 0 || sx >= spr->getWidth() || sy >= spr->getHeight()) continue;
					if (Uint8 c = spr->getPixel(sx, sy)) { out = rgba(pal[c]); return true; }
				}
		return false;
	};
	// which parts' shown faces mostly land on empty art
	bool stray[O_MAX] = {false, false, false, false};
	{
		int hits[O_MAX] = {0, 0, 0, 0}, total[O_MAX] = {0, 0, 0, 0};
		uint32_t dummy;
		for (int z = 0; z < SHAPE_H; ++z)
			for (int y = 0; y < TILE_W; ++y)
				for (int x = 0; x < TILE_W; ++x)
				{
					int o = own[x][y][z];
					if (!o || src[o - 1].floor) continue;
					const Src &sc = src[o - 1];
					const int nb[3][3] = {{x + 1, y, z}, {x, y + 1, z}, {x, y, z + 1}};
					for (int a = 0; a < 3; ++a)
					{
						if (S(nb[a][0], nb[a][1], nb[a][2])) continue;
						float f[3] = {x + 0.5f, y + 0.5f, z + 0.5f};
						f[a] += 0.5f;
						++total[o - 1];
						if (hit(sc.spr, 16.f + (f[0] - f[1]), 24.f + (f[0] + f[1]) * 0.5f - f[2] + sc.yoff, 1, dummy)) ++hits[o - 1];
					}
				}
		for (int i = 0; i < O_MAX; ++i)
		{
			stray[i] = total[i] > 0 && hits[i] * 10 < total[i] * 4; // under 40% on the art
			if (stray[i]) p.stray |= (uint8_t)(1 << i);
		}
	}
	// the colour of a face (voxel c, direction axis/sign), or false: not drawn
	auto faceColor = [&](const int c[3], int axis, int sign, uint32_t &col) -> bool
	{
		int part = own[c[0]][c[1]][c[2]] - 1;
		const Src &sc = src[part];
		bool shown = sign > 0; // +x (east), +y (south), up: the faces the flat game's view sees
		int e[3] = {c[0], c[1], c[2]};
		int epart = part;
		if (!shown)
		{
			// through the solid to its far side, which the flat view does see
			while (S(e[0] + (axis == 0), e[1] + (axis == 1), e[2]) && e[axis] + 1 < (axis == 2 ? SHAPE_H : TILE_W)) ++e[axis];
			epart = own[e[0]][e[1]][e[2]] - 1;
		}
		const Src &es = src[epart];
		float f[3] = {e[0] + 0.5f, e[1] + 0.5f, e[2] + 0.5f};
		f[axis] += 0.5f; // the shown side of that voxel
		float fz = f[2];
		if (es.floor && (axis == 2 || e[2] < 2)) fz = 0.f; // floor sprites are drawn at height 0
		float px = 16.f + (f[0] - f[1]);
		float py = 24.f + (f[0] + f[1]) * 0.5f - fz + es.yoff;
		if (hit(es.spr, px, py, 1, col)) return true;
		if (sc.floor) { col = sc.avg; return true; }
		// art missing here: a part whose voxels mostly miss its art (tiny props on a bigger block) loses the
		// face; one that mostly matches (a hull, a wall) takes the nearest colour, no hole
		if (stray[part]) return false;
		if (hit(es.spr, px, py, 6, col)) return true;
		col = sc.avg;
		return true;
	};

	struct Dir { int axis, sign; };
	const Dir dirs[5] = {{0, 1}, {0, -1}, {1, 1}, {1, -1}, {2, 1}}; // no bottom faces: never seen from above
	const int dims[3] = {TILE_W, TILE_W, SHAPE_H};
	static uint32_t maskCol[SHAPE_H][SHAPE_H];
	static bool mask[SHAPE_H][SHAPE_H], used[SHAPE_H][SHAPE_H];
	for (const Dir &d : dirs)
	{
		int ua = d.axis == 0 ? 1 : 0;
		int va = d.axis == 2 ? 1 : 2;
		int nu = dims[ua], nv = dims[va];
		for (int kk = 0; kk < dims[d.axis]; ++kk)
		{
			bool anyFace = false;
			for (int v = 0; v < nv; ++v)
				for (int u = 0; u < nu; ++u)
				{
					int c[3];
					c[d.axis] = kk; c[ua] = u; c[va] = v;
					int nb[3] = {c[0], c[1], c[2]};
					nb[d.axis] += d.sign;
					bool m = S(c[0], c[1], c[2]) && !S(nb[0], nb[1], nb[2]);
					if (m && relief && d.axis == 2 && kk == 1 && own[c[0]][c[1]][c[2]] == O_FLOOR + 1) m = false; // the relief's top instead
					used[v][u] = false;
					if (m) m = faceColor(c, d.axis, d.sign, maskCol[v][u]);
					mask[v][u] = m;
					anyFace |= m;
				}
			if (!anyFace) continue;
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
					if (!build->alloc(w + 2, h + 2, ax, ay)) return;
					for (int j = -1; j <= h; ++j)
						for (int i = -1; i <= w; ++i)
						{
							int si = glm::clamp(i, 0, w - 1), sj = glm::clamp(j, 0, h - 1);
							build->put(ax + 1 + i, ay + 1 + j, maskCol[v + sj][u + si]);
						}
					float plane = (float)kk + (d.sign > 0 ? 1.f : 0.f);
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

/// The parts of a tile the table draws (an unseen wall or an open UFO door left out) and, for a listed
/// floor, which sides its relief carries on into a neighbour's.
Board::Impl::TileKey Board::Impl::tileKey(Tile *t, int x, int y, int z)
{
	TileKey key;
	for (int part = 0; part < O_MAX; ++part)
	{
		TilePart tp = (TilePart)part;
		const MapData *d = t->getMapData(tp);
		if (!d) continue;
		if ((tp == O_WESTWALL || tp == O_NORTHWALL) && !t->isDiscovered(tp)) continue;
		if (t->isUfoDoorOpen(tp)) continue;
		key.m[part] = d;
	}
	// a hand-built object with versions (the wheat): one picked by the tile's position, so a field of the
	// same part doesn't repeat one shape tile after tile
	if (key.m[O_OBJECT])
	{
		const int n = modelVariantsOf(key.m[O_OBJECT]);
		if (n > 1)
		{
			uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
			h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
			key.variant = (int)(h % (uint32_t)n);
		}
	}
	// where the floor's thickness shows: sides with no floor beside it (or the map's edge); on an upper level
	// not over a wall of the level below on that side (the wall fills that space, and its face and the
	// floor's would fall in the same place and flicker)
	if (key.m[O_FLOOR])
	{
		auto floorAt = [&](int qx, int qy) -> bool
		{
			if (qx < 0 || qy < 0 || qx >= mx || qy >= my) return false;
			Tile *n = battle->getTile(Position(qx, qy, z));
			return n && n->getMapData(O_FLOOR);
		};
		auto wallBelow = [&](int qx, int qy, TilePart wall) -> bool
		{
			if (z == 0 || qx < 0 || qy < 0 || qx >= mx || qy >= my) return false;
			Tile *b = battle->getTile(Position(qx, qy, z - 1));
			return b && b->getMapData(wall);
		};
		if (!floorAt(x - 1, y) && !wallBelow(x, y, O_WESTWALL)) key.edges |= 1;
		if (!floorAt(x + 1, y) && !wallBelow(x + 1, y, O_WESTWALL)) key.edges |= 2;
		if (!floorAt(x, y - 1) && !wallBelow(x, y, O_NORTHWALL)) key.edges |= 4;
		if (!floorAt(x, y + 1) && !wallBelow(x, y + 1, O_NORTHWALL)) key.edges |= 8;
	}
	if (key.m[O_FLOOR] && VR::reliefMode() != VR::RELIEF_FLAT)
		if (const VR::ReliefSpec *rs = VR::reliefSpec(key.m[O_FLOOR]))
		{
			const int nb[4][3] = {{x - 1, y, 1}, {x + 1, y, 2}, {x, y - 1, 4}, {x, y + 1, 8}};
			for (auto &q : nb)
			{
				Tile *n = (q[0] >= 0 && q[1] >= 0 && q[0] < mx && q[1] < my) ? battle->getTile(Position(q[0], q[1], z)) : nullptr;
				const VR::ReliefSpec *ns = n ? VR::reliefSpec(n->getMapData(O_FLOOR)) : nullptr;
				if (ns && ns->sign == rs->sign) key.join |= q[2];
			}
		}
	return key;
}

uint64_t Board::Impl::chunkHash(int cx, int cy, int z, uint64_t *terrain)
{
	uint64_t h = 1469598103934665603ull, th = 1469598103934665603ull;
	auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
	auto mixT = [&](uint64_t v) { th ^= v; th *= 1099511628211ull; };
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
				mixT((uint64_t)(uintptr_t)t->getMapData((TilePart)part));
				mixT(t->isUfoDoorOpen((TilePart)part));
			}
			mix((uint64_t)tileShade(t));
			mix((uint64_t)(poolWarm(t) * 2.f));
			mix(roofed(Position(x, y, z)) ? sunKey() : 0ull); // under a roof it depends on where the sun is
		}
	if (terrain) *terrain = th;
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

/// Fog of war (0.18.0-fog): unexplored ground is a dark void, as on the flat map, under a thin haze: two
/// see-through layers over the unexplored ground (a few voxels above the
/// highest fogged level), spilling half a tile onto explored ground so the known map fades into the
/// dark. Drawn without depth writes: shots inside it show, and their light glows in it (shader).
void Board::Impl::buildFog(int cx, int cy, Chunk &ch)
{
	const int SUB = 4;
	const float CW = TILE_W / (float)SUB, SPILL = 0.6f;
	// how much haze at a corner of the sub-grid (tile units), and the fogged level count it belongs to
	auto amount = [&](int gx, int gy, int &lvl) -> float
	{
		float u = gx / (float)SUB, v = gy / (float)SUB;
		float best = 1e9f;
		lvl = 0;
		for (int ty = (int)std::floor(v) - 1; ty <= (int)std::floor(v) + 1; ++ty)
			for (int tx = (int)std::floor(u) - 1; tx <= (int)std::floor(u) + 1; ++tx)
			{
				int n = fogTop(tx, ty);
				if (!n) continue;
				float dx = std::max(std::max(tx - u, 0.f), u - (tx + 1)), dy = std::max(std::max(ty - v, 0.f), v - (ty + 1));
				float d = std::sqrt(dx * dx + dy * dy);
				if (d < best || (d == best && n > lvl)) { best = d; lvl = n; }
			}
		if (best >= SPILL) return 0.f;
		float k = best / SPILL;
		return 1.f - k * k * (3.f - 2.f * k);
	};
	MeshData md;
	const glm::vec4 col(0.050f, 0.058f, 0.082f, 1.f);
	for (int sy = cy * CHUNK * SUB; sy < std::min(my, (cy + 1) * CHUNK) * SUB; ++sy)
		for (int sx = cx * CHUNK * SUB; sx < std::min(mx, (cx + 1) * CHUNK) * SUB; ++sx)
		{
			int l[4];
			float a[4] = {amount(sx, sy + 1, l[0]), amount(sx + 1, sy + 1, l[1]), amount(sx + 1, sy, l[2]), amount(sx, sy, l[3])};
			if (a[0] <= 0.f && a[1] <= 0.f && a[2] <= 0.f && a[3] <= 0.f) continue;
			int lvl = std::max(std::max(l[0], l[1]), std::max(l[2], l[3]));
			float x0 = sx * CW, x1 = x0 + CW, z0 = sy * CW, z1 = z0 + CW;
			for (int layer = 0; layer < 2; ++layer)
			{
				float y = (lvl - 1) * TILE_H + (layer ? 9.f : 3.f);
				md.addQuad({x0, y, z1}, {x1, y, z1}, {x1, y, z0}, {x0, y, z0}, col, MAT_FOG,
					{(float)layer, 2.f}, {(float)layer, 2.f}, {(float)layer, 2.f}, {(float)layer, 2.f});
				size_t b = md.verts.size() - 4;
				for (int i = 0; i < 4; ++i) md.verts[b + i].color.a = a[i];
			}
		}
	ch.empty = md.indices.empty();
	ch.tris = md.indices.size() / 3;
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
				// unexplored ground: the void, a near-black slab (the haze drifts over it)
				if (z == 0 && fogX >= 0)
				{
					float u0 = fogX / (float)ATLAS, v0 = fogY / (float)ATLAS, u1 = (fogX + 16) / (float)ATLAS, v1 = (fogY + 16) / (float)ATLAS;
					const float sb = 0.02f; // the void: near black, day and night
					md.addQuad(off + glm::vec3(0, -1.5f, TILE_W), off + glm::vec3(TILE_W, -1.5f, TILE_W), off + glm::vec3(TILE_W, -1.5f, 0), off + glm::vec3(0, -1.5f, 0), glm::vec4(sb, sb, sb, 1.f), MAT_BOARD, {u0, v1}, {u1, v1}, {u1, v0}, {u0, v0});
				}
				// walls already seen from outside stay standing (the flat map shows them too)
				TileKey wk;
				for (TilePart tp : {O_WESTWALL, O_NORTHWALL})
				{
					const MapData *d = t->getMapData(tp);
					if (d && t->isDiscovered(tp) && !t->isUfoDoorOpen(tp)) wk.m[tp] = d;
				}
				if (wk.m[O_WESTWALL] || wk.m[O_NORTHWALL])
				{
					const Proto &p = tileProto(wk);
					uint32_t base = (uint32_t)md.verts.size();
					float b = shadeFactor(tileShade(t));
					for (const Vertex &v : p.verts) { Vertex w = v; w.pos += off; w.color = glm::vec4(b, b, b, 1.f); md.verts.push_back(w); }
					for (uint32_t i : p.idx) md.indices.push_back(base + i);
				}
				continue;
			}
			// shade 0 (bright) .. 15 (dark), like the flat map
			float b = shadeFactor(tileShade(t));
			// alpha: the share of warm light (fires, flares) in its own light, + 2 under a roof (shader mode 3)
			glm::vec4 tint(b, b, b, poolWarm(t) + (indoors(Position(x, y, z)) ? 2.f : 0.f));
			TileKey key = tileKey(t, x, y, z);
			{
				const Proto &p = tileProto(key);
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
	ch.tris = md.indices.size() / 3;
	if (!ch.mesh) ch.mesh.reset(new Mesh());
	ch.mesh->upload(md);
}

/// Glides the table so the soldier stands near the player's side of the map, with most of the table
/// in front of it, and points the flat screen's camera at the soldier.
void Board::Impl::focusOnUnit(const BattleUnit *u, Camera *cam)
{
	if (!u) return;
	returnIn = -1.f;
	glm::vec3 up = unitVoxelPos(u);
	float k = tileSize / TILE_W;
	float hd = (mapRect.w - mapRect.y) * 0.5f;
	// the soldier's offset from the table's middle (world, +z toward the player), back in map voxels
	glm::vec3 off = glm::vec3(glm::rotate(glm::mat4(1.f), -yaw, {0, 1, 0}) * glm::vec4(0.f, 0.f, NEAR_SPOT * hd, 0.f)) / k;
	focusTarget.x = glm::clamp(up.x - off.x, 0.f, (float)(mx * TILE_W));
	focusTarget.z = glm::clamp(up.z - off.z, 0.f, (float)(my * TILE_W));
	if (cam)
	{
		cam->centerOnPosition(u->getPosition(), false);
		camSet = cam->getCenterPosition();
		camSetValid = true;
	}
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

/// The alien turn: the table looks where the flat screen looks, as it looks: the map's north-west corner
/// away from the player, zoomed so the flat screen's map area fills the table, centred on the screen's
/// middle at the level shown. win is that area: the flat projection puts a tile at screen x = (x - y) * 16 +
/// offset, y = (x + y) * 8 - level * 24 + offset (Camera::convertMapToScreen), its floor's middle 16, 32
/// pixels into the sprite.
void Board::Impl::followFlatScreen(Camera *cam)
{
	Position off = cam->getMapOffset();
	float W = (float)cam->vrScreenWidth(), H = (float)cam->vrVisibleMapHeight();
	float u = (W * 0.5f - off.x - 16.f) / 16.f;
	float v = (H * 0.5f - off.y - 32.f + viewLevel * 24.f) / 8.f + 1.f;
	win = glm::vec4(u, v, W / 32.f, H / 16.f);
	float X = (u + v) * 0.5f, Y = (v - u) * 0.5f;
	focusTarget = glm::vec3(X * TILE_W, 0.f, Y * TILE_W);
	yaw = FLAT_YAW;
	// the area along the table's width and depth, in tiles (one step along a diagonal is 0.71 tile)
	float tw = 2.f * win.z * 0.70710678f, td = 2.f * win.w * 0.70710678f;
	float w = mapRect.z - mapRect.x, d = mapRect.w - mapRect.y;
	if (tw > 0.f && td > 0.f && w > 0.f && d > 0.f)
	{
		float fit = std::min(w / tw, d / td) * 0.98f;
		alienZoom = glm::clamp(alienZoom, 0.02f / fit, 0.14f / fit);
		tileSize = glm::clamp(fit * alienZoom, 0.02f, 0.14f);
	}
}

/// The map's things are drawn only through the window (and dissolve in and out of it); everything else
/// is left to the darkness under them (drawCover).
void Board::Impl::applyWin(const Shader &sh, bool on) const
{
	bool use = on && winOn && !shadowPass && !feedPass;
	sh.set("uWinOn", use ? 1 : 0);
	if (!use) return;
	sh.set("uWin", win);
	sh.set("uWinReveal", reveal);
	sh.set("uWinInv", glm::inverse(boardMatrix()));
}

/// The darkness over the whole map area of the table during the alien turn: the void's colour with its
/// drifting haze (shader), lit only by the battle's own flashes. It lies just above the table's glass and
/// under the map's ground, so the window's map covers it.
void Board::Impl::drawCover(const Shader &sh)
{
	if (!winOn || shadowPass || feedPass) return;
	if (coverRect != mapRect)
	{
		coverRect = mapRect;
		MeshData md;
		float y = surfaceY() + 0.0006f;
		md.addQuad({mapRect.x, y, mapRect.w}, {mapRect.z, y, mapRect.w}, {mapRect.z, y, mapRect.y}, {mapRect.x, y, mapRect.y},
			glm::vec4(0.050f, 0.058f, 0.082f, 1.f), MAT_FOG, {0, 3}, {0, 3}, {0, 3}, {0, 3});
		coverMesh.upload(md);
	}
	sh.set("uMode", 0);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", glm::mat4(1.f));
	sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
	coverMesh.draw();
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
		bool busy = bs->getBattleGame() && bs->getBattleGame()->isBusy();
		bool tapped = tapSelect > 0.f;
		tapSelect = std::max(0.f, tapSelect - dt);
		const bool alienTurn = !playerTurn && !battle->getDebugMode();
		// is the flat game showing its map at all? Not under a full-screen state (the next turn's screen,
		// the inventory): only small windows may sit over it
		bool mapOnScreen = false;
		if (game)
			for (auto it = game->getStates().rbegin(); it != game->getStates().rend(); ++it)
				if ((*it)->isScreen()) { mapOnScreen = *it == (State*)bs; break; }
		if (alienTurn && !alienView)
		{
			// the alien turn begins: the player's view is kept for their next turn
			alienView = true;
			savedView = true;
			savedYaw = yaw;
			savedTile = tileSize;
			savedFocus = focusTarget;
			inShot = false;
			returnIn = -1.f;
		}
		else if (!alienTurn && alienView)
		{
			alienView = false;
			if (savedView) { yaw = savedYaw; tileSize = savedTile; focus = focusTarget = savedFocus; }
			savedView = false;
			lastSelected = sel; // (the view stays as the player left it: no bringing the first soldier close)
			camSet = c;
			camSetValid = true;
		}
		if (alienView)
		{
			followFlatScreen(cam);
			// shown: dissolving in at once; hidden: the darkness falls (dissolving, no flicker: owner, hidden.3)
			if (!hidden && mapOnScreen) { fallT = -1.f; reveal = std::min(1.f, reveal + dt / REVEAL_IN); }
			else if (reveal > 0.f || fallT >= 0.f)
			{
				if (fallT < 0.f) fallT = 0.f;
				fallT += dt;
				float base = std::max(0.f, 1.f - fallT / FALL_T);
				reveal = base;
				if (base <= 0.f) { reveal = 0.f; fallT = -1.f; }
			}
			winOn = true;
			lastSelected = sel;
			walkedLast = false;
			walkedUnit = sel;
		}
		else
		{
			// back to the player: the whole map dissolves in, once the flat game shows it
			win = glm::vec4(0.f, 0.f, 1e6f, 1e6f);
			fallT = -1.f;
			if (mapOnScreen || !winOn) reveal = std::min(1.f, reveal + dt / REVEAL_IN);
			winOn = reveal < 1.f;
		}
		if (!alienView && bs->getMap()->getProjectile())
		{
			if (!inShot)
			{
				// a new shot (or burst): remember the view to come back to
				inShot = true;
				preShotFocus = focusTarget;
				preShotCam = camSetValid ? camSet : c;
			}
			sinceProjectile = 0.f;
		}
		else if (!alienView) sinceProjectile += dt;

		if (!alienView)
		{
		// The table keeps its own view. The game's camera is followed when the game moves it on purpose:
		// another soldier selected, an alien button pressed, a shot being chased, the alien turn's visible
		// action. Its automatic "centre on the soldier" after a move is ignored (the table settles on the
		// soldier itself when the walk ends).
		if (!camSetValid || c.x != camSet.x || c.y != camSet.y)
		{
			bool follow = true, onUnit = false;
			bool nearSel = sel && !sel->isOut() && std::abs(sel->getPosition().x - c.x) <= 1 && std::abs(sel->getPosition().y - c.y) <= 1;
			if (hidden) follow = false; // nothing may be revealed while the movement is hidden
			else if (inShot)
			{
				if (c.x == preShotCam.x && c.y == preShotCam.y)
				{
					// the camera went back after the shot: so does the table, exactly where it was
					focusTarget = preShotFocus;
					follow = false;
				}
			}
			else if (playerTurn && nearSel)
			{
				// just re-centring on the soldier we already have, or on one tapped on the table (it is
				// within reach already, and so are the tiles around it)
				if (sel == lastSelected || tapped) follow = false;
				else onUnit = true;
			}
			camSet = c;
			camSetValid = true;
			if (follow) returnIn = -1.f; // the game looked somewhere else on purpose
			if (onUnit) focusOnUnit(sel, cam);
			else if (follow) focusTarget = glm::vec3(c.x * TILE_W + TILE_W * 0.5f, 0.f, c.y * TILE_W + TILE_W * 0.5f);
		}
		// the shot is over once the projectile is gone and the game has finished with it
		if (inShot && !bs->getMap()->getProjectile() && !busy && sinceProjectile > 0.2f)
		{
			inShot = false;
			if (c.x == preShotCam.x && c.y == preShotCam.y) { if (!hidden) focusTarget = preShotFocus; }
			else if (playerTurn) returnIn = 0.8f; // the game leaves its camera on the impact: look, then go back
		}
		if (returnIn >= 0.f)
		{
			if (inShot || !playerTurn || held[0] || held[1]) returnIn = -1.f;
			else if ((returnIn -= dt) < 0.f)
			{
				focusTarget = preShotFocus;
				cam->centerOnPosition(preShotCam, false);
				camSet = cam->getCenterPosition();
				camSetValid = true;
				returnIn = -1.f;
			}
		}

		if (sel != lastSelected)
		{
			// a newly selected soldier (next-soldier button, the first one when the mission or our turn
			// starts): bring it close. One tapped on the table is within reach: the view stays.
			lastSelected = sel;
			if (sel && playerTurn && !sel->isOut() && !hidden && !tapped) focusOnUnit(sel, cam);
		}

		// keep a walking unit in view: glide just enough to keep it inside a band around where a
		// focused soldier stands (near the player's side of the table)
		if (!hidden && !inShot && sel && !sel->isOut() && (sel->getStatus() == STATUS_WALKING || sel->getStatus() == STATUS_FLYING)
			&& (playerTurn || sel->getVisible()) && !held[0] && !held[1])
		{
			auto it = cards.find(sel);
			glm::vec3 up = (it != cards.end() && it->second.visInit) ? it->second.vis : unitVoxelPos(sel);
			float k = tileSize / TILE_W;
			// unit offset from the focus target, in table space (metres; +z toward the player)
			glm::vec3 ow = glm::vec3(glm::rotate(glm::mat4(1.f), yaw, {0, 1, 0}) * glm::vec4((up.x - focusTarget.x) * k, 0.f, (up.z - focusTarget.z) * k, 0.f));
			float hw = (mapRect.z - mapRect.x) * 0.5f, hd = (mapRect.w - mapRect.y) * 0.5f;
			glm::vec3 shift(0.f);
			float bx = hw * 0.35f, zFar = -hd * 0.15f, zNear = hd * 0.65f;
			if (ow.x > bx) shift.x = ow.x - bx; else if (ow.x < -bx) shift.x = ow.x + bx;
			if (ow.z > zNear) shift.z = ow.z - zNear; else if (ow.z < zFar) shift.z = ow.z - zFar;
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
		// when the walk ends (turns between steps and reaction fire included), settle on the soldier:
		// close to the player during our turn, just back into view during the aliens'
		bool walkingNow = sel && (sel->getStatus() == STATUS_WALKING || sel->getStatus() == STATUS_FLYING
			|| (walkedLast && (sel->getStatus() == STATUS_TURNING || busy)));
		if (walkedLast && !walkingNow && sel == walkedUnit && sel && !sel->isOut() && !hidden && (playerTurn || sel->getVisible()))
		{
			if (playerTurn && sel->getFaction() == FACTION_PLAYER) focusOnUnit(sel, cam);
			else
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
		}
		walkedLast = walkingNow;
		walkedUnit = sel;
		}
		curtain = 0.f;
	}
	else
	{
		viewLevel = lab ? glm::clamp(labLevel, 0, mz - 1) : mz - 1;
	}
	if (!focusInit) { focus = focusTarget; focusInit = true; }
	if (alienView) focus = focusTarget; // jumps with the flat camera (under the darkness), no glide
	else if (!held[0] && !held[1])
	{
		float a = 1.f - std::exp(-dt * 5.f);
		focus += (focusTarget - focus) * a;
	}

	// rebuild changed chunks; after the first build only a few per frame, to avoid hitches (during the
	// alien turn everything goes on underneath the darkness)
	int budget = 6;
	for (int z = 0; z < mz; ++z)
		for (int cy = 0; cy < cyN; ++cy)
			for (int cx = 0; cx < cxN; ++cx)
			{
				Chunk &ch = chunks[((size_t)z * cyN + cy) * cxN + cx];
				uint64_t th = 0;
				uint64_t h = chunkHash(cx, cy, z, &th);
				// the sun-reach traces only depend on the terrain (and the sun, which clears them itself):
				// something destroyed or a door opened anywhere can change them, so they are all redone
				if (!ch.terrainKnown || th != ch.terrain)
				{
					if (ch.terrainKnown) reachCache.clear();
					ch.terrain = th;
					ch.terrainKnown = true;
				}
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
	{
		smokeSnap.assign((size_t)mx * my * mz, 0);
		fireSnap.assign((size_t)mx * my * mz, 0);
		for (int z = 0; z < mz; ++z)
			for (int y = 0; y < my; ++y)
				for (int x = 0; x < mx; ++x)
					if (Tile *tl = battle->getTile(Position(x, y, z)))
					{
						size_t i = ((size_t)z * my + y) * mx + x;
						smokeSnap[i] = (uint8_t)std::min(255, std::max(0, tl->getSmoke()));
						fireSnap[i] = (uint8_t)std::min(255, std::max(0, tl->getFire()));
					}
	}
	updateEffects(dt);
	// the weather over the part of the map the table shows
	{
		stopTimer -= dt;
		if (stopTimer <= 0.f || stopLevel != viewLevel) { updateStops(); stopTimer = 0.4f; stopLevel = viewLevel; }
		float k = tileSize / TILE_W;
		glm::mat4 inv = glm::inverse(boardMatrix());
		WeatherView v;
		v.mx = mx; v.my = my;
		v.top = (std::min(viewLevel, mz - 1) + 1) * (float)TILE_H;
		v.ceiling = std::min(v.top + 16.f, stopMax + 30.f);
		v.center = glm::vec2(focus.x, focus.z);
		v.radius = std::max(mapRect.z - mapRect.x, mapRect.w - mapRect.y) * 0.5f / k * 1.15f;
		v.viewer = glm::vec3(inv * glm::vec4(viewer, 1.f));
		v.viewerFwd = glm::vec3(inv * glm::vec4(listenerFwd, 0.f));
		v.metersPerVoxel = k;
		for (const glm::vec3 &h : handPts) v.fade.push_back(glm::vec4(glm::vec3(inv * glm::vec4(h, 1.f)), 0.07f / k));
		if (cursorSet && battle->getTile(cursorTile))
			v.fade.push_back(glm::vec4((cursorTile.x + 0.5f) * TILE_W, cursorTile.z * TILE_H + 8.f, (cursorTile.y + 0.5f) * TILE_W, 1.4f * TILE_W));
		v.light = shadeFactor(globalShade());
		v.stop = [this](int x, int y) { return (x < 0 || y < 0 || x >= mx || y >= my) ? 0.f : stopCache[(size_t)y * mx + x]; };
		v.open = [this](int x, int y) { return x >= 0 && y >= 0 && x < mx && y < my && openCache[(size_t)y * mx + x] != 0; };
		weather.update(dt, v);
	}
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
	glm::vec3 lightVox = glm::vec3(inv * glm::vec4(shadowLight, 1.f)); // where the shadow pass turned the standees
	for (BattleUnit *u : *battle->getUnits())
	{
		auto it = cards.find(u);
		if (it == cards.end() || !it->second.tex) continue;
		if (u->isOut()) continue;
		if (!(u->getFaction() == FACTION_PLAYER || u->getVisible() || battle->getDebugMode())) continue;
		if (u->getPosition().z > viewLevel && !shadowPass) continue;
		if (u == hiddenUnit && !shadowPass) continue;
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
		// lit by its tile, like the flat map draws units (dark at night unless a light is near)
		float ub = 1.f;
		float uw = 1.f;
		bool under = false;
		if (Tile *ut = u->getTile()) { ub = shadeFactor(tileShade(ut)); uw = poolWarm(ut); under = indoors(ut->getPosition()); }
		sh.set("uTint", glm::vec4(ub, ub, ub, 1.f));
		sh.set("uPoolWarm", uw);
		sh.set("uPoolRoofed", under ? 1 : 0);
		if (!shadowPass && hasShadowLight)
		{
			// the shadow map holds this card turned to the light: each point of the card you see takes its
			// sun shadow from the matching point of that copy (same height, same distance from the middle),
			// so the card never shades itself while walls and other figures still shade the parts they cover
			glm::vec2 tl(lightVox.x - p.x, lightVox.z - p.z);
			float faceL = std::atan2(tl.x, tl.y);
			glm::mat4 mL = M * glm::translate(glm::mat4(1.f), p + glm::vec3(0, lift, 0))
				* glm::rotate(glm::mat4(1.f), faceL, {0, 1, 0})
				* glm::translate(glm::mat4(1.f), {0.f, card.anchorY - h * 0.5f, 0.f})
				* glm::scale(glm::mat4(1.f), {w, h, 1.f});
			glm::vec3 axis(m * glm::vec4(0.f, 0.f, 0.f, 1.f));
			glm::vec3 rv = glm::normalize(glm::vec3(m * glm::vec4(1.f, 0.f, 0.f, 0.f)));
			glm::vec3 rl = glm::normalize(glm::vec3(mL * glm::vec4(1.f, 0.f, 0.f, 0.f)));
			glm::vec3 nl = glm::normalize(glm::vec3(mL * glm::vec4(0.f, 0.f, 1.f, 0.f)));
			glm::vec3 up = glm::normalize(glm::vec3(m * glm::vec4(0.f, 1.f, 0.f, 0.f)));
			if (glm::dot(nl, shadowLight - axis) < 0.f) nl = -nl;
			sh.set("uFigShadow", 1);
			sh.set("uFigAxis", axis);
			sh.set("uFigRV", rv);
			sh.set("uFigRL", rl);
			sh.set("uFigNL", nl);
			sh.set("uFigUp", up);
		}
		sh.set("uModel", m);
		card.tex->bind(0);
		quad.draw();
	}
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uPoolWarm", 1.f);
	sh.set("uPoolRoofed", 0);
	sh.set("uFigShadow", 0);
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

	// glowing shots and blasts light the terrain and figures (not in the shadow pass)
	{
		int n = 0;
		if (!shadowPass)
		{
			float k = tileSize / TILE_W;
			for (const Glow &g : glows)
			{
				if (n >= 8) break;
				glm::vec3 w = glm::vec3(M * glm::vec4(g.vox, 1.f));
				sh.set(("uGlowPos[" + std::to_string(n) + "]").c_str(), glm::vec4(w, g.radius * k));
				sh.set(("uGlowCol[" + std::to_string(n) + "]").c_str(), g.col);
				++n;
			}
		}
		sh.set("uGlowCount", n);
	}
	// terrain
	sh.set("uMode", 3);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", M);
	atlas.tex.bind(0);
	sh.set("uTerrain", 1);
	// levels the view cuts away still cast shadows: roofs keep the sun (and clouds) out of the rooms below
	for (int z = 0; z <= (shadowPass ? mz - 1 : std::min(viewLevel, mz - 1)); ++z)
		for (int cy = 0; cy < cyN; ++cy)
			for (int cx = 0; cx < cxN; ++cx)
			{
				const Chunk &ch = chunks[((size_t)z * cyN + cy) * cxN + cx];
				if (ch.mesh && !ch.empty) ch.mesh->draw();
			}
	sh.set("uTerrain", 0);

	drawUnits(sh);
	sh.set("uGlowCount", 0);

	if (shadowPass) { sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f)); return; } // only solid things cast shadows

	// fog of war over the unexplored map
	sh.set("uMode", 0);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", M);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE); // the haze: shots inside it still show
	for (const Chunk &fc : fogChunks)
		if (fc.mesh && !fc.empty) fc.mesh->draw();
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);

	drawEffects(sh, M);

	if (!feedPass && !capturePass) drawHud(sh, M);
	if (!feedPass && !shadowPass && lab)
	{
		if (!capturePass) drawLab(sh, M);
		else if (captureTile.x >= 0 && captureTile.z <= viewLevel) drawTicks(sh, M, captureTile, glm::vec4(2.2f, 1.6f, 0.2f, 1.f));
	}
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
	glows.clear();
	glm::mat4 M = boardMatrix();
	auto toWorld = [&](const glm::vec3 &v) { return glm::vec3(M * glm::vec4(v, 1.f)); };
	if (testFlashT > 0.f)
	{
		// a test burst lights its surroundings like an explosion does (fading over half a second)
		float k = std::min(1.f, testFlashT / 0.5f);
		flashes.push_back({toWorld(testFlashAt + glm::vec3(0, 6, 0)), glm::vec3(1.f, 0.55f, 0.18f) * 2.5f * k});
		glows.push_back({testFlashAt + glm::vec3(0, 8, 0), glm::vec3(1.f, 0.55f, 0.2f) * 2.2f * k, 16.f * 5.f});
		testFlashT -= dt;
	}
	static uint32_t st = 777u;
	Map *map = (bs && bs->getMap()) ? bs->getMap() : nullptr;

	// projectile: pick a style from what was fired; trails leave particles behind
	// while the movement is hidden, shots and blasts out of sight aren't shown (the flat game shows
	// "Hidden Movement" then; a shot your soldiers can see makes it show the map, and so the table)
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
			// which shots light their surroundings, and how far (stronger weapons of a class reach further)
			projGlow = projStyle == 1 || projStyle == 2 || projStyle == 3 || dt2 == DT_IN;
			projGlowR = 16.f * (projStyle == 1 ? 2.f + 1.5f * k : projStyle == 2 ? 2.5f + 2.f * k : projStyle == 3 ? 2.5f + k : 2.f);
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
		if (projGlow) glows.push_back({now, projColor * (0.9f + 0.6f * projPower) * (projStyle == 3 ? 1.4f : 1.f), projGlowR});
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
			// the blast lights up the ground around it; a glowing shot's impact flares in its colour
			if (!e->isHit()) glows.push_back({at + glm::vec3(0, 8, 0), glm::vec3(1.f, 0.55f, 0.2f) * (e->isBig() ? 2.2f : 1.5f) * k, 16.f * (e->isBig() ? 5.f : 3.f)});
			else if (projGlow) glows.push_back({at + glm::vec3(0, 4, 0), projColor * 1.4f * k, projGlowR * 0.8f});
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
					size_t si = ((size_t)z * my + y) * mx + x;
					int smoke = si < smokeSnap.size() ? smokeSnap[si] : tl->getSmoke(), fire = si < fireSnap.size() ? fireSnap[si] : tl->getFire();
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
	glEnable(GL_DEPTH_TEST);
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
	const int W = 256, H = 20;
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

/// Corner ticks on the ground round a tile (the terrain lab's marks and shots), faintly through whatever
/// stands in front of them too.
void Board::Impl::drawTicks(const Shader &sh, const glm::mat4 &M, const Position &t, const glm::vec4 &col) const
{
	const GLboolean blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST);
	glm::vec3 at(t.x * TILE_W, t.z * TILE_H + 0.1f, t.y * TILE_W); // on the floor's top
	sh.set("uMode", 2);
	sh.set("uModel", M * glm::translate(glm::mat4(1.f), at));
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	sh.set("uTint", glm::vec4(glm::vec3(col) * 0.45f, 0.35f));
	ticks.draw();
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	sh.set("uTint", col);
	ticks.draw();
	if (blend) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
	if (!depth) glDisable(GL_DEPTH_TEST);
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

/// The terrain lab over the map: part names, red flags on marked tiles, the inspect panel of the tile
/// pointed at (or pinned).
void Board::Impl::drawLab(const Shader &sh, const glm::mat4 &M)
{
	glm::vec3 viewerVox = glm::vec3(glm::inverse(M) * glm::vec4(viewer, 1.f));
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);
	for (const TerrainLabState::LabelAt &a : lab->labels())
	{
		if (a.z > viewLevel) continue;
		const Label &l = label(a.text, (Uint8)(Palette::blockOffset(1) + 1));
		drawLabel(sh, M, glm::vec3(a.x * TILE_W + TILE_W * 0.5f, a.z * TILE_H + TILE_H + 6.f, a.y * TILE_W + TILE_W * 0.5f), viewerVox, l, 0.32f);
	}
	// marks: corner ticks on the ground round the tile (the owner found a box over it distracting) and its
	// number and reason above it
	const Uint8 redIdx = (Uint8)(Palette::blockOffset(Pathfinding::red - 1) - 1);
	const std::string page = lab->pageKey();
	for (const LabMark &m : lab->marks())
	{
		if (m.terrain != lab->terrainName() || m.page != page || m.z > viewLevel) continue;
		drawTicks(sh, M, Position(m.x, m.y, m.z), glm::vec4(2.4f, 0.35f, 0.2f, 1.f));
	}
	glDisable(GL_DEPTH_TEST);
	for (const LabMark &m : lab->marks())
	{
		if (m.terrain != lab->terrainName() || m.page != page || m.z > viewLevel) continue;
		std::string why = m.reasons.substr(0, m.reasons.find(','));
		char num[8];
		snprintf(num, sizeof(num), "#%03d ", m.id);
		const Label &l = label(num + why, redIdx);
		drawLabel(sh, M, glm::vec3(m.x * TILE_W + TILE_W * 0.5f, m.z * TILE_H + TILE_H + 13.f, m.y * TILE_W + TILE_W * 0.5f), viewerVox, l, 0.36f);
	}
	// pictures (PICTURES key): each part's sprite on a card beside it, each assembled piece and each map
	// block as the flat game draws it
	if (lab->pictures())
	{
		auto picture = [&](const std::string &key, auto make) -> Label&
		{
			Label &l = labPictures[key];
			if (!l.tex)
			{
				std::vector<uint32_t> px;
				int w = 0, h = 0;
				make(px, w, h);
				if (w > 0 && h > 0)
				{
					l.tex = std::make_unique<Texture>();
					l.tex->create(w, h, true, true);
					l.tex->update(px.data(), w, h);
					l.w = w; l.h = h;
				}
			}
			return l;
		};
		glEnable(GL_DEPTH_TEST);
		if (page == "parts")
			for (const TerrainLabState::LabelAt &a : lab->labels())
			{
				if (a.z > viewLevel) continue;
				Tile *t = battle->getTile(Position(a.x, a.y, a.z));
				const MapData *md = nullptr;
				for (int part : {O_OBJECT, O_NORTHWALL, O_WESTWALL, O_FLOOR})
					if (!md && t && t->getMapData((TilePart)part)) md = t->getMapData((TilePart)part);
				if (!md || !md->getDataset() || !md->getDataset()->getSurfaceset()) continue;
				char key[32];
				snprintf(key, sizeof(key), "p%p", (const void*)md);
				const Label &l = picture(key, [&](std::vector<uint32_t> &px, int &w, int &h)
				{
					const Surface *spr = static_cast<const SurfaceSet*>(md->getDataset()->getSurfaceset())->getFrame(md->getSprite(0));
					if (!spr) return;
					// cropped to the sprite's drawn pixels, two pixels of backing around them
					int x0 = spr->getWidth(), y0 = spr->getHeight(), x1 = -1, y1 = -1;
					for (int y = 0; y < spr->getHeight(); ++y)
						for (int x = 0; x < spr->getWidth(); ++x)
							if (spr->getPixel(x, y)) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
					if (x1 < 0) return;
					w = x1 - x0 + 5; h = y1 - y0 + 5;
					px.assign((size_t)w * h, Dash::C(16, 22, 40));
					for (int y = y0; y <= y1; ++y)
						for (int x = x0; x <= x1; ++x)
							if (Uint8 c = spr->getPixel(x, y)) px[(size_t)(y - y0 + 2) * w + x - x0 + 2] = rgba(pal[c]);
				});
				// on the empty row behind the part (north), standing on the ground, a tile wide
				// a card leaning back on the empty row behind the part, 0.6 tile wide
				if (l.w > 0) drawCard(sh, M, glm::vec3(a.x * TILE_W + TILE_W * 0.5f, a.z * TILE_H + 0.5f, a.y * TILE_W - TILE_W * 0.55f), viewerVox, l, TILE_W * 0.7f / 32.f, glm::radians(50.f));
			}
		else if (page == "assembled")
		{
			int i = 0;
			for (const TerrainLabState::Piece &pc : lab->pieces())
			{
				char key[32];
				snprintf(key, sizeof(key), "a%d", i++);
				const Label &l = picture(key, [&](std::vector<uint32_t> &px, int &w, int &h)
				{
					flatPicture(pc.x0, pc.y0, 0, pc.x0 + pc.w - 1, pc.y0 + pc.l - 1, std::min(mz - 1, pc.h), px, w, h);
				});
				// behind the piece, as wide as the piece's own footprint across the view
				float wv = (pc.w + pc.l) * TILE_W * 0.5f;
				if (l.w > 0) drawCard(sh, M, glm::vec3((pc.x0 + pc.w * 0.5f) * TILE_W, 0.f, pc.y0 * TILE_W - TILE_W * 0.6f), viewerVox, l, wv / l.w);
			}
		}
		else
		{
			// the whole block, as on the flat screen, over the table's far edge: about 45 cm wide
			const Label &l = picture("block", [&](std::vector<uint32_t> &px, int &w, int &h) { flatPicture(0, 0, 0, mx - 1, my - 1, mz - 1, px, w, h); });
			if (l.w > 0)
			{
				float k = tileSize / TILE_W;
				glm::vec3 farEdge = glm::vec3(glm::inverse(M) * glm::vec4((mapRect.x + mapRect.z) * 0.5f, surfaceY() + 0.06f, mapRect.y - 0.02f, 1.f));
				sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f)); // it stands beyond the map's area
				drawCard(sh, M, farEdge, viewerVox, l, 0.45f / (l.w * k));
				sh.set("uClip", clipRect());
			}
		}
		glDisable(GL_DEPTH_TEST);
	}
	// the inspect panel: beside and above the tile, turned to the head, about 24 cm wide whatever the zoom
	// pinned first, then a fingertip over a tile, then the laser
	bool show = pinned || fingerAge > 0.f || (hoverValid && !hoverGlobe);
	Position t = pinned ? pinTile : fingerAge > 0.f ? labFingerTile : hoverTile;
	if (show && t.x >= 0 && t.y >= 0 && t.z >= 0 && t.x < mx && t.y < my && t.z < mz)
	{
		uint64_t key = ((((uint64_t)t.z * 4096 + t.y) * 4096 + t.x) * 1024 + (lab->generation() & 1023)) * 1024 + (VR::partStyleGeneration() & 1023);
		key = key * 2 + (pinned ? 1 : 0);
		if (key != inspectKey)
		{
			inspectKey = key;
			std::vector<uint32_t> px;
			int w = 0, h = 0;
			if (inspectPicture(t, px, w, h))
			{
				if (pinned) for (int x = 0; x < w; ++x) { px[x] = 0xFF30C8FFu; px[(size_t)(h - 1) * w + x] = 0xFF30C8FFu; } // pinned: an amber edge
				if (!inspectPanel.tex) inspectPanel.tex = std::make_unique<Texture>();
				inspectPanel.tex->create(w, h, true, true);
				inspectPanel.tex->update(px.data(), w, h);
				inspectPanel.w = w;
				inspectPanel.h = h;
			}
			else inspectPanel.w = 0;
		}
		if (inspectPanel.w > 0)
		{
			float k = tileSize / TILE_W; // metres per map voxel
			glm::vec3 c(t.x * TILE_W + TILE_W * 0.5f, t.z * TILE_H + TILE_H, t.y * TILE_W + TILE_W * 0.5f);
			glm::vec3 toV = viewerVox - c;
			toV.y = 0.f;
			glm::vec3 right = glm::length(toV) > 1e-3f ? glm::normalize(glm::cross(glm::vec3(0, 1, 0), toV)) : glm::vec3(1, 0, 0);
			glm::vec3 at = c + right * (0.17f / k) + glm::vec3(0.f, 0.06f / k, 0.f);
			sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f)); // beside the tile, maybe beyond the map's area
			drawLabel(sh, M, at, viewerVox, inspectPanel, 0.24f / (inspectPanel.w * k));
		}
	}
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	sh.set("uClip", clipRect());
}

void Board::Impl::drawCard(const Shader &sh, const glm::mat4 &M, const glm::vec3 &base, const glm::vec3 &viewerVox, const Label &l, float pxSize, float lean) const
{
	if (!l.tex) return;
	glm::vec3 toV = viewerVox - base;
	toV.y = 0.f;
	if (glm::length(toV) < 1e-3f) toV = glm::vec3(0, 0, 1);
	toV = glm::normalize(toV);
	glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), toV));
	// leaning back (radians), its face turned up toward the viewer by as much
	glm::vec3 up = glm::vec3(0, std::cos(lean), 0) - toV * std::sin(lean);
	toV = glm::normalize(glm::cross(right, up));
	glm::mat4 B(1.f);
	B[0] = glm::vec4(right * (l.w * pxSize), 0.f);
	B[1] = glm::vec4(up * (l.h * pxSize), 0.f);
	B[2] = glm::vec4(toV, 0.f);
	B[3] = glm::vec4(base + up * (l.h * pxSize * 0.5f), 1.f);
	sh.set("uMode", 4);
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uModel", M * B);
	l.tex->bind(0);
	quad.draw();
}

void Board::Impl::flatPicture(int x0, int y0, int z0, int x1, int y1, int z1, std::vector<uint32_t> &px, int &W, int &H)
{
	int nx = x1 - x0 + 1, ny = y1 - y0 + 1, nz = z1 - z0 + 1;
	// the flat game's projection: a tile's sprite at ((x - y) * 16, (x + y) * 8 - z * 24), 32 x 40
	W = (nx + ny) * 16 + 8;
	H = (nx + ny) * 8 + nz * 24 + 24;
	int ox = ny * 16 - 16 + 4, oy = nz * 24 - 12;
	px.assign((size_t)W * H, Dash::C(16, 22, 40));
	for (int z = z0; z <= z1; ++z)
		for (int sum = 0; sum <= nx + ny - 2; ++sum)
			for (int dx = 0; dx < nx; ++dx)
			{
				int dy = sum - dx;
				if (dy < 0 || dy >= ny) continue;
				Tile *t = battle->getTile(Position(x0 + dx, y0 + dy, z));
				if (!t) continue;
				int sx = ox + (dx - dy) * 16, sy = oy + (dx + dy) * 8 - (z - z0) * 24;
				for (int part : {O_FLOOR, O_WESTWALL, O_NORTHWALL, O_OBJECT})
				{
					const MapData *md = t->getMapData((TilePart)part);
					if (!md || !md->getDataset() || !md->getDataset()->getSurfaceset() || t->isUfoDoorOpen((TilePart)part)) continue;
					const Surface *spr = static_cast<const SurfaceSet*>(md->getDataset()->getSurfaceset())->getFrame(md->getSprite(0));
					if (!spr) continue;
					for (int y = 0; y < spr->getHeight(); ++y)
						for (int x = 0; x < spr->getWidth(); ++x)
						{
							Uint8 c = spr->getPixel(x, y);
							int X = sx + x, Y = sy + y - md->getYOffset();
							if (c && X >= 0 && Y >= 0 && X < W && Y < H) px[(size_t)Y * W + X] = rgba(pal[c]);
						}
				}
			}
	// cropped to what was drawn, with a small margin
	const uint32_t bg = Dash::C(16, 22, 40);
	int cx0 = W, cy0 = H, cx1 = -1, cy1 = -1;
	for (int y = 0; y < H; ++y)
		for (int x = 0; x < W; ++x)
			if (px[(size_t)y * W + x] != bg) { cx0 = std::min(cx0, x); cy0 = std::min(cy0, y); cx1 = std::max(cx1, x); cy1 = std::max(cy1, y); }
	if (cx1 < 0) { W = H = 0; px.clear(); return; }
	cx0 = std::max(0, cx0 - 3); cy0 = std::max(0, cy0 - 3); cx1 = std::min(W - 1, cx1 + 3); cy1 = std::min(H - 1, cy1 + 3);
	int nw = cx1 - cx0 + 1, nh = cy1 - cy0 + 1;
	std::vector<uint32_t> out((size_t)nw * nh);
	for (int y = 0; y < nh; ++y)
		for (int x = 0; x < nw; ++x) out[(size_t)y * nw + x] = px[(size_t)(y + cy0) * W + x + cx0];
	px.swap(out);
	W = nw; H = nh;
}

// ================================================================== the lab's comparison with the art

static const int CMP_W = 48, CMP_H = 80;  // a comparison picture
static const int CMP_DX = 8, CMP_DY = 24; // where a tile's sprite (32 x 40) sits in it: room for crowns above
static const char *const PART_KIND[] = {"FLOOR", "WEST WALL", "NORTH WALL", "OBJECT"};

void Board::Impl::compareKey(const TileKey &k, bool scratchBuild, Comparison &c)
{
	Proto local;
	const Proto *pp = &local;
	if (scratchBuild)
	{
		if (scratch.px.empty() || scratch.full || scratch.nextY > ATLAS / 2) scratch.reset();
		build = &scratch;
		buildTile(k, local);
		build = &atlas;
	}
	else pp = &tileProto(k);
	const Atlas &at = scratchBuild ? scratch : atlas;
	c.grew = pp->grew;
	c.stray = pp->stray;
	c.model = pp->model;
	c.card = pp->card;
	c.empty = pp->idx.empty();
	// the art as the game draws the tile: floor, walls, object, each where the table lines it up
	c.art.assign((size_t)CMP_W * CMP_H, 0u);
	for (int part : {O_FLOOR, O_WESTWALL, O_NORTHWALL, O_OBJECT})
	{
		const MapData *md = k.m[part];
		if (!md || !md->getDataset() || !md->getDataset()->getSurfaceset()) continue;
		const Surface *spr = static_cast<const SurfaceSet*>(md->getDataset()->getSurfaceset())->getFrame(md->getSprite(0));
		if (!spr) continue;
		for (int y = 0; y < spr->getHeight(); ++y)
			for (int x = 0; x < spr->getWidth(); ++x)
			{
				Uint8 v = spr->getPixel(x, y);
				int X = x + CMP_DX, Y = y - pp->yoffs[part] + CMP_DY;
				if (v && X >= 0 && Y >= 0 && X < CMP_W && Y < CMP_H) c.art[(size_t)Y * CMP_W + X] = rgba(pal[v]);
			}
	}
	VR::Compare::Image img;
	VR::Compare::render(pp->verts, pp->idx, at.px.data(), ATLAS, ATLAS, VR::Compare::isoView((float)CMP_DX, (float)CMP_DY), CMP_W, CMP_H, false, img);
	c.score = VR::Compare::diff(img, c.art, Dash::BG, c.diff);
	c.table.assign((size_t)CMP_W * CMP_H, Dash::BG);
	for (size_t i = 0; i < c.table.size(); ++i) if (img.cover[i] >= 128) c.table[i] = img.px[i];
	// from behind (the north-west, the sides the art never shows) and low
	VR::Compare::render(pp->verts, pp->idx, at.px.data(), ATLAS, ATLAS, VR::Compare::lookView(225.f, 20.f, 1.8f, CMP_W * 0.5f, CMP_H - 12.f), CMP_W, CMP_H, true, img);
	c.look.assign((size_t)CMP_W * CMP_H, Dash::BG);
	for (size_t i = 0; i < c.look.size(); ++i) if (img.cover[i] >= 128) c.look[i] = img.px[i];
	// from above (the ground under a crown, which the game's angle never shows)
	VR::Compare::render(pp->verts, pp->idx, at.px.data(), ATLAS, ATLAS, VR::Compare::lookView(45.f, 78.f, 1.7f, CMP_W * 0.5f, CMP_H * 0.5f + 6.f), CMP_W, CMP_H, true, img);
	c.above.assign((size_t)CMP_W * CMP_H, Dash::BG);
	for (size_t i = 0; i < c.above.size(); ++i) if (img.cover[i] >= 128) c.above[i] = img.px[i];
}

/// The colour a score is written in: green from 85, amber from 70, red under.
static uint32_t scoreColor(int shape) { return shape >= 85 ? Dash::GREEN : shape >= 70 ? Dash::AMBER : Dash::RED; }
/// The lower of a comparison's shape and colour scores (what the sheets sort and colour by).
static int worstScore(const VR::Compare::Score &s) { return std::min(s.shape(), s.colour()); }

/// What the table did with a part, for the sheets and the comparison picture.
static std::string didText(const Board::Impl::Comparison &c, int part, const MapData *md)
{
	if (c.model & (1 << part)) return "hand-built";
	if (c.card) return "no voxels: cards";
	if (part == O_FLOOR && VR::reliefSpec(md)) return VR::reliefMode() == VR::RELIEF_FLAT ? "relief (off)" : "relief";
	if (c.grew & (1 << part)) return "crown added";
	if (c.stray & (1 << part)) return "faces dropped";
	return "";
}

/// The five pictures side by side at x, y with gap g: art, table, difference, behind, above.
static const int CMP_PICS = 5;
static void pasteFour(Dash::Canvas &cv, const Board::Impl::Comparison &c, int x, int y, int g)
{
	const std::vector<uint32_t> *pics[CMP_PICS] = {&c.art, &c.table, &c.diff, &c.look, &c.above};
	for (int k = 0; k < CMP_PICS; ++k)
	{
		int x0 = x + k * (CMP_W + g);
		cv.rect(x0, y, CMP_W, CMP_H, Dash::BG);
		for (int j = 0; j < CMP_H; ++j)
			for (int i = 0; i < CMP_W; ++i)
			{
				uint32_t v = (*pics[k])[(size_t)j * CMP_W + i];
				if ((v >> 24) < 128) continue;
				int X = x0 + i, Y = y + j;
				if (X < cv.W && Y < cv.H) cv.px[(size_t)Y * cv.W + X] = v;
			}
		cv.frame(x0 - 1, y - 1, CMP_W + 2, CMP_H + 2, Dash::LINE);
	}
}

static std::string scoreText(const VR::Compare::Score &s)
{
	std::ostringstream o;
	o << "SHAPE " << s.shape() << "%  COLOUR " << s.colour() << "%  +" << s.tablePct() << "% TABLE ONLY  -" << s.artPct() << "% ART ONLY";
	return o.str();
}

bool Board::Impl::comparePicture(const Position &pos, std::vector<uint32_t> &px, int &W, int &H)
{
	if (!battle || !game || !voxels) return false;
	Tile *t = battle->getTile(pos);
	if (!t) return false;
	Comparison c;
	compareKey(tileKey(t, pos.x, pos.y, pos.z), false, c);
	std::ostringstream title;
	title << (lab ? lab->terrainName() + "  " + lab->pageKey() + "  " : std::string()) << "tile " << pos.x << "," << pos.y << "," << pos.z;
	return comparisonPicture(c, title.str(), px, W, H);
}

bool Board::Impl::partPicture(const MapDataSet *ds, int id, std::vector<uint32_t> &px, int &W, int &H)
{
	if (!ds || !game || !voxels || id < 0 || id >= (int)ds->getSize()) return false;
	const MapData *md = const_cast<MapDataSet*>(ds)->getObject(id);
	int kind = md ? (int)md->getObjectType() : -1;
	if (kind < 0 || kind >= O_MAX) return false;
	TileKey k;
	k.m[kind] = md;
	Comparison c;
	compareKey(k, true, c);
	return comparisonPicture(c, ds->getName() + " " + std::to_string(id) + "  " + PART_KIND[kind], px, W, H);
}

bool Board::Impl::comparisonPicture(const Comparison &c, const std::string &titleText, std::vector<uint32_t> &px, int &W, int &H)
{
	const int g = 6;
	W = CMP_PICS * CMP_W + (CMP_PICS - 1) * g + 12;
	H = 20 + CMP_H + 43;
	Dash::Canvas cv(game, px, W, H);
	if (!cv.ok()) return false;
	cv.text(6, 4, W - 12, titleText, Dash::GOLD);
	pasteFour(cv, c, 6, 16, g);
	static const char *const HEAD[] = {"ART", "TABLE", "DIFF", "BEHIND", "ABOVE"};
	for (int k = 0; k < CMP_PICS; ++k) cv.text(6 + k * (CMP_W + g), 17 + CMP_H, CMP_W, HEAD[k], Dash::DIM, false, 1);
	cv.text(6, 28 + CMP_H, W - 12, scoreText(c.score), scoreColor(worstScore(c.score)));
	cv.text(6, 38 + CMP_H, W - 12, "RED: TABLE ONLY  BLUE: ART ONLY", Dash::DIM);
	cv.text(6, 47 + CMP_H, W - 12, "YELLOW: OTHER COLOUR  GREY: SAME", Dash::DIM);
	return true;
}

bool Board::Impl::partSheet(const MapDataSet *ds, std::vector<uint32_t> &px, int &W, int &H, std::string &report)
{
	if (!ds || !game || !voxels) return false;
	struct Cell { int id, kind; const MapData *md; Comparison c; };
	std::vector<Cell> cells;
	std::vector<std::string> skipped;
	for (size_t i = 0; i < ds->getSize(); ++i)
	{
		const MapData *md = const_cast<MapDataSet*>(ds)->getObject(i);
		if (!md) continue;
		int kind = (int)md->getObjectType();
		if (kind < 0 || kind >= O_MAX) { skipped.push_back(std::to_string(i) + " (type " + std::to_string(kind) + ")"); continue; }
		Cell cell{(int)i, kind, md, {}};
		TileKey k;
		k.m[kind] = md;
		compareKey(k, true, cell.c);
		if (cell.c.empty && cell.c.score.art == 0) continue; // nothing drawn, nothing to draw
		cells.push_back(std::move(cell));
	}
	const int g = 2, pad = 4, cols = 4;
	const int cellW = CMP_PICS * CMP_W + (CMP_PICS - 1) * g + 2 * pad + 4, cellH = 11 + CMP_H + 24;
	const int head = 46;
	int rows = ((int)cells.size() + cols - 1) / cols;
	W = cols * cellW + 8;
	H = head + std::max(1, rows) * cellH + 8;
	Dash::Canvas cv(game, px, W, H);
	if (!cv.ok()) return false;
	int sum = 0, low = 0;
	int sumC = 0;
	for (const Cell &cl : cells) { sum += cl.c.score.shape(); sumC += cl.c.score.colour(); if (worstScore(cl.c.score) < 70) ++low; }
	std::ostringstream t1, t2;
	t1 << ds->getName() << "  " << cells.size() << " parts  (" << OXCE_VR_PROTOTYPE_VERSION << ", relief " << (VR::reliefMode() == VR::RELIEF_STEPS ? "steps" : VR::reliefMode() == VR::RELIEF_SMOOTH ? "smooth" : "flat") << ")";
	t2 << "average shape " << (cells.empty() ? 0 : sum / (int)cells.size()) << "%, colour " << (cells.empty() ? 0 : sumC / (int)cells.size()) << "%,  " << low << " under 70%"
		<< (skipped.empty() ? std::string() : ",  left out (not a floor, wall or object):");
	for (const std::string &s : skipped) t2 << " " << s;
	cv.text(6, 4, W - 12, t1.str(), Dash::GOLD, true);
	cv.text(6, 22, W - 12, t2.str(), Dash::TEXT);
	cv.text(6, 33, W - 12, "ART | TABLE FROM THE GAME'S ANGLE | DIFF: RED TABLE ONLY, BLUE ART ONLY, YELLOW OTHER COLOUR, GREY SAME (DIM EDGES NOT COUNTED) | TABLE FROM BEHIND, LOW | FROM ABOVE", Dash::DIM);
	std::vector<std::pair<int, std::string>> lines;
	for (size_t n = 0; n < cells.size(); ++n)
	{
		const Cell &cl = cells[n];
		int x = 4 + (int)(n % cols) * cellW, y = head + (int)(n / cols) * cellH;
		cv.rect(x, y, cellW - 4, cellH - 4, Dash::C(14, 22, 46));
		std::string did = didText(cl.c, cl.kind, cl.md);
		cv.text(x + pad, y + 2, cellW - 2 * pad, ds->getName() + " " + std::to_string(cl.id) + "  " + PART_KIND[cl.kind], Dash::GOLD);
		pasteFour(cv, cl.c, x + pad, y + 12, g);
		const VR::Compare::Score &s = cl.c.score;
		std::ostringstream sc;
		sc << "SHAPE " << s.shape() << "%  COLOUR " << s.colour() << "%  +" << s.tablePct() << "% -" << s.artPct() << "%";
		cv.text(x + pad, y + 14 + CMP_H, 170, sc.str(), scoreColor(worstScore(s)));
		if (!did.empty()) cv.text(x + pad + 170, y + 14 + CMP_H, cellW - 2 * pad - 170, did, Dash::CYAN, false, 2);
		std::ostringstream ln;
		ln << ds->getName() << " " << cl.id << "\t" << PART_KIND[cl.kind] << "\tshape " << s.shape() << "%\tcolour " << s.colour() << "%\ttable only +" << s.tablePct()
			<< "%\tart only -" << s.artPct() << "%" << (did.empty() ? "" : "\t" + did);
		lines.push_back({worstScore(s), ln.str()});
	}
	std::stable_sort(lines.begin(), lines.end(), [](const std::pair<int, std::string> &a, const std::pair<int, std::string> &b) { return a.first < b.first; });
	std::ostringstream rep;
	rep << "== " << t1.str() << "\n" << t2.str() << "\n";
	for (auto &l : lines) rep << l.second << "\n";
	report = rep.str();
	return true;
}

bool Board::Impl::inspectPicture(const Position &pos, std::vector<uint32_t> &px, int &W, int &H)
{
	if (!battle || !game) return false;
	Tile *t = battle->getTile(pos);
	if (!t) return false;
	W = 320; H = 300; // the comparison strip below (0.21.0-terrain.8)
	Dash::Canvas cv(game, px, W, H);
	if (!cv.ok()) return false;
	static const char *const KIND[] = {"FLOOR", "WEST WALL", "NORTH WALL", "OBJECT"};
	auto spriteOf = [](const MapData *md) -> const Surface*
	{
		if (!md || !md->getDataset() || !md->getDataset()->getSurfaceset()) return nullptr;
		return static_cast<const SurfaceSet*>(md->getDataset()->getSurfaceset())->getFrame(md->getSprite(0));
	};
	auto blit = [&](const Surface *spr, int dx, int dy, float sc, int yoff)
	{
		if (!spr) return;
		int w = (int)(spr->getWidth() * sc), h = (int)(spr->getHeight() * sc);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				int sx = (int)(x / sc), sy = (int)(y / sc) - yoff;
				if (sy < 0 || sy >= spr->getHeight()) continue;
				Uint8 c = spr->getPixel(sx, sy);
				int X = dx + x, Y = dy + y;
				if (c && X >= 0 && Y >= 0 && X < W && Y < H) px[(size_t)Y * W + X] = rgba(pal[c]);
			}
	};
	std::ostringstream title;
	title << lab->terrainName() << "  " << lab->pageKey() << "  tile " << pos.x << "," << pos.y << "," << pos.z;
	cv.title(title.str());
	// as the game draws the tile: floor, walls, object, each lifted by its offset
	cv.text(6, 24, 110, "AS THE GAME DRAWS IT", Dash::DIM);
	cv.rect(6, 34, 98, 122, Dash::C(22, 30, 52));
	for (int part = O_FLOOR; part < O_MAX; ++part)
		if (const MapData *md = t->getMapData((TilePart)part)) blit(spriteOf(md), 7, 35, 3.f, -md->getYOffset());
	// each part on its own, with what the table did with it
	TileKey key = tileKey(t, pos.x, pos.y, pos.z);
	const Proto &tp = tileProto(key);
	int cell = 0;
	for (int part = O_FLOOR; part < O_MAX; ++part)
	{
		const MapData *md = t->getMapData((TilePart)part);
		if (!md) continue;
		int cx = 120 + (cell % 2) * 100, cy = 24 + (cell / 2) * 86;
		++cell;
		cv.rect(cx, cy, 52, 62, Dash::C(22, 30, 52));
		blit(spriteOf(md), cx + 2, cy + 1, 1.5f, -md->getYOffset());
		int id = -1, set = -1;
		t->getMapData(&id, &set, (TilePart)part);
		std::string name = std::string(KIND[part]) + " " + (md->getDataset() ? md->getDataset()->getName() : std::string("?")) + " " + std::to_string(id);
		cv.text(cx, cy + 63, 98, name, Dash::GOLD);
		std::string did;
		if (const VR::ReliefSpec *rs = part == O_FLOOR ? VR::reliefSpec(md) : nullptr)
			did = std::string("relief ") + (rs->sign > 0 ? "up " : "down ") + std::to_string((int)rs->height) + (VR::reliefMode() == VR::RELIEF_FLAT ? " (off)" : "");
		else if (tp.model & (1 << part)) did = "hand-built model";
		else if (tp.grew & (1 << part)) did = "crown added";
		else if (tp.stray & (1 << part)) did = "art mismatch: faces dropped";
		else if (part == O_FLOOR) did = "flat";
		if (!did.empty()) cv.text(cx, cy + 72, 98, did, Dash::CYAN);
	}
	if (!cell) cv.text(120, 40, 190, "nothing on this tile", Dash::DIM);
	// the table's shape against the art, from the game's angle (the whole tile; VrCompare.h)
	if (cell && voxels)
	{
		Comparison c;
		compareKey(key, false, c);
		std::ostringstream m;
		m << "SHAPE " << c.score.shape() << "%  COLOUR " << c.score.colour() << "%";
		cv.text(6, 160, 108, m.str(), scoreColor(worstScore(c.score)));
		// the comparison: art, table from the game's angle, difference, behind, above
		const int g = 6, sx = (W - (CMP_PICS * CMP_W + (CMP_PICS - 1) * g)) / 2, sy = 196;
		pasteFour(cv, c, sx, sy, g);
		static const char *const HEAD[] = {"ART", "TABLE", "DIFF", "BEHIND", "ABOVE"};
		for (int k = 0; k < CMP_PICS; ++k) cv.text(sx + k * (CMP_W + g), sy + CMP_H + 1, CMP_W, HEAD[k], Dash::DIM, false, 1);
	}
	// marked?
	for (const LabMark &m : lab->marks())
		if (m.terrain == lab->terrainName() && m.page == lab->pageKey() && m.x == pos.x && m.y == pos.y && m.z == pos.z)
		{
			char num[8];
			snprintf(num, sizeof(num), "#%03d", m.id);
			cv.text(6, 170, 108, std::string("MARKED ") + num, Dash::RED);
			cv.text(6, 179, 108, m.reasons, Dash::RED);
			break;
		}
	cv.text(6, 288, 308, pinned ? "PINNED (TOUCH IT OR TRIGGER AGAIN TO RELEASE)   A: MARK / UNMARK" : "TOUCH THE TILE OR TRIGGER: PIN   A: MARK / UNMARK", Dash::DIM);
	return true;
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
	{
		// corner ticks around a tile on the ground (the terrain lab's shots): an L at each corner, just
		// outside the tile, so nothing crosses what stands on it
		MeshData tk;
		const float o = 1.2f, len = 4.f, th = 0.9f, hgt = 0.8f, L = (float)TILE_W;
		for (int cx = 0; cx < 2; ++cx)
			for (int cz = 0; cz < 2; ++cz)
			{
				float x = cx ? L + o : -o, z = cz ? L + o : -o, sx = cx ? -1.f : 1.f, sz = cz ? -1.f : 1.f;
				// the arm along x and the arm along z, both starting at the corner point
				float xa = x - sx * th, xb = x + sx * len, za = z - sz * th, zb = z;
				tk.addBox({std::min(xa, xb), 0.f, std::min(za, zb)}, {std::max(xa, xb), hgt, std::max(za, zb)}, glm::vec4(1), 0);
				xa = x - sx * th; xb = x; za = z - sz * th; zb = z + sz * len;
				tk.addBox({std::min(xa, xb), 0.f, std::min(za, zb)}, {std::max(xa, xb), hgt, std::max(za, zb)}, glm::vec4(1), 0);
			}
		_p->ticks.upload(tk);
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
void Board::setListener(const glm::vec3 &forward) { _p->listenerFwd = forward; }
void Board::setMapRect(const glm::vec4 &rect) { _p->mapRect = rect; _p->baseView.init(rect, _p->surfaceY()); }
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
		bool covered = false;
		Position up = u->getPosition();
		int size = u->getArmor()->getSize();
		if (up.z + 1 <= std::min(p.viewLevel, p.mz - 1))
			for (int dx = 0; dx < size && !covered; ++dx)
				for (int dy = 0; dy < size && !covered; ++dy)
				{
					Tile *t = p.battle->getTile(Position(up.x + dx, up.y + dy, up.z + 1));
					covered = t && t->isDiscovered(O_FLOOR) && t->getMapData(O_FLOOR);
				}
		out.push_back({u, w, std::max(0.016f, 6.f * k), u->getFaction() == p.battle->getSide(), covered});
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
	// fog: the finger reaches the void on the ground, which picks the tile at the highest fogged level (as
	// on the flat map, clicking into the dark picks the tile at the level being viewed). An explored floor
	// above it (the roof of a building not yet entered) comes first, as with the laser (raycast).
	const int f = p.fogTop(x, y);
	for (int z = std::min(p.viewLevel, p.mz - 1); z >= 0; --z)
	{
		if (z < f)
		{
			tx = x; ty = y; tz = f - 1;
			floorY = glm::vec3(M * glm::vec4(l.x, 0.5f, l.z, 1.f)).y;
			return true;
		}
		Tile *t = p.battle->getTile(Position(x, y, z));
		if (!t) continue;
		// (a unit stands there: on top of something on the level below, which the game allows)
		bool floor = z == 0 || (t->isDiscovered(O_FLOOR) && (t->getMapData(O_FLOOR) || t->getMapData(O_OBJECT) || t->getUnit()));
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

bool Board::inspectPicture(int x, int y, int z, std::vector<uint32_t> &px, int &w, int &h)
{
	Impl &p = *_p;
	if (!p.hasBattle || !p.lab) return false;
	return p.inspectPicture(Position(x, y, z), px, w, h);
}

bool Board::comparePicture(int x, int y, int z, std::vector<uint32_t> &px, int &w, int &h)
{
	Impl &p = *_p;
	if (!p.hasBattle) return false;
	return p.comparePicture(Position(x, y, z), px, w, h);
}

bool Board::partPicture(const MapDataSet *ds, int id, std::vector<uint32_t> &px, int &w, int &h) { return _p->partPicture(ds, id, px, w, h); }

bool Board::partSheet(const MapDataSet *ds, std::vector<uint32_t> &px, int &w, int &h, std::string &report)
{
	return _p->partSheet(ds, px, w, h, report);
}

const void *Board::palette() const { return _p->pal; }

bool Board::labActive() const { return _p->lab && _p->hasBattle; }

void Board::labFinger(int x, int y, int z)
{
	Impl &p = *_p;
	if (!p.lab || !p.hasBattle) return;
	p.labFingerTile = Position(x, y, z);
	p.fingerAge = 0.25f;
}

void Board::labTouch(int x, int y, int z)
{
	Impl &p = *_p;
	if (!p.lab || !p.hasBattle) return;
	Position t(x, y, z);
	if (p.pinned && p.pinTile == t) p.pinned = false;
	else { p.pinned = true; p.pinTile = t; }
}

bool Board::labFingerMark()
{
	Impl &p = *_p;
	revalidate(p);
	if (!p.lab || !p.hasBattle || p.fingerAge <= 0.f) return false;
	p.lab->tileAction(p.labFingerTile.x, p.labFingerTile.y, p.labFingerTile.z);
	return true;
}

bool Board::mapFootprint(glm::mat4 &worldToMap, glm::vec2 &size) const
{
	const Impl &p = *_p;
	if (!p.hasBattle || p.hasBase || !p.battle) return false;
	worldToMap = glm::inverse(p.boardMatrix());
	size = glm::vec2(p.mx * TILE_W, p.my * TILE_W);
	return true;
}

std::string Board::debugInfo() const
{
	const Impl &p = *_p;
	std::ostringstream ss;
	ss << "focus " << p.focus.x << "," << p.focus.z << " target " << p.focusTarget.x << "," << p.focusTarget.z << " tileSize " << p.tileSize << " yaw " << p.yaw;
	// atlas use and parts that came out with no shape at all (atlas full, blank sprite)
	{
		int empty = 0, cardsN = 0;
		std::map<std::string, int> names;
		for (auto &kv : p.tileProtos)
		{
			if (kv.second.card) ++cardsN;
			if (!kv.second.verts.empty()) continue;
			++empty;
			for (const MapData *md : kv.first.m)
				if (md && md->getDataset() && names.size() < 40) names[md->getDataset()->getName()]++;
		}
		ss << " atlas " << p.atlas.nextY * 100 / ATLAS << "% of rows, " << p.atlas.usedPercent() << "% of texels" << (p.atlas.full ? " FULL" : "") << " tiles " << p.tileProtos.size() << " cards " << cardsN << " empty " << empty;
		for (auto &kv : names) ss << " " << kv.first << ":" << kv.second;
		size_t tris = 0;
		for (const Chunk &c : p.chunks) tris += c.tris;
		ss << " terrain triangles " << tris;
		// the built shapes' geometry (positions, normals, materials), summed over tiles so the order doesn't
		// matter: equal hashes = the same faces lit the same way (0.21.0-terrain.22: checks that models without
		// panel data build exactly as before); without the Skyranger's tiles as a second number
		uint64_t hAll = 0, hNoPlane = 0;
		for (auto &kv : p.tileProtos)
		{
			uint64_t h = 1469598103934665603ull;
			auto mix = [&](float f) { uint32_t u; std::memcpy(&u, &f, 4); h = (h ^ u) * 1099511628211ull; };
			for (const Vertex &v : kv.second.verts)
			{
				mix(v.pos.x); mix(v.pos.y); mix(v.pos.z); mix(v.normal.x); mix(v.normal.y); mix(v.normal.z); mix(v.material);
			}
			for (uint32_t i : kv.second.idx) h = (h ^ i) * 1099511628211ull;
			bool plane = false;
			for (const MapData *md : kv.first.m)
				if (md && md->getDataset() && md->getDataset()->getName() == "PLANE") plane = true;
			hAll += h;
			if (!plane) hNoPlane += h;
		}
		ss << " mesh hash " << std::hex << hAll << " without PLANE " << hNoPlane << std::dec;
	}
	if (p.alienView || p.winOn) ss << " alienTurn " << p.alienView << " hidden " << p.hidden << " reveal " << p.reveal << " win " << p.win.x << "," << p.win.y << "," << p.win.z << "," << p.win.w;
	if (p.battle && p.battle->getSelectedUnit())
	{
		BattleUnit *u = p.battle->getSelectedUnit();
		auto it = p.cards.find(u);
		glm::vec3 v = (it != p.cards.end() && it->second.visInit) ? it->second.vis : glm::vec3(-1.f);
		ss << " selected " << u->getId() << " at " << u->getPosition().x << "," << u->getPosition().y << "," << u->getPosition().z;
		ss << " unit " << v.x << "," << v.z << " inView30 " << p.inView(v, 0.3f) << " inView60 " << p.inView(v, 0.6f);
		// where the unit sits on the table (metres from the map's middle, +z toward the player)
		float k = p.tileSize / TILE_W;
		glm::vec3 ow = glm::vec3(glm::rotate(glm::mat4(1.f), p.yaw, {0, 1, 0}) * glm::vec4((v.x - p.focus.x) * k, 0.f, (v.z - p.focus.z) * k, 0.f));
		ss << " onTable " << ow.x << "," << ow.z << " halfDepth " << (p.mapRect.w - p.mapRect.y) * 0.5f << " inShot " << p.inShot << " level " << p.viewLevel;
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
	p.tapSelect = 0.5f; // a click on a soldier's tile can select it: that is a tap too
	if (p.bs) p.bs->vrTileClick(Position(tx, ty, tz), right);
}

bool Board::selectUnit(BattleUnit *unit)
{
	Impl &p = *_p;
	revalidate(p);
	p.tapSelect = 0.5f; // the view stays where it is: the tapped soldier is within reach
	return p.bs ? p.bs->vrSelectUnit(unit) : false;
}
bool Board::hasContent() const { return _p->hasBattle || _p->hasGlobe || _p->hasBase; }

void Board::update(Game *game, float dt)
{
	Impl &p = *_p;
	p.game = game;
	p.time += dt;
	p.fingerAge = std::max(0.f, p.fingerAge - dt);
	SavedGame *sg = game->getSavedGame();
	SavedBattleGame *sb = sg ? sg->getSavedBattle() : nullptr;
	// the battlescape screen must really be alive (the saved battle keeps a stale pointer after it closes)
	BattlescapeState *live = nullptr;
	if (sb)
		for (State *s : game->getStates())
			if (auto *b = dynamic_cast<BattlescapeState*>(s)) { live = b; break; }
	bool battleReady = sb && live && live == sb->getBattleState() && sb->getMapSizeX() > 0;
	p.bs = battleReady ? live : nullptr;
	// the terrain lab (developer tool, VrLab.h): its own map, no battlescape screen behind it
	TerrainLabState *lab = nullptr;
	for (State *s : game->getStates())
		if (auto *l = dynamic_cast<TerrainLabState*>(s)) { lab = l; break; }
	if (lab && lab->battle() && lab->battle()->getMapSizeX() > 0)
	{
		SavedBattleGame *lb = lab->battle();
		p.bs = nullptr;
		bool fresh = lb != p.battle || lab->generation() != p.labGen;
		int shade = lab->takeShade();
		if (shade >= 0) { lb->setGlobalShade(shade); lb->getTileEngine()->calculateLighting(LL_AMBIENT); }
		if (fresh || shade >= 0 || VR::partStyleGeneration() != p.styleGen)
		{
			p.bindBattle(lb);
			p.labGen = lab->generation();
			p.styleGen = VR::partStyleGeneration();
		}
		p.hasBattle = true;
		p.lab = lab;
		if (fresh)
		{
			p.labLevel = p.mz - 1;
			p.pinned = false;
			p.inspectKey = ~0ull;
			// the whole map in view, from its middle
			float w = p.mapRect.z - p.mapRect.x, d = p.mapRect.w - p.mapRect.y;
			float fit = std::min(w / std::max(1, p.mx), d / std::max(1, p.my)) * 0.95f;
			if (fit > 0.f) p.tileSize = glm::clamp(fit, 0.02f, 0.14f);
			p.focus = p.focusTarget = glm::vec3(p.mx * TILE_W * 0.5f, 0.f, p.my * TILE_W * 0.5f);
			p.focusInit = true;
		}
		else p.focusTarget = p.focus; // no game camera to follow: the table stays where it was slid
		lab->updateLamps(p.hoverTile.x, p.hoverTile.y, p.hoverTile.z, p.hoverValid && !p.hoverGlobe);
		float turn = lab->takeSunTurn();
		if (turn != 0.f) rotateSun(turn);
		std::string w = lab->takeWeather();
		if (!w.empty()) forceWeather(w);
		p.updateBattle(dt);
		p.hasBase = false;
		p.hasGlobe = false;
		p.geo = nullptr;
		p.globe = nullptr;
		return;
	}
	p.lab = nullptr;
	if (battleReady && VR::partStyleGeneration() != p.styleGen) { p.styleGen = VR::partStyleGeneration(); p.firstTile = nullptr; }
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

	// the base screen: the base replaces the globe on the table
	p.hasBase = !p.hasBattle && sg && p.baseView.update(game, dt);

	// geoscape: find the GeoscapeState on the stack
	p.geo = nullptr;
	p.globe = nullptr;
	if (!p.hasBattle && !p.hasBase && sg)
	{
		for (State *s : game->getStates())
			if (auto *g = dynamic_cast<GeoscapeState*>(s)) { p.geo = g; p.globe = g->getGlobe(); break; }
	}
	p.hasGlobe = p.globe != nullptr;
	if (p.hasGlobe)
	{
		if (!p.globeBuilt) p.buildGlobe();
		p.globeCenter = glm::vec3(p.layout.tableCenter.x, p.surfaceY() + Impl::DOME_TOP - p.globeRadius, p.layout.tableCenter.z);
		p.globe->getCenter(&p.cenLon, &p.cenLat);
	}
}

void Board::setFeedPass(bool on) { _p->feedPass = on; }
void Board::setCapturePass(bool on, int x, int y, int z) { _p->capturePass = on; _p->captureTile = Position(x, y, z); }
bool Board::tileWorld(int x, int y, int z, glm::vec3 &centre, float &tileMetres) const
{
	const Impl &p = *_p;
	if (!p.hasBattle || x < 0 || y < 0 || z < 0 || x >= p.mx || y >= p.my || z >= p.mz) return false;
	glm::vec3 v(x * TILE_W + TILE_W * 0.5f, z * TILE_H + TILE_H * 0.4f, y * TILE_W + TILE_W * 0.5f);
	centre = glm::vec3(p.boardMatrix() * glm::vec4(v, 1.f));
	tileMetres = p.tileSize;
	return true;
}
void Board::setHiddenUnit(BattleUnit *unit) { _p->hiddenUnit = unit; }
glm::vec3 Board::voxelToWorld(const glm::vec3 &v) const { return glm::vec3(_p->boardMatrix() * glm::vec4(v.x, v.z, v.y, 1.f)); }

void Board::setShadowPass(bool on, const glm::vec3 &light)
{
	// standees turn toward whoever looks at them: in the shadow pass that is the light, so they
	// cast their full silhouette
	static glm::vec3 saved;
	if (on && !_p->shadowPass) { saved = _p->viewer; _p->viewer = light; _p->shadowLight = light; _p->hasShadowLight = true; }
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

/// How high a tile part reaches (map voxels from its tile's floor): rain lands on it.
float Board::Impl::partTop(const MapData *d)
{
	auto it = partTops.find(d);
	if (it != partTops.end()) return it->second;
	float top = partInfo(d).top;
	partTops[d] = top;
	return top;
}

/// Where rain and snow stop in each column: the highest floor (a roof is the floor of the level above)
/// or object there; over unexplored ground the highest explored floor, else the void on the ground.
/// Walls don't stop it.
void Board::Impl::updateStops()
{
	if (!battle || stopCache.size() != (size_t)mx * my) return;
	for (int y = 0; y < my; ++y)
		for (int x = 0; x < mx; ++x)
		{
			size_t i = (size_t)y * mx + x;
			int nf = fogTop(x, y);
			if (nf)
			{
				// over the void: the highest explored floor above the fog (a roof seen from outside), else
				// the ground; never anything not yet seen
				float h = 0.5f;
				for (int z = mz - 1; z >= nf; --z)
				{
					Tile *t = battle->getTile(Position(x, y, z));
					if (!t || !t->isDiscovered(O_FLOOR)) continue;
					const MapData *fl = t->getMapData(O_FLOOR), *ob = t->getMapData(O_OBJECT);
					if (!fl && !ob) continue;
					h = z * (float)TILE_H + std::max(fl ? std::max(partTop(fl), 0.5f) : 0.f, ob ? partTop(ob) : 0.f);
					break;
				}
				stopCache[i] = h;
				openCache[i] = 0;
				continue;
			}
			float h = 0.f;
			bool roof = false;
			for (int z = mz - 1; z >= 0; --z)
			{
				Tile *t = battle->getTile(Position(x, y, z));
				if (!t) continue;
				const MapData *fl = t->getMapData(O_FLOOR), *ob = t->getMapData(O_OBJECT);
				if (!fl && !ob) continue;
				float base = z * (float)TILE_H;
				h = base + std::max(fl ? std::max(partTop(fl), 0.5f) : 0.f, ob ? partTop(ob) : 0.f);
				roof = z > 0;
				break;
			}
			stopCache[i] = h;
			openCache[i] = roof ? 0 : 1;
		}
	float top = (std::min(viewLevel, mz - 1) + 1) * (float)TILE_H;
	stopMax = 0.f;
	for (float h : stopCache) if (h < top) stopMax = std::max(stopMax, h);
}

bool Board::sky(BoardSky &out) const
{
	const Impl &p = *_p;
	out = BoardSky();
	if (!p.hasBattle || p.hasBase || !p.battle) return false;
	const SkyLight &s = p.weather.sky();
	if (!s.on) return false;
	glm::mat4 M = p.boardMatrix();
	out.on = true;
	out.dir = glm::normalize(glm::vec3(M * glm::vec4(s.dir, 0.f)));
	out.col = s.col;
	out.sky = s.sky;
	out.cloud = glm::vec4(s.cloud, 1.f / 70.f, s.cloudOffset.x, s.cloudOffset.y); // clouds 3-6 tiles across
	out.cloud2 = glm::vec4(s.wind.x, s.wind.y, 4.f, 1.5f); // quarter-tile voxels, stretched 1.5 : 1 along the wind
	out.mapInv = glm::inverse(M);
	out.area = p.clipRect();
	out.flash = s.flash;
	out.mapShade = shadeFactor(p.globalShade());
	out.moonFloor = s.moonFloor;
	out.wet = s.wet;
	out.boost = s.boost;
	return true;
}

void Board::setHandPoints(const std::vector<glm::vec3> &pts) { _p->handPts = pts; }

std::string Board::weatherStatus() const
{
	if (!_p->hasBattle) return "sky=-";
	return _p->weather.status() + (_p->skyMarkText.empty() ? "" : " mark=" + _p->skyMarkText);
}

void Board::forceWeather(const std::string &kind)
{
	Weather::force(kind);
	if (_p->hasBattle && _p->battle) _p->weather.begin(_p->game, _p->battle, _p->shadeOffset);
}

void Board::rotateSun(float degrees)
{
	Impl &p = *_p;
	float az = Weather::forcedSun() >= 0.f ? Weather::forcedSun() : p.weather.info().sunAz;
	Weather::forceSun(std::fmod(az + degrees + 720.f, 360.f));
	if (p.hasBattle && p.battle) p.weather.begin(p.game, p.battle, p.shadeOffset);
}

void Board::realSun()
{
	Weather::forceSun(-1.f);
	if (_p->hasBattle && _p->battle) _p->weather.begin(_p->game, _p->battle, _p->shadeOffset);
}

void Board::setShadeOffset(int offset)
{
	Impl &p = *_p;
	if (offset == p.shadeOffset) return;
	p.shadeOffset = offset;
	// the tiles' colours change: their chunks rebuild by their hash; the sky is worked out again
	if (p.hasBattle && p.battle) p.weather.begin(p.game, p.battle, p.shadeOffset);
}

int Board::shadeOffset() const { return _p->shadeOffset; }

void Board::setSkyMarker(bool on) { _p->skyMarkerOn = on; if (!on) _p->skyMarkText.clear(); }
bool Board::skyMarker() const { return _p->skyMarkerOn; }

void Board::shiftSkyClock(double days, bool reset)
{
	Weather::shiftClock(days, reset);
	skyChanged();
}

void Board::skyChanged()
{
	Impl &p = *_p;
	if (p.hasBattle && p.battle) p.weather.begin(p.game, p.battle, p.shadeOffset);
}

SkyInfo Board::skyInfo() const { return _p->hasBattle ? _p->weather.info() : SkyInfo(); }

void Board::testStrike() { _p->weather.strike(); }

void Board::testShade(int shade)
{
	Impl &p = *_p;
	if (!p.hasBattle || !p.battle) return;
	p.battle->setGlobalShade(shade);
	p.battle->getTileEngine()->calculateLighting(LL_AMBIENT);
	p.firstTile = nullptr; // rebuild the board (tile colours) and roll the sky again
}

float Board::nightLevel() const
{
	const Impl &p = *_p;
	if (!p.hasBattle || !p.battle) return 0.f;
	// the game's darkness runs 0 (day) .. 15; night missions sit around 10 and up
	return glm::clamp((p.globalShade() - 2) / 8.f, 0.f, 1.f);
}

void Board::drawWeather(const Shader &sh) const
{
	const Impl &p = *_p;
	if (!p.hasBattle || p.hasBase || p.shadowPass || !p.atlas.tex.valid()) return;
	// after the room's solid parts (the back wall screens too): see-through things last
	sh.set("uClip", p.clipRect());
	p.applyWin(sh, true);
	p.weather.draw(sh, p.boardMatrix());
	sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
	if (Options::vrDebugPanel && p.skyMarkerOn) const_cast<Impl&>(p).drawSkyMarker(sh);
	p.applyWin(sh, false);
}

namespace
{
/// A thin bar from a to b (world), for the sky marker.
void bar(MeshData &md, const glm::vec3 &a, const glm::vec3 &b, float t, const glm::vec4 &col)
{
	glm::vec3 ax = b - a;
	float len = glm::length(ax);
	if (len < 1e-5f) return;
	ax /= len;
	glm::vec3 p = std::fabs(ax.y) < 0.9f ? glm::normalize(glm::cross(ax, glm::vec3(0, 1, 0))) : glm::vec3(1, 0, 0);
	glm::vec3 q = glm::cross(ax, p);
	p *= t * 0.5f; q *= t * 0.5f;
	glm::vec3 c[8] = {a - p - q, a + p - q, a + p + q, a - p + q, b - p - q, b + p - q, b + p + q, b - p + q};
	static const int f[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
	for (auto &k : f) md.addQuad(c[k[0]], c[k[1]], c[k[2]], c[k[3]], col, 0.f);
}

/// A flat disc of little squares facing the eye: the sun (full) or the moon (lit part by its phase).
void disc(MeshData &md, const glm::vec3 &at, const glm::vec3 &eye, float r, bool moon, float lit, bool waxing, const glm::vec4 &on, const glm::vec4 &off)
{
	glm::vec3 n = eye - at;
	if (glm::length(n) < 1e-4f) return;
	n = glm::normalize(n);
	glm::vec3 right = glm::cross(glm::vec3(0, 1, 0), n);
	right = glm::length(right) > 1e-3f ? glm::normalize(right) : glm::vec3(1, 0, 0);
	glm::vec3 up = glm::cross(n, right);
	const int N = 9;
	float cell = 2.f * r / N;
	for (int iy = 0; iy < N; ++iy)
		for (int ix = 0; ix < N; ++ix)
		{
			float x = (ix + 0.5f) / N * 2.f - 1.f, y = (iy + 0.5f) / N * 2.f - 1.f;
			if (x * x + y * y > 1.f) continue;
			bool isOn = true;
			if (moon)
			{
				float xt = (1.f - 2.f * lit) * std::sqrt(std::max(0.f, 1.f - y * y));
				isOn = waxing ? x > xt : x < -xt;
			}
			glm::vec3 c = at + right * (x * r) + up * (y * r);
			glm::vec3 h1 = right * (cell * 0.45f), h2 = up * (cell * 0.45f);
			md.addQuad(c - h1 - h2, c + h1 - h2, c + h1 + h2, c - h1 + h2, isOn ? on : off, 0.f);
		}
}
}

/// Debug panel on: a compass ring on the table round the map, the sun and the moon where they really
/// stand over the site (or under the horizon, shown on the ring with how far below), and the direction
/// the table's light comes from. Lets the panel's sky clock and sun keys be seen to do something.
void Board::Impl::drawSkyMarker(const Shader &sh)
{
	skyMarkText.clear();
	SkyInfo I = weather.info();
	if (!I.active || !I.outdoor) return;
	glm::mat4 M = boardMatrix();
	float k = tileSize / TILE_W;
	glm::mat4 inv = glm::inverse(M);
	// on the map's ground level, drawn over the terrain so it never hides behind walls
	glm::vec3 C((mapRect.x + mapRect.z) * 0.5f, glm::vec3(M * glm::vec4(0.f, 0.f, 0.f, 1.f)).y, (mapRect.y + mapRect.w) * 0.5f);
	float R = std::min(0.4f, std::min(mapRect.z - mapRect.x, mapRect.w - mapRect.y) * 0.45f);
	// map directions (x east, z south) turned with the map
	auto world = [&](const glm::vec3 &mapDir) { glm::vec3 w = glm::vec3(M * glm::vec4(mapDir, 0.f)); return glm::length(w) > 1e-6f ? glm::normalize(w) : w; };
	auto skyDir = [&](float azDeg, float elDeg)
	{
		float a = glm::radians(azDeg), e = glm::radians(elDeg);
		return world(glm::vec3(std::sin(a) * std::cos(e), std::sin(e), -std::cos(a) * std::cos(e)));
	};
	MeshData md;
	const glm::vec4 ringCol(1.0f, 0.62f, 0.2f, 0.75f);
	// horizon ring
	const int SEG = 72;
	for (int i = 0; i < SEG; ++i)
	{
		float a0 = i * 6.2831853f / SEG, a1 = (i + 1) * 6.2831853f / SEG;
		bar(md, C + glm::vec3(std::cos(a0) * R, 0, std::sin(a0) * R), C + glm::vec3(std::cos(a1) * R, 0, std::sin(a1) * R), 0.0025f, ringCol);
	}
	struct Txt { glm::vec3 at; std::string s; Uint8 col; };
	std::vector<Txt> texts;
	const Uint8 yellow = (Uint8)(Palette::blockOffset(Pathfinding::yellow - 1) - 1), white = (Uint8)(Palette::blockOffset(0) + 1),
		green = (Uint8)(Palette::blockOffset(Pathfinding::green - 1) - 1);
	const char *cn[4] = {"N", "E", "S", "W"};
	for (int i = 0; i < 4; ++i)
	{
		glm::vec3 d = skyDir(i * 90.f, 0.f);
		bar(md, C + d * (R - 0.012f), C + d * (R + 0.012f), 0.004f, ringCol);
		texts.push_back({C + d * (R - 0.035f), cn[i], i == 0 ? yellow : white});
	}
	auto body = [&](float az, float el, bool moon, const std::string &name)
	{
		// up: on its line from the middle, as high as it really is; below the horizon: dimmed, just
		// outside the ring where it is (it rises or sets there), without the line
		bool below = el < 0.f;
		glm::vec3 P = below ? C + skyDir(az, 0.f) * (R + 0.03f) + glm::vec3(0, 0.02f, 0) : C + skyDir(az, el) * R;
		glm::vec4 on = moon ? glm::vec4(0.9f, 0.93f, 1.f, below ? 0.45f : 1.f) : glm::vec4(1.6f, 1.25f, 0.4f, below ? 0.45f : 1.f);
		glm::vec4 off = moon ? glm::vec4(0.12f, 0.13f, 0.18f, below ? 0.35f : 0.8f) : on;
		if (!below) bar(md, C, P, 0.0018f, glm::vec4(glm::vec3(on), 0.55f));
		disc(md, P, viewer, 0.018f, moon, I.moonIllum, I.moonWaxing, on, off);
		char b[96];
		int a = ((int)std::lround(az) % 360 + 360) % 360;
		if (moon) snprintf(b, sizeof(b), "%s %d%% %s  %d %s  dir %d", name.c_str(), (int)(I.moonIllum * 100.f + 0.5f), I.moonWaxing ? "waxing" : "waning", (int)std::lround(std::fabs(el)), below ? "BELOW" : "up", a);
		else snprintf(b, sizeof(b), "%s  %d %s  dir %d", name.c_str(), (int)std::lround(std::fabs(el)), below ? "BELOW" : "up", a);
		texts.push_back({P + glm::vec3(0, 0.03f, 0), b, moon ? white : yellow});
		skyMarkText += (skyMarkText.empty() ? "" : ",") + std::string(moon ? "moon" : "sun") + (below ? "-" : "+") + std::to_string((int)std::lround(std::fabs(el)));
	};
	if (I.realKnown) body(I.realAz, I.realElev, false, "SUN");
	if (I.moonKnown) body(I.moonAz, I.moonAlt, true, "MOON");
	// the table's own light: where the key light (sun or moon) shines from, at the height the darkness gives it
	if (!I.starlit)
	{
		glm::vec3 L = skyDir(I.sunAz, I.sunElev);
		glm::vec3 c = I.sunCol / std::max(0.01f, std::max(I.sunCol.r, std::max(I.sunCol.g, I.sunCol.b)));
		bar(md, C, C + L * (R * 1.15f), 0.004f, glm::vec4(c * 1.4f, 0.9f));
		char b[96];
		snprintf(b, sizeof(b), "TABLE LIGHT  %d up  dir %d  (darkness %d)%s", (int)std::lround(I.sunElev), ((int)std::lround(I.sunAz) % 360 + 360) % 360, I.shade,
			I.moonMirrored && I.sky.compare(0, 5, "night") == 0 ? "  MIRRORED MOON" : "");
		texts.push_back({C + L * (R * 1.15f) + glm::vec3(0, 0.02f, 0), b, green});
	}
	else texts.push_back({C + glm::vec3(0, 0.03f, 0), "STARLIGHT ONLY  (darkness " + std::to_string(I.shade) + ")", green});
	// the clock the sun and moon are worked out for
	{
		int lh = (int)I.localH, lm = (int)((I.localH - lh) * 60.f);
		char b[96];
		snprintf(b, sizeof(b), "%s  /  %02d:%02d LOCAL", I.skyClock.c_str(), lh, lm);
		texts.push_back({C + skyDir(180.f, 0.f) * (R + 0.07f), b, I.skyShift != 0.0 ? yellow : white});
	}
	skyMarkMesh.upload(md, true);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
	sh.set("uModel", glm::mat4(1.f));
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uMode", 2);
	skyMarkMesh.draw();
	glm::vec3 viewerVox = glm::vec3(inv * glm::vec4(viewer, 1.f));
	for (const Txt &t : texts)
	{
		const Label &l = label(t.s, t.col);
		drawLabel(sh, M, glm::vec3(inv * glm::vec4(t.at, 1.f)), viewerVox, l, 0.0026f / k);
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	sh.set("uTint", glm::vec4(1.f));
}

void Board::draw(const Shader &sh, const glm::mat4 &, const glm::vec3 &eye, double)
{
	if (_p->hasBase) _p->baseView.draw(sh, _p->viewer); // labels face the head, not each eye (they must match in both)
	else if (_p->hasBattle && _p->atlas.tex.valid())
	{
		// hand-built lights step with the game's terrain animation (100 ms a frame); the lab has no battle
		// screen to step it, so it goes by the clock there
		const int frame = _p->lab || !_p->battle ? (int)(SDL_GetTicks() / 100) : _p->battle->getAnimFrame();
		sh.set("uAnimFrame", frame & 7);
		// the small scout's cap (frames 9, 10, 11, 10) and lift (8, 8, 17, 17): their purple pixels' colour
		// on each frame against the first, measured from the game's own frames
		static const glm::vec3 PULSE[4] = {{1.f, 1.f, 1.f}, {0.878f, 0.762f, 0.893f}, {0.831f, 0.665f, 0.835f}, {0.878f, 0.762f, 0.893f}};
		sh.set("uPulse[0]", PULSE[0]); sh.set("uPulse[1]", PULSE[1]); sh.set("uPulse[2]", PULSE[2]); sh.set("uPulse[3]", PULSE[3]);
		sh.set("uFlip", glm::vec3(0.738f, 0.456f, 0.708f));
		_p->applyWin(sh, true);
		_p->drawBattle(sh);
		_p->applyWin(sh, false);
		_p->drawCover(sh);
	}
	else if (_p->hasGlobe) _p->drawGlobe(sh);
}

bool Board::raycast(const glm::vec3 &o, const glm::vec3 &d, BoardHit &hit) const
{
	Impl &p = *_p;
	revalidate(p);
	if (p.hasBattle && !p.lab && (!p.game->getSavedGame() || p.game->getSavedGame()->getSavedBattle() != p.battle)) p.hasBattle = false;
	if (p.hasBase)
	{
		int cx, cy;
		if (!p.baseView.raycast(o, d, hit.t, hit.point, cx, cy)) return false;
		hit.base = true;
		hit.globe = false;
		hit.tileX = cx; hit.tileY = cy; hit.tileZ = 0;
		return true;
	}
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
			// the void over unexplored ground: on the ground, picking the highest fogged level
			const int f = p.fogTop(tx, ty);
			if (f && z >= f) standable = standable && tile->isDiscovered(O_FLOOR);
			else if (f) { if (z > 0) continue; standable = true; }
			if (!standable && z > 0) continue;
			glm::vec3 w = o + d * t;
			if (w.x < clip.x || w.x > clip.z || w.z < clip.y || w.z > clip.w) return false;
			hit.t = t;
			hit.point = w;
			hit.tileX = tx; hit.tileY = ty; hit.tileZ = f ? std::max(z, f - 1) : z;
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
		if (w.y < p.surfaceY()) return false; // the part of the dome under the table
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
	if (hit.base) { p.baseView.hover(hit.tileX, hit.tileY); return; }
	p.hoverValid = true;
	p.hoverGlobe = hit.globe;
	p.hoverTile = Position(hit.tileX, hit.tileY, hit.tileZ);
	p.hoverPoint = hit.point;
	p.hoverLon = hit.lon;
	p.hoverLat = hit.lat;
}

void Board::hoverNone() { _p->hoverValid = false; _p->baseView.hoverNone(); }

/// Input can arrive after a screen closed but before the next update(): re-check cached pointers.
static void revalidate(Board::Impl &p)
{
	if (!p.game) { p.bs = nullptr; p.globe = nullptr; p.geo = nullptr; return; }
	bool bsAlive = false, geoAlive = false, labAlive = false;
	for (State *s : p.game->getStates())
	{
		if (s == p.bs) bsAlive = true;
		if (s == p.geo) geoAlive = true;
		if (p.lab && s == (State*)p.lab) labAlive = true;
	}
	if (!labAlive) p.lab = nullptr;
	if (!bsAlive) { p.bs = nullptr; if (!labAlive) p.hasBattle = false; }
	if (!geoAlive) { p.geo = nullptr; p.globe = nullptr; p.hasGlobe = false; }
}

void Board::click(int button)
{
	Impl &p = *_p;
	if (p.hasBase)
	{
		BaseInput in;
		in.clickScreen = clickScreen;
		in.hover = hoverScreen;
		p.baseView.click(button, in);
		return;
	}
	revalidate(p);
	if (!p.hoverValid) return;
	if (p.lab && p.hasBattle && !p.hoverGlobe)
	{
		// the terrain lab: A marks (or unmarks) the tile, the trigger pins its inspect panel (again: unpins)
		if (button == SDL_BUTTON_RIGHT) p.lab->tileAction(p.hoverTile.x, p.hoverTile.y, p.hoverTile.z);
		else if (p.pinned && p.pinTile == p.hoverTile) p.pinned = false;
		else { p.pinned = true; p.pinTile = p.hoverTile; }
		return;
	}
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
	if (p.hasBase) return;
	revalidate(p);
	if (p.hasBattle)
	{
		if (p.lab) { p.labLevel = glm::clamp(p.labLevel + (dir > 0 ? 1 : -1), 0, std::max(0, p.mz - 1)); return; }
		if (!p.bs) return;
		if (dir > 0) p.bs->btnMapUpClick(nullptr); else p.bs->btnMapDownClick(nullptr);
	}
	else if (p.hasGlobe)
	{
		p.globeRadius = glm::clamp(p.globeRadius * (dir > 0 ? 1.1f : 0.9f), 0.18f, 0.45f);
		p.globeCenter.y = p.surfaceY() + Impl::DOME_TOP - p.globeRadius;
	}
}

/// Points the game's camera at the board's focus, and remembers that the board did it, so the
/// next update does not snap the focus back to the camera's whole-tile centre.
static void syncCamera(Board::Impl &p)
{
	if (!p.bs || !p.bs->getMap()) return;
	p.returnIn = -1.f; // the player moved the map: stay there
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
	if (_p->hasBase) { _p->baseView.pan(d); return; }
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
	if (_p->hasBase) return;
	Impl &p = *_p;
	if (p.hasBattle && OpenXcom::VR::viewLocked(p)) return; // the alien turn: the flat screen's view
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
	if (_p->hasBase) return;
	Impl &p = *_p;
	// the alien turn: zooming works (from the flat view's zoom, kept for later alien turns); turning doesn't
	if (p.hasBattle && OpenXcom::VR::viewLocked(p)) { p.alienZoom *= f; return; }
	if (p.hasBattle) p.tileSize = glm::clamp(p.tileSize * f, 0.02f, 0.14f);
	else if (p.hasGlobe)
	{
		p.globeRadius = glm::clamp(p.globeRadius * f, 0.18f, 0.45f);
		p.globeCenter.y = p.surfaceY() + Impl::DOME_TOP - p.globeRadius;
	}
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
	if (p.alienView)
	{
		// the alien turn: jammed, like the wall monitors (nothing of the map)
		static uint32_t seed = 12345u;
		px.assign((size_t)S * S, 0u);
		int roll = (int)(p.animTime * 40.f) % std::max(1, S);
		for (int y = 0; y < S; ++y)
			for (int x = 0; x < S; ++x)
			{
				seed = seed * 1664525u + 1013904223u;
				int n = (int)((seed >> 24) & 255);
				int band = std::abs(y - roll) < 6 ? 20 : 0;
				int v = std::min(255, n * (y & 1 ? 18 : 28) / 255 + band);
				px[(size_t)y * S + x] = 0xFF000000u | ((uint32_t)(v + 6) << 16) | ((uint32_t)(v + 2) << 8) | (uint32_t)v;
			}
		return true;
	}
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
				uint32_t a = p.partInfo(md).avg;
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
	_p->testFlashAt = glm::vec3(tx * TILE_W + 8.f, tz * TILE_H, ty * TILE_W + 8.f);
	_p->testFlashT = 0.8f;
}

void Board::centerOnTile(const glm::vec2 &tile)
{
	Impl &p = *_p;
	revalidate(p);
	if (!p.hasBattle || OpenXcom::VR::viewLocked(p)) return;
	p.focus = glm::vec3(glm::clamp(tile.x, 0.f, (float)p.mx) * TILE_W, 0.f, glm::clamp(tile.y, 0.f, (float)p.my) * TILE_W);
	syncCamera(p);
}

void Board::showRange(const glm::vec2 &tile, float half)
{
	Impl &p = *_p;
	revalidate(p);
	if (!p.hasBattle || OpenXcom::VR::viewLocked(p)) return;
	// the square turned with the table, across the table's map area (a little margin)
	float w = p.mapRect.z - p.mapRect.x, d = p.mapRect.w - p.mapRect.y;
	float ext = 2.f * half * (std::fabs(std::cos(p.yaw)) + std::fabs(std::sin(p.yaw)));
	if (w > 0.f && d > 0.f && ext > 0.f)
	{
		float fit = std::min(w, d) * 0.96f / ext;
		if (p.tileSize > fit) p.tileSize = glm::clamp(fit, 0.02f, 0.14f);
	}
	centerOnTile(tile);
}

bool Board::globeSphere(glm::vec3 &center, float &radius) const
{
	const Impl &p = *_p;
	if (!p.hasGlobe || p.hasBattle) return false;
	center = p.globeCenter;
	radius = p.globeRadius;
	return true;
}

bool Board::canGrab(const glm::vec3 &hand) const
{
	const Impl &p = *_p;
	if (p.hasBase) return p.baseView.contains(hand, 0.1f);
	if (p.hasBattle)
	{
		glm::vec4 c = p.clipRect();
		return hand.x > c.x - 0.1f && hand.x < c.z + 0.1f && hand.z > c.y - 0.1f && hand.z < c.w + 0.1f
			&& hand.y < p.surfaceY() + 0.45f && hand.y > p.surfaceY() - 0.15f;
	}
	if (p.hasGlobe) return glm::length(hand - p.globeCenter) < p.globeRadius + 0.15f && hand.y > p.surfaceY() - 0.05f;
	return false;
}

void Board::beginGrab(int hand, const glm::vec3 &pos)
{
	// (the alien turn: only both hands, to zoom; see updateGrab)
	if (OpenXcom::VR::viewLocked(*_p) && !_p->alienView) return;
	_p->held[hand] = true;
	_p->lastHand[hand] = pos;
}

void Board::updateGrab(int hand, const glm::vec3 &pos)
{
	Impl &p = *_p;
	if (!p.held[hand]) return;
	if (p.hasBase)
	{
		// slide the base across the table
		glm::vec3 dw = pos - p.lastHand[hand];
		p.baseView.pan(glm::vec2(dw.x, dw.z));
		p.lastHand[hand] = pos;
		return;
	}
	revalidate(p);
	int other = 1 - hand;
	if (p.hasBattle && OpenXcom::VR::viewLocked(p))
	{
		// the alien turn: both hands pinch to zoom; nothing slides or turns
		if (p.held[other])
		{
			glm::vec3 o = p.lastHand[other];
			float l0 = glm::length(glm::vec2(p.lastHand[hand].x - o.x, p.lastHand[hand].z - o.z));
			float l1 = glm::length(glm::vec2(pos.x - o.x, pos.z - o.z));
			if (l0 > 0.05f && l1 > 0.05f) p.alienZoom *= l1 / l0;
		}
		p.lastHand[hand] = pos;
		return;
	}
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
		// up the tipped front face of the dome: up and away from the player
		float c = std::cos(Impl::DOME_TILT), sn = std::sin(Impl::DOME_TILT);
		float along = dw.y * c + dw.z * sn;
		ca = glm::clamp(ca - (double)along / p.globeRadius, -1.5, 1.5);
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

namespace OpenXcom
{
namespace VR
{
bool Board::baseShown() const { return _p->hasBase; }
void Board::baseFingers(const Hands &hands, const bool handBusy[2], std::function<void(int, float, float)> haptic, float dt)
{
	if (!_p->hasBase) return;
	BaseInput in;
	in.clickScreen = clickScreen;
	in.hover = hoverScreen;
	in.haptic = haptic;
	in.handBusy[0] = handBusy[0];
	in.handBusy[1] = handBusy[1];
	_p->baseView.fingers(hands, in, dt);
}
bool Board::baseCellTop(int cx, int cy, glm::vec3 &w) const { return _p->hasBase && _p->baseView.cellTop(cx, cy, w); }
std::string Board::baseStatus() const { return _p->baseView.status(); }
}
}
