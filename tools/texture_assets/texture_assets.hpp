#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace texture_assets {

using Bytes = std::vector<uint8_t>;
using Resolver = std::function<std::filesystem::path(const std::filesystem::path &)>;

/**
 * Replaces disc texture payloads with lossless PNG images and JSON layout descriptors.
 * Converts normalized data too and publishes the catalog only after verification.
 */
void Convert(const std::filesystem::path &data_root, const std::function<bool()> &proceed = {});

/**
 * Reconstructs one game-visible file from its JSON descriptor and PNGs.
 * The resolver receives paths relative to data_root, for ordered datapack overrides.
 */
Bytes Read(const std::filesystem::path &data_root, const std::filesystem::path &descriptor,
           const Resolver &resolve = {});

} // namespace texture_assets
