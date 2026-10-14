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
#include <vector>

namespace OpenXcom
{
class Game;

namespace VR
{
namespace Dash
{

/// Each fills px with a W x H RGBA picture; false when there is no game to show.
bool research(Game *game, std::vector<uint32_t> &px, int W, int H);
bool production(Game *game, std::vector<uint32_t> &px, int W, int H);
bool crafts(Game *game, std::vector<uint32_t> &px, int W, int H);
bool ufos(Game *game, std::vector<uint32_t> &px, int W, int H);

}
}
}
