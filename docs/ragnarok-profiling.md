# Ragnarok Online Profiling With D7VK

Ragnarok Online is a 32-bit DirectX 7-era game, so the D7VK artifact to test is `ddraw.dll`.

The **validated** end-to-end workflow (2026-08-16) builds, deploys, launches,
verifies, benchmarks, and restores a D7VK `ddraw.dll` through the Heroic
flatpak runner; see [Heroic Launch Workflow](#heroic-launch-workflow). The
older generic Wine-prefix instructions below remain available and are clearly
labeled as generic alternatives, not the validated route.

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

This is the fixed, validated build shape: `./package-release.sh <version>
<output> --no-package` uses the existing Podman image and produces
`<output>/d7vk-<version>/x32/ddraw.dll`, a PE32 i386 DLL. The release build
completes 283/283 targets.

For a symbol-bearing (non-stripped) build, use the `--dev-build` mode:

```sh
mkdir -p out-debug
podman run --rm -it \
  -v "$PWD:/src:Z" \
  -w /src \
  d7vk-build \
  ./package-release.sh debug /src/out-debug --dev-build
```

## Heroic Launch Workflow (validated 2026-08-16)

This is the **validated** path for deploying and testing a D7VK `ddraw.dll`
with the Heroic flatpak runner. All absolute paths, app ids, display values,
and auth filenames below are **host-specific examples** from one validated
host; substitute your own and re-derive them per host.

### Discovery: Heroic sideload app id

Sideloaded games live in the Heroic sideload library. Library path (example):

```text
~/.var/app/com.heroicgameslauncher.hgl/config/heroic/sideload_apps/library.json
```

Look up the entry whose `title` is your game (example `Ragnarok`) and read its
`app_name` (example `bG6fcz3nWPxwHGpxVbKcpm`). The launch URI shape is:

```text
heroic://launch/sideload/<app-id>
```

### Deploy to the Heroic runner

The D7VK DLL the runner serves to the game lives inside the Proton runner
(`files/lib/wine/d7vk/i386-windows/`). On the validated host the target was
(example path):

```text
/home/chmartin/.var/app/com.heroicgameslauncher.hgl/config/heroic/tools/proton/Proton-CachyOS-latest/files/lib/wine/d7vk/i386-windows/ddraw.dll
```

**Always back up the target DLL (and any Heroic profile you change) before
patching, and compare SHA-256 of source and target after every copy. The copy
runs on the host, so use a host-visible build path (`$PWD/out` maps to the
container's `/src/out` via the bind mount):**

```sh
RUNNER_DLL="$HOME/.var/app/com.heroicgameslauncher.hgl/config/heroic/tools/proton/Proton-CachyOS-latest/files/lib/wine/d7vk/i386-windows/ddraw.dll"
BACKUP_DLL="$RUNNER_DLL.bak.$(date +%Y%m%d-%H%M%S)"
BUILT_DLL="$PWD/out/d7vk-<version>/x32/ddraw.dll"

cp "$RUNNER_DLL" "$BACKUP_DLL"
cp "$BUILT_DLL" "$RUNNER_DLL"
sha256sum "$RUNNER_DLL" "$BUILT_DLL"   # must match
```

`BACKUP_DLL` is timestamped so re-running the workflow never overwrites an
earlier backup. Note the in-container build destination is
`/src/out/d7vk-<version>/x32/ddraw.dll`; on the host that is
`$PWD/out/d7vk-<version>/x32/ddraw.dll`.

The patched runner DLL is **shared by every game using that runner**, and a
runner update may silently replace it — re-verify the hash after any runner
update and re-apply if needed.

### Bottles managed-component trap

If you test under Bottles instead: a **configured managed D7VK component
copies over the bottle's `syswow64/ddraw.dll` on launch**, and editing that
shared managed component **propagated to multiple bottles** during the
2026-08-16 investigation. Do **not** patch the shared managed component for an
isolated A/B; verify hashes after launch there too.

### Environment boundaries

Two environment domains must stay distinct:

- **Host boundary** — `DISPLAY`/`XAUTHORITY` or `WAYLAND_DISPLAY`/
  `XDG_RUNTIME_DIR` are set on the host, before `flatpak run`.
- **Flatpak sandbox** — exposed sockets (`--socket=x11`, `--socket=wayland`,
  `--nosocket=x11`) and stripped env vars (`--unset-env=...`) are `flatpak run`
  options.

Do **not** use `flatpak run --env=XAUTHORITY=<host path>`: the host path is
invalid inside the sandbox.

No Gamescope is used in either path; verify the Heroic game's `wrapperOptions`
is empty before benchmarking.

### Launch via Xwayland (controlled comparison path)

Discover a valid host Xwayland `DISPLAY` and its matching Xauthority first
(`xrandr` reports the display; the Mutter Xwayland authority file is
host-specific). Supply `DISPLAY`, `XAUTHORITY`, and the Electron X11 hint
`ELECTRON_OZONE_PLATFORM_HINT=x11` at the host `env` boundary. This is the
exact shape that succeeded (the `--ozone-platform=x11` flag was not validated):

```sh
env DISPLAY=":0" XAUTHORITY="<host-xauthority-example>" ELECTRON_OZONE_PLATFORM_HINT=x11 \
  flatpak run \
    --socket=x11 \
    --unset-env=WAYLAND_DISPLAY \
    com.heroicgameslauncher.hgl \
    heroic://launch/sideload/bG6fcz3nWPxwHGpxVbKcpm
```

`:0` and the Xauthority value are host-specific examples.

### Launch via native Wayland (validated shape)

Enable Wine Wayland in Heroic, then launch with host
`WAYLAND_DISPLAY`/`XDG_RUNTIME_DIR` at the host `env` boundary, the Wayland
socket exposed, X11 and the X env stripped inside the sandbox, and the
Heroic/Electron `--ozone-platform=wayland` hint **before** the launch URI. This
exact shape succeeded on the validated host and the game log showed `waylanddrv`:

```sh
env WAYLAND_DISPLAY="wayland-0" XDG_RUNTIME_DIR="/run/user/1000" \
  flatpak run \
    --socket=wayland \
    --nosocket=x11 \
    --unset-env=DISPLAY \
    --unset-env=XAUTHORITY \
    com.heroicgameslauncher.hgl \
    --ozone-platform=wayland \
    heroic://launch/sideload/bG6fcz3nWPxwHGpxVbKcpm
```

`wayland-0` and `/run/user/1000` are host-specific examples.

### Verify D7VK is active

Heroic writes a per-game launch log. Example path (host-specific):

```text
~/.var/app/com.heroicgameslauncher.hgl/.local/state/Heroic/logs/games/<app-id>_sideload/launch.log
```

For the sideloaded app, confirm the log contains:

- `D7VK: v<version>` — the D7VK build is active
- the command/wrapper line — it must contain **no Gamescope** and must select
  the Wine runner whose directory you patched
- `enableWineWayland` if Wine Wayland was enabled
- `waylanddrv` for the native-Wayland launch

The Wine command line does not literally reference the DLL; pin the exact
DLL/version with the source/target SHA-256 (after copy) plus `D7VK: v`.

### Benchmark with automatic MangoHud

Set MangoHud to auto-log a fixed-duration window so runs are comparable without
watching the HUD. Set `MANGOHUD_CONFIG` as a Heroic game environment variable
(example):

```text
full,autostart_log=8,log_duration=15,output_folder=<app-accessible-dir>
```

`output_folder` must be a directory the game can write inside the sandbox
(host-specific). Compare the same scene/window and read the resulting
`*_summary.csv` for average FPS. Record the original `MANGOHUD_CONFIG` value
before changing it, and **restore that original value after testing** rather
than blindly unsetting it (the same applies to any Heroic profile setting you
changed for the run).

### Validated measurements (2026-08-16)

- **Controlled Xwayland A/B** (old-vs-new in the same session): reviewed rebased
  build averaged **396.1 FPS**; pre-review rebased build **402.4**; trusted
  pre-rebase build **867.9**; fixed build **879.6**.
- **Fixed native-Wayland validation**: **1388.7 average FPS**. This validates
  the fixed build on the native-Wayland path; it is **not** a controlled
  old-vs-new native A/B.

### Restore

Restore the backed-up DLL from the timestamped `BACKUP_DLL` defined at deploy
time, restore any changed Heroic profile or `MANGOHUD_CONFIG` value, then
re-check the hash:

```sh
cp "$BACKUP_DLL" "$RUNNER_DLL"
sha256sum "$RUNNER_DLL"
```

## Generic Wine-Prefix Deployment (Alternative)

The following generic Wine-prefix steps are **not** the validated Heroic route
(see the Heroic Launch Workflow above); they are retained for non-Heroic
setups.

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
