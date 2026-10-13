#pragma once

#include "gfx/gfx.hpp"

#include <filesystem>
#include <cstdint>
#include <string_view>

namespace gfx {

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
    Count,
};

// Maps are loaded from <executable>/assets/materials, independent of cwd.
// A null handle means no map is available; shaders retain their neutral fallback.
bool LoadMaterialMaps(const std::filesystem::path &executable);
void UnloadMaterialMaps();
TextureHandle MaterialNormalMap(MaterialProfile profile);
TextureHandle MaterialOrmMap(MaterialProfile profile);
uint32_t MaterialMapCount();
MaterialProfile MaterialProfileForTexture(std::string_view texture_name);

} // namespace gfx
