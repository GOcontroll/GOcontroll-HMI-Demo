# GOcontroll-HMI-Demo

A minimal HMI demo for the GOcontroll Moduline Display, built on
[LVGL](https://lvgl.io) (MIT) and rendering directly to KMS/DRM — no Wayland or
X11 compositor required.

It mirrors the functionality of the `go-gtk-demo` (Python + GTK 4 in Cage), but
trades the heavyweight desktop stack for an embedded-first toolkit that matches
the hardware budget of the i.MX 8M Plus / Vivante GC7000 SKU.

## Why LVGL

| | go-gtk-demo (GTK 4 in Cage) | This demo (LVGL on KMS) |
|---|---|---|
| License | LGPL 2.1+ | **MIT** — no link/distribution friction |
| Compositor | Cage (wlroots) | none (direct KMS) |
| Renderer on this SKU | GSK Vulkan → lavapipe (CPU sw via JIT) | LVGL software with **NEON SIMD** on A53 |
| GPU usage | none (etnaviv exposes only GLES 2.0; GTK 4 needs ≥ 3.0) | optional GLES 2.0 backend (`-DUSE_OPENGLES=ON`) |
| RSS (steady state) | ~225 MB | < 25 MB (target) |
| First frame after service start | ~5 s | < 1 s (target) |
| Threads | 18 (incl. 3× llvmpipe workers) | 1 main + LVGL timer |
| GTK thread-safety footgun | `widget.set_label()` from worker | single-threaded `lv_timer` |

## Layout

```
src/
  main.c        program entry, signal handling, main loop
  display.c     DRM init via LVGL's lv_linux_drm + 180° flip
  input.c       cyttsp6_mt via lv_evdev with software calibration
  ipc.c         process_vm_readv/writev wrappers (Simulink IPC)
  config.c      signals.json / parameters.json loader
  ui.c          widgets, voltage timer, slider/exit handlers
lv_conf.h       LVGL build configuration
assets/         GOcontroll.png logo
debian/         packaging (systemd unit, install map, control)
thirdparty/lvgl LVGL v9.5.0 as git submodule
```

## Building from source

Native build on the device:

```sh
git clone --recurse-submodules https://github.com/GOcontroll/GOcontroll-HMI-Demo.git
cd GOcontroll-HMI-Demo
sudo apt install -y cmake libdrm-dev libgbm-dev libegl-dev libgles-dev \
    libevdev-dev libcjson-dev libpng-dev libfreetype-dev build-essential
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/gocontroll-hmi-demo            # run from a non-graphical TTY
```

CMake options:

* `-DUSE_OPENGLES=ON` — route LVGL's output through OpenGL ES 2.0 via EGL+GBM
  on the GC7000 GPU. Adds `libgbm`/`libegl` runtime dep. Default OFF; the
  static UI is too small for the GPU upload to pay off, but the option is here
  for richer UIs.

## Building the Debian package

```sh
sudo apt install -y debhelper dpkg-dev
dpkg-buildpackage -us -uc -b
sudo apt install ../gocontroll-hmi-demo_*.deb
```

The package installs:

* `/usr/bin/gocontroll-hmi-demo` — the binary
* `/usr/bin/gocontroll_demo` — the Simulink companion (same as `go-gtk-demo`;
  the two packages **conflict**)
* `/usr/share/gocontroll-hmi-demo/{GOcontroll.png,signals.json,parameters.json}`
* `/usr/lib/systemd/system/gocontroll-hmi-demo.service` (auto-launches on boot)
* `/usr/lib/systemd/system/gocontroll_demo.service` (Simulink backend)

## Generating signals.json / parameters.json

The `.json` files are produced from a Simulink-generated `.a2l` by
`parse_a2l.py` (see the `go-gtk-demo` repo). They are gitignored on purpose —
ship-generated artifacts. For convenience, fallback addresses derived from the
ELF symbols of the included `gocontroll_demo` binary are embedded in `config.c`
and used if the JSONs are absent.
