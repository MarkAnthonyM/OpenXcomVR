#pragma once
/*
 * OXCE VR tabletop - what lies on the war table.
 * Battlescape: a voxel diorama of the map with cardboard unit standees.
 * Geoscape: a physical globe hovering over the table.
 */
#include "VrGL.h"
#include <memory>
#include <functional>

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

struct BoardHit
{
	float t = 0.f;
	glm::vec3 point{0.f};
	int tileX = -1, tileY = -1, tileZ = -1;   // battlescape tile under the pointer
	double lon = 0.0, lat = 0.0;              // geoscape point under the pointer
	bool globe = false;
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
	/// True while something is on the table.
	bool hasContent() const;
	/// Rebuilds geometry when the game state changed. Needs the GL context.
	void update(Game *game, float dt);
	void draw(const Shader &sh, const glm::mat4 &viewProj, const glm::vec3 &eye, double time);
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
