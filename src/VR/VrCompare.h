#pragma once
/*
 * OXCE VR tabletop - comparing the table's terrain shapes with the game's art (terrain lab, 0.21.0-terrain.6).
 *
 * A small software renderer: it draws a shape the table built (a Proto: triangles in voxel units, X east,
 * Y up, Z south, textured from an atlas) from any orthographic direction into a picture, four samples by
 * four per pixel. From the flat game's own angle (isoView) a voxel step is one pixel sideways, exactly as
 * the sprites are drawn, so the picture can be laid over the part's sprite pixel for pixel: diff() says
 * where the table has something the art doesn't (red), where the art has something the table doesn't
 * (blue), and a shape score. A mismatch next to a pixel of the other kind is the projection's one-pixel
 * stair-step at a slanted edge: drawn dim and not counted.
 *
 * Pure CPU, no GL and no game state: the lab's contact sheets and the inspect panel use it.
 */
#include "VrGL.h"
#include <cstdint>
#include <vector>

namespace OpenXcom
{
namespace VR
{
namespace Compare
{

/// An orthographic view: picture x = ox + dot(ax, p), y = oy + dot(ay, p); nearer = larger dot(az, p).
struct View
{
	glm::vec3 ax, ay, az;
	float ox, oy;
};

/// The flat game's projection (a tile's sprite: x = 16 + X - Z, y = 24 + (X + Z) / 2 - Y), moved by dx, dy.
View isoView(float dx, float dy);
/// Looking at the tile's centre from an azimuth (degrees, 0 = from the south, 90 = from the east; the flat
/// game looks from 45) and an elevation, s pixels per voxel; the tile's centre on the ground lands at cx, cy.
View lookView(float azimuthDeg, float elevationDeg, float s, float cx, float cy);

/// A rendered picture: RGBA (alpha 255 where drawn, 0 where empty) and how much of each pixel is covered.
struct Image
{
	int w = 0, h = 0;
	std::vector<uint32_t> px;
	std::vector<uint8_t> cover; // 0..255
	bool covered(int x, int y) const { return cover[(size_t)y * w + x] >= 128; }
};

/// Draws the triangles (texture RGBA texW x texH, texels with alpha 0 left out like the table's cards).
/// shade: light the faces from above and in front of the eye (the look views); off: the texture's own
/// colours (the comparison with the art).
void render(const std::vector<Vertex> &verts, const std::vector<uint32_t> &idx, const uint32_t *tex, int texW, int texH,
	const View &view, int w, int h, bool shade, Image &out);

struct Score
{
	int both = 0;      // pixels both have
	int tableOnly = 0; // the table has, the art hasn't (not counting one-pixel edges)
	int artOnly = 0;   // the art has, the table hasn't
	int art = 0;       // pixels of art
	/// Shared pixels over everything either has (edges left out), 0..100; 100 when both are empty.
	int shape() const { int u = both + tableOnly + artOnly; return u ? (int)(100.0 * both / u + 0.5) : 100; }
	int tablePct() const { return art ? (int)(100.0 * tableOnly / art + 0.5) : (tableOnly ? 100 : 0); }
	int artPct() const { return art ? (int)(100.0 * artOnly / art + 0.5) : 0; }
};

/// Compares a table picture with the art (RGBA, same size, alpha 0 = empty). diffPx: the art in grey,
/// table-only pixels red, art-only pixels blue (one-pixel edges dim), nothing in the background colour bg.
Score diff(const Image &table, const std::vector<uint32_t> &art, uint32_t bg, std::vector<uint32_t> &diffPx);

}
}
}
