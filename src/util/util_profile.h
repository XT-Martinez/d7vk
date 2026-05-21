#pragma once

#include <cstdint>

#include "util_time.h"

namespace dxvk {

  enum class ProfileEvent : uint32_t {
    DDraw7Blt,
    DDraw7BltFast,
    DDraw7Flip,
    DDraw7Lock,
    DDraw7Unlock,
    DDraw7DownloadSurfaceData,
    DDraw7UploadSurfaceData,
    DDraw7InitializeOrUploadD3D9,
    DDraw7BltProxy,
    DDraw7LockProxy,
    DDraw7LockFullWriteOnlySkip,
    DDraw7BltWindowedPrimary,
    DDraw7BltWindowedPrimaryRenderTarget,
    DDraw7BltSourceDownload,
    DDraw7BltDestDownload,
    DDraw7BltFastWindowedPrimary,
    DDraw7BltFastWindowedPrimaryRenderTarget,
    DDraw7BltFastSourceDownload,
    DDraw7BltFastDestDownload,
    DDraw7LockFullSurface,
    DDraw7LockWriteOnly,
    DDraw7LockReadOnly,
    DDraw7LockDiscard,
    DDraw7LockDownload,
    DDraw7LockCurrentRenderTarget,
    DDraw7LockCurrentRenderTargetSkip,
    TransferToD3D9SurfaceD3D9Lock,
    TransferToD3D9SurfaceDDrawLock,
    TransferToD3D9SurfaceCopy,
    TransferToD3D9SurfaceUnlock,
    TransferToDDrawSurfaceDDrawLock,
    TransferToDDrawSurfaceD3D9Lock,
    TransferToDDrawSurfaceCopy,
    TransferToDDrawSurfaceUnlock,
    DDraw7DirectRenderTargetLock,
    DDraw7DirectRenderTargetUnlock,
    D3D7BeginScene,
    D3D7EndScene,
    D3D7Clear,
    D3D7SetTransform,
    D3D7SetRenderState,
    D3D7DrawPrimitive,
    D3D7DrawIndexedPrimitive,
    D3D7DrawPrimitiveStrided,
    D3D7DrawIndexedPrimitiveStrided,
    D3D7DrawPrimitiveVB,
    D3D7DrawIndexedPrimitiveVB,
    D3D7SetTexture,
    D3D7SetRenderStateDedup,
    D3D7SetTextureDedup,
    D3D7SetTextureStageStateDedup,
    D3D7DrawPrimitiveBatchAppend,
    D3D7DrawPrimitiveBatchFlush,
    Count,
  };

  class Profiler {

  public:

    static bool enabled();

    static void record(ProfileEvent event, uint64_t durationNs);

  };

  class ProfileScope {

  public:

    explicit ProfileScope(ProfileEvent event)
    : m_event(event), m_start(Profiler::enabled() ? high_resolution_clock::now() : high_resolution_clock::time_point()) { }

    ~ProfileScope() {
      if (Profiler::enabled()) {
        auto end = high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_start).count();
        Profiler::record(m_event, uint64_t(ns));
      }
    }

    ProfileScope             (const ProfileScope&) = delete;
    ProfileScope& operator = (const ProfileScope&) = delete;

  private:

    ProfileEvent m_event;
    high_resolution_clock::time_point m_start;

  };

}
