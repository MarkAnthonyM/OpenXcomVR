/*
 * OXCE VR tabletop - comparing the table's terrain shapes with the game's art (see VrCompare.h).
 */
#include "VrCompare.h"
#include <algorithm>
#include <cmath>

namespace OpenXcom
{
namespace VR
{
namespace Compare
{

static const int SS = 4; // samples per pixel side

View isoView(float dx, float dy)
{
	View v;
	v.ax = glm::vec3(1.f, 0.f, -1.f);
	v.ay = glm::vec3(0.5f, -1.f, 0.5f);
	v.az = glm::vec3(1.f, 1.f, 1.f);
	v.ox = 16.f + dx;
	v.oy = 24.f + dy;
	return v;
}

View lookView(float azimuthDeg, float elevationDeg, float s, float cx, float cy)
{
	float a = glm::radians(azimuthDeg), e = glm::radians(elevationDeg);
	glm::vec3 d(std::sin(a) * std::cos(e), std::sin(e), std::cos(a) * std::cos(e)); // towards the eye
	glm::vec3 f = -d;
	glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3(0.f, 1.f, 0.f)));
	glm::vec3 u = glm::cross(r, f);
	glm::vec3 c(8.f, 0.f, 8.f);
	View v;
	v.ax = r * s;
	v.ay = -u * s;
	v.az = d;
	v.ox = cx - glm::dot(v.ax, c);
	v.oy = cy - glm::dot(v.ay, c);
	return v;
}

void render(const std::vector<Vertex> &verts, const std::vector<uint32_t> &idx, const uint32_t *tex, int texW, int texH,
	const View &view, int w, int h, bool shade, Image &out)
{
	const int W = w * SS, H = h * SS;
	std::vector<float> depth((size_t)W * H, -1e30f);
	std::vector<uint32_t> col((size_t)W * H, 0u);
	glm::vec3 L = glm::normalize(glm::normalize(view.az) + glm::vec3(0.f, 1.2f, 0.f) + glm::vec3(0.3f, 0.f, -0.2f));
	for (size_t t = 0; t + 2 < idx.size(); t += 3)
	{
		const Vertex *v[3] = {&verts[idx[t]], &verts[idx[t + 1]], &verts[idx[t + 2]]};
		float X[3], Y[3], Z[3];
		for (int k = 0; k < 3; ++k)
		{
			X[k] = (view.ox + glm::dot(view.ax, v[k]->pos)) * SS;
			Y[k] = (view.oy + glm::dot(view.ay, v[k]->pos)) * SS;
			Z[k] = glm::dot(view.az, v[k]->pos);
		}
		float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
		if (std::fabs(area) < 1e-6f) continue; // seen edge-on
		float light = 1.f;
		if (shade)
		{
			glm::vec3 n = v[0]->normal;
			float nl = glm::dot(n, L);
			if (glm::dot(n, view.az) < 0.f) nl = -nl; // a face seen from behind (cards): lit as its front
			light = 0.42f + 0.58f * std::max(0.f, nl);
		}
		int x0 = std::max(0, (int)std::floor(std::min({X[0], X[1], X[2]})));
		int x1 = std::min(W - 1, (int)std::ceil(std::max({X[0], X[1], X[2]})));
		int y0 = std::max(0, (int)std::floor(std::min({Y[0], Y[1], Y[2]})));
		int y1 = std::min(H - 1, (int)std::ceil(std::max({Y[0], Y[1], Y[2]})));
		float inv = 1.f / area;
		for (int py = y0; py <= y1; ++py)
			for (int px = x0; px <= x1; ++px)
			{
				float sx = px + 0.5f, sy = py + 0.5f;
				float b0 = ((X[1] - sx) * (Y[2] - sy) - (X[2] - sx) * (Y[1] - sy)) * inv;
				float b1 = ((X[2] - sx) * (Y[0] - sy) - (X[0] - sx) * (Y[2] - sy)) * inv;
				float b2 = 1.f - b0 - b1;
				const float eps = -1e-5f;
				if (b0 < eps || b1 < eps || b2 < eps) continue;
				float z = b0 * Z[0] + b1 * Z[1] + b2 * Z[2];
				size_t o = (size_t)py * W + px;
				if (z <= depth[o]) continue;
				glm::vec2 uv = v[0]->uv * b0 + v[1]->uv * b1 + v[2]->uv * b2;
				int tx = glm::clamp((int)std::floor(uv.x * texW), 0, texW - 1);
				int ty = glm::clamp((int)std::floor(uv.y * texH), 0, texH - 1);
				uint32_t c = tex[(size_t)ty * texW + tx];
				if ((c >> 24) < 128) continue; // a card's transparent texel
				if (shade)
				{
					uint32_t r = (uint32_t)std::min(255.f, (c & 255) * light);
					uint32_t g = (uint32_t)std::min(255.f, ((c >> 8) & 255) * light);
					uint32_t b = (uint32_t)std::min(255.f, ((c >> 16) & 255) * light);
					c = r | (g << 8) | (b << 16) | 0xFF000000u;
				}
				depth[o] = z;
				col[o] = c | 0xFF000000u;
			}
	}
	out.w = w; out.h = h;
	out.px.assign((size_t)w * h, 0u);
	out.cover.assign((size_t)w * h, 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			int n = 0;
			uint32_t r = 0, g = 0, b = 0;
			for (int j = 0; j < SS; ++j)
				for (int i = 0; i < SS; ++i)
				{
					uint32_t c = col[(size_t)(y * SS + j) * W + x * SS + i];
					if (!c) continue;
					++n; r += c & 255; g += (c >> 8) & 255; b += (c >> 16) & 255;
				}
			if (!n) continue;
			out.cover[(size_t)y * w + x] = (uint8_t)(n * 255 / (SS * SS));
			out.px[(size_t)y * w + x] = (r / n) | ((g / n) << 8) | ((b / n) << 16) | 0xFF000000u;
		}
}

Score diff(const Image &table, const std::vector<uint32_t> &art, uint32_t bg, std::vector<uint32_t> &diffPx)
{
	const int w = table.w, h = table.h;
	Score s;
	diffPx.assign((size_t)w * h, bg);
	auto A = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && (art[(size_t)y * w + x] >> 24) >= 128; };
	auto T = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && table.covered(x, y); };
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			bool a = A(x, y), t = T(x, y);
			uint32_t &o = diffPx[(size_t)y * w + x];
			if (a) ++s.art;
			if (a && t)
			{
				++s.both;
				// colours averaged over 3 x 3 (each side's own pixels)
				float ca[3] = {0, 0, 0}, ct[3] = {0, 0, 0};
				int na = 0, nt = 0;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						if (A(x + dx, y + dy)) { uint32_t c = art[(size_t)(y + dy) * w + x + dx]; ca[0] += c & 255; ca[1] += (c >> 8) & 255; ca[2] += (c >> 16) & 255; ++na; }
						if (T(x + dx, y + dy)) { uint32_t c = table.px[(size_t)(y + dy) * w + x + dx]; ct[0] += c & 255; ct[1] += (c >> 8) & 255; ct[2] += (c >> 16) & 255; ++nt; }
					}
				float d2 = 0.f;
				for (int k = 0; k < 3; ++k) { float d = ca[k] / na - ct[k] / nt; d2 += d * d; }
				if (d2 > 48.f * 48.f) { ++s.colourOff; o = 0xFF20D0F0u; continue; } // yellow
				uint32_t c = art[(size_t)y * w + x];
				uint32_t l = (((c & 255) * 3 + ((c >> 8) & 255) * 6 + ((c >> 16) & 255)) / 10) * 7 / 10 + 20;
				o = l | (l << 8) | (l << 16) | 0xFF000000u;
			}
			else if (a || t)
			{
				// a slanted edge one pixel off: the other kind is right beside it
				bool edge = a ? (T(x - 1, y) || T(x + 1, y) || T(x, y - 1) || T(x, y + 1))
					: (A(x - 1, y) || A(x + 1, y) || A(x, y - 1) || A(x, y + 1));
				if (t) { if (!edge) ++s.tableOnly; o = edge ? 0xFF282070u : 0xFF3030F0u; } // red (ABGR)
				else { if (!edge) ++s.artOnly; o = edge ? 0xFF703420u : 0xFFF09040u; }     // blue
			}
		}
	return s;
}

}
}
}
