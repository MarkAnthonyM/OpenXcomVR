/*
 * OXCE VR tabletop - relief for painted floor parts (see VrRelief.h).
 */
#include "VrRelief.h"
#include "../Engine/FileMap.h"
#include "../Engine/Logger.h"
#include "../Engine/Surface.h"
#include "../Engine/SurfaceSet.h"
#include "../Mod/MapData.h"
#include "../Mod/MapDataSet.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace OpenXcom
{
namespace VR
{

namespace
{

const char *const LIST = "Resources/VR/terrain-relief.txt";

/// "SET id" -> spec, from the list (read once)
const std::map<std::string, ReliefSpec> &table()
{
	static std::map<std::string, ReliefSpec> t;
	static bool loaded = false;
	if (loaded) return t;
	loaded = true;
	const FileMap::FileRecord *rec = FileMap::at(LIST);
	if (!rec) { Log(LOG_INFO) << "[VR] relief: no " << LIST; return t; }
	auto in = rec->getIStream();
	std::string line;
	int n = 0;
	while (in && std::getline(*in, line))
	{
		size_t hash = line.find('#');
		if (hash != std::string::npos) line.resize(hash);
		std::istringstream ss(line);
		std::string set, kind;
		int id;
		float h;
		if (!(ss >> set >> id >> kind >> h)) continue;
		ReliefSpec s;
		s.sign = kind == "tall" ? 0 : (kind == "down" ? -1 : 1);
		s.height = std::max(0.f, std::min(h, 23.f));
		std::transform(set.begin(), set.end(), set.begin(), ::toupper);
		t[set + " " + std::to_string(id)] = s;
		++n;
	}
	Log(LOG_INFO) << "[VR] relief: " << n << " parts listed in " << LIST;
	return t;
}

float lum(const SDL_Color &c) { return (0.299f * c.r + 0.587f * c.g + 0.114f * c.b) / 255.f; }

}

namespace
{
/// the list's entry for a part, or null
const ReliefSpec *listed(const MapData *md)
{
	if (!md || !md->getDataset()) return nullptr;
	MapDataSet *ds = md->getDataset();
	for (size_t i = 0; i < ds->getSize(); ++i)
		if (ds->getObject(i) == md)
		{
			std::string name = ds->getName();
			std::transform(name.begin(), name.end(), name.begin(), ::toupper);
			auto f = table().find(name + " " + std::to_string(i));
			return f != table().end() ? &f->second : nullptr;
		}
	return nullptr;
}
}

const ReliefSpec *reliefSpec(const MapData *md)
{
	static std::map<const MapData*, const ReliefSpec*> cache;
	auto it = cache.find(md);
	if (it != cache.end()) return it->second;
	const ReliefSpec *r = nullptr;
	if (md && md->getObjectType() == O_FLOOR)
	{
		r = listed(md);
		if (r && r->sign == 0) r = nullptr;
	}
	cache[md] = r;
	return r;
}

float shapeHeight(const MapData *md)
{
	if (!md || md->getObjectType() != O_OBJECT) return 0.f;
	const ReliefSpec *r = listed(md);
	return r && r->sign == 0 ? r->height : 0.f;
}

void reliefField(const MapData *md, const ReliefSpec &spec, const SDL_Color *pal, int join, ReliefField &out)
{
	const int N = RELIEF_N;
	for (int y = 0; y < N; ++y)
		for (int x = 0; x < N; ++x) { out.h[y][x] = 0.f; out.tex[y][x] = 0xFF000000u; }
	const Surface *spr = nullptr;
	if (md->getDataset() && md->getDataset()->getSurfaceset())
		spr = static_cast<const SurfaceSet*>(md->getDataset()->getSurfaceset())->getFrame(md->getSprite(0));
	if (!spr) return;
	const int yoff = md->getYOffset();

	// the painting from above: each cell's spot on the floor projected into the sprite (drawn at height 0)
	std::vector<int> idx(N * N, 0);
	for (int j = 0; j < N; ++j)
		for (int i = 0; i < N; ++i)
		{
			float u = (i + 0.5f) * 16.f / N, v = (j + 0.5f) * 16.f / N;
			float sx = 16.f + (u - v);
			float sy = 24.f + (u + v) * 0.5f + yoff;
			int px = std::max(0, std::min(spr->getWidth() - 1, (int)std::floor(sx)));
			int py = (int)std::floor(sy);
			Uint8 c = 0;
			// the diamond's jagged edge: look a pixel or two inwards
			static const int off[][2] = {{0, 0}, {0, -1}, {0, 1}, {-1, 0}, {1, 0}, {0, -2}, {0, 2}};
			for (auto &o : off)
			{
				int qx = px + o[0], qy = py + o[1];
				if (qx < 0 || qy < 0 || qx >= spr->getWidth() || qy >= spr->getHeight()) continue;
				if ((c = spr->getPixel(qx, qy))) break;
			}
			idx[j * N + i] = c;
			const SDL_Color &pc = pal[c];
			out.tex[j][i] = (uint32_t)pc.r | ((uint32_t)pc.g << 8) | ((uint32_t)pc.b << 16) | 0xFF000000u;
		}

	// the tile's own ground: the commonest 16-colour ramp along its rim
	std::map<int, int> rim;
	for (int k = 0; k < N; ++k)
		for (int c : {idx[k], idx[(N - 1) * N + k], idx[k * N], idx[k * N + N - 1]}) if (c) rim[c >> 4]++;
	int ground = -1, best = 0;
	for (auto &kv : rim) if (kv.second > best) { best = kv.second; ground = kv.first; }
	std::vector<float> B(N * N);
	std::vector<float> groundLums;
	for (int k = 0; k < N * N; ++k)
	{
		B[k] = lum(pal[idx[k]]);
		if ((idx[k] >> 4) == ground) groundLums.push_back(B[k]);
	}
	float gB = 0.5f;
	if (!groundLums.empty())
	{
		std::nth_element(groundLums.begin(), groundLums.begin() + groundLums.size() / 2, groundLums.end());
		gB = groundLums[groundLums.size() / 2];
	}

	// the feature: unlike the ground (another ramp, or much lighter / darker), without specks of dither
	std::vector<uint8_t> f(N * N), t(N * N);
	for (int k = 0; k < N * N; ++k) f[k] = idx[k] && ((idx[k] >> 4) != ground || std::fabs(B[k] - gB) > 0.18f);
	auto at = [&](const std::vector<uint8_t> &m, int x, int y) -> int { return (x < 0 || y < 0 || x >= N || y >= N) ? 0 : m[y * N + x]; };
	auto morph = [&](bool grow)
	{
		for (int y = 0; y < N; ++y)
			for (int x = 0; x < N; ++x)
			{
				int s = at(f, x, y) + at(f, x - 1, y) + at(f, x + 1, y) + at(f, x, y - 1) + at(f, x, y + 1);
				t[y * N + x] = grow ? s > 0 : s == 5;
			}
		f.swap(t);
	};
	morph(false); morph(true);  // opening: specks go
	morph(true); morph(false);  // closing: small gaps fill
	{
		std::vector<int> lab(N * N, 0);
		int next = 0;
		for (int s = 0; s < N * N; ++s)
		{
			if (!f[s] || lab[s]) continue;
			std::vector<int> stack{s}, cells;
			lab[s] = ++next;
			while (!stack.empty())
			{
				int c = stack.back(); stack.pop_back(); cells.push_back(c);
				int cx = c % N, cy = c / N;
				const int nb[4][2] = {{cx - 1, cy}, {cx + 1, cy}, {cx, cy - 1}, {cx, cy + 1}};
				for (auto &q : nb)
				{
					if (!at(f, q[0], q[1])) continue;
					int qi = q[1] * N + q[0];
					if (!lab[qi]) { lab[qi] = next; stack.push_back(qi); }
				}
			}
			if (cells.size() < 14) for (int c : cells) f[c] = 0; // under ~3.5 voxels square: dither, not a rock
		}
	}

	// rounded off from the feature's edge (chamfer distance), lighter rock higher / darker hole deeper
	std::vector<float> d(N * N);
	for (int k = 0; k < N * N; ++k) d[k] = f[k] ? 1e6f : 0.f;
	for (int pass = 0; pass < 2; ++pass)
		for (int s = 0; s < N * N; ++s)
		{
			int k = pass ? N * N - 1 - s : s, x = k % N, y = k / N, dir = pass ? 1 : -1;
			if (!f[k]) continue;
			const int nb[4][2] = {{x + dir, y}, {x, y + dir}, {x + dir, y + dir}, {x - dir, y + dir}};
			const float w[4] = {1.f, 1.f, 1.414f, 1.414f};
			for (int q = 0; q < 4; ++q)
			{
				int nx = nb[q][0], ny = nb[q][1];
				float nd = (nx < 0 || ny < 0 || nx >= N || ny >= N) ? 0.f : d[ny * N + nx];
				d[k] = std::min(d[k], nd + w[q]);
			}
		}
	float meanF = 0.f;
	int nf = 0;
	for (int k = 0; k < N * N; ++k) if (f[k]) { meanF += B[k]; ++nf; }
	if (nf) meanF /= nf;
	std::vector<float> h(N * N, 0.f), hb(N * N, 0.f);
	for (int k = 0; k < N * N; ++k)
	{
		if (!f[k]) continue;
		if (spec.sign > 0) h[k] = std::sqrt(d[k]) * 1.6f + (B[k] - meanF) * 3.f;
		else h[k] = std::sqrt(d[k]) * 2.4f + (gB - B[k]) * 4.f;
		h[k] = std::max(0.f, h[k]);
	}
	// a little blur, then back inside the feature
	for (int y = 0; y < N; ++y)
		for (int x = 0; x < N; ++x)
		{
			float s = 0.f, w = 0.f;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					int nx = x + dx, ny = y + dy;
					if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
					float k = (dx || dy) ? ((dx && dy) ? 0.36f : 0.6f) : 1.f;
					s += h[ny * N + nx] * k; w += k;
				}
			hb[y * N + x] = f[y * N + x] ? s / w : 0.f;
		}
	float peak = 0.f;
	for (float v : hb) peak = std::max(peak, v);
	if (peak <= 0.f) return;
	float scale = spec.height / peak * spec.sign;
	// down to the floor at the sides that don't carry on into a neighbouring feature (over 2 voxels)
	const float EDGE = 4.f;
	for (int y = 0; y < N; ++y)
		for (int x = 0; x < N; ++x)
		{
			float e = 1e6f;
			if (!(join & 1)) e = std::min(e, x + 0.5f);
			if (!(join & 2)) e = std::min(e, N - x - 0.5f);
			if (!(join & 4)) e = std::min(e, y + 0.5f);
			if (!(join & 8)) e = std::min(e, N - y - 0.5f);
			float k = std::min(1.f, e / EDGE);
			k = k * k * (3.f - 2.f * k);
			out.h[y][x] = hb[y * N + x] * scale * k;
		}
}

}
}
