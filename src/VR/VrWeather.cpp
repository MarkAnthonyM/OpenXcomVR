/*
 * OXCE VR tabletop - the sky over a battle on the table (see VrWeather.h).
 */
#include "VrWeather.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/Logger.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/GameTime.h"
#include "../Savegame/Base.h"
#include "../Savegame/Craft.h"
#include "../Savegame/Ufo.h"
#include "../Savegame/MissionSite.h"
#include "../Savegame/AlienBase.h"
#include "../Mod/MapDataSet.h"
#include "../Mod/Mod.h"
#include "../Mod/AlienDeployment.h"
#include "../Mod/RuleUfo.h"
#include <SDL_mixer.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace OpenXcom
{
namespace VR
{

namespace
{
const float TILE_W = 16.f;
const float PI = 3.14159265f;
std::string forced = "auto";
float forcedAz = -1.f;
int clockDay = -1, clockHour = 0, clockMinute = 0;

/// Days since J2000.0 (2000-01-01 12:00 UT).
double daysJ2000(int y, int mo, int d, double hours)
{
	int a = (14 - mo) / 12, yy = y + 4800 - a, mm = mo + 12 * a - 3;
	long jdn = d + (153 * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
	return jdn + (hours - 12.0) / 24.0 - 2451545.0;
}

/// The moon over a place (low-precision orbit, good to a degree or two): altitude and azimuth (degrees,
/// azimuth from north clockwise), the lit fraction of its disc and whether it is waxing.
void moonAt(double days, double latDeg, double lonEastDeg, float &alt, float &az, float &illum, bool &waxing)
{
	const double R = 3.14159265358979 / 180.0;
	double L = 218.316 + 13.176396 * days, M = 134.963 + 13.064993 * days, F = 93.272 + 13.229350 * days;
	double lam = (L + 6.289 * std::sin(M * R)) * R, beta = 5.128 * std::sin(F * R) * R;
	double g = 357.529 + 0.98560028 * days, q = 280.459 + 0.98564736 * days;
	double lamS = (q + 1.915 * std::sin(g * R) + 0.020 * std::sin(2 * g * R)) * R;
	double eps = (23.439 - 0.0000004 * days) * R;
	double ra = std::atan2(std::sin(lam) * std::cos(eps) - std::tan(beta) * std::sin(eps), std::cos(lam));
	double dec = std::asin(std::sin(beta) * std::cos(eps) + std::cos(beta) * std::sin(eps) * std::sin(lam));
	double gmst = (280.46061837 + 360.98564736629 * days) * R;
	double ha = gmst + lonEastDeg * R - ra;
	double la = latDeg * R;
	alt = (float)(std::asin(std::sin(la) * std::sin(dec) + std::cos(la) * std::cos(dec) * std::cos(ha)) / R);
	double a = std::atan2(-std::cos(dec) * std::sin(ha), std::sin(dec) * std::cos(la) - std::cos(dec) * std::cos(ha) * std::sin(la)) / R;
	az = (float)std::fmod(a + 360.0, 360.0);
	double el = lam - lamS;
	illum = (float)((1.0 - std::cos(el)) * 0.5);
	waxing = std::sin(el) > 0.0;
}

enum Kind { NONE, RAIN, SNOW, DUST, SAND };
const char *kindName(int k)
{
	static const char *n[] = {"none", "rain", "snow", "dust", "sand"};
	return n[glm::clamp(k, 0, 4)];
}

struct Rng
{
	uint32_t s;
	explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1) {}
	float f() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) / 16777216.f; }
	float r(float a, float b) { return a + (b - a) * f(); }
};

uint32_t fnv(uint32_t h, const void *data, size_t n)
{
	const unsigned char *p = (const unsigned char*)data;
	for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
	return h;
}

glm::vec3 lonLatToUnit(double lon, double lat)
{
	// latitude is positive towards the south in OpenXcom (as on the table's globe)
	return glm::vec3((float)(std::cos(lat) * std::sin(lon)), (float)(-std::sin(lat)), (float)(std::cos(lat) * std::cos(lon)));
}

/// Where the battle is on the globe: the UFO, mission site or alien base being fought over, else the craft.
bool missionSite(SavedGame *sg, double &lon, double &lat)
{
	if (!sg) return false;
	auto take = [&](const Target *t) { lon = t->getLongitude(); lat = t->getLatitude(); return true; };
	for (Ufo *u : *sg->getUfos()) if (u->isInBattlescape()) return take(u);
	for (MissionSite *m : *sg->getMissionSites()) if (m->isInBattlescape()) return take(m);
	for (AlienBase *b : *sg->getAlienBases()) if (b->isInBattlescape()) return take(b);
	for (Base *b : *sg->getBases())
	{
		if (b->isInBattlescape()) return take(b);
		for (Craft *c : *b->getCrafts()) if (c->isInBattlescape()) return take(c);
	}
	return false;
}

/// The sun by the game's darkness (0..11): how high (degrees) and what colour.
const float SUN_ELEV[12] = {62, 57, 51, 46, 40, 35, 30, 25, 20, 17, 14, 12};
const glm::vec3 SUN_COL[12] = {
	{1.00f, 0.97f, 0.92f}, {1.00f, 0.96f, 0.89f}, {1.00f, 0.94f, 0.85f}, {1.00f, 0.91f, 0.78f}, // high white
	{1.00f, 0.86f, 0.66f}, {1.00f, 0.80f, 0.56f}, {1.00f, 0.74f, 0.48f}, {1.00f, 0.68f, 0.42f}, {1.00f, 0.62f, 0.36f}, // low gold
	{1.00f, 0.55f, 0.30f}, {1.00f, 0.49f, 0.26f}, {0.96f, 0.43f, 0.23f}}; // deep orange

/// How much the game's darkness dims a tile (as the board's shadeFactor).
float shadeLight(int shade)
{
	static const float k[16] = {1.f, 0.804f, 0.639f, 0.506f, 0.397f, 0.306f, 0.232f, 0.172f,
		0.129f, 0.091f, 0.062f, 0.041f, 0.028f, 0.020f, 0.015f, 0.013f};
	return k[glm::clamp(shade, 0, 15)];
}

/// A box along an axis (xz direction d), centre c, half sizes (along, up, across).
void orientedBox(MeshData &md, const glm::vec3 &c, const glm::vec2 &d, const glm::vec3 &h, const glm::vec4 &col)
{
	glm::vec3 a(d.x * h.x, 0.f, d.y * h.x), u(0.f, h.y, 0.f), s(-d.y * h.z, 0.f, d.x * h.z);
	glm::vec3 p[8] = {c - a - u - s, c + a - u - s, c + a + u - s, c - a + u - s,
		c - a - u + s, c + a - u + s, c + a + u + s, c - a + u + s};
	static const int f[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
	for (auto &q : f) md.addQuad(p[q[0]], p[q[1]], p[q[2]], p[q[3]], col, 0.f);
}

/// Thunder, made once: a crack and a long low rumble (brown noise in a few rolling swells).
Mix_Chunk *thunderSound()
{
	static Mix_Chunk *chunk = nullptr;
	static bool tried = false;
	if (tried) return chunk;
	tried = true;
	int freq = 0, channels = 0;
	Uint16 format = 0;
	if (!Mix_QuerySpec(&freq, &format, &channels) || format != AUDIO_S16SYS || channels < 1) return nullptr;
	const int n = freq * 4;
	Uint8 *buf = (Uint8*)SDL_malloc((size_t)n * channels * 2);
	if (!buf) return nullptr;
	Sint16 *out = (Sint16*)buf;
	Rng rng(0x7417u);
	float brown = 0.f, lp1 = 0.f, lp2 = 0.f, crackLp = 0.f;
	struct Swell { float at, len, gain; } swells[5];
	for (int i = 0; i < 5; ++i) swells[i] = {rng.r(0.05f, 2.2f), rng.r(0.4f, 1.3f), rng.r(0.45f, 1.f)};
	for (int i = 0; i < n; ++i)
	{
		float t = (float)i / freq;
		float w = rng.f() * 2.f - 1.f;
		brown = (brown + w * 0.04f) * 0.996f;
		lp1 += (brown - lp1) * 0.08f;
		lp2 += (lp1 - lp2) * 0.08f;
		crackLp += (w - crackLp) * 0.35f;
		float env = 0.f;
		for (const Swell &s : swells)
		{
			float x = (t - s.at) / s.len;
			if (x > -1.f && x < 3.f) env += s.gain * (x < 0.f ? (1.f + x) : std::exp(-x * 1.4f));
		}
		env *= std::exp(-t * 0.55f);
		float crack = t < 0.25f ? std::exp(-t * 22.f) * crackLp * 0.5f : 0.f;
		float v = glm::clamp(lp2 * 9.f * env + crack, -1.f, 1.f);
		if (i > n - freq / 4) v *= (float)(n - i) / (freq / 4); // fade out
		for (int c = 0; c < channels; ++c) out[i * channels + c] = (Sint16)(v * 26000.f);
	}
	chunk = Mix_QuickLoad_RAW(buf, (Uint32)(n * channels * 2));
	return chunk;
}
}

struct Weather::Impl
{
	bool active = false;
	bool outdoor = false;
	int kind = NONE;
	bool mist = false;
	bool mars = false;
	std::string terrain = "-";
	std::string skyName = "indoor";
	bool night = false;
	float sunAz = 0.f, sunElev = 0.f;   // degrees (azimuth from north, clockwise)
	SkyLight sky;
	glm::vec2 wind{1.f, 0.f};           // xz direction (map), unit
	float windSpeed = 20.f;             // voxels/s
	Rng rng;
	double time = 0.0;

	// lightning
	float nextStrike = 0.f;
	float strikeT = -1.f;               // seconds since the last strike started (<0 none)
	float pulse[3] = {0.f, 0.f, 0.f};   // its pulses: start times
	int pulses = 0;
	float thunderIn = -1.f, thunderVol = 1.f;
	int strikes = 0;

	struct Drop { glm::vec3 p; float speed, size, phase, life, maxLife; };
	std::vector<Drop> drops;
	struct Splash { glm::vec3 p; float age; };
	std::vector<Splash> splashes;
	MeshData md;
	mutable Mesh mesh;
	mutable bool dirty = false;
	int drawn = 0;

	// mist: three thin sheets over the ground, only where the sky is open
	int mx = 0, my = 0;
	std::vector<uint32_t> openMask;
	mutable Texture maskTex;
	mutable bool maskDirty = false;
	mutable Mesh mistMesh;
	mutable bool mistBuilt = false;
	float maskTimer = 0.f;
	glm::vec3 mistCol{0.5f};
	float mistAlpha = 0.f;
	std::vector<glm::vec4> fade;
	SkyInfo info;

	// the sky over the table (world space): the disc, its clouds, light shafts and stars
	glm::vec3 baseCol{0.f};             // the sun's or moon's light before its clouds
	float cover = 0.f;                  // how much of the disc its clouds hide (smoothed)
	MeshData cubeMd, glowMd, beamMd;
	mutable Mesh cubeMesh, glowMesh, beamMesh;
	mutable bool skyDirty = false;
	glm::vec4 beamClip{1.f, 0.f, -1.f, 0.f};
	struct Star { glm::vec3 dir; float size, phase, rate; };
	std::vector<Star> stars;
	struct Beam { glm::vec2 at; float phase; };
	std::vector<Beam> beams;
	uint32_t visSeed = 1;
	void updateSky(float dt, const WeatherView &v);

	void respawnFalling(Drop &d, const WeatherView &v, bool anywhere);
	void respawnDust(Drop &d, const WeatherView &v);
	float fadeAt(const glm::vec3 &p) const
	{
		float f = 1.f;
		for (const glm::vec4 &q : fade)
		{
			float d = glm::length(p - glm::vec3(q));
			f = std::min(f, glm::smoothstep(q.w * 0.35f, q.w, d));
		}
		return f;
	}
	void startStrike()
	{
		strikeT = 0.f;
		pulses = 1 + (int)(rng.f() * 3.f);
		pulse[0] = 0.f;
		for (int i = 1; i < pulses; ++i) pulse[i] = pulse[i - 1] + rng.r(0.08f, 0.22f);
		thunderIn = rng.r(0.4f, 2.6f);
		thunderVol = glm::clamp(1.15f - thunderIn / 3.f, 0.3f, 1.f);
		nextStrike = rng.r(10.f, 30.f);
		++strikes;
	}
};

Weather::Weather() : _p(new Impl()) {}
Weather::~Weather() = default;

void Weather::force(const std::string &kind) { forced = kind; }
std::string Weather::forcedWeather() { return forced; }
float Weather::forcedSun() { return forcedAz; }
void Weather::forceSun(float azimuth) { forcedAz = azimuth; }
void Weather::forceClock(int day, int hour, int minute) { clockDay = day; clockHour = hour; clockMinute = minute; }

const SkyLight &Weather::sky() const { return _p->sky; }

void Weather::end()
{
	Impl &p = *_p;
	p.active = false;
	p.outdoor = false;
	p.sky = SkyLight();
	p.drops.clear();
	p.splashes.clear();
	p.kind = NONE;
	p.mist = false;
	p.mistBuilt = false;
	p.dirty = false;
}

void Weather::begin(Game *game, SavedBattleGame *battle, int shadeOffset)
{
	end();
	Impl &p = *_p;
	p.info = SkyInfo();
	if (!game || !battle) return;
	p.active = true;
	p.mx = battle->getMapSizeX();
	p.my = battle->getMapSizeY();
	p.openMask.assign((size_t)p.mx * p.my, 0u);
	p.maskTimer = 0.f;
	SavedGame *sg = game->getSavedGame();
	int gameShade = glm::clamp(battle->getGlobalShade(), 0, 15);
	int shade = glm::clamp(gameShade + shadeOffset, 0, 15);
	SkyInfo &I = p.info;
	I.active = true;
	I.mission = battle->getMissionType();
	I.shadeGame = gameShade;
	I.shade = shade;
	I.forced = forced;

	// what the map is made of
	bool indoor = battle->getMissionType() == "STR_BASE_DEFENSE";
	bool snowy = false, rainy = false, desert = false;
	p.mars = false;
	std::string sets;
	if (battle->getMapDataSets())
		for (MapDataSet *ds : *battle->getMapDataSets())
		{
			std::string n = ds->getName();
			std::transform(n.begin(), n.end(), n.begin(), ::toupper);
			sets += n + " ";
			if (n.find("UBASE") != std::string::npos || n.find("U_BASE") != std::string::npos || n.find("XBASE") != std::string::npos) indoor = true;
			if (n.find("MARS") != std::string::npos) p.mars = true;
			if (n.find("POLAR") != std::string::npos || n.find("MOUNT") != std::string::npos) snowy = true;
			if (n.find("DESERT") != std::string::npos) desert = true;
			if (n.find("JUNGLE") != std::string::npos || n.find("FOREST") != std::string::npos || n.find("CULTIVAT") != std::string::npos
				|| n.find("BARN") != std::string::npos || n.find("URBAN") != std::string::npos) rainy = true;
		}
	p.terrain = p.mars ? "mars" : snowy ? "snowy" : desert ? "desert" : rainy ? "rainy" : "other";
	p.outdoor = !indoor;
	I.sets = sets;
	I.outdoor = !indoor;
	I.terrain = p.mars ? "Mars" : snowy ? "polar / mountain" : desert ? "desert" : rainy ? "jungle / forest / farm / city" : "other";
	// where the game's darkness came from: the mission's own rule, or the globe at the site
	{
		const AlienDeployment *dep = nullptr;
		Mod *mod = game->getMod();
		if (sg)
		{
			for (MissionSite *m : *sg->getMissionSites()) if (m->isInBattlescape()) dep = m->getDeployment();
			for (AlienBase *b : *sg->getAlienBases()) if (!dep && b->isInBattlescape()) dep = b->getDeployment();
			for (Ufo *u : *sg->getUfos()) if (!dep && u->isInBattlescape()) dep = mod->getDeployment(u->getRules()->getType(), false);
		}
		if (!dep) dep = mod->getDeployment(battle->getMissionType(), false);
		I.shadeFrom = "the globe at the site";
		if (dep && dep->getShade() != -1) I.shadeFrom = "the mission (always " + std::to_string(dep->getShade()) + ")";
		else if (dep && dep->getMinShade() != -1 && gameShade == dep->getMinShade()) I.shadeFrom = "the mission (at least " + std::to_string(dep->getMinShade()) + ")";
		else if (dep && dep->getMaxShade() != -1 && gameShade == dep->getMaxShade()) I.shadeFrom = "the mission (at most " + std::to_string(dep->getMaxShade()) + ")";
	}
	if (indoor)
	{
		I.indoorWhy = battle->getMissionType() == "STR_BASE_DEFENSE" ? "base defence" : "base map sets";
		I.sky = "indoor";
		p.terrain = "indoor";
		p.skyName = "indoor";
		Log(LOG_INFO) << "[VR] sky: indoor mission, no sun or weather (" << battle->getMissionType() << ")";
		return;
	}
	if (Options::vrVanillaSky)
	{
		// the table as it was before the sky came in: the room's light over the table, no weather
		I.vanilla = true;
		I.outdoor = false;
		I.sky = "vanilla";
		p.outdoor = false;
		p.skyName = "vanilla";
		Log(LOG_INFO) << "[VR] sky: vanilla (vrVanillaSky): the room's light, no sun or weather";
		return;
	}

	// the same mission always gets the same sky
	double lon = 0.0, lat = 0.0;
	bool site = missionSite(sg, lon, lat);
	uint32_t h = 2166136261u;
	const std::string &mt = battle->getMissionType();
	h = fnv(h, mt.data(), mt.size());
	int32_t q[8] = {(int32_t)std::lround(lon * 10000.0), (int32_t)std::lround(lat * 10000.0), gameShade, p.mx, p.my, 0, 0, 0};
	GameTime *gt = sg ? sg->getTime() : nullptr;
	if (gt) { q[5] = gt->getDay() + 40 * gt->getMonth(); q[6] = gt->getHour(); q[7] = gt->getMinute(); }
	h = fnv(h, q, sizeof(q));
	p.rng = Rng(h);
	I.seed = h;
	I.site = site;
	I.lon = lon * 180.0 / 3.14159265358979;
	if (I.lon > 180.0) I.lon -= 360.0;
	I.lat = -lat * 180.0 / 3.14159265358979;
	if (gt)
	{
		I.gmtH = gt->getHour(); I.gmtM = gt->getMinute();
		I.localH = (float)std::fmod(I.gmtH + I.gmtM / 60.0 + I.lon / 15.0 + 48.0, 24.0);
	}
	Rng &rng = p.rng;

	// where the sun stands over the site (the table's far edge is north)
	float az = rng.r(0.f, 360.f);
	bool morning = rng.f() < 0.5f;
	bool real = false;
	int cYear = gt ? gt->getYear() : 1999, cMonth = gt ? gt->getMonth() : 1, cDay = gt ? gt->getDay() : 1;
	double cHours = gt ? gt->getHour() + gt->getMinute() / 60.0 : 12.0;
	if (clockDay > 0) { cDay = clockDay; cHours = clockHour + clockMinute / 60.0; }
	if (site && gt && !p.mars)
	{
		double hours = cHours;
		glm::vec3 sun = lonLatToUnit((12.0 - hours) / 24.0 * 6.283185307, 0.0);
		glm::vec3 up = lonLatToUnit(lon, lat);
		glm::vec3 east = lonLatToUnit(lon + 1e-3, lat) - up, north = lonLatToUnit(lon, lat - 1e-3) - up;
		if (glm::length(east) > 1e-6f && glm::length(north) > 1e-6f)
		{
			east = glm::normalize(east);
			north = glm::normalize(north);
			float e = glm::dot(sun, east), n = glm::dot(sun, north);
			I.realElev = std::asin(glm::clamp(glm::dot(sun, up), -1.f, 1.f)) * 180.f / PI;
			I.realKnown = true;
			if (std::sqrt(e * e + n * n) > 0.05f)
			{
				az = std::atan2(e, n) * 180.f / PI;
				if (az < 0.f) az += 360.f;
				morning = e > 0.f;
				real = true;
			}
		}
	}
	I.sunFrom = real ? "the real sun at the site" : p.mars ? "rolled (Mars)" : !site ? "rolled (no site)" : "rolled (sun overhead)";
	float elev;
	glm::vec3 col, skyCol;
	bool night = shade >= 12;
	const glm::vec3 NIGHT_SKY(0.045f, 0.055f, 0.095f);   // starlight
	if (!night)
	{
		elev = SUN_ELEV[shade];
		col = SUN_COL[shade] * (0.72f / std::max(std::sin(elev * PI / 180.f), 0.42f));
		float k = shade / 11.f;
		skyCol = glm::mix(glm::vec3(0.16f, 0.19f, 0.24f), glm::vec3(0.12f, 0.08f, 0.09f), k);
		// twilight: the last of the sun fades as the night sky takes over
		if (shade == 10) { col *= 0.75f; skyCol = glm::mix(skyCol, NIGHT_SKY, 0.35f); }
		if (shade == 11) { col *= 0.5f; skyCol = glm::mix(skyCol, NIGHT_SKY, 0.65f); }
		p.skyName = shade <= 3 ? "day" : (morning ? "dawn" : "dusk");
	}
	else
	{
		// the moon: where it really is over the site at that hour and how full it is; when it is
		// down, only the starlit sky lights the map
		float malt = 40.f, maz = std::fmod(az + 180.f, 360.f), illum = 0.6f;
		bool waxing = true;
		if (site && gt && !p.mars)
		{
			moonAt(daysJ2000(cYear, cMonth, cDay, cHours), I.lat, I.lon, malt, maz, illum, waxing);
			I.moonKnown = true;
			I.sunFrom = "the real moon at the site";
		}
		else I.sunFrom = p.mars ? "rolled (Mars)" : "rolled (no site)";
		I.moonIllum = illum; I.moonAlt = malt; I.moonAz = maz; I.moonWaxing = waxing;
		az = maz;
		elev = std::max(malt, 12.f);
		skyCol = NIGHT_SKY;
		if (malt > 0.f)
		{
			col = glm::vec3(0.50f, 0.62f, 1.00f) * (0.08f + 0.52f * illum) * (0.55f / std::max(std::sin(elev * PI / 180.f), 0.5f));
			skyCol += glm::vec3(0.01f, 0.012f, 0.02f) * illum;
			p.skyName = "night";
		}
		else
		{
			col = glm::vec3(0.f);
			I.starlit = true;
			p.skyName = "night (starlit)";
		}
	}
	if (forcedAz >= 0.f) { az = forcedAz; real = false; I.sunFrom = "set by hand"; }
	p.night = night;
	if (p.mars) skyCol *= glm::vec3(1.25f, 0.8f, 0.65f);
	p.sunAz = az;
	p.sunElev = elev;
	float a = az * PI / 180.f, e = elev * PI / 180.f;
	p.sky.on = true;
	p.sky.dir = glm::vec3(std::sin(a) * std::cos(e), std::sin(e), -std::cos(a) * std::cos(e));
	p.wind = glm::normalize(glm::vec2(std::cos(rng.r(0.f, 6.2832f)), std::sin(rng.r(0.f, 6.2832f))) + glm::vec2(1e-3f));

	// the weather: rolled from the mission, so a reload keeps it
	float r1 = rng.f(), r2 = rng.f(), r3 = rng.f(), r4 = rng.f();
	int kind = NONE;
	float cloud = 0.f;
	bool mist = false;
	auto roll = [](const char *what, float r, float need) {
		char b[64]; snprintf(b, sizeof(b), "%s: rolled %.2f, needs < %.2f", what, r, need); return std::string(b); };
	if (p.mars) { if (r1 < 0.6f) kind = SAND; I.rollKind = roll("sand", r1, 0.6f); }
	else if (snowy) { if (r1 < 0.5f) kind = SNOW; I.rollKind = roll("snow", r1, 0.5f); }
	else if (desert) { if (r1 < 0.4f) kind = DUST; I.rollKind = roll("dust", r1, 0.4f); }
	else if (rainy) { if (r1 < 0.35f) kind = RAIN; I.rollKind = roll("rain", r1, 0.35f); }
	else I.rollKind = "none for this terrain";
	if (p.mars) I.rollClouds = "clouds: none on Mars";
	else if (shade > 7) I.rollClouds = "clouds: too dark (needs <= 7)";
	else { I.rollClouds = roll("clouds", r2, 0.7f); if (r2 < 0.7f) cloud = 0.30f + 0.15f * r3; }
	if (p.mars) I.rollMist = "mist: none on Mars";
	else if (kind == DUST) I.rollMist = "mist: not in a dust storm";
	else if (!morning) I.rollMist = "mist: not morning";
	else if (shade < 3 || shade > 10) I.rollMist = "mist: needs darkness 3-10";
	else { I.rollMist = roll("mist", r4, 0.5f); mist = r4 < 0.5f; }
	if (!Options::vrWeather) { kind = NONE; cloud = 0.f; mist = false; }
	if (forced != "auto")
	{
		kind = NONE; mist = false; cloud = 0.f;
		if (forced == "rain") kind = RAIN;
		else if (forced == "snow") kind = SNOW;
		else if (forced == "dust") kind = DUST;
		else if (forced == "sand") kind = SAND;
		else if (forced == "mist") mist = true;
		else if (forced == "clouds") cloud = 0.4f;
	}
	// overcast under rain and snow, hazy in a sandstorm
	if (kind == RAIN) { col *= 0.55f; skyCol *= 1.25f; cloud = std::max(cloud, 0.5f); }
	if (kind == SNOW) { col *= 0.7f; skyCol = skyCol * 1.2f + glm::vec3(0.02f, 0.025f, 0.035f); cloud = std::max(cloud, 0.35f); }
	if (kind == DUST || kind == SAND) { col *= 0.82f; skyCol = skyCol * 1.1f + glm::vec3(0.03f, 0.02f, 0.01f); }
	if (night) cloud *= 0.5f;
	p.kind = kind;
	p.mist = mist;
	p.sky.col = col;
	p.baseCol = col;
	p.cover = 0.f;
	p.visSeed = h ^ 0x9E3779B9u;
	{
		// stars and light shafts, fixed for the mission
		Rng vr(p.visSeed);
		p.stars.clear();
		for (int i = 0; i < 180; ++i)
		{
			float a = vr.r(0.f, 6.2832f), e = std::asin(vr.r(std::sin(0.26f), 0.995f));
			p.stars.push_back({glm::vec3(std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)), vr.r(0.0025f, 0.0055f), vr.r(0.f, 6.2832f), vr.r(0.6f, 2.2f)});
		}
		p.beams.clear();
		for (int i = 0; i < 5; ++i) p.beams.push_back({glm::vec2(vr.r(0.1f, 0.9f), vr.r(0.1f, 0.9f)), vr.r(0.f, 6.2832f)});
	}
	p.sky.sky = skyCol;
	p.sky.cloud = cloud;
	p.windSpeed = kind == DUST || kind == SAND ? rng.r(35.f, 55.f) : rng.r(10.f, 28.f);
	p.nextStrike = rng.r(4.f, 9.f);
	// mist takes the colour of the low sun, as bright as the morning allows
	float light = shadeLight(shade);
	p.mistCol = glm::mix(glm::vec3(0.86f, 0.87f, 0.90f), SUN_COL[std::min(shade, 11)], 0.3f) * glm::clamp(light * 2.f, 0.15f, 0.8f);
	p.mistAlpha = mist ? 0.065f : 0.f;
	I.sky = p.skyName;
	I.sunAz = az; I.sunElev = elev;
	I.sunCol = col; I.skyCol = skyCol;
	I.weather = kindName(kind);
	I.cloud = cloud;
	I.mist = mist;
	Log(LOG_INFO) << "[VR] sky: " << p.skyName << " shade " << shade << (night ? " moon az " : " sun az ") << (int)az << " elev " << (int)elev
		<< (night ? " moon " + std::to_string((int)(I.moonIllum * 100)) + "% at " + std::to_string((int)I.moonAlt) : std::string())
		<< " (" << I.sunFrom << ")" << " terrain " << p.terrain
		<< " weather " << kindName(kind) << (mist ? " +mist" : "") << " clouds " << cloud << " [" << sets << "]";
}

void Weather::strike()
{
	if (_p->kind == RAIN) _p->startStrike();
}

void Weather::Impl::respawnFalling(Drop &d, const WeatherView &v, bool anywhere)
{
	float x0 = std::max(0.f, v.center.x - v.radius), x1 = std::min(v.mx * TILE_W, v.center.x + v.radius);
	float z0 = std::max(0.f, v.center.y - v.radius), z1 = std::min(v.my * TILE_W, v.center.y + v.radius);
	if (x1 <= x0) { x0 = 0.f; x1 = v.mx * TILE_W; }
	if (z1 <= z0) { z0 = 0.f; z1 = v.my * TILE_W; }
	d.p.x = rng.r(x0, x1);
	d.p.z = rng.r(z0, z1);
	d.p.y = anywhere ? rng.r(0.f, v.ceiling) : v.ceiling + rng.r(0.f, 12.f);
	d.phase = rng.r(0.f, 6.2832f);
	if (kind == RAIN) { d.speed = rng.r(150.f, 190.f); d.size = rng.r(7.f, 11.f); }
	else { d.speed = rng.r(10.f, 18.f); d.size = rng.r(0.45f, 0.75f); }
}

void Weather::Impl::respawnDust(Drop &d, const WeatherView &v)
{
	float x0 = std::max(0.f, v.center.x - v.radius), x1 = std::min(v.mx * TILE_W, v.center.x + v.radius);
	float z0 = std::max(0.f, v.center.y - v.radius), z1 = std::min(v.my * TILE_W, v.center.y + v.radius);
	if (x1 <= x0) { x0 = 0.f; x1 = v.mx * TILE_W; }
	if (z1 <= z0) { z0 = 0.f; z1 = v.my * TILE_W; }
	for (int tries = 0; tries < 4; ++tries)
	{
		d.p.x = rng.r(x0, x1);
		d.p.z = rng.r(z0, z1);
		float g = v.stop ? v.stop((int)(d.p.x / TILE_W), (int)(d.p.z / TILE_W)) : 0.f;
		if (g >= v.top) continue;
		d.p.y = g + rng.r(0.6f, 13.f);
		break;
	}
	d.speed = windSpeed * rng.r(0.7f, 1.3f);
	d.size = rng.r(0.35f, 0.6f);
	d.phase = rng.r(0.f, 6.2832f);
	d.maxLife = rng.r(1.5f, 4.5f);
	d.life = 0.f;
}

namespace
{
/// A cube of half size h with the given axes (world), into md.
void axisCube(MeshData &md, const glm::vec3 &c, const glm::vec3 &ax, const glm::vec3 &ay, const glm::vec3 &az, float h, const glm::vec4 &col)
{
	glm::vec3 a = ax * h, u = ay * h, s = az * h;
	glm::vec3 p[8] = {c - a - u - s, c + a - u - s, c + a + u - s, c - a + u - s,
		c - a - u + s, c + a - u + s, c + a + u + s, c - a + u + s};
	static const int f[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
	for (auto &q : f) md.addQuad(p[q[0]], p[q[1]], p[q[2]], p[q[3]], col, 0.f);
}

/// A camera-facing square for the soft sprites (uv 0..1 over it).
void sprite(MeshData &md, const glm::vec3 &c, const glm::vec3 &right, const glm::vec3 &up, float half, const glm::vec4 &col)
{
	glm::vec3 r = right * half, u = up * half;
	md.addQuad(c - r - u, c + r - u, c + r + u, c - r + u, col, 0.f, {0, 1}, {1, 1}, {1, 0}, {0, 0});
}
}

void Weather::Impl::updateSky(float dt, const WeatherView &v)
{
	cubeMd.verts.clear(); cubeMd.indices.clear();
	glowMd.verts.clear(); glowMd.indices.clear();
	beamMd.verts.clear(); beamMd.indices.clear();
	skyDirty = true;
	glm::vec3 C((v.mapRect.x + v.mapRect.z) * 0.5f, v.tableY, (v.mapRect.y + v.mapRect.w) * 0.5f);
	glm::vec3 dirW = glm::vec3(v.board * glm::vec4(sky.dir, 0.f));
	if (glm::length(dirW) < 1e-6f) return;
	dirW = glm::normalize(dirW);
	bool hasLight = glm::length(baseCol) > 1e-4f;
	bool overcast = kind == RAIN || kind == SNOW;
	float t = (float)time;
	glm::vec3 tint = hasLight ? baseCol / std::max(baseCol.r, std::max(baseCol.g, baseCol.b)) : glm::vec3(0.6f, 0.7f, 1.f);

	// the disc: beyond the table edge in the light's direction, at its height
	float coverNow = 0.f;
	if (hasLight)
	{
		glm::vec3 P = C + dirW * 0.9f;
		glm::vec3 toEye = v.head - P;
		float dEye = glm::length(toEye);
		toEye = dEye > 1e-4f ? toEye / dEye : glm::vec3(0, 0, 1);
		glm::vec3 right = glm::cross(glm::vec3(0, 1, 0), toEye);
		right = glm::length(right) > 1e-3f ? glm::normalize(right) : glm::vec3(1, 0, 0);
		glm::vec3 up = glm::cross(toEye, right);
		float nearFade = glm::smoothstep(0.3f, 0.65f, dEye);   // fades when it would be right at the head
		float low = 1.f - glm::clamp((sunElev - 12.f) / 50.f, 0.f, 1.f);
		float r = night ? 0.034f : 0.038f + 0.026f * low;
		float dim = overcast ? 0.45f : 1.f;
		// clouds drifting past it (only when the mission has clouds)
		struct Puff { glm::vec2 xy; float s, alpha; int seed; };
		std::vector<Puff> puffs;
		if (sky.cloud > 0.f || overcast)
		{
			int n = overcast ? 6 : 2 + (int)(sky.cloud * 6.f);
			const float span = 0.7f;
			for (int i = 0; i < n; ++i)
			{
				Rng pr(visSeed + 77u * (uint32_t)(i + 1));
				float speed = pr.r(0.008f, 0.016f);
				float x = std::fmod(t * speed + i * span / n + pr.r(0.f, 0.05f), span) - span * 0.5f;
				float y = pr.r(-0.05f, 0.05f);
				float s = overcast ? pr.r(0.06f, 0.09f) : pr.r(0.03f, 0.06f);
				float edge = 1.f - glm::smoothstep(span * 0.32f, span * 0.5f, std::fabs(x));
				puffs.push_back({glm::vec2(x, y), s, edge, (int)(visSeed + i * 131u)});
				// how much of the disc it hides
				float d = glm::length(glm::vec2(x, y));
				float c = (1.f - glm::smoothstep(std::max(0.f, s * 0.6f - r), s + r, d)) * glm::clamp(s / (r * 1.2f), 0.f, 1.f) * edge;
				coverNow = std::max(coverNow, c);
			}
		}
		if (overcast) coverNow = std::max(coverNow, 0.5f);
		// halo
		float haloA = (night ? 0.16f : 0.34f + 0.15f * low) * nearFade * dim * (1.f - 0.7f * cover);
		sprite(glowMd, P - toEye * 0.005f, right, up, r * (night ? 3.2f : 4.5f + 2.f * low), glm::vec4(tint, haloA));
		// the voxel disc (the moon shows its phase: the unlit part is a faint earthlit grey)
		const int N = 13;
		float cell = 2.f * r / N;
		for (int iy = 0; iy < N; ++iy)
			for (int ix = 0; ix < N; ++ix)
			{
				float x = (ix + 0.5f) / N * 2.f - 1.f, y = (iy + 0.5f) / N * 2.f - 1.f;
				float rr = x * x + y * y;
				if (rr > 1.f) continue;
				glm::vec4 col;
				if (!night)
				{
					glm::vec3 c = glm::mix(glm::vec3(1.f, 0.98f, 0.9f), tint, glm::clamp(rr * 1.2f + 0.3f * low, 0.f, 1.f)) * (1.6f - 0.4f * rr);
					col = glm::vec4(c * dim, nearFade);
				}
				else
				{
					float xt = (1.f - 2.f * info.moonIllum) * std::sqrt(std::max(0.f, 1.f - y * y));
					bool lit = info.moonWaxing ? x > xt : x < -xt;
					uint32_t hsh = (uint32_t)(ix * 73856093) ^ (uint32_t)(iy * 19349663);
					hsh ^= hsh >> 13; hsh *= 0x5bd1e995; hsh ^= hsh >> 15;
					float crater = (hsh & 7) == 0 ? 0.8f : 1.f;
					col = lit ? glm::vec4(glm::vec3(0.88f, 0.9f, 0.97f) * 1.3f * crater * dim, nearFade) : glm::vec4(0.07f, 0.08f, 0.11f, 0.55f * nearFade);
				}
				axisCube(cubeMd, P + right * (x * r) + up * (y * r), right, up, toEye, cell * 0.46f, col);
			}
		// cloud puffs in front of the disc, lit by it
		for (const Puff &pf : puffs)
		{
			Rng cr((uint32_t)pf.seed);
			glm::vec3 Q = P + right * pf.xy.x + up * pf.xy.y + toEye * 0.025f;
			float bright = night ? 0.22f : 0.95f;
			for (int k = 0; k < 16; ++k)
			{
				glm::vec3 o(cr.r(-1.f, 1.f), cr.r(-0.45f, 0.45f), cr.r(-0.3f, 0.3f));
				if (o.x * o.x + o.y * o.y * 4.f > 1.1f) continue;
				float h = pf.s * cr.r(0.22f, 0.34f);
				float shade = 0.75f + 0.35f * glm::clamp(o.y / 0.45f, -1.f, 1.f);
				glm::vec3 c = glm::mix(glm::vec3(1.f), tint, night ? 0.6f : 0.35f) * bright * shade;
				axisCube(cubeMd, Q + right * (o.x * pf.s) + up * (o.y * pf.s) + toEye * (o.z * pf.s), right, up, toEye, h, glm::vec4(c, 0.92f * pf.alpha * nearFade));
			}
		}
	}
	cover += (coverNow - cover) * (1.f - std::exp(-dt * 2.f));
	sky.col = baseCol * (1.f - 0.55f * cover);
	info.cover = cover;

	// stars over the table on a clear night (fewer under a bright moon)
	if (night && !overcast)
	{
		float moonWash = (info.moonAlt > 0.f ? info.moonIllum : 0.f);
		float k = 0.75f * (1.f - 0.7f * moonWash) * (1.f - sky.cloud * 1.6f);
		if (k > 0.02f)
			for (const Star &st : stars)
			{
				glm::vec3 P = C + st.dir * 1.1f;
				if (glm::length(v.head - P) < 0.6f) continue; // none right at the face
				glm::vec3 toEye = glm::normalize(v.head - P);
				glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), toEye) + glm::vec3(1e-4f, 0, 0));
				glm::vec3 up = glm::cross(toEye, right);
				float tw = 0.65f + 0.35f * std::sin(t * st.rate + st.phase);
				sprite(glowMd, P, right, up, st.size, glm::vec4(0.85f, 0.9f, 1.f, k * tw));
			}
	}

	// light shafts slanting across the map at a low sun, or in mist and dust
	if (hasLight && !night && !overcast && (sunElev <= 31.f || mist || kind == DUST || kind == SAND))
	{
		beamClip = v.mapRect;
		float base = (mist || kind == DUST || kind == SAND) ? 0.07f : 0.045f;
		for (const Beam &b : beams)
		{
			glm::vec3 g(glm::mix(v.mapRect.x, v.mapRect.z, b.at.x), v.tableY + 0.004f, glm::mix(v.mapRect.y, v.mapRect.w, b.at.y));
			glm::vec3 axis = dirW;
			glm::vec3 mid = g + axis * 0.3f;
			glm::vec3 side = glm::cross(axis, v.head - mid);
			if (glm::length(side) < 1e-4f) continue;
			side = glm::normalize(side) * 0.022f;
			float a = base * (0.55f + 0.45f * std::sin(t * 0.35f + b.phase)) * (1.f - 0.7f * cover);
			glm::vec3 top = g + axis * 0.6f;
			beamMd.addQuad(g - side, g + side, top + side, top - side, glm::vec4(tint, a), 0.f, {0, 0}, {1, 0}, {1, 1}, {0, 1});
		}
	}
}

void Weather::drawSky(const Shader &sh) const
{
	const Impl &p = *_p;
	if (!p.active || !p.outdoor) return;
	if (p.skyDirty)
	{
		p.cubeMesh.upload(p.cubeMd, true);
		p.glowMesh.upload(p.glowMd, true);
		p.beamMesh.upload(p.beamMd, true);
		p.skyDirty = false;
	}
	sh.set("uModel", glm::mat4(1.f));
	sh.set("uUVRect", glm::vec4(0.f, 0.f, 1.f, 1.f));
	sh.set("uTint", glm::vec4(1.f));
	sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
	glEnable(GL_BLEND);
	glDepthMask(GL_FALSE);
	// halo and stars (soft additive sprites), then the disc and its clouds, then the shafts over the map
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	sh.set("uMode", 7);
	sh.set("uShape", 0);
	if (!p.glowMd.indices.empty()) p.glowMesh.draw();
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	sh.set("uMode", 2);
	if (!p.cubeMd.indices.empty()) p.cubeMesh.draw();
	if (!p.beamMd.indices.empty())
	{
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		sh.set("uMode", 7);
		sh.set("uShape", 1);
		sh.set("uClip", p.beamClip);
		p.beamMesh.draw();
		sh.set("uClip", glm::vec4(1.f, 0.f, -1.f, 0.f));
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	sh.set("uShape", 0);
}

void Weather::update(float dt, const WeatherView &v)
{
	Impl &p = *_p;
	if (!p.active || !p.outdoor) return;
	dt = std::min(dt, 0.1f);
	p.time += dt;
	p.fade = v.fade;

	// clouds drift with the wind
	if (p.sky.cloud > 0.f) p.sky.cloudOffset += p.wind * (p.kind == RAIN ? 9.f : 5.f) * dt;
	p.updateSky(dt, v);

	// lightning: a few quick pulses, thunder a moment later
	p.sky.flash = 0.f;
	if (p.kind == RAIN)
	{
		p.nextStrike -= dt;
		if (p.nextStrike <= 0.f && p.strikeT < 0.f) p.startStrike();
		if (p.strikeT >= 0.f)
		{
			p.strikeT += dt;
			float f = 0.f;
			for (int i = 0; i < p.pulses; ++i)
			{
				float t = p.strikeT - p.pulse[i];
				if (t >= 0.f) f = std::max(f, (i == 0 ? 1.f : 0.75f) * std::exp(-t * 16.f));
			}
			p.sky.flash = f;
			if (p.strikeT > p.pulse[p.pulses - 1] + 0.6f) p.strikeT = -1.f;
		}
		if (p.thunderIn >= 0.f)
		{
			p.thunderIn -= dt;
			if (p.thunderIn < 0.f && !Options::mute)
				if (Mix_Chunk *c = thunderSound())
				{
					Mix_VolumeChunk(c, (int)(Options::soundVolume * p.thunderVol));
					Mix_PlayChannel(-1, c, 0);
				}
		}
	}

	// the open sky over the ground, for the mist (fog of war and roofs change)
	p.maskTimer -= dt;
	if (p.mist && p.maskTimer <= 0.f && v.open)
	{
		p.maskTimer = 0.7f;
		bool changed = false;
		for (int y = 0; y < p.my; ++y)
			for (int x = 0; x < p.mx; ++x)
			{
				uint32_t c = v.open(x, y) ? 0xFFFFFFFFu : 0xFF000000u;
				uint32_t &m = p.openMask[(size_t)y * p.mx + x];
				if (m != c) { m = c; changed = true; }
			}
		if (changed) p.maskDirty = true;
	}

	if (p.kind == NONE) { p.drawn = 0; return; }

	// how many: by the area the table shows (sparse, so the figures stay readable)
	float tiles = (2.f * v.radius / TILE_W) * (2.f * v.radius / TILE_W);
	size_t want = 0;
	switch (p.kind)
	{
	case RAIN: want = (size_t)std::min(700.f, tiles * 1.1f); break;
	case SNOW: want = (size_t)std::min(600.f, tiles * 1.0f); break;
	default: want = (size_t)std::min(450.f, tiles * 0.8f); break;
	}
	bool falling = p.kind == RAIN || p.kind == SNOW;
	while (p.drops.size() < want)
	{
		Impl::Drop d{};
		if (falling) p.respawnFalling(d, v, true);
		else { p.respawnDust(d, v); d.life = p.rng.r(0.f, d.maxLife); }
		p.drops.push_back(d);
	}
	if (p.drops.size() > want) p.drops.resize(want);

	glm::vec3 windV(p.wind.x * p.windSpeed, 0.f, p.wind.y * p.windSpeed);
	auto outside = [&](const glm::vec3 &q)
	{
		return q.x < 0.f || q.z < 0.f || q.x >= v.mx * TILE_W || q.z >= v.my * TILE_W
			|| std::fabs(q.x - v.center.x) > v.radius || std::fabs(q.z - v.center.y) > v.radius;
	};
	auto stopAt = [&](const glm::vec3 &q) { return v.stop ? v.stop((int)(q.x / TILE_W), (int)(q.z / TILE_W)) : 0.f; };
	for (Impl::Drop &d : p.drops)
	{
		if (falling)
		{
			glm::vec3 vel = p.kind == RAIN ? glm::vec3(windV.x * 0.6f, -d.speed, windV.z * 0.6f)
				: glm::vec3(windV.x * 0.5f + std::sin((float)p.time * 1.3f + d.phase) * 5.f, -d.speed, windV.z * 0.5f + std::cos((float)p.time * 1.1f + d.phase) * 5.f);
			d.p += vel * dt;
			if (outside(d.p)) { p.respawnFalling(d, v, false); continue; }
			float s = stopAt(d.p);
			if (d.p.y <= s)
			{
				if (p.kind == RAIN && s < v.top && p.splashes.size() < 300) p.splashes.push_back({glm::vec3(d.p.x, s + 0.2f, d.p.z), 0.f});
				p.respawnFalling(d, v, false);
			}
		}
		else
		{
			d.life += dt;
			d.p += glm::vec3(p.wind.x * d.speed, std::sin((float)p.time * 2.f + d.phase) * 2.5f, p.wind.y * d.speed) * dt;
			if (d.life >= d.maxLife || outside(d.p) || d.p.y < stopAt(d.p)) p.respawnDust(d, v);
		}
	}
	for (Impl::Splash &s : p.splashes) s.age += dt;
	p.splashes.erase(std::remove_if(p.splashes.begin(), p.splashes.end(), [](const Impl::Splash &s) { return s.age > 0.2f; }), p.splashes.end());

	// the mesh (map voxels), rebuilt each frame
	float bright = glm::clamp(v.light * 1.6f, 0.2f, 1.f) + p.sky.flash * 0.9f;
	MeshData &md = p.md;
	md.verts.clear();
	md.indices.clear();
	int n = 0;
	for (const Impl::Drop &d : p.drops)
	{
		float s = stopAt(d.p);
		if (s >= v.top) continue; // under a roof above the view level
		float f = p.fadeAt(d.p);
		if (f <= 0.02f) continue;
		if (p.kind == RAIN)
		{
			glm::vec3 vel(windV.x * 0.6f, -d.speed, windV.z * 0.6f);
			glm::vec3 dir = glm::normalize(vel);
			glm::vec3 a = d.p - dir * d.size, b = d.p;
			if (a.y > v.ceiling) continue;
			if (b.y < s) b.y = s;
			glm::vec3 mid = (a + b) * 0.5f, toEye = v.viewer - mid;
			glm::vec3 side = glm::cross(dir, toEye);
			if (glm::length(side) < 1e-4f) continue;
			side = glm::normalize(side) * 0.17f;
			glm::vec4 c(glm::vec3(0.62f, 0.70f, 0.82f) * bright, 0.42f * f);
			md.addQuad(b - side, b + side, a + side, a - side, c, 0.f);
		}
		else if (p.kind == SNOW)
		{
			if (d.p.y > v.ceiling) continue;
			glm::vec4 c(glm::vec3(0.92f, 0.95f, 1.0f) * bright, 0.85f * f);
			md.addBox(d.p - glm::vec3(d.size), d.p + glm::vec3(d.size), c, 0.f);
		}
		else
		{
			float life = glm::clamp(std::min(d.life / 0.5f, (d.maxLife - d.life) / 0.6f), 0.f, 1.f);
			glm::vec3 base = p.kind == SAND ? glm::vec3(0.74f, 0.37f, 0.20f) : glm::vec3(0.80f, 0.67f, 0.47f);
			glm::vec4 c(base * bright, 0.6f * f * life);
			if (c.a <= 0.01f) continue;
			orientedBox(md, d.p, p.wind, glm::vec3(d.size * 2.4f, d.size, d.size), c);
		}
		++n;
	}
	for (const Impl::Splash &s : p.splashes)
	{
		float k = s.age / 0.2f, r = 0.4f + 1.4f * k;
		float f = p.fadeAt(s.p);
		glm::vec4 c(glm::vec3(0.70f, 0.78f, 0.88f) * bright, 0.5f * (1.f - k) * f);
		if (c.a <= 0.01f) continue;
		md.addQuad(s.p + glm::vec3(-r, 0, r), s.p + glm::vec3(r, 0, r), s.p + glm::vec3(r, 0, -r), s.p + glm::vec3(-r, 0, -r), c, 0.f);
	}
	p.drawn = n;
	p.dirty = true;
}

void Weather::draw(const Shader &sh, const glm::mat4 &M) const
{
	const Impl &p = *_p;
	if (!p.active || !p.outdoor) return;
	bool parts = p.kind != NONE && !p.md.indices.empty();
	if (!parts && !p.mist) return;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	sh.set("uModel", M);
	if (parts)
	{
		if (p.dirty) { p.mesh.upload(p.md, true); p.dirty = false; }
		sh.set("uMode", 2);
		sh.set("uTint", glm::vec4(1.f));
		p.mesh.draw();
	}
	if (p.mist && p.mx > 0 && p.my > 0)
	{
		if (!p.mistBuilt)
		{
			MeshData md;
			float X = p.mx * TILE_W, Z = p.my * TILE_W;
			for (float y : {1.5f, 3.5f, 6.f, 9.f})
				md.addQuad({0.f, y, Z}, {X, y, Z}, {X, y, 0.f}, {0.f, y, 0.f}, glm::vec4(1.f), 0.f, {0, 1}, {1, 1}, {1, 0}, {0, 0});
			p.mistMesh.upload(md);
			p.mistBuilt = true;
		}
		if (p.maskDirty || !p.maskTex.valid())
		{
			if (!p.maskTex.valid()) p.maskTex.create(p.mx, p.my, false, true);
			p.maskTex.update(p.openMask.data(), p.mx, p.my);
			p.maskDirty = false;
		}
		// hands and cursor in world space: the mist parts around them
		float k = glm::length(glm::vec3(M[0]));
		int nf = 0;
		for (const glm::vec4 &f : p.fade)
		{
			if (nf >= 3) break;
			glm::vec3 w = glm::vec3(M * glm::vec4(glm::vec3(f), 1.f));
			sh.set(("uFade[" + std::to_string(nf) + "]").c_str(), glm::vec4(w, f.w * k));
			++nf;
		}
		sh.set("uFadeCount", nf);
		sh.set("uMapInv", glm::inverse(M));
		sh.set("uUVRect", glm::vec4(0.f, 0.f, 1.f, 1.f));
		sh.set("uMode", 6);
		sh.set("uTint", glm::vec4(p.mistCol * (1.f + p.sky.flash), p.mistAlpha));
		p.maskTex.bind(0);
		p.mistMesh.draw();
		sh.set("uFadeCount", 0);
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	sh.set("uTint", glm::vec4(1.f));
}

SkyInfo Weather::info() const
{
	const Impl &p = *_p;
	SkyInfo I = p.info;
	I.drawn = p.drawn;
	I.strikes = p.strikes;
	I.nextStrike = p.kind == RAIN ? p.nextStrike : -1.f;
	I.forced = forced;
	return I;
}

std::string Weather::status() const
{
	const Impl &p = *_p;
	if (!p.active) return "sky=-";
	std::ostringstream o;
	o << "sky=" << p.skyName;
	if (!p.outdoor) return o.str();
	o << " sun=" << (int)p.sunAz << "," << (int)p.sunElev << " weather=" << kindName(p.kind) << (p.mist ? "+mist" : "")
		<< " clouds=" << (int)(p.sky.cloud * 100) << " drawn=" << p.drawn << " strikes=" << p.strikes
		<< " moon=" << (int)(p.info.moonIllum * 100) << "%," << (int)p.info.moonAlt << (p.info.starlit ? ",starlit" : "")
		<< " cover=" << (int)(p.cover * 100) << " sky=" << p.cubeMd.verts.size() / 24 << "," << p.glowMd.verts.size() / 4 << "," << p.beamMd.verts.size() / 4;
	return o.str();
}

}
}
