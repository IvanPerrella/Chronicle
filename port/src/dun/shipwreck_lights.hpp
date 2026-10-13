#pragma once

// Capture the original fire draw positions and its per-fire flicker after the
// flame sprite has updated them. Active only around Shipwreck's fire draw.
void BeginShipwreckFireLights(const float player_position[4]);
void RecordShipwreckFireLight(const float position[4], float flicker_width);
void EndShipwreckFireLights();
