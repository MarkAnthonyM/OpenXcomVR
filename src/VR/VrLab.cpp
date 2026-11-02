/*
 * OXCE VR tabletop - the terrain lab (see VrLab.h).
 */
#include "VrLab.h"
#include "../Engine/Game.h"
#include "../Engine/CrossPlatform.h"
#include "../Engine/FileMap.h"
#include "../Engine/Logger.h"
#include "../Engine/Action.h"
#include "../Interface/Window.h"
#include "../Interface/Text.h"
#include "../Interface/TextButton.h"
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
#include <map>
#include <set>
#include <sstream>

namespace OpenXcom
{

namespace VR
{

bool labRequested()
{
	static int on = -1;
	if (on < 0)
	{
		on = 0;
		for (const std::string &a : CrossPlatform::getArgs())
		{
			std::string l = a;
			std::transform(l.begin(), l.end(), l.begin(), ::tolower);
			if (l == "-vrterrainlab" || l == "--vrterrainlab") on = 1;
		}
	}
	return on == 1;
}

static ReliefMode g_relief = RELIEF_FLAT;
static unsigned g_styleGen = 0;
ReliefMode reliefMode() { return g_relief; }
void setReliefMode(ReliefMode mode) { if (mode != g_relief) { g_relief = mode; ++g_styleGen; } }
unsigned partStyleGeneration() { return g_styleGen; }

}

namespace
{
const char *const RELIEF_NAMES[] = {"FLAT", "STEPS", "SMOOTH"};
const char *const LIGHT_NAMES[] = {"DAY", "DUSK", "NIGHT"};
const int LIGHT_SHADES[] = {0, 6, 12};
const char *const WEATHER_NAMES[] = {"AUTO", "CLEAR", "RAIN", "SNOW", "MIST"};
const char *const WEATHER_KINDS[] = {"auto", "clear", "rain", "snow", "mist"};
const int COLS = 10; // parts per row on the parts page

bool isBlanks(const MapDataSet *ds) { return ds->getName() == "BLANKS"; }
}

TerrainLabState::TerrainLabState()
{
	_window = new Window(this, 320, 200, 0, 0);
	_txtTitle = new Text(300, 17, 10, 8);
	_txtTerrain = new Text(300, 9, 10, 30);
	_txtPage = new Text(300, 9, 10, 40);
	_btnPrevTerrain = new TextButton(72, 16, 10, 58);
	_btnNextTerrain = new TextButton(72, 16, 86, 58);
	_btnPrevPage = new TextButton(72, 16, 162, 58);
	_btnNextPage = new TextButton(72, 16, 238, 58);
	_btnRelief = new TextButton(72, 16, 10, 80);
	_btnLight = new TextButton(72, 16, 86, 80);
	_btnSun = new TextButton(72, 16, 162, 80);
	_btnWeather = new TextButton(72, 16, 238, 80);
	_txtInfo = new Text(300, 64, 10, 104);
	_btnQuit = new TextButton(72, 16, 238, 176);

	setInterface("geoscape");
	add(_window, "genericWindow", "geoscape");
	add(_txtTitle, "genericText", "geoscape");
	add(_txtTerrain, "genericText", "geoscape");
	add(_txtPage, "genericText", "geoscape");
	for (TextButton *b : {_btnPrevTerrain, _btnNextTerrain, _btnPrevPage, _btnNextPage, _btnRelief, _btnLight, _btnSun, _btnWeather, _btnQuit})
		add(b, "genericButton2", "geoscape");
	add(_txtInfo, "genericText", "geoscape");
	centerAllSurfaces();
	setWindowBackground(_window, "geoscape");

	_txtTitle->setBig();
	_txtTitle->setText("TERRAIN LAB");
	_txtInfo->setWordWrap(true);
	_txtInfo->setText("A developer tool: the terrain on the table is built exactly as in a mission. "
		"Slide, turn and zoom the table as usual. The keys here only change what the table shows; "
		"nothing is saved.");

	_btnPrevTerrain->setText("< TERRAIN");
	_btnNextTerrain->setText("TERRAIN >");
	_btnPrevPage->setText("< PAGE");
	_btnNextPage->setText("PAGE >");
	_btnSun->setText("TURN SUN");
	_btnQuit->setText("QUIT");
	_btnPrevTerrain->onMouseClick((ActionHandler)&TerrainLabState::btnPrevTerrainClick);
	_btnNextTerrain->onMouseClick((ActionHandler)&TerrainLabState::btnNextTerrainClick);
	_btnPrevPage->onMouseClick((ActionHandler)&TerrainLabState::btnPrevPageClick);
	_btnNextPage->onMouseClick((ActionHandler)&TerrainLabState::btnNextPageClick);
	_btnRelief->onMouseClick((ActionHandler)&TerrainLabState::btnReliefClick);
	_btnLight->onMouseClick((ActionHandler)&TerrainLabState::btnLightClick);
	_btnSun->onMouseClick((ActionHandler)&TerrainLabState::btnSunClick);
	_btnWeather->onMouseClick((ActionHandler)&TerrainLabState::btnWeatherClick);
	_btnQuit->onMouseClick((ActionHandler)&TerrainLabState::btnQuitClick);

	collectTerrains();
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
	const auto &args = CrossPlatform::getArgs();
	for (size_t i = 0; i + 1 < args.size(); ++i)
	{
		std::string l = args[i];
		std::transform(l.begin(), l.end(), l.begin(), ::tolower);
		if (l != "-vrterrainlab") continue;
		for (size_t k = 0; k < _terrains.size(); ++k)
			if (_terrains[k].rule->getName() == args[i + 1]) _terrain = (int)k;
	}
	Log(LOG_INFO) << "[VR] terrain lab: " << _terrains.size() << " terrains";
}

int TerrainLabState::pageCount() const
{
	if (_terrains.empty()) return 1;
	return 1 + (int)_terrains[_terrain].rule->getMapBlocks()->size();
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
	if (_page == 0) buildParts(rule);
	else buildBlock(rule, _page - 1);
	if (_battle->getMapSizeX() <= 0) _battle->initMap(1, 1, 1, false);

	for (int i = 0; i < _battle->getMapSizeXYZ(); ++i)
		_battle->getTile(i)->setDiscovered(true, O_FLOOR);
	_battle->initUtilities(mod);
	_battle->setGlobalShade(LIGHT_SHADES[_light]);
	_battle->getTileEngine()->calculateLighting(LL_AMBIENT);
	++_generation;
	updateTexts();
}

/// Every part of the terrain's sets, one per tile with a tile of space around it: floors, west walls,
/// north walls, objects, each kind starting a new row. Walls and objects stand on the set's first floor.
void TerrainLabState::buildParts(RuleTerrain *rule)
{
	struct Item { MapData *md; int id, set; std::string name; };
	std::vector<std::vector<Item>> groups;
	MapData *ground = nullptr;
	int groundId = 0, groundSet = 0;
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
				if (kind == O_FLOOR && !ground) { ground = md; groundId = (int)i; groundSet = s; }
			}
			if (!g.empty()) groups.push_back(g);
		}
	}
	// the ground: the floor the terrain's map blocks use most (plain grass, snow, sand...)
	{
		std::map<std::pair<int, int>, int> used;
		for (MapBlock *mb : *rule->getMapBlocks())
		{
			auto in = FileMap::getIStream("MAPS/" + mb->getName() + ".MAP");
			char size[3];
			if (!in || !in->read(size, 3)) continue;
			unsigned char v[4];
			while (in->read((char*)v, 4))
			{
				unsigned int id = v[O_FLOOR];
				int set = 0;
				if (id && rule->getMapData(&id, &set)) ++used[{set, (int)id}];
			}
		}
		int best = 0;
		for (auto &kv : used)
			if (kv.second > best && !isBlanks(sets[kv.first.first]))
			{
				best = kv.second;
				groundSet = kv.first.first;
				groundId = kv.first.second;
				ground = sets[groundSet]->getObject(groundId);
			}
	}
	int rows = 0;
	for (auto &g : groups) rows += ((int)g.size() + COLS - 1) / COLS;
	const int W = 2 * COLS + 1, L = std::max(3, 2 * rows + 1);
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
			Tile *t = _battle->getTile(Position(x, y, 0));
			t->setMapData(it.md, it.id, it.set, it.md->getObjectType());
			_labels.push_back({x, y, 0, it.name});
		}
		row += ((int)g.size() + COLS - 1) / COLS;
	}
}

/// One of the terrain's map blocks, read the way the game reads it (BattlescapeGenerator::loadMAP), with
/// a tile of empty space around it.
void TerrainLabState::buildBlock(RuleTerrain *rule, int block)
{
	MapBlock *mb = rule->getMapBlocks()->at(block);
	std::string file = "MAPS/" + mb->getName() + ".MAP";
	auto in = FileMap::getIStream(file);
	char size[3];
	if (!in || !in->read(size, 3)) { Log(LOG_WARNING) << "[VR] terrain lab: can't read " << file; return; }
	int sy = (unsigned char)size[0], sx = (unsigned char)size[1], sz = (unsigned char)size[2];
	_battle->initMap(sx + 2, sy + 2, std::max(sz, 1), false);
	int x = 0, y = 0, z = sz - 1;
	unsigned char v[4];
	while (z >= 0 && in->read((char*)v, 4))
	{
		for (int part = O_FLOOR; part < O_MAX; ++part)
		{
			unsigned int id = v[part];
			if (!id) continue;
			int set = 0;
			if (MapData *md = rule->getMapData(&id, &set))
				_battle->getTile(Position(x + 1, y + 1, z))->setMapData(md, (int)id, set, (TilePart)part);
		}
		if (++x == sx) { x = 0; if (++y == sy) { y = 0; --z; } }
	}
}

void TerrainLabState::updateTexts()
{
	std::ostringstream t, p;
	if (_terrains.empty()) t << "No terrains found";
	else
	{
		const Entry &e = _terrains[_terrain];
		t << e.label << "  (" << (_terrain + 1) << "/" << _terrains.size() << ")  sets:";
		for (MapDataSet *ds : *e.rule->getMapDataSets()) t << " " << ds->getName();
		if (_page == 0) p << "Page 1/" << pageCount() << ": every part, one per tile";
		else
		{
			MapBlock *mb = e.rule->getMapBlocks()->at(_page - 1);
			p << "Page " << (_page + 1) << "/" << pageCount() << ": map block " << mb->getName() << " (" << mb->getSizeX() << "x" << mb->getSizeY() << ")";
		}
	}
	_txtTerrain->setText(t.str());
	_txtPage->setText(p.str());
	_btnRelief->setText(std::string("RELIEF: ") + RELIEF_NAMES[VR::reliefMode()]);
	_btnLight->setText(std::string("LIGHT: ") + LIGHT_NAMES[_light]);
	_btnWeather->setText(std::string("SKY: ") + WEATHER_NAMES[_weather]);
}

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
	updateTexts();
}

void TerrainLabState::btnSunClick(Action *) { _sunReq += 45.f; }

void TerrainLabState::btnWeatherClick(Action *)
{
	_weather = (_weather + 1) % 5;
	_weatherReq = WEATHER_KINDS[_weather];
	updateTexts();
}

void TerrainLabState::btnQuitClick(Action *) { _game->quit(); }

}
