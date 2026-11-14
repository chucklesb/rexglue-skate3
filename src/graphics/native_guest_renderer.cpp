#include <rex/graphics/native_guest_renderer.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>

#include <rex/logging.h>

#include <rex/cvar.h>

// Defined here (shared TU) rather than in a backend's command_processor.cpp
// so both D3D12 and Vulkan see the same definition.
REXCVAR_DEFINE_BOOL(native_render_suppress_emulated_draws, true, "GPU",
                    "While the registered native guest-output renderer is actively "
                    "replacing frames, skip emulated draw and resolve execution in the "
                    "command processor (PM4 parsing, fences, queries and memexport draws "
                    "still run). Menus/pause yield to the emulated path and execute "
                    "normally.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_INT32(native_render_suppress_mode, 2, "GPU",
                     "Which emulated passes to suppress while the native guest-output "
                     "renderer is active. 0 = framebuffer-sized passes only (surface "
                     "pitch >= 1280); the game's whole postfx chain then still executes "
                     "at 1152x640 x resolution scale EVERY frame and paces the pipeline "
                     "(~half the achievable frame rate). 1 = suppress everything (perf "
                     "probing; mid-gameplay lightmap page composition breaks). 2 = "
                     "suppress everything EXCEPT the memory-composition passes whose "
                     "outputs the native renderer samples from guest memory: lightmap "
                     "page composition (pitch 1024) and small composite surfaces (pitch "
                     "<= 512, CAS outfit pieces). Menus/pause/loading always render "
                     "fully (the native renderer yields there), so shop/outfit "
                     "composition is unaffected by any mode. 3 = portrait-window mode: "
                     "like 0 the sub-framebuffer RTT passes execute (one-shot frontend "
                     "portrait renders, census pitches 560-1200), but the 1152-wide "
                     "main scene + postfx band stays suppressed; that band is the "
                     "whole-pipeline cost at scaled resolutions and portraits never "
                     "need it (that band is pitch 1200: 1152 rounded up to 80-pixel "
                     "EDRAM tiles). 4 = scene-photo mode: like 3, but the 1152x640 "
                     "band executes too, for one-shot renders into it that the game "
                     "reads back.")
    .range(0, 4)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_INT32(native_render_suppress_extra_pitch, 0, "GPU",
                     "While the native guest-output renderer is active, also suppress "
                     "the emulated passes (draws and resolves) at exactly this surface "
                     "pitch, on top of native_render_suppress_mode. 0 = none. For an "
                     "app to drop a known-unused pass class in a context where it is "
                     "the only one at that pitch (Skate 3: the replay's per-frame aux "
                     "skater passes at 160).")
    .range(0, 8192)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_INT32(native_render_suppress_exempt_pitch, 0, "GPU",
                     "While the native guest-output renderer is active, let the emulated "
                     "passes (draws and resolves) at exactly this surface pitch EXECUTE "
                     "whatever native_render_suppress_mode says. 0 = none. For an app to "
                     "let a one-shot pass through briefly (Skate 3: the team photo atlas, "
                     "1536x512 at pitch 1600, rebuilt in the skater-photo window). "
                     "Depth-only draws there still follow "
                     "native_render_suppress_exempt_depth_only.")
    .range(0, 8192)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(native_render_suppress_exempt_depth_only, true, "GPU",
                    "Within the suppression-EXEMPT passes (see "
                    "native_render_suppress_mode), also skip draws with no pixel shader "
                    "(depth/stencil-only: shadow-map casters, z-prepasses). Their output "
                    "feeds only the suppressed scene passes; the native renderer builds "
                    "its own shadows, and on Vulkan this stream is the dominant "
                    "remaining emulated GPU cost (2-12 ms/frame at 3x, the bimodal-FPS "
                    "slow state). Disable if lightmap-page or CAS composition content "
                    "regresses (a composition pass depth/stencil-testing against its own "
                    "no-PS lay-down would need this off).")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(native_render_pass_census, false, "GPU",
                    "Diagnostics: count emulated draws by surface pitch (suppressed, "
                    "executed under the native renderer, executed with it inactive) "
                    "for the app to take per context. Off costs one cvar read per draw.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(native_render_pass_census_detail, false, "GPU",
                    "Diagnostics (Vulkan): log each distinct render-target setup (pitch, "
                    "MSAA, EDRAM bases, scissor, resolve destination) of the emulated "
                    "draws and resolves that still execute under the native renderer, "
                    "once each.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace rex::graphics {
namespace {

std::mutex g_detail_mu;
std::unordered_set<uint64_t> g_detail_seen;

constexpr uint32_t kCensusPitches = 8192;
std::atomic<uint32_t> g_census_draws[kCensusPitches][kPassCensusKinds];
std::atomic<uint64_t> g_census_indices[kCensusPitches][kPassCensusKinds];
std::atomic<uint32_t> g_census_native_frames{0};
std::atomic<uint32_t> g_census_emulated_frames{0};

std::atomic<NativeGuestOutputRenderer> g_renderer{nullptr};
std::atomic<void*> g_renderer_user_data{nullptr};
// True while the registered renderer actually replaced the last presented
// frame (false when it yields: menus, early-outs, no renderer).
std::atomic<bool> g_native_output_active{false};
std::atomic<NativeGuestOutputPostProcessor> g_post_processor{nullptr};
std::atomic<void*> g_post_processor_user_data{nullptr};
std::atomic<bool> g_post_process_requested{false};

}  // namespace

void SetNativeGuestOutputRenderer(NativeGuestOutputRenderer renderer, void* user_data) {
  g_renderer_user_data.store(user_data, std::memory_order_release);
  g_renderer.store(renderer, std::memory_order_release);
}

bool TryRenderNativeGuestOutput(const NativeGuestOutputRenderContext& context) {
  NativeGuestOutputRenderer renderer = g_renderer.load(std::memory_order_acquire);
  if (renderer == nullptr) {
    g_native_output_active.store(false, std::memory_order_relaxed);
    return false;
  }
  void* user_data = g_renderer_user_data.load(std::memory_order_acquire);
  const bool rendered = renderer(context, user_data);
  g_native_output_active.store(rendered, std::memory_order_relaxed);
  if (REXCVAR_GET(native_render_pass_census)) {
    (rendered ? g_census_native_frames : g_census_emulated_frames)
        .fetch_add(1, std::memory_order_relaxed);
  }
  return rendered;
}

bool HasNativeGuestOutputRenderer() {
  return g_renderer.load(std::memory_order_acquire) != nullptr;
}

void SetNativeGuestOutputPostProcessor(NativeGuestOutputPostProcessor post_processor,
                                       void* user_data) {
  g_post_processor_user_data.store(user_data, std::memory_order_release);
  g_post_processor.store(post_processor, std::memory_order_release);
}

bool HasNativeGuestOutputPostProcessor() {
  return g_post_processor.load(std::memory_order_acquire) != nullptr;
}

void InvokeNativeGuestOutputPostProcessor(const NativeGuestOutputRenderContext& context) {
  NativeGuestOutputPostProcessor post_processor = g_post_processor.load(std::memory_order_acquire);
  if (post_processor == nullptr) {
    return;
  }
  post_processor(context, g_post_processor_user_data.load(std::memory_order_acquire));
}

void RequestNativeGuestOutputPostProcess(bool requested) {
  g_post_process_requested.store(requested, std::memory_order_release);
}

bool IsNativeGuestOutputPostProcessRequested() {
  return g_post_process_requested.load(std::memory_order_acquire);
}

bool IsNativeGuestOutputActive() {
  return g_native_output_active.load(std::memory_order_relaxed);
}

namespace {
std::atomic<double> g_wide_aspect{0.0};
}  // namespace

void SetNativeGuestOutputWideAspect(double aspect) {
  g_wide_aspect.store(aspect > 0.0 ? aspect : 0.0, std::memory_order_relaxed);
}

double GetNativeGuestOutputWideAspect() {
  return g_wide_aspect.load(std::memory_order_relaxed);
}

bool ApplyNativeGuestOutputWideAspect(uint32_t& guest_output_width, uint32_t guest_output_height,
                                      uint32_t& display_width, uint32_t& display_height) {
  const double aspect = g_wide_aspect.load(std::memory_order_relaxed);
  if (aspect <= 0.0 || !guest_output_width || !guest_output_height ||
      !HasNativeGuestOutputRenderer() || !g_native_output_active.load(std::memory_order_relaxed)) {
    return false;
  }
  // Even width keeps every half-resolution derived target (bloom, SSAO,
  // menu snapshots) an exact divide.
  uint32_t wide_width =
      uint32_t(std::clamp(aspect * double(guest_output_height) + 0.5, 1.0, 16384.0)) & ~1u;
  if (wide_width <= guest_output_width) {
    return false;
  }
  guest_output_width = wide_width;
  // The display aspect is the actual pixel aspect: the presenter maps the
  // wide image edge-to-edge on a matching window.
  display_width = guest_output_width;
  display_height = guest_output_height;
  return true;
}

bool ShouldSuppressEmulatedDraws() {
  return REXCVAR_GET(native_render_suppress_emulated_draws) &&
         g_native_output_active.load(std::memory_order_relaxed);
}

bool ShouldSuppressExemptDepthOnlyDraws() {
  return REXCVAR_GET(native_render_suppress_exempt_depth_only);
}

bool ShouldSuppressPassAtPitch(uint32_t surface_pitch) {
  const int32_t extra = REXCVAR_GET(native_render_suppress_extra_pitch);
  if (extra > 0 && surface_pitch == uint32_t(extra)) {
    return true;
  }
  const int32_t exempt = REXCVAR_GET(native_render_suppress_exempt_pitch);
  if (exempt > 0 && surface_pitch == uint32_t(exempt)) {
    return false;
  }
  switch (REXCVAR_GET(native_render_suppress_mode)) {
    case 0:
      return surface_pitch >= 1280;
    case 1:
      return true;
    case 3:
      // Portrait-window mode: the one-shot frontend portrait RTTs (Skate 3:
      // the 512x512 card at pitch 560, plus 800/640/600 + small mips)
      // execute, while the main scene/postfx band, the whole-pipeline cost
      // at scaled resolutions, stays suppressed like the framebuffer. That
      // band's pitch is 1200, not 1152: EDRAM surfaces are allocated in
      // 80-pixel tiles, so 1152 rounds up (until 2026-11 only 1152 was
      // excluded, and the whole emulated scene ran in every window).
      return surface_pitch >= 1152;
    case 4:
      // Scene-photo mode: like 3 but the 1152x640 band (pitch 1200)
      // executes too, for one-shot renders INTO that surface whose resolve
      // the game reads back (Skate 3's skater photo).
      return surface_pitch >= 1280 || surface_pitch == 1152;
    default:
      // Execute only the memory-composition passes the native renderer
      // samples: lightmap page composition (1024) and small composite
      // surfaces (<= 512, CAS outfit pieces). The <= 512 window also lets a
      // few tiny postfx pyramid mips through, negligible, and safer than
      // guessing which small surfaces matter.
      return !(surface_pitch == 1024 || surface_pitch <= 512);
  }
}

bool PassCensusEnabled() {
  return REXCVAR_GET(native_render_pass_census);
}

bool PassCensusDetailEnabled() {
  return REXCVAR_GET(native_render_pass_census_detail);
}

void NotePassDetail(const PassDetail& d) {
  const uint32_t f[] = {d.resolve,      d.pitch,        d.msaa,        d.color_base,
                        d.color_format, d.depth_base,   d.scissor_br_x, d.scissor_br_y,
                        d.dest_base,    d.dest_pitch,   d.dest_height};
  uint64_t h = 1469598103934665603ull;
  for (uint32_t v : f) {
    h = (h ^ v) * 1099511628211ull;
  }
  for (int i = 0; i < 4; ++i) {
    h = (h ^ d.tex_base[i]) * 1099511628211ull;
  }
  {
    std::lock_guard<std::mutex> lk(g_detail_mu);
    if (g_detail_seen.size() >= 4096 || !g_detail_seen.insert(h).second) {
      return;
    }
  }
  if (d.resolve) {
    REXLOG_INFO(
        "pass detail: RESOLVE pitch={} msaa={} color_base={} fmt={} scissor={}x{} "
        "dest=0x{:08X} {}x{}",
        d.pitch, d.msaa, d.color_base, d.color_format, d.scissor_br_x, d.scissor_br_y,
        d.dest_base, d.dest_pitch, d.dest_height);
  } else {
    std::string tex;
    for (int i = 0; i < 4; ++i) {
      if (d.tex_base[i] != 0) {
        tex += fmt::format(" tex=0x{:08X}/{}x{}/f{}", d.tex_base[i], d.tex_w[i], d.tex_h[i],
                           d.tex_fmt[i]);
      }
    }
    REXLOG_INFO(
        "pass detail: draw pitch={} msaa={} color_base={} fmt={} depth_base={} scissor={}x{}{}",
        d.pitch, d.msaa, d.color_base, d.color_format, d.depth_base, d.scissor_br_x,
        d.scissor_br_y, tex);
  }
}

void NotePassCensus(uint32_t surface_pitch, PassCensusKind kind, uint32_t index_count) {
  const uint32_t p = std::min(surface_pitch, kCensusPitches - 1);
  const uint32_t k = uint32_t(kind);
  g_census_draws[p][k].fetch_add(1, std::memory_order_relaxed);
  g_census_indices[p][k].fetch_add(index_count, std::memory_order_relaxed);
}

size_t TakePassCensus(PassCensusEntry* out, size_t max_entries, uint32_t* native_frames,
                      uint32_t* emulated_frames) {
  size_t n = 0;
  for (uint32_t p = 0; p < kCensusPitches; ++p) {
    PassCensusEntry e;
    bool any = false;
    for (uint32_t k = 0; k < kPassCensusKinds; ++k) {
      e.draws[k] = g_census_draws[p][k].exchange(0, std::memory_order_relaxed);
      e.indices[k] = g_census_indices[p][k].exchange(0, std::memory_order_relaxed);
      any |= e.draws[k] != 0;
    }
    if (any && n < max_entries) {
      e.pitch = p;
      out[n++] = e;
    }
  }
  if (native_frames) {
    *native_frames = g_census_native_frames.exchange(0, std::memory_order_relaxed);
  }
  if (emulated_frames) {
    *emulated_frames = g_census_emulated_frames.exchange(0, std::memory_order_relaxed);
  }
  return n;
}

}  // namespace rex::graphics

namespace rex::graphics::nrhi {
namespace {

// Set once during app startup before any CreateShader (see native_rhi.h);
// lives in this shared TU so every backend reads the same value.
std::string g_shader_bytecode_cache_dir;

}  // namespace

void SetShaderBytecodeCacheDirectory(const char* path) {
  g_shader_bytecode_cache_dir = path != nullptr ? path : "";
}

const char* GetShaderBytecodeCacheDirectory() {
  return g_shader_bytecode_cache_dir.c_str();
}

}  // namespace rex::graphics::nrhi
