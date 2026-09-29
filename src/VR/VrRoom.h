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
	glm::vec3 tableCenter{0.f, 0.90f, -1.0f};   // center of the table's playing surface
	glm::vec2 tableSize{2.4f, 1.6f};            // playing surface x/z
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
