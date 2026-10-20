#pragma once
/*
 * OXCE VR tabletop - what lies on the war table.
 * Battlescape: a voxel diorama of the map with cardboard unit standees.
 * Geoscape: a physical globe hovering over the table.
 */
#include "VrGL.h"
#include "VrHands.h"
#include "VrWeather.h"
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace OpenXcom
{
class Game;
class BattleUnit;
class BattlescapeState;
class SavedBattleGame;

namespace VR
{

struct RoomLayout;
class Shader;

/// The sky over an outdoor battle on the table (world space).
struct BoardSky
{
	bool on = false;
	glm::vec3 dir{0.f, 1.f, 0.f};   // towards the sun or moon
	glm::vec3 col{1.f};             // its light
	glm::vec3 sky{0.f};             // the sky's light
	glm::vec4 cloud{0.f};           // cloud shadows: strength, scale (per map voxel), drift (map voxels)
	glm::vec4 cloud2{1.f, 0.f, 4.f, 1.5f}; // their wind direction (map xz), voxel size (map voxels), stretch along the wind
	glm::mat4 mapInv{1.f};          // world -> map voxels
	glm::vec4 area{0.f};            // the map on the table (world xz rect)
	float flash = 0.f;              // lightning
	float mapShade = 0.f;           // the map's darkness as a brightness factor (tiles brighter carry the game's own light)
	float moonFloor = 0.f;          // moonlit night: the unlit ground shows at least this bright under the moon
	float wet = 0.f;                // rain: the ground glistens
	float boost = 1.f;              // the map's sunlit part: how much brighter than the game's darkness
};

struct BoardHit
{
	float t = 0.f;
	glm::vec3 point{0.f};
	int tileX = -1, tileY = -1, tileZ = -1;   // battlescape tile under the pointer
	double lon = 0.0, lat = 0.0;              // geoscape point under the pointer
	bool globe = false;
	bool base = false;                        // the base on the table: tileX, tileY = grid cell
};

class Board
{
public:
	Board();
	~Board();
	void init(const RoomLayout &layout);
	/// Where the player's head is (standees turn to face it).
	void setViewer(const glm::vec3 &head);
	/// Lets the board click a point of the flat game screen (game pixel coords, SDL button).
	std::function<void(int, int, int)> clickScreen;
	/// Moves the game's mouse to a point of the flat game screen (game pixel coords).
	std::function<void(int, int)> hoverScreen;
	/// The base screen: its base is on the table (fingers tap, hold and place facilities there).
	bool baseShown() const;
	void baseFingers(const Hands &hands, const bool handBusy[2], std::function<void(int, float, float)> haptic, float dt);
	bool baseCellTop(int cx, int cy, glm::vec3 &world) const;
	std::string baseStatus() const;
	/// True while something is on the table.
	bool hasContent() const;
	/// Rebuilds geometry when the game state changed. Needs the GL context.
	void update(Game *game, float dt);
	void draw(const Shader &sh, const glm::mat4 &viewProj, const glm::vec3 &eye, double time);
	/// Rain, snow and mist over an outdoor battle (and the debug panel's sky marker): see-through, drawn after
	/// the room's solid parts.
	void drawWeather(const Shader &sh) const;
	bool raycast(const glm::vec3 &o, const glm::vec3 &d, BoardHit &hit) const;
	/// Table area (world xz rect: xmin, zmin, xmax, zmax) the battle map is shown in.
	void setMapRect(const glm::vec4 &rect);
	/// The live battlescape, or null.
	BattlescapeState *battleState() const;
	SavedBattleGame *battle() const;
	/// Heads of the figures on the table (world), for finger taps.
	struct UnitMarker { BattleUnit *unit; glm::vec3 head; float radius; bool ours; };
	std::vector<UnitMarker> unitMarkers() const;
	/// Map tile under a world point (highest visible floor at or below it). floorY = its world height.
	bool tileUnder(const glm::vec3 &world, int &tx, int &ty, int &tz, float &floorY) const;
	std::string debugInfo() const;
	/// Continuous map tile coordinates (x, y) under a world point.
	bool tileCoords(const glm::vec3 &world, glm::vec2 &tile) const;
	/// Shows which way the selected soldier would turn (tile-space direction) while a finger drags.
	void setTurnPreview(bool on, const glm::vec2 &dir);
	/// A fingertip hovers over a map tile: the game-style cursor follows it (call every frame).
	void fingerHover(int tx, int ty, int tz);
	/// Clicks a map tile (move / fire / select, exactly like clicking it on the flat map).
	void clickTile(int tx, int ty, int tz, bool right);
	/// Selects one of our units directly.
	bool selectUnit(BattleUnit *unit);
	/// World position of a tile's floor centre.
	glm::vec3 tileCenter(int tx, int ty, int tz) const;
	void hover(const BoardHit &hit);
	void hoverNone();
	void click(int button);
	void wheel(int dir);
	/// Stick / trackpad movement: slide the view across the map by a table-space distance (xz, metres),
	/// turn it, zoom it. On the geoscape the same calls spin and zoom the globe.
	void pan(const glm::vec2 &worldDelta);
	void rotate(float radians);
	void zoom(float factor);
	/// Whole-map overview for the wrist pad and the wall screen: S x S RGBA, turned like the table when
	/// align is set. uv in [-0.5, 0.5] (up = +v) maps back to a tile; centerOnTile moves the table there.
	bool renderMinimap(std::vector<uint32_t> &px, int S, bool align);
	glm::vec2 minimapTile(const glm::vec2 &uv, bool align) const;
	void centerOnTile(const glm::vec2 &tile);
	/// Test helper: an explosion effect at a tile (no damage, effect only).
	void testBurst(int tx, int ty, int tz, bool big);
	/// Lighting: draw only what casts shadows; this frame's flashes (world position, colour);
	/// whether aliens are in sight or it is their turn; how dark the mission is (0 day .. 1 night).
	/// The alien turn: the player can't move the map; the hidden movement curtain (0 open .. 1 covered).
	bool viewLocked() const;
	float curtain() const;
	void setShadowPass(bool on, const glm::vec3 &light = glm::vec3(0.f));
	/// Drawing for the motion scanner's camera: the map and figures without the table's markers (cursor,
	/// selection, path, waypoints, readouts).
	void setFeedPass(bool on);
	void lights(std::vector<std::pair<glm::vec3, glm::vec3>> &out) const;
	bool alert() const;
	float nightLevel() const;
	/// The sun or moon, sky, clouds and lightning of an outdoor battle (VrWeather); false indoors or off the battle.
	bool sky(BoardSky &out) const;
	/// Hands over the table (world): rain, snow, dust and mist thin out around them.
	void setHandPoints(const std::vector<glm::vec3> &pts);
	std::string weatherStatus() const;
	/// Tests: force the weather ("rain", "snow", "dust", "sand", "mist", "clouds", "clear", "auto"), a lightning
	/// strike, the mission's darkness (the game's global shade, relit).
	void forceWeather(const std::string &kind);
	void testStrike();
	void testShade(int shade);
	/// The left-wrist debug panel: turn the sun (degrees, then it stays set by hand) or give it back
	/// to the real sun; darken or lighten the table's map (the game's own darkness is unchanged).
	void rotateSun(float degrees);
	void realSun();
	void setShadeOffset(int offset);
	/// vrVanillaSky changed: the sky is worked out again (vanilla: the pre-0.13 table).
	void skyChanged();
	/// The debug panel's sky clock: move the time the sun and moon are worked out for (days; reset = the game's).
	void shiftSkyClock(double days, bool reset = false);
	int shadeOffset() const;
	SkyInfo skyInfo() const;
	/// The geoscape dome as a sphere (world centre, radius); false when no globe is shown.
	bool globeSphere(glm::vec3 &center, float &radius) const;
	bool canGrab(const glm::vec3 &handWorld) const;
	void beginGrab(int hand, const glm::vec3 &handWorld);
	void updateGrab(int hand, const glm::vec3 &handWorld);
	void endGrab(int hand);
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
