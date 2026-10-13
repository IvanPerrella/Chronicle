#include "gfx/materialmaps.hpp"

#include "platform/imagefile.hpp"
#include "platform/paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gfx {
namespace {

struct Maps {
    TextureHandle normal = kNullTexture;
    TextureHandle orm = kNullTexture;
};

constexpr std::array<const char *, static_cast<size_t>(MaterialProfile::Count)> kNames = {
    "wood_floor", "wood_wall", "cloth", "leather", "skin", "metal", "stone", "prop", "flat",
};
std::array<Maps, kNames.size()> g_maps{};
uint32_t g_loaded_count = 0;
std::unordered_map<std::string, MaterialProfile> g_texture_assignments;
std::vector<std::pair<std::string, MaterialProfile>> g_prefix_assignments;

std::vector<uint8_t> Downsample(const std::vector<uint8_t> &source, int width, int height,
                                bool normal_map, uint32_t level) {
    const int next_width = std::max(1, width / 2);
    const int next_height = std::max(1, height / 2);
    std::vector<uint8_t> result(static_cast<size_t>(next_width) * next_height * 4);
    for (int y = 0; y < next_height; ++y) {
        for (int x = 0; x < next_width; ++x) {
            float channels[4]{};
            for (int oy = 0; oy < 2; ++oy) {
                for (int ox = 0; ox < 2; ++ox) {
                    const int sx = std::min(width - 1, x * 2 + ox);
                    const int sy = std::min(height - 1, y * 2 + oy);
                    const size_t pixel = (static_cast<size_t>(sy) * width + sx) * 4;
                    for (int c = 0; c < 4; ++c) channels[c] += source[pixel + c] * 0.25f;
                }
            }
            const size_t dst = (static_cast<size_t>(y) * next_width + x) * 4;
            if (normal_map) {
                float nx = channels[0] * (2.0f / 255.0f) - 1.0f;
                float ny = channels[1] * (2.0f / 255.0f) - 1.0f;
                float nz = channels[2] * (2.0f / 255.0f) - 1.0f;
                const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (length > 0.001f) { nx /= length; ny /= length; nz /= length; }
                result[dst + 0] = static_cast<uint8_t>(std::clamp((nx * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                result[dst + 1] = static_cast<uint8_t>(std::clamp((ny * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
                result[dst + 2] = static_cast<uint8_t>(std::clamp((nz * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
            } else {
                result[dst + 0] = static_cast<uint8_t>(channels[0]);
                // Broaden fine microfacets at distance instead of leaving shimmering glints.
                result[dst + 1] = static_cast<uint8_t>(std::clamp(channels[1] + 1.5f * level, 0.0f, 255.0f));
                result[dst + 2] = static_cast<uint8_t>(channels[2]);
            }
            result[dst + 3] = 255;
        }
    }
    return result;
}

TextureHandle LoadOne(const std::filesystem::path &dir, const nlohmann::json &record, const char *field) {
    if (!record.contains(field) || !record[field].is_string()) return kNullTexture;
    const std::string name = record[field].get<std::string>();
    const std::filesystem::path leaf = PathsFromUtf8(name);
    if (leaf.filename() != leaf || leaf.extension() != ".png") return kNullTexture;
    const auto file = dir / leaf;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    if (!imagefile::LoadPng(file, width, height, rgba) || width <= 0 || height <= 0 ||
        rgba.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
        std::fprintf(stderr, "material maps: cannot decode %s\n", PathsDisplay(file).c_str());
        return kNullTexture;
    }
    const uint32_t levels = std::bit_width(static_cast<uint32_t>(std::max(width, height)));
    TextureHandle texture = CreateTexture({static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                           TextureFormat::Rgba8, levels, false});
    if (texture == kNullTexture) return kNullTexture;
    const bool normal_map = std::string_view(field) == "normal";
    int mip_width = width;
    int mip_height = height;
    for (uint32_t level = 0; level < levels; ++level) {
        if (!UpdateTextureNow(texture, level, 0, 0, static_cast<uint32_t>(mip_width),
                              static_cast<uint32_t>(mip_height), rgba.data())) {
            DestroyTexture(texture);
            return kNullTexture;
        }
        if (level + 1 < levels) {
            rgba = Downsample(rgba, mip_width, mip_height, normal_map, level + 1);
            mip_width = std::max(1, mip_width / 2);
            mip_height = std::max(1, mip_height / 2);
        }
    }
    ++g_loaded_count;
    return texture;
}

} // namespace

bool LoadMaterialMaps(const std::filesystem::path &executable) {
    UnloadMaterialMaps();
    const auto dir = executable.parent_path() / "assets" / "materials";
    const auto file = dir / "manifest.json";
    std::ifstream input(file);
    if (!input) {
        std::fprintf(stderr, "material maps: missing %s; using neutral detail\n", PathsDisplay(file).c_str());
        return false;
    }
    try {
        const nlohmann::json manifest = nlohmann::json::parse(input);
        if (manifest.at("version").get<int>() != 1 || !manifest.at("profiles").is_object()) {
            std::fprintf(stderr, "material maps: unsupported manifest %s\n", PathsDisplay(file).c_str());
            return false;
        }
        const auto &profiles = manifest.at("profiles");
        for (size_t i = 0; i < kNames.size(); ++i) {
            if (!profiles.contains(kNames[i]) || !profiles.at(kNames[i]).is_object()) continue;
            const auto &record = profiles.at(kNames[i]);
            g_maps[i].normal = LoadOne(dir, record, "normal");
            g_maps[i].orm = LoadOne(dir, record, "orm");
        }
        auto read_assignments = [&](const char *key, bool prefix) {
            if (!manifest.contains(key) || !manifest.at(key).is_object()) return;
            const auto &groups = manifest.at(key);
            for (size_t i = 0; i < kNames.size(); ++i) {
                if (!groups.contains(kNames[i]) || !groups.at(kNames[i]).is_array()) continue;
                for (const auto &entry : groups.at(kNames[i])) {
                    if (!entry.is_string()) continue;
                    const std::string name = entry.get<std::string>();
                    if (name.empty() || name.size() > 32) continue;
                    if (prefix) g_prefix_assignments.emplace_back(name, static_cast<MaterialProfile>(i));
                    else g_texture_assignments.emplace(name, static_cast<MaterialProfile>(i));
                }
            }
        };
        read_assignments("texture_assignments", false);
        read_assignments("texture_prefix_assignments", true);
    } catch (const nlohmann::json::exception &error) {
        std::fprintf(stderr, "material maps: invalid manifest %s: %s\n", PathsDisplay(file).c_str(), error.what());
        UnloadMaterialMaps();
        return false;
    }
    std::fprintf(stderr, "material maps: loaded %u/%zu textures from %s\n", g_loaded_count,
                 kNames.size() * 2, PathsDisplay(dir).c_str());
    return g_loaded_count == kNames.size() * 2;
}

void UnloadMaterialMaps() {
    for (Maps &maps : g_maps) {
        if (maps.normal != kNullTexture) DestroyTexture(maps.normal);
        if (maps.orm != kNullTexture) DestroyTexture(maps.orm);
        maps = {};
    }
    g_loaded_count = 0;
    g_texture_assignments.clear();
    g_prefix_assignments.clear();
}

TextureHandle MaterialNormalMap(MaterialProfile profile) {
    const size_t index = static_cast<size_t>(profile);
    return index < g_maps.size() ? g_maps[index].normal : kNullTexture;
}

TextureHandle MaterialOrmMap(MaterialProfile profile) {
    const size_t index = static_cast<size_t>(profile);
    return index < g_maps.size() ? g_maps[index].orm : kNullTexture;
}

uint32_t MaterialMapCount() { return g_loaded_count; }

MaterialProfile MaterialProfileForTexture(std::string_view texture_name) {
    if (const auto found = g_texture_assignments.find(std::string(texture_name));
        found != g_texture_assignments.end()) return found->second;
    for (const auto &[prefix, profile] : g_prefix_assignments) {
        if (texture_name.starts_with(prefix)) return profile;
    }
    return MaterialProfile::Prop;
}

} // namespace gfx
