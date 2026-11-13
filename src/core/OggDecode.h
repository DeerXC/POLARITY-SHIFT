#pragma once
#include <cstdint>
#include <vector>
// Decodes an Ogg Vorbis file (all of it, in memory) into 16 bit PCM, interleaved: the music of the game is shipped as .ogg (about a seventh of the size of the .wav) and decoded once
// at the start of the game into the same PCM that a .wav gives. The decoder is stb_vorbis (public domain, src/third_party/stb_vorbis.c).
// It is OFF until you switch it on: tools/audio/enable_ogg.py adds OggDecode.cpp and stb_vorbis.c to GE_06.vcxproj and defines GAME_AUDIO_OGG. Without it Decode() always says "no" and
// the game plays the .wav files that are next to the .ogg (tools/audio/ogg_to_wav.py makes them).
namespace OggDecode
{
#ifdef GAME_AUDIO_OGG
bool Decode(const std::vector<std::uint8_t>& file, int& channels, int& sampleRate, std::vector<std::int16_t>& samples);
#else
inline bool Decode(const std::vector<std::uint8_t>&, int&, int&, std::vector<std::int16_t>&)
{
    return false;
}
#endif
} // namespace OggDecode
