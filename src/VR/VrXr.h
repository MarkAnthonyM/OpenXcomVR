#pragma once
/*
 * OXCE VR tabletop - OpenXR session wrapper.
 * Keeps all platform/OpenXR headers out of the rest of the code.
 */
#include "VrGL.h"
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>

namespace OpenXcom
{
namespace VR
{

struct Pose
{
	glm::vec3 pos{0.f};
	glm::quat rot{1.f, 0.f, 0.f, 0.f};
	glm::mat4 matrix() const;
	glm::vec3 forward() const { return rot * glm::vec3(0, 0, -1); }
	glm::vec3 up() const { return rot * glm::vec3(0, 1, 0); }
	glm::vec3 right() const { return rot * glm::vec3(1, 0, 0); }
};

struct Button
{
	bool down = false, pressed = false, released = false;
	void update(bool now) { pressed = now && !down; released = !now && down; down = now; }
};

enum { HAND_JOINTS = 26 };   // same order as XR_EXT_hand_tracking (palm, wrist, thumb x4, index..little x5)

struct HandState
{
	bool active = false;       // pose is tracked
	Pose aim;                  // pointing ray origin/direction (tracking space)
	Pose grip;                 // where the controller is held
	float trigger = 0.f, squeeze = 0.f;
	float squeezeForce = 0.f;  // Index only: how hard the grip is actually squeezed
	bool hasForce = false;     // squeezeForce is meaningful (Index controllers)
	bool triggerTouch = false, thumbTouch = false;
	// skeletal hand from XR_EXT_hand_tracking (tracking space); jointsValid false = not available
	bool jointsValid = false;
	glm::vec3 jointPos[HAND_JOINTS];
	glm::quat jointRot[HAND_JOINTS];
	float jointRadius[HAND_JOINTS];
	Button grabBtn;            // deliberate grab (force/threshold + hold time), see VrSystem
	glm::vec2 stick{0.f};
	Button triggerBtn, squeezeBtn, a, b, stickClick;
	float triggerHyst(bool wasDown) const { return wasDown ? 0.35f : 0.65f; }
};

struct EyeView
{
	Pose pose;                 // tracking space
	float angleLeft = -0.8f, angleRight = 0.8f, angleUp = 0.8f, angleDown = -0.8f;
	glm::mat4 projection(float nearZ, float farZ) const;
};

class XrRuntime
{
public:
	XrRuntime();
	~XrRuntime();
	/// Creates instance/session on the current GL context. Returns false if no runtime/headset.
	bool init(const std::string &appName);
	void shutdown();
	/// Processes runtime events. Returns false when the app should stop using VR.
	bool pollEvents();
	/// True while frames should be submitted.
	bool isRunning() const;
	/// True when the headset is on the user's head and showing us.
	bool isFocused() const;
	/// Waits for the next frame and locates views and controllers.
	/// Returns true if a frame was begun (then endFrame() must follow); shouldRender says whether to draw.
	bool beginFrame(EyeView views[2], HandState hands[2], bool &shouldRender);
	/// Recommended per-eye render size.
	int eyeWidth() const;
	int eyeHeight() const;
	/// Gets the swapchain texture for this eye for this frame (acquires it).
	GLuint acquireEye(int eye);
	void releaseEye(int eye);
	/// Submits the frame. Must be called after a successful beginFrame.
	void endFrame(bool rendered);
	void haptic(int hand, float amplitude, float seconds);
	/// Whether the tracking space has its origin on the floor.
	bool floorLevel() const;
	std::string runtimeName() const;
	/// True when the runtime gives us finger joints (XR_EXT_hand_tracking).
	bool hasHandTracking() const;
	/// Handle of the GL context current on this thread (to detect context re-creation).
	static void *currentGLContext();
	struct Impl;
private:
	std::unique_ptr<Impl> _p;
};

}
}
