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
	vUV = aUV;
	vColor = vec4(pow(aColor.rgb, vec3(2.2)), aColor.a);
	vMat = int(aMat + 0.5);
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
uniform vec3 uLightPos[4];
uniform vec3 uLightCol[4];
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

vec3 lighting(vec3 base, vec3 n, float spec, float rough)
{
	vec3 v = normalize(uEye - vWorld);
	// cool ambient from above, warm-ish bounce from below
	float hemi = n.y * 0.5 + 0.5;
	vec3 col = base * mix(vec3(0.020, 0.022, 0.028), vec3(0.060, 0.070, 0.085), hemi);
	for (int i = 0; i < 4; ++i)
	{
		vec3 L = uLightPos[i] - vWorld;
		float d2 = max(dot(L, L), 0.05);
		L *= inversesqrt(d2);
		float ndl = max(dot(n, L), 0.0);
		vec3 h = normalize(L + v);
		float s = pow(max(dot(n, h), 0.0), mix(80.0, 8.0, rough)) * spec;
		col += (base * ndl + s) * uLightCol[i] / d2;
	}
	return col;
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
	vec3 n = normalize(vNormal);
	if (!gl_FrontFacing) n = -n;

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
		fragColor = vec4(lighting(base, n, 0.1, 0.8) + base * 0.35, 1.0);
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
	else if (vMat == 4) // light strips
	{
		fragColor = vec4(base * 4.0 * uTint.rgb, 1.0);
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

	vec3 col = lighting(base, n, spec, rough) + emissive;
	fragColor = vec4(col * uTint.rgb, vColor.a * uTint.a);
}
)GLSL";

}
}
