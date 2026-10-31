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
	glm::vec2 wind{1.f, 0.f};        // the way they drift (map xz, unit)
	float wet = 0.f;                 // rain: the ground glistens
	float boost = 1.f;               // the map's sunlit part: how much brighter than the game's darkness (glow)
	float flash = 0.f;               // lightning right now (0..1)
	float moonFloor = 0.f;           // moonlit night: how bright the unlit ground shows (a tile brightness factor)
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
	glm::vec3 viewerFwd{0.f, 0.f, -1.f}; // ...and where it faces (thunder comes from where its strike was)
	float metersPerVoxel = 0.f;          // the table's scale (how far away the table's rain sounds)
	std::vector<glm::vec4> fade;         // hands and the cursor (xyz, radius): the weather thins out there
	float light = 1.f;                   // how bright the map is (the game's darkness, 0..1)
	std::function<float(int, int)> stop; // a tile column: the height rain and snow stop at (a roof, the ground)
	std::function<bool(int, int)> open;  // a tile column: explored ground under the open sky (the mist lies there)
};

/// Everything the left-wrist debug panel shows about the sky (VrDebug).
struct SkyInfo
{
	bool active = false, outdoor = false;
	std::string mission, terrain, sets, indoorWhy;
	bool site = false;
	double lon = 0.0, lat = 0.0;                 // degrees, east and north positive
	int gmtH = 0, gmtM = 0;                      // the game clock
	float localH = 0.f;                          // solar time at the site (hours)
	int shadeGame = 0, shade = 0;                // the game's darkness / what the table shows
	std::string shadeFrom;                       // where the game's darkness came from
	std::string sky;                             // day, dawn, dusk, night, indoor
	float sunAz = 0.f, sunElev = 0.f;            // the table's sun (degrees; azimuth from north, clockwise)
	float realElev = 0.f, realAz = 0.f;          // the real sun at the site (height, direction)
	bool realKnown = false;
	std::string sunFrom;
	glm::vec3 sunCol{1.f}, skyCol{0.f};
	std::string weather, forced;
	std::string rollKind, rollClouds, rollMist;  // how each roll went
	std::string rollStrength;                    // rain/snow: how strong (drizzle .. thunderstorm, flurries .. blizzard)
	float cloud = 0.f;
	bool mist = false;
	int drawn = 0, strikes = 0;
	float nextStrike = -1.f;
	uint32_t seed = 0;
	bool vanilla = false;                        // vrVanillaSky: the pre-0.13 table
	bool moonKnown = false;                      // worked out from the date, time and site
	float moonIllum = 0.f, moonAlt = 0.f, moonAz = 0.f;
	bool moonWaxing = false;
	bool starlit = false;                        // (no longer used: a moon under the horizon is mirrored above it)
	bool moonMirrored = false;                   // night: the moon is down, the table uses it mirrored above the horizon
	float tableMoonAlt = 0.f;                    // night: how high the table's moon stands
	float moonLight = 0.f;                       // night: its light (the phase, never below 0.4)
	std::string skyClock;                        // the date and time the sun and moon are worked out for
	double skyShift = 0.0;                       // the debug panel's shift of that clock from the game's (days)
};

/// Tests: a thunder variant (near or far) as a WAV file.
bool writeThunderWav(const std::string &path, bool nearStrike, int variant);
/// Tests: one of the weather's sound loops as a WAV file: "room" (rain all around, left ear) or "table"
/// (rain on the table) at a strength (0 drizzle .. 2 thunderstorm), or "wind" (a blizzard's).
bool writeWeatherLoopWav(const std::string &path, const std::string &which, int strength);

class Weather
{
public:
	Weather();
	~Weather();
	/// A new battle: works out the sun and rolls the weather for it. shadeOffset: the debug panel's
	/// darkness change (the table only; the rolls keep the game's own darkness).
	void begin(Game *game, SavedBattleGame *battle, int shadeOffset = 0);
	void end();
	void update(float dt, const WeatherView &view);
	/// Rain, snow, dust and mist (board matrix: map voxels -> world). Mist needs uMapInv set.
	void draw(const Shader &sh, const glm::mat4 &M) const;
	const SkyLight &sky() const;
	std::string status() const;
	SkyInfo info() const;
	/// The forced weather ("auto" when none) and sun direction (< 0 when none).
	static std::string forcedWeather();
	static int forcedWeatherStrength();          // rain/snow strength when forced (-1 rolled)
	static float forcedSun();
	/// Tests: the weather of the next battle (and the current one, re-rolled): "rain", "snow",
	/// "dust", "sand", "mist", "clear" or "auto" (back to normal). Rain and snow come steady; forcing the
	/// same again steps through light / steady / heavy; "rain:2" picks one.
	static void force(const std::string &kind);
	/// Tests: the sun's direction (degrees from north, clockwise) for the next battles; < 0 back to normal.
	static void forceSun(float azimuth);
	/// Tests: the date and time the sun and moon are worked out for (day of month, hour, minute; day < 0 =
	/// the game's own clock). The weather roll keeps the game's clock.
	static void forceClock(int day, int hour, int minute);
	/// The debug panel: move the sky's clock (sun and moon only) by this many days from now on (0 = back
	/// to the game's clock); the game's own clock and darkness are untouched.
	static void shiftClock(double days, bool reset = false);
	static double clockShift();
	/// Tests: a lightning strike now (rain only).
	void strike();
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
