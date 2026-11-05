#pragma once
/*
 * OXCE VR tabletop - the terrain lab (a developer tool, not for players).
 *
 * Started with -vrTerrainLab on the command line (read straight from the arguments, so it is never
 * saved into options.cfg). Instead of the main menu, the table shows a terrain on its own: every part
 * of its terrain sets laid out one per tile with its name, the pieces made of several parts put
 * together (ASSEMBLED), or one of its map blocks as the game builds it (craft and UFO terrains too).
 * The flat screen on the wall holds the lab's controls.
 *
 * Checking (owner's workflow, 0.21.0-terrain.2): point at a tile to see its parts' flat art beside it
 * (the inspect panel; the trigger pins it), A marks the tile for fixing (reasons and a typed note in a
 * window over the table) or unmarks it. Each lab run that marks something gets a folder
 * <user folder>/terrain-lab/<date_time>/ with notes.txt and, per mark, NNN-view.png (the headset view)
 * and NNN-art.png (the inspect panel). Open marks persist in terrain-lab/marks.txt and show as red
 * flags until they are removed.
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
class ToggleTextButton;
class TextEdit;
class SavedBattleGame;
class RuleTerrain;
class MapData;
class MapDataSet;

namespace VR
{

/// -vrTerrainLab is on the command line.
bool labRequested();

/// How the relief painted on floor parts (rocks, mounds, craters) is drawn on the table: voxel steps in
/// missions (owner's choice, terrain.1); the lab can switch to flat or smooth to compare.
enum ReliefMode { RELIEF_FLAT = 0, RELIEF_STEPS = 1, RELIEF_SMOOTH = 2 };
ReliefMode reliefMode();
void setReliefMode(ReliefMode mode);
/// Bumps whenever something changes how parts are built (the table rebuilds its shapes).
unsigned partStyleGeneration();

/// Asks VrSystem to save the current view (the headset's, or the preview's) as a PNG once the mark window
/// has folded away, and a close-up of tile x, y, z from the same direction (closePath empty: none). Both
/// are clean: no lab flags, labels, cards or panels, no hands; corner ticks show the tile.
void requestViewCapture(const std::string &path, const std::string &closePath, int x, int y, int z);
/// Saves the inspect panel of a tile of the lab's map as a PNG (false if there is nothing to show).
bool saveInspectPicture(const std::string &path, int x, int y, int z);
/// Saves the table's shape of a tile against its art (VrCompare.h) as a PNG.
bool saveComparePicture(const std::string &path, int x, int y, int z);
/// Saves the contact sheet of a terrain set (every part against its art) as a PNG; report: its scores as
/// text, worst first. False when the table has no map bound (it needs the palette and voxel shapes).
bool savePartSheet(const MapDataSet *ds, const std::string &path, std::string &report);

}

/// A tile marked for fixing in the terrain lab.
struct LabMark
{
	int id = 0;                 // number within its session (NNN in the file names)
	std::string session;        // the session folder's name (date_time)
	std::string terrain, page;  // e.g. "POLAR", "parts" / "assembled" / "block POLAR03"
	int x = 0, y = 0, z = 0;
	std::string parts;          // what stands on the tile, e.g. "floor POLAR 37; object POLAR 12"
	std::string reasons, note;
};

/// The lab's screen: controls on the flat screen, the terrain on the table.
class TerrainLabState : public State
{
public:
	struct LabelAt { int x, y, z; std::string text; };
	/// A piece of the ASSEMBLED page: where it stands (tiles) and its parts.
	struct Piece { int x0, y0, w, l, h; std::string name; };

	TerrainLabState();
	~TerrainLabState() override;

	/// The map the table shows (owned by the lab).
	SavedBattleGame *battle() const { return _battle; }
	/// Changes whenever the map is replaced (the table binds the new one).
	unsigned generation() const { return _generation; }
	/// Names floating over the parts (parts page only).
	const std::vector<LabelAt> &labels() const { return _labels; }
	/// The pieces on the ASSEMBLED page (empty on the others).
	const std::vector<Piece> &pieces() const { return _pieces; }
	std::string terrainName() const;
	/// "parts", "assembled" or "block <name>".
	std::string pageKey() const;
	/// Open marks (all terrains and pages).
	const std::vector<LabMark> &marks() const { return _marks; }
	/// The parts standing on a tile of the lab's map, named for the notes.
	std::string partsAt(int x, int y, int z) const;
	/// Picture plaques beside the parts / the flat game's view of blocks and pieces (PICTURES key).
	bool pictures() const { return _pictures; }

	/// Requests for the table, taken once by Board::update: the darkness to use (-1: none), degrees to turn
	/// the sun by (0: none), the weather to force ("": none).
	int takeShade() { int s = _shadeReq; _shadeReq = -1; return s; }
	float takeSunTurn() { float t = _sunReq; _sunReq = 0.f; return t; }
	std::string takeWeather() { std::string w = _weatherReq; _weatherReq.clear(); return w; }

	/// A on a tile of the table: mark it (asks why) or, if marked, unmark it.
	void tileAction(int x, int y, int z);
	/// The mark window's OK.
	void addMark(int x, int y, int z, const std::string &reasons, const std::string &note);
	/// The table's lamps (soldiers' light) for the tile the laser points at (hover false: none); called
	/// every frame by Board::update. True when the tiles' light changed.
	bool updateLamps(int hx, int hy, int hz, bool hover);
	/// Contact sheets (SHEETS key, test op `sheets`): every part of each terrain set against its art, one PNG
	/// per set and scores.txt (worst first), in <user folder>/terrain-lab/sheets/<date_time>/. which: "" the
	/// current terrain, "all" every terrain, else a terrain's name. Returns the folder ("" if nothing made).
	std::string makeSheets(const std::string &which);
	/// A terrain set of any terrain, loaded (test op comparepart); null if none has that name.
	MapDataSet *findSet(const std::string &name);

private:
	struct Entry { std::string label; RuleTerrain *rule; };

	Window *_window;
	Text *_txtTitle, *_txtTerrain, *_txtPage, *_txtInfo;
	TextButton *_btnPrevTerrain, *_btnNextTerrain, *_btnPrevPage, *_btnNextPage;
	TextButton *_btnRelief, *_btnLight, *_btnSun, *_btnWeather;
	TextButton *_btnLamps, *_btnStrength, *_btnPictures, *_btnClear, *_btnSheets, *_btnQuit;

	std::vector<Entry> _terrains;
	int _terrain = 0, _page = 0, _light = 0, _weather = 0, _strength = 1, _lamps = 0;
	bool _pictures = false;
	SavedBattleGame *_battle = nullptr;
	std::vector<SavedBattleGame*> _retired; // replaced maps, deleted once the table has moved on
	unsigned _generation = 0;
	std::vector<LabelAt> _labels;
	std::vector<Piece> _pieces;
	int _shadeReq = -1;
	float _sunReq = 0.f;
	std::string _weatherReq;
	// lamps: what the tiles' unit light was last set for
	int _lampKey = -2, _lampsLit = -1;
	// marks
	std::vector<LabMark> _marks;
	std::string _session;       // this run's folder name, made at the first mark ("" until then)
	int _nextId = 1;

	void collectTerrains();
	int pageCount() const;
	/// Builds the map for the current terrain and page and shows it.
	void rebuild();
	void buildParts(RuleTerrain *rule);
	void buildAssembled(RuleTerrain *rule);
	void buildBlock(RuleTerrain *rule, int block);
	/// The floor the terrain's map blocks use most (plain grass, snow, sand...).
	MapData *groundOf(RuleTerrain *rule, int &id, int &set) const;
	void updateTexts();
	void applyWeather();
	// marks on disk
	std::string labFolder() const;
	void loadMarks();
	void saveMarks() const;
	void appendNotes(const std::string &text);

	void btnPrevTerrainClick(Action *action);
	void btnNextTerrainClick(Action *action);
	void btnPrevPageClick(Action *action);
	void btnNextPageClick(Action *action);
	void btnReliefClick(Action *action);
	void btnLightClick(Action *action);
	void btnSunClick(Action *action);
	void btnWeatherClick(Action *action);
	void btnLampsClick(Action *action);
	void btnStrengthClick(Action *action);
	void btnPicturesClick(Action *action);
	void btnClearClick(Action *action);
	void btnSheetsClick(Action *action);
	void btnQuitClick(Action *action);
};

/// The window over the table that asks why a tile is marked.
class LabMarkState : public State
{
public:
	LabMarkState(TerrainLabState *lab, int x, int y, int z, const std::string &parts);
	~LabMarkState() override;
private:
	TerrainLabState *_lab;
	int _x, _y, _z;
	Window *_window;
	Text *_txtTitle, *_txtParts, *_txtNote;
	std::vector<ToggleTextButton*> _reasons;
	TextEdit *_edtNote;
	TextButton *_btnOk, *_btnCancel;
	void btnOkClick(Action *action);
	void btnCancelClick(Action *action);
};

}
