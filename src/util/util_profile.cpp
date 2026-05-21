#include "util_profile.h"

#include <array>
#include <atomic>
#include <sstream>

#include "util_env.h"
#include "log/log.h"

namespace dxvk {

  namespace {

    struct ProfileCounter {
      std::atomic<uint64_t> calls   = { 0u };
      std::atomic<uint64_t> totalNs = { 0u };
      std::atomic<uint64_t> maxNs   = { 0u };
    };

    constexpr uint32_t EventCount = uint32_t(ProfileEvent::Count);

    std::array<ProfileCounter, EventCount> g_counters;
    std::atomic<uint64_t> g_nextDumpNs = { 0u };

    constexpr std::array<const char*, EventCount> g_names = {{
      "DDraw7Surface::Blt",
      "DDraw7Surface::BltFast",
      "DDraw7Surface::Flip",
      "DDraw7Surface::Lock",
      "DDraw7Surface::Unlock",
      "DDraw7Surface::DownloadSurfaceData",
      "DDraw7Surface::UploadSurfaceData",
      "DDraw7Surface::InitializeOrUploadD3D9",
      "DDraw7Surface::Blt proxy",
      "DDraw7Surface::Lock proxy",
      "DDraw7Surface::Lock full-writeonly skip",
      "DDraw7Surface::Blt windowed primary",
      "DDraw7Surface::Blt windowed primary render-target",
      "DDraw7Surface::Blt source download",
      "DDraw7Surface::Blt dest download",
      "DDraw7Surface::BltFast windowed primary",
      "DDraw7Surface::BltFast windowed primary render-target",
      "DDraw7Surface::BltFast source download",
      "DDraw7Surface::BltFast dest download",
      "DDraw7Surface::Lock full-surface",
      "DDraw7Surface::Lock writeonly",
      "DDraw7Surface::Lock readonly",
      "DDraw7Surface::Lock discard",
      "DDraw7Surface::Lock download",
      "DDraw7Surface::Lock current render-target",
      "DDraw7Surface::Lock current render-target skip",
      "Transfer::BlitToD3D9Surface D3D9 lock",
      "Transfer::BlitToD3D9Surface DDraw lock",
      "Transfer::BlitToD3D9Surface copy",
      "Transfer::BlitToD3D9Surface unlock",
      "Transfer::BlitToDDrawSurface DDraw lock",
      "Transfer::BlitToDDrawSurface D3D9 lock",
      "Transfer::BlitToDDrawSurface copy",
      "Transfer::BlitToDDrawSurface unlock",
      "DDraw7Surface::Direct render-target lock",
      "DDraw7Surface::Direct render-target unlock",
      "D3D7Device::BeginScene",
      "D3D7Device::EndScene",
      "D3D7Device::Clear",
      "D3D7Device::SetTransform",
      "D3D7Device::SetRenderState",
      "D3D7Device::DrawPrimitive",
      "D3D7Device::DrawIndexedPrimitive",
      "D3D7Device::DrawPrimitiveStrided",
      "D3D7Device::DrawIndexedPrimitiveStrided",
      "D3D7Device::DrawPrimitiveVB",
      "D3D7Device::DrawIndexedPrimitiveVB",
      "D3D7Device::SetTexture",
      "D3D7Device::SetRenderState dedup",
      "D3D7Device::SetTexture dedup",
      "D3D7Device::SetTextureStageState dedup",
      "D3D7Device::DrawPrimitive batch append",
      "D3D7Device::DrawPrimitive batch flush",
    }};

    uint64_t nowNs() {
      return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        high_resolution_clock::now().time_since_epoch()).count());
    }

    uint64_t intervalNs() {
      static uint64_t interval = [] {
        std::string value = env::getEnvVar("D7VK_PROFILE_INTERVAL");
        if (value.empty())
          return uint64_t(5'000'000'000ull);

        char* end = nullptr;
        double seconds = std::strtod(value.c_str(), &end);

        if (end == value.c_str() || seconds <= 0.0)
          return uint64_t(5'000'000'000ull);

        return uint64_t(seconds * 1'000'000'000.0);
      }();

      return interval;
    }

    void updateMax(std::atomic<uint64_t>& target, uint64_t value) {
      uint64_t old = target.load(std::memory_order_relaxed);
      while (old < value && !target.compare_exchange_weak(old, value,
             std::memory_order_relaxed, std::memory_order_relaxed)) {
      }
    }

    void dump(uint64_t next) {
      std::stringstream s;
      s << "D7VK profile summary over ~"
        << double(intervalNs()) / 1'000'000'000.0
        << "s:";

      for (uint32_t i = 0; i < EventCount; i++) {
        uint64_t calls = g_counters[i].calls.exchange(0u, std::memory_order_relaxed);
        uint64_t total = g_counters[i].totalNs.exchange(0u, std::memory_order_relaxed);
        uint64_t max   = g_counters[i].maxNs.exchange(0u, std::memory_order_relaxed);

        if (!calls)
          continue;

        double totalMs = double(total) / 1'000'000.0;
        double avgUs   = double(total) / double(calls) / 1'000.0;
        double maxUs   = double(max) / 1'000.0;

        s << "\n  " << g_names[i]
          << ": calls=" << calls
          << ", total=" << totalMs << " ms"
          << ", avg=" << avgUs << " us"
          << ", max=" << maxUs << " us";
      }

      Logger::info(s.str());
      g_nextDumpNs.store(next, std::memory_order_relaxed);
    }

  }

  bool Profiler::enabled() {
    static bool enabled = [] {
      std::string value = env::getEnvVar("D7VK_PROFILE");
      return value == "1" || value == "true" || value == "True" || value == "TRUE";
    }();

    return enabled;
  }

  void Profiler::record(ProfileEvent event, uint64_t durationNs) {
    if (!enabled())
      return;

    uint32_t index = uint32_t(event);
    if (index >= EventCount)
      return;

    g_counters[index].calls.fetch_add(1u, std::memory_order_relaxed);
    g_counters[index].totalNs.fetch_add(durationNs, std::memory_order_relaxed);
    updateMax(g_counters[index].maxNs, durationNs);

    uint64_t now = nowNs();
    uint64_t next = g_nextDumpNs.load(std::memory_order_relaxed);

    if (!next) {
      uint64_t desired = now + intervalNs();
      g_nextDumpNs.compare_exchange_strong(next, desired, std::memory_order_relaxed);
      return;
    }

    if (now >= next && g_nextDumpNs.compare_exchange_strong(next, now + intervalNs(),
        std::memory_order_relaxed)) {
      dump(now + intervalNs());
    }
  }

}
