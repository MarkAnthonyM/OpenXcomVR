#pragma once
/*
 * OXCE VR tabletop - hand-built terrain models (0.21.0-terrain.7).
 *
 * A few terrain parts the table can't shape well from their collision voxels and art (trees first) have
 * a model made by hand: bin/common/Resources/VR/models/<SET>_<id>.vox (MagicaVoxel's format), e.g.
 * CULTIVAT_9.vox. Models hold shapes only, never the game's pictures: each voxel says what kind of thing
 * it is, and the table colours it from the player's own sprite when it builds the tile (VrBoard).
 *
 * Layout: x east, y south, z up; two model voxels per table voxel (32 across a tile); the model's x, y
 * centre is the tile's centre (models may be wider than a tile: crowns overhang their neighbours).
 * Layer z = 0 is paint: drawn flat on the floor's top (the grass patch drawn under a tree). Layers z >= 1
 * are solid and stand on the floor's top, half a table voxel each.
 *
 * Kinds are MagicaVoxel palette entries: 1 wood (trunk, branches), 2 leaves, 3 fruit, 4 ground patch.
 * Any other entry counts by its colour in the file's palette (green leaves, red fruit, else wood), so a
 * model edited with other colours still works.
 *
 * Display only: the game's collision, line of fire and movement still use the part's own voxels.
 */
#include <cstdint>
#include <string>
#include <vector>

namespace OpenXcom
{
namespace VR
{

enum ModelKind : uint8_t { MK_EMPTY = 0, MK_WOOD = 1, MK_LEAF = 2, MK_FRUIT = 3, MK_PATCH = 4 };

struct Model
{
	int sx = 0, sy = 0, sz = 0;
	float perX = 2.f, perZ = 2.f; // model voxels per table voxel across (x, y) and up (z)
	std::vector<uint8_t> kind; // sx * sy * sz, ModelKind
	uint8_t at(int x, int y, int z) const
	{
		if (x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz) return MK_EMPTY;
		return kind[((size_t)z * sy + y) * sx + x];
	}
};

/// The hand-built model of part `id` of terrain set `set`, or null (read once, then cached).
const Model *terrainModel(const std::string &set, int id);

/// Reads a MagicaVoxel file's first model into m (kinds as above); false if it isn't one.
bool parseVox(const std::vector<uint8_t> &bytes, Model &m);

}
}
