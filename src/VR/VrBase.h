#pragma once
/*
 * OXCE VR tabletop - the base on the table.
 *
 * While the base screen (or a screen opened from it) is up, the table shows
 * the base as a voxel relief of the game's own facility pictures: corridors,
 * walls and machinery stand out by brightness, craft sit in their hangars as
 * small voxel models, and facilities under construction stand in scaffolding
 * with the days left floating above them.
 *
 * Fingers: tap a facility to open its screen (the game's right-click action:
 * laboratory -> research, workshop -> manufacture, hangar -> the craft,
 * stores -> sell, quarters -> soldiers ...); hold a finger on it for a second
 * to dismantle it (the game asks to confirm); tap an empty spot to build there.
 * While a facility is being placed, its footprint follows the finger (green
 * where it fits) and a tap places it.
 */
#include "VrGL.h"
#include "VrHands.h"
#include <functional>
#include <memory>
#include <string>

namespace OpenXcom
{
class Game;

namespace VR
{

struct BaseInput
{
	std::function<void(int, int, int)> clickScreen; // game pixel x, y, SDL button
	std::function<void(int, int)> hover;            // move the game's mouse to a game pixel
	std::function<void(int, float, float)> haptic;  // hand, amplitude, seconds
	bool handBusy[2] = {false, false};
};

class BaseDiorama
{
public:
	BaseDiorama();
	~BaseDiorama();
	/// Table area the base may use (world xz rect) and the table's surface height.
	void init(const glm::vec4 &mapRect, float surfaceY);
	/// Follows the game: true while a base is on the table.
	bool update(Game *game, float dt);
	bool active() const;
	void draw(const Shader &sh, const glm::vec3 &eye) const;
	/// Laser: the grid cell under the ray.
	bool raycast(const glm::vec3 &o, const glm::vec3 &d, float &t, glm::vec3 &point, int &cx, int &cy) const;
	void hover(int cx, int cy);
	void hoverNone();
	/// Laser click: left = a tap (open / build / place), right = the hold action (dismantle).
	void click(int button, const BaseInput &in);
	void fingers(const Hands &hands, const BaseInput &in, float dt);
	/// Sliding the base across the table (stick, grip).
	void pan(const glm::vec2 &worldDelta);
	bool contains(const glm::vec3 &world, float margin) const;
	/// Test helpers: the top of a grid cell (world) and a one-line state.
	bool cellTop(int cx, int cy, glm::vec3 &world) const;
	std::string status() const;
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
