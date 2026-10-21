#pragma once
/*
 * OXCE VR tabletop - the motion scanner in your hand.
 *
 * When a soldier uses a motion scanner, the game opens its scanner screen (the
 * time units are spent exactly as in the flat game). A handheld scanner then
 * appears in the player's hand: the device drawn on that screen (DETBORD.PCK),
 * made solid: a slab with a raised frame, its screens set in and its four
 * buttons standing out.
 *
 * - The big screen is a camera. It looks out of the back of the device and
 *   shows the table in the radar's green, at the radar's own 152 x 152 pixels.
 *   Every couple of seconds a ping goes out from the soldier who used the
 *   scanner and spreads to the edge of the game's range (the same 19 x 19
 *   tiles), lighting up each movement contact as it passes: a ring on the
 *   ground at the contact's tile (any floor: like the game, it doesn't say
 *   which). The picture then fades before the next ping. Contacts out of view
 *   get an arrow at the screen's edge.
 * - The long bar at the top of the side panel is a compass strip: where the
 *   camera points, on the map's 8 directions.
 * - The tall side display shows the game's own radar picture, small (north up,
 *   or turning with the device), and a readout: how many contacts, how far and
 *   which way the nearest one is.
 * - Buttons, pressed with the other hand's index finger: red closes it (letting
 *   go never does), blue and yellow zoom the camera in and out, green switches
 *   the side radar between north-up and turning.
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
class BattleUnit;

namespace VR
{

struct ScannerContext
{
	Game *game = nullptr;
	const std::vector<uint32_t> *pixels = nullptr; // current game frame, RGBA
	int surfW = 0, surfH = 0;
	glm::vec2 north{0.f, -1.f}, east{1.f, 0.f};     // the battle map's directions on the table (world xz)
	glm::vec3 eye{0.f, 1.3f, 0.f};
	glm::vec3 headFwd{0.f, 0.f, -1.f};              // where the head faces (world): sounds come from the device
	std::function<glm::vec3(int, int, int)> tileCenter; // world position of a map tile's floor centre
	std::function<glm::vec3(const glm::vec3 &)> voxelToWorld; // a map voxel (x, y, height) on the table
	std::function<void(int, int, int)> clickScreen; // game pixel x, y, SDL button
	std::function<void(int, float, float)> haptic;  // hand, amplitude, seconds
};

/// Tests: the scanner's sounds as WAV files (prefix + name + .wav).
bool writeScannerSounds(const std::string &prefix);

class HandScanner
{
public:
	HandScanner();
	~HandScanner();
	void update(const ScannerContext &ctx, const Hands &hands, float dt);
	void draw(const Shader &sh) const;
	bool isOpen() const;
	std::string status() const;
	/// The camera: false when there is nothing to film this frame (closed, or every other frame).
	bool feedCamera(glm::mat4 &view, glm::mat4 &proj, glm::vec3 &eye);
	/// Binds the camera's picture as the render target; endFeed resolves it into the screen's texture.
	bool beginFeed();
	/// The unit whose figure the camera mustn't draw (soldier view: it looks out of its eyes), or null.
	BattleUnit *feedHides() const;
	void endFeed();
	/// The scene shader's uniforms for the camera pass (green picture, ping, contact rings); the overlay
	/// (arrows to contacts out of view) is drawn after the scene, still into the camera's picture.
	void feedUniforms(const Shader &sh) const;
	void feedOverlay(const Shader &sh) const;
	/// Tests: press a button (as the other hand would): "red" (close), "blue", "yellow", "green".
	bool testPress(const std::string &button = "red");
	/// Tests: the three screens side by side as a PNG.
	bool dump(const std::string &path) const;
	/// Tests: hold the opening or closing animation at this many seconds (below 0: run again).
	void freeze(float t);
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
