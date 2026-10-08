#include "VrXr.h"
#include "../Engine/Logger.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <vector>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32
#else
#include <X11/Xlib.h>
#include <GL/glx.h>
#define XR_USE_PLATFORM_XLIB
#endif
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace OpenXcom
{
namespace VR
{

glm::mat4 Pose::matrix() const
{
	return glm::translate(glm::mat4(1.f), pos) * glm::mat4_cast(rot);
}

glm::mat4 EyeView::projection(float nearZ, float farZ) const
{
	// Asymmetric frustum from OpenXR tangent angles (GL clip space, -1..1 depth).
	float l = std::tan(angleLeft), r = std::tan(angleRight), u = std::tan(angleUp), d = std::tan(angleDown);
	float w = r - l, h = u - d;
	glm::mat4 m(0.f);
	m[0][0] = 2.f / w;
	m[1][1] = 2.f / h;
	m[2][0] = (r + l) / w;
	m[2][1] = (u + d) / h;
	m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
	m[2][3] = -1.f;
	m[3][2] = -(2.f * farZ * nearZ) / (farZ - nearZ);
	return m;
}

static Pose toPose(const XrPosef &p)
{
	Pose o;
	o.pos = glm::vec3(p.position.x, p.position.y, p.position.z);
	o.rot = glm::quat(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z);
	return o;
}

#define XR_CHECK(call) xrCheck((call), #call, _p->instance)
static bool xrCheck(XrResult r, const char *what, XrInstance inst)
{
	if (XR_SUCCEEDED(r)) return true;
	char buf[XR_MAX_RESULT_STRING_SIZE] = "?";
	if (inst != XR_NULL_HANDLE) xrResultToString(inst, r, buf);
	Log(LOG_ERROR) << "[VR] " << what << " failed: " << buf << " (" << (int)r << ")";
	return false;
}

struct Swap
{
	XrSwapchain handle = XR_NULL_HANDLE;
	std::vector<XrSwapchainImageOpenGLKHR> images;
	int w = 0, h = 0;
	uint32_t index = 0;
};

struct XrRuntime::Impl
{
	XrInstance instance = XR_NULL_HANDLE;
	XrSystemId system = XR_NULL_SYSTEM_ID;
	::XrSession session = XR_NULL_HANDLE;
	XrSpace appSpace = XR_NULL_HANDLE;
	XrSessionState state = XR_SESSION_STATE_UNKNOWN;
	bool running = false;
	bool stage = false;
	bool exitRequested = false;
	XrViewConfigurationView cfg[2];
	Swap swaps[2];
	XrFrameState frame{XR_TYPE_FRAME_STATE};
	XrView views[2];
	bool frameBegun = false;
	std::string runtime;

	XrActionSet actionSet = XR_NULL_HANDLE;
	bool handExt = false, motionRangeExt = false;
	bool frameExt = false;       // XR_VALVE_frame_controller_interaction (Steam Frame controllers)
	ControllerKind kind[2] = {CTRL_OTHER, CTRL_OTHER};
	bool kindDirty = true;
	void readKinds();
	XrHandTrackerEXT tracker[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
	PFN_xrCreateHandTrackerEXT createHandTracker = nullptr;
	PFN_xrDestroyHandTrackerEXT destroyHandTracker = nullptr;
	PFN_xrLocateHandJointsEXT locateHandJoints = nullptr;
	XrAction squeezeForce = XR_NULL_HANDLE, triggerTouch = XR_NULL_HANDLE, thumbTouch = XR_NULL_HANDLE;
	XrAction aTouch = XR_NULL_HANDLE, trackpad = XR_NULL_HANDLE, trackpadTouch = XR_NULL_HANDLE;
	XrAction aimPose = XR_NULL_HANDLE, gripPose = XR_NULL_HANDLE, trigger = XR_NULL_HANDLE, squeeze = XR_NULL_HANDLE,
		stick = XR_NULL_HANDLE, btnA = XR_NULL_HANDLE, btnB = XR_NULL_HANDLE, stickClick = XR_NULL_HANDLE, vibrate = XR_NULL_HANDLE;
	XrAction btnX = XR_NULL_HANDLE, btnY = XR_NULL_HANDLE, dpadUp = XR_NULL_HANDLE, dpadDown = XR_NULL_HANDLE,
		dpadLeft = XR_NULL_HANDLE, btnMenu = XR_NULL_HANDLE;
	XrPath hand[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrSpace aimSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE}, gripSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};

	XrPath path(const char *s)
	{
		XrPath p = XR_NULL_PATH;
		xrStringToPath(instance, s, &p);
		return p;
	}
};

/// Reads which controller each hand holds (the runtime's current interaction profile).
void XrRuntime::Impl::readKinds()
{
	kindDirty = false;
	if (!session) return;
	for (int h = 0; h < 2; ++h)
	{
		XrInteractionProfileState st{XR_TYPE_INTERACTION_PROFILE_STATE};
		ControllerKind k = CTRL_OTHER;
		if (xrGetCurrentInteractionProfile(session, hand[h], &st) == XR_SUCCESS && st.interactionProfile != XR_NULL_PATH)
		{
			char buf[XR_MAX_PATH_LENGTH];
			uint32_t n = 0;
			if (xrPathToString(instance, st.interactionProfile, sizeof(buf), &n, buf) == XR_SUCCESS)
			{
				std::string p(buf);
				if (p.find("index_controller") != std::string::npos) k = CTRL_INDEX;
				else if (p.find("frame_controller") != std::string::npos) k = CTRL_FRAME;
				else if (p.find("touch") != std::string::npos) k = CTRL_TOUCH; // Oculus/Meta Touch (Quest)
				if (k != kind[h]) Log(LOG_INFO) << "[VR] " << (h ? "right" : "left") << " controller: " << p;
			}
		}
		kind[h] = k;
	}
}

XrRuntime::XrRuntime() : _p(new Impl) {}
XrRuntime::~XrRuntime() { shutdown(); }

static XrAction makeAction(XrRuntime::Impl *p, XrActionType type, const char *name, const char *loc)
{
	XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
	ai.actionType = type;
	std::strncpy(ai.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
	std::strncpy(ai.localizedActionName, loc, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	ai.countSubactionPaths = 2;
	ai.subactionPaths = p->hand;
	XrAction a = XR_NULL_HANDLE;
	xrCheck(xrCreateAction(p->actionSet, &ai, &a), name, p->instance);
	return a;
}

bool XrRuntime::init(const std::string &appName)
{
	// ---- instance
	uint32_t extCount = 0;
	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr)))
	{
		Log(LOG_ERROR) << "[VR] No OpenXR runtime found. Is SteamVR installed and set as the OpenXR runtime?";
		return false;
	}
	std::vector<XrExtensionProperties> exts(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
	xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());
	bool hasGL = false;
	for (auto &e : exts)
	{
		if (!std::strcmp(e.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) hasGL = true;
		if (!std::strcmp(e.extensionName, XR_EXT_HAND_TRACKING_EXTENSION_NAME)) _p->handExt = true;
		if (!std::strcmp(e.extensionName, XR_EXT_HAND_JOINTS_MOTION_RANGE_EXTENSION_NAME)) _p->motionRangeExt = true;
		if (!std::strcmp(e.extensionName, "XR_VALVE_frame_controller_interaction")) _p->frameExt = true;
	}
	if (!hasGL)
	{
		Log(LOG_ERROR) << "[VR] OpenXR runtime does not support OpenGL (XR_KHR_opengl_enable).";
		return false;
	}
	std::vector<const char*> enabled = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
	if (_p->handExt) enabled.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
	if (_p->handExt && _p->motionRangeExt) enabled.push_back(XR_EXT_HAND_JOINTS_MOTION_RANGE_EXTENSION_NAME);
	if (_p->frameExt) enabled.push_back("XR_VALVE_frame_controller_interaction");
	XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
	std::strncpy(ici.applicationInfo.applicationName, appName.c_str(), XR_MAX_APPLICATION_NAME_SIZE - 1);
	ici.applicationInfo.applicationVersion = 1;
	std::strncpy(ici.applicationInfo.engineName, "OpenXcom Extended", XR_MAX_ENGINE_NAME_SIZE - 1);
	ici.applicationInfo.engineVersion = 1;
	ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
	ici.enabledExtensionCount = (uint32_t)enabled.size();
	ici.enabledExtensionNames = enabled.data();
	if (!XR_CHECK(xrCreateInstance(&ici, &_p->instance))) return false;

	XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
	xrGetInstanceProperties(_p->instance, &ip);
	_p->runtime = ip.runtimeName;
	Log(LOG_INFO) << "[VR] OpenXR runtime: " << ip.runtimeName << " "
		<< XR_VERSION_MAJOR(ip.runtimeVersion) << "." << XR_VERSION_MINOR(ip.runtimeVersion) << "." << XR_VERSION_PATCH(ip.runtimeVersion);

	// ---- system
	XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
	sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (!XR_CHECK(xrGetSystem(_p->instance, &sgi, &_p->system)))
	{
		Log(LOG_ERROR) << "[VR] No headset found. Is it connected and is SteamVR running?";
		return false;
	}
	PFN_xrGetOpenGLGraphicsRequirementsKHR getReq = nullptr;
	xrGetInstanceProcAddr(_p->instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&getReq);
	XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
	if (!getReq || !XR_CHECK(getReq(_p->instance, _p->system, &req))) return false;
	if (_p->handExt)
	{
		XrSystemHandTrackingPropertiesEXT htp{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
		XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
		sp.next = &htp;
		if (XR_SUCCEEDED(xrGetSystemProperties(_p->instance, _p->system, &sp)) && !htp.supportsHandTracking) _p->handExt = false;
	}

	// ---- session on the engine's GL context
#ifdef _WIN32
	XrGraphicsBindingOpenGLWin32KHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
	binding.hDC = wglGetCurrentDC();
	binding.hGLRC = wglGetCurrentContext();
#else
	XrGraphicsBindingOpenGLXlibKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
	Display *dpy = glXGetCurrentDisplay();
	GLXContext ctx = glXGetCurrentContext();
	binding.xDisplay = dpy;
	binding.glxContext = ctx;
	binding.glxDrawable = glXGetCurrentDrawable();
	int fbId = 0, screen = 0;
	glXQueryContext(dpy, ctx, GLX_FBCONFIG_ID, &fbId);
	glXQueryContext(dpy, ctx, GLX_SCREEN, &screen);
	int attrs[] = {GLX_FBCONFIG_ID, fbId, None};
	int n = 0;
	GLXFBConfig *cfgs = glXChooseFBConfig(dpy, screen, attrs, &n);
	if (cfgs && n > 0)
	{
		binding.glxFBConfig = cfgs[0];
		XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, cfgs[0]);
		if (vi) { binding.visualid = (uint32_t)vi->visualid; XFree(vi); }
		XFree(cfgs);
	}
#endif
	XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
	sci.next = &binding;
	sci.systemId = _p->system;
	if (!XR_CHECK(xrCreateSession(_p->instance, &sci, &_p->session))) return false;

	// ---- reference space: floor-level stage if the runtime has one
	uint32_t spaceCount = 0;
	xrEnumerateReferenceSpaces(_p->session, 0, &spaceCount, nullptr);
	std::vector<XrReferenceSpaceType> spaces(spaceCount);
	xrEnumerateReferenceSpaces(_p->session, spaceCount, &spaceCount, spaces.data());
	for (auto s : spaces) if (s == XR_REFERENCE_SPACE_TYPE_STAGE) _p->stage = true;
	XrReferenceSpaceCreateInfo rsi{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
	rsi.referenceSpaceType = _p->stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
	rsi.poseInReferenceSpace.orientation.w = 1.f;
	if (!XR_CHECK(xrCreateReferenceSpace(_p->session, &rsi, &_p->appSpace))) return false;
	Log(LOG_INFO) << "[VR] tracking space: " << (_p->stage ? "STAGE (floor level)" : "LOCAL (seated)");

	// ---- views & swapchains
	uint32_t viewCount = 0;
	xrEnumerateViewConfigurationViews(_p->instance, _p->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
	if (viewCount != 2)
	{
		Log(LOG_ERROR) << "[VR] expected 2 stereo views, got " << viewCount;
		return false;
	}
	for (auto &c : _p->cfg) c = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
	xrEnumerateViewConfigurationViews(_p->instance, _p->system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &viewCount, _p->cfg);

	uint32_t fmtCount = 0;
	xrEnumerateSwapchainFormats(_p->session, 0, &fmtCount, nullptr);
	std::vector<int64_t> fmts(fmtCount);
	xrEnumerateSwapchainFormats(_p->session, fmtCount, &fmtCount, fmts.data());
	int64_t chosen = 0;
	for (auto f : fmts) if (f == GL_SRGB8_ALPHA8) chosen = f;
	if (!chosen)
	{
		for (auto f : fmts) if (f == GL_RGBA8) chosen = f;
		Log(LOG_WARNING) << "[VR] runtime has no sRGB swapchain format; colors may look washed out";
	}
	if (!chosen && !fmts.empty()) chosen = fmts[0];

	for (int e = 0; e < 2; ++e)
	{
		Swap &s = _p->swaps[e];
		s.w = (int)_p->cfg[e].recommendedImageRectWidth;
		s.h = (int)_p->cfg[e].recommendedImageRectHeight;
		XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
		ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		ci.format = chosen;
		ci.sampleCount = 1;
		ci.width = s.w;
		ci.height = s.h;
		ci.faceCount = 1;
		ci.arraySize = 1;
		ci.mipCount = 1;
		if (!XR_CHECK(xrCreateSwapchain(_p->session, &ci, &s.handle))) return false;
		uint32_t imgCount = 0;
		xrEnumerateSwapchainImages(s.handle, 0, &imgCount, nullptr);
		s.images.assign(imgCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
		xrEnumerateSwapchainImages(s.handle, imgCount, &imgCount, (XrSwapchainImageBaseHeader*)s.images.data());
	}
	Log(LOG_INFO) << "[VR] eye resolution " << _p->swaps[0].w << "x" << _p->swaps[0].h;

	// ---- actions
	XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
	std::strcpy(asci.actionSetName, "tabletop");
	std::strcpy(asci.localizedActionSetName, "Tabletop");
	if (!XR_CHECK(xrCreateActionSet(_p->instance, &asci, &_p->actionSet))) return false;
	_p->hand[0] = _p->path("/user/hand/left");
	_p->hand[1] = _p->path("/user/hand/right");
	_p->aimPose = makeAction(_p.get(), XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim pose");
	_p->gripPose = makeAction(_p.get(), XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip pose");
	_p->trigger = makeAction(_p.get(), XR_ACTION_TYPE_FLOAT_INPUT, "select", "Select / click");
	_p->squeeze = makeAction(_p.get(), XR_ACTION_TYPE_FLOAT_INPUT, "grab", "Grab table");
	_p->stick = makeAction(_p.get(), XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Scroll / turn");
	_p->btnA = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a", "Right click / panel");
	_p->btnB = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b", "Back / recenter");
	_p->stickClick = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Stick click");
	_p->vibrate = makeAction(_p.get(), XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Haptic");
	_p->squeezeForce = makeAction(_p.get(), XR_ACTION_TYPE_FLOAT_INPUT, "grab_force", "Grab force (Index)");
	_p->triggerTouch = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_touch", "Index finger on trigger");
	_p->thumbTouch = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "thumb_touch", "Thumb resting");
	_p->aTouch = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "a_touch", "Thumb on A (pinch)");
	_p->trackpad = makeAction(_p.get(), XR_ACTION_TYPE_VECTOR2F_INPUT, "trackpad", "Trackpad (zoom)");
	_p->trackpadTouch = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "trackpad_touch", "Trackpad touched");
	_p->btnX = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x", "Right click (Frame X)");
	_p->btnY = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y", "Zoom in (Frame Y)");
	_p->dpadUp = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "dpad_up", "Level up (Frame D-pad)");
	_p->dpadDown = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "dpad_down", "Level down (Frame D-pad)");
	_p->dpadLeft = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "dpad_left", "Recenter / seat height (Frame D-pad)");
	_p->btnMenu = makeAction(_p.get(), XR_ACTION_TYPE_BOOLEAN_INPUT, "button_menu", "Back (menu / view)");

	struct B { XrAction a; const char *p; };
	auto suggest = [&](const char *profile, std::vector<B> list)
	{
		std::vector<XrActionSuggestedBinding> sb;
		for (auto &b : list) sb.push_back({b.a, _p->path(b.p)});
		XrInteractionProfileSuggestedBinding s{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
		s.interactionProfile = _p->path(profile);
		s.countSuggestedBindings = (uint32_t)sb.size();
		s.suggestedBindings = sb.data();
		XrResult r = xrSuggestInteractionProfileBindings(_p->instance, &s);
		if (XR_FAILED(r)) Log(LOG_WARNING) << "[VR] bindings rejected for " << profile << " (" << (int)r << ")";
	};
	auto both = [](std::vector<B> &v, XrAction a, const char *suffix)
	{
		static std::vector<std::string> keep; // stable storage for the strings
		keep.push_back(std::string("/user/hand/left") + suffix);
		v.push_back({a, keep.back().c_str()});
		keep.push_back(std::string("/user/hand/right") + suffix);
		v.push_back({a, keep.back().c_str()});
	};
	{
		std::vector<B> v;
		both(v, _p->aimPose, "/input/aim/pose");
		both(v, _p->gripPose, "/input/grip/pose");
		both(v, _p->trigger, "/input/trigger/value");
		both(v, _p->squeeze, "/input/squeeze/value");
		both(v, _p->stick, "/input/thumbstick");
		both(v, _p->btnA, "/input/a/click");
		both(v, _p->btnB, "/input/b/click");
		both(v, _p->stickClick, "/input/thumbstick/click");
		both(v, _p->vibrate, "/output/haptic");
		both(v, _p->squeezeForce, "/input/squeeze/force");
		both(v, _p->triggerTouch, "/input/trigger/touch");
		both(v, _p->thumbTouch, "/input/thumbstick/touch");
		both(v, _p->thumbTouch, "/input/a/touch");
		both(v, _p->thumbTouch, "/input/b/touch");
		both(v, _p->thumbTouch, "/input/trackpad/touch");
		both(v, _p->aTouch, "/input/a/touch");
		both(v, _p->trackpad, "/input/trackpad");
		both(v, _p->trackpadTouch, "/input/trackpad/touch");
		suggest("/interaction_profiles/valve/index_controller", v);
	}
	{
		std::vector<B> v;
		both(v, _p->aimPose, "/input/aim/pose");
		both(v, _p->gripPose, "/input/grip/pose");
		both(v, _p->trigger, "/input/trigger/value");
		both(v, _p->squeeze, "/input/squeeze/value");
		both(v, _p->stick, "/input/thumbstick");
		both(v, _p->stickClick, "/input/thumbstick/click");
		both(v, _p->vibrate, "/output/haptic");
		v.push_back({_p->btnA, "/user/hand/left/input/x/click"});
		v.push_back({_p->btnB, "/user/hand/left/input/y/click"});
		v.push_back({_p->btnA, "/user/hand/right/input/a/click"});
		v.push_back({_p->btnB, "/user/hand/right/input/b/click"});
		both(v, _p->triggerTouch, "/input/trigger/touch");
		both(v, _p->thumbTouch, "/input/thumbstick/touch");
		v.push_back({_p->thumbTouch, "/user/hand/left/input/x/touch"});
		v.push_back({_p->thumbTouch, "/user/hand/left/input/y/touch"});
		v.push_back({_p->thumbTouch, "/user/hand/right/input/a/touch"});
		v.push_back({_p->thumbTouch, "/user/hand/right/input/b/touch"});
		v.push_back({_p->aTouch, "/user/hand/left/input/x/touch"});
		v.push_back({_p->aTouch, "/user/hand/right/input/a/touch"});
		v.push_back({_p->btnMenu, "/user/hand/left/input/menu/click"});
		suggest("/interaction_profiles/oculus/touch_controller", v);
	}
	if (_p->frameExt)
	{
		// Steam Frame controllers: left has a D-pad and View, right has A/B/X/Y and Menu
		std::vector<B> v;
		both(v, _p->aimPose, "/input/aim/pose");
		both(v, _p->gripPose, "/input/grip/pose");
		both(v, _p->trigger, "/input/trigger/value");
		both(v, _p->squeeze, "/input/squeeze/value");
		both(v, _p->stick, "/input/thumbstick");
		both(v, _p->stickClick, "/input/thumbstick/click");
		both(v, _p->vibrate, "/output/haptic");
		both(v, _p->triggerTouch, "/input/trigger/touch");
		both(v, _p->thumbTouch, "/input/thumbstick/touch");
		v.push_back({_p->btnA, "/user/hand/right/input/a/click"});
		v.push_back({_p->btnB, "/user/hand/right/input/b/click"});
		v.push_back({_p->btnX, "/user/hand/right/input/x/click"});
		v.push_back({_p->btnY, "/user/hand/right/input/y/click"});
		v.push_back({_p->btnMenu, "/user/hand/right/input/menu/click"});
		v.push_back({_p->btnMenu, "/user/hand/left/input/view/click"});
		v.push_back({_p->dpadUp, "/user/hand/left/input/dpad_up/click"});
		v.push_back({_p->dpadDown, "/user/hand/left/input/dpad_down/click"});
		v.push_back({_p->dpadLeft, "/user/hand/left/input/dpad_left/click"});
		v.push_back({_p->thumbTouch, "/user/hand/right/input/a/touch"});
		v.push_back({_p->thumbTouch, "/user/hand/right/input/b/touch"});
		v.push_back({_p->thumbTouch, "/user/hand/right/input/x/touch"});
		v.push_back({_p->thumbTouch, "/user/hand/right/input/y/touch"});
		v.push_back({_p->aTouch, "/user/hand/right/input/a/touch"});
		// the left thumb resting on the D-pad pinches, like the right one on A
		for (const char *d : {"/user/hand/left/input/dpad_up/touch", "/user/hand/left/input/dpad_down/touch",
			"/user/hand/left/input/dpad_left/touch", "/user/hand/left/input/dpad_right/touch"})
		{
			v.push_back({_p->thumbTouch, d});
			v.push_back({_p->aTouch, d});
		}
		suggest("/interaction_profiles/valve/frame_controller_valve", v);
	}
	{
		std::vector<B> v;
		both(v, _p->aimPose, "/input/aim/pose");
		both(v, _p->gripPose, "/input/grip/pose");
		both(v, _p->trigger, "/input/trigger/value");
		both(v, _p->squeeze, "/input/squeeze/click");
		both(v, _p->stick, "/input/trackpad");
		both(v, _p->stickClick, "/input/trackpad/click");
		both(v, _p->btnB, "/input/menu/click");
		both(v, _p->vibrate, "/output/haptic");
		suggest("/interaction_profiles/htc/vive_controller", v);
	}
	{
		std::vector<B> v;
		both(v, _p->aimPose, "/input/aim/pose");
		both(v, _p->gripPose, "/input/grip/pose");
		both(v, _p->trigger, "/input/select/click");
		both(v, _p->btnB, "/input/menu/click");
		both(v, _p->vibrate, "/output/haptic");
		suggest("/interaction_profiles/khr/simple_controller", v);
	}
	for (int h = 0; h < 2; ++h)
	{
		XrActionSpaceCreateInfo si{XR_TYPE_ACTION_SPACE_CREATE_INFO};
		si.poseInActionSpace.orientation.w = 1.f;
		si.subactionPath = _p->hand[h];
		si.action = _p->aimPose;
		XR_CHECK(xrCreateActionSpace(_p->session, &si, &_p->aimSpace[h]));
		si.action = _p->gripPose;
		XR_CHECK(xrCreateActionSpace(_p->session, &si, &_p->gripSpace[h]));
	}
	XrSessionActionSetsAttachInfo att{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
	att.countActionSets = 1;
	att.actionSets = &_p->actionSet;
	if (!XR_CHECK(xrAttachSessionActionSets(_p->session, &att))) return false;

	if (_p->handExt)
	{
		xrGetInstanceProcAddr(_p->instance, "xrCreateHandTrackerEXT", (PFN_xrVoidFunction*)&_p->createHandTracker);
		xrGetInstanceProcAddr(_p->instance, "xrDestroyHandTrackerEXT", (PFN_xrVoidFunction*)&_p->destroyHandTracker);
		xrGetInstanceProcAddr(_p->instance, "xrLocateHandJointsEXT", (PFN_xrVoidFunction*)&_p->locateHandJoints);
		bool ok = _p->createHandTracker && _p->locateHandJoints;
		for (int h = 0; h < 2 && ok; ++h)
		{
			XrHandTrackerCreateInfoEXT ci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
			ci.hand = h == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
			ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
			ok = XR_SUCCEEDED(_p->createHandTracker(_p->session, &ci, &_p->tracker[h]));
		}
		if (!ok) _p->handExt = false;
	}
	Log(LOG_INFO) << "[VR] finger tracking: " << (_p->handExt ? "XR_EXT_hand_tracking" : "not available (hands posed from controller buttons)");

	for (auto &v : _p->views) v = {XR_TYPE_VIEW};
	return true;
}

void XrRuntime::shutdown()
{
	if (!_p) return;
	for (auto &t : _p->tracker) if (t && _p->destroyHandTracker) { _p->destroyHandTracker(t); t = XR_NULL_HANDLE; }
	for (auto &s : _p->swaps) if (s.handle) { xrDestroySwapchain(s.handle); s.handle = XR_NULL_HANDLE; }
	for (int h = 0; h < 2; ++h)
	{
		if (_p->aimSpace[h]) xrDestroySpace(_p->aimSpace[h]);
		if (_p->gripSpace[h]) xrDestroySpace(_p->gripSpace[h]);
		_p->aimSpace[h] = _p->gripSpace[h] = XR_NULL_HANDLE;
	}
	if (_p->appSpace) { xrDestroySpace(_p->appSpace); _p->appSpace = XR_NULL_HANDLE; }
	if (_p->actionSet) { xrDestroyActionSet(_p->actionSet); _p->actionSet = XR_NULL_HANDLE; }
	if (_p->session) { xrDestroySession(_p->session); _p->session = XR_NULL_HANDLE; }
	if (_p->instance) { xrDestroyInstance(_p->instance); _p->instance = XR_NULL_HANDLE; }
	_p->running = false;
}

bool XrRuntime::pollEvents()
{
	if (!_p->instance) return false;
	XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
	while (xrPollEvent(_p->instance, &ev) == XR_SUCCESS)
	{
		switch (ev.type)
		{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
		{
			auto *sc = (XrEventDataSessionStateChanged*)&ev;
			_p->state = sc->state;
			Log(LOG_INFO) << "[VR] session state -> " << (int)sc->state;
			if (sc->state == XR_SESSION_STATE_READY)
			{
				XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
				bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
				if (XR_CHECK(xrBeginSession(_p->session, &bi))) _p->running = true;
			}
			else if (sc->state == XR_SESSION_STATE_STOPPING)
			{
				xrEndSession(_p->session);
				_p->running = false;
			}
			else if (sc->state == XR_SESSION_STATE_EXITING || sc->state == XR_SESSION_STATE_LOSS_PENDING)
			{
				_p->running = false;
				_p->exitRequested = true;
			}
			break;
		}
		case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
			_p->kindDirty = true;
			break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			_p->exitRequested = true;
			_p->running = false;
			break;
		default:
			break;
		}
		ev = {XR_TYPE_EVENT_DATA_BUFFER};
	}
	return !_p->exitRequested;
}

bool XrRuntime::isRunning() const { return _p->running; }
bool XrRuntime::isFocused() const { return _p->state == XR_SESSION_STATE_FOCUSED; }
int XrRuntime::eyeWidth() const { return _p->swaps[0].w; }
int XrRuntime::eyeHeight() const { return _p->swaps[0].h; }
bool XrRuntime::floorLevel() const { return _p->stage; }
bool XrRuntime::hasHandTracking() const { return _p->handExt; }
std::string XrRuntime::runtimeName() const { return _p->runtime; }

bool XrRuntime::beginFrame(EyeView views[2], HandState hands[2], bool &shouldRender)
{
	shouldRender = false;
	_p->frameBegun = false;
	if (!_p->running) return false;
	XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
	_p->frame = {XR_TYPE_FRAME_STATE};
	if (!XR_CHECK(xrWaitFrame(_p->session, &wi, &_p->frame))) return false;
	XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
	if (!XR_CHECK(xrBeginFrame(_p->session, &bi))) return false;
	_p->frameBegun = true;

	// input
	XrActiveActionSet active{_p->actionSet, XR_NULL_PATH};
	XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
	sync.countActiveActionSets = 1;
	sync.activeActionSets = &active;
	bool synced = xrSyncActions(_p->session, &sync) == XR_SUCCESS;
	XrTime t = _p->frame.predictedDisplayTime;
	for (int h = 0; h < 2; ++h)
	{
		HandState &hs = hands[h];
		XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
		gi.subactionPath = _p->hand[h];
		XrActionStateFloat f{XR_TYPE_ACTION_STATE_FLOAT};
		XrActionStateBoolean b{XR_TYPE_ACTION_STATE_BOOLEAN};
		XrActionStateVector2f v{XR_TYPE_ACTION_STATE_VECTOR2F};
		gi.action = _p->trigger; hs.trigger = (synced && xrGetActionStateFloat(_p->session, &gi, &f) == XR_SUCCESS && f.isActive) ? f.currentState : 0.f;
		gi.action = _p->squeeze; hs.squeeze = (synced && xrGetActionStateFloat(_p->session, &gi, &f) == XR_SUCCESS && f.isActive) ? f.currentState : 0.f;
		gi.action = _p->stick; hs.stick = (synced && xrGetActionStateVector2f(_p->session, &gi, &v) == XR_SUCCESS && v.isActive) ? glm::vec2(v.currentState.x, v.currentState.y) : glm::vec2(0.f);
		gi.action = _p->btnA; hs.a.update(synced && xrGetActionStateBoolean(_p->session, &gi, &b) == XR_SUCCESS && b.isActive && b.currentState);
		gi.action = _p->btnB; hs.b.update(synced && xrGetActionStateBoolean(_p->session, &gi, &b) == XR_SUCCESS && b.isActive && b.currentState);
		gi.action = _p->stickClick; hs.stickClick.update(synced && xrGetActionStateBoolean(_p->session, &gi, &b) == XR_SUCCESS && b.isActive && b.currentState);
		gi.action = _p->squeezeForce;
		hs.hasForce = synced && xrGetActionStateFloat(_p->session, &gi, &f) == XR_SUCCESS && f.isActive;
		hs.squeezeForce = hs.hasForce ? f.currentState : 0.f;
		gi.action = _p->triggerTouch; hs.triggerTouch = synced && xrGetActionStateBoolean(_p->session, &gi, &b) == XR_SUCCESS && b.isActive && b.currentState;
		gi.action = _p->thumbTouch; hs.thumbTouch = synced && xrGetActionStateBoolean(_p->session, &gi, &b) == XR_SUCCESS && b.isActive && b.currentState;
		gi.action = _p->aTouch; hs.aTouch = synced && xrGetActionStateBoolean(_p->session, &gi, &b) == XR_SUCCESS && b.isActive && b.currentState;
		gi.action = _p->trackpadTouch; hs.trackpadTouch = synced && xrGetActionStateBoolean(_p->session, &gi, &b) == XR_SUCCESS && b.isActive && b.currentState;
		gi.action = _p->trackpad; hs.trackpad = (synced && xrGetActionStateVector2f(_p->session, &gi, &v) == XR_SUCCESS && v.isActive) ? glm::vec2(v.currentState.x, v.currentState.y) : glm::vec2(0.f);
		auto readBtn = [&](XrAction a, Button &btn) { gi.action = a; btn.update(synced && xrGetActionStateBoolean(_p->session, &gi, &b) == XR_SUCCESS && b.isActive && b.currentState); };
		readBtn(_p->btnX, hs.x);
		readBtn(_p->btnY, hs.y);
		readBtn(_p->dpadUp, hs.dpadUp);
		readBtn(_p->dpadDown, hs.dpadDown);
		readBtn(_p->dpadLeft, hs.dpadLeft);
		readBtn(_p->btnMenu, hs.menu);
		if (_p->kindDirty) _p->readKinds();
		hs.kind = _p->kind[h];
		hs.triggerBtn.update(hs.trigger > hs.triggerHyst(hs.triggerBtn.down));
		hs.squeezeBtn.update(hs.squeeze > (hs.squeezeBtn.down ? 0.3f : 0.6f));

		XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
		const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
		hs.active = false;
		if (xrLocateSpace(_p->aimSpace[h], _p->appSpace, t, &loc) == XR_SUCCESS && (loc.locationFlags & need) == need)
		{
			hs.aim = toPose(loc.pose);
			hs.active = true;
		}
		loc = {XR_TYPE_SPACE_LOCATION};
		if (xrLocateSpace(_p->gripSpace[h], _p->appSpace, t, &loc) == XR_SUCCESS && (loc.locationFlags & need) == need)
		{
			hs.grip = toPose(loc.pose);
		}
		else if (hs.active)
		{
			hs.grip = hs.aim;
		}

		hs.jointsValid = false;
		if (_p->handExt && _p->tracker[h])
		{
			XrHandJointLocationEXT jl[XR_HAND_JOINT_COUNT_EXT];
			XrHandJointLocationsEXT locs{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
			locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
			locs.jointLocations = jl;
			XrHandJointsLocateInfoEXT li{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
			li.baseSpace = _p->appSpace;
			li.time = t;
			XrHandJointsMotionRangeInfoEXT mr{XR_TYPE_HAND_JOINTS_MOTION_RANGE_INFO_EXT};
			if (_p->motionRangeExt)
			{
				// the real finger positions, not curled around the controller
				mr.handJointsMotionRange = XR_HAND_JOINTS_MOTION_RANGE_UNOBSTRUCTED_EXT;
				li.next = &mr;
			}
			if (XR_SUCCEEDED(_p->locateHandJoints(_p->tracker[h], &li, &locs)) && locs.isActive)
			{
				bool all = true;
				for (int j = 0; j < XR_HAND_JOINT_COUNT_EXT; ++j)
				{
					if (!(jl[j].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) { all = false; break; }
					Pose jp = toPose(jl[j].pose);
					hs.jointPos[j] = jp.pos;
					hs.jointRot[j] = jp.rot;
					hs.jointRadius[j] = jl[j].radius > 0.f ? jl[j].radius : 0.008f;
				}
				hs.jointsValid = all;
			}
		}
	}

	if (!_p->frame.shouldRender) return true;

	XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
	vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	vli.displayTime = t;
	vli.space = _p->appSpace;
	XrViewState vs{XR_TYPE_VIEW_STATE};
	uint32_t count = 0;
	for (auto &v : _p->views) v = {XR_TYPE_VIEW};
	if (!XR_CHECK(xrLocateViews(_p->session, &vli, &vs, 2, &count, _p->views))) return true;
	if (!(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) return true;
	for (int e = 0; e < 2; ++e)
	{
		views[e].pose = toPose(_p->views[e].pose);
		views[e].angleLeft = _p->views[e].fov.angleLeft;
		views[e].angleRight = _p->views[e].fov.angleRight;
		views[e].angleUp = _p->views[e].fov.angleUp;
		views[e].angleDown = _p->views[e].fov.angleDown;
	}
	shouldRender = true;
	return true;
}

void *XrRuntime::currentGLContext()
{
#ifdef _WIN32
	return (void*)wglGetCurrentContext();
#else
	return (void*)glXGetCurrentContext();
#endif
}

GLuint XrRuntime::acquireEye(int eye)
{
	Swap &s = _p->swaps[eye];
	XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
	if (!XR_CHECK(xrAcquireSwapchainImage(s.handle, &ai, &s.index))) return 0;
	XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
	wi.timeout = XR_INFINITE_DURATION;
	if (!XR_CHECK(xrWaitSwapchainImage(s.handle, &wi))) return 0;
	return s.images[s.index].image;
}

void XrRuntime::releaseEye(int eye)
{
	XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
	xrReleaseSwapchainImage(_p->swaps[eye].handle, &ri);
}

void XrRuntime::endFrame(bool rendered)
{
	if (!_p->frameBegun) return;
	_p->frameBegun = false;
	XrCompositionLayerProjectionView pv[2];
	XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
	const XrCompositionLayerBaseHeader *layers[1] = {(XrCompositionLayerBaseHeader*)&layer};
	XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
	ei.displayTime = _p->frame.predictedDisplayTime;
	ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	if (rendered && _p->frame.shouldRender)
	{
		for (int e = 0; e < 2; ++e)
		{
			pv[e] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
			pv[e].pose = _p->views[e].pose;
			pv[e].fov = _p->views[e].fov;
			pv[e].subImage.swapchain = _p->swaps[e].handle;
			pv[e].subImage.imageRect.offset = {0, 0};
			pv[e].subImage.imageRect.extent = {_p->swaps[e].w, _p->swaps[e].h};
			pv[e].subImage.imageArrayIndex = 0;
		}
		layer.space = _p->appSpace;
		layer.viewCount = 2;
		layer.views = pv;
		ei.layerCount = 1;
		ei.layers = layers;
	}
	XR_CHECK(xrEndFrame(_p->session, &ei));
}

void XrRuntime::haptic(int hand, float amplitude, float seconds)
{
	if (!_p->session || !isFocused()) return;
	XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};
	v.amplitude = amplitude;
	v.duration = (XrDuration)(seconds * 1e9);
	v.frequency = XR_FREQUENCY_UNSPECIFIED;
	XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
	hi.action = _p->vibrate;
	hi.subactionPath = _p->hand[hand];
	xrApplyHapticFeedback(_p->session, &hi, (XrHapticBaseHeader*)&v);
}

}
}
