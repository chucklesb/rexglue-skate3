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
#include <functional>
#include <string>

namespace rex::ui::renderdoc {

// Runs `fn` while the graphics backend holds every GPU queue it submits to.
// StartFrameCapture/EndFrameCapture called from a thread that isn't
// presenting make RenderDoc submit its own work (initial resource contents,
// readbacks) on the app's queues. Vulkan queues need external
// synchronization, so without the guard that races the app's submits on its
// other threads: intermittent device loss, or a crash in the driver
// (RTX 4070, RenderDoc 1.46). D3D12 queues are thread-safe and need no guard.
// `owner` is the backend object that installs it; Clear only removes that
// owner's guard.
using QueueGuard = std::function<void(const std::function<void()>& fn)>;
void SetQueueGuard(const void* owner, QueueGuard guard);
void ClearQueueGuard(const void* owner);

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
