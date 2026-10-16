#pragma once
/*
 * OXCE VR tabletop - the sky debug panel on the left wrist (vr-prototype-weather).
 *
 * With Options::vrDebugPanel on, turning the inside of the left wrist toward the
 * eyes during a battle raises a panel along the forearm. Two pages, picked with
 * the tabs at its top and pressed with the right index fingertip:
 *  - INFO: the mission and its terrain, where and when it is, the darkness level
 *    and where it came from, the sun the table uses (direction, height, colour)
 *    next to the real sun, how each weather roll went, live particle and
 *    lightning numbers, the settings and the frame time.
 *  - CONTROLS: force the weather (or back to the mission's own roll), darken or
 *    lighten the table's map, turn the sun or give it back to the real sun, and
 *    strike lightning. These change the table only, never the game: the game's
 *    darkness, what soldiers see and the flat screen are unchanged.
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
class Board;

class WristDebug
{
public:
	WristDebug();
	~WristDebug();
	void update(Game *game, Board *board, const Hands &hands, const glm::vec3 &eye,
		const std::function<void(int, float, float)> &haptic, float dt);
	void draw(const Shader &sh) const;
	std::string status() const;
	/// Tests: hold the panel at a point facing a direction (instead of the wrist); off with pin = false.
	void testPin(bool pin, const glm::vec3 &center = glm::vec3(0.f), const glm::vec3 &normal = glm::vec3(0.f, 0.f, 1.f));
	void testPage(int page);
	/// Tests: press a control by its id (INFO, CONTROLS, AUTO, CLEAR, RAIN, SNOW, DUST, SAND, MIST,
	/// CLOUDS, DARKER, LIGHTER, GAME, TURNL, TURNR, REAL, STRIKE).
	bool testPress(const std::string &id);
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
