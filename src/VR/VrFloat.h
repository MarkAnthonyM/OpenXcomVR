#pragma once
/*
 * OXCE VR tabletop - floating pop-up windows.
 *
 * When the game opens a pop-up (a state that isn't a full screen: messages,
 * confirmations, the options and save menus, research or production reports;
 * also the briefing, debriefing and next-turn screens, and every running
 * dogfight side by side), its picture is projected as a floating window over the table,
 * turned toward the player. It materializes as a projection: a bright line
 * unfolds into the window with rolling scanlines and flicker, and folds back
 * into the line when the pop-up closes.
 *
 * Fingertips press the window's buttons (a touch clicks where it lands); the
 * laser treats the window like the wall screen.
 */
#include "VrGL.h"
#include "VrHands.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace OpenXcom
{
class Game;

namespace VR
{

struct FloatContext
{
	Game *game = nullptr;
	const std::vector<uint32_t> *pixels = nullptr; // current game frame, RGBA
	int surfW = 0, surfH = 0;
	float tableY = 0.f;                             // table surface height
	glm::vec4 table{0.f};                           // playing surface (xz rect)
	bool keyboard = false;                          // the table keyboard is up: float above it
	glm::vec3 eye{0.f, 1.3f, 0.f};                  // the player's head: windows lean to face it
	std::function<void(int, int, int)> clickScreen; // game pixel x, y, SDL button
	std::function<void(int, int)> hover;            // move the game's mouse to a game pixel
	std::function<void(int, int, int, bool)> mouse; // a mouse button down / up at a game pixel (also the wheel: 4 up, 5 down)
	std::function<void(int, float, float)> haptic;  // hand, amplitude, seconds
};

class FloatWindow
{
public:
	FloatWindow();
	~FloatWindow();
	void init();
	void update(const FloatContext &ctx, const Hands &hands, float dt);
	/// Drawn after the table's contents (it covers them) and before the hands.
	void draw(const Shader &sh) const;
	/// Laser: distance along the ray and where it lands as a uv over the whole game frame.
	bool raycast(const glm::vec3 &o, const glm::vec3 &d, float &t, glm::vec2 &gameUV) const;
	bool isOpen() const;
	/// Where the right stick scrolls: the last point a finger was on, or the middle of the window (game pixel).
	bool scrollPoint(int &gx, int &gy) const;
	/// A point (a fingertip) is close in front of an open window: the laser stays off there.
	bool fingerNear(const glm::vec3 &point) const;
	bool isClosed() const;
	/// Test helper: the window point over a game pixel, and the window's normal.
	bool pointAt(float gx, float gy, glm::vec3 &world, glm::vec3 &normal) const;
	std::string status() const;
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
