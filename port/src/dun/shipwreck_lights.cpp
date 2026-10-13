#include "dun/shipwreck_lights.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "gfx/shipwreck_fog.hpp"

namespace {

struct Candidate {
    gfx::detail::ShipwreckLight light;
    float distance_squared;
};

std::array<Candidate, gfx::detail::kMaxShipwreckLights> nearest;
float eye[3];
uint32_t count = 0;
bool collecting = false;
bool reported = false;

} // namespace

void BeginShipwreckFireLights(const float player_position[4]) {
    collecting = true;
    count = 0;
    std::copy_n(player_position, 3, eye);
}

void RecordShipwreckFireLight(const float position[4], float flicker_width) {
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
    candidate.light.position_radius[3] = 45.0f;
    candidate.light.color_intensity[0] = 1.0f;
    candidate.light.color_intensity[1] = 0.42f;
    candidate.light.color_intensity[2] = 0.13f;
    candidate.light.color_intensity[3] = 0.8f * std::clamp(flicker_width, 0.85f, 1.15f);

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

void EndShipwreckFireLights() {
    collecting = false;
    std::array<gfx::detail::ShipwreckLight, gfx::detail::kMaxShipwreckLights> lights = {};
    for (uint32_t i = 0; i < count; ++i) lights[i] = nearest[i].light;
    gfx::detail::ShipwreckFogSetLights(std::span<const gfx::detail::ShipwreckLight>(lights.data(), count));
    if (!reported && count != 0) {
        std::fprintf(stderr, "Shipwreck: captured %u world-space fire lights from flame draws\n", count);
        reported = true;
    }
}
