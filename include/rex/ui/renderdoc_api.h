#pragma once
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstdint>
#include <memory>
#include <string>

#include <rex/platform/dynlib.h>

#if REX_PLATFORM_MAC
struct RENDERDOC_API_1_0_0;
#else
#include <renderdoc_app.h>
#endif

namespace rex {
namespace ui {

class RenderDocAPI {
 public:
  static std::unique_ptr<RenderDocAPI> CreateIfConnected();

  RenderDocAPI(const RenderDocAPI&) = delete;
  RenderDocAPI& operator=(const RenderDocAPI&) = delete;

  ~RenderDocAPI();

  // Always present if this object exists.
  const RENDERDOC_API_1_0_0* api_1_0_0() const { return api_1_0_0_; }

  // Capture helpers for callers that cannot include renderdoc_app.h.
  // Captures the next presented frame (as RenderDoc's capture key does).
  void TriggerCapture() const;
  // Explicit capture bracket on any device/window (wildcard NULL, NULL).
  void StartFrameCapture() const;
  bool EndFrameCapture() const;
  // Path prefix for capture files; RenderDoc appends "_frameN.rdc".
  void SetCaptureFilePathTemplate(const char* path_template) const;
  // Captures made so far in this process, and the file path of capture
  // `index` (empty when out of range).
  uint32_t NumCaptures() const;
  std::string CapturePath(uint32_t index) const;

 private:
  explicit RenderDocAPI() = default;

  rex::platform::DynamicLibrary library_;

  const RENDERDOC_API_1_0_0* api_1_0_0_ = nullptr;
};

}  // namespace ui
}  // namespace rex
