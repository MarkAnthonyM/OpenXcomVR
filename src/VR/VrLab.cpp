/*
 * OXCE VR tabletop - the terrain lab (see VrLab.h).
 */
#include "VrLab.h"
#include "VrVersion.h"
#include "../Engine/Game.h"
#include "../Engine/CrossPlatform.h"
#include "../Engine/FileMap.h"
#include "../Engine/Logger.h"
#include "../Engine/Action.h"
#include "../Engine/Options.h"
#include "../Interface/Window.h"
#include "../Interface/Text.h"
#include "../Interface/TextButton.h"
#include "../Interface/ToggleTextButton.h"
#include "../Interface/TextEdit.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleTerrain.h"
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleUfo.h"
#include "../Mod/MapBlock.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/MapData.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Battlescape/TileEngine.h"
#include "../Battlescape/Position.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace OpenXcom
{

namespace VR
{

static bool hasArg(const std::string &name, std::string *value = nullptr)
{
	const auto &args = CrossPlatform::getArgs();
	for (size_t i = 0; i < args.size(); ++i)
	{
		std::string l = args[i];
		std::transform(l.begin(), l.end(), l.begin(), ::tolower);
		if (l != name && l != "-" + name) continue;
		if (value && i + 1 < args.size()) *value = args[i + 1];
		return true;
	}
	return false;
}

bool labRequested()
{
	static int on = -1;
	if (on < 0) on = hasArg("-vrterrainlab") ? 1 : 0;
	return on == 1;
}

static ReliefMode g_relief = RELIEF_STEPS;
static bool g_reliefRead = false;
static unsigned g_styleGen = 0;
/// voxel steps (owner's choice); -vrRelief flat|steps|smooth on the command line compares (not saved)
ReliefMode reliefMode()
{
	if (!g_reliefRead)
	{
		g_reliefRead = true;
		std::string v;
		if (hasArg("-vrrelief", &v))
		{
			std::transform(v.begin(), v.end(), v.begin(), ::tolower);
			g_relief = v == "flat" ? RELIEF_FLAT : v == "smooth" ? RELIEF_SMOOTH : RELIEF_STEPS;
		}
	}
	return g_relief;
}
void setReliefMode(ReliefMode mode) { reliefMode(); if (mode != g_relief) { g_relief = mode; ++g_styleGen; } }
unsigned partStyleGeneration() { return g_styleGen; }

}

namespace
{
const char *const RELIEF_NAMES[] = {"FLAT", "STEPS", "SMOOTH"};
const char *const LIGHT_NAMES[] = {"DAY", "DUSK", "NIGHT"};
const int LIGHT_SHADES[] = {0, 6, 12};
const char *const WEATHER_NAMES[] = {"AUTO", "CLEAR", "RAIN", "SNOW", "DUST", "SAND", "MIST", "CLOUDS"};
const char *const WEATHER_KINDS[] = {"auto", "clear", "rain", "snow", "dust", "sand", "mist", "clouds"};
const int WEATHERS = 8;
const char *const STRENGTH_NAMES[] = {"LIGHT", "NORMAL", "HEAVY"};
const char *const LAMP_NAMES[] = {"OFF", "GRID", "HAND"};
const char *const REASONS[] = {"WRONG DIRECTION", "TOO TALL", "TOO LOW", "SHOULD BE FLAT",
	"SHOULD HAVE RELIEF", "CROWN WRONG", "HIDDEN SIDE WRONG", "OTHER"};
const int COLS = 10;     // parts per row on the parts page
const int ROW_W = 21;    // the parts page's width in tiles (the assembled page wraps at it too)
const int LAMP_POWER = 15, LAMP_STEP = 7; // a soldier's personal light (xcom1 armors), lamps every 7 tiles

bool isBlanks(const MapDataSet *ds) { return ds->getName() == "BLANKS"; }
std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
std::string oneLine(std::string s) { for (char &c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' '; return s; }

/// A map block's object parts as the game reads them (BattlescapeGenerator::loadMAP): x, y, z and the part.
struct BlockObj { int x, y, z; MapData *md; int id, set, part; }; // part: which of the tile's four bytes
bool readBlock(RuleTerrain *rule, MapBlock *mb, std::vector<BlockObj> &out, int parts = 1 << O_OBJECT)
{
	auto in = FileMap::getIStream("MAPS/" + mb->getName() + ".MAP");
	char size[3];
	if (!in || !in->read(size, 3)) return false;
	int sy = (unsigned char)size[0], sx = (unsigned char)size[1], sz = (unsigned char)size[2];
	int x = 0, y = 0, z = sz - 1;
	unsigned char v[4];
	while (z >= 0 && in->read((char*)v, 4))
	{
		for (int part = O_FLOOR; part < O_MAX; ++part)
		{
			if (!(parts & (1 << part)) || !v[part]) continue;
			unsigned int id = v[part];
			int set = 0;
			if (MapData *md = rule->getMapData(&id, &set)) out.push_back({x, y, z, md, (int)id, set, part});
		}
		if (++x == sx) { x = 0; if (++y == sy) { y = 0; --z; } }
	}
	return true;
}
}

TerrainLabState::TerrainLabState()
{
	_window = new Window(this, 320, 200, 0, 0);
	_txtTitle = new Text(300, 17, 10, 6);
	_txtTerrain = new Text(300, 9, 10, 26);
	_txtPage = new Text(300, 9, 10, 36);
	_btnPrevTerrain = new TextButton(72, 16, 10, 50);
	_btnNextTerrain = new TextButton(72, 16, 86, 50);
	_btnPrevPage = new TextButton(72, 16, 162, 50);
	_btnNextPage = new TextButton(72, 16, 238, 50);
	_btnRelief = new TextButton(72, 16, 10, 70);
	_btnLight = new TextButton(72, 16, 86, 70);
	_btnSun = new TextButton(72, 16, 162, 70);
	_btnWeather = new TextButton(72, 16, 238, 70);
	_btnStrength = new TextButton(72, 16, 10, 90);
	_btnLamps = new TextButton(72, 16, 86, 90);
	_btnPictures = new TextButton(72, 16, 162, 90);
	_btnClear = new TextButton(72, 16, 238, 90);
	_txtInfo = new Text(300, 64, 10, 112);
	_btnSheets = new TextButton(72, 16, 162, 178);
	_btnQuit = new TextButton(72, 16, 238, 178);

	setInterface("geoscape");
	add(_window, "genericWindow", "geoscape");
	add(_txtTitle, "genericText", "geoscape");
	add(_txtTerrain, "genericText", "geoscape");
	add(_txtPage, "genericText", "geoscape");
	for (TextButton *b : {_btnPrevTerrain, _btnNextTerrain, _btnPrevPage, _btnNextPage, _btnRelief, _btnLight, _btnSun, _btnWeather,
		_btnStrength, _btnLamps, _btnPictures, _btnClear, _btnSheets, _btnQuit})
		add(b, "genericButton2", "geoscape");
	add(_txtInfo, "genericText", "geoscape");
	centerAllSurfaces();
	setWindowBackground(_window, "geoscape");

	_txtTitle->setBig();
	_txtTitle->setText("TERRAIN LAB");
	_txtInfo->setWordWrap(true);

	_btnPrevTerrain->setText("< TERRAIN");
	_btnNextTerrain->setText("TERRAIN >");
	_btnPrevPage->setText("< PAGE");
	_btnNextPage->setText("PAGE >");
	_btnSun->setText("TURN SUN");
	_btnClear->setText("CLEAR MARKS");
	_btnSheets->setText("SHEETS");
	_btnQuit->setText("QUIT");
	_btnPrevTerrain->onMouseClick((ActionHandler)&TerrainLabState::btnPrevTerrainClick);
	_btnNextTerrain->onMouseClick((ActionHandler)&TerrainLabState::btnNextTerrainClick);
	_btnPrevPage->onMouseClick((ActionHandler)&TerrainLabState::btnPrevPageClick);
	_btnNextPage->onMouseClick((ActionHandler)&TerrainLabState::btnNextPageClick);
	_btnRelief->onMouseClick((ActionHandler)&TerrainLabState::btnReliefClick);
	_btnLight->onMouseClick((ActionHandler)&TerrainLabState::btnLightClick);
	_btnSun->onMouseClick((ActionHandler)&TerrainLabState::btnSunClick);
	_btnWeather->onMouseClick((ActionHandler)&TerrainLabState::btnWeatherClick);
	_btnStrength->onMouseClick((ActionHandler)&TerrainLabState::btnStrengthClick);
	_btnLamps->onMouseClick((ActionHandler)&TerrainLabState::btnLampsClick);
	_btnPictures->onMouseClick((ActionHandler)&TerrainLabState::btnPicturesClick);
	_btnClear->onMouseClick((ActionHandler)&TerrainLabState::btnClearClick);
	_btnSheets->onMouseClick((ActionHandler)&TerrainLabState::btnSheetsClick);
	_btnQuit->onMouseClick((ActionHandler)&TerrainLabState::btnQuitClick);

	collectTerrains();
	loadMarks();
	rebuild();
}

TerrainLabState::~TerrainLabState()
{
	if (_battle)
	{
		_battle->getMapDataSets()->clear(); // the lab keeps its terrain sets loaded
		delete _battle;
	}
	for (SavedBattleGame *b : _retired) { b->getMapDataSets()->clear(); delete b; }
}

/// Every terrain the game knows: the globe's (farms, forests, cities...), then each craft's and UFO's.
void TerrainLabState::collectTerrains()
{
	Mod *mod = _game->getMod();
	std::set<RuleTerrain*> seen;
	auto addT = [&](RuleTerrain *t, const std::string &what)
	{
		if (!t || !seen.insert(t).second) return;
		_terrains.push_back({t->getName() + what, t});
	};
	for (const std::string &n : mod->getTerrainList()) addT(mod->getTerrain(n), "");
	for (const std::string &n : mod->getCraftsList())
		if (RuleCraft *c = mod->getCraft(n)) addT(c->getBattlescapeTerrainData(), " (craft " + n + ")");
	for (const std::string &n : mod->getUfosList())
		if (RuleUfo *u = mod->getUfo(n)) addT(u->getBattlescapeTerrainData(), " (UFO " + n + ")");
	// a terrain to start on, if named after the switch (-vrTerrainLab POLAR)
	std::string start;
	if (VR::hasArg("-vrterrainlab", &start))
		for (size_t k = 0; k < _terrains.size(); ++k)
			if (_terrains[k].rule->getName() == start) _terrain = (int)k;
	Log(LOG_INFO) << "[VR] terrain lab: " << _terrains.size() << " terrains";
}

std::string TerrainLabState::terrainName() const
{
	return _terrains.empty() ? std::string() : _terrains[_terrain].rule->getName();
}

/// Page 1 the parts, page 2 the assembled pieces, then the map blocks.
int TerrainLabState::pageCount() const
{
	if (_terrains.empty()) return 1;
	return 2 + (int)_terrains[_terrain].rule->getMapBlocks()->size();
}

std::string TerrainLabState::pageKey() const
{
	if (_page == 0) return "parts";
	if (_page == 1) return "assembled";
	if (_terrains.empty()) return "";
	return "block " + _terrains[_terrain].rule->getMapBlocks()->at(_page - 2)->getName();
}

void TerrainLabState::rebuild()
{
	if (_terrains.empty()) { updateTexts(); return; }
	RuleTerrain *rule = _terrains[_terrain].rule;
	Mod *mod = _game->getMod();
	for (MapDataSet *ds : *rule->getMapDataSets()) ds->loadData(mod->getMCDPatch(ds->getName()));

	// the old map stays alive until the table has let go of it (it binds the new one on its next update)
	for (SavedBattleGame *b : _retired) { b->getMapDataSets()->clear(); delete b; }
	_retired.clear();
	if (_battle) _retired.push_back(_battle);

	_battle = new SavedBattleGame(mod, _game->getLanguage());
	_battle->setMissionType("STR_TERRAIN_LAB");
	for (MapDataSet *ds : *rule->getMapDataSets()) _battle->getMapDataSets()->push_back(ds);
	_labels.clear();
	_pieces.clear();
	if (_page == 0) buildParts(rule);
	else if (_page == 1) buildAssembled(rule);
	else buildBlock(rule, _page - 2);
	if (_battle->getMapSizeX() <= 0) _battle->initMap(1, 1, 1, false);

	for (int i = 0; i < _battle->getMapSizeXYZ(); ++i)
		_battle->getTile(i)->setDiscovered(true, O_FLOOR);
	_battle->initUtilities(mod);
	_battle->setGlobalShade(LIGHT_SHADES[_light]);
	_battle->getTileEngine()->calculateLighting(LL_AMBIENT);
	_lampKey = -99; // the lamps are put back on the new map
	++_generation;
	updateTexts();
}

MapData *TerrainLabState::groundOf(RuleTerrain *rule, int &groundId, int &groundSet) const
{
	auto &sets = *rule->getMapDataSets();
	std::map<std::pair<int, int>, int> used;
	for (MapBlock *mb : *rule->getMapBlocks())
	{
		std::vector<BlockObj> floors;
		readBlock(rule, mb, floors, 1 << O_FLOOR);
		for (const BlockObj &f : floors) ++used[{f.set, f.id}];
	}
	MapData *ground = nullptr;
	int best = 0;
	for (auto &kv : used)
		if (kv.second > best && !isBlanks(sets[kv.first.first]))
		{
			best = kv.second;
			groundSet = kv.first.first;
			groundId = kv.first.second;
			ground = sets[groundSet]->getObject(groundId);
		}
	if (!ground)
		for (int s = 0; s < (int)sets.size() && !ground; ++s)
		{
			if (isBlanks(sets[s])) continue;
			for (size_t i = 0; i < sets[s]->getSize(); ++i)
				if (sets[s]->getObject(i)->getObjectType() == O_FLOOR) { ground = sets[s]->getObject(i); groundId = (int)i; groundSet = s; break; }
		}
	return ground;
}

/// Every part of the terrain's sets, one per tile with a tile of space around it: floors, west walls,
/// north walls, objects, each kind starting a new row. Walls and objects stand on the terrain's ground.
void TerrainLabState::buildParts(RuleTerrain *rule)
{
	struct Item { MapData *md; int id, set; std::string name; };
	std::vector<std::vector<Item>> groups;
	auto &sets = *rule->getMapDataSets();
	for (int s = 0; s < (int)sets.size(); ++s)
	{
		if (isBlanks(sets[s])) continue;
		for (int kind = O_FLOOR; kind < O_MAX; ++kind)
		{
			std::vector<Item> g;
			for (size_t i = 0; i < sets[s]->getSize(); ++i)
			{
				MapData *md = sets[s]->getObject(i);
				if (!md || md->getObjectType() != (TilePart)kind) continue;
				g.push_back({md, (int)i, s, sets[s]->getName() + " " + std::to_string(i)});
			}
			if (!g.empty()) groups.push_back(g);
		}
	}
	int groundId = 0, groundSet = 0;
	MapData *ground = groundOf(rule, groundId, groundSet);
	int rows = 0;
	for (auto &g : groups) rows += ((int)g.size() + COLS - 1) / COLS;
	const int W = ROW_W, L = std::max(3, 2 * rows + 1);
	_battle->initMap(W, L, 2, false);
	if (ground)
		for (int y = 0; y < L; ++y)
			for (int x = 0; x < W; ++x)
				_battle->getTile(Position(x, y, 0))->setMapData(ground, groundId, groundSet, O_FLOOR);
	int row = 0;
	for (auto &g : groups)
	{
		for (size_t k = 0; k < g.size(); ++k)
		{
			int x = 1 + 2 * (int)(k % COLS), y = 1 + 2 * (row + (int)(k / COLS));
			const Item &it = g[k];
			_battle->getTile(Position(x, y, 0))->setMapData(it.md, it.id, it.set, it.md->getObjectType());
			_labels.push_back({x, y, 0, it.name});
		}
		row += ((int)g.size() + COLS - 1) / COLS;
	}
}

/// Pieces made of several object parts: parts that stand next to each other (beside, above or below) in
/// the same arrangement every time they appear in the terrain's map blocks (each sees the other there in
/// at least 3 of 4 of its appearances). A part next to copies of itself (hedge rows, crop fields) doesn't
/// count: that's a pattern, not a piece. Each piece is built from one place it really appears, on the
/// terrain's ground, in rows with two tiles between them.
void TerrainLabState::buildAssembled(RuleTerrain *rule)
{
	struct Occ { int block; BlockObj o; };
	std::vector<std::vector<BlockObj>> blocks;
	std::map<const MapData*, int> count;
	std::map<std::tuple<const MapData*, int, int, int, const MapData*>, int> pairs;
	for (MapBlock *mb : *rule->getMapBlocks())
	{
		std::vector<BlockObj> objs;
		readBlock(rule, mb, objs);
		std::map<std::tuple<int, int, int>, const BlockObj*> at;
		for (const BlockObj &o : objs) at[{o.x, o.y, o.z}] = &o;
		for (const BlockObj &o : objs)
		{
			++count[o.md];
			for (int dz = -1; dz <= 1; ++dz)
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (!dx && !dy && !dz) continue;
						auto it = at.find({o.x + dx, o.y + dy, o.z + dz});
						if (it == at.end() || it->second->md == o.md) continue;
						++pairs[{o.md, dx, dy, dz, it->second->md}];
					}
		}
		blocks.push_back(objs);
	}
	auto linked = [&](const MapData *a, int dx, int dy, int dz, const MapData *b) -> bool
	{
		auto ab = pairs.find({a, dx, dy, dz, b}), ba = pairs.find({b, -dx, -dy, -dz, a});
		if (ab == pairs.end() || ba == pairs.end()) return false;
		return ab->second * 4 >= count[a] * 3 && ba->second * 4 >= count[b] * 3;
	};
	// grow each piece from the first place one of its parts appears, through linked neighbours
	std::set<const MapData*> done;
	struct PieceParts { std::vector<BlockObj> parts; };
	std::vector<PieceParts> found;
	for (auto &objs : blocks)
	{
		std::map<std::tuple<int, int, int>, const BlockObj*> at;
		for (const BlockObj &o : objs) at[{o.x, o.y, o.z}] = &o;
		for (const BlockObj &seed : objs)
		{
			if (done.count(seed.md)) continue;
			std::vector<const BlockObj*> piece{&seed};
			std::set<std::tuple<int, int, int>> in{{seed.x, seed.y, seed.z}};
			for (size_t k = 0; k < piece.size(); ++k)
			{
				const BlockObj &o = *piece[k];
				for (int dz = -1; dz <= 1; ++dz)
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							auto it = at.find({o.x + dx, o.y + dy, o.z + dz});
							if (it == at.end() || in.count({o.x + dx, o.y + dy, o.z + dz})) continue;
							if (it->second->md == o.md || !linked(o.md, dx, dy, dz, it->second->md)) continue;
							in.insert({o.x + dx, o.y + dy, o.z + dz});
							piece.push_back(it->second);
						}
			}
			if (piece.size() < 2) continue;
			PieceParts pp;
			for (const BlockObj *o : piece) { pp.parts.push_back(*o); done.insert(o->md); }
			found.push_back(pp);
		}
	}
	// lay them out: rows ROW_W wide, two tiles apart, a tile of ground around each
	struct Placed { const PieceParts *pp; int x0, y0, minX, minY, minZ, w, l, h; };
	std::vector<Placed> placed;
	int cx = 1, cy = 1, rowL = 0, H = 2;
	for (const PieceParts &pp : found)
	{
		int minX = 1 << 20, minY = 1 << 20, minZ = 1 << 20, maxX = -1, maxY = -1, maxZ = -1;
		for (const BlockObj &o : pp.parts)
		{
			minX = std::min(minX, o.x); minY = std::min(minY, o.y); minZ = std::min(minZ, o.z);
			maxX = std::max(maxX, o.x); maxY = std::max(maxY, o.y); maxZ = std::max(maxZ, o.z);
		}
		int w = maxX - minX + 1, l = maxY - minY + 1, h = maxZ - minZ + 1;
		if (cx > 1 && cx + w + 1 > ROW_W) { cx = 1; cy += rowL + 2; rowL = 0; }
		placed.push_back({&pp, cx, cy, minX, minY, minZ, w, l, h});
		cx += w + 2;
		rowL = std::max(rowL, l);
		H = std::max(H, h + 1);
	}
	const int W = ROW_W, L = std::max(3, cy + rowL + 1);
	_battle->initMap(W, L, H, false);
	int groundId = 0, groundSet = 0;
	if (MapData *ground = groundOf(rule, groundId, groundSet))
		for (int y = 0; y < L; ++y)
			for (int x = 0; x < W; ++x)
				_battle->getTile(Position(x, y, 0))->setMapData(ground, groundId, groundSet, O_FLOOR);
	auto &sets = *rule->getMapDataSets();
	for (const Placed &p : placed)
	{
		std::set<std::string> names;
		for (const BlockObj &o : p.pp->parts)
		{
			Tile *t = _battle->getTile(Position(p.x0 + o.x - p.minX, p.y0 + o.y - p.minY, o.z - p.minZ));
			if (t) t->setMapData(o.md, o.id, o.set, O_OBJECT);
			names.insert(std::to_string(o.id));
		}
		std::string name = sets[p.pp->parts[0].set]->getName();
		for (const std::string &n : names) name += (name.back() == ' ' || &n == &*names.begin() ? " " : "+") + n;
		_pieces.push_back({p.x0, p.y0, p.w, p.l, p.h, name});
		_labels.push_back({p.x0 + (p.w - 1) / 2, p.y0 + (p.l - 1) / 2, p.h - 1, name});
	}
	Log(LOG_INFO) << "[VR] terrain lab: " << terrainName() << " assembled " << found.size() << " pieces";
}

/// One of the terrain's map blocks, read the way the game reads it (BattlescapeGenerator::loadMAP), with
/// a tile of empty space around it.
void TerrainLabState::buildBlock(RuleTerrain *rule, int block)
{
	MapBlock *mb = rule->getMapBlocks()->at(block);
	auto in = FileMap::getIStream("MAPS/" + mb->getName() + ".MAP");
	char size[3];
	if (!in || !in->read(size, 3)) { Log(LOG_WARNING) << "[VR] terrain lab: can't read MAPS/" << mb->getName() << ".MAP"; return; }
	int sy = (unsigned char)size[0], sx = (unsigned char)size[1], sz = (unsigned char)size[2];
	_battle->initMap(sx + 2, sy + 2, std::max(sz, 1), false);
	std::vector<BlockObj> all;
	readBlock(rule, mb, all, 0xF);
	// each part goes where the map file puts it (its byte in the tile), as the game does: a part's own MCD
	// type can be outside 0-3 (CULTA16-18 use one typed 10), which wrote past the tile's four slots
	for (const BlockObj &o : all)
		_battle->getTile(Position(o.x + 1, o.y + 1, o.z))->setMapData(o.md, o.id, o.set, (TilePart)o.part);
}

std::string TerrainLabState::partsAt(int x, int y, int z) const
{
	if (!_battle) return "";
	Tile *t = _battle->getTile(Position(x, y, z));
	if (!t) return "";
	static const char *const KIND[] = {"floor", "west wall", "north wall", "object"};
	std::string out;
	for (int part = O_FLOOR; part < O_MAX; ++part)
	{
		MapData *md = t->getMapData((TilePart)part);
		if (!md || !md->getDataset()) continue;
		int id = -1, set = -1;
		t->getMapData(&id, &set, (TilePart)part);
		out += (out.empty() ? "" : "; ") + std::string(KIND[part]) + " " + md->getDataset()->getName() + " " + std::to_string(id);
	}
	return out.empty() ? "nothing" : out;
}

void TerrainLabState::updateTexts()
{
	std::ostringstream t, p, info;
	if (_terrains.empty()) t << "No terrains found";
	else
	{
		const Entry &e = _terrains[_terrain];
		t << e.label << "  (" << (_terrain + 1) << "/" << _terrains.size() << ")  sets:";
		for (MapDataSet *ds : *e.rule->getMapDataSets()) t << " " << ds->getName();
		p << "Page " << (_page + 1) << "/" << pageCount() << ": ";
		if (_page == 0) p << "every part, one per tile";
		else if (_page == 1) p << (_pieces.empty() ? "no pieces of several parts" : std::to_string(_pieces.size()) + " pieces of several parts");
		else
		{
			MapBlock *mb = e.rule->getMapBlocks()->at(_page - 2);
			p << "map block " << mb->getName() << " (" << mb->getSizeX() << "x" << mb->getSizeY() << ")";
		}
	}
	int here = 0;
	for (const LabMark &m : _marks) if (m.terrain == terrainName() && m.page == pageKey()) ++here;
	info << "Fingertip over a tile (or the laser): its art and comparison. Touch it (or trigger): pin it. A: mark it for fixing (A again: unmark). "
		<< _marks.size() << " marks open (" << here << " on this page). Notes: "
		<< (_session.empty() ? labFolder() + "<date_time>/" : labFolder() + _session + "/");
	_txtTerrain->setText(t.str());
	_txtPage->setText(p.str());
	_txtInfo->setText(info.str());
	_btnRelief->setText(std::string("RELIEF: ") + RELIEF_NAMES[VR::reliefMode()]);
	_btnLight->setText(std::string("LIGHT: ") + LIGHT_NAMES[_light]);
	_btnWeather->setText(std::string("SKY: ") + WEATHER_NAMES[_weather]);
	_btnStrength->setText(_weather >= 2 ? std::string("STR: ") + STRENGTH_NAMES[_strength] : std::string("STRENGTH -"));
	_btnLamps->setText(std::string("LAMPS: ") + LAMP_NAMES[_lamps]);
	_btnPictures->setText(_pictures ? "PICTURES ON" : "PICTURES OFF");
}

/// The table's lamps: a soldier's personal light (power 15, one less per tile, one level costing 3) on the
/// game's unit light layer, so the light pools look as in a mission. GRID: every 7 tiles on the ground;
/// HAND: one where the laser points.
bool TerrainLabState::updateLamps(int hx, int hy, int hz, bool hover)
{
	if (!_battle) return false;
	int key = _lamps == 0 ? -2 : _lamps == 1 ? -1 : hover ? (hz * 4096 + hy) * 4096 + hx : -3;
	if (key == _lampKey) return false;
	_lampKey = key;
	std::vector<Position> lamps;
	if (_lamps == 1)
		for (int y = LAMP_STEP / 2; y < _battle->getMapSizeY(); y += LAMP_STEP)
			for (int x = LAMP_STEP / 2; x < _battle->getMapSizeX(); x += LAMP_STEP) lamps.push_back(Position(x, y, 0));
	if (_lamps == 2 && hover) lamps.push_back(Position(hx, hy, hz));
	for (int i = 0; i < _battle->getMapSizeXYZ(); ++i)
	{
		Tile *t = _battle->getTile(i);
		t->resetLight(LL_UNITS);
		Position p = t->getPosition();
		int best = 0;
		for (const Position &l : lamps)
		{
			float d = std::sqrt((float)((p.x - l.x) * (p.x - l.x) + (p.y - l.y) * (p.y - l.y))) + 3.f * std::abs(p.z - l.z);
			best = std::max(best, LAMP_POWER - (int)std::lround(d));
		}
		if (best > 0) t->addLight(best, LL_UNITS);
	}
	_lampsLit = (int)lamps.size();
	return true;
}

// ------------------------------------------------------------------ marks

std::string TerrainLabState::labFolder() const { return Options::getUserFolder() + "terrain-lab/"; }

void TerrainLabState::loadMarks()
{
	std::ifstream in(labFolder() + "marks.txt");
	std::string line;
	while (std::getline(in, line))
	{
		if (line.empty() || line[0] == '#') continue;
		std::vector<std::string> f;
		std::stringstream ss(line);
		std::string cell;
		while (std::getline(ss, cell, '\t')) f.push_back(cell);
		if (f.size() < 7) continue;
		LabMark m;
		m.session = f[0]; m.id = std::atoi(f[1].c_str()); m.terrain = f[2]; m.page = f[3];
		m.x = std::atoi(f[4].c_str()); m.y = std::atoi(f[5].c_str()); m.z = std::atoi(f[6].c_str());
		if (f.size() > 7) m.parts = f[7];
		if (f.size() > 8) m.reasons = f[8];
		if (f.size() > 9) m.note = f[9];
		_marks.push_back(m);
	}
	Log(LOG_INFO) << "[VR] terrain lab: " << _marks.size() << " open marks from " << labFolder() << "marks.txt";
}

void TerrainLabState::saveMarks() const
{
	CrossPlatform::createFolder(labFolder());
	std::ofstream out(labFolder() + "marks.txt", std::ios::trunc);
	out << "# terrain lab: open marks (session, number, terrain, page, x, y, z, parts, reasons, note), tab separated\n";
	for (const LabMark &m : _marks)
		out << m.session << '\t' << m.id << '\t' << m.terrain << '\t' << m.page << '\t' << m.x << '\t' << m.y << '\t' << m.z << '\t'
			<< oneLine(m.parts) << '\t' << oneLine(m.reasons) << '\t' << oneLine(m.note) << '\n';
}

/// Adds to this run's notes.txt, making the run's folder first if this is its first mark.
void TerrainLabState::appendNotes(const std::string &text)
{
	if (_session.empty())
	{
		std::time_t now = std::time(nullptr);
		char stamp[32];
		std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M", std::localtime(&now));
		_session = stamp;
		for (int k = 2; CrossPlatform::folderExists(labFolder() + _session); ++k) _session = std::string(stamp) + "-" + std::to_string(k);
		CrossPlatform::createFolder(labFolder());
		CrossPlatform::createFolder(labFolder() + _session);
		char when[32];
		std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", std::localtime(&now));
		std::ofstream head(labFolder() + _session + "/notes.txt", std::ios::app);
		head << "Terrain lab notes - prototype " << OXCE_VR_PROTOTYPE_VERSION << " - " << when << "\n\n";
		Log(LOG_INFO) << "[VR] terrain lab: notes in " << labFolder() << _session;
	}
	std::ofstream out(labFolder() + _session + "/notes.txt", std::ios::app);
	out << text;
}

void TerrainLabState::tileAction(int x, int y, int z)
{
	if (!_battle) return;
	std::string page = pageKey();
	for (auto it = _marks.begin(); it != _marks.end(); ++it)
	{
		if (it->terrain != terrainName() || it->page != page || it->x != x || it->y != y || it->z != z) continue;
		std::ostringstream n;
		n << "removed: mark #" << (it->id < 100 ? (it->id < 10 ? "00" : "0") : "") << it->id << " of " << it->session
			<< " (" << it->terrain << "  " << it->page << "  tile " << x << "," << y << "," << z << ": " << it->reasons << ")\n\n";
		_marks.erase(it);
		appendNotes(n.str());
		saveMarks();
		updateTexts();
		Log(LOG_INFO) << "[VR] terrain lab: unmarked " << x << "," << y << "," << z;
		return;
	}
	_game->pushState(new LabMarkState(this, x, y, z, partsAt(x, y, z)));
}

void TerrainLabState::addMark(int x, int y, int z, const std::string &reasons, const std::string &note)
{
	LabMark m;
	m.id = _nextId++;
	m.terrain = terrainName();
	m.page = pageKey();
	m.x = x; m.y = y; m.z = z;
	m.parts = partsAt(x, y, z);
	m.reasons = reasons.empty() ? "other" : reasons;
	m.note = oneLine(note);
	char num[8];
	snprintf(num, sizeof(num), "%03d", m.id);
	std::ostringstream n;
	n << "#" << num << "  " << m.terrain << "  page " << (_page + 1) << " (" << m.page << ")  tile " << x << "," << y << "," << z
		<< "  relief " << lower(RELIEF_NAMES[VR::reliefMode()]) << "  light " << lower(LIGHT_NAMES[_light])
		<< "  lamps " << lower(LAMP_NAMES[_lamps]) << "  sky " << WEATHER_KINDS[_weather]
		<< (_weather >= 2 ? std::string(" ") + lower(STRENGTH_NAMES[_strength]) : std::string()) << "\n"
		<< "      parts: " << m.parts << "\n"
		<< "      reason: " << m.reasons << "\n";
	if (!m.note.empty()) n << "      note: " << m.note << "\n";
	n << "\n";
	appendNotes(n.str()); // makes the session folder on the first mark
	m.session = _session;
	_marks.push_back(m);
	saveMarks();
	std::string base = labFolder() + _session + "/" + num;
	VR::requestViewCapture(base + "-view.png", base + "-close.png", x, y, z);
	VR::saveInspectPicture(base + "-art.png", x, y, z);
	VR::saveComparePicture(base + "-compare.png", x, y, z);
	updateTexts();
	Log(LOG_INFO) << "[VR] terrain lab: mark #" << num << " " << m.terrain << " " << m.page << " " << x << "," << y << "," << z << " (" << m.reasons << ")";
}

// ------------------------------------------------------------------ keys

void TerrainLabState::btnPrevTerrainClick(Action *)
{
	if (_terrains.empty()) return;
	_terrain = (_terrain + (int)_terrains.size() - 1) % (int)_terrains.size();
	_page = 0;
	rebuild();
}

void TerrainLabState::btnNextTerrainClick(Action *)
{
	if (_terrains.empty()) return;
	_terrain = (_terrain + 1) % (int)_terrains.size();
	_page = 0;
	rebuild();
}

void TerrainLabState::btnPrevPageClick(Action *) { _page = (_page + pageCount() - 1) % pageCount(); rebuild(); }
void TerrainLabState::btnNextPageClick(Action *) { _page = (_page + 1) % pageCount(); rebuild(); }

void TerrainLabState::btnReliefClick(Action *)
{
	VR::setReliefMode((VR::ReliefMode)((VR::reliefMode() + 1) % 3));
	updateTexts();
}

void TerrainLabState::btnLightClick(Action *)
{
	_light = (_light + 1) % 3;
	_shadeReq = LIGHT_SHADES[_light];
	if (_light > 0 && _lamps == 0) _lamps = 1; // dusk and night: the lamps come on (they can be turned off)
	_lampKey = -99;                             // the darkness is worked out again: put the lamps back
	updateTexts();
}

void TerrainLabState::btnSunClick(Action *) { _sunReq += 45.f; }

void TerrainLabState::applyWeather()
{
	std::string k = WEATHER_KINDS[_weather];
	_weatherReq = _weather >= 2 ? k + ":" + std::to_string(_strength) : k;
}

void TerrainLabState::btnWeatherClick(Action *)
{
	_weather = (_weather + 1) % WEATHERS;
	applyWeather();
	updateTexts();
}

void TerrainLabState::btnStrengthClick(Action *)
{
	_strength = (_strength + 1) % 3;
	if (_weather >= 2) applyWeather();
	updateTexts();
}

void TerrainLabState::btnLampsClick(Action *)
{
	_lamps = (_lamps + 1) % 3;
	updateTexts();
}

void TerrainLabState::btnPicturesClick(Action *)
{
	_pictures = !_pictures;
	updateTexts();
}

void TerrainLabState::btnClearClick(Action *)
{
	if (_marks.empty()) return;
	std::ostringstream n;
	n << "cleared all " << _marks.size() << " open marks\n\n";
	appendNotes(n.str());
	_marks.clear();
	saveMarks();
	updateTexts();
}

void TerrainLabState::btnSheetsClick(Action *)
{
	std::string folder = makeSheets("");
	_txtInfo->setText(folder.empty() ? std::string("No sheets made (see the log).") : "Sheets saved: " + folder);
}

std::string TerrainLabState::makeSheets(const std::string &which)
{
	if (_terrains.empty()) return "";
	std::vector<RuleTerrain*> rules;
	if (which.empty()) rules.push_back(_terrains[_terrain].rule);
	else
		for (const Entry &e : _terrains)
			if (which == "all" || e.rule->getName() == which) rules.push_back(e.rule);
	if (rules.empty()) { Log(LOG_WARNING) << "[VR] sheets: no terrain " << which; return ""; }
	char stamp[32];
	time_t now = time(nullptr);
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H%M%S", std::localtime(&now));
	std::string folder = labFolder() + "sheets/" + stamp + "/";
	CrossPlatform::createFolder(labFolder());
	CrossPlatform::createFolder(labFolder() + "sheets/");
	CrossPlatform::createFolder(folder);
	Mod *mod = _game->getMod();
	std::set<MapDataSet*> done;
	std::ostringstream all;
	int made = 0;
	for (RuleTerrain *rule : rules)
		for (MapDataSet *ds : *rule->getMapDataSets())
		{
			if (isBlanks(ds) || !done.insert(ds).second) continue;
			ds->loadData(mod->getMCDPatch(ds->getName()));
			std::string report;
			if (!VR::savePartSheet(ds, folder + ds->getName() + ".png", report)) continue;
			all << report << "\n";
			++made;
		}
	if (!made) return "";
	std::ofstream f(folder + "scores.txt");
	f << "Terrain lab contact sheets, " << OXCE_VR_PROTOTYPE_VERSION << "\n"
		<< "shape: pixels the table's shape (seen from the game's angle) and the art share, out of all either has;\n"
		<< "colour: of the pixels both have, those in about the same colour (3 x 3 averages);\n"
		<< "table only / art only: as a share of the art's pixels. One-pixel edges are not counted.\n"
		<< "Each set's parts are listed worst first (the lower of shape and colour).\n\n" << all.str();
	Log(LOG_INFO) << "[VR] sheets: " << made << " sets in " << folder;
	return folder;
}

void TerrainLabState::btnQuitClick(Action *) { _game->quit(); }

// ------------------------------------------------------------------ the mark window

LabMarkState::LabMarkState(TerrainLabState *lab, int x, int y, int z, const std::string &parts) : _lab(lab), _x(x), _y(y), _z(z)
{
	_screen = false;
	_window = new Window(this, 256, 180, 32, 10, POPUP_BOTH);
	_txtTitle = new Text(236, 9, 42, 18);
	_txtParts = new Text(236, 18, 42, 30);
	for (int i = 0; i < 8; ++i) _reasons.push_back(new ToggleTextButton(116, 14, 42 + (i % 2) * 120, 52 + (i / 2) * 17));
	_txtNote = new Text(236, 9, 42, 122);
	_edtNote = new TextEdit(this, 236, 10, 42, 133);
	_btnOk = new TextButton(80, 16, 42, 158);
	_btnCancel = new TextButton(80, 16, 198, 158);

	setInterface("geoscape");
	add(_window, "genericWindow", "geoscape");
	add(_txtTitle, "genericText", "geoscape");
	add(_txtParts, "genericText", "geoscape");
	for (ToggleTextButton *b : _reasons) add(b, "genericButton2", "geoscape");
	add(_txtNote, "genericText", "geoscape");
	add(_edtNote, "genericText", "geoscape");
	add(_btnOk, "genericButton2", "geoscape");
	add(_btnCancel, "genericButton2", "geoscape");
	centerAllSurfaces();
	setWindowBackground(_window, "geoscape");

	std::ostringstream t;
	t << "MARK " << lab->terrainName() << " " << lab->pageKey() << "  TILE " << x << "," << y << "," << z;
	_txtTitle->setText(t.str());
	_txtParts->setWordWrap(true);
	_txtParts->setText(parts);
	for (int i = 0; i < 8; ++i) _reasons[i]->setText(REASONS[i]);
	_txtNote->setText("NOTE (optional, the table's keyboard):");
	_edtNote->setText("");
	_edtNote->setFocus(true, false); // not modal: the reason buttons stay clickable while it takes the typing
	_btnOk->setText("MARK");
	_btnCancel->setText("CANCEL");
	_btnOk->onMouseClick((ActionHandler)&LabMarkState::btnOkClick);
	_btnCancel->onMouseClick((ActionHandler)&LabMarkState::btnCancelClick);
	_btnCancel->onKeyboardPress((ActionHandler)&LabMarkState::btnCancelClick, Options::keyCancel);
}

LabMarkState::~LabMarkState() {}

void LabMarkState::btnOkClick(Action *)
{
	std::string reasons;
	for (int i = 0; i < 8; ++i)
		if (_reasons[i]->getPressed()) reasons += (reasons.empty() ? "" : ", ") + lower(REASONS[i]);
	TerrainLabState *lab = _lab;
	int x = _x, y = _y, z = _z;
	std::string note = _edtNote->getText();
	_game->popState();
	lab->addMark(x, y, z, reasons, note);
}

void LabMarkState::btnCancelClick(Action *) { _game->popState(); }

}
