#include "common.h"

#include <libvu0.h>

#include "battlemenu.hpp"
#include "edit.hpp"
#include "runscript.hpp"

/**
 * Runs the original town menu handler.
 */
int PortOriginalEdMenuMode();

/** The menu requested by the current town event. */
PORT_OVERRIDE int PortEventMenuMode = ED_MENU_NONE;

namespace {

/** The world-map command's native-width script result destination. */
RS_STACKDATA *world_map_result = nullptr;

} // namespace

/**
 * Opens the world map while retaining the script result pointer at native width.
 */
PORT_OVERRIDE int PortWorldMapCommand(RS_STACKDATA *stack, int argument_count) {
    if (stack->type != RS_PTR) {
        return 0;
    }
    world_map_result = stack->p;
    PortEventMenuMode = ED_MENU_WORLD_MAP;
    return 1;
}

/**
 * Publishes the world-map choice when its menu finishes.
 */
int EdMenuMode() {
    const int done = PortOriginalEdMenuMode();
    if (done && PortEventMenuMode == ED_MENU_WORLD_MAP && world_map_result != nullptr) {
        world_map_result->i = MenuMapJumpMode + 1;
        world_map_result = nullptr;
    }
    return done;
}
