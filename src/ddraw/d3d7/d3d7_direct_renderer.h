#pragma once

#include "../ddraw_include.h"

namespace dxvk {

  class D3D7Device;

  /**
  * \brief D3D7 -> Vulkan direct renderer (phase 0 skeleton).
  *
  * Optional rendering path that bypasses the D3D9/DXVK D3D9 layer and
  * targets DxvkContext directly. Constructed only when the
  * `D7VK_DIRECT_VULKAN` env var is set on a D3D7Device.
  *
  * Phase 0 lays down only the class shell and ownership wiring. No
  * draw/state methods exist yet; D3D7Device entry points still
  * forward to D3D9 unconditionally. Real direct-path logic is
  * introduced in phase 1+ — see docs/d3d7-vulkan-direct-scoping.md.
  */
  class D3D7DirectRenderer {

  public:

    explicit D3D7DirectRenderer(D3D7Device* pDevice);

    ~D3D7DirectRenderer();

    D3D7DirectRenderer             (const D3D7DirectRenderer&) = delete;
    D3D7DirectRenderer& operator = (const D3D7DirectRenderer&) = delete;

  private:

    D3D7Device* m_device = nullptr;

  };

}
