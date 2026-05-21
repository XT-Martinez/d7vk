#pragma once

#include "../ddraw_include.h"

namespace dxvk {

  class D3D7Device;
  class DxvkDevice;

  /**
  * \brief D3D7 -> Vulkan direct renderer.
  *
  * Optional rendering path that bypasses the D3D9/DXVK D3D9 layer and
  * targets DxvkContext directly. Constructed only when the
  * `D7VK_DIRECT_VULKAN` env var is set on a D3D7Device.
  *
  * Currently a phase 1 work-in-progress: the renderer acquires the
  * shared DxvkDevice but does not yet route any draw/state entry
  * points. See docs/d3d7-vulkan-direct-scoping.md for the plan.
  */
  class D3D7DirectRenderer {

  public:

    explicit D3D7DirectRenderer(D3D7Device* pDevice);

    ~D3D7DirectRenderer();

    D3D7DirectRenderer             (const D3D7DirectRenderer&) = delete;
    D3D7DirectRenderer& operator = (const D3D7DirectRenderer&) = delete;

    // True once the renderer has acquired all dependencies and is safe to
    // route entry points through.
    bool IsActive() const { return m_dxvkDevice != nullptr; }

    // Phase 1.2: route the color portion of Clear through
    // DxvkContext::clearRenderTarget directly. Any depth/stencil portion
    // is delegated to D3D9 in the same call. Returns true if the caller
    // should consider the Clear handled (do NOT also call D9's Clear).
    // Returns false to fall through to D9 entirely (e.g. for rect-clears
    // we haven't implemented yet).
    bool Clear(DWORD flags, DWORD count, D3DRECT* rects, D3DCOLOR color,
               float z, DWORD stencil);

  private:

    // Non-owning. Lifetime is bound by the D3D9 device that the D3D7Device
    // holds; that device keeps a Rc<DxvkDevice> alive for us.
    D3D7Device*  m_device     = nullptr;
    DxvkDevice*  m_dxvkDevice = nullptr;

    bool m_directClearLogged = false;
    // Debug: when D7VK_DIRECT_CLEAR_DEBUG=1, force every direct color
    // clear to magenta so we can visually confirm our DxvkContext writes
    // are what reach the screen.
    bool m_directClearDebug  = false;

  };

}
