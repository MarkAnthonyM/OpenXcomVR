#pragma once
/*
 * OXCE VR tabletop - geoscape dashboards for the back-wall monitors.
 *
 * Always on while the geoscape runs: research and production progress on the
 * left monitors, craft status and UFO activity on the right ones. Each is
 * drawn into an RGBA picture with the game's own fonts and texts, straight
 * from the saved game (no game screen has to be open).
 */
#include <cstdint>
#include <string>
#include <vector>

namespace OpenXcom
{
class Game;
class Font;

namespace VR
{
namespace Dash
{

inline uint32_t C(int r, int g, int b) { return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r; }

// the geoscape's look: deep blue panels, gold headings, pale text
const uint32_t BG = C(8, 14, 34), BAND = C(20, 34, 72), LINE = C(40, 70, 120);
const uint32_t GOLD = C(255, 214, 90), TEXT = C(220, 228, 240), DIM = C(130, 150, 180);
const uint32_t GREEN = C(70, 220, 90), CYAN = C(90, 220, 255), AMBER = C(255, 170, 50), RED = C(240, 70, 60);

/// A picture to draw on with the game's fonts (RGBA, filled with BG).
struct Canvas
{
	Game *g;
	int W, H;
	std::vector<uint32_t> &px;
	Font *big = nullptr, *small = nullptr;
	void *pal = nullptr;

	Canvas(Game *game, std::vector<uint32_t> &out, int w, int h);
	bool ok() const { return big && small; }
	std::string tr(const std::string &id) const;
	void rect(int x, int y, int w, int h, uint32_t c);
	/// A one-pixel outline.
	void frame(int x, int y, int w, int h, uint32_t c);
	/// Text in a box w wide starting at x (cut off at its edge); align 0 left, 1 centre, 2 right.
	void text(int x, int y, int w, const std::string &s, uint32_t color, bool isBig = false, int align = 0);
	void title(const std::string &s);
	/// A bar: value of max, filled in color over a dark track.
	void bar(int x, int y, int w, int h, int value, int max, uint32_t color);
};

/// Each fills px with a W x H RGBA picture; false when there is no game to show.
bool research(Game *game, std::vector<uint32_t> &px, int W, int H);
bool production(Game *game, std::vector<uint32_t> &px, int W, int H);
bool crafts(Game *game, std::vector<uint32_t> &px, int W, int H);
bool ufos(Game *game, std::vector<uint32_t> &px, int W, int H);

}
}
}
