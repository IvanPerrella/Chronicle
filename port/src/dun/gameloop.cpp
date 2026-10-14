#include "dun/gameloop.hpp"
#include "dun/shipwreck_lights.hpp"

#include <libvu0.h>

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iterator>
#include <typeinfo>
#include <vector>

#include "battlemenu.hpp"
#include "btactstatus.hpp"
#include "btitem.hpp"
#include "btmisc.hpp"
#include "btsysscript.hpp"
#include "camera.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "clothread.hpp"
#include "clsmes.hpp"
#include "collisiondata.hpp"
#include "collision.hpp"
#include "dataread.hpp"
#include "debugfont.hpp"
#include "dispctrl.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "draw3d.hpp"
#include "dranmapfield.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "dungeon.h"
#include "edit.hpp"
#include "editloop3.hpp"
#include "effectmacro.hpp"
#include "frame.hpp"
#include "gamemode.hpp"
#include "gamepad.hpp"
#include "gameutil.hpp"
#include "gfx/gfx.hpp"
#include "healeffect.hpp"
#include "hit_machingun_effect.hpp"
#include "hitmark.hpp"
#include "hitvalue.hpp"
#include "itembombeffect.hpp"
#include "main.hpp"
#include "mainitemmodel.hpp"
#include "mainselect.hpp"
#include "mathutil.hpp"
#include "menu_draw.hpp"
#include "menu_dungeon.hpp"
#include "menu_misc.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "motionmodel.hpp"
#include "nowload.hpp"
#include "npcharacter.hpp"
#include "platform/config.hpp"
#include "randomitem.hpp"
#include "rect.hpp"
#include "runeffect.hpp"
#include "shot_effect.hpp"
#include "shot_effect_pack.hpp"
#include "shot_firebar.hpp"
#include "shot_freefuncs.hpp"
#include "snd.hpp"
#include "stealitem.hpp"
#include "texture.hpp"
#include "textureanime.hpp"
#include "userstatus.hpp"
#include "weaponeffect.hpp"
#include "weaponelement.hpp"

// The dungeon's MainDraw and LoaderLoop, replaced to drop what the renderer has no use for: the VU1
// program call each opens with and MainDraw's wait for GIF path idle before the frame grab, which
// the renderer orders itself. Everything else is retail's, PAL's branch only.
//
// port/include/stubs/dun/gameloop.hpp renames the unit's MainDraw to DunMainDraw, apart from
// editloop's, as the PS2 link does.

// Defined by ps2/src/dun/gameloop.cpp without a header declaration.
struct BOMB_INFO {
    sceVu0FVECTOR pos;
    s32           unk_10;
    s32           aiming;
    s32           throw_step;
    s32           unk_1C;
};

extern BOMB_INFO       BombInfo;
extern CItemBombEffect CBomb__2[3];
extern CDebugFont      CDbgMsg;
extern CRunEffect      CRunFx__2;
extern CFrameVu1      *CharaFrame;
extern CCharacter      CharaHand;
extern ClsMes          DngMes1;
extern CHitValue       HitValue[32];
extern char           *MapInfoNameArea[7];
extern s32             MonstorNameOff;
extern CHitPointMark   MyHitPointMark[16];
extern CCharacter      NewChangeFx;
extern s32             NewChangeFxFlag;
extern CSHOT          *NowShotData;
extern CSHOT_FIREBAR   OzumondFire;
extern CTexture       *TEX_Floor1;
extern CTexture       *TEX_WepGage;
extern CHitMark        WeaponCrashEffect;
extern s32             autoDemo;
extern CFrame         *bicCursorFrame;
extern CFrame         *bombCursorFrame;
extern s32             exitMenuFlag;
extern s32             gameTask;
extern char            floor_name[32];
extern s32             infoMap;
extern s32             itemNowSel;
extern s32             iventMarker;
extern sceVu0FVECTOR   iventPos;
extern s32             lightingMode;
extern s32             lockOnTargetDraw;
extern s32             oldMsgNo;
extern s32             rogoAlphaA[3];
extern s32             rogoSwitch2;
extern s32             rogoY3;

// Draws the floor's monsters. CMonstorUnit::DrawMonstor draws the ones taking part; a dormant one
// within video.detail_distance of the player is passed off as taking part for the length of the
// call, so that its model draws and animates without the monster waking.
static void DrawMonstorDetail() {
    CMonstorUnit *unit = NowMonstorUnit;
    float         detail = ConfigDetailDistance();
    bool          dormant[16];

    for (int i = 0; i < 16; i++) {
        MONSTOR *monster = &unit->monster[i];
        dormant[i] = monster->state == 1 && monster->revealed != 0 && monster->player_distance < detail;

        if (dormant[i]) {
            monster->state = 2;
        }
    }

    unit->DrawMonstor();

    for (int i = 0; i < 16; i++) {
        if (dormant[i]) {
            unit->monster[i].state = 1;
        }
    }
}

namespace {

// Where the status panel's pieces go in a window wider or taller than the frame: life, weapon and
// water (left of the quick items at 0x124) to the left edge, the floor plate (from 0x1FC) to the
// right edge, the quick items between them stay centred; the panel to the top edge, the weapon's
// portrait and gauge to the bottom edge.
constexpr gfx::UiAnchor kStatusAnchor = {0x124, 0x1FC, 240.0f, 240.0f};
// The mini map hangs under the floor plate.
constexpr gfx::UiAnchor kMiniMapAnchor = gfx::UiAnchor::Side(1, -1);

float CharaHeight(CUserStatus *status) {
    float chara_height[6] = {16.0f, 14.0f, 16.0f, 16.0f, 18.0f, 15.0f};
    return chara_height[static_cast<int>(status->cur_chara)];
}

CTexture *NamedTexture(const char *name) {
    return TexManager.GetTexture(const_cast<char *>(name), -1);
}

// The lower Shipwreck medium belongs to the map, not the player's latest floor.
// Collision models are shared between instances of a part, so save and restore their
// transform around each one-time floor probe.
int CollisionPolygonBound(CFrame *frame, int &visits, int depth = 0) {
    if (!frame) return 0;
    if (++visits > 4096 || depth > 64) return -1;
    if (frame->flags == 4) return 0;
    int count = 0;
    if (frame->collision && (frame->flags & 1)) {
        auto *mesh = dynamic_cast<CCollisionMDT *>(frame->collision);
        if (!mesh || typeid(*frame->collision) != typeid(CCollisionMDT) ||
            mesh->mesh_count < 0 || mesh->mesh_count > 32768 ||
            (mesh->mesh_count && !mesh->mesh)) return -1;
        count = mesh->mesh_count;
    }
    if (!(frame->flags & 2) && !(frame->flags & 4)) {
        for (CFrame *child = frame->child; child; child = child->brother) {
            int child_count = CollisionPolygonBound(child, visits, depth + 1);
            if (child_count < 0 || child_count > 32768 - count) return -1;
            count += child_count;
        }
    }
    return count;
}

void InvalidateCollisionWorld(CFrame *frame, int &visits, int depth = 0) {
    if (!frame || ++visits > 4096 || depth > 64) return;
    frame->world_valid = false;
    for (CFrame *child = frame->child; child; child = child->brother)
        InvalidateCollisionWorld(child, visits, depth + 1);
}

float ShipwreckCellDeck(CDungeonMap &map, int x, int z) {
    const MAP_CELL &cell = map.cells[x + z * 20];
    if (cell.parts_no < 0 || cell.parts_no >= static_cast<int>(std::size(map.parts)) || cell.direction < 0 ||
        cell.direction > 3) return std::numeric_limits<float>::quiet_NaN();
    CFrame *frame = map.parts[cell.parts_no].collision;
    if (!frame) return std::numeric_limits<float>::quiet_NaN();
    int visits = 0;
    int bound = CollisionPolygonBound(frame, visits);
    if (bound <= 0 || bound > 32768) return std::numeric_limits<float>::quiet_NaN();

    sceVu0FVECTOR saved_position, saved_rotation;
    sceVu0FMATRIX saved_local;
    sceVu0CopyVector(saved_position, frame->position);
    frame->GetRotation(saved_rotation);
    std::memcpy(saved_local, frame->local, sizeof(saved_local));
    const int saved_no_rotation = frame->no_rotation;
    const int saved_rot_type = frame->rot_type;
    const int saved_srt = frame->srt;
    int turn = cell.direction + map.parts[cell.parts_no].collision_turn;
    if (turn > 3) turn -= 3;
    if (turn == 3) turn = -1;
    frame->SetRotation(0.0f, PI * (-90.0f * turn) / 180.0f, 0.0f);
    frame->SetPosition(160.0f * x, 0.0f, 160.0f * z);

    CBoxVu0 box = {};
    box.min[0] = 160.0f * x - 12.0f;
    box.max[0] = 160.0f * x + 12.0f;
    box.min[1] = -75.0f;
    box.max[1] = 25.0f;
    box.min[2] = 160.0f * z - 12.0f;
    box.max[2] = 160.0f * z + 12.0f;
    std::vector<CCPoly> polys(static_cast<size_t>(bound));
    int count = frame->PickUpNearPoly(polys.data(), box);

    frame->SetRotation(saved_rotation[0], saved_rotation[1], saved_rotation[2]);
    frame->SetPosition(saved_position);
    std::memcpy(frame->local, saved_local, sizeof(saved_local));
    frame->no_rotation = saved_no_rotation;
    frame->rot_type = saved_rot_type;
    frame->srt = saved_srt;
    int invalidated = 0;
    InvalidateCollisionWorld(frame, invalidated);

    if (count <= 0 || count > bound) return std::numeric_limits<float>::quiet_NaN();
    // A downward foot ray should only consider walkable upward surfaces, not a
    // wall, underside, or the overhead rail of the same authored part.
    int floors = 0;
    for (int i = 0; i < count; ++i) {
        if (polys[i].normal[1] > 0.45f) polys[floors++] = polys[i];
    }
    if (!floors) return std::numeric_limits<float>::quiet_NaN();
    sceVu0FVECTOR probe = {160.0f * x, 25.0f, 160.0f * z, 1.0f};
    sceVu0FVECTOR hit = {};
    CCPoly floor = {};
    if (!GetFootPoly(probe, 100.0f, &floor, hit, polys.data(), floors, 0))
        return std::numeric_limits<float>::quiet_NaN();
    return hit[1] - 2.0f;
}

void UpdateShipwreckDeckField(CDungeonMap &map, int floor_number) {
    static const CDungeonMap *cached_map = nullptr;
    static int cached_seed = 0;
    static int cached_floor = -1;
    static uint64_t cached_layout = 0;
    static std::array<float, 400> heights = {};
    uint64_t layout = 1469598103934665603ull;
    int occupied = 0;
    for (const MAP_CELL &cell : map.cells) {
        layout = (layout ^ static_cast<uint32_t>(cell.parts_no + 1)) * 1099511628211ull;
        layout = (layout ^ static_cast<uint32_t>(cell.direction + 1)) * 1099511628211ull;
        if (cell.parts_no >= 0 && cell.parts_no < static_cast<int>(std::size(map.parts))) {
            CFrame *collision = map.parts[cell.parts_no].collision;
            layout = (layout ^ reinterpret_cast<uintptr_t>(collision)) * 1099511628211ull;
            if (collision) {
                auto *mesh = dynamic_cast<CCollisionMDT *>(collision->collision);
                layout = (layout ^ static_cast<uint32_t>(mesh ? mesh->mesh_count : 0)) *
                         1099511628211ull;
            }
        }
        occupied += cell.parts_no >= 0;
    }
    if (map.map_type != 1 || map.room_num <= 0 || occupied < 8) return;
    if (cached_map != &map || cached_seed != map.map_seed ||
        cached_floor != floor_number || cached_layout != layout) {
        std::array<float, 400> candidate;
        candidate.fill(std::numeric_limits<float>::quiet_NaN());
        int measured_count = 0;
        for (int z = 0; z < 20; ++z) {
            for (int x = 0; x < 20; ++x) {
                float measured = ShipwreckCellDeck(map, x, z);
                candidate[x + z * 20] = measured;
                measured_count += std::isfinite(measured);
            }
        }
        if (measured_count == 0) {
            // Some floor layouts appear before their collision resources do.
            // Reprobe on the next draw instead of caching an all-empty layout.
            std::array<float, 400> fallback;
            fallback.fill(8.0f);
            gfx::SetAtmosphereDeckHeights(fallback);
            return;
        }
        // Empty cells and door centres inherit the nearest measured walkable
        // elevation. This is a stable map fallback, never the player's height.
        const auto measured = candidate;
        for (int z = 0; z < 20; ++z) {
            for (int x = 0; x < 20; ++x) {
                float &height = candidate[x + z * 20];
                if (std::isfinite(height)) continue;
                int nearest = std::numeric_limits<int>::max();
                height = 8.0f;
                for (int other_z = 0; other_z < 20; ++other_z) {
                    for (int other_x = 0; other_x < 20; ++other_x) {
                        float neighboring_height = measured[other_x + other_z * 20];
                        if (!std::isfinite(neighboring_height)) continue;
                        int delta_x = x - other_x, delta_z = z - other_z;
                        int distance = delta_x * delta_x + delta_z * delta_z;
                        if (distance < nearest) {
                            nearest = distance;
                            height = neighboring_height;
                        }
                    }
                }
            }
        }
        heights = candidate;
        cached_map = &map;
        cached_seed = map.map_seed;
        cached_floor = floor_number;
        cached_layout = layout;
        if (std::getenv("DC_SHIPWRECK_FOG_TRACE")) {
            std::fprintf(stderr, "FogDeckField seed=%d floor=%d measured_corridor=%.2f measured_room2=%.2f\n",
                         map.map_seed, floor_number, heights[10 + 5 * 20], heights[8 + 5 * 20]);
        }
    }
    gfx::SetAtmosphereDeckHeights(heights);
}

} // namespace

PORT_OVERRIDE void DunMainDraw() {
    // Temporary isolated regression pose: derive the target from this generated
    // floor's room layout instead of assuming the same random seed every run.
    static bool sweep_pose_applied = false;
    static int sweep_play_draws = 0;
    static int sweep_pose_holds = 0;
    static float sweep_x = 0.0f, sweep_y = 0.0f, sweep_z = 0.0f;
    if (gameTask == GAME_TASK_PLAY && selectMapNo == DUNGEON_SHIPWRECK)
        ++sweep_play_draws;
    if (!sweep_pose_applied && sweep_play_draws >= 80 &&
        gameTask == GAME_TASK_PLAY && selectMapNo == DUNGEON_SHIPWRECK &&
        std::getenv("DC_SHIPWRECK_SWEEP_POSE") && NowDngMap && NowDngMap->room_num > 2) {
        const ROOM_INFO &target = NowDngMap->rooms[2];
        const int cell_x = target.x + target.width / 2;
        // The interior deck is just south of room 2's northern cell; its
        // geometric centre lies on the cross-beam/void in this room layout.
        const int cell_z = target.y + 1;
        if (target.width > 0 && target.height > 0 && cell_x >= 0 && cell_x < 20 &&
            cell_z >= 0 && cell_z < 20) {
            const float deck_y = ShipwreckCellDeck(*NowDngMap, cell_x, cell_z);
            if (std::isfinite(deck_y)) {
                sweep_x = 160.0f * cell_x;
                sweep_y = deck_y + 2.0f;
                sweep_z = 160.0f * cell_z + 40.0f;
                NowCamera__3->SetAngleSoon(0.0f);
                NowCamera__3->SetHeight(30.0f);
                NowCamera__3->FollowOn();
                sweep_pose_applied = true;
                std::fprintf(stderr, "DC_SHIPWRECK_SWEEP_POSE seed=%d rooms=%d target_cell=%d,%d deck=%.2f pos=%.1f,%.1f,%.1f after %d play draws\n",
                             NowDngMap->map_seed, NowDngMap->room_num, cell_x, cell_z,
                             deck_y, sweep_x, sweep_y, sweep_z, sweep_play_draws);
                for (int room = 0; room < NowDngMap->room_num; ++room) {
                    const ROOM_INFO &bounds = NowDngMap->rooms[room];
                    std::fprintf(stderr, "DC_SHIPWRECK_SWEEP_POSE room%d bounds=%d,%d %dx%d\n",
                                 room, bounds.x, bounds.y, bounds.width, bounds.height);
                }
            }
        }
    }
    if (sweep_pose_applied) {
        CharaFrame->SetPosition(sweep_x, sweep_y, sweep_z);
        NowCamera__3->SetFollow(sweep_x, sweep_y, sweep_z);
        // Reapply translation after MoveChara each draw. The input still owns
        // next_angle and height, so the isolated sweep can yaw and pitch.
        NowCamera__3->Step(-1);
        ++sweep_pose_holds;
        if (sweep_pose_holds == 1 || sweep_pose_holds == 24 || sweep_pose_holds % 120 == 0) {
            sceVu0FVECTOR camera_eye;
            NowCamera__3->GetPos(camera_eye);
            std::fprintf(stderr, "DC_SHIPWRECK_SWEEP_POSE hold=%d char=%.1f,%.1f,%.1f camera=%.1f,%.1f,%.1f\n",
                         sweep_pose_holds, CharaFrame->position[0], CharaFrame->position[1],
                         CharaFrame->position[2], camera_eye[0], camera_eye[1], camera_eye[2]);
        }
    }
    sceVu0FMATRIX camera;
    sceVu0FVECTOR eye;
    sceVu0FMATRIX view;
    sceVu0FMATRIX unit;
    sceGsTex0     frame_tex;
    sceGsTex0     water_tex;
    int           i;
    bool          scene_captured = false;

    EdEventInfo.main_texture_animation = NULL;
    NowCamera__3->GetPos(eye);
    NowCamera__3->GetCameraMatrix(camera);
    sceVu0UnitMatrix(unit);
    sceVu0MulMatrix(view, unit, camera);
    MGSetViewMatrix(view, eye);
    SndSetCamera(NowCamera__3);

    // The entry script begins its camera move before Toan reaches the hook shot. Keep the
    // dungeon fade closed until the camera has settled and Toan is about to enter the frame.
    struct ShipwreckEntryReveal {
        bool holding = false;
        bool revealed = false;
        int settled = 0;
        float eye[3] = {};
    };
    static ShipwreckEntryReveal entry_reveal;
    const bool entry_script = selectMapNo == DUNGEON_SHIPWRECK &&
                              gameTask == GAME_TASK_SCRIPT_RUN && BtEventInfo.running_script_no == 100;
    if (!entry_script) {
        if (entry_reveal.holding) DispFade__3.FadeInStart(8.0f);
        entry_reveal = {};
    } else if (BtAllDrawFlag != 0) {
        if (!entry_reveal.holding && !entry_reveal.revealed && DispFade__3.GetRate() > 0.0f) {
            DispFade__3.FadeInStart(0.0f);
            entry_reveal.holding = true;
            std::copy_n(eye, 3, entry_reveal.eye);
        }
        if (entry_reveal.holding) {
            const float dx = eye[0] - entry_reveal.eye[0];
            const float dy = eye[1] - entry_reveal.eye[1];
            const float dz = eye[2] - entry_reveal.eye[2];
            entry_reveal.settled = dx * dx + dy * dy + dz * dz < 0.75f * 0.75f
                                       ? entry_reveal.settled + 1 : 0;
            std::copy_n(eye, 3, entry_reveal.eye);
            sceVu0FVECTOR toan_head;
            sceVu0CopyVector(toan_head, CharaFrame->position);
            toan_head[1] += 12.0f;
            toan_head[3] = 1.0f;
            int screen[4] = {};
            const bool in_front = MGRotTransPers2D(screen, toan_head, 0) != 0;
            // The foreground deck hides Toan below roughly four fifths of this shot.
            const bool above_deck = in_front && screen[0] >= 0 && screen[0] < 640 &&
                                    screen[1] >= 0 && screen[1] < SCREEN_HEIGHT * 4 / 5;
            if (entry_reveal.settled >= 3 && BtActStatus.player_visible &&
                EdEventInfo.player_draw && above_deck) {
                DispFade__3.FadeInStart(8.0f);
                entry_reveal.holding = false;
                entry_reveal.revealed = true;
            }
        }
    }
    if (std::getenv("DC_CAMERA_TRACE") && selectMapNo == DUNGEON_SHIPWRECK) {
        static int trace_draw = 0;
        std::fprintf(stderr,
                     "DC_HOOK_FADE draw=%d task=%d demo=%d fade=%.2f all_draw=%d eye=(%.2f,%.2f,%.2f)\n",
                     trace_draw++, gameTask, autoDemo, DispFade__3.GetRate(), BtAllDrawFlag,
                     eye[0], eye[1], eye[2]);
    }

    if (BtAllDrawFlag == 0) {
        if (BtItemListCashFlag != 0) {
            TexManager.ReloadTexture(Vif1Packet, 0x28);
            DngActiveItemTextureCopy();
            DngActiveWeaponTextureCopy();
            BtItemListCashFlag = 0;
        }

        EdFadeInOut();
        return;
    }

    if (lightingMode == 0) {
        MGSetPLight(main_light, main_lightcolor);
        MGSetAmbient(main_ambientlight);
        MGSetBGColor(main_bgColor[0], main_bgColor[1], main_bgColor[2], 128.0f);
        MGSetFogParm(main_fogRate[0], main_fogRate[1], main_fogColor[0], main_fogColor[1], main_fogColor[2], main_fogRate[2], main_fogRate[3]);
    } else {
        MGSetPLight(sub_light, sub_lightcolor);
        MGSetAmbient(sub_ambientlight);
        MGSetBGColor(sub_bgColor[0], sub_bgColor[1], sub_bgColor[2], 128.0f);
        MGSetFogParm(sub_fogRate[0], sub_fogRate[1], sub_fogColor[0], sub_fogColor[1], sub_fogColor[2], sub_fogRate[2], sub_fogRate[3]);
    }

    if (BtItemListCashFlag != 0) {
        TexManager.ReloadTexture(Vif1Packet, 0x28);
        DngActiveItemTextureCopy();
        DngActiveWeaponTextureCopy();
        BtItemListCashFlag = 0;
    }

    TexManager.ReloadTexture(Vif1Packet, 3);
    BtTexAnime.TexAnime(3);
    NowDngMap->DrawBGModel(NowCamera__3);
    NowDngMap->DrawDummyModel(NowCamera__3);

    if (NowDngMap->map_type == 1) {
        NowDngMap->DrawMap(NowCamera__3, CharaFrame);
    } else {
        NowDngMap->DrawMapFreeStyle();
    }

    // The map field's own draw, not the virtual one it inherits: retail binds
    // this call at compile time.
    NowDranMapField->CDranMapField::Draw();

    if (itemOpenSmallFlag == 0 && itemOpenBigFlag == 0) {
        sceVu0FVECTOR box_pos;

        sceVu0CopyVector(box_pos, CharaFrame->position);
        NowDngMap->DrawItemBox(box_pos);
    } else {
        if (itemOpenSmallFlag != 0) {
            itemOpenSmall.Draw();
        }

        if (itemOpenBigFlag != 0) {
            itemOpenBig.Draw();
        }
    }

    if (BtEventMode != 0) {
        sceVu0FVECTOR focus = {200.0f, 500.0f, 0.0f, 0.0f};

        DepthOfField(focus, 3, 0x40, 0);
    }

    if (atraGetStatus == 0) {
        sceVu0FVECTOR atra_pos;

        TexManager.ReloadTexture(Vif1Packet, 0x16);
        sceVu0CopyVector(atra_pos, CharaFrame->position);
        NowDngMap->DrawAtraBoll(atra_pos);
    }

    Draw_MainUnitShadow();

    if (BtActStatus.player_visible != 0 && EdEventInfo.player_draw != 0) {
        Draw_MainUnit();
    }

    if (CMonUnitHyde == 0 && BtEventMode == 0) {
        DrawMonstorDetail();
    }

    if (CharaMainHandViewFlag != 0) {
        TexManager.ReloadTexture(Vif1Packet, 0x11);

        if (UserStatus->cur_chara == CHARA_XIAO) {
            CharaHand.Draw();
        }

        if (NowWeapon != NULL && UserStatus->cur_chara == CHARA_XIAO && NowWeapon->frame != NULL && BtActStatus.weapon_visible != 0) {
            TexManager.ReloadTexture(Vif1Packet, 0x1D);
            NowWeapon->Draw();
        }
    }

    if (BtEventMode != 0) {
        for (i = 0; i < 6; i++) {
            if (EdEventInfo.npc_draw[i] != 0) {
                TexManager.ReloadTexture(Vif1Packet, i + 0x20);
                NPCUnit[i].TextureAnime(NPCUnit[i].texture_block);
                NPCUnit[i].Draw();

                if (i == BtEventInfo.bee_npc) {
                    DrawBee(NPCUnit[BtEventInfo.bee_npc].frame, 0xF);
                }
            }
        }
    }

    NowDngMap->DrawNPCDraw();
    mainItemModel.Draw();
    TexManager.ReloadTexture(Vif1Packet, 6);
    DrawWaterLing();

    if (Water_Splash_actFlag != 0) {
        Water_Splash.Draw();
    }

    if (NowDngMap->map_type == 1) {
        CRect_i_      area;
        sceVu0FVECTOR water_pos;

        TexManager.ReloadTexture(Vif1Packet, 0xD);
        MGGetFBuffTex(&frame_tex);
        area.x = 0;
        area.y = 0;
        area.width = 0x280;
        area.height = SCREEN_HALF_HEIGHT;
        water_tex = *(sceGsTex0 *) &NamedTexture("water")->tex0;
        MGMoveImage(&frame_tex, area, &water_tex, 0, 0, 0);
        sceVu0CopyVector(water_pos, CharaFrame->position);
        NowDngMap->DrawWater(water_pos, driveStepHold);
    }

    if (CEffectHyde == 0 && BtEventMode == 0) {
        TexManager.ReloadTexture(Vif1Packet, 0x13);
        NowShockWave->Draw(NowCamera__3);

        for (i = 0; i < 3; i++) {
            CBomb__2[i].Draw(NowCamera__3);
        }

        for (i = 0; i < 5; i++) {
            MasekiEffect[i].Draw();
        }

        CSHOT_EFFECT *shot = NowShotEffect->effect;

        for (i = 0; i < 5; i++) {
            shot[i].Draw();
        }

        NowMainEffect->Draw();
    }

    sceVu0FVECTOR raster_pos;

    sceVu0CopyVector(raster_pos, CharaFrame->position);
    // Shipwreck's collision ground is two units above the visible d03b deck
    // (runtime first-room measurements: character/nav Y=10, deck Y≈8).
    if (ConfigGet().enhanced_lighting && selectMapNo == DUNGEON_SHIPWRECK) {
        UpdateShipwreckDeckField(*NowDngMap, UserStatus->cur_floor);
    }
    const bool enhanced_shipwreck = ConfigGet().enhanced_lighting &&
                                    selectMapNo == DUNGEON_SHIPWRECK &&
                                    gfx::RayTracingEnabled();
    if (!enhanced_shipwreck) {
        TexManager.ReloadTexture(Vif1Packet, 0xE);
        NowDngMap->DrawRaster(CharaFrame);
    }
    if (enhanced_shipwreck && ConfigGet().shipwreck_local_lights) {
        BeginShipwreckFireLights(CharaFrame->position);
    }

    if (NowDngMap->map_type == 1) {
        NowDngMap->DrawFire(CharaFrame, NowCamera__3);
    } else {
        NowDngMap->DrawFireFreeStyle(CharaFrame, NowCamera__3);
    }
    if (enhanced_shipwreck && ConfigGet().shipwreck_local_lights) EndShipwreckFireLights();

    if (atraGetStatus != 0 && atraShortGetType != 0) {
        TexManager.ReloadTexture(Vif1Packet, 0x1C);
        shortAtraEffect.Draw();
    }

    if (gateItemFlag != 0) {
        TexManager.ReloadTexture(Vif1Packet, 0x1C);
        MGDraw(itemBoxModel);
    }

    if (itemOpenSmallFlag != 0) {
        sceVu0FVECTOR ambient;
        sceVu0FVECTOR lift;
        sceVu0FVECTOR held;

        MGGetAmbient(ambient);
        TexManager.ReloadTexture(Vif1Packet, 0x16);
        itemOpenSmallFx.Draw();
        TexManager.ReloadTexture(Vif1Packet, 0x1C);
        sceVu0CopyVector(lift, itemBoxModel->position);
        sceVu0CopyVector(held, lift);

        static float itemposr = -PI;

        itemposr += 0.10471976f;

        if (itemposr >= PI) {
            itemposr -= TWO_PI;
        }

        lift[1] += 0.5f * sinf(itemposr);
        itemBoxModel->SetPosition(lift);
        MGDraw(itemBoxModel);
        MGSetAmbient(ambient);
        itemBoxModel->SetPosition(held);
    }

    if (itemOpenBigFlag != 0) {
        sceVu0FVECTOR lift;
        sceVu0FVECTOR held;

        TexManager.ReloadTexture(Vif1Packet, 0x16);
        MGDraw(itemOpenBigFx.frame);
        sceVu0CopyVector(lift, itemBoxModel->position);
        sceVu0CopyVector(held, lift);

        static float itemposr = -PI;

        itemposr += 0.10471976f;

        if (itemposr >= PI) {
            itemposr -= TWO_PI;
        }

        lift[1] += 0.03f * sinf(itemposr);
        itemBoxModel->SetPosition(lift);
        TexManager.ReloadTexture(Vif1Packet, 0x1C);
        MGDraw(itemBoxModel);
    }

    if (NewChangeFxFlag != 0) {
        sceVu0FVECTOR pos;
        sceVu0FVECTOR rot;

        TexManager.ReloadTexture(Vif1Packet, 0xB);
        sceVu0CopyVector(pos, CharaMain.pos);
        CharaMain.GetRotation(rot);
        NewChangeFx.SetPosition(pos);
        NewChangeFx.SetRotation(rot);
        NewChangeFx.Draw();
    }

    if (EscapeFlag != 0) {
        TexManager.ReloadTexture(Vif1Packet, 0x1C);
        EscapeEffect.Draw();
    }

    NowDngMap->DrawTrapCircle();
    TexManager.ReloadTexture(Vif1Packet, 1);

    if (CMonUnitHyde == 0 && BtEventMode == 0) {
        NowMonstorUnit->DrawMonstorCursor();
    }

    if (BtEventInfo.event_marker != 0) {
        sceVu0FVECTOR mark;

        sceVu0CopyVector(mark, CharaMain.pos);
        mark[1] += 22.0f;

        static float bic_posr = -PI_SHORT;

        bic_posr += 0.104719736f;

        if (bic_posr >= PI_SHORT) {
            bic_posr -= TWO_PI_SHORT;
        }

        mark[1] += 0.5f * sinf(bic_posr);

        mark[1] += CharaHeight(UserStatus) - 15.0f;
        bicCursorFrame->SetPosition(mark);
        MGDraw(bicCursorFrame);
        BtEventInfo.event_marker = false;
    }

    if (BtAllClear == 0 && DebugStatus[10] == 0) {
        setbilinear(0);

        if (autoDemo == 0 && CMonUnitHyde == 0) {
            setTargetCursor(lockOnTargetFlag);

            if (lockOnTargetDraw != 0) {
                MGDraw(cursorFrame);
                lockOnTargetDraw = 0;
            }

            setbilinear(0);
        }

        if (iventInfo != DNG_EVENT_NONE && iventMarker != 0) {
            sceVu0CopyVector(iventPos, CharaMain.pos);
            iventPos[1] += 22.0f;

            static float bic_posr = -PI_SHORT;

            bic_posr += 0.104719736f;

            if (bic_posr >= PI_SHORT) {
                bic_posr -= TWO_PI_SHORT;
            }

            iventPos[1] += 0.5f * sinf(bic_posr);

            iventPos[1] += CharaHeight(UserStatus) - 15.0f;
            bicCursorFrame->SetPosition(iventPos);
        }

        if (BombInfo.aiming != 0) {
            bombCursorFrame->SetPosition(BombInfo.pos);
            MGDraw(bombCursorFrame);
        }

        if (CEffectHyde == 0 && BtEventMode == 0) {
            MGSetGsZBUF(NULL);
            RandomItem->Draw();
            StealItem.Draw();
        }

        if (autoDemo == 0) {
            sceGsAlpha blend;
            sceGsZbuf  depth;

            TexManager.ReloadTexture(Vif1Packet, 0x12);

            blend = mgAlpha;
            blend.bits.a = 0;
            blend.bits.b = 2;
            blend.bits.c = 0;
            blend.bits.d = 1;
            MGSetGsALPHA(&blend);

            depth = mgZBuffer;
            depth.bits.zmsk = 1;
            MGSetGsZBUF(&depth);

            for (i = 0; i < 4; i++) {
                CWeaponElFx[i].Draw();
            }

            WeaponCrashEffect.Draw();

            for (i = 0; i < 16; i++) {
                HitMark[i].Draw();
                HitPointMark[i].Draw();
                MyHitPointMark[i].Draw();
            }

            HealEffect.Draw();

            if (CEffectHyde == 0 && BtEventMode == 0) {
                NowShotData->draw();
                OzumondShotEffect.Draw();
                OzumondFire.Draw();
            }

            CWeaponFx.Draw();
            CRunFx__2.Step();
            CRunFx__2.Draw();
            MGSetGsALPHA(NULL);
            MGSetGsZBUF(NULL);
        }

        if (rogoSwitch2 == 1 && BtEventInfo.floor_title_off == 0) {
            TEX_Floor1 = TexManager.GetTexture(floor_name, -1);
            TexManager.ReloadTexture(Vif1Packet, 8);
            StartMessageDraw(TEX_Floor1, selectMapNo, UserStatus->cur_floor, BtUraDongeon, rogoAlphaA[2]);
        }

        TexManager.ReloadTexture(Vif1Packet, 2);

        gfx::CaptureScene();
        scene_captured = true;
        gfx::SetUiAnchor(kStatusAnchor);

        int gauge_alpha = rogoY3 + 0x60;

        if ((int) BtActStatus.action_gauge >= 100) {
            if (BtActStatus.gauge_flash == 0) {
                set2DSprite(Vif1Packet, TEX_WepGage, CRect_i_(0x28, SCREEN_HEIGHT - 0x34, 0x80, 0x1C), CRect_i_(0, 0, 0x80, 0x1C), gauge_alpha);
            } else {
                set2DSprite(Vif1Packet, TEX_WepGage, CRect_i_(0x28, SCREEN_HEIGHT - 0x34, 0x80, 0x1C), CRect_i_(0, 0x1C, 0x80, 0x1C), gauge_alpha);
            }
        }

        float wear = BtActStatus.action_gauge;

        if ((int) wear < 100) {
            int left = (int) wear;

            set2DSprite(Vif1Packet, TEX_WepGage, CRect_i_(0x28, SCREEN_HEIGHT - 0x34, 0x80, 0x1C), CRect_i_(0, 0x54, 0x80, 0x1C), gauge_alpha);
            set2DSprite(Vif1Packet, TEX_WepGage, CRect_i_(0x42, SCREEN_HEIGHT - 0x34, left, 0x1C), CRect_i_(0x1A, 0x38, left, 0x1C), gauge_alpha);
        }

        if (infoMap != 0 && NowDngMap->map_type == 1) {
            sceVu0FVECTOR map_pos;
            sceVu0FVECTOR map_rot;

            gfx::UiAnchorScope map_anchor(kMiniMapAnchor);

            TexManager.ReloadTexture(Vif1Packet, 0x1F);
            sceVu0CopyVector(map_pos, CharaFrame->position);
            CharaFrame->GetRotation(map_rot);
            NowDngMap->DrawMiniMap(map_pos, map_rot[1]);
            NowMonstorUnit->DrawMapSymbol(map_pos);
            RandomItem->MapSymbolDraw();
        }

        topStatusInfo(rogoY3, itemNowSel, UserStatus->cur_floor);
        BtStatusErrDraw(rogoY3);
        gfx::SetUiAnchor({});

        for (i = 0; i < 32; i++) {
            HitValue[i].Draw();
        }

        if (exitMenuFlag != 0) {
            TexManager.ReloadTexture(Vif1Packet, 7);
            set2DSprite(Vif1Packet, NamedTexture("pause"), CRect_i_(0x100, 0xCC, 0x80, 0x28), 0, 0);
        }

        setbilinear(1);
    }

    if (MonstorNameOff == 0) {
        MonsterNameDraw();
    }

    SetMonsterNameDrawFlag(0);

    if (EdEventInfo.screen_filter != 0) {
        sceVu0FVECTOR fade;

        sceVu0CopyVector(fade, EdEventInfo.screen_filter_color);
        MGFillBox(CRect_i_(0, 0, 0x2800, 0xE00), (int) fade[0], (int) fade[1], (int) fade[2], (int) fade[3]);
    }

    if (BtEventMode != 0) {
        EdEventSpriteDraw();
        EBDraw();
        TexManager.ReloadTexture(Vif1Packet, 0x1A);
        BtEventMes0.DrawMesWin();
        BtEventMes1.DrawMesWin();
    }

    for (i = 0; i < 1; i++) {
        if (EdEventInfo.item_frame[i] != NULL) {
            TexManager.ReloadTexture(Vif1Packet, i + 0x28);
            MGDraw(EdEventInfo.item_frame[i]);
        }
    }

    setbilinear(0);
    TexManager.ReloadTexture(Vif1Packet, 0x1A);
    SystemMesStep();
    SystemMesDraw();

    int showing;

    if (DngMessMan.hide_timer > 0) {
        showing = 0;
    } else {
        showing = DngMessMan.enabled;
    }

    if (showing != 0) {
        if (DngMessMan.steev_window == 0) {
            int mes_no = DngMessMan.message;

            DngMes1.mes_no[0] = DngMessMan.insert_mes_1;
            DngMes1.mes_no[1] = DngMessMan.insert_mes_2;
            DngMes1.values[0] = DngMessMan.insert_value_1;
            DngMes1.values[1] = DngMessMan.insert_value_2;
            DngMes1.value_signed = true;
            DngMes1.value_show = false;

            if (mes_no != oldMsgNo) {
                oldMsgNo = mes_no;
                Mes1MakeFlg = 1;
            }

            if (Mes1MakeFlg != 0) {
                Mes1MakeFlg = DngMes1.MakeMesWin(mes_no);
            }

            if (mes_no != -1) {
                DngMes1.text_x = 0x136;
                DngMes1.text_y = 0x154;
                DngMes1.auto_pos = MES_POS_BOTTOM_RIGHT;
                DngMes1.DrawMesWin();
            }
        } else {
            int mes_no = DngMessMan.message;
            int pos[4];

            DngMesStb.value_signed = true;
            DngMesStb.value_show = false;

            if (mes_no != oldMsgNo) {
                oldMsgNo = mes_no;
                Mes1MakeFlg = 1;
            }

            if (Mes1MakeFlg != 0) {
                Mes1MakeFlg = DngMesStb.MakeMesWin(mes_no);
            }

            if (mes_no != -1) {
                DngMesStb.auto_pos = MES_POS_BOTTOM;
                DngMesStb.AutoSet(pos);
                DngMesStb.DrawMesWin();
            }
        }
    }

    if (BtEventMode != 0) {
        if (DebugStatus[4] != 0) {
            TexManager.ReloadTexture(Vif1Packet, 0xC);
            EdDDrawFont();
        }
    } else {
        TexManager.ReloadTexture(Vif1Packet, 0xC);
        DebugInfomationDraw();
    }

    // Entry scripts hide the HUD, so the usual world/HUD boundary above does
    // not run. Resolve the HDR world before drawing the global black fade;
    // otherwise fog and tone mapping composite over that fade and reveal the
    // empty shaft even while the game's fade rate is fully opaque.
    if (!scene_captured) {
        gfx::CaptureScene();
    }
    DispFade__3.FadeIn(Vif1Packet);
    DispFade__3.FadeOut(Vif1Packet);
    EdFadeInOut();

    if (frameCaputer != 0) {
        TexManager.ReloadTexture(Vif1Packet, 0x17);
        CTexture *frame_image = NamedTexture("frame_image");
        if (ConfigGet().enhanced_lighting && frame_image != nullptr) {
            PortTextureRef ref = Draw3DResolveTex0(frame_image->tex0);
            if (ref.valid) gfx::CaptureMenuBackground(ref.binding.texture);
        }
        MGMoveFrameBuffImage((sceGsTex0 *) &frame_image->tex0, 0, 0, 0);
    }
}

extern LOADTEXTURE_INFO2 texdata__2[];

// Retail formats the floor's gate key image over the third name in texdata__2, a string literal the
// PS2 left writable and a host need not; the port formats it into a buffer of its own.
PORT_OVERRIDE void LoadBaseTexture() {
    LOADTEXTURE_INFO2 info[96];
    int               size;
    int               i;
    char             *name;
    char              gate_key[16];

    snprintf(gate_key, sizeof(gate_key), "gatekey0%d.img", selectMapNo + 1);

    char path[64] = "dun/pack/teximg2.pac";

    if (LanguageCode > LANG_JAPANESE) {
        sprintf(path, "dun/pack/teximg2_%d.pac", LanguageCode);
    }

    LoadFile(path, (void *) read_buffer, NULL);
    wait_now_loading_vsync();

    i = 0;

    while ((name = texdata__2[i].name) != NULL) {
        if (i == 2) {
            name = gate_key;
        }

        // A name that starts with '#' asks the manager for a blank page of that size rather than
        // for a file inside the pack.
        if (name[0] == '#') {
            info[i].name = name;
        } else {
            u_int *found = GetPackFile(read_buffer, name, &size);

            if (found != NULL) {
                info[i].name = (char *) found;
            } else {
                printf("Error::Pack->FileNotFound [%d]%s!!\n", i, name);
                exit__2(-1);
            }
        }

        info[i].block_no = texdata__2[i].block_no;
        i++;
    }

    info[i].name = NULL;
    TexManager.LoadTextureBlock(-1, info);

    // The message window's pages, and the three images the language's own pack supplies in place of
    // the names written here.
    LOADTEXTURE_INFO2 mes_info[8] = {
        {(char *) "#mes_frame_buff#640#480#4", 0x1A, 0},
        {(char *) "#fukidashibase#640#224#4",  0x1A, 0},
        {(char *) "#fontbase#512#256#1",       0x1A, 0},
        {(char *) "meswin/gaiji.img",          0x1A, 0},
        {(char *) "meswin/fuki256.img",        0x1A, 0},
        {(char *) "meswin/syst04.img",         0x1A, 0},
    };

    char mes_path[64] = "meswin/mes_tex.pak";

    if (LanguageCode > LANG_JAPANESE) {
        sprintf(mes_path, "meswin/mes_tex_%d.pak", LanguageCode);
    }

    LoadFile(mes_path, read_buffer, NULL);
    wait_now_loading_vsync();

    mes_info[3].name = (char *) GetPackFile(read_buffer, (char *) "gaiji.img", NULL);
    mes_info[4].name = (char *) GetPackFile(read_buffer, (char *) "fuki256.img", NULL);
    mes_info[5].name = (char *) GetPackFile(read_buffer, (char *) "syst04.img", NULL);
    TexManager.LoadTextureBlock(-1, mes_info);
}

PORT_OVERRIDE int LoaderLoop() {
    char       name[96];
    int        i;
    int        chosen = 0;
    static int nowCursor = 0;

    CDbgMsg.length = sprintf(&CDbgMsg.text[CDbgMsg.length], "- MapInfomationFile Loader -\n");

    for (i = 0; i < 7; i++) {
        strcpy(name, MapInfoNameArea[i]);

        if (nowCursor == i) {
            CDbgMsg.length += sprintf(&CDbgMsg.text[CDbgMsg.length], ">>[%2d] %s\n", i + 1, name);
        } else {
            CDbgMsg.length += sprintf(&CDbgMsg.text[CDbgMsg.length], "  [%2d] %s\n", i + 1, name);
        }
    }

    if (GamePad.Down(PAD_UP) != 0 && nowCursor != 0) {
        nowCursor--;
    }

    if (GamePad.Down(PAD_DOWN) != 0 && nowCursor != 6) {
        nowCursor++;
    }

    if (GamePad.Down(PAD_CIRCLE) != 0) {
        return 2;
    }

    if (GamePad.Down(PAD_START) != 0 || GamePad.Down(PAD_CROSS) != 0) {
        selectMapNo = nowCursor;
        main_select_menu_no = nowCursor;
        MapJump(selectMapNo + 200, -1);
        MGSetBGColor(0.0f, 0.0f, 0.0f, 128.0f);
        chosen = 1;
    }

    TexManager.ReloadTexture(Vif1Packet, 12);
    CDbgMsg.Draw();
    return chosen;
}
