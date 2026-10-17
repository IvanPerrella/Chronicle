#include "dun/shipwreck_lights.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "gfx/gfx.hpp"

namespace {

struct Candidate {
    gfx::LocalLight light;
    float distance_squared;
};

std::array<Candidate, 8> nearest;
float eye[3];
uint32_t count = 0;
bool collecting = false;
bool reported[2] = {};
DungeonLightScene scene = DungeonLightScene::Shipwreck;

} // namespace

void BeginDungeonFireLights(const float player_position[4], DungeonLightScene next_scene) {
    collecting = true;
    scene = next_scene;
    count = 0;
    std::copy_n(player_position, 3, eye);
}

void RecordDungeonFireLight(const float position[4], float flicker_width, float scale) {
    if (!collecting) return;
    Candidate candidate = {};
    float dx = position[0] - eye[0];
    float dy = position[1] - eye[1];
    float dz = position[2] - eye[2];
    candidate.distance_squared = dx * dx + dy * dy + dz * dz;
    std::copy_n(position, 3, candidate.light.position_radius);
    // Confine enhanced illumination to the lantern's nearby beams and boards.
    // The shader applies squared edge falloff and inverse-square-like distance
    // attenuation within this radius.
    candidate.light.position_radius[1] += 4.6f;
    const bool gallery = scene == DungeonLightScene::Gallery;
    candidate.light.position_radius[3] = (gallery ? 72.0f : 45.0f) * std::clamp(scale, 0.5f, 1.5f);
    candidate.light.color_intensity[0] = gallery ? 0.48f : 1.0f;
    candidate.light.color_intensity[1] = gallery ? 0.18f : 0.42f;
    candidate.light.color_intensity[2] = gallery ? 1.0f : 0.13f;
    candidate.light.color_intensity[3] = (gallery ? 1.2f : 0.8f) *
                                         std::clamp(flicker_width, 0.85f, 1.15f) *
                                         std::clamp(scale, 0.5f, 1.5f);

    if (count < nearest.size()) {
        nearest[count++] = candidate;
        return;
    }
    uint32_t farthest = 0;
    for (uint32_t i = 1; i < count; ++i) {
        if (nearest[i].distance_squared > nearest[farthest].distance_squared) farthest = i;
    }
    if (candidate.distance_squared < nearest[farthest].distance_squared) nearest[farthest] = candidate;
}

void EndDungeonFireLights() {
    collecting = false;
    std::array<gfx::LocalLight, 8> lights = {};
    for (uint32_t i = 0; i < count; ++i) lights[i] = nearest[i].light;
    const int index = scene == DungeonLightScene::Gallery ? 1 : 0;
    gfx::SetLocalLights(std::span<const gfx::LocalLight>(lights.data(), count));
    if (!reported[index] && count != 0) {
        std::fprintf(stderr, "%s: captured %u world-space fire lights from flame draws\n",
                     index ? "Gallery" : "Shipwreck", count);
        reported[index] = true;
    }
}
