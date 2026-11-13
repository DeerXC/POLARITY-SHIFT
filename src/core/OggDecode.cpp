#include "core/OggDecode.h"

#ifdef GAME_AUDIO_OGG
#include <cstdlib>

// stb_vorbis.c is its own translation unit, compiled as C (see GE_06.vcxproj). The one function that is used here:
// decodes a whole file; returns the number of frames (-1 on an error); *output is malloc'ed and must be free()d.
extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

namespace OggDecode
{
bool Decode(const std::vector<std::uint8_t>& file, int& channels, int& sampleRate, std::vector<std::int16_t>& samples)
{
    if (file.empty() || file.size() > 0x7FFFFFFFu) return false;
    int decodedChannels = 0;
    int decodedRate = 0;
    short* decoded = nullptr;
    const int frames = stb_vorbis_decode_memory(file.data(), static_cast<int>(file.size()), &decodedChannels, &decodedRate, &decoded);
    if (frames <= 0 || decoded == nullptr || decodedChannels < 1 || decodedChannels > 2)
    {
        std::free(decoded);
        return false;
    }
    samples.assign(decoded, decoded + static_cast<std::size_t>(frames) * static_cast<std::size_t>(decodedChannels));
    std::free(decoded);
    channels = decodedChannels;
    sampleRate = decodedRate;
    return true;
}
} // namespace OggDecode
#endif
