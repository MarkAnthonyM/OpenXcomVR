/*
 * OXCE VR tabletop - the prototype's own sounds (see VrAudio.h).
 */
#include "VrAudio.h"
#include "../Engine/Options.h"
#include "../Engine/Logger.h"
#include <SDL_mixer.h>
#include <algorithm>
#include <cmath>

namespace OpenXcom
{
namespace VR
{

namespace
{
const int GAME_RESERVED = 5;   // Game::initAudio: channels 0-4 (UI, ambient, unit responses)
int allocated = -1;            // the channel count we set (the game re-initialising audio resets it)
}

int audioChannel(AudioSlot slot)
{
	if (Options::mute) return -1;
	int freq = 0, channels = 0;
	Uint16 format = 0;
	if (!Mix_QuerySpec(&freq, &format, &channels)) return -1;
	int n = Mix_AllocateChannels(-1);
	if (n != allocated)
	{
		// ours go right after the game's reserved channels, reserved too; the free pool keeps its size
		Mix_AllocateChannels(n + AUDIO_SLOTS);
		Mix_ReserveChannels(GAME_RESERVED + AUDIO_SLOTS);
		allocated = Mix_AllocateChannels(-1);
		for (int i = 0; i < AUDIO_SLOTS; ++i) Mix_Volume(GAME_RESERVED + i, MIX_MAX_VOLUME);
		Log(LOG_INFO) << "[VR] audio: channels " << GAME_RESERVED << "-" << (GAME_RESERVED + AUDIO_SLOTS - 1) << " of " << allocated << " for the table's own sounds";
	}
	return GAME_RESERVED + (int)slot;
}

int playAt(AudioSlot slot, Mix_Chunk *chunk, float volume, float angleDeg)
{
	int ch = audioChannel(slot);
	if (ch < 0 || !chunk) return -1;
	Mix_Volume(ch, (int)(Options::soundVolume * std::max(0.f, std::min(1.f, volume))));
	if (Mix_PlayChannel(ch, chunk, 0) < 0) return -1;
	if (Options::StereoSound) moveSound(ch, angleDeg);
	else Mix_SetPosition(ch, 0, 0);
	return ch;
}

bool moveSound(int channel, float angleDeg)
{
	if (channel < 0) return false;
	if (!Mix_Playing(channel)) { Mix_SetPosition(channel, 0, 0); return false; }
	if (Options::StereoSound)
	{
		int a = (int)std::lround(angleDeg) % 360;
		if (a < 0) a += 360;
		Mix_SetPosition(channel, (Sint16)a, 0);
	}
	return true;
}

}
}
