#include "d3d7_direct_renderer.h"

#include "d3d7_device.h"

#include "../../d3d9/d3d9_bridge.h"

#include "../../util/log/log.h"
#include "../../util/util_string.h"

namespace dxvk {

  D3D7DirectRenderer::D3D7DirectRenderer(D3D7Device* pDevice)
    : m_device(pDevice) {
    if (likely(m_device->GetBridge() != nullptr))
      m_dxvkDevice = m_device->GetBridge()->GetDXVKDevice();

    if (unlikely(m_dxvkDevice == nullptr)) {
      Logger::warn("D3D7DirectRenderer: failed to acquire DxvkDevice via bridge; direct path will not engage");
      return;
    }

    Logger::info("D3D7DirectRenderer: direct Vulkan path enabled — DxvkDevice acquired (phase 1.1 — entry points still route to D3D9)");
  }

  D3D7DirectRenderer::~D3D7DirectRenderer() {
  }

}
