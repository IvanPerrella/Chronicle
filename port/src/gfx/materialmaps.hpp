#pragma once

#include "gfx/gfx.hpp"

#include <filesystem>
#include <cstdint>
#include <string_view>

namespace gfx {

/**
 * Semantic surface class used by the native material shaders.
 */
enum class MaterialProfile : uint8_t {
    WoodFloor,
    WoodWall,
    Cloth,
    Leather,
    Skin,
    Metal,
    Stone,
    Prop,
    Flat,
    Grass,
    Moss,
    Effect,
    Count,
};

/**
 * Exact texture-record lookup result, with authored normal and ORM maps.
 * Unresolved records have resolved=false and must remain visible in the census.
 */
struct MaterialBinding {
    MaterialProfile profile = MaterialProfile::Flat;
    TextureHandle normal = kNullTexture;
    TextureHandle orm = kNullTexture;
    bool resolved = false;
    bool excluded = false;
};

/**
 * Loads generated materials next to the executable, independent of cwd.
 * Normal alpha is physical response coverage; ORM alpha is wet coverage.
 */
bool LoadMaterialMaps(const std::filesystem::path &executable);
/**
 * Destroys loaded material textures and clears the runtime census.
 */
void UnloadMaterialMaps();
/**
 * Returns the default normal map for a semantic class.
 */
TextureHandle MaterialNormalMap(MaterialProfile profile);
/**
 * Returns the default ambient-occlusion, roughness and metalness map.
 */
TextureHandle MaterialOrmMap(MaterialProfile profile);
/**
 * Returns the number of unique material images loaded from the manifest.
 */
uint32_t MaterialMapCount();
/**
 * Resolves a retail texture by exact name and optional live-content fingerprint.
 * Colliding names require a matching variant, including the main-dungeon image.
 */
MaterialBinding ResolveMaterial(std::string_view texture_name, uint32_t texture_key = 0);
/**
 * Assigns an explicit neutral procedural material to an untextured lit strip.
 * Surface identity is retained for the runtime coverage count.
 */
MaterialBinding ResolveUntexturedMaterial(const void *surface);
/**
 * Associates a live registry key with its post-decode RGBA FNV-1a fingerprint.
 * EnterTexture overwrites the association whenever the registry key is reused.
 */
void RegisterMaterialTextureFingerprint(uint32_t texture_key, uint64_t fingerprint);
/**
 * Returns the count of unresolved exact names and texture variants seen in play.
 */
uint32_t UnresolvedMaterialCount();
/**
 * Returns the count of distinct lit strips assigned an untextured material.
 */
uint32_t UntexturedMaterialCount();

} // namespace gfx
