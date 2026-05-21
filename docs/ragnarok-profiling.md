# Ragnarok Online Profiling With D7VK

Ragnarok Online is a 32-bit DirectX 7-era game, so the D7VK artifact to test is `ddraw.dll`.

## Build In Podman

Build the container image:

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

The DLLs will be under `out/dxvk-local/x32/`. For Ragnarok, start with `ddraw.dll`.

For a debug-symbol build:

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
cp out/dxvk-local/x32/ddraw.dll /path/to/Ragnarok/
WINEPREFIX=/path/to/prefix winecfg
```

In `winecfg`, set `ddraw` to `native,builtin`.

Use the HUD to verify D7VK is active:

```sh
WINEPREFIX=/path/to/prefix \
DXVK_HUD=devinfo,fps,frametimes,drawcalls,submissions,gpuload,ffshaders,swvp \
D7VK_LOG_LEVEL=info \
D7VK_LOG_PATH=/tmp/d7vk-logs \
wine /path/to/Ragnarok/Ragexe.exe
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

For DDraw/D7VK-specific experiments:

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
  env WINEPREFIX=/path/to/prefix DXVK_HUD=frametimes wine /path/to/Ragnarok/Ragexe.exe
perf report
```

For Vulkan-side profiling, use `DXVK_HUD=gpuload,submissions,drawcalls,frametimes` first. If a focused capture is needed, use RenderDoc against the Wine process, but keep captures short and reproduce a known slow scene.

For API trace debugging, enable only when needed because it can distort performance:

```ini
ddraw.apitraceMode = True
```

Then compare the trace behavior with a normal run before drawing performance conclusions.

## D7VK Aggregate Profiling

This repo also has an opt-in D7VK profiler for the D3D7/DDraw7 path. Build it with:

```sh
mkdir -p out-profiler
podman run --rm \
  -v "$PWD:/src:Z" \
  -w /src \
  d7vk-build \
  meson setup --cross-file /src/build-win32.txt \
    --buildtype debugoptimized \
    --prefix /src/out-profiler/d7vk-profile \
    --bindir x32 \
    --libdir x32 \
    -Db_ndebug=false \
    -Dbuild_id=true \
    -Denable_dxgi=false \
    -Denable_d3d8=false \
    -Denable_d3d10=false \
    -Denable_d3d11=false \
    /src/out-profiler/d7vk-profile/build.32
podman run --rm \
  -v "$PWD:/src:Z" \
  -w /src/out-profiler/d7vk-profile/build.32 \
  d7vk-build \
  ninja install
```

Install it:

```sh
cp out-profiler/d7vk-profile/x32/ddraw.dll /var/mnt/DATA/Bottles/PROTON/drive_c/windows/syswow64/ddraw.dll
```

Run the game with:

```sh
D7VK_LOG_LEVEL=info
D7VK_LOG_PATH=/var/mnt/DATA/Bottles/PROTON/d7vk-logs
D7VK_PROFILE=1
D7VK_PROFILE_INTERVAL=5
```

The log will contain periodic `D7VK profile summary` blocks with call counts, total time, average time, and max time for selected DDraw7 and D3D7 entry points.

### uaRO Findings

For uaRO/Ragnarok Online, the main measured cost is a required per-frame
render-target round trip:

- `Lock` on the current render target downloads D3D9 data back to the DDraw
  surface.
- The game updates the DDraw surface.
- Windowed presentation uploads that surface back to D3D9.

An experimental skip for current-render-target lock readback
(`D7VK_ASSUME_CURRENT_RT_LOCK_DISCARD=1`) greatly reduces transfer time, but it
breaks rendering for this game. Do not use it for normal play.

Since the transfer is required for correctness and happens once per frame, the
most effective safe optimization is a frame cap. Use DXVK's D3D9 config option,
not `DXVK_FRAME_RATE`:

```sh
DXVK_CONFIG="d3d9.maxFrameRate = 144"
```

Alternatively, create `dxvk.conf` beside `uaRO.exe`:

```ini
d3d9.maxFrameRate = 144
```

In testing, this reduced the transfer workload from roughly 230+ frame cycles
per second to exactly 144 frame cycles per second while preserving rendering.
