#pragma once

enum class DungeonLightScene { Shipwreck, Gallery };

void BeginDungeonFireLights(const float player_position[4], DungeonLightScene scene);
void RecordDungeonFireLight(const float position[4], float flicker_width, float scale);
void EndDungeonFireLights();
