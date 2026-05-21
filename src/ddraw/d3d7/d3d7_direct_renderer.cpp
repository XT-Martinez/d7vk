#include "d3d7_direct_renderer.h"

#include "d3d7_device.h"

#include "../../d3d9/d3d9_bridge.h"

#include "../../util/log/log.h"
#include "../../util/util_env.h"
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

    {
      std::string value = env::getEnvVar("D7VK_DIRECT_CLEAR_DEBUG");
      m_directClearDebug = value == "1" || value == "true" || value == "True" || value == "TRUE";
    }

    Logger::info(str::format(
      "D3D7DirectRenderer: direct Vulkan path enabled — DxvkDevice acquired",
      m_directClearDebug ? " (DIRECT_CLEAR_DEBUG: forcing magenta clears)" : ""));
  }

  D3D7DirectRenderer::~D3D7DirectRenderer() {
  }


  bool D3D7DirectRenderer::Clear(DWORD flags, DWORD count, D3DRECT* rects, D3DCOLOR color,
                                 float z, DWORD stencil) {
    if (unlikely(!IsActive()))
      return false;

    // Rect-clears are not implemented yet; full fallback.
    if (count != 0 || rects != nullptr)
      return false;

    bool didColor = false;
    if (flags & D3DCLEAR_TARGET) {
      float r, g, b, a;
      if (unlikely(m_directClearDebug)) {
        // Magenta: visually distinct from anything uaRO clears to.
        r = 1.0f; g = 0.0f; b = 1.0f; a = 1.0f;
      } else {
        // D3DCOLOR is 0xAARRGGBB.
        a = ((color >> 24) & 0xff) / 255.0f;
        r = ((color >> 16) & 0xff) / 255.0f;
        g = ((color >>  8) & 0xff) / 255.0f;
        b = ( color        & 0xff) / 255.0f;
      }

      m_device->GetBridge()->EmitDirectClearRenderTarget(r, g, b, a);
      didColor = true;
      flags &= ~D3DCLEAR_TARGET;
    }

    // Anything left (depth/stencil) is delegated to D3D9's Clear.
    if (flags != 0) {
      auto* d9 = m_device->GetCommonD3DDevice()->GetD3D9Device();
      HRESULT hr = d9->Clear(0, nullptr, flags, color, z, stencil);
      if (unlikely(FAILED(hr))) {
        if (!didColor)
          return false;
        Logger::debug("D3D7DirectRenderer::Clear: D3D9 depth/stencil Clear failed");
      }
    }

    if (didColor && unlikely(!m_directClearLogged)) {
      m_directClearLogged = true;
      Logger::info("D3D7DirectRenderer::Clear: first color clear routed through DxvkContext");
    }

    return true;
  }

}
