#pragma once
/*
 * OXCE VR tabletop - virtual hands.
 *
 * Joint data comes from XR_EXT_hand_tracking when the runtime offers it
 * (SteamVR does for Index controllers), otherwise a hand is posed from the
 * controller's finger inputs (trigger = index, grip = other fingers,
 * thumb touch = thumb).
 *
 * The tracked ("raw") hand is then made physical: the palm cannot pass into
 * solid colliders and each finger is solved joint by joint, so a fingertip
 * pressed onto the table edge stops at the surface and the finger bends.
 */
#include "VrGL.h"
#include "VrXr.h"
#include <vector>
#include <functional>

namespace OpenXcom
{
namespace VR
{

/// Axis-aligned solid in world space. Joints are pushed out of it.
struct Collider
{
	glm::vec3 mn{0.f}, mx{0.f};
	int id = -1;                       // owner id (table, button n, ...), reported in contacts
	std::vector<glm::vec4> holes;      // xz rects (xmin, zmin, xmax, zmax) where this solid is open
	bool insideHole(const glm::vec3 &p, float r) const;
};

enum Finger { F_THUMB = 0, F_INDEX, F_MIDDLE, F_RING, F_LITTLE };

struct HandPose
{
	bool valid = false;
	bool skeletal = false;             // from real finger tracking
	glm::vec3 raw[HAND_JOINTS];        // tracked, world space
	glm::vec3 pos[HAND_JOINTS];        // after collision, world space
	float radius[HAND_JOINTS];
	glm::quat palmRot{1.f, 0.f, 0.f, 0.f}; // world orientation of the palm
	float curl[5] = {0, 0, 0, 0, 0};   // 0 straight .. 1 fully curled
	// gestures
	bool pinch = false, pinchStarted = false, pinchEnded = false;
	glm::vec3 pinchPoint{0.f};
	bool indexExtended = false;
	// contacts this frame (joint index -> collider id, -1 none)
	int contact[HAND_JOINTS];
	float rawDepth[HAND_JOINTS];       // how far the tracked joint went into the solid (m)
	glm::vec3 tipVelocity{0.f};        // raw index tip velocity, world m/s
	bool ghost = false;                // hand pushed too deep: shown see-through, no collision
	glm::vec3 tip(int finger) const;
	glm::vec3 rawTip(int finger) const;
};

/// Joint index of a finger's tip.
int fingerTip(int finger);

class Hands
{
public:
	Hands() { for (auto &h : _side) for (int &f : h) f = -1; }
	void init();
	/// Builds both hands for this frame. rig = tracking space -> world.
	void update(const HandState in[2], const glm::mat4 &rig, float dt, const std::vector<Collider> &colliders);
	/// Desktop preview: pose a simulated right hand (grip pose in world) with finger curls.
	void simulate(int hand, const Pose &gripWorld, float indexCurl, float othersCurl, bool thumbDown, float dt, const std::vector<Collider> &colliders);
	void hide(int hand) { pose[hand].valid = false; }
	void draw(const Shader &sh) const;
	HandPose pose[2];
private:
	void solve(int hand, float dt, const std::vector<Collider> &colliders);
	void buildProcedural(int hand, const glm::mat4 &gripWorld, const float curl[5]);
	Mesh _sphere, _cyl;
	glm::vec3 _lastRawTip[2];
	bool _lastValid[2] = {false, false};
	int _side[2][HAND_JOINTS] = {};    // per joint: which face it was outside of last frame
	float _smoothCurl[2][5] = {{0}};
};

}
}
