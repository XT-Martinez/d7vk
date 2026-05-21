# D3D7 → Vulkan Direct Path — Scoping Doc

Status: scoping / design draft. Nothing implemented yet. Companion to
[`optimizations.md`](optimizations.md), which explains *why* this is the
only remaining lever to close the dgVoodoo gap on uaRO.

## TL;DR

Today the call chain is:

```
game → DDraw / D3D7 (this repo) → D3D9 (DXVK D3D9, src/d3d9)
     → DXVK runtime (src/dxvk) → Vulkan
```

For each game-issued draw, the D3D9 layer does a full per-draw
`PrepareDraw` pass that re-validates VBs, textures, samplers, dirty
state, FF VS/FS keys, etc. uaRO issues ~3,750 D3D7 state/draw calls per
frame; ~10 ms/frame of CPU is unaccounted in our own wrappers and falls
into that path. dgVoodoo (D3D7 → D3D11) handles the same scene at 450
FPS — i.e. ~2.2 ms total — which makes per-call cost the dominant cost.

The proposal is a **fourth, optional rendering path** in D7VK:

```
game → DDraw / D3D7 (this repo) → DxvkContext / DxvkDevice (src/dxvk)
                                → Vulkan
```

Gated by `D7VK_DIRECT_VULKAN=1`, falling back to the existing D3D9 path
for anything not yet implemented. Goal: keep the FF/texture/state model
of D3D7 but pay state validation, pipeline lookup, and resource binding
cost **once per frame** (or once per real state change), not once per
draw.

## Success Criteria

In order of priority:

1. **Correctness floor:** uaRO renders identical frames to the current
   build on the same map.
2. **Perf floor:** crowded-area FPS strictly better than the D3D9 path
   (target ≥ 150 FPS; minimum acceptable is no regression).
3. **Perf goal:** within 2× of dgVoodoo on the same scene (≥ 225 FPS).
   Hitting parity (~450 FPS) is a stretch goal — dgVoodoo also benefits
   from talking native D3D11 to a real Windows driver instead of going
   through Wine.
4. **No regression** for any other D3D7 title currently in d7vk's
   compatibility set: keep the D3D9 path as the default. Flag is opt-in.

## What Stays The Same

We keep, with no changes:

- All DDraw surface code (`src/ddraw/ddraw{,2,3,4,7}/`). Surface
  creation, blits, locks, color keys, palette handling — these are
  orthogonal to the draw path.
- D3D3/5/6 device wrappers (`src/ddraw/d3d{3,5,6}/`). They forward to
  D3D7Device; the direct path lives inside D3D7Device.
- The D3D9 path (`src/d3d9/`). It remains the default and the fallback
  for things the direct path doesn't yet implement.
- DXVK itself (`src/dxvk/`). We *consume* it as a library — same way
  the D3D9 layer does. No DXVK changes expected.
- Profiler scaffolding (`src/util/util_profile.{h,cpp}`). We add new
  event IDs but the framework is reused.
- The DDraw → D3D9 swapchain emulation: presenting still goes via the
  existing D3D9 swapchain. We blit our rendered Vulkan image into the
  D3D9 back buffer at `EndScene`. (Long-term we can own the swapchain
  too, but that's out of scope for v1.)

## What Gets Replaced (in D3D7Device only)

When `m_directVulkan` is true, these entry points stop forwarding to
`m_device9` and instead drive a new `D3D7DirectRenderer`:

- `SetRenderState`, `SetTextureStageState`, `SetTexture`
- `SetTransform`, `MultiplyTransform`, `SetViewport`
- `SetMaterial`, `SetLight`, `LightEnable`, `SetClipPlane`
- `BeginScene`, `EndScene`, `Clear`, `SetRenderTarget`
- `DrawPrimitive`, `DrawIndexedPrimitive`
- `DrawPrimitiveStrided`, `DrawIndexedPrimitiveStrided`
- `DrawPrimitiveVB`, `DrawIndexedPrimitiveVB`
- `BeginStateBlock`, `EndStateBlock`, `ApplyStateBlock`,
  `CaptureStateBlock`, `CreateStateBlock`, `DeleteStateBlock`

The existing dedup caches in `D3D7Device` become *the* state cache for
the direct path — no longer a parallel cache.

## Target Architecture

```
D3D7Device
  ├─ if (m_directVulkan)
  │    └─ D3D7DirectRenderer  ──► DxvkContext / DxvkDevice
  └─ else
       └─ m_device9 (D3D9DeviceEx)  ──► DxvkContext / DxvkDevice
```

`D3D7DirectRenderer` is the new class. It owns:

- A `Rc<DxvkDevice>` (obtained via the D3D9 device's interop or, more
  cleanly, from a new DxvkInstance handle held by the DDraw interface).
- A persistent `Rc<DxvkContext>` for our draw stream. Whether we share
  the D3D9 device's context or get a second one is **OPEN Q #1** —
  sharing avoids submission-ordering hazards with the swapchain blit
  but means cooperating with the D3D9 layer; not sharing is cleaner
  but needs an explicit barrier on the present image.
- A pipeline cache (FF VS/FS keys → `Rc<DxvkGraphicsPipeline>`).
- A vertex/index ring allocator backed by host-visible DxvkBuffers.
- A sampler cache (`D3DTSS` mapped state → `Rc<DxvkSampler>`).
- Per-stage texture binding state (DDraw7Surface → `DxvkImageView`).
- A per-frame command set: bound RT, bound DS, dirty flags.

Conceptually it is a slimmed-down `D3D9DeviceEx` that knows D3D7 is
**all** fixed-function, **never** programmable, has **8** texture
stages at most, **no** stream sources beyond stream 0, and a much
smaller render-state matrix.

## Major Subsystems To Build

### 1. Pipeline state and FF shader emulation

D3D7 is always FF VS + FS. The key fits in a few words:

- VS key bits: lighting on/off, specular, fog mode, # of lights, vertex
  blend mode, vertex format flags (XYZ vs XYZRHW, normal present,
  diffuse present, specular present, # tex coord sets).
- PS key bits: per-stage colorop/alphaop/arg0/arg1/arg2 (texop stage
  count up to 8), texcoord index per stage, tex transform flags, alpha
  test func+ref (or via spec const), fog mode.

Strategy: **reuse DXVK's D3D9 FF shader generator** (`src/d3d9/
d3d9_fixed_function.cpp`, 3000 LOC). It is the right shape — already
generates SPIR-V for D3D9's FVF/TSS world. Two options:

- **(A) Adapter** — translate D3D7 state into the D3D9 FF key structs
  (`D3D9FFShaderKeyVS`, `D3D9FFShaderKeyFS`) and call
  `D3D9FFShaderModuleSet::GetShaderModule`. Pros: ~zero new shader code.
  Cons: drags in the D3D9 `D3D9DeviceEx` because the FF generator's
  constructors take a `D3D9DeviceEx*`. We'd have to instantiate a
  minimal D3D9 device just for FF shader gen, or refactor the FF gen
  out of D3D9.
- **(B) Fork** — copy `d3d9_fixed_function.{cpp,h}` into
  `src/ddraw/d3d7_direct/` and trim. Pros: clean separation, no D3D9
  dependency at runtime. Cons: ~3000 LOC of duplicated work.
- **(C) Minimal generator** — write a from-scratch FF generator that
  only covers what D3D7 (and uaRO in particular) actually uses. Pros:
  smallest, fastest cache hits. Cons: real risk of subtle FF
  incompatibilities later.

Recommendation: **start with (A)** using a stub `D3D9DeviceEx`-shaped
adapter, ship perf numbers, then move to (B) only if (A)'s wiring cost
shows up in profiles.

### 2. Vertex / index buffer streaming

D3D7's `DrawPrimitive` is `DrawPrimitiveUP`-shaped: user pointer + FVF,
no buffer object. We don't want to do `AllocUPBuffer` per draw the way
D3D9 does (look at `src/d3d9/d3d9_device.cpp:3194` — that's a per-draw
slice allocation).

Design:

- Single per-frame **ring buffer** for UP vertex data, ~16 MB host
  visible, mapped persistently.
- Per-draw: `memcpy` the verts into the ring at the current head,
  record `(offset, stride, count)` for the recorded command.
- Wrap → seal and recycle when fence-safe. Same pattern as DXVK's
  staging buffer.
- For `DrawPrimitiveVB` (D3D7 vertex buffer object), the vertex data
  already lives in a `D3D7VertexBuffer` backed by a DxvkBuffer — we
  bind that directly, no copy.

Index buffers:

- We already maintain the per-size pool (`m_ib9[]` in `D3D7Device`).
  Replace those with our own DxvkBuffer pool keyed on size; same
  XS/S/M/L/XL bucket idea.

### 3. Texture / sampler binding

DDraw7Surface already has a D3D9 backing texture. The direct path
needs its **DxvkImage / DxvkImageView**. Two routes:

- **Reuse** the existing D3D9 texture (it's already a DxvkImage under
  the hood; `D3D9CommonTexture::GetImage()`). Grab the image from the
  D3D9 texture pointer cached on the DDraw surface and hand its view
  to `DxvkContext::bindResourceImageView`.
- Or own a separate set of DxvkImages just for the direct path.

The first is much less work and avoids double-uploads for textures
that are also used by the proxy path (Blt, lock-readback). Pick that.

Samplers: walk the dedup TSS cache, hash to a small struct, look up in
a `std::unordered_map<SamplerKey, Rc<DxvkSampler>>`. ~Few hundred unique
samplers across a game's lifetime.

### 4. Render targets and depth-stencil

`SetRenderTarget` on D3D7Device today calls into a D3D9 surface. For
the direct path:

- Resolve the DDraw7Surface to its underlying DxvkImage.
- Defer the actual `bindRenderTargets` call to the next `BeginScene` /
  first draw — we don't want to rebind RTs more than once per scene.
- `Clear` becomes a `clearRenderTarget` on the bound DxvkImageView; if
  it lands before any draw it can be merged with the LOAD_OP.

Depth-stencil: same pattern, view of `m_ds`.

### 5. Constant buffers (FF state)

D3D9 FF emulation needs UBO-resident state: viewport/transforms,
material, lights array, per-stage TSS state, fog parameters, alpha
test ref, etc. We allocate **one persistent host-visible UBO** per
frame, write the whole FF state block into it on dirty, bind it
once. Spec constants (texture types, projected mask, etc.) are pushed
via `DxvkContext::setSpecConstant`-style API, same as D3D9.

### 6. State block recording

D3D7 state blocks capture render state, TSS, transforms, light state,
texture bindings, viewport. Our existing `D3D7StateBlock` records into
the D3D9 device. For the direct path we re-record into the direct
renderer's shadow state. Tractable, but tedious — large state table.

### 7. Submission and synchronization

The D3D9 swapchain still owns Present. We need to either:

- Render into a DxvkImage that **is** the D3D9 back buffer
  (`D3D9SwapChainEx::GetBackBuffer` → DxvkImage), so Present picks up
  our draws natively, **or**
- Render into our own DxvkImage and blit/copy to the D3D9 back buffer
  at EndScene.

The first is faster and avoids a copy but requires that we use the
**same** DxvkContext as the D3D9 device, otherwise we have to insert
explicit cross-queue / cross-context barriers. That ties our submission
ordering to D3D9's. **OPEN Q #1** above.

The second is simpler and what I'd recommend for v1.

## Phasing (incremental — each phase shippable behind the flag)

Phase 0 — **Skeleton + fallback**
- Add `D7VK_DIRECT_VULKAN` flag, `D3D7DirectRenderer` class, hook all
  the entry points listed above with `if (m_directVulkan) ...`.
- All direct paths currently just forward to `m_device9` (no behavior
  change). Lets us land plumbing without correctness risk.
- Ship: identical perf, identical pixels.

Phase 1 — **Clear / Present plumbing**
- Direct `Clear` via `DxvkContext::clearRenderTarget`.
- EndScene blits our DxvkImage to D3D9 back buffer; D3D9 presents.
- Other draws still forward to D3D9.
- Ship: black screen with our clear color; verifies submission/blit
  pipeline.

Phase 2 — **Vertex streaming + tri-list draws**
- Implement `D3DPT_TRIANGLELIST` (and LINELIST/POINTLIST) only,
  FVF restricted to `XYZ`/`XYZRHW`/diffuse/1 set of texcoords.
- FF VS + FS via option (A) above (reuse D3D9 FF generator).
- Texture binding for stage 0 only.
- Forward everything else (strips, fans, multi-texture, etc.) to D3D9.
- Ship: HUD / 2D sprites in uaRO render correctly via direct path,
  rest of frame via D3D9. Measure perf delta on the subset.

Phase 3 — **Full primitive type coverage + 8 stages**
- Triangle strips, fans, line strips.
- Multi-texture, all TSS ops we see in uaRO.
- Indexed draws (DIP, DIPS, DIPVB).
- Ship: most of uaRO frame on direct path. This is where the perf
  numbers start being meaningful.

Phase 4 — **Lighting / transforms / fog**
- Full FF lighting (8 lights), all transform stack semantics, fog
  modes, vertex blend.
- Ship: every D3D7 game in our compat set that was working on D3D9
  path also works on direct.

Phase 5 — **State blocks**
- Replay against direct shadow state.

Phase 6 — **Drop D3D9 fallback** (only after phase 4 is stable)

## Effort Estimate

Very rough, in calendar weeks of focused work by one person familiar
with the codebase:

| phase | scope | est. weeks |
| --- | --- | --- |
| 0 | skeleton, flag, plumbing | 0.5 |
| 1 | clear / present, blit | 0.5 – 1 |
| 2 | tri-list + minimal FF | 2 – 3 |
| 3 | full primitives + multi-texture + indexed | 2 – 3 |
| 4 | lights / transforms / fog parity | 2 – 4 |
| 5 | state blocks | 1 |
| 6 | cleanup, drop fallback | 0.5 |
| **total to phase 4 (uaRO playable)** |  | **7 – 11 weeks** |
| **total to feature parity** |  | **8 – 12 weeks** |

The longest pole is phase 4 (FF light/transform parity). Most uaRO
draws don't seem to use full FF lighting — phase 2/3 may already be
enough to measure the win on this specific game.

## Risks And Unknowns

1. **OPEN Q #1 — DxvkContext sharing with D3D9.** Either we share
   (tight coupling, submission-order constraints) or run independent
   (need explicit barriers around the swapchain image). Prototype phase
   1 will answer this.

2. **FF generator coupling.** The D3D9 FF generator (option A) wants a
   `D3D9DeviceEx*` for things like shader option flags and pipeline
   layout. We may end up needing a "FF generator interface" refactor
   in DXVK D3D9. Pre-emptive refactor would be ~1 week.

3. **DDraw lock semantics on RTs we now own.** Currently `D7VK_DIRECT_
   CURRENT_RT_LOCK` works because the D3D9 backing IS the truth. If we
   render into a separate DxvkImage, locks on the RT have to be
   intercepted at the DDraw7Surface layer to read back from our image
   instead. Solvable, but it's an extra hook.

4. **Color key bridge.** D3D7's color key is currently a D9 bridge
   feature (`d3d9_bridge.cpp`). The direct path needs to either invoke
   the bridge's color-key state on the direct renderer or re-implement
   color keying as a spec constant + shader branch. The D9 FF generator
   already supports the latter — likely free under option A.

5. **Compatibility risk for other titles.** d7vk supports D3D7 games
   beyond uaRO. Keeping the D3D9 path as the default fallback (via the
   flag) means we don't have to solve the long tail before shipping a
   uaRO win.

6. **Perf may underperform the estimate.** dgVoodoo is on native
   Windows D3D11. We're on Linux/Wine/Vulkan. Even with a thin direct
   path, Wine syscall / scheduler overhead may eat some of the savings.
   This is unmeasurable until phase 3.

7. **Maintenance cost.** Two render paths permanently is more code to
   keep in sync. Phase 6 (drop D3D9 fallback) is what makes this
   sustainable. If we never get there, the cost is real.

## Recommendation

Build phase 0+1+2 first as a **time-boxed experiment** (~3 weeks). At
the end of phase 2 we will have:

- Direct-path draws emitting correctly for a subset of uaRO's frame.
- Hard numbers on how much per-draw cost is actually reclaimable
  (compare `D3D7Device::DrawPrimitive` avg µs direct vs D3D9 path).
- A concrete answer to OPEN Q #1.

If phase-2 numbers show ≥ 2× per-draw improvement on the direct
subset, commit to phases 3–4. If they don't, the FF refactor and ring
allocator are still worth keeping (we can drop them back into the D9
path), and we revisit whether the D9 PrepareDraw cost is actually
elidable a different way (e.g. patches to DXVK D3D9 to fast-path the
unchanged-state case).

## Files We'll Touch / Create

New:

- `src/ddraw/d3d7/d3d7_direct_renderer.{cpp,h}` — the new renderer
- `src/ddraw/d3d7/d3d7_direct_pipeline.{cpp,h}` — pipeline cache
- `src/ddraw/d3d7/d3d7_direct_streaming.{cpp,h}` — ring allocator
- `src/ddraw/d3d7/d3d7_direct_state.{cpp,h}` — FF state → spirv key
- `docs/d3d7-vulkan-direct-progress.md` — phase tracking

Modified:

- `src/ddraw/d3d7/d3d7_device.{cpp,h}` — branch on `m_directVulkan`
- `src/ddraw/d3d7/meson.build` — wire new TUs
- `src/util/util_profile.{h,cpp}` — direct-path event IDs
- Possibly `src/d3d9/d3d9_fixed_function.{cpp,h}` — if we factor out
  the FF generator's D3D9 device dependency

Out of scope for v1:

- D3D3/5/6 routing through the direct path (they go through D3D7Device
  already, so they come along for free if their feature subset is in
  phase 4).
- Programmable shaders — D3D7 has none.
- Multi-RT / MRT — D3D7 has none.

## Appendix — Reference Pointers

- D3D9 per-draw cost: `src/d3d9/d3d9_device.cpp:7680` (`PrepareDraw`),
  `src/d3d9/d3d9_device.cpp:3171` (`DrawPrimitiveUP`).
- D3D9 FF shader generator: `src/d3d9/d3d9_fixed_function.{cpp,h}`.
- DxvkContext draw API: `src/dxvk/dxvk_context.h` —
  `bindRenderTargets`, `bindShader`, `bindVertexBuffer`,
  `bindIndexBuffer`, `bindResourceImageView`, `bindResourceSampler`,
  `setRasterizerState`, `setBlendMode`, `setMultisampleState`,
  `setLogicOpState`, `setInputLayout`, `draw`, `drawIndexed`,
  `clearRenderTarget`.
- D3D7 entry points the renderer must handle:
  `src/ddraw/d3d7/d3d7_device.h:50-142`.
- Existing dedup caches (will become the direct path's shadow state):
  `src/ddraw/d3d7/d3d7_device.h:237-263`.
