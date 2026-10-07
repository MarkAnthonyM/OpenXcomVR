#pragma once
#include "VrGL.h"

namespace OpenXcom
{
namespace VR
{

/// Material ids understood by kSceneFS.
enum Material
{
	MAT_PLAIN = 0,
	MAT_FLOOR = 1,
	MAT_WALL = 2,
	MAT_HOLOGLASS = 3,
	MAT_LIGHT = 4,
	MAT_CEILING = 5,
	MAT_DISPLAY = 6,
	MAT_GLOSSY = 7,
	MAT_FOG = 9,      // fog of war on the table (8 is the board's textured terrain)
};

/// Where things are in the command center (meters, y up, player starts at the origin facing -Z).
struct RoomLayout
{
	glm::vec3 tableCenter{0.f, 0.90f, -0.8f};   // center of the table's playing surface (near edge 20 cm in front of the player)
	glm::vec2 tableSize{1.9f, 1.2f};            // playing surface x/z
	float tableRim = 0.035f;                    // width of the frame around the playing surface
	/// Scales the table (option vrTableScale), keeping its near edge where it is.
	void scaleTable(float s)
	{
		float nearZ = tableCenter.z + tableSize.y * 0.5f;
		tableSize *= s;
		tableCenter.z = nearZ - tableSize.y * 0.5f;
	}
	// room shell (x0, z0) .. (x1, z1), ceiling height; the back wall (z0) carries the game screens
	float roomX0 = -3.4f, roomX1 = 3.4f, roomZ0 = -2.7f, roomZ1 = 2.2f, roomH = 3.2f;
	glm::vec3 lightPos[4];
	glm::vec3 lightCol[4];
	glm::vec3 panelPos{0.f, 1.62f, -2.66f};     // the game screen, fixed on the back wall
	float panelTiltDeg = 0.f;
	float panelWidth = 2.0f;
	/// Live screens beside the game screen on the back wall (centre, width, height).
	glm::vec3 statsPos{-2.05f, 1.86f, -2.665f};
	glm::vec2 statsSize{1.36f, 0.85f};
	glm::vec3 rosterPos{-2.05f, 0.98f, -2.665f};
	glm::vec2 rosterSize{1.36f, 0.68f};
	glm::vec3 minimapPos{2.05f, 1.62f, -2.665f};
	glm::vec2 minimapSize{1.1f, 1.1f};
};

/// Builds the static X-COM style command center: room shell, consoles, war table.
void buildCommandCenter(MeshData &out, RoomLayout &layout);
/// A stylised controller, in grip space.
void buildController(MeshData &out, bool left);

}
}
