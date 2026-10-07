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
	glm::vec3 lightPos[4];
	glm::vec3 lightCol[4];
	glm::vec3 panelPos{0.f, 1.62f, -2.05f};     // default game screen placement
	float panelTiltDeg = 0.f;
	float panelWidth = 1.7f;
};

/// Builds the static X-COM style command center: room shell, consoles, war table.
void buildCommandCenter(MeshData &out, RoomLayout &layout);
/// A stylised controller, in grip space.
void buildController(MeshData &out, bool left);

}
}
