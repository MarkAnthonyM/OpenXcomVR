#pragma once
/*
 * OXCE VR tabletop - the terrain lab (a developer tool, not for players).
 *
 * Started with -vrTerrainLab on the command line (read straight from the arguments, so it is never
 * saved into options.cfg). Instead of the main menu, the table shows a terrain on its own: every part
 * of its terrain sets laid out one per tile with its name, or one of its map blocks as the game builds
 * it (craft and UFO terrains included). The flat screen on the wall holds the lab's controls.
 *
 * The table draws the lab's map with the same code as a mission (VrBoard), so what the lab shows is
 * what a mission shows.
 */
#include "../Engine/State.h"
#include <string>
#include <vector>

namespace OpenXcom
{
class Window;
class Text;
class TextButton;
class SavedBattleGame;
class RuleTerrain;

namespace VR
{

/// -vrTerrainLab is on the command line.
bool labRequested();

/// How the relief painted on floor tiles (rocks, mounds, craters) is drawn on the table.
enum ReliefMode { RELIEF_FLAT = 0, RELIEF_STEPS = 1, RELIEF_SMOOTH = 2 };
ReliefMode reliefMode();
void setReliefMode(ReliefMode mode);
/// Bumps whenever something changes how parts are built (the table rebuilds its shapes).
unsigned partStyleGeneration();

}

/// The lab's screen: controls on the flat screen, the terrain on the table.
class TerrainLabState : public State
{
public:
	struct LabelAt { int x, y, z; std::string text; };

	TerrainLabState();
	~TerrainLabState() override;

	/// The map the table shows (owned by the lab).
	SavedBattleGame *battle() const { return _battle; }
	/// Changes whenever the map is replaced (the table binds the new one).
	unsigned generation() const { return _generation; }
	/// Names floating over the parts (parts page only).
	const std::vector<LabelAt> &labels() const { return _labels; }

	/// Requests for the table, taken once by Board::update: the darkness to use (-1: none), degrees to turn
	/// the sun by (0: none), the weather to force ("": none).
	int takeShade() { int s = _shadeReq; _shadeReq = -1; return s; }
	float takeSunTurn() { float t = _sunReq; _sunReq = 0.f; return t; }
	std::string takeWeather() { std::string w = _weatherReq; _weatherReq.clear(); return w; }

private:
	struct Entry { std::string label; RuleTerrain *rule; };

	Window *_window;
	Text *_txtTitle, *_txtTerrain, *_txtPage, *_txtInfo;
	TextButton *_btnPrevTerrain, *_btnNextTerrain, *_btnPrevPage, *_btnNextPage;
	TextButton *_btnRelief, *_btnLight, *_btnSun, *_btnWeather, *_btnQuit;

	std::vector<Entry> _terrains;
	int _terrain = 0, _page = 0, _light = 0, _weather = 0;
	SavedBattleGame *_battle = nullptr;
	std::vector<SavedBattleGame*> _retired; // replaced maps, deleted once the table has moved on
	unsigned _generation = 0;
	std::vector<LabelAt> _labels;
	int _shadeReq = -1;
	float _sunReq = 0.f;
	std::string _weatherReq;

	void collectTerrains();
	int pageCount() const;
	/// Builds the map for the current terrain and page and shows it.
	void rebuild();
	void buildParts(RuleTerrain *rule);
	void buildBlock(RuleTerrain *rule, int block);
	void updateTexts();

	void btnPrevTerrainClick(Action *action);
	void btnNextTerrainClick(Action *action);
	void btnPrevPageClick(Action *action);
	void btnNextPageClick(Action *action);
	void btnReliefClick(Action *action);
	void btnLightClick(Action *action);
	void btnSunClick(Action *action);
	void btnWeatherClick(Action *action);
	void btnQuitClick(Action *action);
};

}
