# D7VK Optimizations For Ragnarok Online (uaRO)

This document records the Ragnarok Online (uaRO) performance work that has
landed in this repository. It has two parts:

- **Current State** — what the current tree actually implements, keyed to the
  final four-commit series and the upstream base it was transplanted onto.
- **Historical Research Record** — the chronological record of the earlier
  optimization research, kept for context. Everything in that section is
  historical: the experiments and environment flags it describes are **not**
  present in the current tree, and its numbers describe earlier builds, not
  current behavior.

Companion reading: [`ragnarok-profiling.md`](ragnarok-profiling.md) covers how
to build, install, and measure the current tree.

---

## Current State (2026-08-15)

### Branch and anchor

This branch (`d3d7-ro-latest-devel`) is a **fresh transplant** on the latest
verified upstream default branch (`origin/devel` of WinterSnowfall/d7vk),
frozen at:

```
c0fe0f78b241913ca391e5c90a797aaa1d374398   (2026-08-14)
```

It is not a merge or replay of rewritten upstream history. Because upstream
force-rewrote its history again, the earlier personal branches
(`d3d7-perf-profiling`, `d3d7-ro-migrate`) are preserved unmoved as historical
source material and were **not** merged. Only the intended runtime surfaces
were grafted onto the fresh root, and latest upstream implementations of the
shared code remain authoritative wherever they exist.

### Final four-commit series

The four topical units are identified by their exact stable subjects; the
frozen base SHA above remains exact:

1. `09e3c7a09` — `[meta] Add Podman build container and ignore local outputs`
2. `[d3d7] Restore RO state dedup and draw batching optimizations`
3. `[ddraw7] Restore current-RT scratch lock and fullscreen present fix`
4. `[docs] Consolidate Ragnarok optimization findings` — this documentation
   commit (it cannot record its own final SHA, since a commit ID is derived
   from the commit's content).

Commit IDs may change during local autosquash until the final audit, so units
2–4 are referenced by subject rather than SHA.

### Current implementation inventory

**Build metadata**

- `Containerfile.podman` provides the Fedora 42 MinGW32 / Meson / Ninja /
  glslang toolchain. Both DNF operations use `--setopt=fastestmirror=True` for
  reliable cold builds (the earlier mirror-selection stalls motivated this).
- `.gitignore` carries the local build/output patterns (`out*` trees,
  `build*/`, `.cache/`).

**D3D7 device — `src/ddraw/d3d7/d3d7_device.{cpp,h}`**

- **Raw-input dedup** at the top of `SetRenderState` (`m_d3d7RsCache`) and
  `SetTextureStageState` (`m_d3d7TssCache`), keyed on the raw D3D7 input and
  checked before `LockDevice`, validity checks, and state translation. The
  fast path is bypassed while a state block is recording and excludes ten
  render states whose slow paths have non-idempotent or multi-call behavior
  (`IsSlowPathD3D7Rs`) plus `D3DTSS_ADDRESS` (sets both U and V sampler
  states). `ZWRITEENABLE` is not excluded: its depth-write bookkeeping
  (`SetDepthWriteEnabled`) is idempotent for identical input, so repeated
  identical calls are safe under the raw-input dedup.
- **Post-translation D3D9 dedup** for render, sampler, and texture-stage state
  (`m_d3d9RsCache`, `m_d3d9SsCache`, `m_d3d9TssCache`). Cache entries and
  valid bits update only after a successful D3D9 call.
- **`SetTexture` dedup** — a top-level fast path for unchanged, initialized,
  clean, non-recording binds plus the `m_d3d9TexCache` post-translation cache.
  The stage-0 color-key bridge cache (`m_colorKeyCacheValid`) is value-keyed
  and stays coherent when a surface or its color-key values change.
- **FVF memoization** (`CachedFVFSize`) and **`SetD3D9FVF` dedup**, covering
  every draw path; a failed `SetFVF` never marks the cache valid.
- **Default-on primitive batching** (`FlushDrawBatch`, `AppendVerticesToBatch`):
  consecutive non-recorded `DrawPrimitive` calls batch for point lists and for
  line/triangle lists whose vertex count forms whole primitives; a per-call
  line/triangle list with a remainder stays immediate, preserving the existing
  primitive-count behavior. Triangle strips and fans are expanded to
  triangle-list form with winding and flat-shading provoking vertices
  preserved so they coalesce with list draws. A flush issues one
  `DrawPrimitiveUP`. Every operation that can change draw-visible state or
  ordering flushes the pending batch first (state/transform/viewport/material/
  light/clip/color-key changes, scene boundaries, `Clear`, all non-batched draw
  entry points, state-block apply, swapchain reset). DirectDraw surface writes
  and recreation also flush first: destination `Blt`/`BltFast`, a writable or
  current-render-target `Lock`, `GetDC`, and `SetSurfaceDesc` recreation. These
  are the covered boundaries, not a claim that every conceivable DirectDraw
  boundary is handled. Dedup hits do not flush.
- **Cache invalidation**: applying a state block and a successful swapchain
  reset invalidate all dedup caches; the reset also flushes a pending batch
  before resetting.
- The D3D7 multithread lock is **forced off** (an explicit single-render-thread
  tradeoff for uaRO); the latest upstream D3D9 locking is untouched.
- Latest upstream bridge (`IDxvkLegacyD3DDeviceBridge`), caps
  (`ApplyD3D7DeviceCaps`), dirty-surface helpers, and reset handling remain
  authoritative.

**DDraw7 surface — `src/ddraw/ddraw7/ddraw7_surface.{cpp,h}`**

- **Scratch current-RT lock**: an eligible `Lock` of the current render target
  on a D3D9-windowed swapchain returns a scratch CPU buffer and returns without
  downloading or locking the proxy. The matching `Unlock` clears the active
  flag and returns early, **discarding the writes** — no proxy unlock, no dirty
  tracking, no lock bookkeeping, no presentation.
- **Early-present gate**: the early primary-surface `Blt` / `BltFast`
  presentation path keys off the actual D3D9 swapchain `Windowed` state
  (`GetPresentParameters()->Windowed`) rather than DirectDraw
  `DDSCL_EXCLUSIVE`, preserving the uaRO fullscreen black-screen fix.
- Latest upstream fallback lock bookkeeping (`m_readOnlyLock`, atomic
  `m_lockCount`, dirty tracking, shadow/proxy selection) and the newer
  `legacyPresentGuard` logic in the later shadow-surface presentation path are
  retained unchanged.

**Common device — `src/ddraw/d3d_common_device.{cpp,h}`**

- **Common-device flush bridge**: `D3DCommonDevice` is the shared device state
  object the runtime pairs build on and exposes `FlushPendingDraws()`, which
  forwards to the active `D3D7Device`. The DirectDraw surface entry points that
  write to or recreate surface data call it (via `GetCommonD3DDevice()`) so a
  pending D3D7 batch is flushed before a destination write or recreation. It
  ships alongside the two runtime pairs.

### Current limitations

- The scratch-lock elision assumes uaRO's observed **single render thread**
  and single current-RT lock pattern. It is not general DirectDraw semantics.
- The eligible scratch lock **intentionally discards the game's writes** and
  does not provide readback. Any game that reads the locked pixels or relies on
  the writes landing on the render target is unsupported.

### Validation status

- A 32-bit release build of the transplanted series completed in the Podman
  container (**283/283 targets**) and produced
  `out-builds/verify-ro-latest/d7vk-ro-latest/x32/ddraw.dll` (PE32 i386).
- Two independent code reviewers found no blockers or warnings.
- Validated live on **2026-08-16** through the Heroic runner; the full
  discovery → back-up → hash → deploy → launch → verify → benchmark → restore
  workflow is documented in [`ragnarok-profiling.md`](ragnarok-profiling.md).
  Controlled Xwayland old-vs-new A/B in the same session: reviewed rebased
  build 396.1 average FPS, pre-review rebased 402.4, trusted pre-rebase 867.9,
  fixed 879.6. Fixed native-Wayland validation: 1388.7 average FPS (not a
  controlled old-vs-new native A/B).
- The historical measurements in the Research Record below describe their own
  build/flag/workload context and are **not** offered as proof of current
  performance; use the 2026-08-16 results and the Heroic workflow instead.

---

## Historical Research Record

Everything below is **history**. It describes the earlier optimization
research performed on the pre-rewrite personal branch, before the fresh
transplant. Measurements are observations from their own build, flag, and
workload context, not current truth. Experiments that do not exist in the
current tree — the aggregate profiler, the direct Vulkan renderer path, and
the various opt-in environment flags — are described for the record only.
The current tree is exactly the inventory in the Current State section above.

### Early target and baseline

- Game: uaRO (Ragnarok Online client), windowed.
- Platform: Linux + Wine + Bottles (PROTON prefix), Vulkan back end via DXVK.
- Same crowded map, similar camera, used as the steady-state benchmark.
- GPU utilization in that scene: ~12–15%. CPU-bound.
- Reference: dgVoodoo (D3D7 → D3D11 wrapper, separate stack) reached
  ~450–500 FPS in the same scene.

Early measurements (original branch, first optimized build):

| build | login / quiet | crowded in-game |
| --- | --- | --- |
| upstream `origin/devel` | ~230 FPS | ~60–80 FPS (MangoHud) |
| optimized (early branch) | ~444 FPS (BeginScene profile) | ~80–90 FPS (MangoHud) |
| dgVoodoo (reference) | not measured | ~450–500 FPS |

The early conclusion — that the remaining gap was in per-call overhead in the
D3D7 → D3D9 → DXVK → Vulkan stack rather than GPU work — was later refined:
the per-call wrapper cost turned out to be mostly recoverable (see input dedup
below), and the dominant costs were the render-target lock round trip and, in
busy scenes, texture-bind churn (see the scratch-lock and busy-area sections).

### Original profiling scaffolding — aggregate profiler (REMOVED)

An opt-in aggregate-counter profiler was added in `src/util/util_profile.{h,cpp}`
(commit `49c76e0b`), enabled with `D7VK_PROFILE=1`, dumping periodic
`D7VK profile summary` blocks (per-event call count, total/avg/max ns) at
`D7VK_PROFILE_INTERVAL` (default 5 s). `ProfileScope` RAII helpers hooked
`DDraw7Surface::Blt/Lock/Unlock`, transfer helpers, and selected `D3D7Device`
entry points.

**This infrastructure is removed from the current tree.** The profiler,
`ProfileScope`, and `D7VK_PROFILE` / `D7VK_PROFILE_INTERVAL` do not exist in
HEAD. Later measurement also showed the disabled-mode cost of the profiler's
cross-TU `enabled()` calls on hot paths was material at the FPS levels uaRO
reaches, which is part of why it was stripped. See the historical/retired
section of `ragnarok-profiling.md`.

### Direct current-RT lock (superseded by the scratch lock)

An opt-in flag `D7VK_DIRECT_CURRENT_RT_LOCK=1` routed locks of the *current*
render target straight to the backing D3D9 surface's `LockRect`/`UnlockRect`,
bypassing the DDraw-side proxy upload/download round trip.

- Login/loading: ~230 → ~444 FPS (BeginScene-rate).
- Crowded in-game: negligible — the lock+unlock cost (~951 µs lock + ~408 µs
  unlock) was load-independent and only ~10% of in-game frame time.

It was opt-in because it changed correctness semantics for unusual patterns.
It was later superseded entirely by the scratch lock (below); both flags are
retired.

### State-cache dedup (first pass)

The first dedup pass (commit `77f70491`, always-on) cached the live
post-translation D3D9 value for render state, sampler state, texture-stage
state, and bound texture pointers. `SetRenderState`, `SetTextureStageState`,
and `SetTexture` short-circuited on unchanged values; recording state blocks
bypassed dedup and caches invalidated on `ApplyStateBlock`.

Measured hit rate was high (~46% of `SetRenderState`) but **no measurable FPS
improvement** — DXVK's D3D9 layer already optimized redundant state sets, so
what was skipped was nearly free downstream. Kept because it was correct,
free, and became infrastructure for later work. The far bigger win came later
from hoisting dedup above the wrapper entry cost itself (see input dedup
below); the post-translation caches survive in the current tree.

### DrawPrimitive batching (opt-in, list-only era)

The first batching implementation was opt-in via `D7VK_BATCH_DRAWS=1` and
batched only `D3DPT_TRIANGLELIST`, `D3DPT_LINELIST`, and `D3DPT_POINTLIST` with
the same FVF, emitted as one `DrawPrimitiveUP`.

- Only ~3.8% of game `DrawPrimitive` calls were batchable (uaRO uses mostly
  triangle strips/fans for sprites/quads); of those ~30% merged.
- ~78 → ~83 FPS (BeginScene rate), ~80 → ~90 FPS (MangoHud) in the busy scene.

The win came from collapsing (N–1) `PrepareDraw` cycles per merged batch, not
from vertex-upload bandwidth. Batching is now **default-on and wider** in the
current tree (strips and fans included, no flag).

### Universal triangle-list expansion (rolled back)

An attempt to convert incoming `TRIANGLESTRIP/FAN` and `LINESTRIP` to list
form (winding preserved) so everything could batch together, behind the same
flag. Mechanically it worked — batch-append rate jumped from 3.8% to ~41% —
but on uaRO it was a **net loss** (~80 → ~70 FPS):

- The game issues a `SetTexture` roughly every 1.7 draws; every non-dedup
  `SetTexture` forces a flush.
- Net batch size stayed ~1.7 game-draws, so the batch state machine ran on
  every draw instead of only on ~3.8% of them, almost never amortizing its own
  overhead.
- Per-call wrapper cost in `SetTexture` doubled (0.59 → 1.10 µs avg).

A bulk-`memcpy` expand and a batch byte cap fixed correctness cliffs (an early
version deadlocked the loading screen with many small `vector::insert` calls on
long strips), but the flush-frequency problem was inherent to the game's call
pattern. Reverted. The current tree's batching includes strip/fan expansion
only because it is default-on at the draw layer while the flush matrix above
still flushes on every real texture change.

### Where the remaining time went (early analysis)

In-game busy area, ~76 FPS (13.2 ms/frame), GPU at 12–15% (early branch):

| accounted-for | per frame | % of frame |
| --- | --- | --- |
| Direct render-target lock + unlock | ~1.36 ms | ~10 % |
| `D3D7Device::DrawPrimitive` wrapper | ~0.69 ms | ~5 % |
| `D3D7Device::SetTexture` wrapper | ~0.61 ms | ~5 % |
| `D3D7Device::SetRenderState` wrapper | ~0.13 ms | ~1 % |
| other D7VK wrappers | ~0.1 ms | <1 % |
| **profiled D7VK total** | **~2.9 ms** | **~22 %** |
| **unaccounted (D3D9 / DXVK / Vulkan / driver / wine)** | **~10.3 ms** | **~78 %** |

Call volume: ~2,168 `DrawPrimitive`, ~1,269 `SetTexture`, ~310
`SetRenderState`, ~3,750 D3D7 state/draw calls per frame total.

This analysis pointed at `PrepareDraw` in DXVK's `DrawPrimitiveUP`. **That
model was later invalidated** by the phase 2.0 experiment (below), which showed
`PrepareDraw` does almost no work when nothing is dirty; the unaccounted cost
was actually the per-draw `EmitCs` dispatch + CS-thread work + Vulkan
submission, which later analysis attributed to texture-bind churn in busy
scenes.

### Original suggested next steps (superseded)

The early handoff suggested a D3D7 → Vulkan direct path, investigating
`VK_SUBOPTIMAL_KHR` swapchain churn, and per-primtype draw telemetry. The
direct path was subsequently **pursued and rejected** (see below), so these
are historical only.

### Phase 1 — `D3D7DirectRenderer` groundwork (REMOVED)

Three commits (`6510aa71`, `a5e2e822`, `900a1b75`) laid plumbing for a direct
Vulkan path: a `D3D7DirectRenderer` skeleton behind `D7VK_DIRECT_VULKAN=1`, a
`GetDXVKDevice()` bridge accessor, and direct render-target clear routing.
Phase 1 answered an open question (sharing the D3D9 device's CS thread is
fine) and was a tiny clear-path regression.

**All of this is removed from the current tree.** `D3D7DirectRenderer`,
`D7VK_DIRECT_VULKAN`, and the direct clear path do not exist in HEAD.

### Phase 2.0 — piggyback `DrawPrimitive` (rolled back)

A `DrawPrimitiveUPFastPath` identical to `DrawPrimitiveUP` but skipping
`PrepareDraw`, gated on a `m_d9StateDirty` flag. Measured on uaRO busy area:

- Fast path engaged on **42%** of draws (~850/frame); uaRO does ~one
  `SetTexture` per two draws, limiting the fast-path share.
- `DrawPrimitive` wrapper avg cost rose 0.31 → 0.44 µs/draw; busy-area FPS
  dropped ~88–98 → ~80.

**Conclusion: the perf model was wrong.** `PrepareDraw` does almost no work
when nothing is dirty. The ~10 ms/frame unaccounted cost is the actual `EmitCs`
dispatch + CS-thread work + Vulkan submission per draw, which the piggyback
still pays. Rolled back. This finding supersedes the earlier "skip-PrepareDraw"
angle in the early time analysis above.

### Updated direction (deferred)

Given the phase 2.0 finding, the genuine win was argued to require attacking
per-draw CS-thread / Vulkan submission cost — CS-thread bundling, or
descriptor-indexed / texture-array rendering with per-vertex texture selectors
to decouple texture bind from draw. Both require owning more of the pipeline
and were framed as effectively the full FF rewrite. That work remains deferred
and is **not** implemented in the current tree.

### Scratch RT lock — origin of the current scratch lock

Instrumenting the D3D9 `LockImage` path showed that any surface with
`D3DUSAGE_RENDERTARGET` always triggers a `copyImageToBuffer` +
`WaitForResource`, regardless of lock-flag intent. For uaRO at the login
screen this was ~1.4 ms of GPU fence wait per frame (~70% of a 2 ms/frame
budget at 500 fps). A diagnostic replacing the D3D9 `LockRect` with a scratch
CPU buffer (writes discarded) jumped login from 500 → 1290 fps, with
user-confirmed visual correctness in both login and busy area.

Results (pre-transplant branch, `D7VK_RT_LOCK_SCRATCH=1` era):

| scene | baseline (`origin/devel`) | post-pivot baseline | with scratch lock | dgVoodoo reference |
| --- | --- | --- | --- | --- |
| login | ~230 fps | ~500 fps | ~1290 fps | ~800 fps |
| busy area | ~60–80 fps | ~90 fps | ~112 fps | ~100 fps |

A "proper" sysmem-mirror variant was tried and produced a **black screen**:
the mirror starts blank and `UpdateSurface` overwrote the entire RT with the
mirror's content every frame. The scratch path is correct for uaRO and ~7% of
the cost, so it shipped — which is exactly the behavior now unconditional in
the current tree, with the discarded-write and single-thread caveats documented
in Current State above.

Other things that didn't work along the way: a forced D3D9 flush at `EndScene`
(small regression — there was no CPU work to overlap), and an initial wrapper
overhead optimization whose regression turned out to be **profile-scope
overhead from instrumentation**, not the wrapper changes (the color-key cache
landed neutral; FVF dedup wasn't independently re-tested). That later
discovery is why the profiler was stripped — see the removed profiler section
above.

### Personal-fork pivot — default-on baseline (flags retired)

After the upstream PR (opt-in `ddraw.directRenderTargetLock` /
`ddraw.assumeRenderTargetLockDiscard` config options) was rejected, the repo
was repositioned (as of 2026-05-23) as a personal fork for Ragnarok Online,
with optimizations landing unconditional rather than gated. The scratch lock,
draw batching, and state dedup all became default-on; `D7VK_BATCH_DRAWS`,
`D7VK_RT_LOCK_SCRATCH`, `D7VK_DIRECT_CURRENT_RT_LOCK`, and
`D7VK_ASSUME_CURRENT_RT_LOCK_DISCARD` were retired, and the direct-Vulkan
renderer/profiler flags followed.

This is the lineage of the current tree's default-on behavior, but note the
pivot-era tree also still contained the removed renderer and profiler; the
fresh transplant in the Current State section is the only description of what
exists today.

### Top-level input dedup for state setters (carried into the transplant)

Profiling at a quiet spot (profile build at 148 fps) showed per-call cost was
already low (0.14 µs for TSS hits) but call volume × ~90% dedup-hit rate meant
the wrapper was paying the COM entry prelude, `D3DDeviceLock`,
`Logger::debug`, validity/range checks, the state-translation switch, and
lambda construction on ~10k calls/frame:

| method | calls/frame | µs/frame (profile ON) | µs/call |
| --- | --- | --- | --- |
| SetTextureStageState | 8,780 | 1,248 | 0.14 |
| SetRenderState | 2,002 | 708 | 0.35 |
| DrawPrimitive | 1,219 | 239 | 0.20 |
| SetTexture | 107 | 128 | 1.20 |
| DrawPrimitive flush | 23 | 18 | 0.79 |

The fix hoisted a fresh D3D7-input-keyed dedup to the top of
`SetRenderState` and `SetTextureStageState` (`m_d3d7RsCache` /
`m_d3d7TssCache`), plus the slow-path carve-outs that still exist today (ten
render states; `D3DTSS_ADDRESS`). A `Logger::debug` `str::format` was also
dropped from the `SetRenderState` hot entry.

Result on uaRO quiet spot (pre-transplant build):

| build | quiet-spot fps |
| --- | --- |
| dgVoodoo reference | ~550 |
| previous default-on | 410–430 |
| with input dedup | **~730** (1.7×) |

The dominant cost was C++ entry overhead on dedup-hit paths, not the dedup
work itself. This optimization is part of the current transplant.

The then-current quiet-spot CPU breakdown (~730 fps = 1.37 ms/frame) after
subtracting profile-scope overhead:

| category | estimated µs/frame |
| --- | --- |
| state setters (fast-path hits) | ~150 |
| DrawPrimitive + flush submission | ~200 |
| RT lock+unlock (scratch) | ~0 (eliminated) |
| texture upload/init (~4/frame) | ~50 |
| Lock proxy + DDraw7 misc | ~80 |
| D9 → DXVK → Vulkan submission | ~750 |
| GPU + present | ~140 |
| **frame total** | **~1,370** |

### Direct D3D7→Vulkan investigation and busy-area profiling (rejected)

A design pass plus live profiling (AMD Radeon 780M, integrated/UMA) settled the
direct-path question: **the direct path was judged the weakest lever, and the
real bottleneck was texture-bind churn.**

Draw-mix instrumentation (temporary per-frame counter from `EndScene`):

| metric | quiet corner | busy town (Prontera-like) |
|---|---|---|
| `DrawPrimitive` calls/frame | 508 | ~4,400 |
| actual GPU draws (batch flushes) | 50 | ~1,475 |
| appends per flush | 10.2 | 2.7 |
| `DrawIndexedPrimitive` | 0 | **0** |
| strip/fan vertex inflation | 1.10× | 1.37× |
| FVF | constant (`0x1C4`) | constant (`0x1C4`) |

(`0x1C4` = `XYZRHW | DIFFUSE | SPECULAR | TEX1`, 2D pre-transformed sprites.
uaRO never uses indexed draws and never calls `SetTransform`.) Flush cause in
the busy scene was ~99% texture changes — the engine submits sprites in
texture-thrashing order and each new texture forces a batch flush; draw order
can't be reordered because sprites are alpha-blended.

MangoHUD showed **GPU ~25%, one CPU core ~67%** — hard single-thread
(render-thread) bound. Profiler breakdown in the busy area: `SetTexture`
dominated (~21% of frame, ~1,363 calls × ~1.9 µs), with the removable overhead
inside `SetTexture` being the color-key emulation (`ColorKeyToRGB` recompute +
bridge calls on every bind). The flush's actual `DrawPrimitiveUP`→`PrepareDraw`
was only ~2.8%.

Changes shipped that pass:

- **`SetFVF` dedup** (`SetD3D9FVF`): the FVF is near-constant (~2 real
  changes/frame) yet every draw path set it, costing a D3D9 hashmap lookup each
  time. Confirmed **+~30 fps in the quiet corner** (730→760 region).
- **`GetFVFSize` memoization** (`CachedFVFSize`): stop walking FVF bits on
  every `DrawPrimitive`.
- **Color-key cache value-keyed** instead of surface-pointer-keyed: the bridge
  color-key state is global D3D9 state, so consecutive sprites sharing a
  color-key made the bridge calls redundant.

All three are part of the current transplant. Their busy-area effect was real
but at/under the live-town FPS noise floor (±~15 fps as the crowd moves).

Conclusion: the wrapper approach was judged to be near the practical floor
(~3–4% from native, itself a structural floor given the texture-bind wall),
with only a long-horizon FF-shader / texture-array rewrite able to beat it.
That rewrite is deferred.

### Upstream v1.11 review (2026-06-13)

Reviewed upstream (`origin`, WinterSnowfall/d7vk) at v1.11 plus the then-current
`devel` tip. Verdict: **nothing to pull for performance.**

- Upstream force-pushed/rebased `devel` but stayed on the same DXVK 2.7.1
  base; there was **no DXVK runtime uplift**, so the dominant remaining cost
  (the D9 → DXVK → Vulkan submission path) was byte-for-byte unchanged.
- The genuinely new delta was 21 ddraw-layer commits, none touching uaRO's hot
  path (`SetTexture` / `DrawPrimitive` on 2D `XYZRHW` sprites): D3D3/5/6
  feature work, SWVP / lighting / transform paths, Wine-test fixes, The Sims
  workarounds, and object lifecycle refactors.
- `26447cb25` "[meta] Simplify the color keying implementation" was the only
  relevant commit: a perf-neutral refactor (dropped `withTolerance` and
  `colorKeyTolerance`, reordered channels, `ColorKeyToRGB` → `ColorKeyToARGB`).
  It is a *behavioral* change to color-key matching worth a visual recheck on
  rebase, but not a speedup.

Since then, upstream's later refactors — the bridge, caps, reset handling,
fallback lock bookkeeping, and `legacyPresentGuard` — are exactly what the
current transplant preserves as authoritative.

### Historical environment flags reference (all retired or removed)

| flag | status in current tree |
| --- | --- |
| `D7VK_PROFILE` | **removed** — aggregate profiler does not exist |
| `D7VK_PROFILE_INTERVAL` | **removed** — aggregate profiler does not exist |
| `D7VK_DIRECT_CURRENT_RT_LOCK` | **retired** — replaced by scratch lock |
| `D7VK_RT_LOCK_SCRATCH` | **retired** — scratch lock is unconditional |
| `D7VK_ASSUME_CURRENT_RT_LOCK_DISCARD` | **removed** — experimental, broke uaRO |
| `D7VK_BATCH_DRAWS` | **retired** — batching is unconditional |
| `D7VK_DIRECT_VULKAN` | **removed** — `D3D7DirectRenderer` does not exist |

`D7VK_LOG_LEVEL` and `D7VK_LOG_PATH` are DXVK-standard and remain valid for
log capture.

### Historical files touched

The original branch touched `src/util/util_profile.{h,cpp}` (profiler, now
removed), `src/ddraw/ddraw_format.h` (transfer helper hooks, since reverted to
upstream), `src/ddraw/ddraw7/ddraw7_surface.{cpp,h}`,
`src/ddraw/d3d7/d3d7_device.{cpp,h}`, `Containerfile.podman`, `.gitignore`, and
these docs. The current tree ships only `Containerfile.podman`, `.gitignore`,
the two runtime pairs, the common-device bridge pair
(`src/ddraw/d3d_common_device.{cpp,h}`), and these two docs; none of the
profiler files exist.
