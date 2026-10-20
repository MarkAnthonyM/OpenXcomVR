#pragma once
/*
 * OXCE VR tabletop - the motion scanner in your hand.
 *
 * When a soldier uses a motion scanner, the game opens its scanner screen (the
 * time units are spent exactly as in the flat game). Instead of only showing it
 * on the wall, a handheld scanner appears in the player's hand: a voxel model of
 * the game's own scanner item art, held by its grip, with the game's live
 * scanner picture on its screen.
 *
 * The picture turns with the device, like a motion tracker: whatever part of the
 * table it points at shows at the top of its screen. The scan is still the
 * game's: centred on the soldier who used it, the same 19 x 19 tiles, the same
 * blips; only its orientation follows the hand. The whole square stays on the
 * round screen at every angle.
 *
 * It closes with the button on its left side, pressed with the other hand's
 * index finger (letting go never closes it).
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

struct ScannerContext
{
	Game *game = nullptr;
	const std::vector<uint32_t> *pixels = nullptr; // current game frame, RGBA
	int surfW = 0, surfH = 0;
	glm::vec2 north{0.f, -1.f}, east{1.f, 0.f};     // the battle map's directions on the table (world xz)
	glm::vec3 eye{0.f, 1.3f, 0.f};
	std::function<void(int, int, int)> clickScreen; // game pixel x, y, SDL button
	std::function<void(int, float, float)> haptic;  // hand, amplitude, seconds
};

class HandScanner
{
public:
	HandScanner();
	~HandScanner();
	void update(const ScannerContext &ctx, const Hands &hands, float dt);
	void draw(const Shader &sh) const;
	bool isOpen() const;
	std::string status() const;
	/// Tests: press the close button (as the other hand would).
	bool testPress();
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
