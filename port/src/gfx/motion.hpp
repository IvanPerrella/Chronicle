#pragma once

#include "context.hpp"
#include "streamline.hpp"

namespace gfx::detail {

// Rigid, depth-writing world geometry only. The target is cleared to zero for background and
// geometry without valid history. This is an input to a future temporal renderer, not a complete
// dense motion field for DLSS until skinned geometry and depth sprites are covered.
void MotionInit();
void MotionResize();
void MotionDestroyTargets();
void MotionShutdown();
void MotionSetEnabled(bool enabled);
void MotionSetCamera(const StreamlineCamera &camera);
const StreamlineCamera *MotionCamera();
bool MotionBegin();
void MotionDraw(MeshHandle handle, uint32_t first_index, uint32_t index_count,
                const MeshConstants &constants, const DrawState &state);
void MotionEnd();
void MotionDiscard();
const Image *MotionImage();
VkImageView MotionDepthView();

} // namespace gfx::detail
