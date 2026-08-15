# Ragnarok Online Profiling With D7VK

Ragnarok Online is a 32-bit DirectX 7-era game, so the D7VK artifact to test is `ddraw.dll`.

## Build In Podman

Build the container image from the tree's `Containerfile.podman` (Fedora 42
MinGW32/Meson/Ninja toolchain with DNF fastest-mirror selection):

```sh
podman build -f Containerfile.podman -t d7vk-build .
```

Initialize submodules once, either on the host or inside the container:

```sh
git submodule update --init --recursive
```

Build a 32-bit release package:

```sh
mkdir -p out
podman run --rm -it \
  -v "$PWD:/src:Z" \
  -w /src \
  d7vk-build \
  ./package-release.sh local /src/out --no-package
```

The DLLs will be under `/src/out/d7vk-local/x32/`. For Ragnarok, start with `ddraw.dll`.

For a symbol-bearing (non-stripped) build, use the `--dev-build` mode:

```sh
mkdir -p out-debug
podman run --rm -it \
  -v "$PWD:/src:Z" \
  -w /src \
  d7vk-build \
  ./package-release.sh debug /src/out-debug --dev-build
```

## Deploy To A Wine Prefix

Copy `ddraw.dll` next to the Ragnarok executable and set a native override:

```sh
cp out/d7vk-local/x32/ddraw.dll <game-dir>/
WINEPREFIX=<your-prefix> winecfg
```

In `winecfg`, set `ddraw` to `native,builtin`.

Use the HUD to verify D7VK is active:

```sh
WINEPREFIX=<your-prefix> \
DXVK_HUD=devinfo,fps,frametimes,drawcalls,submissions,gpuload,ffshaders,swvp \
D7VK_LOG_LEVEL=info \
D7VK_LOG_PATH=/tmp/d7vk-logs \
wine <game-dir>/Ragexe.exe
```

## First Bottleneck Pass

Run with the same map, camera, resolution, and player count each time. Capture:

- average FPS and frame-time spikes from `frametimes`
- `drawcalls` and `submissions`
- `ffshaders`, especially whether it climbs during stutter
- `swvp`, because software vertex processing can move work to the CPU
- D7VK logs from `/tmp/d7vk-logs`

Interpretation:

- High GPU load with steady CPU usually means shader, fill-rate, present, or driver-side Vulkan work.
- Low GPU load with bad frame times usually points to game CPU, Wine, DDraw interop, locking, upload, or synchronization.
- Stutter when `ffshaders` or pipelines increase usually means shader or fixed-function variant creation.
- Many submissions per frame can indicate excessive flush/present behavior.

## Useful Config Toggles

Create a `dxvk.conf` beside the game executable and test one change at a time:

```ini
d3d9.presentInterval = 0
d3d9.maxFrameRate = 0
```

For DDraw/D3D7-specific probes (all are existing D7VK config options):

```ini
ddraw.forceSWVP = False
ddraw.cpuProcessVertices = True
ddraw.forceLegacyPresent = False
ddraw.emulateFrontBuffer = False
ddraw.apitraceMode = False
```

Do not treat these as optimizations by default. They are probes to identify which subsystem changes behavior.

## Deeper Profiling

CPU profile the Wine process:

```sh
perf record -F 997 -g --call-graph dwarf -- \
  env WINEPREFIX=<your-prefix> DXVK_HUD=frametimes wine <game-dir>/Ragexe.exe
perf report
```

For Vulkan-side profiling, use `DXVK_HUD=gpuload,submissions,drawcalls,frametimes` first. If a focused capture is needed, use RenderDoc against the Wine process, but keep captures short and reproduce a known slow scene.

For API trace debugging, enable only when needed because it can distort performance:

```ini
ddraw.apitraceMode = True
```

Then compare the trace behavior with a normal run before drawing performance conclusions.

## Frame Cap

A stable frame cap can reduce load while preserving rendering. Use DXVK's D3D9
config option, not `DXVK_FRAME_RATE`:

```sh
DXVK_CONFIG="d3d9.maxFrameRate = 144"
```

Alternatively, create `dxvk.conf` beside `uaRO.exe`:

```ini
d3d9.maxFrameRate = 144
```

## Historical / Retired

The following apply to **earlier revisions only** and are not available in the
current tree:

- The opt-in in-tree aggregate profiler (`D7VK_PROFILE`, `D7VK_PROFILE_INTERVAL`,
  `ProfileScope`, and its `out-profiler` debugoptimized build with
  `-Db_ndebug=false`) has been **removed**. There is no in-tree profiler today;
  use the external methods above.
- The opt-in flags `D7VK_BATCH_DRAWS`, `D7VK_RT_LOCK_SCRATCH`,
  `D7VK_DIRECT_CURRENT_RT_LOCK`, and `D7VK_ASSUME_CURRENT_RT_LOCK_DISCARD` were
  retired/removed; the corresponding behaviors (primitive batching, scratch
  current-RT lock) are now unconditional in the current tree. See the Current
  State section of `optimizations.md` for what exists today.
