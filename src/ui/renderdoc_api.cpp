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

#include <mutex>

#include <rex/logging.h>
#include <rex/ui/renderdoc_api.h>
#include <rex/ui/renderdoc_capture.h>

namespace rex {
namespace ui {

std::unique_ptr<RenderDocAPI> RenderDocAPI::CreateIfConnected() {
#if REX_PLATFORM_MAC
  // The vendored RenderDoc app API header doesn't expose a macOS path.
  // Keep RenderDoc optional and simply report "not connected" on this platform.
  return nullptr;
#else
  std::unique_ptr<RenderDocAPI> renderdoc_api(new RenderDocAPI());

  pRENDERDOC_GetAPI get_api = nullptr;

  // Try to load the RenderDoc library. If RenderDoc is attached, the library
  // should already be loaded into the process and this will increment the
  // reference count. If not attached, the load will fail and we return nullptr.
  if (!renderdoc_api->library_.Load(platform::lib_names::kRenderDoc)) {
    return nullptr;
  }
  get_api = renderdoc_api->library_.GetSymbol<pRENDERDOC_GetAPI>("RENDERDOC_GetAPI");

  // get_api will be null if RenderDoc is not connected, or the API isn't
  // available on this platform, or there was an error.
  if (!get_api || !get_api(eRENDERDOC_API_Version_1_0_0, (void**)&renderdoc_api->api_1_0_0_) ||
      !renderdoc_api->api_1_0_0_) {
    return nullptr;
  }

  REXLOG_INFO("RenderDoc API initialized");

  return renderdoc_api;
#endif
}

RenderDocAPI::~RenderDocAPI() {
  library_.Close();
}

#if REX_PLATFORM_MAC
void RenderDocAPI::TriggerCapture() const {}
void RenderDocAPI::StartFrameCapture() const {}
bool RenderDocAPI::EndFrameCapture() const {
  return false;
}
void RenderDocAPI::SetCaptureFilePathTemplate(const char*) const {}
uint32_t RenderDocAPI::NumCaptures() const {
  return 0;
}
std::string RenderDocAPI::CapturePath(uint32_t) const {
  return {};
}
#else
void RenderDocAPI::TriggerCapture() const {
  api_1_0_0_->TriggerCapture();
}

void RenderDocAPI::StartFrameCapture() const {
  api_1_0_0_->StartFrameCapture(nullptr, nullptr);
}

bool RenderDocAPI::EndFrameCapture() const {
  return api_1_0_0_->EndFrameCapture(nullptr, nullptr) != 0;
}

void RenderDocAPI::SetCaptureFilePathTemplate(const char* path_template) const {
  // SetLogFilePathTemplate is the 1.0.0 name of SetCaptureFilePathTemplate.
  api_1_0_0_->SetLogFilePathTemplate(path_template);
}

uint32_t RenderDocAPI::NumCaptures() const {
  return api_1_0_0_->GetNumCaptures();
}

std::string RenderDocAPI::CapturePath(uint32_t index) const {
  uint32_t length = 0;
  if (!api_1_0_0_->GetCapture(index, nullptr, &length, nullptr) || length == 0) {
    return {};
  }
  std::string path(length, '\0');
  if (!api_1_0_0_->GetCapture(index, path.data(), &length, nullptr)) {
    return {};
  }
  path.resize(path.find('\0') == std::string::npos ? path.size() : path.find('\0'));
  return path;
}
#endif

namespace renderdoc {
namespace {

const RenderDocAPI* Api() {
  static std::once_flag once;
  static std::unique_ptr<RenderDocAPI> api;
  std::call_once(once, [] { api = RenderDocAPI::CreateIfConnected(); });
  return api.get();
}

}  // namespace

bool Attached() {
  return Api() != nullptr;
}

bool TriggerCapture(const std::string& path_template) {
  const RenderDocAPI* api = Api();
  if (!api) {
    return false;
  }
  if (!path_template.empty()) {
    api->SetCaptureFilePathTemplate(path_template.c_str());
  }
  api->TriggerCapture();
  return true;
}

bool StartFrameCapture(const std::string& path_template) {
  const RenderDocAPI* api = Api();
  if (!api) {
    return false;
  }
  if (!path_template.empty()) {
    api->SetCaptureFilePathTemplate(path_template.c_str());
  }
  api->StartFrameCapture();
  return true;
}

bool EndFrameCapture() {
  const RenderDocAPI* api = Api();
  return api && api->EndFrameCapture();
}

uint32_t NumCaptures() {
  const RenderDocAPI* api = Api();
  return api ? api->NumCaptures() : 0;
}

std::string CapturePath(uint32_t index) {
  const RenderDocAPI* api = Api();
  return api ? api->CapturePath(index) : std::string();
}

}  // namespace renderdoc

}  // namespace ui
}  // namespace rex
