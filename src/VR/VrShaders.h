#pragma once
/*
 * OXCE VR tabletop - GLSL sources.
 * Everything renders in linear space into an sRGB target.
 * Vertex colors are authored in sRGB and linearized in the vertex shader.
 */

namespace OpenXcom
{
namespace VR
{

static const char *kSceneVS = R"GLSL(
#version 330
in vec3 aPos;
in vec3 aNormal;
in vec2 aUV;
in vec4 aColor;
in float aMat;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform float uTime;
uniform vec4 uUVRect;        // uv = aUV * zw + xy (a sub-rectangle of a texture)
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;
flat out int vMat;
void main()
{
	vec4 w = uModel * vec4(aPos, 1.0);
	vWorld = w.xyz;
	vNormal = mat3(uModel) * aNormal;
	vUV = aUV * uUVRect.zw + uUVRect.xy;
	vColor = vec4(pow(aColor.rgb, vec3(2.2)), aColor.a);
	vMat = int(aMat + 0.5);
	if (vMat == 9)
	{
		// fog of war: the top of each fog column drifts up and down a little
		float ph = fract(sin(dot(floor(w.xz * 180.0), vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
		w.y += aUV.x * sin(uTime * 0.9 + ph) * 0.0016;
		vWorld = w.xyz;
	}
	gl_Position = uViewProj * w;
}
)GLSL";

static const char *kSceneFS = R"GLSL(
#version 330
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;
flat in int vMat;
uniform int uMode;          // 0 lit materials, 1 game panel, 2 unlit color, 3 lit texture w/ alpha test, 4 unlit texture w/ alpha
uniform sampler2D uTex;
uniform vec2 uTexSize;
uniform vec3 uEye;
uniform float uTime;
uniform vec4 uTint;
uniform float uScan;         // game panel scanlines: texels per game pixel row (1 normally, 4 for a 4x prescaled picture), 0 off
uniform int uLightCount;
uniform vec3 uLightPos[12];  // light 0 is the overhead light over the table and casts the shadows
uniform vec3 uLightCol[12];
uniform float uAmbient;      // room ambient scale (night missions are darker)
uniform float uAlert;        // 0..1: aliens in sight / alien turn, light strips go red
uniform float uNight;
uniform int uGlowCount;      // board only: light from glowing shots and blasts (world pos + radius, colour)
uniform vec4 uGlowPos[8];
uniform vec3 uGlowCol[8];        // 0..1: how dark the mission is; the room dims and its strips turn dim blue
uniform int uShadowOn;
uniform int uShadowPass;     // rendering the shadow map: depth only
uniform mat4 uShadowVP;
uniform sampler2D uShadowMap;
uniform float uShadowTexel;
uniform vec3 uRoomMin;       // the room shell, for soft shading where surfaces meet
uniform vec3 uRoomMax;
uniform vec4 uPedestal;      // xz rect of the table's foot (floor contact shadow)
uniform vec4 uTableRect;     // the holo table's glow: xz rect, lights undersides of things above it
uniform float uTableY;
uniform vec3 uTableGlow;
uniform int uHoleCount;      // open hatches in the table: these xz rects are cut out...
uniform vec4 uHoles[40];
uniform vec2 uHoleY;         // ...but only between these heights
uniform vec3 uSun;           // globe mode: world-space direction towards the sun
uniform vec4 uClip;          // world-space xz clip rectangle (xmin, zmin, xmax, zmax); disabled when xmin > xmax
uniform vec4 uSunKey;        // outdoor battle: xyz world direction towards the sun or moon, w 1 = on. It lights the
uniform vec3 uSunKeyCol;     // map (and what is over it) instead of light 0, and casts the shadows
uniform vec4 uSunArea;       // the map on the table (world xz rect): where the sun shines
uniform vec3 uSunKeySky;     // the sky's light over the map
uniform float uMapShade;     // outdoor battle: how bright the map's darkness leaves an unlit tile (the game's shade
uniform vec3 uLampCol;       // as a factor); tiles brighter than that carry the game's own light, lit in this colour
uniform float uMoonFloor;    // moonlit night: the unlit ground shows at least this bright under the moon
uniform float uFlash;        // lightning (0..1): lights the map's surfaces from their own colour, however dark the night
uniform vec3 uPoolSkyCol;     // soldiers' own light under the sun or moon: the sky light's colour
uniform float uPoolWarm;
uniform int uPoolRoofed;
uniform int uFigShadow;        // a standee is drawn: its sun shadow comes from its light-facing copy...
uniform vec3 uFigAxis, uFigRV, uFigRL, uFigNL, uFigUp; // ...(card middle, your card's across, the copy's across and facing, up)      // this draw (a figure) stands under a roof: the sky can't light its tile
uniform vec3 uPoolFillCol;    // ...and the sky's fill there
uniform float uPoolSunY;      // how high the sun or moon stands (sine, at least 0.2): flat ground gets the full light      // this draw's share of warm light (fires, flares) in the game's own light (x vertex alpha)
uniform vec4 uCloud;
uniform vec4 uCloud2;
uniform int uTerrain;        // the map's terrain is being drawn: matte under the sun and moon (no shine)...
uniform float uWet;
uniform float uSunBoost;     // the battle map's sunlit part: how much brighter than the game's darkness          // ...unless it is raining: wet ground glistens a little        // cloud wind direction (map xz), voxel size, stretch along the wind         // cloud shadows: strength, scale per map voxel, drift (map voxels)
uniform mat4 uMapInv;        // world -> battle map voxels
uniform int uFadeCount;      // mode 6 (mist): it parts around these (world xyz, radius)
uniform vec4 uFade[3];
uniform vec3 uCrt;           // mode 1: a CRT turning on or off: how far the picture is open (vertically, across), glow
uniform float uScanK;        // mode 1: how deep the scanlines go (0.12 normally)
uniform float uVignette;     // mode 1: darker toward the screen's edges (0 none)
uniform int uBuild;          // the motion scanner forming (or dissolving): nothing beyond this world plane...
uniform vec4 uBuildPlane;
uniform vec3 uBuildGlow;     // ...and a glowing band along it
uniform int uFeed;           // the motion scanner's camera: the picture in the radar's green, with the ping
uniform vec3 uFeedO;         // ...the soldier's tile (world)
uniform vec3 uFeedE;         // ...one tile east and south as world vectors over their squared length (dot gives tiles)
uniform vec3 uFeedS;
uniform vec4 uFeedClock;     // ...the ping: seconds into it, pings so far, the wave's speed (tiles/s), seconds between pings
uniform vec3 uFeedFill;      // ...the picture fills in over x seconds after the wave, each point stays y + up to z seconds
uniform int uFeedN;          // ...contacts: offset from the soldier (tiles), ring radius (tiles), brightness
uniform vec4 uFeedC[24];
out vec4 fragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p)
{
	float v = 0.0, a = 0.5;
	for (int i = 0; i < 5; ++i) { v += a * noise(p); p *= 2.03; a *= 0.5; }
	return v;
}
float gridLine(float x, float period, float width)
{
	float d = abs(fract(x / period + 0.5) - 0.5) * period;
	return 1.0 - smoothstep(0.0, width, d);
}

float shadowAt(vec3 pos, vec3 n)
{
	if (uShadowOn == 0) return 1.0;
	vec4 p = uShadowVP * vec4(pos + n * 0.006, 1.0);
	vec3 q = p.xyz / p.w * 0.5 + 0.5;
	if (q.x <= 0.0 || q.x >= 1.0 || q.y <= 0.0 || q.y >= 1.0 || q.z >= 1.0) return 1.0;
	float lit = 0.0;
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			float d = texture(uShadowMap, q.xy + vec2(x, y) * uShadowTexel).r;
			lit += (q.z - 0.0025 <= d) ? 1.0 : 0.0;
		}
	return mix(0.25, 1.0, lit / 9.0);
}

bool gNoSun = false;         // the holo glass around the map keeps the room's light (no sun glare on it)
bool gMap = false;           // a surface of the battle map: under the sun the room's lights stay off it

/// How much of a point is under the battle's sun: over the map, up to about head height above it.
float sunMask()
{
	if (uSunKey.w < 0.5 || gNoSun) return 0.0;
	vec2 q = max(max(uSunArea.xy - vWorld.xz, vWorld.xz - uSunArea.zw), vec2(0.0));
	float h = vWorld.y - uTableY;
	return (1.0 - smoothstep(0.0, 0.03, length(q))) * smoothstep(-0.12, -0.08, h) * (1.0 - smoothstep(0.6, 0.8, h));
}

/// Cloud shadows drifting over the battle map: 1 in the clear, less under a cloud.
// cloud shadows: a layer of voxel clouds (quarter-tile blocks) drifting with the wind, lumpy and
// clustered, stretched along the wind, their shadow cast along the sun onto the ground, walls and roofs.
// 0 in the open, uCloud.x right under a cloud.
float gCov = -1.0;   // this fragment's cover, worked out once
float gShN = -1.0;
float shadowFactor(vec3 n) { return shadowAt(vWorld, n); }   // this fragment's sun shadow (0 blocked .. 1 in the sun), worked out once
float gBoost = 1.0;  // the battle map's sunlit part: how much brighter than the game's darkness (glow)
float cloudCover()
{
	if (gCov >= 0.0) return gCov;
	gCov = 0.0;
	if (uCloud.x <= 0.0) return 0.0;
	vec3 mp = (uMapInv * vec4(vWorld, 1.0)).xyz;
	vec3 L = (uMapInv * vec4(uSunKey.xyz, 0.0)).xyz;
	L /= max(length(L), 1e-5);
	// along the sun up to the cloud layer, about 12 tiles over the ground
	vec2 xz = mp.xz + L.xz * ((200.0 - mp.y) / max(L.y, 0.12));
	// the cloud's own voxel grid, moving with it
	vec2 c = xz - uCloud.zw;
	float vs = uCloud2.z;
	c = (floor(c / vs) + 0.5) * vs;
	// into the wind's frame, stretched along it
	vec2 w = uCloud2.xy;
	vec2 q = vec2(dot(c, w) / uCloud2.w, dot(c, vec2(-w.y, w.x))) * uCloud.y;
	// billowing outlines: the noise warped by itself
	vec2 warp = vec2(noise(q * 0.8 + vec2(3.1, 7.7)), noise(q * 0.8 + vec2(11.3, 1.9))) - 0.5;
	q += warp * 1.1;
	float v = 0.0, a = 0.5;
	for (int i = 0; i < 4; ++i) { v += a * noise(q); q = q * 2.07 + vec2(1.7, 9.2); a *= 0.5; }
	v /= 0.9375;
	// clusters: some stretches of sky crowded, others clear
	float clump = noise(c * uCloud.y * 0.3 + vec2(5.0, 2.0));
	float thr = 0.50 + 0.14 * (0.5 - clump);
	gCov = uCloud.x * step(thr, v);
	return gCov;
}
float cloudAt() { return 1.0 - cloudCover(); }
float sunShadow(vec3 n)
{
	if (gShN < 0.0)
	{
		float f;
		if (uFigShadow == 1)
		{
			// a standee: ask from the matching point of its light-facing copy in the shadow map
			vec3 d = vWorld - uFigAxis;
			f = shadowAt(uFigAxis + uFigRL * dot(d, uFigRV) + uFigUp * dot(d, uFigUp), uFigNL);
		}
		else f = shadowFactor(n);
		gShN = clamp((f - 0.25) / 0.75, 0.0, 1.0);
	}
	return gShN;
}
/// How open to the sun a point is: 0 in the shade of a wall, a figure or a roof, about 0.1 under a
/// cloud, 1 in the sun. Clouds only count where the sun reaches, so they never show indoors.
float sunOpen(vec3 n) { return sunShadow(n) * clamp(1.0 - cloudCover() / 0.75, 0.0, 1.0); }

vec3 lighting(vec3 base, vec3 n, float spec, float rough)
{
	vec3 v = normalize(uEye - vWorld);
	float sunK = sunMask();
	// cool ambient from above, warm-ish bounce from below
	float hemi = n.y * 0.5 + 0.5;
	vec3 col = base * mix(vec3(0.020, 0.022, 0.028), vec3(0.060, 0.070, 0.085), hemi) * uAmbient;
	for (int i = 0; i < 12; ++i)
	{
		if (i >= uLightCount) break;
		vec3 L = uLightPos[i] - vWorld;
		float d2 = max(dot(L, L), 0.05);
		L *= inversesqrt(d2);
		float ndl = max(dot(n, L), 0.0);
		vec3 h = normalize(L + v);
		float s = pow(max(dot(n, h), 0.0), mix(80.0, 8.0, rough)) * spec;
		vec3 c = (base * ndl + s) * uLightCol[i] / d2;
		if (i == 0) c *= (uSunKey.w > 0.5) ? (1.0 - sunK) : shadowFactor(n);
		else if (gMap) c *= 1.0 - sunK;
		col += c;
	}
	if (sunK > 0.0)
	{
		// the sun or moon (parallel light), drifting cloud shadows, and the sky
		vec3 L = normalize(uSunKey.xyz);
		float ndl = max(dot(n, L), 0.0);
		vec3 hv = normalize(L + v);
		float s = pow(max(dot(n, hv), 0.0), mix(80.0, 8.0, rough)) * spec;
		// under a cloud the sun is mostly gone and the sky's light a little dimmer
		// shadows from the sun are deeper than the room light's; in any shade (a wall, a figure, a roof,
		// a cloud) the sky's light is dimmer too
		float sh = mix(0.08, 1.0, sunShadow(n));
		float open = sunOpen(n);
		col += sunK * ((base * ndl + s) * uSunKeyCol * sh * (1.0 - cloudCover()) * gBoost + base * uSunKeySky * (0.6 + 0.4 * n.y) * mix(0.7, 1.0, open));
	}
	// the holo table lights from below: undersides of hands and figures, fading with height
	float h = vWorld.y - uTableY;
	if (h > -0.01 && vWorld.x > uTableRect.x - 0.1 && vWorld.x < uTableRect.z + 0.1 && vWorld.z > uTableRect.y - 0.1 && vWorld.z < uTableRect.w + 0.1)
	{
		float under = clamp(-n.y * 0.8 + 0.2, 0.0, 1.0);
		col += base * uTableGlow * under * exp(-max(h, 0.0) / 0.25);
	}
	return col;
}

/// Soft darkening where the room's floor, walls and ceiling meet, and under the table.
float roomOcclusion(vec3 p)
{
	vec3 a = p - uRoomMin, b = uRoomMax - p;
	float dx = min(a.x, b.x), dy0 = a.y, dy1 = b.y, dz = min(a.z, b.z);
	const float r = 0.45;
	float e = exp(-dx / r) * exp(-dy0 / r) + exp(-dz / r) * exp(-dy0 / r) + exp(-dx / r) * exp(-dz / r)
		+ 0.6 * (exp(-dx / r) * exp(-dy1 / r) + exp(-dz / r) * exp(-dy1 / r));
	float occ = 1.0 - 0.6 * clamp(e, 0.0, 1.0);
	if (p.y < 0.02)
	{
		vec2 q = max(max(uPedestal.xy - p.xz, p.xz - uPedestal.zw), vec2(0.0));
		occ *= 1.0 - 0.55 * exp(-length(q) / 0.35);
	}
	return occ;
}

vec3 displayContent(vec2 uv)
{
	// Stylized tactical world map for the decorative wall screens.
	vec2 p = uv * vec2(2.0, 1.0);
	float land = smoothstep(0.52, 0.56, fbm(p * 3.0 + vec2(1.7, 4.2)) + 0.08 * sin(uv.y * 3.14159));
	vec3 col = mix(vec3(0.004, 0.018, 0.035), vec3(0.02, 0.10, 0.09), land);
	col += vec3(0.0, 0.10, 0.12) * (gridLine(uv.x, 1.0 / 24.0, 0.002) + gridLine(uv.y, 1.0 / 12.0, 0.003)) * 0.6;
	// sweep line
	float sweep = fract(uTime * 0.05);
	col += vec3(0.05, 0.35, 0.30) * exp(-abs(uv.x - sweep) * 120.0);
	// blips
	for (int i = 0; i < 6; ++i)
	{
		vec2 c = vec2(hash(vec2(i, 3.0)), 0.15 + 0.7 * hash(vec2(i, 7.0)));
		float blink = step(0.5, fract(uTime * 0.7 + float(i) * 0.37));
		float d = length((uv - c) * vec2(2.0, 1.0));
		vec3 bc = (i % 3 == 0) ? vec3(1.0, 0.15, 0.05) : vec3(0.1, 1.0, 0.4);
		col += bc * blink * (1.0 - smoothstep(0.004, 0.009, d)) * 2.0;
	}
	col *= 0.85 + 0.15 * sin(uv.y * 600.0);
	return col;
}

float feedHash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

/// The motion scanner's camera: brightness in eight steps of the radar's green, shown only where the ping
/// has been. The wave spreads from the soldier to the edge of the scan (the game's 19 x 19 tiles); each pixel
/// of the little picture comes up a moment after it passes (scattered points that join up), stays, and
/// drops out again at random just before the next wave gets there; beyond the range it stays black.
/// Contacts are rings on whatever is at their tile, on any floor (the game doesn't say which).
vec3 feedColor(vec3 c)
{
	vec3 rel = vWorld - uFeedO;
	vec2 t = vec2(dot(rel, uFeedE), dot(rel, uFeedS));
	float d = length(t);
	bool inRange = max(abs(t.x), abs(t.y)) < 9.5;
	float now = uFeedClock.x, speed = uFeedClock.z, cyc = uFeedClock.w;
	float vis = 0.0;
	if (inRange)
	{
		float t0 = d / speed + feedHash(gl_FragCoord.xy) * uFeedFill.x;
		float life = uFeedFill.y + feedHash(gl_FragCoord.yx + 17.31) * uFeedFill.z;
		float a = now - t0;
		if (a >= 0.0 && a < life) vis = 1.0;
		else if (a < 0.0 && uFeedClock.y > 0.5 && a + cyc < life) vis = 1.0; // the last ping's, not gone yet
	}
	float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
	float v = pow(clamp(l * 2.2, 0.0, 1.0), 0.55);
	v = floor(v * 7.0 + 0.5) / 7.0;
	vec3 g = mix(vec3(0.002, 0.010, 0.003), vec3(0.20, 0.62, 0.14), v);
	for (int i = 0; i < 24; ++i)
	{
		if (i >= uFeedN) break;
		vec4 k = uFeedC[i];
		float r = length(t - k.xy);
		float rw = max(fwidth(r), 1e-4);
		float ring = 1.0 - smoothstep(0.0, max(0.07, rw * 1.2), abs(r - k.z));
		float fill = (1.0 - smoothstep(k.z * 0.5, k.z, r)) * 0.3;
		g = mix(g, vec3(1.0, 0.62, 0.10) * min(k.w, 1.6), clamp(ring + fill, 0.0, 1.0));
	}
	g *= vis;
	// the wavefront itself: it fizzles where it meets the edge of the range, the sides first, the corners last
	float R = now * speed;
	if (inRange && R < 14.0)
	{
		float fw = max(fwidth(d), 1e-4);
		float fizz = 1.0 - 0.75 * smoothstep(8.5, 13.6, R);
		float band = 1.0 - smoothstep(0.0, max(0.35, fw * 1.5), abs(d - R));
		g += vec3(0.30, 0.95, 0.40) * band * fizz;
	}
	return g;
}

float gBuildEdge = 0.0;

void sceneMain()
{
	if (uBuild == 1)
	{
		float b = dot(uBuildPlane.xyz, vWorld) + uBuildPlane.w;
		if (b > 0.0) discard;
		gBuildEdge = 1.0 - smoothstep(0.0, 0.006, -b);
	}
	if (uClip.x <= uClip.z && (vWorld.x < uClip.x || vWorld.x > uClip.z || vWorld.z < uClip.y || vWorld.z > uClip.w)) discard;
	if (uHoleCount > 0 && vWorld.y > uHoleY.x && vWorld.y < uHoleY.y)
	{
		for (int i = 0; i < uHoleCount; ++i)
		{
			vec4 h = uHoles[i];
			if (vWorld.x > h.x && vWorld.x < h.z && vWorld.z > h.y && vWorld.z < h.w) discard;
		}
	}
	if (uShadowPass == 1)
	{
		if ((uMode == 3 || uMode == 4) && texture(uTex, vUV).a < 0.5) discard;
		fragColor = vec4(0.0);
		return;
	}
	vec3 n = normalize(vNormal);
	// two-sided surfaces: turn the normal toward the viewer (independent of triangle winding)
	if (dot(n, uEye - vWorld) < 0.0) n = -n;

	if (uMode == 1)
	{
		// Game screen: sharp-bilinear so the pixel art stays crisp but doesn't shimmer.
		vec2 texel = vUV * uTexSize;
		vec2 scale = max(fwidth(texel), vec2(1e-4));
		vec2 f = fract(texel);
		vec2 region = clamp((f - 0.5) / scale + 0.5, 0.0, 1.0) ;
		vec2 uv = (floor(texel) + region) / uTexSize;
		// mip level from the true footprint (the sharpened uv jumps at texel edges): a mipmapped,
		// prescaled picture filters cleanly when it is small or seen at an angle
		vec3 c = textureGrad(uTex, uv, dFdx(vUV), dFdy(vUV)).rgb;
		// faint scanlines, one per game pixel row, only visible up close
		if (uScan > 0.0) c *= 1.0 - uScanK * (0.5 - 0.5 * sin(texel.y / uScan * 6.2831853));
		if (uVignette > 0.0) c *= 1.0 - uVignette * smoothstep(0.3, 0.75, length(vUV - 0.5));
		if (uCrt.x < 0.999 || uCrt.y < 0.999)
		{
			// a tube turning on or off: only a band (then a line, a dot) of the picture, glowing
			vec2 q = abs(vUV - 0.5) * 2.0;
			if (q.y > max(uCrt.x, 0.012) || q.x > max(uCrt.y, 0.012)) { fragColor = vec4(0.004, 0.007, 0.005, 1.0); return; }
			c = mix(c, vec3(0.7, 1.0, 0.8), uCrt.z) * (1.0 + 2.0 * uCrt.z);
		}
		fragColor = vec4(c * uTint.rgb * 1.15, 1.0);
		return;
	}
	if (uMode == 5)
	{
		// geoscape globe: lit by the game's sun, with a soft terminator and a faint rim glow
		float d = dot(n, normalize(uSun));
		float day = smoothstep(-0.12, 0.25, d);
		vec3 c = vColor.rgb * mix(0.10, 1.05, day);
		float rim = pow(1.0 - max(dot(n, normalize(uEye - vWorld)), 0.0), 3.0);
		c += vec3(0.10, 0.35, 0.60) * rim * 0.6;
		fragColor = vec4(c * uTint.rgb, 1.0);
		return;
	}
	if (uMode == 2)
	{
		fragColor = vec4(vColor.rgb * uTint.rgb, vColor.a * uTint.a);
		return;
	}
	if (uMode == 6)
	{
		// low mist: soft wisps drifting over open ground (uTex: where the sky is open, per map tile)
		vec3 mp = (uMapInv * vec4(vWorld, 1.0)).xyz;
		float w = fbm(mp.xz * 0.028 + vec2(uTime * 0.05, uTime * 0.02) + vec2(mp.y * 0.11));
		float a = smoothstep(0.38, 0.8, w) * texture(uTex, vUV).r * uTint.a;
		for (int i = 0; i < 3; ++i)
		{
			if (i >= uFadeCount) break;
			a *= smoothstep(uFade[i].w * 0.35, uFade[i].w, length(vWorld - uFade[i].xyz));
		}
		if (a < 0.004) discard;
		fragColor = vec4(uTint.rgb, a);
		return;
	}
	if (uMode == 3 || uMode == 4)
	{
		vec4 t = texture(uTex, vUV);
		if (uMode == 3 && t.a < 0.5) discard;
		if (t.a < 0.02) discard;
		vec3 base = t.rgb * vColor.rgb * uTint.rgb;
		if (uMode == 4) { fragColor = vec4(base, t.a * uTint.a); return; }
		// the highlight follows the surface's own brightness, so a tile darkened by the night stays dark
		float k = dot(vColor.rgb * uTint.rgb, vec3(1.0 / 3.0));
		// glowing shots and blasts light the surface from its unlit colour, so they show at night too
		vec3 glow = vec3(0.0);
		for (int i = 0; i < 8; ++i)
		{
			if (i >= uGlowCount) break;
			vec3 L = uGlowPos[i].xyz - vWorld;
			float d = length(L), r = uGlowPos[i].w;
			if (d >= r) continue;
			float f = 1.0 - d / r;
			glow += uGlowCol[i] * f * f * (0.35 + 0.65 * max(dot(n, L / max(d, 1e-4)), 0.0));
		}
		// lightning: a flash shows the map in its own colours for a moment (only what is on the table already)
		gMap = true;
		float sk = sunMask();
		if (uFlash > 0.0) glow += vec3(0.75, 0.82, 1.0) * uFlash * 0.55 * (0.55 + 0.45 * max(n.y, 0.0)) * sk;
		if (sk > 0.5 && uMapShade > 0.0)
		{
			// under the sun or moon a tile's brightness has two parts: the darkness around it, lit by the
			// sun or moon and the sky (with shadows), and the game's own light where the tile is brighter
			// than that (soldiers' light, flares, fires), lit by a lamp of its own
			float b = max(dot(vColor.rgb * uTint.rgb, vec3(1.0 / 3.0)), 1e-4);
			vec3 albedo = base / b;
			// a moonlit night shows the unlit ground faintly; a cloud passing over the moon swallows it
			// (the moonlit floor is shaded by roofs, walls and clouds like the moonlight itself)
			float amb = max(min(b, uMapShade), uMoonFloor > 0.0 ? uMoonFloor * sunShadow(n) * cloudAt() : 0.0), loc = max(b - uMapShade, 0.0);
			// the game's own light on this tile: fires, flares and lit items (warm, a lamp of their own), the
			// rest is the soldiers' light. In the open that is the sky's light itself, as strong as the game
			// makes the tile (so it has the sun's or moon's shadows); under a roof it stays a soft light
			float va = vColor.a;
			bool roofed = va > 1.5 || uPoolRoofed == 1;
			float warm = clamp((va > 1.5 ? va - 2.0 : va) * uPoolWarm, 0.0, 1.0);
			float locSky = loc * (1.0 - warm), locWarm = loc * warm;
			vec3 skyLit = vec3(0.0);
			if (!roofed)
			{
				// the sun or moon itself, with its shadows: flat ground in full light, faces turned to it a
				// little more; figures (flat cards for round bodies) catch it from most sides
				vec3 L = normalize(uSunKey.xyz);
				float d = dot(n, L);
				float dirK = uTerrain == 1 ? min(max(d, 0.0) / uPoolSunY, 1.6) : 0.55 + 0.45 * clamp(d * 2.0, 0.0, 1.0);
				float shK = mix(0.08, 1.0, sunShadow(n)) * cloudAt();
				skyLit = albedo * locSky * (uPoolSkyCol * dirK * shK * 0.85 + uPoolFillCol * mix(0.45, 1.0, sunOpen(n)));
				locSky = 0.0;
			}
			vec3 bAmb = albedo * amb;
			// the sunlit part glows brighter than the game's darkness (golden hour, sunset); the shade keeps it
			gBoost = uSunBoost;
			// terrain is matte (grass, dirt, stone don't mirror the sun) except wet in the rain; figures keep their sheen
			vec3 lit = uTerrain == 1 ? lighting(bAmb, n, 0.12 * k * uWet, 0.35) : lighting(bAmb, n, 0.1 * k, 0.8);
			// in any shade the base light is dimmer and cooler, so shadows read deep on busy ground
			vec3 c = lit + bAmb * 0.22 * mix(vec3(0.42, 0.46, 0.56), vec3(1.0), sunOpen(n)) + albedo * (locSky * uPoolSkyCol + locWarm * uLampCol) * (0.8 + 0.2 * max(n.y, 0.0)) + skyLit;
			fragColor = vec4(c + t.rgb * glow, 1.0);
			return;
		}
		fragColor = vec4(lighting(base, n, 0.1 * k, 0.8) + base * 0.22 + t.rgb * glow, 1.0);
		return;
	}

	vec3 base = vColor.rgb;
	vec3 emissive = vec3(0.0);
	float spec = 0.25, rough = 0.6;
	if (vMat == 1) // floor tiles
	{
		vec2 p = vWorld.xz;
		float seam = max(gridLine(p.x, 1.0, 0.012), gridLine(p.y, 1.0, 0.012));
		float var = hash(floor(p)) * 0.25 + fbm(p * 6.0) * 0.15;
		base *= (0.85 + var) * (1.0 - 0.6 * seam);
		spec = 0.35; rough = 0.35;
	}
	else if (vMat == 2) // wall panels
	{
		float u = dot(vWorld.xz, vec2(abs(n.z), abs(n.x)));
		float seamV = gridLine(u, 1.2, 0.01);
		float seamH = max(gridLine(vWorld.y - 0.9, 100.0, 0.012), gridLine(vWorld.y - 2.6, 100.0, 0.012));
		float var = hash(vec2(floor(u / 1.2), floor(vWorld.y / 1.7))) * 0.18;
		base *= (0.9 + var) * (1.0 - 0.55 * max(seamV, seamH));
		spec = 0.15; rough = 0.7;
	}
	else if (vMat == 3) // holo table glass
	{
		vec2 p = vWorld.xz;
		float g = max(gridLine(p.x, 0.1, 0.0015), gridLine(p.y, 0.1, 0.0015));
		float g2 = max(gridLine(p.x, 0.5, 0.003), gridLine(p.y, 0.5, 0.003));
		float pulse = 0.75 + 0.25 * sin(uTime * 1.3 - length(p) * 4.0);
		emissive = vec3(0.02, 0.22, 0.26) * (g * 0.5 + g2 * 1.1) * pulse;
		base = vec3(0.010, 0.016, 0.022);
		spec = 1.0; rough = 0.1;
		gNoSun = true;
	}
	else if (vMat == 4) // light strips (red alert while aliens are in sight)
	{
		vec3 alert = vec3(1.0, 0.012, 0.006) * (0.45 + 0.55 * max(0.0, sin(uTime * 3.0))) * 0.6;
		vec3 strip = mix(base, vec3(0.03, 0.07, 0.30) * max(max(base.r, base.g), base.b), uNight);
		fragColor = vec4(mix(strip, alert, smoothstep(0.0, 0.6, uAlert)) * 4.0 * uTint.rgb, 1.0);
		return;
	}
	else if (vMat == 5) // ceiling
	{
		vec2 p = vWorld.xz;
		float seam = max(gridLine(p.x, 0.6, 0.01), gridLine(p.y, 0.6, 0.01));
		base *= 1.0 - 0.5 * seam;
		spec = 0.05;
	}
	else if (vMat == 6) // decorative wall display
	{
		fragColor = vec4(displayContent(vUV) * 1.4, 1.0);
		return;
	}
	else if (vMat == 7) // glossy bezel / controller plastic
	{
		spec = 0.6; rough = 0.25;
	}
	else if (vMat == 9) // fog of war: voxel cloud, slowly churning
	{
		vec3 q = floor(vWorld / 0.006);              // ~2 board voxels per cell at the default zoom
		float n1 = noise(q.xz * 0.21 + vec2(uTime * 0.18, -uTime * 0.11) + q.y * 0.37);
		float n2 = noise(q.xz * 0.53 - vec2(uTime * 0.07, uTime * 0.13) + q.y * 0.91);
		float k = 0.72 + 0.38 * n1 + 0.18 * n2;
		vec3 fogCol = base * k;
		float top = smoothstep(0.6, 0.95, n.y);
		vec3 c = lighting(fogCol, n, 0.05, 0.9) * 0.6 + fogCol * (0.22 + 0.10 * top) + vec3(0.010, 0.022, 0.035) * n1;
		c *= mix(1.0, 0.08, uNight); // unexplored ground is near black at night, as on the flat map
		fragColor = vec4(c * uTint.rgb, 0.94 * uTint.a);
		return;
	}

	if (vMat == 1 || vMat == 2 || vMat == 5) base *= roomOcclusion(vWorld);
	vec3 col = lighting(base, n, spec, rough) + emissive;
	fragColor = vec4(col * uTint.rgb, vColor.a * uTint.a);
}

void main()
{
	sceneMain();
	if (uFeed == 1) fragColor.rgb = feedColor(fragColor.rgb);
	if (uBuild == 1) fragColor.rgb += uBuildGlow * gBuildEdge;
}
)GLSL";

}
}
