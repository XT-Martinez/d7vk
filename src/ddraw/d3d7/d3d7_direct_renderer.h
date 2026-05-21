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

  private:

    // Non-owning. Lifetime is bound by the D3D9 device that the D3D7Device
    // holds; that device keeps a Rc<DxvkDevice> alive for us.
    D3D7Device*  m_device     = nullptr;
    DxvkDevice*  m_dxvkDevice = nullptr;

  };

}
