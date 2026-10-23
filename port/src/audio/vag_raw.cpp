#include <algorithm>

#include "audio/vag.hpp"

namespace audio {

void VagDecoder::DecodeBlock(const std::uint8_t *block, std::int16_t *out) {
    int shift = block[0] & 0x0F;
    int filter = std::min(block[0] >> 4, 4);
    // The SPU treats the reserved shifts 13 to 15 like 9.
    if (shift > 12) {
        shift = 9;
    }
    const int f0 = kVagFilter[filter][0];
    const int f1 = kVagFilter[filter][1];
    for (int i = 0; i < kVagBlockSamples; i++) {
        const int byte = block[2 + i / 2];
        const int nibble = (i & 1) ? (byte >> 4) : (byte & 0x0F);
        int       sample = static_cast<std::int16_t>(nibble << 12) >> shift;
        sample += (history[0] * f0 + history[1] * f1 + 32) >> 6;
        sample = std::clamp(sample, -32768, 32767);
        history[1] = history[0];
        history[0] = sample;
        out[i] = static_cast<std::int16_t>(sample);
    }
}

VagSample DecodeVag(std::span<const std::uint8_t> data) {
    VagSample         result;
    VagDecoder        decoder;
    const std::size_t blocks = data.size() / kVagBlockBytes;
#ifdef __PSP__
    std::int16_t scratch[kVagBlockSamples];
    std::size_t  length = 0;
    for (std::size_t b = 0; b < blocks; b++) {
        length = b + 1;
        const std::uint8_t flags = data[b * kVagBlockBytes + 1];
        if (flags & kVagLoopEnd) {
            break;
        }
    }
    result.adpcm.assign(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(length * kVagBlockBytes));
    result.history.reserve(length * 2);
#else
    result.pcm.reserve(blocks * kVagBlockSamples);
#endif
    for (std::size_t b = 0; b < blocks; b++) {
        const std::uint8_t *block = data.data() + b * kVagBlockBytes;
        const std::uint8_t  flags = block[1];
        const std::size_t   start = result.Frames();
        if (flags & kVagLoopStart) {
            result.loop_start = static_cast<int>(start);
        }
#ifdef __PSP__
        result.history.push_back(static_cast<std::int16_t>(decoder.history[0]));
        result.history.push_back(static_cast<std::int16_t>(decoder.history[1]));
        decoder.DecodeBlock(block, scratch);
        result.frames = start + kVagBlockSamples;
#else
        result.pcm.resize(start + kVagBlockSamples);
        decoder.DecodeBlock(block, result.pcm.data() + start);
#endif
        if (flags & kVagLoopEnd) {
            result.loops = (flags & kVagLoopRepeat) != 0;
            break;
        }
    }
    if (result.loop_start >= static_cast<int>(result.Frames())) {
        result.loops = false;
    }
    return result;
}

} // namespace audio
