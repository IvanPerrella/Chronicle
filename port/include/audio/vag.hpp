#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace audio {

constexpr int kVagBlockBytes = 16;
constexpr int kVagBlockSamples = 28;

enum VagFlag : std::uint8_t {
    kVagLoopEnd = 0x01,
    kVagLoopRepeat = 0x02,
    kVagLoopStart = 0x04,
};

// The SPU's ADPCM prediction filters, in 1/64 units.
constexpr int kVagFilter[5][2] = {
    {0,   0  },
    {60,  0  },
    {115, -52},
    {98,  -55},
    {122, -60},
};

struct VagDecoder {
    int history[2] = {0, 0};

    void DecodeBlock(const std::uint8_t *block, std::int16_t *out);
};

struct VagSample {
#ifdef __PSP__
    // Kept as the SPU keeps it, in 16-byte ADPCM blocks of 28 samples, with the decoder's history
    // going into each block so that any one decodes alone, to what decoding from the start gives
    // (the synth decodes as it plays). Decoded PCM takes 2.8 times the memory: 15 MB for the
    // opening's sound set alone.
    std::vector<std::uint8_t> adpcm;
    std::vector<std::int16_t> history; // two a block
    std::size_t               frames = 0;
#else
    std::vector<std::int16_t> pcm;
#endif
    int  loop_start = 0;
    bool loops = false;

#ifdef __PSP__
    std::size_t Frames() const { return frames; }

    // Block `block` (frame / kVagBlockSamples) into out[kVagBlockSamples].
    void DecodeBlock(std::size_t block, std::int16_t *out) const {
        VagDecoder decoder{{history[block * 2], history[block * 2 + 1]}};
        decoder.DecodeBlock(adpcm.data() + block * kVagBlockBytes, out);
    }
#else
    std::size_t Frames() const { return pcm.size(); }
#endif
};

// Decodes from the start of a VAG body up to and including the first block with the loop-end flag,
// or to the end of the span. A sample whose end block also repeats loops back to the last block
// marked loop-start, or to its first sample when none is marked, as the SPU does.
VagSample DecodeVag(std::span<const std::uint8_t> data);

} // namespace audio
