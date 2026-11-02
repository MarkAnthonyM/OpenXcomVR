#pragma once
/*
 * OXCE VR tabletop - relief for floor parts whose rocks, mounds and holes are only painted.
 *
 * X-COM's floor parts are flat: a rock or a crater is a picture on a flat tile (the collision shape is the
 * same thin slab as plain grass). For the parts named in Resources/VR/terrain-relief.txt (a hand-checked
 * list: which parts, up or down, how high), the table raises or sinks the ground following the painting:
 * the feature is found where the picture differs from the tile's own ground colour, rounded off from its
 * edges, lighter rock a little higher, a darker hole floor a little deeper. Display only: the game's
 * collision, line of fire and movement are untouched.
 */
#include <SDL.h>
#include <cstdint>

namespace OpenXcom
{
class MapData;

namespace VR
{

struct ReliefSpec
{
	int sign = 0;        // +1 rocks / mound, -1 hole
	float height = 0.f;  // peak height or depth in map voxels (24 = one level)
};

/// Grid of the relief over one tile (32 x 32: half a voxel per cell; [y][x], x east, y south).
static const int RELIEF_N = 32;

struct ReliefField
{
	float h[RELIEF_N][RELIEF_N];        // map voxels above (+) or below (-) the floor's top
	uint32_t tex[RELIEF_N][RELIEF_N];   // the painting seen from straight above (RGBA)
};

/// The relief listed for a floor part, or null.
const ReliefSpec *reliefSpec(const MapData *md);
/// Works out a listed floor's heights from its painting. join: sides that carry on into a neighbouring
/// listed part (1 west, 2 east, 4 north, 8 south) are left as they are; the others come down to the floor.
void reliefField(const MapData *md, const ReliefSpec &spec, const SDL_Color *pal, int join, ReliefField &out);

}
}
