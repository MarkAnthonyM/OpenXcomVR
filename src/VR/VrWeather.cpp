/*
 * OXCE VR tabletop - the sky over a battle on the table (see VrWeather.h).
 */
#include "VrWeather.h"
#include "VrAudio.h"
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
std::string fmt2(const char *f, double a) { char b[96]; snprintf(b, sizeof(b), f, a); return b; }
int forcedStrength = -1;                  // rain/snow strength when forced (0 light, 1 steady, 2 heavy; -1 rolled)
const char *STRENGTH_NAME[2][3] = {{"drizzle", "steady rain", "thunderstorm"}, {"flurries", "steady snow", "blizzard"}};
float forcedAz = -1.f;
int clockDay = -1, clockHour = 0, clockMinute = 0;
double skyShift = 0.0;                    // the debug panel's sky clock shift (days)

/// The calendar date and hour of a day count since J2000.0.
void dateOf(double days, int &y, int &mo, int &d, double &hours)
{
	double jd = days + 2451545.0 + 0.5;
	long z = (long)std::floor(jd);
	double f = jd - z;
	long a = z;
	if (z >= 2299161) { long al = (long)((z - 1867216.25) / 36524.25); a = z + 1 + al - al / 4; }
	long b = a + 1524, c = (long)((b - 122.1) / 365.25), dd = (long)(365.25 * c), e = (long)((b - dd) / 30.6001);
	d = (int)(b - dd - (long)(30.6001 * e));
	mo = (int)(e < 14 ? e - 1 : e - 13);
	y = (int)(mo > 2 ? c - 4716 : c - 4715);
	hours = f * 24.0;
}

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
// how high the table's sun stands: kept below a seated player's line of sight (about 25-35 degrees down
// onto the table) for most of the day, so shadows reach out past what casts them from any seat
const float SUN_ELEV[12] = {50, 47, 44, 40, 35, 32, 29, 26, 23, 20, 17, 15};
// the evening sun: a warm late-morning white, a rich gold, then deep orange going red-orange at the end
const glm::vec3 SUN_COL[12] = {
	{1.00f, 0.95f, 0.86f}, {1.00f, 0.93f, 0.82f}, {1.00f, 0.91f, 0.77f}, {1.00f, 0.87f, 0.70f}, // warm day
	{1.00f, 0.80f, 0.42f}, {1.00f, 0.75f, 0.33f}, {1.00f, 0.70f, 0.26f}, {1.00f, 0.64f, 0.20f}, {1.00f, 0.57f, 0.15f}, // deep gold
	{1.00f, 0.46f, 0.19f}, {1.00f, 0.38f, 0.15f}, {1.00f, 0.30f, 0.12f}}; // orange to red-orange
// the morning sun from darkness 4 on: paler and pinker than the evening's
const glm::vec3 DAWN_COL[12] = {
	{1.00f, 0.95f, 0.86f}, {1.00f, 0.93f, 0.82f}, {1.00f, 0.91f, 0.77f}, {1.00f, 0.87f, 0.72f},
	{1.00f, 0.84f, 0.68f}, {1.00f, 0.79f, 0.64f}, {1.00f, 0.74f, 0.61f}, {1.00f, 0.69f, 0.59f}, {1.00f, 0.64f, 0.58f},
	{1.00f, 0.59f, 0.57f}, {1.00f, 0.54f, 0.56f}, {0.98f, 0.49f, 0.55f}};
// the sky's fill light (what lights the shadows): a clear blue day, a cool blue-violet dusk and a
// lighter lavender dawn, so the warm sunlit faces stand out against cool shadows
const glm::vec3 DAY_FILL(0.16f, 0.19f, 0.24f), DUSK_FILL(0.085f, 0.085f, 0.175f), DAWN_FILL(0.11f, 0.10f, 0.18f);
glm::vec3 sunColour(int shade, bool morning) { shade = glm::clamp(shade, 0, 11); return morning ? DAWN_COL[shade] : SUN_COL[shade]; }

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

/// Thunder, made in code (the game has none): a low rumble (brown noise, filtered) rolling in a few swells.
/// Near: a crack first, about 4 s. Far: no crack, duller (one more low-pass), a soft start and a longer roll,
/// about 6 s. Each variant is its own noise; every one is scaled to the same peak (distance is the volume's job).
std::vector<float> synthThunder(bool nearStrike, int variant, int freq)
{
	Rng rng(0x7417u + (uint32_t)variant * 7919u + (nearStrike ? 0u : 104729u));
	const float len = nearStrike ? 4.f : 6.f;
	const int n = (int)(freq * len);
	std::vector<float> out((size_t)n, 0.f);
	// the filters are tuned for 22-44 kHz (the coefficients are per sample)
	const float cut = (nearStrike ? 0.08f : 0.05f) * 44100.f / (float)freq;
	float brown = 0.f, lp1 = 0.f, lp2 = 0.f, lp3 = 0.f, crackLp = 0.f;
	struct Swell { float at, len, gain; };
	std::vector<Swell> swells;
	// a near clap starts loud: its first swell comes right after the crack
	if (nearStrike) swells.push_back(Swell{rng.r(0.02f, 0.2f), rng.r(0.3f, 0.8f), 1.f});
	for (int i = 0; i < (nearStrike ? 4 : 6); ++i)
		swells.push_back(nearStrike ? Swell{rng.r(0.05f, 2.2f), rng.r(0.4f, 1.3f), rng.r(0.45f, 1.f)}
			: Swell{rng.r(0.2f, 3.6f), rng.r(0.8f, 2.0f), rng.r(0.5f, 1.f)});
	float peak = 1e-6f;
	for (int i = 0; i < n; ++i)
	{
		float t = (float)i / freq;
		float w = rng.f() * 2.f - 1.f;
		brown = (brown + w * 0.04f) * 0.996f;
		lp1 += (brown - lp1) * cut;
		lp2 += (lp1 - lp2) * cut;
		lp3 += (lp2 - lp3) * cut;
		crackLp += (w - crackLp) * 0.35f;
		float env = 0.f;
		for (const Swell &sw : swells)
		{
			float x = (t - sw.at) / sw.len;
			if (x > -1.f && x < 3.f) env += sw.gain * (x < 0.f ? (1.f + x) : std::exp(-x * 1.4f));
		}
		env *= std::exp(-t * (nearStrike ? 0.55f : 0.35f));
		float v;
		if (nearStrike) v = lp2 * env + (t < 0.25f ? std::exp(-t * 22.f) * crackLp * 0.055f : 0.f);
		else v = lp3 * env * std::min(1.f, t / 0.35f);
		out[(size_t)i] = v;
		peak = std::max(peak, std::fabs(v));
	}
	const int fade = freq / 4;
	for (int i = 0; i < n; ++i)
	{
		float v = out[(size_t)i] * 0.8f / peak;
		if (i > n - fade) v *= (float)(n - i) / fade;
		out[(size_t)i] = v;
	}
	return out;
}

const int THUNDER_VARIANTS = 4;

/// A thunder variant as a mixer chunk, made the first time it is needed.
Mix_Chunk *thunderSound(bool nearStrike, int variant)
{
	static Mix_Chunk *chunks[2][THUNDER_VARIANTS] = {};
	static bool tried[2][THUNDER_VARIANTS] = {};
	int k = nearStrike ? 0 : 1;
	variant = ((variant % THUNDER_VARIANTS) + THUNDER_VARIANTS) % THUNDER_VARIANTS;
	if (tried[k][variant]) return chunks[k][variant];
	tried[k][variant] = true;
	int freq = 0, channels = 0;
	Uint16 format = 0;
	if (!Mix_QuerySpec(&freq, &format, &channels) || format != AUDIO_S16SYS || channels < 1) return nullptr;
	std::vector<float> s = synthThunder(nearStrike, variant, freq);
	Uint8 *buf = (Uint8*)SDL_malloc(s.size() * channels * 2);
	if (!buf) return nullptr;
	Sint16 *out = (Sint16*)buf;
	for (size_t i = 0; i < s.size(); ++i)
		for (int c = 0; c < channels; ++c) out[i * channels + c] = (Sint16)(glm::clamp(s[i], -1.f, 1.f) * 32000.f);
	chunks[k][variant] = Mix_QuickLoad_RAW(buf, (Uint32)(s.size() * channels * 2));
	return chunks[k][variant];
}

// ---- rain and wind, made in code like the thunder: loops a few seconds long, their ends crossfaded into
// their starts so they repeat without a seam (the filters are tuned for 22-44 kHz)

/// A one-pole low-pass at about hz.
struct LowPass
{
	float a = 0.f, y = 0.f;
	LowPass(float hz, int freq) { a = 1.f - std::exp(-6.2832f * hz / (float)freq); }
	float operator()(float x) { y += (x - y) * a; return y; }
};
/// A resonant band-pass (state-variable filter) whose centre can move.
struct BandPass
{
	float low = 0.f, band = 0.f;
	float operator()(float x, float hz, float q, int freq)
	{
		float f = 2.f * std::sin(3.14159f * std::min(hz, freq * 0.2f) / (float)freq);
		float high = x - low - band / q;
		band += f * high;
		low += f * band;
		return band;
	}
};

/// Loops `len` seconds: makes `len + xf` with gen, then fades the extra into the start (equal power: the
/// layers are noise, so they don't cancel).
template <class Gen> std::vector<float> loopOf(float len, float xf, int freq, Gen gen)
{
	const int n = (int)(len * freq), x = (int)(xf * freq);
	std::vector<float> s((size_t)(n + x));
	for (int i = 0; i < n + x; ++i) s[(size_t)i] = gen(i);
	for (int i = 0; i < x; ++i)
	{
		float k = (float)i / x;
		s[(size_t)i] = s[(size_t)i] * std::sqrt(k) + s[(size_t)(n + i)] * std::sqrt(1.f - k);
	}
	s.resize((size_t)n);
	return s;
}
/// Scales a sound to an RMS level (clipping the rare peak softly).
void toRms(std::vector<float> &s, float rms)
{
	double sum = 0.0;
	for (float v : s) sum += (double)v * v;
	float cur = (float)std::sqrt(sum / std::max<size_t>(1, s.size()));
	float g = cur > 1e-9f ? rms / cur : 0.f;
	for (float &v : s) v = std::tanh(v * g);
}

/// Drops landing: each a tick of bright noise, now and then with a short ring (a drop on a puddle or a
/// hard surface). `rate` drops a second, sizes mostly small with the odd big one; `bright` how far up
/// their noise reaches (a muffled 0 .. a crisp 1).
struct Drops
{
	struct Hit { int at; float amp, tau, ring, ringHz, ringTau; };
	std::vector<Hit> hits;
	Rng rng;
	int freq;
	LowPass lp;
	float hp = 0.f;
	Drops(uint32_t seed, float rate, float bright, int n, int freq_) : rng(seed), freq(freq_), lp(2500.f + 6000.f * bright, freq_)
	{
		float t = 0.f;
		while (true)
		{
			t += -std::log(std::max(1e-6f, rng.f())) / rate;
			int at = (int)(t * freq);
			if (at >= n) break;
			float a = std::pow(rng.f(), 2.5f) * 0.9f + 0.1f;
			bool ring = rng.f() < 0.35f;
			hits.push_back(Hit{at, a, rng.r(0.0008f, 0.003f), ring ? a * rng.r(0.2f, 0.45f) : 0.f, rng.r(1400.f, 4200.f), rng.r(0.006f, 0.02f)});
		}
	}
	size_t next = 0;
	std::vector<Hit> live;
	float operator()(int i)
	{
		while (next < hits.size() && hits[next].at <= i) live.push_back(hits[next++]);
		float noise = 0.f, tone = 0.f;
		for (size_t k = 0; k < live.size();)
		{
			const Hit &h = live[k];
			float t = (float)(i - h.at) / freq;
			float e = std::exp(-t / h.tau), er = h.ring > 0.f ? std::exp(-t / h.ringTau) : 0.f;
			noise += h.amp * e;
			// the ring falls a little in pitch as it fades
			if (h.ring > 0.f) tone += h.ring * er * std::sin(6.2832f * h.ringHz * t * (1.f - 0.15f * t / (h.ringTau * 4.f)));
			if (e < 0.002f && er < 0.002f) { live[k] = live.back(); live.pop_back(); }
			else ++k;
		}
		float w = rng.f() * 2.f - 1.f;
		// the ticks' noise: band-limited (a high-pass by subtracting a slow copy, then the brightness low-pass)
		hp += (w - hp) * 0.08f;
		return lp((w - hp) * noise) + tone;
	}
};

/// The rain all around the room, as heard from indoors: a muffled wash with drops in it, a little
/// different in each ear (so it surrounds rather than sitting in the middle of the head). Strength: 0
/// drizzle, 1 steady, 2 thunderstorm.
std::vector<float> synthRainRoom(int strength, int ear, int freq)
{
	static const float rate[3] = {120.f, 450.f, 1400.f}, washK[3] = {0.25f, 0.55f, 1.f}, cutHz[3] = {1300.f, 1700.f, 2300.f};
	const float len = 6.f, xf = 0.6f;
	const int n = (int)((len + xf) * freq);
	Rng rng(0x5a17u + (uint32_t)strength * 31u + (uint32_t)ear * 7777u);
	Drops drops(0xd409u + (uint32_t)strength * 131u + (uint32_t)ear * 911u, rate[strength], 0.f, n, freq);
	LowPass wash1(cutHz[strength], freq), wash2(cutHz[strength], freq), dropLp(cutHz[strength] * 1.4f, freq), rumble(120.f, freq);
	float hp = 0.f;
	float brown = 0.f;
	std::vector<float> s = loopOf(len, xf, freq, [&](int i)
	{
		float w = rng.f() * 2.f - 1.f;
		float hiss = wash2(wash1(w));
		hp += (hiss - hp) * (150.f * 6.2832f / freq);
		brown = (brown + w * 0.02f) * 0.998f;
		return (hiss - hp) * washK[strength] + dropLp(drops(i)) * 0.6f + rumble(brown) * 0.4f * washK[strength];
	});
	toRms(s, 0.16f);
	return s;
}

/// The rain on the table: drops pattering close by, crisp, with a little hiss under them.
std::vector<float> synthRainTable(int strength, int freq)
{
	static const float rate[3] = {35.f, 140.f, 420.f}, hissK[3] = {0.04f, 0.1f, 0.2f};
	const float len = 4.7f, xf = 0.4f;
	const int n = (int)((len + xf) * freq);
	Rng rng(0x7ab1u + (uint32_t)strength * 53u);
	Drops drops(0x91c3u + (uint32_t)strength * 17u, rate[strength], 0.7f, n, freq);
	LowPass hissLp(7000.f, freq);
	float hp = 0.f;
	std::vector<float> s = loopOf(len, xf, freq, [&](int i)
	{
		float w = hissLp(rng.f() * 2.f - 1.f);
		hp += (w - hp) * (2500.f * 6.2832f / freq);
		return drops(i) + (w - hp) * hissK[strength];
	});
	toRms(s, strength == 0 ? 0.07f : 0.12f);
	return s;
}

/// A blizzard's wind: noise through two moving resonances (the howl) over a low rush, swelling in gusts.
std::vector<float> synthWind(int freq)
{
	const float len = 12.f, xf = 1.5f;
	Rng rng(0x3e1du);
	BandPass b1, b2;
	LowPass rush(300.f, freq), rush2(300.f, freq);
	std::vector<float> s = loopOf(len, xf, freq, [&](int i)
	{
		float t = (float)i / freq;
		float w = rng.f() * 2.f - 1.f;
		float g = 0.55f + 0.3f * std::sin(t * 6.2832f / 6.5f) + 0.15f * std::sin(t * 6.2832f / 2.3f + 1.f);
		float f1 = 280.f + 220.f * g + 60.f * std::sin(t * 6.2832f / 1.7f);
		float f2 = 650.f + 400.f * g + 90.f * std::sin(t * 6.2832f / 2.9f + 2.f);
		float howl = b1(w, f1, 6.f, freq) * 0.5f + b2(w, f2, 9.f, freq) * 0.25f * g;
		return (howl + rush2(rush(w)) * 2.5f) * g;
	});
	toRms(s, 0.15f);
	return s;
}

enum Loop { LOOP_ROOM, LOOP_TABLE, LOOP_WIND, LOOPS };
const AudioSlot LOOP_SLOT[LOOPS] = {AUDIO_RAIN_ROOM, AUDIO_RAIN_TABLE, AUDIO_WIND};

/// A weather loop as a mixer chunk (the room's rain in stereo), made the first time it is needed and kept.
Mix_Chunk *loopSound(int loop, int strength)
{
	static Mix_Chunk *chunks[LOOPS][3] = {};
	static bool tried[LOOPS][3] = {};
	strength = glm::clamp(strength, 0, 2);
	if (tried[loop][strength]) return chunks[loop][strength];
	int freq = audioFreq();
	if (!freq) return nullptr;
	tried[loop][strength] = true;
	if (loop == LOOP_ROOM) chunks[loop][strength] = makeStereoChunk(synthRainRoom(strength, 0, freq), synthRainRoom(strength, 1, freq));
	else chunks[loop][strength] = makeChunk(loop == LOOP_TABLE ? synthRainTable(strength, freq) : synthWind(freq));
	return chunks[loop][strength];
}
}

bool writeWeatherLoopWav(const std::string &path, const std::string &which, int strength)
{
	const int freq = 44100;
	strength = glm::clamp(strength, 0, 2);
	std::vector<float> s = which == "room" ? synthRainRoom(strength, 0, freq) : which == "table" ? synthRainTable(strength, freq) : synthWind(freq);
	bool ok = writeWav(path, s, freq);
	Log(LOG_INFO) << "[VR] weather loop " << which << " " << strength << (ok ? " written to " : " NOT written to ") << path;
	return ok;
}

bool writeThunderWav(const std::string &path, bool nearStrike, int variant)
{
	const int freq = 44100;
	std::vector<float> s = synthThunder(nearStrike, variant, freq);
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
	uint32_t bytes = (uint32_t)s.size() * 2;
	fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32(freq); u32(freq * 2); u16(2); u16(16);
	fwrite("data", 1, 4, f); u32(bytes);
	for (float v : s) { int16_t x = (int16_t)(glm::clamp(v, -1.f, 1.f) * 32000.f); fwrite(&x, 2, 1, f); }
	fclose(f);
	Log(LOG_INFO) << "[VR] thunder " << (nearStrike ? "near" : "far") << " " << variant << " written to " << path;
	return true;
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
	float cloudSpeed = 16.f;            // cloud shadows' drift (map voxels/s), before gusts
	Rng rng;
	double time = 0.0;

	// lightning
	float nextStrike = 0.f;
	float strikeT = -1.f;               // seconds since the last strike started (<0 none)
	float pulse[3] = {0.f, 0.f, 0.f};   // its pulses: start times
	int pulses = 0;
	float thunderIn = -1.f, thunderVol = 1.f;
	glm::vec2 strikeDir{0.f, -1.f};     // where the strike was (map xz, unit): its thunder comes from there
	int thunderVariant = 0;
	int thunderSlot = 0;                // two channels, so a new clap doesn't cut the last one's roll
	Rng trng{0x7a3du};                  // the thunder's own dice (the weather's stay as they were)
	struct Rolling { int channel; glm::vec2 dir; };
	std::vector<Rolling> rolling;       // thunder still playing: kept in its direction as the head turns
	int strikes = 0;
	int lightning = 0;                  // 0 none, 1 now and then far off, 2 often, near and far
	float flashAmp = 1.f;               // this strike's brightness (near 1, far dim)
	bool lastNear = false;
	int strength = 1;                   // rain/snow: 0 light, 1 steady, 2 heavy
	// the rain's and wind's loops (LOOP_ROOM, LOOP_TABLE, LOOP_WIND): each fades in and out on its own channel
	struct LoopPlay { int channel = -1, key = -1; float level = 0.f, volume = 0.f, want = 0.f; };
	LoopPlay loops[LOOPS];
	void updateSound(float dt, const WeatherView &v);
	void reset();
	void stopSound(int fadeMs)
	{
		for (LoopPlay &l : loops)
		{
			fadeOut(l.channel, fadeMs);
			l = LoopPlay();
		}
	}
	bool haze = false;                  // a blizzard's pale haze (drawn like the mist)
	float gustPhase = 0.f;
	float gust() const                  // steady and heavy rain/snow rise and ease over a minute or so
	{
		if ((kind != RAIN && kind != SNOW) || strength == 0) return 1.f;
		return 1.f + 0.3f * (float)std::sin(time * 6.2832 / 55.0 + gustPhase) + 0.1f * (float)std::sin(time * 6.2832 / 17.0 + gustPhase * 2.0);
	}

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

	glm::vec3 baseCol{0.f};             // the sun's or moon's light

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
	/// A direction on the map (xz) as heard: degrees clockwise from where the head faces.
	float thunderAngle(const WeatherView &v, const glm::vec2 &dir) const
	{
		glm::vec2 f(v.viewerFwd.x, v.viewerFwd.z);
		if (glm::length(f) < 1e-4f) return 0.f;
		f = glm::normalize(f);
		glm::vec2 r(-f.y, f.x);
		return std::atan2(glm::dot(dir, r), glm::dot(dir, f)) * 180.f / PI;
	}
	void startStrike()
	{
		strikeT = 0.f;
		pulses = 1 + (int)(rng.f() * 3.f);
		pulse[0] = 0.f;
		for (int i = 1; i < pulses; ++i) pulse[i] = pulse[i - 1] + rng.r(0.08f, 0.22f);
		// near or far: a thunderstorm half the time near, steady rain only far
		bool nearStrike = lightning == 2 && rng.f() < 0.5f;
		if (nearStrike) { flashAmp = 1.f; thunderIn = rng.r(0.15f, 0.7f); thunderVol = 1.f; }
		else { flashAmp = rng.r(0.28f, 0.45f); thunderIn = rng.r(2.f, 5.f); thunderVol = rng.r(0.3f, 0.45f); }
		nextStrike = lightning == 2 ? rng.r(4.f, 10.f) : rng.r(15.f, 35.f);
		lastNear = nearStrike;
		float az = trng.r(0.f, 2.f * PI);
		strikeDir = glm::vec2(std::sin(az), -std::cos(az)); // bearing from the map's north, clockwise
		thunderVariant = (int)(trng.f() * THUNDER_VARIANTS) % THUNDER_VARIANTS;
		Log(LOG_INFO) << "[VR] lightning: " << (nearStrike ? "near" : "far") << " strike, bearing " << (int)std::lround(az * 180.f / PI)
			<< ", thunder variant " << thunderVariant;
		++strikes;
	}
};

Weather::Weather() : _p(new Impl()) {}
Weather::~Weather() = default;

void Weather::force(const std::string &kind)
{
	// rain and snow come at steady strength when forced; forcing the same again steps through the
	// strengths (drizzle / steady / thunderstorm, flurries / steady / blizzard); "rain:2" picks one
	std::string k = kind;
	int want = -1;
	size_t c = k.find(':');
	if (c != std::string::npos) { want = std::atoi(k.c_str() + c + 1) % 3; k = k.substr(0, c); }
	bool fall = k == "rain" || k == "snow";
	if (!fall) forcedStrength = -1;
	else if (want >= 0) forcedStrength = want;
	else if (forced == k && forcedStrength >= 0) forcedStrength = (forcedStrength + 1) % 3;
	else forcedStrength = 1;
	forced = k;
}
int Weather::forcedWeatherStrength() { return forcedStrength; }
std::string Weather::forcedWeather() { return forced; }
float Weather::forcedSun() { return forcedAz; }
void Weather::forceSun(float azimuth) { forcedAz = azimuth; }
void Weather::forceClock(int day, int hour, int minute) { clockDay = day; clockHour = hour; clockMinute = minute; }
void Weather::shiftClock(double days, bool reset) { skyShift = reset ? 0.0 : skyShift + days; }
double Weather::clockShift() { return skyShift; }

const SkyLight &Weather::sky() const { return _p->sky; }

void Weather::end()
{
	_p->reset();
	_p->stopSound(800);
}

/// Back to no weather; the sound carries on (a re-roll keeps it, or fades it to the new weather's).
void Weather::Impl::reset()
{
	Impl &p = *this;
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
	Impl &p = *_p;
	p.reset();
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
	if (skyShift != 0.0) dateOf(daysJ2000(cYear, cMonth, cDay, cHours) + skyShift, cYear, cMonth, cDay, cHours);
	{
		char b[64];
		int hh = (int)cHours, mm = (int)((cHours - hh) * 60.0 + 0.5);
		if (mm == 60) { mm = 0; ++hh; }
		snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d GMT", cYear, cMonth, cDay, hh % 24, mm);
		I.skyClock = b;
		I.skyShift = skyShift;
		I.gmtH = hh % 24; I.gmtM = mm;
		I.localH = (float)std::fmod(cHours + I.lon / 15.0 + 48.0, 24.0);
	}
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
			I.realAz = std::fmod(std::atan2(e, n) * 180.f / PI + 360.f, 360.f);
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
	// the moon over the site, day or night (the debug panel's sky marker shows it either way)
	if (site && gt && !p.mars)
	{
		moonAt(daysJ2000(cYear, cMonth, cDay, cHours), I.lat, I.lon, I.moonAlt, I.moonAz, I.moonIllum, I.moonWaxing);
		I.moonKnown = true;
	}
	float elev;
	glm::vec3 col, skyCol;
	bool night = shade >= 12;
	const glm::vec3 NIGHT_SKY(0.045f, 0.055f, 0.095f);   // starlight
	if (!night)
	{
		elev = SUN_ELEV[shade];
		col = sunColour(shade, morning) * (0.72f / std::max(std::sin(elev * PI / 180.f), 0.42f));
		// the evening's golden hour glows: a deep gold reads as brown on ground the game has darkened
		if (!morning && shade >= 4 && shade <= 8) col *= 1.25f;
		float k = shade / 11.f;
		skyCol = glm::mix(DAY_FILL, morning ? DAWN_FILL : DUSK_FILL, k);
		// twilight: the last of the sun fades as the night sky takes over
		if (shade == 10) { col *= 0.75f; skyCol = glm::mix(skyCol, NIGHT_SKY, 0.35f); }
		if (shade == 11) { col *= 0.5f; skyCol = glm::mix(skyCol, NIGHT_SKY, 0.65f); }
		p.skyName = shade <= 3 ? "day" : (morning ? "dawn" : "dusk");
	}
	else
	{
		// the moon: where it really is over the site at that hour and how full it is
		float malt = 40.f, maz = std::fmod(az + 180.f, 360.f), illum = 0.6f;
		bool waxing = true;
		if (I.moonKnown)
		{
			malt = I.moonAlt; maz = I.moonAz; illum = I.moonIllum; waxing = I.moonWaxing;
			I.sunFrom = "the real moon at the site";
		}
		else I.sunFrom = p.mars ? "rolled (Mars)" : "rolled (no site)";
		I.moonIllum = illum; I.moonAlt = malt; I.moonAz = maz; I.moonWaxing = waxing;
		az = maz;
		// a moon under the horizon is mirrored above it (same direction, as high as it is low), so every
		// night has a moon; its light follows the phase but never drops below 40% of a full moon's
		I.moonMirrored = malt < 0.f;
		if (I.moonMirrored && I.moonKnown) I.sunFrom = "the real moon, mirrored above the horizon";
		I.tableMoonAlt = std::fabs(malt);
		I.moonLight = std::max(illum, 0.4f);
		elev = std::max(I.tableMoonAlt, 20.f); // a low moon's shadows stop at about 2.7 times what casts them
		skyCol = NIGHT_SKY;
		col = glm::vec3(0.50f, 0.62f, 1.00f) * (0.08f + 0.52f * I.moonLight) * (0.55f / std::max(std::sin(elev * PI / 180.f), 0.5f));
		skyCol += glm::vec3(0.01f, 0.012f, 0.02f) * I.moonLight;
		p.skyName = I.moonMirrored ? "night (moon mirrored)" : "night";
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
	float r1 = rng.f(), r2 = rng.f(), r3 = rng.f(), r4 = rng.f(), r5 = rng.f();
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
	else if (shade >= 12)
	{
		// at night their shadows make the moonlight come and go in patches
		I.rollClouds = roll("night clouds", r2, 0.6f); if (r2 < 0.6f) cloud = 0.65f + 0.2f * r3;
	}
	else if (shade > 7) I.rollClouds = "clouds: too dark (needs <= 7 or a moonlit night)";
	else { I.rollClouds = roll("clouds", r2, 0.4f); if (r2 < 0.4f) cloud = 0.60f + 0.15f * r3; }
	if (p.mars) I.rollMist = "mist: none on Mars";
	else if (kind == DUST) I.rollMist = "mist: not in a dust storm";
	else if (!morning) I.rollMist = "mist: not morning";
	else if (shade < 3 || shade > 10) I.rollMist = "mist: needs darkness 3-10";
	else { I.rollMist = roll("mist", r4, 0.3f); mist = r4 < 0.3f; }
	if (!Options::vrWeather) { kind = NONE; cloud = 0.f; mist = false; }
	if (forced != "auto")
	{
		kind = NONE; mist = false; cloud = 0.f;
		if (forced == "rain") kind = RAIN;
		else if (forced == "snow") kind = SNOW;
		else if (forced == "dust") kind = DUST;
		else if (forced == "sand") kind = SAND;
		else if (forced == "mist") mist = true;
		else if (forced == "clouds") cloud = night ? 0.75f : 0.68f;
	}
	// rain and snow come light (40%), steady (40%) or heavy (20%): drizzle / steady rain / thunderstorm,
	// flurries / steady snow / blizzard
	int strength = 1;
	if (kind == RAIN || kind == SNOW)
	{
		strength = r5 < 0.4f ? 0 : r5 < 0.8f ? 1 : 2;
		if (forced != "auto" && forcedStrength >= 0) strength = forcedStrength;
		I.rollStrength = std::string(STRENGTH_NAME[kind == SNOW][strength]) + (forced != "auto" ? " (forced)" : fmt2(": rolled %.2f (light < 0.40, steady < 0.80, heavy above)", r5));
	}
	else I.rollStrength = "";
	p.strength = strength;
	bool haze = false;
	// overcast under rain and snow, hazy in a sandstorm
	if (kind == RAIN)
	{
		static const float sun[3] = {0.8f, 0.55f, 0.4f}, sky[3] = {1.1f, 1.25f, 1.3f}, cl[3] = {0.3f, 0.5f, 0.75f};
		col *= sun[strength]; skyCol *= sky[strength]; cloud = std::max(cloud, cl[strength]);
	}
	if (kind == SNOW)
	{
		static const float sun[3] = {0.85f, 0.7f, 0.5f}, cl[3] = {0.25f, 0.35f, 0.7f};
		col *= sun[strength]; skyCol = skyCol * 1.2f + glm::vec3(0.02f, 0.025f, 0.035f); cloud = std::max(cloud, cl[strength]);
		haze = strength == 2; // a blizzard: a pale haze over the map
	}
	if (kind == DUST || kind == SAND) { col *= 0.82f; skyCol = skyCol * 1.1f + glm::vec3(0.03f, 0.02f, 0.01f); }
	p.kind = kind;
	p.mist = mist;
	p.haze = haze;
	p.sky.col = col;
	p.baseCol = col;
	// a moonlit night: the map's unlit ground shows faintly in the moonlight (a full, high moon the most),
	// so the moon and its cloud shadows can be seen; a moonless night stays as black as the game's
	p.sky.moonFloor = night ? 0.05f * I.moonLight * glm::smoothstep(0.f, 25.f, I.tableMoonAlt) : 0.f;
	p.sky.sky = skyCol;
	p.sky.cloud = cloud;
	p.sky.wind = p.wind;
	p.sky.wet = kind == RAIN ? 1.f : 0.f;
	// golden hour and sunset: the sunlit ground glows as if the map were a few darkness steps lighter;
	// the shade keeps the game's darkness, so shadows stand out against it
	{
		int lift = shade >= 4 && shade <= 8 ? 3 : shade == 9 || shade == 10 ? 2 : shade == 11 ? 1 : 0;
		p.sky.boost = lift > 0 ? shadeLight(shade - lift) / std::max(shadeLight(shade), 1e-3f) : 1.f;
	}
	p.windSpeed = kind == DUST || kind == SAND ? rng.r(35.f, 55.f) : rng.r(10.f, 28.f);
	float windT = kind == DUST || kind == SAND ? (p.windSpeed - 35.f) / 20.f : (p.windSpeed - 10.f) / 18.f;
	{
		// the clouds ride the same wind: 40-100% of a tile a second, storms (steady rain, thunderstorms,
		// blizzards, dust, sand) 70-100% of about 1.4 tiles a second
		bool storm = kind == DUST || kind == SAND || (kind == RAIN && strength >= 1) || (kind == SNOW && strength == 2);
		p.cloudSpeed = storm ? 22.f * (0.7f + 0.3f * windT) : 16.f * (0.4f + 0.6f * windT);
	}
	// light rain and snow fall nearly straight, a thunderstorm drives the rain, a blizzard the snow sideways
	if (kind == RAIN) p.windSpeed *= strength == 0 ? 0.5f : strength == 2 ? 1.6f : 1.f;
	if (kind == SNOW) p.windSpeed *= strength == 0 ? 0.6f : strength == 2 ? 2.4f : 1.f;
	// lightning: none in a drizzle, now and then far off in steady rain, often near and far in a storm
	p.lightning = kind == RAIN ? strength : 0;
	p.nextStrike = p.lightning == 2 ? rng.r(3.f, 8.f) : rng.r(8.f, 20.f);
	p.gustPhase = rng.r(0.f, 6.2832f);
	// mist takes the colour of the low sun, as bright as the morning allows
	float light = shadeLight(shade);
	p.mistCol = glm::mix(glm::vec3(0.86f, 0.87f, 0.90f), sunColour(shade, morning), 0.3f) * glm::clamp(light * 2.f, 0.15f, 0.8f);
	p.mistAlpha = mist ? 0.065f : 0.f;
	if (haze) { p.mistCol = glm::vec3(0.86f, 0.89f, 0.95f) * glm::clamp(light * 2.f, 0.15f, 0.85f); p.mistAlpha = 0.09f; }
	I.sky = p.skyName;
	I.sunAz = az; I.sunElev = elev;
	I.sunCol = col; I.skyCol = skyCol;
	I.weather = kind == RAIN || kind == SNOW ? STRENGTH_NAME[kind == SNOW][strength] : kindName(kind);
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

/// The rain's sound: a wash all around the room and drops pattering on the table (from where the table is,
/// quieter the further away), louder the harder it rains and rising and easing with the gusts; a
/// blizzard's wind, leaning towards the ear it blows from. Flurries and steady snow are silent.
void Weather::Impl::updateSound(float dt, const WeatherView &v)
{
	static const float ROOM[3] = {0.3f, 0.5f, 0.75f}, TABLE[3] = {0.45f, 0.6f, 0.8f}, WIND = 0.7f;
	const float IN_S = 2.f, OUT_S = 1.f;  // fades: when the weather starts, and when it stops or changes
	bool rain = kind == RAIN, blizzard = kind == SNOW && strength == 2;
	float g = strength >= 1 ? 0.8f + 0.2f * gust() : 1.f;
	loops[LOOP_ROOM].want = rain ? ROOM[strength] * g : 0.f;
	loops[LOOP_TABLE].want = rain ? TABLE[strength] * g : 0.f;
	loops[LOOP_WIND].want = blizzard ? WIND * (0.55f + 0.45f * gust() / 1.4f) : 0.f;
	// where the table's middle is, as heard
	glm::vec3 c(v.center.x, v.top * 0.3f, v.center.y);
	glm::vec2 toTable(c.x - v.viewer.x, c.z - v.viewer.z);
	float tableAngle = glm::length(toTable) > 1e-3f ? thunderAngle(v, glm::normalize(toTable)) : 0.f;
	float tableDist = glm::length(c - v.viewer) * v.metersPerVoxel;    // metres
	// the wind comes from upwind: which ear that is
	float windPan = 0.f;
	{
		glm::vec2 f(v.viewerFwd.x, v.viewerFwd.z);
		if (glm::length(f) > 1e-4f) { f = glm::normalize(f); windPan = glm::dot(-wind, glm::vec2(-f.y, f.x)); }
	}
	for (int i = 0; i < LOOPS; ++i)
	{
		LoopPlay &l = loops[i];
		int key = i == LOOP_WIND ? 0 : strength;
		if (l.channel >= 0 && !playing(l.channel)) { Log(LOG_INFO) << "[VR] weather sound: loop " << i << " was stopped (the game re-opened its sound?)"; l.channel = -1; }
		if (l.want > 0.f && (l.channel < 0 || l.key != key))
		{
			// a new strength starts over from silence; the old one is cut (the forced changes in tests)
			if (l.channel >= 0 && l.key != key) l.level = 0.f;
			bool was = l.channel >= 0;
			l.channel = playLoop(LOOP_SLOT[i], loopSound(i, key));
			l.key = key;
			Log(LOG_INFO) << "[VR] weather sound: loop " << i << " strength " << key << (was ? " restarted" : " started") << " on channel " << l.channel;
		}
		if (l.want > 0.f) { l.volume = l.want; l.level = std::min(1.f, l.level + dt / IN_S); }
		else l.level = std::max(0.f, l.level - dt / OUT_S);
		if (l.channel < 0) continue;
		if (l.level <= 0.f && l.want <= 0.f) { Mix_HaltChannel(l.channel); l.channel = -1; l.key = -1; continue; }
		setLevel(l.channel, l.volume * l.level);
		if (i == LOOP_TABLE) placeLoop(l.channel, tableAngle, glm::smoothstep(0.4f, 4.f, tableDist) * 0.8f);
		else if (i == LOOP_WIND) placeLoop(l.channel, 0.f, 0.f, windPan);
	}
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
	static const float rainK[3] = {0.8f, 1.f, 1.2f}, snowK[3] = {0.8f, 1.f, 1.7f};
	if (kind == RAIN) { d.speed = rng.r(150.f, 190.f) * rainK[strength]; d.size = rng.r(7.f, 11.f) * (strength == 0 ? 0.7f : strength == 2 ? 1.25f : 1.f); }
	else { d.speed = rng.r(10.f, 18.f) * snowK[strength]; d.size = rng.r(0.45f, 0.75f); }
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

void Weather::update(float dt, const WeatherView &v)
{
	Impl &p = *_p;
	if (!p.active || !p.outdoor) { p.stopSound(800); return; }  // (indoors: no weather to hear)
	dt = std::min(dt, 0.1f);
	p.time += dt;
	p.fade = v.fade;

	// clouds drift with the wind
	if (p.sky.cloud > 0.f)
	{
		// gusts: the drift wanders about 15% either way
		float gust = 1.f + 0.1f * (float)std::sin(p.time * 0.21) + 0.05f * (float)std::sin(p.time * 0.53 + 1.3);
		p.sky.cloudOffset += p.wind * p.cloudSpeed * gust * dt;
	}

	// thunder still rolling stays where its strike was while the head turns
	for (size_t i = 0; i < p.rolling.size();)
	{
		if (moveSound(p.rolling[i].channel, p.thunderAngle(v, p.rolling[i].dir))) ++i;
		else p.rolling.erase(p.rolling.begin() + (long)i);
	}

	// lightning: a few quick pulses, thunder a moment later
	p.sky.flash = 0.f;
	if (p.kind == RAIN)
	{
		p.nextStrike -= dt;
		if (p.lightning > 0 && p.nextStrike <= 0.f && p.strikeT < 0.f) p.startStrike();
		if (p.strikeT >= 0.f)
		{
			p.strikeT += dt;
			float f = 0.f;
			for (int i = 0; i < p.pulses; ++i)
			{
				float t = p.strikeT - p.pulse[i];
				if (t >= 0.f) f = std::max(f, (i == 0 ? 1.f : 0.75f) * std::exp(-t * 16.f));
			}
			p.sky.flash = f * p.flashAmp;
			if (p.strikeT > p.pulse[p.pulses - 1] + 0.6f) p.strikeT = -1.f;
		}
		if (p.thunderIn >= 0.f)
		{
			p.thunderIn -= dt;
			if (p.thunderIn < 0.f && !Options::mute)
				if (Mix_Chunk *c = thunderSound(p.lastNear, p.thunderVariant))
				{
					p.thunderSlot ^= 1;
					int ch = playAt(p.thunderSlot ? AUDIO_THUNDER_B : AUDIO_THUNDER_A, c, p.thunderVol, p.thunderAngle(v, p.strikeDir));
					if (ch >= 0)
					{
						p.rolling.erase(std::remove_if(p.rolling.begin(), p.rolling.end(), [ch](const Impl::Rolling &r) { return r.channel == ch; }), p.rolling.end());
						p.rolling.push_back({ch, p.strikeDir});
					}
				}
		}
	}

	p.updateSound(dt, v);

	// the open sky over the ground, for the mist (fog of war and roofs change)
	p.maskTimer -= dt;
	if ((p.mist || p.haze) && p.maskTimer <= 0.f && v.open)
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
	default: break;
	}
	if (p.kind == RAIN || p.kind == SNOW)
	{
		// light, steady or heavy, and gusting in the steady and heavy ones
		static const float dens[3] = {0.35f, 1.f, 1.8f};
		want = (size_t)((float)want * dens[p.strength] * p.gust());
	}
	switch (p.kind)
	{
	case RAIN: case SNOW: break;
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

	float gw = 0.85f + 0.15f * p.gust();
	glm::vec3 windV(p.wind.x * p.windSpeed * gw, 0.f, p.wind.y * p.windSpeed * gw);
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
	if (!parts && !p.mist && !p.haze) return;
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
	if ((p.mist || p.haze) && p.mx > 0 && p.my > 0)
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
		<< " moon=" << (int)(p.info.moonIllum * 100) << "%," << (int)p.info.moonAlt << (p.info.moonMirrored ? ",mirrored" : "") << " glow=" << std::lround(p.sky.boost * 100.f) << "%"
		<< " cloudspeed=" << std::lround(p.cloudSpeed)
		<< (p.kind == RAIN || p.kind == SNOW ? std::string(" strength=") + std::to_string(p.strength) : std::string());
	// the weather's sound: how loud each loop is meant to be now (% of the game's sound volume)
	static const char *loopName[LOOPS] = {"room", "table", "wind"};
	std::string snd;
	for (int i = 0; i < LOOPS; ++i)
		if (p.loops[i].want > 0.f) snd += std::string(snd.empty() ? "" : ",") + loopName[i] + ":" + std::to_string((int)std::lround(p.loops[i].want * 100.f));
	o << " sound=" << (snd.empty() ? "-" : snd);
	return o.str();
}

}
}
