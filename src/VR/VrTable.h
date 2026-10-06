#pragma once
/*
 * OXCE VR tabletop - the physical controls built into the war table, and the
 * live wall screens.
 *
 *  - Button bay (near edge, right): the battlescape icon panel. When a battle
 *    starts, hatches open and a spring-loaded button rises for each control.
 *  - Inventory tray (near edge, left): the selected soldier's inventory grid
 *    with small floating voxel models of the items. Pinch an item to move it.
 *  - Finger taps on the map: tap a soldier's head to select, a tile to move.
 *  - Wall screens: soldier stats, minimap and squad roster during a battle.
 */
#include "VrGL.h"
#include "VrHands.h"
#include <memory>
#include <functional>
#include <string>

namespace OpenXcom
{
class Game;

namespace VR
{

struct RoomLayout;
class Board;
class Shader;

struct TableContext
{
	Game *game = nullptr;
	Board *board = nullptr;
	const std::vector<uint32_t> *pixels = nullptr; // current game frame, RGBA
	int surfW = 0, surfH = 0;
	std::function<void(int, int, int)> clickScreen; // game pixel x, y, SDL button
	std::function<void(int, float, float)> haptic;  // hand, amplitude, seconds
	bool handBusy[2] = {false, false};              // hand is grabbing something else
};

struct TableHit
{
	enum Kind { NONE, BUTTON, ITEM, TRAY } kind = NONE;
	float t = 1e9f;
	int index = -1;
	glm::vec3 point{0.f};
};

class Table
{
public:
	Table();
	~Table();
	void init(const RoomLayout &layout);
	/// Table area left for the battle map.
	glm::vec4 mapRect() const;
	/// Runs button physics, item pinching and finger taps. Call after the hands were solved.
	void update(TableContext &ctx, const Hands &hands, float dt);
	/// Solid parts of the table (with holes where the bay is open) and the button caps.
	void colliders(std::vector<Collider> &out) const;
	/// Open hatches the table glass must not cover (world xz rects).
	const std::vector<glm::vec4> &holes() const;
	float surfaceY() const;
	void draw(const Shader &sh) const;
	void drawWalls(const Shader &sh) const;
	/// Laser / mouse.
	bool raycast(const glm::vec3 &o, const glm::vec3 &d, TableHit &hit) const;
	void pointerMove(const TableHit *hit);
	void pointerClick(const TableHit &hit, int button, TableContext &ctx);
	/// One line about what the table is doing (for logs and tests).
	std::string status() const;
	/// Test helpers: where an item floats, and the centre of an inventory cell.
	bool itemCenter(int index, glm::vec3 &world) const;
	bool cellCenter(const std::string &slot, float cx, float cy, glm::vec3 &world) const;
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
