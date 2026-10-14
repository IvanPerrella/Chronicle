#include "fireomni.hpp"

#include <libgraph.h>

#include <cstdlib>

#include "camera.hpp"
#include "draw3d.hpp"
#include "dun/shipwreck_lights.hpp"
#include "draw2d_port.hpp"
#include "dungeon.h"
#include "main.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "texture.hpp"

namespace {

// Retail's raw TEX1/TEST/ZBUF/ALPHA writes ahead of each layer: no alpha test, depth tested but not
// written, additive Cs * As + Cd. The sprites pick TEX1 themselves.
void SetLayerRegisters() {
    const draw2d::Services &services = draw2d::Get();
    sceGsTest               test = mgPixelTest;
    test.bits.ate = 0;
    test.bits.zte = 1;
    services.set_test(&test);
    sceGsZbuf zbuffer = mgZBuffer;
    zbuffer.bits.zmsk = 1;
    services.set_zbuf(&zbuffer);
    sceGsAlpha alpha = mgAlpha;
    alpha.bits.a = 0;
    alpha.bits.b = 2;
    alpha.bits.c = 0;
    alpha.bits.d = 1;
    services.set_alpha(&alpha);
}

} // namespace

PORT_OVERRIDE void CFireOmni::DrawFire(int unused0, int unused1, CCamera *camera, float *colour, float scale, int layers,
                                     float camera_offset) {
    sceVu0FVECTOR camera_direction;
    sceVu0FVECTOR camera_ref;
    int           near_top_left[4];
    int           near_bottom_right[4];
    sceVu0FVECTOR world;
    int           top_left[4];
    int           bottom_right[4];
    int           top_right[4];
    int           bottom_left[4];

    if (texture_set == 0) {
        if (core == NULL) {
            core = TexManager.GetTexture(const_cast<char *>("lightling"), -1);
        }

        if (glow == NULL) {
            glow = TexManager.GetTexture(const_cast<char *>("blender"), -1);
        }
    }

    camera->GetPos(camera_direction);
    camera->GetRef(camera_ref);
    sceVu0SubVector(camera_direction, camera_direction, camera_ref);
    sceVu0Normalize(camera_direction, camera_direction);
    sceVu0ScaleVector(camera_direction, camera_direction, camera_offset);
    spRGBA white = {0x80, 0x80, 0x80, 0x80};
    const bool compact_dungeon_fire = EnhancedLightingEnabled() &&
        (MapNo == 200 + DUNGEON_SHIPWRECK || MapNo == 200 + DUNGEON_GALLERY_OF_TIME);
    const bool gallery_fire = EnhancedLightingEnabled() && MapNo == 200 + DUNGEON_GALLERY_OF_TIME;
    if (gallery_fire) white = {0xC0, 0xB0, 0xC0, 0xC0};

    if (layers & 1) {
        SetLayerRegisters();

        world[0] = pos[0];
        world[1] = pos[1] + 4.6f;
        world[2] = pos[2];
        world[3] = 1.0f;

        if (MGRotTransPers3DSprite(top_left, bottom_right, world,
                                   (gallery_fire ? 6.0f : 18.0f) * scale,
                                   (gallery_fire ? 8.0f : 9.0f) * scale, 1) == 1) {
            set3DSpriteFog(Vif1Packet, core, CRect_i_(0, 0, 64, 64), top_left, bottom_right, &white);
            // The broad additive GS halo washes over the room once the enhanced
            // world lights and volume fog are active. Keep the flame core.
            if (!compact_dungeon_fire || gallery_fire)
                set3DSpriteFog(Vif1Packet, glow, CRect_i_(2, 2, 124, 124), top_left, bottom_right, &white);
        }
    }

    if (layers & 2) {
        SetLayerRegisters();
        // Retail's large additive fire card spills warm light well beyond the
        // lantern. Keep a compact flame in the enhanced Shipwreck scene.
        const float flame_extent = compact_dungeon_fire ? 20.0f : 45.0f;
        const float flame_width = gallery_fire ? 10.0f : flame_extent;
        const float flame_height = gallery_fire ? 13.0f : flame_extent / 2.0f;

        flicker_count++;
        srand(flicker_seed);

        if (flicker_count >= 5) {
            flicker_height = flicker_width = 1.0f + 0.1f * rand() / 2147483648.0f;
            flicker_count = 0;
        }

        world[0] = pos[0];
        world[1] = pos[1] + 4.6f;
        world[2] = pos[2];
        world[3] = 1.0f;

        if (MGRotTransPers3DSprite(top_left, bottom_right, world, flame_width * flicker_width * scale,
                                   flame_height * flicker_height * scale, 1) == 1) {
            top_right[0] = bottom_right[0];
            top_right[1] = top_left[1];
            top_right[2] = top_left[2];
            top_right[3] = top_left[3];
            bottom_left[0] = top_left[0];
            bottom_left[1] = bottom_right[1];
            bottom_left[2] = bottom_right[2];
            bottom_left[3] = bottom_right[3];
            world[0] += camera_direction[0];
            world[1] += camera_direction[1];
            world[2] += camera_direction[2];

            if (MGRotTransPers3DSprite(near_top_left, near_bottom_right, world,
                                       flame_width * scale + flicker_width,
                                       flame_height * scale + flicker_height / 2.0f, 0) == 1) {
                top_right[2] = top_left[2] = bottom_left[2] = bottom_right[2] = near_top_left[2];
            }

            set3DSpriteFog(Vif1Packet, core, CRect_i_(0, 0, 64, 64), top_left, top_right, bottom_left, bottom_right,
                           0x80);
        }
        RecordDungeonFireLight(pos, flicker_width, scale);
    }

    const draw2d::Services &services = draw2d::Get();
    services.set_test(nullptr);
    services.set_zbuf(nullptr);
    services.set_alpha(nullptr);
}
