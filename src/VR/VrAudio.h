#pragma once
/*
 * OXCE VR tabletop - the prototype's own sounds (thunder, the motion scanner).
 *
 * They play on mixer channels of their own, apart from the game's: the game uses
 * 16 channels with 0-4 reserved for itself and the rest picked at random, so a
 * position set on a shared channel (Mix_SetPosition) could stick to the game's
 * next sound there. A few channels are added after the game's reserved ones and
 * reserved as well (the game keeps as many free channels as before).
 */
#include <string>
#include <vector>

struct Mix_Chunk;

namespace OpenXcom
{
namespace VR
{

enum AudioSlot { AUDIO_THUNDER_A, AUDIO_THUNDER_B, AUDIO_SCANNER_PING, AUDIO_SCANNER_BLIP, AUDIO_SCANNER_UI, AUDIO_SLOTS };

/// The mixer channel of a slot (-1: no sound).
int audioChannel(AudioSlot slot);
/// Plays a sound on a slot's channel at a volume (0..1 of the game's sound volume), coming from a
/// direction: degrees clockwise from straight ahead (only with the game's stereo sound on). Returns the
/// channel, or -1.
int playAt(AudioSlot slot, Mix_Chunk *chunk, float volume, float angleDeg);
/// The mixer's sample rate (0: no sound).
int audioFreq();
/// A mono sound (-1..1 at audioFreq) as a mixer chunk in the mixer's format; kept for good (null: no sound).
Mix_Chunk *makeChunk(const std::vector<float> &mono);
/// Tests: a mono sound as a 16-bit WAV file.
bool writeWav(const std::string &path, const std::vector<float> &mono, int freq);
/// Keeps a playing sound in its direction while the head turns; false (and the position cleared) once it
/// has finished.
bool moveSound(int channel, float angleDeg);

}
}
