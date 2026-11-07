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

// Explicit capture of everything submitted between the two calls, on any
// device and window (RenderDoc's wildcard). Unlike TriggerCapture this does
// not depend on RenderDoc tracking the presenting window (it does not on
// every window system, e.g. Wayland is capture-only). An empty template
// keeps the current one. EndFrameCapture returns whether a capture was
// written.
bool StartFrameCapture(const std::string& path_template);
bool EndFrameCapture();

// Captures written so far in this process, and the path of capture `index`
// (empty when out of range).
uint32_t NumCaptures();
std::string CapturePath(uint32_t index);

}  // namespace rex::ui::renderdoc
