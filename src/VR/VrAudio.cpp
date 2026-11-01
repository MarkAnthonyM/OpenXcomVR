/*
 * OXCE VR tabletop - the prototype's own sounds (see VrAudio.h).
 */
#include "VrAudio.h"
#include "../Engine/Options.h"
#include "../Engine/Logger.h"
#include <SDL_mixer.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

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

int audioFreq()
{
	if (Options::mute) return 0;
	int freq = 0, channels = 0;
	Uint16 format = 0;
	if (!Mix_QuerySpec(&freq, &format, &channels)) return 0;
	return freq;
}

Mix_Chunk *makeChunk(const std::vector<float> &mono)
{
	int freq = 0, channels = 0;
	Uint16 format = 0;
	if (mono.empty() || !Mix_QuerySpec(&freq, &format, &channels) || format != AUDIO_S16SYS || channels < 1) return nullptr;
	Uint8 *buf = (Uint8*)SDL_malloc(mono.size() * channels * 2);
	if (!buf) return nullptr;
	Sint16 *out = (Sint16*)buf;
	for (size_t i = 0; i < mono.size(); ++i)
		for (int c = 0; c < channels; ++c) out[i * channels + c] = (Sint16)(std::max(-1.f, std::min(1.f, mono[i])) * 32000.f);
	return Mix_QuickLoad_RAW(buf, (Uint32)(mono.size() * channels * 2));
}

Mix_Chunk *makeStereoChunk(const std::vector<float> &left, const std::vector<float> &right)
{
	int freq = 0, channels = 0;
	Uint16 format = 0;
	size_t n = std::min(left.size(), right.size());
	if (!n || !Mix_QuerySpec(&freq, &format, &channels) || format != AUDIO_S16SYS || channels < 1) return nullptr;
	Uint8 *buf = (Uint8*)SDL_malloc(n * channels * 2);
	if (!buf) return nullptr;
	Sint16 *out = (Sint16*)buf;
	auto s16 = [](float v) { return (Sint16)(std::max(-1.f, std::min(1.f, v)) * 32000.f); };
	for (size_t i = 0; i < n; ++i)
	{
		if (channels == 1) { out[i] = s16((left[i] + right[i]) * 0.5f); continue; }
		for (int c = 0; c < channels; ++c) out[i * channels + c] = s16(c % 2 ? right[i] : left[i]);
	}
	return Mix_QuickLoad_RAW(buf, (Uint32)(n * channels * 2));
}

int playLoop(AudioSlot slot, Mix_Chunk *chunk)
{
	int ch = audioChannel(slot);
	if (ch < 0 || !chunk) return -1;
	Mix_Volume(ch, 0);
	if (Mix_PlayChannel(ch, chunk, -1) < 0) return -1;
	Mix_SetPosition(ch, 0, 0);
	return ch;
}

void setLevel(int channel, float volume)
{
	if (channel >= 0) Mix_Volume(channel, (int)std::lround(Options::soundVolume * std::max(0.f, std::min(1.f, volume))));
}

void placeLoop(int channel, float angleDeg, float distance, float pan)
{
	if (channel < 0) return;
	if (!Options::StereoSound) { Mix_SetPosition(channel, 0, 0); return; }
	if (pan < 2.f)
	{
		// at most about two thirds as loud in the far ear
		pan = std::max(-1.f, std::min(1.f, pan)) * 0.35f;
		Mix_SetPanning(channel, (Uint8)std::lround(255.f * std::min(1.f, 1.f - pan)), (Uint8)std::lround(255.f * std::min(1.f, 1.f + pan)));
		return;
	}
	int a = (int)std::lround(angleDeg) % 360;
	if (a < 0) a += 360;
	Mix_SetPosition(channel, (Sint16)a, (Uint8)std::lround(255.f * std::max(0.f, std::min(1.f, distance))));
}

bool playing(int channel) { return channel >= 0 && Mix_Playing(channel); }

bool writeWav(const std::string &path, const std::vector<float> &mono, int freq)
{
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) return false;
	auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
	uint32_t bytes = (uint32_t)mono.size() * 2;
	fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32((uint32_t)freq); u32((uint32_t)freq * 2); u16(2); u16(16);
	fwrite("data", 1, 4, f); u32(bytes);
	for (float v : mono) { int16_t x = (int16_t)(std::max(-1.f, std::min(1.f, v)) * 32000.f); fwrite(&x, 2, 1, f); }
	fclose(f);
	return true;
}

void freeChunk(Mix_Chunk *chunk)
{
	if (!chunk) return;
	Uint8 *buf = chunk->abuf;       // Mix_QuickLoad_RAW doesn't own its samples
	Mix_FreeChunk(chunk);           // (halts any channel playing it first)
	SDL_free(buf);
}

void fadeOut(int channel, int ms)
{
	if (channel >= 0 && Mix_Playing(channel)) Mix_FadeOutChannel(channel, ms);
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
