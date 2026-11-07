#pragma once
/**
 * @file        ui/renderdoc_capture.h
 * @brief       Programmatic RenderDoc captures without renderdoc_app.h.
 *
 * For app code (test harnesses, debug hotkeys) that cannot include the
 * RenderDoc app API header. Everything is a no-op returning false/0 when
 * RenderDoc is not attached to the process.
 */

#include <cstdint>
#include <string>

namespace rex::ui::renderdoc {

// RenderDoc is injected into this process (checked once, then cached).
bool Attached();

// Captures the next presented frame into files named
// "<path_template>_frame<N>.rdc" (RenderDoc's naming). An empty template
// keeps RenderDoc's current one. False when RenderDoc is not attached.
bool TriggerCapture(const std::string& path_template);

// Captures written so far in this process, and the path of capture `index`
// (empty when out of range).
uint32_t NumCaptures();
std::string CapturePath(uint32_t index);

}  // namespace rex::ui::renderdoc
