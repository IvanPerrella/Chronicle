#pragma once

#include <filesystem>
#include <optional>

namespace datapacks {

/**
 * Loads sibling datapacks/datapacks.json; later load_order folders take precedence.
 */
void Load(const std::filesystem::path &data_root);

/**
 * Clears configured packs and their case-insensitive file indexes.
 */
void Clear();

/**
 * Finds a replacement for a relative asset path within the configured pack roots.
 */
std::optional<std::filesystem::path> Resolve(const std::filesystem::path &relative);

} // namespace datapacks
