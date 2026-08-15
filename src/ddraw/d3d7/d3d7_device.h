#pragma once

#include "../ddraw_include.h"
#include "../ddraw_wrapped_object.h"
#include "../ddraw_options.h"
#include "../ddraw_util.h"
#include "../ddraw_caps.h"

#include "../d3d_common_device.h"
#include "../d3d_light.h"

#include "../../d3d9/d3d9_bridge.h"

#include "../../util/sync/sync_scoped.h"

#include "d3d7_interface.h"

#include <array>
#include <unordered_map>
#include <vector>

namespace dxvk {

  class D3DCommonDevice;
  class DDrawCommonInterface;
  class DDraw7Surface;
  class D3D7StateBlock;

  using D3DMultithread = sync::ScopedDeviceLock;
  using D3DDeviceLock  = sync::ScopedDeviceGuard;

  /**
  * \brief D3D7 device implementation
  */
  class D3D7Device final : public DDrawWrappedObject<D3D7Interface, IDirect3DDevice7> {

  friend class D3D7StateBlock;

  public:
    D3D7Device(
          D3DCommonDevice* commonD3DDevice,
          Com<IDirect3DDevice7>&& d3d7DeviceProxy,
          D3D7Interface* pParent,
          GUID deviceGUID,
          const d3d9::D3DPRESENT_PARAMETERS* pParams9,
          Com<d3d9::IDirect3DDevice9>&& pDevice9,
          DDraw7Surface* pRT,
          DWORD CreationFlags9);

    ~D3D7Device();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject);

    HRESULT STDMETHODCALLTYPE GetCaps(D3DDEVICEDESC7 *desc);

    HRESULT STDMETHODCALLTYPE EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK cb, void *ctx);

    HRESULT STDMETHODCALLTYPE BeginScene();

    HRESULT STDMETHODCALLTYPE EndScene();

    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D7 **d3d);

    HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirectDrawSurface7 *surface, DWORD flags);

    HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirectDrawSurface7 **surface);

    HRESULT STDMETHODCALLTYPE Clear(DWORD count, D3DRECT *rects, DWORD flags, D3DCOLOR color, D3DVALUE z, DWORD stencil);

    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix);

    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix);

    HRESULT STDMETHODCALLTYPE SetViewport(D3DVIEWPORT7 *data);

    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix);

    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT7 *data);

    HRESULT STDMETHODCALLTYPE SetMaterial(D3DMATERIAL7 *data);

    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL7 *data);

    HRESULT STDMETHODCALLTYPE SetLight(DWORD idx, D3DLIGHT7 *data);

    HRESULT STDMETHODCALLTYPE GetLight(DWORD idx, D3DLIGHT7 *data);

    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE dwRenderStateType, DWORD dwRenderState);

    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE dwRenderStateType, LPDWORD lpdwRenderState);

    HRESULT STDMETHODCALLTYPE BeginStateBlock();

    HRESULT STDMETHODCALLTYPE EndStateBlock(LPDWORD lpdwBlockHandle);

    HRESULT STDMETHODCALLTYPE PreLoad(IDirectDrawSurface7 *surface);

    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE primitive_type, DWORD fvf, void *pVertex, DWORD vertexCount, DWORD flags);

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE primitive_type, DWORD fvf, void *pVertex, DWORD vertexCount, WORD *pIndex, DWORD indexCount, DWORD flags);

    HRESULT STDMETHODCALLTYPE SetClipStatus(D3DCLIPSTATUS *clip_status);

    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS *clip_status);

    HRESULT STDMETHODCALLTYPE DrawPrimitiveStrided(D3DPRIMITIVETYPE d3dptPrimitiveType, DWORD dwVertexTypeDesc, LPD3DDRAWPRIMITIVESTRIDEDDATA lpVertexArray, DWORD dwVertexCount, DWORD dwFlags);

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveStrided(D3DPRIMITIVETYPE d3dptPrimitiveType, DWORD dwVertexTypeDesc, LPD3DDRAWPRIMITIVESTRIDEDDATA lpVertexArray, DWORD  dwVertexCount, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags);

    HRESULT STDMETHODCALLTYPE DrawPrimitiveVB(D3DPRIMITIVETYPE primitive_type, IDirect3DVertexBuffer7 *vb, DWORD startVertex, DWORD primitiveCount, DWORD flags);

    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE primitive_type, IDirect3DVertexBuffer7 *vb, DWORD startVertex, DWORD primitiveCount, WORD *pIndex, DWORD indexCount, DWORD flags);

    HRESULT STDMETHODCALLTYPE ComputeSphereVisibility(D3DVECTOR *lpCenters, D3DVALUE *lpRadii, DWORD dwNumSpheres, DWORD dwFlags, DWORD *lpdwReturnValues);

    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirectDrawSurface7 **surface);

    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirectDrawSurface7 *surface);

    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD dwStage, D3DTEXTURESTAGESTATETYPE d3dTexStageStateType, LPDWORD lpdwState);

    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD dwStage, D3DTEXTURESTAGESTATETYPE d3dTexStageStateType, DWORD dwState);

    HRESULT STDMETHODCALLTYPE ValidateDevice(LPDWORD lpdwPasses);

    HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD dwBlockHandle);

    HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD dwBlockHandle);

    HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD dwBlockHandle);

    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE d3dsbType, LPDWORD lpdwBlockHandle);

    HRESULT STDMETHODCALLTYPE Load(IDirectDrawSurface7 *dst_surface, POINT *dst_point, IDirectDrawSurface7 *src_surface, RECT *src_rect, DWORD flags);

    HRESULT STDMETHODCALLTYPE LightEnable(DWORD dwLightIndex, BOOL bEnable);

    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD dwLightIndex, BOOL *pbEnable);

    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD dwIndex, D3DVALUE *pPlaneEquation);

    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD dwIndex, D3DVALUE *pPlaneEquation);

    HRESULT STDMETHODCALLTYPE GetInfo(DWORD info_id, void *info, DWORD info_size);

    HRESULT InitializeRTAndDS();

    void UpdateSurfaceDirtyTracking(bool dirtyRenderTarget, bool dirtyDepthStencil, bool dirtyPrimarySurface);

    HRESULT ResetD3D9Swapchain(d3d9::D3DPRESENT_PARAMETERS* params);

    // Public internal entry point (not COM) that flushes any pending batched
    // draw, forwarding to the private batching implementation.
    void FlushPendingDraws();

    D3DCommonDevice* GetCommonD3DDevice() {
      return m_commonD3DDevice.ptr();
    }

    D3DDeviceLock LockDevice() {
      return m_multithread.acquire();
    }

    DDraw7Surface* GetRenderTarget() const {
      return m_rt.ptr();
    }

    DDraw7Surface* GetDepthStencil() const {
      return m_ds.ptr();
    }

    void GetD3D9ActiveLights(std::vector<d3d9::D3DLIGHT9>* lights9) {
      for (const auto& [idx, light9] : m_lights) {
        if (m_lightsStates[idx])
          lights9->push_back(light9);
      }
    }

  private:

    inline void DDrawDirtySurfaceUpload();

    inline bool ShouldRecord() const { return m_recorder != nullptr; }

    void InvalidateDedupCaches();

    void FlushDrawBatch();

    // Append vertices into the active batch, expanding triangle strips and
    // fans into triangle-list form so they can coalesce with regular tri-list
    // draws. Caller must have already started the batch with effective
    // primitive type D3DPT_TRIANGLELIST (or D3DPT_LINELIST / D3DPT_POINTLIST
    // for the line/point cases, which pass through unchanged).
    void AppendVerticesToBatch(D3DPRIMITIVETYPE sourcePrim, const void* vertices, uint32_t vertexCount, uint32_t fvfSize);

    // SetFVF dedup: the underlying D3D9 SetFVF does a hashmap lookup
    // (m_fvfTable) on every call before early-outing on an unchanged
    // declaration. Every draw entry point sets the FVF, so in Ragnarok Online
    // this fires thousands of times per frame, almost always with the same
    // value. Cache the last value set and skip the COM call when unchanged.
    // Routed through here by all draw paths; invalidated in
    // InvalidateDedupCaches() (device reset).
    void SetD3D9FVF(d3d9::IDirect3DDevice9* device9, DWORD fvf);

    inline void RefreshLastUsedDevice() {
      if (unlikely(m_commonD3DDevice != m_commonIntf->GetCommonD3DDevice()))
        m_commonIntf->SetCommonD3DDevice(m_commonD3DDevice.ptr());
    }

    Com<D3DCommonDevice>            m_commonD3DDevice;

    DDrawCommonInterface*           m_commonIntf     = nullptr;

    Com<IDxvkLegacyD3DDeviceBridge> m_bridge;

    D3DMultithread                  m_multithread;

    D3DDEVICEDESC7                  m_desc;

    Com<DDraw7Surface>              m_rt;
    Com<DDraw7Surface, false>       m_ds;

    std::array<Com<DDraw7Surface, false>, ddrawCaps::TextureStageCount> m_textures;

    std::unordered_map<DWORD, d3d9::D3DLIGHT9> m_lights;
    std::unordered_map<DWORD, BOOL>            m_lightsStates;

    D3D7StateBlock*                 m_recorder       = nullptr;
    DWORD                           m_recorderHandle = 0u;
    DWORD                           m_handle         = 0u;
    std::unordered_map<DWORD, D3D7StateBlock> m_stateBlocks;

    std::array<Com<d3d9::IDirect3DIndexBuffer9>, ddrawCaps::IndexBufferCount> m_ib9;

    // Dedup caches for D3D9-side state. Indexed by the post-translation D3D9
    // state enum. Sized to comfortably cover the highest enum value in use.
    static constexpr uint32_t MaxD3D9RenderState       = 256;
    static constexpr uint32_t MaxD3D9SamplerState      = 16;
    static constexpr uint32_t MaxD3D9TextureStageState = 64;

    DWORD m_d3d9RsCache[MaxD3D9RenderState]                                    = { };
    bool  m_d3d9RsCacheValid[MaxD3D9RenderState]                               = { };

    DWORD m_d3d9SsCache[ddrawCaps::TextureStageCount][MaxD3D9SamplerState]     = { };
    bool  m_d3d9SsCacheValid[ddrawCaps::TextureStageCount][MaxD3D9SamplerState]= { };

    DWORD m_d3d9TssCache[ddrawCaps::TextureStageCount][MaxD3D9TextureStageState]    = { };
    bool  m_d3d9TssCacheValid[ddrawCaps::TextureStageCount][MaxD3D9TextureStageState] = { };

    // D3D7-input dedup caches. Hot-path optimization: when the game re-emits
    // the same (type, value) pair, we short-circuit at the very top of
    // SetRenderState / SetTextureStageState — no translation, no LockDevice.
    // Keyed on the raw D3D7 input; only populated for types that the slow
    // path treats as 1:1 translations (no side effects beyond a single D3D9
    // SetRenderState / SetTextureStageState / SetSamplerState call).
    static constexpr uint32_t MaxD3D7RenderState       = 256;
    static constexpr uint32_t MaxD3D7TextureStageState = 32;

    DWORD m_d3d7RsCache[MaxD3D7RenderState]                                    = { };
    bool  m_d3d7RsCacheValid[MaxD3D7RenderState]                               = { };

    DWORD m_d3d7TssCache[ddrawCaps::TextureStageCount][MaxD3D7TextureStageState] = { };
    bool  m_d3d7TssCacheValid[ddrawCaps::TextureStageCount][MaxD3D7TextureStageState] = { };

    d3d9::IDirect3DBaseTexture9* m_d3d9TexCache[ddrawCaps::TextureStageCount]  = { };
    bool                         m_d3d9TexCacheValid[ddrawCaps::TextureStageCount] = { };

    // Color-key bridge state cached at stage 0.
    DDraw7Surface*        m_colorKeyLastSurface     = nullptr;
    bool                  m_colorKeyLastEnable      = false;
    bool                  m_colorKeyLastValid       = false;
    DWORD                 m_colorKeyLastLow         = 0;
    DWORD                 m_colorKeyLastHigh        = 0;
    bool                  m_colorKeyCacheValid      = false;

    // DrawPrimitive batching. Consecutive DrawPrimitive calls with the
    // same primitive type and FVF are accumulated into a single vertex blob
    // and emitted as one DrawPrimitiveUP call. Flushed on any state change,
    // scene boundary, or alternative draw entry point.
    bool                  m_batchActive       = false;
    D3DPRIMITIVETYPE      m_batchPrimType     = D3DPT_TRIANGLELIST;
    DWORD                 m_batchFvf          = 0;
    uint32_t              m_batchFvfSize      = 0;
    uint32_t              m_batchVertexCount  = 0;
    std::vector<uint8_t>  m_batchVertices;

    // SetFVF dedup cache. See SetD3D9FVF.
    DWORD                 m_lastSetFvf        = 0;
    bool                  m_lastSetFvfValid   = false;

    // GetFVFSize memoization. The FVF is near-constant in Ragnarok Online
    // (changes ~twice/frame) yet DrawPrimitive recomputes the size by walking
    // the FVF bits on every call (thousands/frame). Cache the last result.
    DWORD                 m_fvfSizeKey        = 0;
    uint32_t              m_fvfSizeVal        = 0;
    bool                  m_fvfSizeValid      = false;

    inline uint32_t CachedFVFSize(DWORD fvf) {
      if (likely(m_fvfSizeValid && m_fvfSizeKey == fvf))
        return m_fvfSizeVal;
      m_fvfSizeVal   = uint32_t(GetFVFSize(fvf));
      m_fvfSizeKey   = fvf;
      m_fvfSizeValid = true;
      return m_fvfSizeVal;
    }

  };

}
