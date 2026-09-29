#pragma once
/*
 * OXCE VR tabletop - the small API the engine calls into.
 * No GL or OpenXR headers here, so engine files can include it freely.
 *
 * Every function is a cheap no-op when VR is off (vrMode 0) or the
 * engine was built without OXCE_VR.
 */
#include <SDL.h>

namespace OpenXcom
{
class Game;

namespace VR
{

enum Mode { MODE_OFF = 0, MODE_HEADSET = 1, MODE_PREVIEW = 2 };

#ifdef OXCE_VR

/// Adjusts video options before the window is created (forces OpenGL output, etc).
void configureOptions();
/// Starts VR once the engine's GL window exists. Falls back to flat mode on failure.
void startup(Game *game);
void shutdown();
/// True while VR (headset or desktop preview) is driving the display.
bool active();
/// True in desktop preview mode (the window shows the VR scene instead of the flat game).
bool preview();
/// Hands the freshly composed game frame (engine's 8-bit screen surface) to VR.
/// Returns true if the engine should skip presenting it to the desktop window.
bool onScreenFlip(SDL_Surface *surface);
/// Runs one VR frame (input, simulation, rendering, submit). Called once per engine loop.
void frame();
/// Lets VR rewrite or swallow a desktop event. Returns false to drop the event.
bool filterEvent(SDL_Event &ev);
/// Mouse position/buttons as the game should see them (the laser pointer in VR).
Uint8 getMouseState(int *x, int *y);
/// Replacement for SDL_WarpMouse that keeps the VR pointer and the game in sync.
void warpMouse(Uint16 x, Uint16 y);
/// Whether the engine loop should skip its own frame-rate sleep (the headset paces us).
bool pacesLoop();

#else

inline void configureOptions() {}
inline void startup(Game *) {}
inline void shutdown() {}
inline bool active() { return false; }
inline bool preview() { return false; }
inline bool onScreenFlip(SDL_Surface *) { return false; }
inline void frame() {}
inline bool filterEvent(SDL_Event &) { return true; }
inline Uint8 getMouseState(int *x, int *y) { return SDL_GetMouseState(x, y); }
inline void warpMouse(Uint16 x, Uint16 y) { SDL_WarpMouse(x, y); }
inline bool pacesLoop() { return false; }

#endif

}
}
