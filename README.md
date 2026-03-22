# MangoHud (Windows)

A Vulkan and DirectX overlay for monitoring FPS, temperatures, CPU/GPU load and more. **Windows-only fork.**

![Example gif showing a standard performance readout with frametimes](assets/overlay_example.gif)

---

- [MangoHud (Windows)](#mangohud-windows)
  - [About This Fork](#about-this-fork)
  - [What Changed](#what-changed)
  - [GPU Support](#gpu-support)
  - [Installation - Build From Source](#installation---build-from-source)
    - [Dependencies](#dependencies)
    - [Meson Options](#meson-options)
  - [Usage](#usage)
    - [Vulkan Games](#vulkan-games)
    - [DirectX 11/12 Games](#directx-1112-games)
  - [Hud Configuration](#hud-configuration)
  - [Keybindings](#keybindings)
  - [FPS Logging](#fps-logging)

## About This Fork

This is a **Windows-only** port of [MangoHud](https://github.com/flightlessmango/MangoHud). The original MangoHud is a Linux-first project. This fork replaces the Linux-specific backends with native Windows APIs so the overlay runs natively on Windows without Wine or WSL.

All the core functionality is the same: FPS counter, frame timing graphs, CPU/GPU stats, temperature, VRAM, RAM, battery, network I/O, benchmarking, logging, and configurable HUD layout.

## What Changed

The overlay itself is identical. Under the hood, the system monitoring backends were swapped out for Windows equivalents:

| Feature | Linux (original) | Windows (this fork) |
|---------|-----------------|---------------------|
| GPU enumeration | `/sys/class/drm/` sysfs | DXGI `EnumAdapters` |
| NVIDIA monitoring | NVML + XNVCtrl | NVML (`nvml.dll`) + NVAPI |
| AMD monitoring | sysfs gpu_metrics | ADL (`atiadlxx.dll`) + DXGI VRAM |
| Intel monitoring | fdinfo / i915 perf | DXGI VRAM (basic) |
| VRAM (all vendors) | sysfs / NVML | DXGI `QueryVideoMemoryInfo` |
| CPU stats | `/proc/stat` | `GetSystemTimes()` + `NtQuerySystemInformation` (per-core) |
| CPU frequency | `/sys/devices/system/cpu/` | `CallNtPowerInformation` (per-core MHz) |
| RAM stats | `/proc/meminfo` | `GlobalMemoryStatusEx()` |
| Process memory | `/proc/[pid]/stat` | `GetProcessMemoryInfo()` |
| Battery | `/sys/class/power_supply/` | `GetSystemPowerStatus()` |
| Network I/O | `/sys/class/net/` | IP Helper API (`GetIfTable2`) |
| Disk I/O | `/proc/[pid]/io` | `GetProcessIoCounters()` |
| Keybinds | X11/Wayland key events | `GetAsyncKeyState()` |
| Graphics hook (Vulkan) | Implicit Vulkan layer | Implicit Vulkan layer (same) |
| Graphics hook (GL/DX) | `LD_PRELOAD` GLX/EGL | DXGI proxy DLL (drop next to game exe) |
| Config GUI | GOverlay / MangoJuice | MangoJuice (GTK4, ported to Windows) |
| Config path | `~/.config/MangoHud/` | `%APPDATA%\MangoHud\` |

**Not yet ported** (contributions welcome):
- Intel GPU load/temperature (no public Windows API; DXGI VRAM works)
- CPU temperature (requires admin privileges or LibreHardwareMonitor)
- Media player display (D-Bus not available on Windows)
- D3D12 overlay rendering on MinGW builds (works on MSVC; MinGW builds collect stats but don't render for D3D12 games due to imgui backend limitations)

**Linux-only concepts removed** (not applicable on Windows):
- `mangoapp`, `mangohudctl`, `mangoplot` helper tools
- GameMode / VkBasalt detection
- Wine/Proton sync method display
- Steam Deck fan speed
- FEX emulation stats, ftrace

## GPU Support

| Vendor | Monitoring Method | Metrics Available |
|--------|------------------|-------------------|
| NVIDIA | NVML (`nvml.dll`) | Load, temperature, VRAM, clocks, power, fan speed, throttling |
| AMD | ADL (`atiadlxx.dll`) + DXGI | Load, temperature, clocks, fan speed, VRAM usage |
| Intel | DXGI | VRAM usage, device name (load/temp not available via public API) |

All vendors get VRAM monitoring through DXGI `IDXGIAdapter3::QueryVideoMemoryInfo`, which supplements or backs up the vendor-specific APIs.

## Installation - Build From Source

---

### Cross-compiling from Linux (MinGW)

```bash
git clone --recurse-submodules https://github.com/Leclowndu93150/MangoHud-Windows.git
cd MangoHud-Windows
meson setup build64 --cross-file mingw64.txt
ninja -C build64
```

For 32-bit:
```bash
meson setup build32 --cross-file mingw32.txt
ninja -C build32
```

### Native build on Windows (MSYS2/MinGW)

Install MSYS2, then from a MinGW64 shell:

```bash
pacman -S mingw-w64-x86_64-meson mingw-w64-x86_64-ninja mingw-w64-x86_64-gcc mingw-w64-x86_64-glslang mingw-w64-x86_64-python-mako
git clone --recurse-submodules https://github.com/Leclowndu93150/MangoHud-Windows.git
cd MangoHud-Windows
meson setup build
ninja -C build
```

### Dependencies

- GCC/G++ (MinGW-w64) or MSVC
- Meson >= 0.60
- Ninja
- glslang
- Python 3 + Mako

All other dependencies (imgui, implot, spdlog, vulkan-headers, minhook) are pulled automatically as Meson subprojects.

### Meson Options

| Option | Default | Description |
|--------|---------|-------------|
| with_nvml | enabled | NVML support for NVIDIA GPU metrics |
| loglevel | info | Max log level in release builds |
| tests | auto | Build tests |
| include_doc | true | Install example config files |

## Usage

---

### Vulkan Games

Set the environment variable before launching:

```
set MANGOHUD=1
```

Or register the Vulkan implicit layer in the Windows Registry:
```
HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers
```
Add the full path to `MangoHud.<arch>.json` with a DWORD value of `0`.

### DirectX 11/12 Games (Easy Way - MangoJuice)

Use **MangoJuice** (included, see below) to manage your games. Just add your game, click "Enable", done. MangoJuice copies a DXGI proxy DLL next to the game executable. The game loads it automatically on startup, no injection, no admin rights, no anti-cheat issues. This is the same approach used by ReShade and SpecialK.

### DirectX 11/12 Games (Manual)

Copy `dxgi.dll` from the build output into the same folder as the game's `.exe` file. The game will load it on startup and the overlay will appear automatically.

To remove it, just delete the `dxgi.dll` from the game's folder.

The proxy DLL forwards all DXGI calls to the real system DLL while hooking `Present` to render the overlay. It supports both D3D11 and D3D12, including proper handling of window resize and fullscreen toggling.

## MangoJuice (GUI Configuration Tool)

---

MangoJuice is the graphical configuration tool for MangoHud. It's included as a submodule in the [`mangojuice/`](mangojuice/) directory.

Features:
- Full MangoHud configuration editor (GPU, CPU, memory, battery, visual settings, colors, keybinds, logging)
- **Game Manager**: add games, enable/disable the overlay per-game with one click
- Profile/preset management
- vkBasalt configuration
- Live preview of settings

### Building MangoJuice (requires MSYS2)

From an MSYS2 MinGW64 shell:

```bash
pacman -S mingw-w64-x86_64-gtk4 mingw-w64-x86_64-libadwaita mingw-w64-x86_64-vala mingw-w64-x86_64-libgee
cd mangojuice
meson setup build
ninja -C build
```

The MangoJuice executable and its GTK4/libadwaita runtime DLLs need to be bundled together for distribution.

## Hud Configuration

---

MangoHud looks for configuration files in this order:

1. `%APPDATA%\MangoHud\MangoHud.conf`
2. The `MANGOHUD_CONFIG` environment variable (comma-separated key=value pairs)
3. The `MANGOHUD_CONFIGFILE` environment variable (path to a config file)

Example `MangoHud.conf`:
```ini
fps
gpu_stats
gpu_temp
cpu_stats
cpu_temp
ram
vram
frame_timing
```

See the included `MangoHud.conf.example` for all available options.

Changes to the config file are picked up automatically (the config directory is monitored for file changes).

## Keybindings

---

Default keybindings (configurable in `MangoHud.conf`):

| Keybind | Action |
|---------|--------|
| F12 | Toggle HUD visibility |
| F2 | Toggle FPS logging |
| F4 | Reload configuration |

Keybinds use Windows virtual key codes (VK_ constants).

## FPS Logging

---

When logging is enabled (toggle with F2 by default), MangoHud writes frame time data to a CSV file. The output directory defaults to the current working directory and can be changed via the `output_folder` config option.

Log files can be visualized at [FlightlessMango.com](https://flightlessmango.com) or with any CSV-compatible tool.
