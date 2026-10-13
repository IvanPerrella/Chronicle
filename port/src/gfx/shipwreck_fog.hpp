#pragma once

#include "context.hpp"
#include <span>

namespace gfx::detail {

inline constexpr uint32_t kMaxShipwreckLights = 8;

struct ShipwreckLight {
    float position_radius[4]; // world xyz, influence radius
    float color_intensity[4]; // linear radiance RGB, original-fire flicker multiplier
};

struct ShipwreckLightBlock {
    uint32_t count[4] = {};
    float position_radius[kMaxShipwreckLights][4] = {};
    float color_intensity[kMaxShipwreckLights][4] = {};
};

// Depth sampling is optional: the base renderer still accepts devices whose selected
// depth/stencil format cannot be sampled.
bool ShipwreckFogDepthSupported();
void ShipwreckFogInit();
void ShipwreckFogResize();
void ShipwreckFogDestroyTarget();
void ShipwreckFogShutdown();

// Called on replayed world draws, after interpolation has rebuilt MeshConstants.
void ShipwreckFogTrackCamera(const MeshConstants &constants);
// Shipwreck's room and corridor floor elevations are static in world space.
// Pass one elevation per 160-unit map cell so the lower medium does not follow
// the player and shift globally when crossing a doorway.
void ShipwreckFogSetDeckField(std::span<const float, 400> cell_heights);
void ShipwreckFogSetLights(std::span<const ShipwreckLight> lights);
const ShipwreckLightBlock &ShipwreckFogLights();
void QueueShipwreckFog(bool enabled);
void QueueShipwreckLocalLights(bool enabled);
void ApplyPendingShipwreckOptions();
bool ShipwreckFogEnabled();
bool ShipwreckLocalLightsEnabled();

// Returns source unchanged when the display frame is not a Shipwreck scene.
// Call once after the world draw and before bloom/tone mapping and HUD draws.
Image &ShipwreckFogApply(VkCommandBuffer cmd, Image &source, Image &depth);

} // namespace gfx::detail
