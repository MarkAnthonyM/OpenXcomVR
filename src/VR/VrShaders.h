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
uniform int uLightCount;
uniform vec3 uLightPos[12];  // light 0 is the overhead light over the table and casts the shadows
uniform vec3 uLightCol[12];
uniform float uAmbient;      // room ambient scale (night missions are darker)
uniform float uAlert;        // 0..1: aliens in sight / alien turn, light strips go red
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

float shadowFactor(vec3 n)
{
	if (uShadowOn == 0) return 1.0;
	vec4 p = uShadowVP * vec4(vWorld + n * 0.006, 1.0);
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

vec3 lighting(vec3 base, vec3 n, float spec, float rough)
{
	vec3 v = normalize(uEye - vWorld);
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
		if (i == 0) c *= shadowFactor(n);
		col += c;
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

void main()
{
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
		vec3 c = texture(uTex, uv).rgb;
		// faint scanlines, only visible up close
		c *= 0.94 + 0.06 * sin(texel.y * 6.2831853);
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
	if (uMode == 3 || uMode == 4)
	{
		vec4 t = texture(uTex, vUV);
		if (uMode == 3 && t.a < 0.5) discard;
		if (t.a < 0.02) discard;
		vec3 base = t.rgb * vColor.rgb * uTint.rgb;
		if (uMode == 4) { fragColor = vec4(base, t.a * uTint.a); return; }
		fragColor = vec4(lighting(base, n, 0.1, 0.8) + base * 0.22, 1.0);
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
	}
	else if (vMat == 4) // light strips (red alert while aliens are in sight)
	{
		vec3 alert = vec3(1.0, 0.012, 0.006) * (0.45 + 0.55 * max(0.0, sin(uTime * 3.0))) * 0.6;
		fragColor = vec4(mix(base, alert, smoothstep(0.0, 0.6, uAlert)) * 4.0 * uTint.rgb, 1.0);
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
		fragColor = vec4(c * uTint.rgb, 0.94 * uTint.a);
		return;
	}

	if (vMat == 1 || vMat == 2 || vMat == 5) base *= roomOcclusion(vWorld);
	vec3 col = lighting(base, n, spec, rough) + emissive;
	fragColor = vec4(col * uTint.rgb, vColor.a * uTint.a);
}
)GLSL";

}
}
