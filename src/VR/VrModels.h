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
 * Kinds are MagicaVoxel palette entries: 1 wood (trunk, branches), 2 leaves, 3 fruit, 4 ground patch,
 * 5 plain (0.21.0-terrain.13: takes whatever the sprite draws where it lands, of any colour: wheat, hulls).
 * Any other entry counts by its colour in the file's palette (green leaves, red fruit, else wood), so a
 * model edited with other colours still works.
 *
 * A <SET>_<id>.ref file beside a model (0.21.0-terrain.13) says where its voxels take their colours (see
 * Model::ref) and may lower it (baseY). Floors can have models too (the small scout's cap).
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

enum ModelKind : uint8_t { MK_EMPTY = 0, MK_WOOD = 1, MK_LEAF = 2, MK_FRUIT = 3, MK_PATCH = 4, MK_PLAIN = 5 };

struct Model
{
	int sx = 0, sy = 0, sz = 0;
	float perX = 2.f, perZ = 2.f; // model voxels per table voxel across (x, y) and up (z)
	float baseY = 0.f; // table height layer z = 1 stands on (0: the floor's top; a cap over a dome: below it)
	std::vector<uint8_t> kind; // sx * sy * sz, ModelKind
	/// Where each voxel takes its colour (from <SET>_<id>.ref, 0.21.0-terrain.13; empty: from the part's own
	/// sprite as usual): 0 = as usual, else REF_SET | part << 16 | sprite row << 8 | column: that pixel of
	/// another (or the same) part of the set. Lets a model cut from a bigger shape (the small scout's hull)
	/// take its colours from wherever the art shows that spot, e.g. the far side's outer hull from the near
	/// side's sprite. References only, never colours: the pixels are the player's own.
	std::vector<uint32_t> ref;
	static const uint32_t REF_SET = 1u << 24;
	uint8_t at(int x, int y, int z) const
	{
		if (x < 0 || y < 0 || z < 0 || x >= sx || y >= sy || z >= sz) return MK_EMPTY;
		return kind[((size_t)z * sy + y) * sx + x];
	}
};

/// The hand-built model of part `id` of terrain set `set`, or null (read once, then cached). A part may
/// have versions (0.21.0-terrain.13: the wheat), <SET>_<id>-2.vox, -3 ...: `variant` 0 is the plain file,
/// 1 the -2 file and so on; the table picks one per tile by its position (VrBoard tileKey).
const Model *terrainModel(const std::string &set, int id, int variant = 0);
/// How many versions part `id` of `set` has (1 with only the plain file, 0 with none).
int modelVariants(const std::string &set, int id);

/// Reads a MagicaVoxel file's first model into m (kinds as above); false if it isn't one.
bool parseVox(const std::vector<uint8_t> &bytes, Model &m);

}
}
