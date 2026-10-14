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
#include <iomanip>
#include <sstream>
#include <unordered_set>
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

struct Variant {
    MaterialProfile profile = MaterialProfile::Flat;
    Maps maps;
    bool excluded = false;
};

struct Record : Variant {
    std::unordered_map<uint64_t, Variant> variants;
};

constexpr std::array<const char *, static_cast<size_t>(MaterialProfile::Count)> kNames = {
    "wood_floor", "wood_wall", "cloth", "leather", "skin", "metal", "stone", "prop", "flat",
    "grass", "moss", "effect",
};
std::array<Maps, kNames.size()> g_maps{};
uint32_t g_loaded_count = 0;
std::unordered_map<std::string, Record> g_records;
std::unordered_set<std::string> g_unresolved;
std::unordered_set<const void *> g_untextured_surfaces;
std::unordered_map<std::string, TextureHandle> g_loaded_files;
std::unordered_map<uint32_t, uint64_t> g_texture_fingerprints;

std::string Normalize(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    for (char c : name) {
        if (c == 0) break;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        result.push_back(c);
    }
    return result;
}

bool ParseFingerprint(std::string_view text, uint64_t &fingerprint) {
    if (text.size() != 16) return false;
    uint64_t value = 0;
    for (char c : text) {
        unsigned digit = c >= '0' && c <= '9' ? c - '0' :
                         c >= 'a' && c <= 'f' ? c - 'a' + 10 : 16;
        if (digit > 15) return false;
        value = (value << 4) | digit;
    }
    fingerprint = value;
    return true;
}

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
            result[dst + 3] = static_cast<uint8_t>(std::clamp(channels[3], 0.0f, 255.0f));
        }
    }
    return result;
}

TextureHandle LoadOne(const std::filesystem::path &dir, const nlohmann::json &record, const char *field) {
    if (!record.contains(field) || !record[field].is_string()) return kNullTexture;
    const std::string name = record[field].get<std::string>();
    if (const auto found = g_loaded_files.find(name); found != g_loaded_files.end()) return found->second;
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
    g_loaded_files.emplace(name, texture);
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
        if (manifest.at("version").get<int>() != 2 || !manifest.at("profiles").is_object() ||
            !manifest.at("texture_records").is_object()) {
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
        for (auto it = manifest.at("texture_records").begin();
             it != manifest.at("texture_records").end(); ++it) {
            if (!it.value().is_object() || !it.value().contains("profile") ||
                !it.value().at("profile").is_string()) continue;
            const std::string name = Normalize(it.key());
            if (name.empty() || name.size() > 32 || name != it.key()) continue;
            const std::string profile_name = it.value().at("profile").get<std::string>();
            const auto profile_it = std::find_if(kNames.begin(), kNames.end(),
                                                 [&](const char *candidate) { return profile_name == candidate; });
            if (profile_it == kNames.end()) {
                std::fprintf(stderr, "material maps: unknown profile %s for %s\n", profile_name.c_str(), name.c_str());
                continue;
            }
            Record record;
            record.profile = static_cast<MaterialProfile>(profile_it - kNames.begin());
            if (!it.value().contains("classification") || !it.value().at("classification").is_string()) {
                std::fprintf(stderr, "material maps: missing classification for %s\n", name.c_str());
                continue;
            }
            const std::string classification = it.value().at("classification").get<std::string>();
            if (classification != "authored" && classification != "excluded") continue;
            record.excluded = classification == "excluded";
            record.maps.normal = LoadOne(dir, it.value(), "normal");
            record.maps.orm = LoadOne(dir, it.value(), "orm");
            bool variants_valid = true;
            if (it.value().contains("variants")) {
                if (!it.value().at("variants").is_object() || it.value().at("variants").empty()) {
                    variants_valid = false;
                } else {
                    for (auto variant = it.value().at("variants").begin();
                         variant != it.value().at("variants").end(); ++variant) {
                        uint64_t fingerprint = 0;
                        if (!ParseFingerprint(variant.key(), fingerprint) || !variant.value().is_object()) {
                            variants_valid = false;
                            break;
                        }
                        Variant value;
                        value.profile = record.profile;
                        value.excluded = record.excluded;
                        if (variant.value().contains("profile")) {
                            if (!variant.value().at("profile").is_string()) {
                                variants_valid = false;
                                break;
                            }
                            const std::string variant_profile = variant.value().at("profile").get<std::string>();
                            const auto found_profile = std::find_if(kNames.begin(), kNames.end(),
                                [&](const char *candidate) { return variant_profile == candidate; });
                            if (found_profile == kNames.end()) {
                                variants_valid = false;
                                break;
                            }
                            value.profile = static_cast<MaterialProfile>(found_profile - kNames.begin());
                        }
                        if (variant.value().contains("classification")) {
                            if (!variant.value().at("classification").is_string()) {
                                variants_valid = false;
                                break;
                            }
                            const std::string variant_class = variant.value().at("classification").get<std::string>();
                            if (variant_class != "authored" && variant_class != "excluded") {
                                variants_valid = false;
                                break;
                            }
                            value.excluded = variant_class == "excluded";
                        }
                        value.maps = {LoadOne(dir, variant.value(), "normal"),
                                      LoadOne(dir, variant.value(), "orm")};
                        if (value.maps.normal == kNullTexture || value.maps.orm == kNullTexture) {
                            variants_valid = false;
                            break;
                        }
                        record.variants.emplace(fingerprint, value);
                    }
                }
            }
            if (!variants_valid) {
                std::fprintf(stderr, "material maps: invalid variants for %s\n", name.c_str());
                continue;
            }
            if (record.maps.normal == kNullTexture || record.maps.orm == kNullTexture) {
                std::fprintf(stderr, "material maps: incomplete record %s\n", name.c_str());
                continue;
            }
            g_records.emplace(name, record);
        }
    } catch (const nlohmann::json::exception &error) {
        std::fprintf(stderr, "material maps: invalid manifest %s: %s\n", PathsDisplay(file).c_str(), error.what());
        UnloadMaterialMaps();
        return false;
    }
    std::fprintf(stderr, "material maps: loaded %u images and %zu exact records from %s\n", g_loaded_count,
                 g_records.size(), PathsDisplay(dir).c_str());
    return !g_records.empty();
}

void UnloadMaterialMaps() {
    for (const auto &[name, texture] : g_loaded_files) DestroyTexture(texture);
    g_loaded_files.clear();
    g_maps.fill({});
    g_loaded_count = 0;
    g_records.clear();
    g_unresolved.clear();
    g_untextured_surfaces.clear();
    g_texture_fingerprints.clear();
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

MaterialBinding ResolveMaterial(std::string_view texture_name, uint32_t texture_key) {
    const std::string name = Normalize(texture_name);
    if (const auto found = g_records.find(name); found != g_records.end()) {
        const Record &record = found->second;
        const Variant *binding = &record;
        if (!record.variants.empty()) {
            const auto fingerprint = g_texture_fingerprints.find(texture_key);
            const auto variant = fingerprint == g_texture_fingerprints.end() ? record.variants.end() :
                                 record.variants.find(fingerprint->second);
            if (variant != record.variants.end()) binding = &variant->second;
            else {
                std::ostringstream identifier;
                identifier << name << '#' << std::hex << std::setw(16) << std::setfill('0') <<
                              (fingerprint == g_texture_fingerprints.end() ? 0 : fingerprint->second);
                if (g_unresolved.insert(identifier.str()).second)
                    std::fprintf(stderr, "material maps: unresolved variant %s\n", identifier.str().c_str());
                return {};
            }
        }
        return {binding->profile, binding->maps.normal, binding->maps.orm, true, binding->excluded};
    }
    if (!name.empty() && g_unresolved.insert(name).second) {
        std::fprintf(stderr, "material maps: unresolved texture %s\n", name.c_str());
    }
    return {};
}

uint32_t UnresolvedMaterialCount() { return static_cast<uint32_t>(g_unresolved.size()); }

MaterialBinding ResolveUntexturedMaterial(const void *surface) {
    if (surface != nullptr && g_untextured_surfaces.insert(surface).second) {
        const size_t count = g_untextured_surfaces.size();
        if ((count & (count - 1)) == 0)
            std::fprintf(stderr, "material maps: %zu untextured strips assigned authored neutral prop detail\n", count);
    }
    const Maps &maps = g_maps[static_cast<size_t>(MaterialProfile::Prop)];
    return {MaterialProfile::Prop, maps.normal, maps.orm, true, false};
}

uint32_t UntexturedMaterialCount() { return static_cast<uint32_t>(g_untextured_surfaces.size()); }

void RegisterMaterialTextureFingerprint(uint32_t texture_key, uint64_t fingerprint) {
    if (texture_key != 0) g_texture_fingerprints[texture_key] = fingerprint;
}

} // namespace gfx
