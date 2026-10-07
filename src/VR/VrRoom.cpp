#include "VrRoom.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace OpenXcom
{
namespace VR
{

static const glm::vec4 kWall(0.23f, 0.25f, 0.28f, 1.f);
static const glm::vec4 kFloor(0.16f, 0.17f, 0.19f, 1.f);
static const glm::vec4 kCeiling(0.12f, 0.13f, 0.15f, 1.f);
static const glm::vec4 kMetal(0.30f, 0.32f, 0.35f, 1.f);
static const glm::vec4 kDark(0.07f, 0.08f, 0.09f, 1.f);
static const glm::vec4 kCyan(0.10f, 0.75f, 0.85f, 1.f);
static const glm::vec4 kAmber(1.00f, 0.55f, 0.12f, 1.f);
static const glm::vec4 kWhiteLight(0.95f, 0.97f, 1.0f, 1.f);

static void wallDisplay(MeshData &m, const glm::vec3 &center, const glm::vec3 &right, float w, float h)
{
	glm::vec3 up(0, 1, 0);
	glm::vec3 n = glm::normalize(glm::cross(right, up));
	glm::vec3 c = center + n * 0.02f;
	glm::vec3 r = right * (w * 0.5f), u = up * (h * 0.5f);
	// frame
	glm::vec3 fr = right * (w * 0.5f + 0.06f), fu = up * (h * 0.5f + 0.06f);
	m.addQuad(center - fr - fu + n * 0.01f, center + fr - fu + n * 0.01f, center + fr + fu + n * 0.01f, center - fr + fu + n * 0.01f, kDark, MAT_GLOSSY);
	m.addQuad(c - r - u, c + r - u, c + r + u, c - r + u, glm::vec4(1), MAT_DISPLAY, {0, 1}, {1, 1}, {1, 0}, {0, 0});
}

static void console(MeshData &m, const glm::mat4 &xf)
{
	MeshData c;
	// body
	c.addBox({-0.55f, 0.f, -0.35f}, {0.55f, 0.85f, 0.35f}, kMetal, MAT_PLAIN);
	// kick plate light
	c.addBox({-0.5f, 0.05f, 0.351f}, {0.5f, 0.07f, 0.36f}, kCyan, MAT_LIGHT);
	// slanted display, facing +z and up
	glm::mat4 tilt = glm::translate(glm::mat4(1), {0.f, 0.95f, 0.05f}) * glm::rotate(glm::mat4(1), glm::radians(-35.f), {1, 0, 0});
	MeshData s;
	s.addBox({-0.5f, -0.25f, -0.03f}, {0.5f, 0.25f, 0.0f}, kDark, MAT_GLOSSY);
	s.addQuad({-0.46f, -0.21f, 0.002f}, {0.46f, -0.21f, 0.002f}, {0.46f, 0.21f, 0.002f}, {-0.46f, 0.21f, 0.002f}, glm::vec4(1), MAT_DISPLAY, {0, 1}, {1, 1}, {1, 0}, {0, 0});
	c.append(s, tilt);
	// support for the screen
	c.addBox({-0.06f, 0.85f, -0.1f}, {0.06f, 1.0f, 0.05f}, kDark, MAT_PLAIN);
	m.append(c, xf);
}

void buildCommandCenter(MeshData &m, RoomLayout &L)
{
	const float x0 = L.roomX0, x1 = L.roomX1, z0 = L.roomZ0, z1 = L.roomZ1, h = L.roomH;

	// ---- shell
	m.addQuad({x0, 0, z1}, {x1, 0, z1}, {x1, 0, z0}, {x0, 0, z0}, kFloor, MAT_FLOOR, {x0, z1}, {x1, z1}, {x1, z0}, {x0, z0});
	m.addQuad({x0, h, z0}, {x1, h, z0}, {x1, h, z1}, {x0, h, z1}, kCeiling, MAT_CEILING);
	m.addQuad({x0, 0, z0}, {x1, 0, z0}, {x1, h, z0}, {x0, h, z0}, kWall, MAT_WALL); // back (-z), facing +z
	m.addQuad({x1, 0, z1}, {x0, 0, z1}, {x0, h, z1}, {x1, h, z1}, kWall, MAT_WALL); // front (+z)
	m.addQuad({x0, 0, z1}, {x0, 0, z0}, {x0, h, z0}, {x0, h, z1}, kWall, MAT_WALL); // left (-x)
	m.addQuad({x1, 0, z0}, {x1, 0, z1}, {x1, h, z1}, {x1, h, z0}, kWall, MAT_WALL); // right (+x)

	// skirting + cornice light strips
	for (int side = 0; side < 4; ++side)
	{
		glm::vec3 a, b, n;
		switch (side)
		{
		case 0: a = {x0, 0, z0}; b = {x1, 0, z0}; n = {0, 0, 1}; break;
		case 1: a = {x1, 0, z1}; b = {x0, 0, z1}; n = {0, 0, -1}; break;
		case 2: a = {x0, 0, z1}; b = {x0, 0, z0}; n = {1, 0, 0}; break;
		default: a = {x1, 0, z0}; b = {x1, 0, z1}; n = {-1, 0, 0}; break;
		}
		glm::vec3 o = n * 0.01f;
		m.addQuad(a + o, b + o, b + o + glm::vec3(0, 0.12f, 0), a + o + glm::vec3(0, 0.12f, 0), kDark, MAT_PLAIN);
		glm::vec3 y1(0, h - 0.25f, 0), y2(0, h - 0.22f, 0);
		m.addQuad(a + o + y1, b + o + y1, b + o + y2, a + o + y2, kCyan * 0.6f, MAT_LIGHT);
	}

	// pillars in the corners and halfway down the side walls
	for (float px : {x0 + 0.25f, x1 - 0.25f})
		for (float pz : {z0 + 0.25f, -0.6f, z1 - 0.25f})
		{
			m.addBox({px - 0.25f, 0, pz - 0.25f}, {px + 0.25f, h, pz + 0.25f}, kMetal * 0.9f, MAT_PLAIN);
			float s = px < 0 ? 1.f : -1.f;
			m.addBox({px + s * 0.251f - 0.005f, 0.4f, pz - 0.02f}, {px + s * 0.251f + 0.005f, h - 0.4f, pz + 0.02f}, kAmber * 0.5f, MAT_LIGHT);
		}

	// ---- back wall: bezels for the game screen and the live screens (their pictures are drawn by VR code)
	auto bezel = [&](const glm::vec3 &c, glm::vec2 size)
	{
		glm::vec3 fr(size.x * 0.5f + 0.05f, 0, 0), fu(0, size.y * 0.5f + 0.05f, 0);
		glm::vec3 b = glm::vec3(c.x, c.y, z0 + 0.012f);
		m.addBox(b - fr - fu, b + fr + fu + glm::vec3(0, 0, 0.02f), kDark, MAT_GLOSSY);
	};
	bezel(L.statsPos, L.statsSize);
	bezel(L.rosterPos, L.rosterSize);
	bezel(L.minimapPos, L.minimapSize);
	// a strip light under the screens
	m.addBox({x0 + 0.6f, 0.42f, z0 + 0.01f}, {x1 - 0.6f, 0.45f, z0 + 0.03f}, kCyan * 0.7f, MAT_LIGHT);

	// ---- decorative displays on the side walls (none where a game screen is)
	wallDisplay(m, {x0, 1.9f, -1.6f}, {0, 0, -1}, 1.5f, 0.9f);
	wallDisplay(m, {x1, 1.9f, -1.6f}, {0, 0, 1}, 1.5f, 0.9f);

	// consoles along the side walls
	for (float cz : {-1.6f, 0.9f})
	{
		console(m, glm::translate(glm::mat4(1), {x0 + 0.75f, 0.f, cz}) * glm::rotate(glm::mat4(1), glm::radians(90.f), {0, 1, 0}));
		console(m, glm::translate(glm::mat4(1), {x1 - 0.75f, 0.f, cz}) * glm::rotate(glm::mat4(1), glm::radians(-90.f), {0, 1, 0}));
	}

	// ---- war table
	const glm::vec3 T = L.tableCenter;
	const float hx = L.tableSize.x * 0.5f, hz = L.tableSize.y * 0.5f;
	const float rim = L.tableRim;
	// pedestal
	m.addBox({T.x - hx * 0.6f, 0.f, T.z - hz * 0.5f}, {T.x + hx * 0.6f, 0.10f, T.z + hz * 0.5f}, kDark, MAT_PLAIN);
	m.addBox({T.x - hx * 0.45f, 0.10f, T.z - hz * 0.35f}, {T.x + hx * 0.45f, T.y - 0.12f, T.z + hz * 0.35f}, kMetal * 0.8f, MAT_PLAIN);
	m.addBox({T.x - hx * 0.46f, 0.35f, T.z + hz * 0.35f}, {T.x + hx * 0.46f, 0.38f, T.z + hz * 0.351f + 0.005f}, kCyan, MAT_LIGHT);
	// top slab + rim
	m.addBox({T.x - hx - rim, T.y - 0.12f, T.z - hz - rim}, {T.x + hx + rim, T.y - 0.04f, T.z + hz + rim}, kMetal, MAT_GLOSSY);
	m.addBox({T.x - hx - rim, T.y - 0.02f, T.z - hz - rim}, {T.x + hx + rim, T.y + 0.02f, T.z - hz}, kDark, MAT_GLOSSY);
	m.addBox({T.x - hx - rim, T.y - 0.02f, T.z + hz}, {T.x + hx + rim, T.y + 0.02f, T.z + hz + rim}, kDark, MAT_GLOSSY);
	m.addBox({T.x - hx - rim, T.y - 0.02f, T.z - hz}, {T.x - hx, T.y + 0.02f, T.z + hz}, kDark, MAT_GLOSSY);
	m.addBox({T.x + hx, T.y - 0.02f, T.z - hz}, {T.x + hx + rim, T.y + 0.02f, T.z + hz}, kDark, MAT_GLOSSY);
	// glow line on the rim's inner edge
	float gy = T.y + 0.0205f;
	m.addQuad({T.x - hx, gy, T.z + hz + 0.012f}, {T.x + hx, gy, T.z + hz + 0.012f}, {T.x + hx, gy, T.z + hz}, {T.x - hx, gy, T.z + hz}, kCyan, MAT_LIGHT);
	m.addQuad({T.x - hx, gy, T.z - hz}, {T.x + hx, gy, T.z - hz}, {T.x + hx, gy, T.z - hz - 0.012f}, {T.x - hx, gy, T.z - hz - 0.012f}, kCyan, MAT_LIGHT);
	// holo glass surface
	m.addQuad({T.x - hx, T.y - 0.025f, T.z + hz}, {T.x + hx, T.y - 0.025f, T.z + hz}, {T.x + hx, T.y - 0.025f, T.z - hz}, {T.x - hx, T.y - 0.025f, T.z - hz}, glm::vec4(1), MAT_HOLOGLASS);

	// floor ring light around the table
	float r0 = 1.45f;
	for (int i = 0; i < 48; ++i)
	{
		float a0 = i / 48.f * 6.2831853f, a1 = (i + 0.6f) / 48.f * 6.2831853f;
		glm::vec3 p0 = T + glm::vec3(std::cos(a0) * r0 * 1.2f, 0, std::sin(a0) * r0 * 0.9f);
		glm::vec3 p1 = T + glm::vec3(std::cos(a1) * r0 * 1.2f, 0, std::sin(a1) * r0 * 0.9f);
		p0.y = p1.y = 0.003f;
		glm::vec3 d = glm::normalize(p1 - p0), n = glm::vec3(-d.z, 0, d.x) * 0.02f;
		m.addQuad(p0 - n, p1 - n, p1 + n, p0 + n, kCyan * 0.35f, MAT_LIGHT);
	}

	// ceiling light ring above the table
	{
		float y = h - 0.02f;
		glm::vec3 c = T;
		float ax = hx + 0.4f, az = hz + 0.4f, w = 0.12f;
		m.addQuad({c.x - ax, y, c.z - az}, {c.x + ax, y, c.z - az}, {c.x + ax, y, c.z - az + w}, {c.x - ax, y, c.z - az + w}, kWhiteLight, MAT_LIGHT);
		m.addQuad({c.x - ax, y, c.z + az - w}, {c.x + ax, y, c.z + az - w}, {c.x + ax, y, c.z + az}, {c.x - ax, y, c.z + az}, kWhiteLight, MAT_LIGHT);
		m.addQuad({c.x - ax, y, c.z - az}, {c.x - ax + w, y, c.z - az}, {c.x - ax + w, y, c.z + az}, {c.x - ax, y, c.z + az}, kWhiteLight, MAT_LIGHT);
		m.addQuad({c.x + ax - w, y, c.z - az}, {c.x + ax, y, c.z - az}, {c.x + ax, y, c.z + az}, {c.x + ax - w, y, c.z + az}, kWhiteLight, MAT_LIGHT);
	}

	L.lightPos[0] = {T.x, h - 0.4f, T.z};
	L.lightCol[0] = {2.2f, 2.3f, 2.5f};
	L.lightPos[1] = {x0 + 0.8f, h - 0.3f, z0 + 0.8f};
	L.lightCol[1] = {0.6f, 0.9f, 1.1f};
	L.lightPos[2] = {x1 - 0.8f, h - 0.3f, z0 + 0.8f};
	L.lightCol[2] = {0.6f, 0.9f, 1.1f};
	L.lightPos[3] = {0.0f, h - 0.3f, z1 - 0.6f};
	L.lightCol[3] = {0.9f, 0.75f, 0.6f};
}

void buildController(MeshData &m, bool left)
{
	// grip space: -Z points forward out of the hand, +Y is up
	m.addBox({-0.018f, -0.03f, -0.02f}, {0.018f, 0.015f, 0.10f}, glm::vec4(0.15f, 0.16f, 0.18f, 1), MAT_GLOSSY);
	m.addBox({-0.022f, 0.0f, -0.08f}, {0.022f, 0.03f, -0.015f}, glm::vec4(0.22f, 0.23f, 0.26f, 1), MAT_GLOSSY);
	glm::vec4 accent = left ? kAmber : kCyan;
	m.addBox({-0.0225f, 0.012f, -0.075f}, {0.0225f, 0.018f, -0.02f}, accent, MAT_LIGHT);
}

}
}
