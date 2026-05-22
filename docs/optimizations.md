# D7VK Optimizations On Top Of Upstream

This document summarizes the optimization work landed on top of upstream D7VK
(`origin/devel`) while profiling Ragnarok Online (uaRO) under Wine. It is
intended as a handoff brief for further work — in particular, a possible
D3D7 → Vulkan direct path that bypasses the D3D9/DXVK layer entirely.

Companion reading: [`ragnarok-profiling.md`](ragnarok-profiling.md) covers how
to build, install, and capture profile data with the artifacts described
below.

## Target And Baseline

- Game: uaRO (Ragnarok Online client), windowed.
- Platform: Linux + Wine + Bottles (PROTON prefix), Vulkan back end via DXVK.
- Same crowded map, similar camera, used as the steady-state benchmark.
- GPU utilization in that scene: **~12–15%**. CPU-bound.
- Reference: dgVoodoo (D3D7 → D3D11 wrapper, separate stack) reaches **~450–500 FPS**
  in the same scene.

| build | login / quiet | crowded in-game |
| --- | --- | --- |
| upstream `origin/devel` | ~230 FPS | **~60–80 FPS** (MangoHud) |
| optimized (this branch) | ~444 FPS (BeginScene profile) | **~80–90 FPS** (MangoHud) |
| dgVoodoo (reference) | not measured | **~450–500 FPS** |

The remaining gap is firmly in **per-call overhead in the D3D7 → D3D9 → DXVK
→ Vulkan stack**, not GPU work. See "Where the remaining time goes" below.

## What Landed (3 commits)

```
c2dafaa2 [meta] Add Podman build container, .gitignore and Ragnarok profiling notes
49c76e0b [meta] Add opt-in D7VK aggregate profiler and direct render-target lock
77f70491 [d3d7] State-cache dedup and opt-in DrawPrimitive batching
```

### 1. Profiling scaffolding (`49c76e0b`)

New aggregate-counter profiler in `src/util/util_profile.{h,cpp}` with:

- `D7VK_PROFILE=1` to enable, `D7VK_PROFILE_INTERVAL=<seconds>` (default 5s)
- per-event {call count, total ns, avg ns, max ns}
- periodic `D7VK profile summary` block dumped to the D7VK log
- `ProfileScope` RAII helper, zero overhead when profiler is off

Hooks added in `DDraw7Surface::Blt/Lock/Unlock`, transfer helpers
(`ddraw_format.h`), and selected `D3D7Device` entry points. All hooks
disabled at runtime unless `D7VK_PROFILE=1`.

This was the foundation. Without it, none of the diagnoses below would
have been possible.

### 2. Direct current-RT lock (`49c76e0b`, opt-in)

Flag: `D7VK_DIRECT_CURRENT_RT_LOCK=1`.

In `src/ddraw/ddraw7/ddraw7_surface.cpp`, when the game locks the
*current* render-target surface, we route the lock straight to the
backing D3D9 surface's `LockRect` (and the matching `UnlockRect`),
bypassing the Wine DDraw-side proxy upload / download round trip.

**Result:**

- Game loading / login screen: ~230 FPS → ~444 FPS (BeginScene-rate).
- Crowded in-game: **negligible** — the lock+unlock cost is the same
  ~951 µs lock + ~408 µs unlock whether at login or in-game (the GPU
  fence wait wasn't actually load-dependent), but it's only ~10 % of
  in-game frame time. The big win is only visible when the rest of the
  frame is cheap.

It is opt-in because it changes correctness semantics for unusual
patterns (anything that depended on the proxy DDraw mirror seeing the
last frame's pixels). Verified visually correct on uaRO.

### 3. State-cache dedup (`77f70491`, always on)

In `src/ddraw/d3d7/d3d7_device.{cpp,h}`, `D3D7Device` now caches the
live post-translation D3D9 value for:

- render state (per `D3DRENDERSTATETYPE`)
- sampler state (per stage × `D3DSAMPLERSTATETYPE`)
- texture stage state (per stage × `D3DTEXTURESTAGESTATETYPE`)
- bound texture pointer (per stage)

`SetRenderState`, `SetTextureStageState`, and `SetTexture` short-circuit
when the value is unchanged. Recording state blocks (`ShouldRecord()`)
bypasses dedup (D3D9 defers state while a state block is open) and
caches are invalidated on `ApplyStateBlock`. `SetTexture` preserves its
existing color-key bridge calls regardless of dedup hit.

**Result:** measurably high hit rates (~46 % of `SetRenderState`,
variable for TSS) but **no measurable FPS improvement** — DXVK's D3D9
layer already optimizes redundant state sets, so what we skip was
already nearly free downstream. The dedup adds cheap cache-check
overhead but nothing more in release builds. Kept in tree because (a)
correct, (b) free, (c) infrastructure for future per-call work.

### 4. DrawPrimitive batching (`77f70491`, opt-in)

Flag: `D7VK_BATCH_DRAWS=1`.

Consecutive `DrawPrimitive` calls with the same primitive type **and**
the same FVF are accumulated into a single vertex blob and emitted as
one `DrawPrimitiveUP` call. Currently restricted to:

- `D3DPT_TRIANGLELIST`
- `D3DPT_LINELIST`
- `D3DPT_POINTLIST`

(`D3DPT_TRIANGLESTRIP/FAN/LINESTRIP` are *not* batched — see "What didn't
work" below.)

Flushed before:

- any actual state change (`SetRenderState`, `SetTexture`, `SetTextureStageState`,
  `SetTransform`, `MultiplyTransform`, `SetViewport`, `SetMaterial`,
  `SetLight`, `LightEnable`, `SetClipPlane`, color-key bridge updates),
- scene boundaries (`BeginScene`, `EndScene`),
- `Clear`,
- any non-batched draw entry point (`DrawIndexedPrimitive`,
  `DrawPrimitiveStrided`, `DrawIndexedPrimitiveStrided`, `DrawPrimitiveVB`,
  `DrawIndexedPrimitiveVB`),
- state-block apply, swapchain reset.

**Result, in busy in-game scene with `D7VK_BATCH_DRAWS=1`:**

- ~3.8 % of game `DrawPrimitive` calls were actually batchable (this
  game uses mostly triangle strips/fans for sprites/quads). Of those,
  ~30 % got merged.
- **~78 FPS → ~83 FPS** (BeginScene rate), **~80 FPS → ~90 FPS** (MangoHud).
- Visually identical, no regressions observed.

Modest win for the size of the change, but real. The win comes from
collapsing (N–1) `PrepareDraw` cycles in DXVK D3D9 per merged batch (see
`src/d3d9/d3d9_device.cpp:7680`), not from vertex-upload bandwidth.

## What Didn't Work (rolled back, not in any commit)

**Universal primitive-to-triangle-list expansion** behind the same
`D7VK_BATCH_DRAWS=1` flag — converting incoming `TRIANGLESTRIP/FAN` and
`LINESTRIP` into the corresponding list form (with strip winding
preserved) so *everything* could batch together.

Mechanically it worked: batch-append rate jumped from 3.8 % to ~41 %.
But on uaRO it was a **net loss** (~80 FPS → ~70 FPS) because:

- The game issues a `SetTexture` roughly every 1.7 draws. Every non-dedup
  `SetTexture` forces a flush.
- Net effect: average batch size ~1.7 game-draws, so the batch state
  machine ran on every draw (instead of only on ~3.8 % of them) but
  almost never accumulated enough to amortize its own overhead.
- Per-call wrapper cost in `SetTexture` measurably doubled (0.59 µs →
  1.10 µs avg) because the flush ran inside it most of the time.

A bulk-`memcpy` expand and a 1 MB batch-byte cap were added to address
correctness/perf cliffs and *did* prevent the second symptom
(initial implementation deadlocked the loading screen by trying to do
N small `vector::insert` calls per strip on a strip with many vertices
— see `uaRO_ddraw.batch-expand-broken.log` for the post-mortem), but
the underlying flush-frequency problem was inherent to the game's call
pattern, not the implementation. Reverted.

Conclusion: simple draw-call batching at the D7VK layer can't claw back
much more — the next floor is the per-call cost in the D3D9 / DXVK / Vulkan
stack below us.

## Where The Remaining Time Goes

In-game busy area, ~76 FPS (13.2 ms/frame), GPU at 12–15 %:

| accounted-for | per frame | % of frame |
| --- | --- | --- |
| `Direct render-target lock` + `unlock` | ~1.36 ms | ~10 % |
| `D3D7Device::DrawPrimitive` wrapper | ~0.69 ms | ~5 % |
| `D3D7Device::SetTexture` wrapper | ~0.61 ms | ~5 % |
| `D3D7Device::SetRenderState` wrapper | ~0.13 ms | ~1 % |
| other D7VK wrappers | ~0.1 ms | <1 % |
| **profiled D7VK total** | **~2.9 ms** | **~22 %** |
| **unaccounted (D3D9 / DXVK / Vulkan / driver / wine)** | **~10.3 ms** | **~78 %** |

The game's per-frame call volume is what kills us:

- ~2,168 `DrawPrimitive` calls/frame
- ~1,269 `SetTexture` calls/frame
- ~310 `SetRenderState` calls/frame
- ~3,750 D3D7 state/draw calls per frame, total

dgVoodoo's 450 FPS in the same scene means it handles those same ~3,750
calls per frame in ~2.2 ms instead of ~13.2 ms. The gap **isn't in our
wrappers** — it's in everything we forward to.

DXVK's `D3D9DeviceEx::DrawPrimitiveUP` (see `src/d3d9/d3d9_device.cpp:3171`)
calls `PrepareDraw` per draw, which scans vertex-buffer / texture /
sampler / dirty-flag masks every time (`src/d3d9/d3d9_device.cpp:7680`).
Per-call PrepareDraw + UP-buffer alloc + EmitCs encoding looks to be
the dominant unaccounted cost.

That's the case for the D3D7 → Vulkan direct path: skip the D9 device
state machine entirely, talk to `DxvkContext` directly, do FF emulation
ourselves on a much tighter pipeline-state cache.

## Environment Flags Reference

| flag | default | effect |
| --- | --- | --- |
| `D7VK_PROFILE` | unset | `=1` enables the aggregate profiler |
| `D7VK_PROFILE_INTERVAL` | 5 | profile summary interval in seconds |
| `D7VK_LOG_LEVEL` | (DXVK default) | use `info` to capture profile dumps |
| `D7VK_LOG_PATH` | (DXVK default) | directory for D7VK log files |
| `D7VK_DIRECT_CURRENT_RT_LOCK` | unset | `=1` routes current-RT locks straight to D3D9 |
| `D7VK_RT_LOCK_SCRATCH` | unset | `=1` returns a scratch CPU buffer for current-RT direct locks and discards the game's writes at Unlock — eliminates D3D9 LockRect's mandatory copyImageToBuffer+WaitForResource. Verified visually correct on uaRO at login *and* busy area; produces 2.5× login FPS and ~1.24× busy-area FPS gains. Visual safety is per-game; only enable for titles that don't rely on the lock's read content or its writes landing on the RT. |
| `D7VK_BATCH_DRAWS` | unset | `=1` enables tri-list / line-list / point-list batching |
| `D7VK_ASSUME_CURRENT_RT_LOCK_DISCARD` | unset | experimental, **breaks uaRO**, do not use |

`d3d9.maxFrameRate` (DXVK config) remains the safest knob for stability;
the optimizations above stack on top of it.

## Files Touched

- `src/util/util_profile.{h,cpp}` — new profiler scaffolding
- `src/util/meson.build` — wire new translation unit
- `src/ddraw/ddraw_format.h` — profile hooks in transfer helpers
- `src/ddraw/ddraw7/ddraw7_surface.{cpp,h}` — direct RT lock + profile hooks
- `src/ddraw/d3d7/d3d7_device.{cpp,h}` — state dedup + DrawPrimitive batching
- `Containerfile.podman` — reproducible mingw32 build container
- `docs/ragnarok-profiling.md` — profiling workflow notes
- `.gitignore` — out\*/ artifact trees

## Suggested Next Steps

1. **D3D7 → Vulkan direct path** (`D7VK_DIRECT_VULKAN=1` style flag).
   Replace the D3D9 device entirely for hot draw paths: D7VK consumes
   `DrawPrimitive` and friends, produces `DxvkContext` calls directly,
   maintains its own fixed-function pipeline-state cache and Vulkan
   pipeline cache. Vertex / index buffer allocation via a ring buffer.
   FF emulation likely cribbed from DXVK's existing FF shader generator.
   Estimated scope: weeks of work to get Ragnarok rendering correctly;
   the only path with the right order-of-magnitude potential to close
   the dgVoodoo gap.

2. **Investigate `VK_SUBOPTIMAL_KHR` swapchain churn** seen in profile
   logs (clusters of 3–7 recreations during steady-state play). Likely
   related to D7VK's emulated windowed flip path. Probably small per-frame
   cost but bug-shaped and easy to verify.

3. **Per-frame draw-call distribution telemetry** — add per-primtype
   counters to the profiler so we can directly verify what fraction of
   uaRO's calls are strips vs lists, instead of inferring from batch
   hit rates.

Items 2 and 3 are cheap and would either be quick wins or sharpen the
case for item 1.

## Phase 1 — D7VK_DIRECT_VULKAN groundwork (committed: 6510aa71, a5e2e822, 900a1b75)

Three small commits laid the plumbing for the direct-Vulkan path:

- `D3D7DirectRenderer` skeleton class behind `D7VK_DIRECT_VULKAN=1`.
- `DxvkD3D8Bridge::GetDXVKDevice()` non-COM accessor that exposes the
  shared `DxvkDevice` to D7VK.
- `DxvkD3D8Bridge::EmitDirectClearRenderTarget` +
  `D3D9DeviceEx::EmitDirectClearRenderTarget` route the color portion
  of D3D7's `Clear` through `DxvkContext::clearRenderTarget`, while
  D9's `Clear` still handles any depth/stencil portion. Visually
  verified with `D7VK_DIRECT_CLEAR_DEBUG=1` (forces magenta clears).

Phase 1 answered OPEN Q #1: **sharing the D3D9 device's CS thread is
fine**. Our direct CS lambdas interleave correctly with D9's
submission and survive Present.

Cost-wise: phase 1 is a tiny regression for Clear (one operation
became two CS lambdas), but Clear is only ~98/frame so the absolute
impact is negligible.

## Phase 2.0 — Piggyback DrawPrimitive (rolled back, important finding)

**What we tried:** Add a `DrawPrimitiveUPFastPath` on `D3D9DeviceEx`
that's identical to `DrawPrimitiveUP` but skips `PrepareDraw`. Track
"has any D9 state-changing wrapper fired since the last draw?" via a
single `m_d9StateDirty` flag in `D3D7Device`. Route `DrawPrimitive`
through the fast path whenever state is clean and the FVF matches
what D9 last had. Mark dirty in every non-dedup state-set wrapper;
clear after every successful slow-path draw.

**What we measured on uaRO busy area:**

- Fast path engaged on **42%** of draws (~850/frame), fallback on 58%
  (~1150/frame). uaRO does roughly one `SetTexture` per two draws,
  which is what limits the fast-path share.
- `D3D7Device::DrawPrimitive` wrapper avg cost rose from
  **0.31 µs → 0.44 µs** (+0.13 µs / draw).
- FPS in busy area dropped from **~88-98 → ~80**.

**Conclusion — the perf model was wrong.** D9's `PrepareDraw` does
almost no work when nothing is dirty (most of its checks are gated on
dirty bits). The **~10 ms/frame "unaccounted" cost is not
`PrepareDraw`** — it's the actual `EmitCs` dispatch + CS-thread work +
Vulkan submission per draw, which the piggyback approach still pays.

This invalidates the "skip-PrepareDraw" angle. We rolled the change
back; the bridge accessor and the `D3D7DirectRenderer` shell from
phase 1 stay in place because the next attempt will reuse them.

## Updated Direction For The Next Pivot

Given the phase 2.0 finding, the genuine win has to attack per-draw
CS-thread / Vulkan submission cost, not D9 device state. Two attack
surfaces:

1. **CS-thread bundling** — emit one CS lambda that issues *many*
   `ctx->draw` calls, sharing the bound vertex buffer and pipeline.
   D7VK already does this at the wrapper layer for triangle/line/
   point lists (the `D7VK_BATCH_DRAWS=1` path) and observed a small
   win there. The reason it doesn't go further on uaRO is that the
   game's `SetTexture` between every ~2 draws flushes batches.

2. **Decouple texture bind from draw** — use descriptor indexing or
   texture-array + per-vertex texture index, so we can keep
   accumulating draws across `SetTexture` calls. This is the real
   unlock for uaRO's call pattern.

Either of these requires owning more of the pipeline: at minimum, our
own descriptor management and a way to encode the per-draw texture
selector. That's effectively the full FF rewrite from the scoping
doc, but with the *texture-array* approach as the central design
choice (not the state machine).

See `d3d7-vulkan-direct-scoping.md` for the broader plan. The pieces
that landed in phase 1 (renderer skeleton, bridge accessor, direct
clear path) are still applicable.

## Scratch RT Lock (the big lever)

After the wrapper-overhead analysis dead-ended (3 ms savings ceiling at
busy area, not enough to close the dgVoodoo gap), profiler instrumentation
of the D3D9 LockImage path on a render target revealed the actual
bottleneck: **`D3D9DeviceEx::LockImage` always submits a
`copyImageToBuffer` + `WaitForResource` for any surface with
`D3DUSAGE_RENDERTARGET`**, regardless of the game's lock-flag intent.
For uaRO at the login screen this is ~1.4 ms of GPU fence wait per
frame — 70% of the entire 2 ms/frame budget at 500 fps.

A diagnostic experiment confirmed the ceiling: replacing the D3D9
LockRect with a scratch CPU buffer (writes discarded) jumped login
from 500 → 1290 fps. The user-confirmed visual correctness in **both**
login and busy area — uaRO neither reads the lock's pixels nor relies
on its writes landing on the RT.

### Results

| scene | baseline (`origin/devel`) | post-pivot baseline (`900a1b75`) | with `D7VK_RT_LOCK_SCRATCH=1` | dgVoodoo reference |
| --- | --- | --- | --- | --- |
| login | ~230 fps | ~500 fps | **~1290 fps** | ~800 fps |
| busy area | ~60–80 fps | ~90 fps | **~112 fps** | ~100 fps |

Both targets exceeded.

### Why a proper sysmem-mirror version doesn't work

A "correct" implementation would lock a D3DPOOL_SYSTEMMEM mirror
surface (no GPU fence wait) and push the game's writes back to the RT
via `UpdateSurface` at Unlock. We tried this: it produced a **black
screen**. The reason is that the sysmem mirror starts blank/zero and
`UpdateSurface` unconditionally overwrites the entire RT with the
mirror's content — overwriting the GPU-rendered pixels with the
mirror's (mostly empty) state every frame.

A correct mirror approach would need per-region dirty tracking on the
CPU side, or a per-call WRITEONLY-flag gate plus per-region copies.
That's more code and per-frame overhead; the scratch path is correct
for uaRO and ~7% of the cost, so we ship that and document the
visual-safety constraint.

### What didn't work along the way

- **Forced D9 Flush at EndScene** (small regression). Theory: GPU
  isn't running between EndScene and Lock under tiny per-frame
  workloads, so force-flushing should let GPU work overlap the Lock
  wait. Actual: at login uaRO has effectively zero CPU work between
  EndScene and Lock; forcing an extra submission just adds Vulkan
  submit overhead with no overlap to hide it behind.
- **D7→D9 wrapper overhead optimization** (color-key cache, FVF
  dedup). Measurable wrappers ate ~5 ms/frame in busy area; we
  expected ~1.5 ms recoverable. But the regression we saw was
  actually profile-scope overhead from instrumentation, not the
  wrapper changes themselves. After ripping out the new ProfileScopes,
  the color-key cache landed neutral; FVF dedup wasn't independently
  re-tested. Both turned out to be the wrong attack surface — the
  real cost was the RT lock fence wait, which dominates the frame at
  every fps level.

## Personal-Fork Pivot (post-upstream rejection)

The upstream PR (`ddraw.directRenderTargetLock` /
`ddraw.assumeRenderTargetLockDiscard` as opt-in config options, on
the `upstream-pr` branch) was rejected. As of 2026-05-23 this repo is
repositioned as a personal fork specifically for Ragnarok Online
clients, so optimizations land unconditional rather than gated.

### Default-on baseline (`6a41d1bb`, `b3071585`)

Removed all env-var / config gates: the RT-lock scratch path,
`D3D7DirectRenderer` (Clear via DxvkContext), and `DrawPrimitive`
batching all fire unconditionally. The unused `m_directD3D9Lock`
field and its Unlock branch went away with the scratch path becoming
the only behavior. `D7VK_ASSUME_CURRENT_RT_LOCK_DISCARD` is also
gone — it was unmeasured and now-redundant.

The `D7VK_DIRECT_VULKAN` env var was retired alongside the bridge
between `D3D7Device` and `D3D7DirectRenderer` becoming
unconditional. The renderer is allocated on every device; only its
Clear-routing entry point is wired today, but the plumbing is ready
for further direct-path work.

Also stripped all `ProfileScope` / `Profiler::record` calls from
`d3d7_device.cpp`. At ~19k scope enter/exits per frame on
high-traffic paths (state setters + draws), even the disabled-mode
cost of `Profiler::enabled()` cross-TU function calls was burning
material time. The `util_profile.{h,cpp}` infrastructure stays so we
can re-add targeted scopes when diagnosing.

### Top-level input dedup for state setters (`6afc003b`)

**Profiling at a quiet spot** identified the next dominant cost
(profile build at 148 fps):

| method | calls/frame | μs/frame (profile ON) | μs/call |
| --- | --- | --- | --- |
| SetTextureStageState | 8,780 | 1,248 | 0.14 |
| SetRenderState | 2,002 | 708 | 0.35 |
| DrawPrimitive | 1,219 | 239 | 0.20 |
| SetTexture | 107 | 128 | 1.20 |
| DrawPrimitive flush | 23 | 18 | 0.79 |

**Key insight:** the per-call cost was already low (0.14 μs for TSS
hits), but call volume × 90 % dedup-hit rate meant the function was
paying the COM entry prelude, `D3DDeviceLock`, `Logger::debug`,
validity / range checks, the state-translation switch, lambda
construction, and `ConvertSamplerStateType` lookup on ~10k calls per
frame for hits the existing dedup catches *internally*.

Fix: hoist a fresh, D3D7-input-keyed dedup check to the very top of
`SetRenderState` and `SetTextureStageState`. New caches
`m_d3d7RsCache` / `m_d3d7TssCache` are indexed on the raw D3D7 input
and populated alongside the existing D3D9-output caches. The fast
path does bounds + slow-path-exclusion + valid flag + value compare
and returns `D3D_OK`. The slow path is the existing logic
unchanged.

**Slow-path carve-outs:**

- `SetRenderState`: 10 states with side effects beyond a plain D9
  `SetRenderState` (color-key, antialias, zbias, etc.).
- `SetTextureStageState`: `D3DTSS_ADDRESS` (multi-state side
  effect — sets both U and V).

Also dropped the `str::format`-based `Logger::debug` from
`SetRenderState`'s hot entry. `str::format` was being evaluated ~2k
times per frame even though the log level kept the output discarded.

**Result on uaRO quiet spot:**

| build | quiet-spot fps |
| --- | --- |
| dgVoodoo reference | ~550 |
| previous default-on (`b3071585`) | 410-430 |
| with input dedup (`6afc003b`) | **~730** (1.7×) |

The dgVoodoo gap is closed and inverted by ~180 fps. The dominant
cost was C++ entry overhead on dedup-hit paths, not the dedup work
itself.

### Updated environment-flag reference

| flag | default | status | effect |
| --- | --- | --- | --- |
| `D7VK_PROFILE` | unset | active | `=1` enables the aggregate profiler |
| `D7VK_PROFILE_INTERVAL` | 5 | active | profile summary interval in seconds |
| `D7VK_LOG_LEVEL` | (DXVK default) | active | use `info` to capture profile dumps |
| `D7VK_LOG_PATH` | (DXVK default) | active | directory for D7VK log files |
| `D7VK_DIRECT_CURRENT_RT_LOCK` | — | **retired** | always on |
| `D7VK_RT_LOCK_SCRATCH` | — | **retired** | always on |
| `D7VK_ASSUME_CURRENT_RT_LOCK_DISCARD` | — | **retired** | removed (was unused/experimental) |
| `D7VK_DIRECT_VULKAN` | — | **retired** | always on; `D3D7DirectRenderer` is allocated unconditionally |
| `D7VK_BATCH_DRAWS` | — | **retired** | always on |

### Where the remaining time goes

At ~730 fps quiet spot = 1.37 ms/frame, the CPU breakdown (subtracting
profile-scope overhead from the previous profile dump and scaling for
fps) is roughly:

| category | estimated μs/frame |
| --- | --- |
| state setters (fast-path hits) | ~150 |
| DrawPrimitive + flush submission | ~200 |
| RT lock+unlock (scratch) | ~0 (eliminated) |
| texture upload/init (~4/frame) | ~50 |
| Lock proxy + DDraw7 misc | ~80 |
| D9 → DXVK → Vulkan submission | ~750 |
| GPU + present | ~140 |
| **frame total** | **~1,370** |

The bulk of the remaining time is now in the D9 → DXVK → Vulkan
submission path. Further wins likely require either:

1. **Hoisted dedup for `SetTexture`** — currently 107 calls/frame
   at ~1.2 μs/call = 128 μs/frame. Lower volume than TSS/RS but the
   per-call cost is higher; the same top-level input-dedup pattern
   would apply.
2. **Extending `D3D7DirectRenderer` to absorb `DrawPrimitive`** —
   bypass `DrawPrimitiveUP`'s `PrepareDraw` / state validation /
   UP-buffer alloc and talk to `DxvkContext` directly for tri-list
   batches. The phase-1 plumbing is already in place; what stopped
   us before was the perf model (PrepareDraw isn't free, but it's
   also not the dominant cost — see phase 2.0 finding). Worth a
   fresh attempt now that state-setter cost is no longer the
   bottleneck.
3. **`SetRenderState` slow-path inlining** for special cases that
   still hit it (color-key, antialias) — these are rare per frame
   but each does meaningful work; if any are accidentally hot in a
   given scene, a focused refactor could help.

Items 1 and 2 are the two strongest leads.
