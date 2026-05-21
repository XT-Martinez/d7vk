#include "d3d7_direct_renderer.h"

#include "d3d7_device.h"

#include "../../util/log/log.h"
#include "../../util/util_string.h"

namespace dxvk {

  D3D7DirectRenderer::D3D7DirectRenderer(D3D7Device* pDevice)
    : m_device(pDevice) {
    Logger::info("D3D7DirectRenderer: direct Vulkan path enabled (phase 0 — no-op skeleton; entry points still route to D3D9)");
  }

  D3D7DirectRenderer::~D3D7DirectRenderer() {
  }

}
