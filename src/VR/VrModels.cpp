/*
 * OXCE VR tabletop - hand-built terrain models (see VrModels.h).
 */
#include "VrModels.h"
#include "../Engine/FileMap.h"
#include "../Engine/Logger.h"
#include <cstring>
#include <iterator>
#include <map>
#include <memory>

namespace OpenXcom
{
namespace VR
{

static uint32_t u32(const std::vector<uint8_t> &b, size_t o)
{
	return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

bool parseVox(const std::vector<uint8_t> &b, Model &m)
{
	if (b.size() < 20 || std::memcmp(b.data(), "VOX ", 4) != 0 || std::memcmp(b.data() + 8, "MAIN", 4) != 0) return false;
	// MAIN's children: SIZE, XYZI (the first model), RGBA and others (skipped)
	size_t o = 20 + u32(b, 12);
	size_t end = std::min(b.size(), o + (size_t)u32(b, 16));
	bool haveSize = false, haveVox = false;
	std::vector<uint8_t> idx; // colour index per cell, as read
	uint32_t pal[256];
	bool havePal = false;
	while (o + 12 <= end)
	{
		std::string id(reinterpret_cast<const char*>(&b[o]), 4);
		size_t n = u32(b, o + 4), kids = u32(b, o + 8), c = o + 12;
		if (c + n > b.size()) return false;
		if (id == "SIZE" && !haveSize && n >= 12)
		{
			m.sx = (int)u32(b, c); m.sy = (int)u32(b, c + 4); m.sz = (int)u32(b, c + 8);
			if (m.sx <= 0 || m.sy <= 0 || m.sz <= 0 || m.sx > 256 || m.sy > 256 || m.sz > 256) return false;
			idx.assign((size_t)m.sx * m.sy * m.sz, 0);
			haveSize = true;
		}
		else if (id == "XYZI" && haveSize && !haveVox && n >= 4)
		{
			size_t cnt = u32(b, c);
			if (4 + cnt * 4 > n) return false;
			for (size_t i = 0; i < cnt; ++i)
			{
				const uint8_t *v = &b[c + 4 + i * 4];
				if (v[0] < m.sx && v[1] < m.sy && v[2] < m.sz) idx[((size_t)v[2] * m.sy + v[1]) * m.sx + v[0]] = v[3];
			}
			haveVox = true;
		}
		else if (id == "RGBA" && n >= 1024)
		{
			for (int i = 0; i < 256; ++i) pal[i] = u32(b, c + i * 4); // entry i holds colour index i + 1
			havePal = true;
		}
		o = c + n + kids;
	}
	if (!haveVox) return false;
	// colour index -> kind: 1..4 are the kinds; others by their colour in the file's palette
	uint8_t kindOf[256];
	kindOf[0] = MK_EMPTY;
	for (int i = 1; i < 256; ++i)
	{
		if (i <= 4) { kindOf[i] = (uint8_t)i; continue; }
		uint32_t c = havePal ? pal[i - 1] : 0u;
		int r = c & 255, g = (c >> 8) & 255;
		kindOf[i] = g > r * 1.15f ? MK_LEAF : (r > g * 1.6f && r > 90 ? MK_FRUIT : MK_WOOD);
	}
	m.kind.resize(idx.size());
	for (size_t i = 0; i < idx.size(); ++i) m.kind[i] = kindOf[idx[i]];
	return true;
}

const Model *terrainModel(const std::string &set, int id)
{
	static std::map<std::string, std::unique_ptr<Model>> cache;
	std::string key = set + "_" + std::to_string(id);
	auto it = cache.find(key);
	if (it != cache.end()) return it->second.get();
	std::unique_ptr<Model> &slot = cache[key];
	const std::string path = "Resources/VR/models/" + key + ".vox";
	if (!FileMap::fileExists(path)) return nullptr; // most parts have none (FileMap::at would stop the game)
	const FileMap::FileRecord *rec = FileMap::at(path);
	if (!rec) return nullptr;
	auto in = rec->getIStream();
	if (!in) return nullptr;
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(*in)), std::istreambuf_iterator<char>());
	std::unique_ptr<Model> m(new Model);
	if (!parseVox(bytes, *m)) { Log(LOG_WARNING) << "[VR] model " << path << ": not a MagicaVoxel file"; return nullptr; }
	Log(LOG_INFO) << "[VR] model " << path << ": " << m->sx << "x" << m->sy << "x" << m->sz;
	slot = std::move(m);
	return slot.get();
}

}
}
