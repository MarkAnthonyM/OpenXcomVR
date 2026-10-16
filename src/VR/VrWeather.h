#pragma once
/*
 * OXCE VR tabletop - the sky over a battle on the table.
 *
 * Sun: the table's key light stands where the real sun stands over the mission
 * site at landing (the far edge of the table is north), while the game's own
 * darkness level for the mission sets how high it is and its colour: a high
 * white sun by day, a low gold or deep orange one at dusk and dawn, faint blue
 * moonlight at night. The tiles stay exactly as dark as the game makes them.
 *
 * Weather (cosmetic only, decided once per mission from the mission itself, so a
 * reload brings the same weather): snow on polar and mountain maps, rain with
 * lightning on jungle, forest, farmland and city maps, blowing dust in the
 * desert and red sand on Mars, drifting cloud shadows by day and a low mist at
 * dawn. Roofs keep rain and snow out; it all thins out around the hands and the
 * cursor. Base defence and alien base missions are indoors: no sun, no weather.
 * Options::vrWeather turns the weather off (the sun stays).
 */
#include "VrGL.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace OpenXcom
{
class Game;
class SavedBattleGame;

namespace VR
{

/// How the sun (or moon) and the sky light the map, in map voxel space
/// (x east, y up, z south: the map's top edge is north).
struct SkyLight
{
	bool on = false;                 // an outdoor mission: the sun lights the table
	glm::vec3 dir{0.f, 1.f, 0.f};    // towards the sun or moon
	glm::vec3 col{1.f};              // its light
	glm::vec3 sky{0.f};              // light from the whole sky
	float cloud = 0.f;               // cloud shadows: how dark (0 clear sky)
	glm::vec2 cloudOffset{0.f};      // their drift (map voxels)
	float flash = 0.f;               // lightning right now (0..1)
};

/// What the weather needs to know about the table each frame (map voxels).
struct WeatherView
{
	int mx = 0, my = 0;                  // map size in tiles
	float top = 0.f;                     // the highest visible height (the game's view level)
	float ceiling = 0.f;                 // where rain and snow start (a little over the highest roof in view)
	glm::vec2 center{0.f};               // the middle of what the table shows (xz)...
	float radius = 0.f;                  // ...and how far it reaches
	glm::vec3 viewer{0.f};               // the player's head
	std::vector<glm::vec4> fade;         // hands and the cursor (xyz, radius): the weather thins out there
	float light = 1.f;                   // how bright the map is (the game's darkness, 0..1)
	std::function<float(int, int)> stop; // a tile column: the height rain and snow stop at (a roof, the ground)
	std::function<bool(int, int)> open;  // a tile column: explored ground under the open sky (the mist lies there)
};

class Weather
{
public:
	Weather();
	~Weather();
	/// A new battle: works out the sun and rolls the weather for it.
	void begin(Game *game, SavedBattleGame *battle);
	void end();
	void update(float dt, const WeatherView &view);
	/// Rain, snow, dust and mist (board matrix: map voxels -> world). Mist needs uMapInv set.
	void draw(const Shader &sh, const glm::mat4 &M) const;
	const SkyLight &sky() const;
	std::string status() const;
	/// Tests: the weather of the next battle (and the current one, re-rolled): "rain", "snow",
	/// "dust", "sand", "mist", "clear" or "auto" (back to normal).
	static void force(const std::string &kind);
	/// Tests: the sun's direction (degrees from north, clockwise) for the next battles; < 0 back to normal.
	static void forceSun(float azimuth);
	/// Tests: a lightning strike now (rain only).
	void strike();
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
