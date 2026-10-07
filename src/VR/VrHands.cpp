#include "VrHands.h"
#include "VrRoom.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <algorithm>

namespace OpenXcom
{
namespace VR
{

// joint indices (XR_EXT_hand_tracking order)
enum
{
	J_PALM = 0, J_WRIST = 1,
	J_THUMB_MC = 2, J_THUMB_PROX = 3, J_THUMB_DIST = 4, J_THUMB_TIP = 5,
	J_INDEX_MC = 6, // + 5 per finger: metacarpal, proximal, intermediate, distal, tip
};

int fingerTip(int finger) { return finger == F_THUMB ? J_THUMB_TIP : J_INDEX_MC + 5 * (finger - 1) + 4; }
glm::vec3 HandPose::tip(int finger) const { return pos[fingerTip(finger)]; }
glm::vec3 HandPose::rawTip(int finger) const { return raw[fingerTip(finger)]; }

bool Collider::insideHole(const glm::vec3 &p, float r) const
{
	for (const glm::vec4 &h : holes)
		if (p.x > h.x + r * 0.5f && p.x < h.z - r * 0.5f && p.z > h.y + r * 0.5f && p.z < h.w - r * 0.5f) return true;
	return false;
}

// ------------------------------------------------------------------ collision helpers

/// Push needed to move sphere (c, r) out of box. face: 0..5 (-x,+x,-y,+y,-z,+z) used/preferred.
static bool sphereBox(const glm::vec3 &c, float r, const Collider &b, int &face, glm::vec3 &push, float &dist)
{
	glm::vec3 q = glm::clamp(c, b.mn, b.mx);
	glm::vec3 d = c - q;
	float d2 = glm::dot(d, d);
	if (d2 > 0.f)
	{
		dist = std::sqrt(d2) - r;
		// remember which side we are on (dominant axis of the offset)
		glm::vec3 a = glm::abs(d);
		int axis = (a.x >= a.y && a.x >= a.z) ? 0 : (a.y >= a.z ? 1 : 2);
		face = axis * 2 + (d[axis] > 0.f ? 1 : 0);
		if (dist >= 0.f) return false;
		push = d / std::sqrt(d2) * (-dist);
		return true;
	}
	// centre inside: leave through the remembered face, else the nearest one
	float exits[6] = {c.x - b.mn.x, b.mx.x - c.x, c.y - b.mn.y, b.mx.y - c.y, c.z - b.mn.z, b.mx.z - c.z};
	if (face < 0 || face > 5)
	{
		face = 0;
		for (int i = 1; i < 6; ++i) if (exits[i] < exits[face]) face = i;
	}
	float amount = exits[face] + r;
	push = glm::vec3(0.f);
	push[face / 2] = (face % 2) ? amount : -amount;
	dist = -amount;
	return true;
}

// ------------------------------------------------------------------ setup

void Hands::init()
{
	MeshData s;
	s.addSphere({0, 0, 0}, 1.f, 14, 10, glm::vec4(1), MAT_GLOSSY);
	_sphere.upload(s);
	MeshData c;
	c.addCylinder({0, 0, 0}, 1.f, 1.f, 14, glm::vec4(1), MAT_GLOSSY, false);
	_cyl.upload(c);
	for (auto &h : _side) for (int &f : h) f = -1;
}

// ------------------------------------------------------------------ procedural hand (no finger tracking)

static glm::vec3 bendToward(const glm::vec3 &v, const glm::vec3 &toward, float angle)
{
	glm::vec3 axis = glm::cross(v, toward);
	float l = glm::length(axis);
	if (l < 1e-5f) return v;
	return glm::angleAxis(angle, axis / l) * v;
}

void Hands::buildProcedural(int h, const glm::mat4 &G, const float curl[5])
{
	HandPose &P = pose[h];
	// grip space: +X user's right, -Z little->thumb, -Y where the fingers point
	const float side = h == 1 ? -1.f : 1.f;   // palm normal is -X for the right hand
	const glm::vec3 palmN(side, 0.f, 0.f), back = -palmN, fwd(0.f, -1.f, 0.f);
	glm::vec3 L[HAND_JOINTS];
	float R[HAND_JOINTS];
	L[J_PALM] = back * 0.014f + glm::vec3(0, -0.012f, 0);
	L[J_WRIST] = back * 0.012f + glm::vec3(0, 0.055f, 0.004f);
	R[J_PALM] = 0.022f; R[J_WRIST] = 0.021f;

	// fingers: metacarpal base, knuckle, then 3 bones that curl toward the palm
	const float mcZ[4] = {-0.012f, -0.002f, 0.008f, 0.017f};
	const float knZ[4] = {-0.026f, -0.007f, 0.012f, 0.029f};
	const float knY[4] = {-0.046f, -0.049f, -0.046f, -0.039f};
	const float len[4][3] = {{0.040f, 0.024f, 0.020f}, {0.044f, 0.028f, 0.021f}, {0.041f, 0.026f, 0.020f}, {0.032f, 0.019f, 0.018f}};
	const float spread[4] = {-0.07f, 0.f, 0.06f, 0.14f};
	const float rad[4] = {0.0095f, 0.0098f, 0.0092f, 0.0082f};
	for (int f = 0; f < 4; ++f)
	{
		int m = J_INDEX_MC + 5 * f;
		float c = curl[f + 1];
		L[m] = back * 0.012f + glm::vec3(0, 0.034f, mcZ[f]);
		L[m + 1] = back * 0.008f + glm::vec3(0, knY[f], knZ[f]);
		glm::vec3 dir = glm::normalize(fwd + glm::vec3(0, 0, spread[f] * (1.f - c * 0.6f)));
		const float flex[3] = {glm::radians(78.f), glm::radians(98.f), glm::radians(68.f)};
		glm::vec3 p = L[m + 1];
		float acc = 0.f;
		for (int b = 0; b < 3; ++b)
		{
			acc += flex[b] * c;
			glm::vec3 d = std::cos(acc) * dir + std::sin(acc) * palmN;
			p += d * len[f][b];
			L[m + 2 + b] = p;
		}
		R[m] = rad[f] * 1.2f; R[m + 1] = rad[f] * 1.08f; R[m + 2] = rad[f]; R[m + 3] = rad[f] * 0.92f; R[m + 4] = rad[f] * 0.85f;
	}

	// thumb: up along the controller when lifted, over the top (onto the buttons) when resting
	float tc = curl[F_THUMB];
	L[J_THUMB_MC] = palmN * 0.006f + glm::vec3(0, 0.026f, -0.026f);
	glm::vec3 open = glm::normalize(glm::vec3(0, -0.45f, -1.f) + palmN * 0.25f);
	glm::vec3 rest = glm::normalize(glm::vec3(0, -0.95f, -0.22f) + palmN * 0.5f);
	glm::vec3 td = glm::normalize(glm::mix(open, rest, tc));
	const float tl[3] = {0.040f, 0.032f, 0.026f};
	glm::vec3 tp = L[J_THUMB_MC];
	for (int b = 0; b < 3; ++b)
	{
		if (b > 0) td = bendToward(td, palmN, glm::radians(18.f + 30.f * tc));
		tp += td * tl[b];
		L[J_THUMB_PROX + b] = tp;
	}
	R[J_THUMB_MC] = 0.012f; R[J_THUMB_PROX] = 0.0105f; R[J_THUMB_DIST] = 0.0095f; R[J_THUMB_TIP] = 0.0085f;

	for (int j = 0; j < HAND_JOINTS; ++j)
	{
		P.raw[j] = glm::vec3(G * glm::vec4(L[j], 1.f));
		P.radius[j] = R[j];
	}
	P.palmRot = glm::quat_cast(glm::mat3(G));
	for (int f = 0; f < 5; ++f) P.curl[f] = curl[f];
}

// ------------------------------------------------------------------ per frame

static float fingerCurl(const glm::vec3 *p, int finger)
{
	int a, b, c, d;
	if (finger == F_THUMB) { a = J_THUMB_MC; b = J_THUMB_PROX; c = J_THUMB_DIST; d = J_THUMB_TIP; }
	else { int m = J_INDEX_MC + 5 * (finger - 1); a = m; b = m + 1; c = m + 3; d = m + 4; }
	glm::vec3 d0 = glm::normalize(p[b] - p[a]), d1 = glm::normalize(p[d] - p[c]);
	float ang = std::acos(glm::clamp(glm::dot(d0, d1), -1.f, 1.f));
	return glm::clamp(ang / glm::radians(finger == F_THUMB ? 70.f : 200.f), 0.f, 1.f);
}

void Hands::update(const HandState in[2], const glm::mat4 &rig, float dt, const std::vector<Collider> &colliders)
{
	for (int h = 0; h < 2; ++h)
	{
		HandPose &P = pose[h];
		P.valid = in[h].active;
		if (!P.valid) { _lastValid[h] = false; continue; }
		P.skeletal = in[h].jointsValid;
		if (P.skeletal)
		{
			for (int j = 0; j < HAND_JOINTS; ++j)
			{
				P.raw[j] = glm::vec3(rig * glm::vec4(in[h].jointPos[j], 1.f));
				P.radius[j] = glm::clamp(in[h].jointRadius[j], 0.005f, 0.025f);
			}
			P.palmRot = glm::quat_cast(glm::mat3(rig)) * in[h].jointRot[J_PALM];
			for (int f = 0; f < 5; ++f) P.curl[f] = fingerCurl(P.raw, f);
		}
		else
		{
			const HandState &s = in[h];
			float target[5];
			target[F_INDEX] = (s.triggerTouch || s.trigger > 0.02f) ? 0.18f + 0.82f * s.trigger : 0.f;
			if (s.triggerTouch && s.aTouch) target[F_INDEX] = std::max(target[F_INDEX], 0.42f); // pinch posture
			float others = s.squeeze;
			target[F_MIDDLE] = others;
			target[F_RING] = others;
			target[F_LITTLE] = others;
			target[F_THUMB] = s.thumbTouch ? 1.f : (s.trigger > 0.5f ? 0.8f : 0.f);
			float a = 1.f - std::exp(-dt * 22.f);
			for (int f = 0; f < 5; ++f) _smoothCurl[h][f] += (target[f] - _smoothCurl[h][f]) * a;
			buildProcedural(h, rig * s.grip.matrix(), _smoothCurl[h]);
		}
		_pinchIntent[h] = in[h].triggerTouch && in[h].aTouch;
		shapePinch(h, dt);
		solve(h, dt, colliders);
	}
}

void Hands::simulate(int h, const Pose &gripWorld, float indexCurl, float othersCurl, bool thumbDown, float dt, const std::vector<Collider> &colliders)
{
	HandPose &P = pose[h];
	P.valid = true;
	P.skeletal = false;
	_pinchIntent[h] = indexCurl > 0.5f && thumbDown;
	float target[5] = {thumbDown ? 1.f : 0.f, _pinchIntent[h] ? 0.42f : indexCurl, othersCurl, othersCurl, othersCurl};
	float a = dt <= 0.f ? 1.f : 1.f - std::exp(-dt * 22.f);
	for (int f = 0; f < 5; ++f) _smoothCurl[h][f] += (target[f] - _smoothCurl[h][f]) * a;
	buildProcedural(h, gripWorld.matrix(), _smoothCurl[h]);
	shapePinch(h, dt);
	solve(h, dt, colliders);
}

/// FABRIK: bends a joint chain (root fixed, bone lengths kept) so its end reaches a target.
static void fabrik(glm::vec3 *p, int n, const glm::vec3 &target)
{
	float len[4];
	for (int i = 0; i < n; ++i) len[i] = glm::length(p[i + 1] - p[i]);
	glm::vec3 root = p[0];
	for (int it = 0; it < 6; ++it)
	{
		p[n] = target;
		for (int i = n - 1; i >= 0; --i)
		{
			glm::vec3 d = p[i] - p[i + 1];
			float l = glm::length(d);
			p[i] = p[i + 1] + (l > 1e-6f ? d / l : glm::vec3(0, 1, 0)) * len[i];
		}
		p[0] = root;
		for (int i = 0; i < n; ++i)
		{
			glm::vec3 d = p[i + 1] - p[i];
			float l = glm::length(d);
			p[i + 1] = p[i] + (l > 1e-6f ? d / l : glm::vec3(0, -1, 0)) * len[i];
		}
	}
}

/// The pinch: thumb and index bend toward each other until their pads meet, eased over a few
/// frames so the fingers close (and open) smoothly instead of snapping.
void Hands::shapePinch(int h, float dt)
{
	HandPose &P = pose[h];
	float target = _pinchIntent[h] ? 1.f : 0.f;
	float a = dt <= 0.f ? 1.f : 1.f - std::exp(-dt * 16.f);
	_pinchBlend[h] += (target - _pinchBlend[h]) * a;
	int it = fingerTip(F_INDEX);
	glm::vec3 iTip = P.raw[it], tTip = P.raw[J_THUMB_TIP];
	// where the pads meet: nearer the index, the thumb travels further (as real hands do)
	glm::vec3 meet = glm::mix(iTip, tTip, 0.35f);
	glm::vec3 n = iTip - tTip;
	n = glm::length(n) > 1e-5f ? glm::normalize(n) : glm::vec3(0, 1, 0);
	_pinchMeet[h] = meet;
	float b = _pinchBlend[h];
	b = b * b * (3.f - 2.f * b);
	if (b < 1e-3f) return;
	glm::vec3 iGoal = glm::mix(iTip, meet + n * P.radius[it] * 0.8f, b);
	glm::vec3 tGoal = glm::mix(tTip, meet - n * P.radius[J_THUMB_TIP] * 0.8f, b);
	glm::vec3 ic[4] = {P.raw[J_INDEX_MC + 1], P.raw[J_INDEX_MC + 2], P.raw[J_INDEX_MC + 3], P.raw[J_INDEX_MC + 4]};
	fabrik(ic, 3, iGoal);
	for (int k = 0; k < 4; ++k) P.raw[J_INDEX_MC + 1 + k] = ic[k];
	glm::vec3 tc[4] = {P.raw[J_THUMB_MC], P.raw[J_THUMB_PROX], P.raw[J_THUMB_DIST], P.raw[J_THUMB_TIP]};
	fabrik(tc, 3, tGoal);
	for (int k = 0; k < 4; ++k) P.raw[J_THUMB_MC + k] = tc[k];
}

void Hands::solve(int h, float dt, const std::vector<Collider> &colliders)
{
	HandPose &P = pose[h];
	for (int j = 0; j < HAND_JOINTS; ++j) { P.contact[j] = -1; P.rawDepth[j] = 0.f; }

	// raw tip velocity (for taps)
	glm::vec3 tipNow = P.raw[fingerTip(F_INDEX)];
	if (_lastValid[h] && dt > 0.f) P.tipVelocity = (tipNow - _lastRawTip[h]) / dt;
	else P.tipVelocity = glm::vec3(0.f);
	_lastRawTip[h] = tipNow;
	_lastValid[h] = true;

	// 1. the hand body (palm, wrist, knuckles) cannot enter solids: move the whole hand out
	static const int body[] = {J_PALM, J_WRIST, J_THUMB_MC, J_THUMB_PROX, J_INDEX_MC, J_INDEX_MC + 1, J_INDEX_MC + 5, J_INDEX_MC + 6,
		J_INDEX_MC + 10, J_INDEX_MC + 11, J_INDEX_MC + 15, J_INDEX_MC + 16};
	glm::vec3 offset(0.f);
	for (int iter = 0; iter < 3; ++iter)
	{
		glm::vec3 worst(0.f);
		for (int j : body)
		{
			glm::vec3 c = P.raw[j] + offset;
			for (const Collider &col : colliders)
			{
				if (col.insideHole(c, P.radius[j])) continue;
				int face = _side[h][j];
				glm::vec3 push; float dist;
				if (sphereBox(c, P.radius[j], col, face, push, dist))
				{
					// keep the largest push per axis
					for (int a = 0; a < 3; ++a)
						if (std::fabs(push[a]) > std::fabs(worst[a])) worst[a] = push[a];
					P.contact[j] = col.id;
				}
			}
		}
		if (glm::dot(worst, worst) == 0.f) break;
		offset += worst;
	}
	// a hand shoved deep into the table would jump around: let it pass through as a ghost instead
	P.ghost = glm::length(offset) > 0.09f;
	if (P.ghost)
	{
		for (int j = 0; j < HAND_JOINTS; ++j) { P.pos[j] = P.raw[j]; P.contact[j] = -1; }
		for (int &f : _side[h]) f = -1;
		P.pinch = false;
		return;
	}
	for (int j = 0; j < HAND_JOINTS; ++j) P.pos[j] = P.raw[j] + offset;

	// 2. fingers, joint by joint from the knuckle out: a blocked joint slides along the surface
	//    while the bone keeps its length, which bends the finger
	auto solveJoint = [&](int j, int parent)
	{
		float len = glm::length(P.raw[j] - P.raw[parent]);
		glm::vec3 shifted = P.raw[j] + offset;
		glm::vec3 c = P.raw[j] + (P.pos[parent] - P.raw[parent]);
		float r = P.radius[j];
		bool touched = false;
		glm::vec3 normal(0.f);
		for (int iter = 0; iter < 3; ++iter)
		{
			bool any = false;
			for (const Collider &col : colliders)
			{
				if (col.insideHole(c, r)) continue;
				int face = _side[h][j];
				glm::vec3 push; float dist;
				bool pen = sphereBox(c, r, col, face, push, dist);
				if (!pen && dist < 0.0025f) { P.contact[j] = col.id; touched = true; }
				if (dist > r * 4.f) face = -1;
				_side[h][j] = face;
				if (!pen) continue;
				c += push;
				normal = glm::normalize(push);
				P.contact[j] = col.id;
				touched = any = true;
			}
			c = P.pos[parent] + glm::normalize(c - P.pos[parent]) * len;
			if (!any) break;
		}
		P.pos[j] = c;
		if (touched && glm::dot(normal, normal) > 0.f) P.rawDepth[j] = std::max(0.f, glm::dot(normal, c - shifted));
	};
	auto chain = [&](const int *joints, int n)
	{
		for (int k = 1; k < n; ++k) solveJoint(joints[k], joints[k - 1]);
	};
	const int thumb[] = {J_THUMB_PROX, J_THUMB_DIST, J_THUMB_TIP};
	chain(thumb, 3);
	for (int f = 0; f < 4; ++f)
	{
		int m = J_INDEX_MC + 5 * f;
		const int fj[] = {m + 1, m + 2, m + 3, m + 4};
		chain(fj, 4);
	}

	// 3. gestures: the pinch is the controller state (index on trigger + thumb on A), so it is
	//    instant and reliable; the pinch point is where the pads meet (also while they close)
	bool was = P.pinch;
	P.pinch = _pinchIntent[h];
	P.pinchStarted = P.pinch && !was;
	P.pinchEnded = !P.pinch && was;
	P.pinchPoint = _pinchMeet[h] + (P.pos[J_PALM] - P.raw[J_PALM]);
	P.indexExtended = P.curl[F_INDEX] < 0.35f;
}

// ------------------------------------------------------------------ drawing

void Hands::draw(const Shader &sh) const
{
	auto bone = [&](const glm::vec3 &a, const glm::vec3 &b, float r)
	{
		glm::vec3 d = b - a;
		float l = glm::length(d);
		if (l < 1e-4f) return;
		glm::quat q = glm::rotation(glm::vec3(0, 1, 0), d / l);
		sh.set("uModel", glm::translate(glm::mat4(1.f), a) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.f), {r, l, r}));
		_cyl.draw();
	};
	auto ball = [&](const glm::vec3 &c, float r)
	{
		sh.set("uModel", glm::translate(glm::mat4(1.f), c) * glm::scale(glm::mat4(1.f), glm::vec3(r)));
		_sphere.draw();
	};
	const glm::vec4 glove(0.20f, 0.22f, 0.26f, 1.f);
	for (int h = 0; h < 2; ++h)
	{
		const HandPose &P = pose[h];
		if (!P.valid) continue;
		if (P.ghost)
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
		}
		float alpha = P.ghost ? 0.3f : 1.f;
		sh.set("uMode", 0);
		sh.set("uTint", glm::vec4(glm::vec3(glove), alpha));
		const glm::vec3 *p = P.pos;
		// palm: thick spokes from the wrist to every knuckle and across the knuckles
		for (int f = 0; f < 4; ++f)
		{
			int m = J_INDEX_MC + 5 * f;
			bone(p[J_WRIST], p[m + 1], 0.0125f);
			if (f < 3) bone(p[m + 1], p[m + 6], 0.011f);
		}
		bone(p[J_WRIST], p[J_THUMB_MC], 0.014f);
		bone(p[J_THUMB_MC], p[J_INDEX_MC + 1], 0.010f);
		ball(p[J_WRIST], P.radius[J_WRIST]);
		ball(p[J_PALM], P.radius[J_PALM] * 0.75f);
		// thumb and fingers
		const int thumb[] = {J_THUMB_MC, J_THUMB_PROX, J_THUMB_DIST, J_THUMB_TIP};
		for (int k = 0; k < 3; ++k) bone(p[thumb[k]], p[thumb[k + 1]], P.radius[thumb[k + 1]] * 0.95f);
		for (int k = 0; k < 3; ++k) ball(p[thumb[k]], P.radius[thumb[k]]);
		for (int f = 0; f < 4; ++f)
		{
			int m = J_INDEX_MC + 5 * f;
			for (int k = 1; k < 4; ++k) bone(p[m + k], p[m + k + 1], P.radius[m + k + 1] * 0.95f);
			for (int k = 1; k < 4; ++k) ball(p[m + k], P.radius[m + k]);
		}
		// fingertips glow, brighter when touching something
		for (int f = 0; f < 5; ++f)
		{
			int t = fingerTip(f);
			bool touching = P.contact[t] >= 0;
			glm::vec3 c = touching ? glm::vec3(0.5f, 1.6f, 1.8f) : glm::vec3(0.12f, 0.45f, 0.55f);
			if (f == F_INDEX || f == F_THUMB) c *= P.pinch ? 1.8f : 1.f;
			sh.set("uTint", glm::vec4(c, alpha));
			ball(p[t], P.radius[t]);
		}
		if (P.ghost)
		{
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
		}
	}
	sh.set("uTint", glm::vec4(1.f));
}

}
}
