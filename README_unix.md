# WinUAE Unix Port

This is an early macOS/Linux port of the WinUAE source tree. The current Unix build is a native executable with SDL3 video/input support when SDL3 is available and an integrated Qt configuration UI when Qt Widgets is available.

## Current Status

- Builds with CMake as the `winuae_unix` target, producing a `winuae` binary.
- Uses `od-unix/` host abstractions.
- SDL3 provides the current window, framebuffer presentation, mouse input, keyboard input, audio output, and playback-device selection.
- SDL3 gamepads and non-gamepad joysticks are exposed through the WinUAE input-device layer for game-port use; the Qt Game Ports/Input pages have first remap/test dialogs backed by SDL3 device enumeration and WinUAE config keys.
- Qt Widgets provides an initial Windows-style configuration UI. When Qt is available, it is integrated into `winuae_unix`.
- Host clipboard text paste is available through the same paste input event as Windows. The `clipboard_sharing` option has native text clipboard-device support, and SDL3 builds exchange bitmap clipboard data through `image/bmp`, `image/png` when libpng is available, and macOS `image/tiff` through ImageIO, bridged through Amiga IFF ILBM conversion.
- Floppy drive click sounds are present and audible when enabled.
- The integrated Qt Output page can toggle the core Sample ripper; ripped WAV files use the configured Rips path.
- The integrated Qt Output page can run Pro Wizard when the default `WINUAE_UNIX_WITH_PROWIZARD` build option is enabled. Save prompts use Qt warning dialogs with the same OK/Yes/No/Cancel return contract as Windows.
- When opened during emulation, the integrated Qt Output page can play, start/stop, and save core input re-recordings.
- The Qt Paths page now writes real Unix target path settings for configuration files, NVRAM, screenshots, videos, save images, rips, data, and ROMs, so runtime helpers use the configured directories. Older `unix.ui.*` path keys are still read for compatibility.
- Native Unix serial support is available for POSIX serial devices and TCP listener endpoints.
- A2065 Ethernet and SANA-II `uaenet.device` can use the built-in SLIRP user-mode NAT backend, libpcap-backed native packet adapters, and direct TAP/TUN devices where the host exposes them.
- Unix JIT is wired for ARM64 and x86_64 hosts. macOS uses `MAP_JIT` plus write/execute protection switching where required.
- Native CD/DVD access is available on macOS and Linux; `WINUAE_UNIX_WITH_NATIVE_SCSI` adds Linux SG_IO and macOS SCSITaskLib direct SCSI/tape passthrough through the same SPTI-style slot used by Windows.
- The shared Toccata/Prelude/UAESND sound-board backend and PCI ES1370/FM801 paths are built by default, with SDL3 host capture hooks when SDL3 is available.
- UAE Zorro II/Zorro III RTG RAM can be configured and autoconfigured, with an initial Unix `uaegfx.card` install path and guest Picasso96 monitor-driver smoke coverage, including 8-bit Workbench mode open and direct 15-bit/16-bit/24-bit/32-bit P96 screen-open tests. Accelerated RTG operations are still incomplete.
- The Qt Expansions page can enable common Zorro/expansion board ROM entries, CPU boards, PPC quickstart presets, and shared sound boards using the same `*_rom_file`, `*_rom_options`, and CPU-board keys as WinUAE. PPC emulation uses the external `qemu-uae` plugin ABI; a QEMU 11.0 based plugin tree is expected next to `WinUAE/` as `qemu-uae-v11.0`.
- Full UI parity with the Windows configuration dialogs and platform packaging are still incomplete.
- If SDL3 is not found, CMake currently builds a headless/null-video target.

## Requirements

- CMake 3.20 or newer
- C and C++ compiler with C++17 support
- zlib development headers
- pkg-config or pkgconf
- SDL3 development headers and libraries, recommended for a usable windowed emulator
- Qt 6 or Qt 5 Widgets, recommended for the native Unix configuration UI

### macOS

Install Xcode Command Line Tools and Homebrew dependencies:

```sh
xcode-select --install
brew install cmake pkg-config sdl3
```

For the Qt frontend:

```sh
brew install qt
```

The system zlib is normally enough on macOS.

The macOS build defaults to `CMAKE_OSX_DEPLOYMENT_TARGET=13.0` so the app is not accidentally tied to the build machine's current macOS release. Bundled libraries and frameworks must support the same or an older deployment target. The packaging script checks every bundled Mach-O file and fails if, for example, Homebrew SDL3 or Qt was built with a newer `minos` than the app target. Use the private dependency build below for repeatable release artifacts.

For repeatable release builds, build SDL3, QtBase, libFLAC when CHD is enabled, and optional libraries such as libpng into a private prefix with the same deployment target instead of using Homebrew bottles. The helper defaults QtBase to bundled third-party libraries so Homebrew dylibs with newer deployment targets are not pulled into the release app. If SDL3's CMake or pkg-config metadata is missing from the private prefix, the WinUAE CMake build can still use a matching `include/SDL3` and `libSDL3.0.dylib` pair from `CMAKE_PREFIX_PATH`. Supplying `WINUAE_LIBPNG_SOURCE` gives the Unix screenshot backend a deployment-target-compatible PNG library; without it, CMake may skip a too-new Homebrew libpng and fall back to BMP screenshots. Supplying `WINUAE_FLAC_SOURCE` lets the dependency helper build CHD's required FLAC library with the same deployment target. Supplying `WINUAE_LIBMPEG2_SOURCE` (the libmpeg2 0.5.1 tarball from videolan.org, extracted) enables CD32 FMV video decode; without a deployment-target-compatible libmpeg2, CMake silently disables FMV playback. PPC release packages also need deployment-target-compatible QEMU-UAE dependencies such as GLib, gettext, and PCRE2; Homebrew bottles built for a newer macOS will be rejected by the bundle verifier.

```sh
WINUAE_MACOS_DEPLOYMENT_TARGET=13.0 \
WINUAE_SDL3_SOURCE=/path/to/SDL3-source \
WINUAE_QT_SOURCE=/path/to/qtbase-source \
WINUAE_LIBPNG_SOURCE=/path/to/libpng-source \
WINUAE_FLAC_SOURCE=/path/to/flac-source \
WINUAE_LIBMPEG2_SOURCE=/path/to/libmpeg2-0.5.1 \
tools/macos-build-deps.sh /opt/winuae-macos-13

source /opt/winuae-macos-13/winuae-macos-deps-env.sh
cmake -S . -B /tmp/winuae_cmake_build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_PREFIX_PATH=/opt/winuae-macos-13
```

The same helper is available as a CMake target after configure; pass source paths through the environment:

```sh
WINUAE_SDL3_SOURCE=/path/to/SDL3-source \
WINUAE_QT_SOURCE=/path/to/qtbase-source \
WINUAE_LIBPNG_SOURCE=/path/to/libpng-source \
WINUAE_FLAC_SOURCE=/path/to/flac-source \
WINUAE_LIBMPEG2_SOURCE=/path/to/libmpeg2-0.5.1 \
cmake --build /tmp/winuae_cmake_build --target winuae_unix_macos_deps
```

To target a different macOS release, configure with an explicit deployment target and build/bundle SDL3 and Qt with a matching target:

```sh
cmake -S . -B /tmp/winuae_cmake_build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
```

### Debian/Ubuntu

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config zlib1g-dev libsdl3-dev
```

For the Qt frontend:

```sh
sudo apt install qt6-base-dev
```

For native ALSA MIDI support:

```sh
sudo apt install libasound2-dev
```

### Fedora

```sh
sudo dnf install gcc gcc-c++ cmake pkgconf-pkg-config zlib-devel SDL3-devel
```

For the Qt frontend:

```sh
sudo dnf install qt6-qtbase-devel
```

For native ALSA MIDI support:

```sh
sudo dnf install alsa-lib-devel
```

## Build

From the `WinUAE/` directory:

```sh
cmake -S . -B /tmp/winuae_cmake_build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/winuae_cmake_build --target winuae_unix -j
```

The executable will be:

```sh
/tmp/winuae_cmake_build/winuae
```

On Linux, install the executable, shared resources, documentation, and desktop/MIME metadata into a prefix with:

```sh
cmake --install /tmp/winuae_cmake_build --prefix /opt/winuae
```

This installs the desktop entry, `.uae` MIME type, and hicolor icons. The
binary is named `winuae` everywhere — build tree, installed command, and app
bundle; only the CMake target keeps the `winuae_unix` name for developer
stability. On macOS, use the `.app` and DMG targets below instead of
installing the raw executable.

On Linux, CPack can create packages from the same install rules:

```sh
cmake --build /tmp/winuae_cmake_build --target package
```

This currently produces a `.tar.gz` package and, when Debian packaging tools
are available, a `.deb` package with shared-library dependencies inferred by
`dpkg-shlibdeps`. The Debian package installs the emulator as `winuae`.

For a Debian/Ubuntu package build from a clean CMake directory, use:

```sh
tools/debian-build-package.sh --build-dir /tmp/winuae_deb_build
```

The helper checks the Linux desktop/MIME/icon packaging metadata, requires a
`qemu-uae.so` plugin when PPC support is enabled, builds the CPack `DEB`
package, and prints the generated `.deb` path and package fields. Extra CMake
options can be passed after `--`, for example:

```sh
tools/debian-build-package.sh -- -DWINUAE_UNIX_WITH_QT_UI=OFF
```

Archive and CHD support compile the 7-Zip/LZMA SDK sources. A system SDK
source package (for example the openSUSE `lzma-sdk-devel` package) is used
when found under `WINUAE_LZMA_SDK_SYSTEM_PATHS` (or an explicit
`-DWINUAE_LZMA_SDK_DIR=`); otherwise `WINUAE_LZMA_SDK_FETCH` downloads the
SHA-verified `lzma1604.7z` archive. Pass `-DWINUAE_LZMA_SDK_FETCH=OFF` for
offline distribution builds.

The `qemu-uae.so` PPC plugin is a mandatory part of Unix packages. CMake
resolves it at configure time, in this order, and fails the configure with
instructions when none applies:

1. `-DWINUAE_QEMU_UAE_PLUGIN_FILE=/path/to/qemu-uae.so` — a prebuilt plugin.
2. A patched `qemu-uae` source tree at `WINUAE_QEMU_UAE_SOURCE_DIR`
   (default: sibling `../qemu-uae-v11.0`) — the developer path.
3. The `uae-ppc-plugin` builder at `WINUAE_QEMU_UAE_BUILDER_DIR`
   (default: sibling `../uae-ppc-plugin`). The builder downloads a
   SHA-verified QEMU source tarball, applies the UAE patch deck, and builds
   the plugin during the WinUAE build. For offline builds (distribution
   packaging), declare the QEMU tarball as a package source and pass
   `-DWINUAE_QEMU_UAE_QEMU_TARBALL=/path/to/qemu-11.0.1.tar.xz`.
4. When no builder checkout is found and `WINUAE_QEMU_UAE_BUILDER_FETCH` is
   `ON` (the default), the builder is cloned at build time from
   `WINUAE_QEMU_UAE_BUILDER_URL`.

On Linux the plugin target is part of the default build, so plain
`cmake --build` + `cmake --install` flows always produce and install the
plugin. A distribution package build needs `meson`, `ninja`, GLib and libfdt
development headers for the embedded QEMU build, plus the builder checkout
and QEMU tarball as additional sources when the build host has no network
access.

The Linux install/package metadata can be checked on any Unix host:

```sh
cmake --build /tmp/winuae_cmake_build --target winuae_unix_linux_package_metadata_check
```

If Qt Widgets is available, CMake links the Windows-style configuration UI into `winuae_unix` by default.

On macOS, a local `WinUAE.app` bundle can be created from the same build:

```sh
cmake --build /tmp/winuae_cmake_build --target winuae_unix_macos_app -j
```

The app bundle is written to:

```sh
/tmp/winuae_cmake_build/package/WinUAE.app
```

The bundling script copies the Qt UI resources and runs `macdeployqt` when it is available. It does not bundle Kickstart ROMs, disks, or other Amiga system media.

For a GUI launch smoke test of the packaged app, run:

```sh
cmake --build /tmp/winuae_cmake_build --target winuae_unix_macos_app_smoke
```

This starts `WinUAE.app` from an isolated temporary home directory and uses an opt-in smoke-test environment variable to verify that the Qt configuration window reaches a visible state.

To create a drag-install DMG with `WinUAE.app`, an `/Applications` link, a volume icon, and a Finder background arrow:

```sh
cmake --build /tmp/winuae_cmake_build --target winuae_unix_macos_dmg -j
```

The DMG is written next to the app bundle in `/tmp/winuae_cmake_build/package/`.
The DMG target verifies the generated image by mounting it, checking the app bundle, `/Applications` link, Finder layout metadata, volume icon, background image, and `.uae` document declaration, then running the bundled executable with `-h` from an isolated temporary home directory. You can rerun the verification directly:

```sh
tools/macos-verify-dmg.sh /tmp/winuae_cmake_build/package/WinUAE-6.1.0.dmg
```

For a single local release gate, build:

```sh
cmake --build /tmp/winuae_cmake_build --target winuae_unix_macos_release_check -j
```

This produces and verifies the DMG, then launches the packaged Qt app from an isolated temporary home directory when the integrated Qt UI is enabled. Use the private dependency prefix path for release artifacts instead of newer Homebrew Qt/SDL bottles.

### QEMU-UAE PPC Plugin

The WinUAE side of PPC accelerator support is built by default with
`WINUAE_UNIX_WITH_PPC_QEMU=ON`. The PowerPC CPU itself is a separate
`qemu-uae` plugin loaded at runtime. The default source layout is:

```sh
WinUAE_mac/
  WinUAE/
  qemu-uae-v11.0/
```

The helper builds the sibling QEMU 11.0 plugin tree and can copy the result
into a WinUAE build directory:

```sh
tools/build-qemu-uae.sh ../qemu-uae-v11.0 /tmp/winuae_cmake_build/qemu-uae.so
```

On macOS release builds, build GLib into the private dependency prefix first so
the plugin does not inherit newer Homebrew dylibs:

```sh
WINUAE_GLIB_SOURCE=/path/to/glib-2.88.1 \
tools/macos-build-qemu-deps.sh ../winuae-macos-deps
```

CMake also exposes this as:

```sh
cmake --build /tmp/winuae_cmake_build --target winuae_unix_qemu_uae_plugin -j
```

`WINUAE_UNIX_BUILD_QEMU_UAE_PLUGIN` controls whether the plugin build target
is enabled, and `WINUAE_QEMU_UAE_SOURCE_DIR` can point at a different
`qemu-uae` source tree. `WINUAE_QEMU_UAE_PLUGIN_FILE` can point at an already
built plugin. Without a patched source tree, the target builds through the
release-oriented `uae-ppc-plugin` builder (`WINUAE_QEMU_UAE_BUILDER_DIR`,
cloned from `WINUAE_QEMU_UAE_BUILDER_URL` when missing and
`WINUAE_QEMU_UAE_BUILDER_FETCH` is enabled; `WINUAE_QEMU_UAE_QEMU_TARBALL`
supplies a local QEMU tarball for offline builds). `WINUAE_QEMU_UAE_DEPS_PREFIX`
defaults to the private macOS dependency prefix and is passed to the plugin
helper as `QEMU_UAE_DEPS_PREFIX`. Set `QEMU_UAE_NINJA=/path/to/ninja` if QEMU
configure cannot find Ninja itself. On macOS, the app bundler copies
`qemu-uae.so` into `WinUAE.app/Contents/PlugIns/` before dependency deployment.
On Linux, install/package rules copy it into
`$libdir/winuae/plugins/qemu-uae.so`, and the runtime loader searches that path
relative to the installed `winuae` binary. App and CPack package targets fail
if PPC support is enabled but the plugin is missing.

Local app bundles are ad-hoc signed by default. For Developer ID release signing and notarization, pass signing/notary settings through the packaging environment:

```sh
WINUAE_CODESIGN_IDENTITY="Developer ID Application: Example Team" \
WINUAE_CODESIGN_OPTIONS="--options runtime --timestamp" \
WINUAE_DMG_CODESIGN_IDENTITY="Developer ID Application: Example Team" \
WINUAE_DMG_CODESIGN_OPTIONS="--timestamp" \
WINUAE_NOTARY_PROFILE=winuae-notary \
cmake --build /tmp/winuae_cmake_build --target winuae_unix_macos_dmg -j
```

`WINUAE_NOTARY_PROFILE` is the keychain profile configured with `xcrun notarytool store-credentials`. Leave it unset for local unsigned or ad-hoc signed builds.

To force a configure from scratch:

```sh
rm -rf /tmp/winuae_cmake_build
cmake -S . -B /tmp/winuae_cmake_build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/winuae_cmake_build --target winuae_unix -j
```

## Run

The port accepts normal WinUAE command-line `-s` configuration overrides. A minimal A1200 example:

```sh
/tmp/winuae_cmake_build/winuae \
  -s kickstart_rom_file=/path/to/A1200.rom \
  -s floppy0=/path/to/disk.adf \
  -s nr_floppies=1 \
  -s chipset=aga \
  -s chipset_compatible=A1200 \
  -s cpu_model=68020 \
  -s chipmem_size=4 \
  -s cachesize=0
```

When the integrated Qt UI is built, `winuae_unix` opens the configuration UI by default. To boot directly from a config or command-line settings, disable the GUI:

```sh
/tmp/winuae_cmake_build/winuae \
  -f /path/to/config.uae \
  -s use_gui=no
```

The executable also tries to load `default.uae` from the configuration path by default, and the Qt configuration UI starts with it loaded when it exists — save a configuration named `default` to set your startup defaults, like on Windows. Configs given on the command line take precedence. A missing default config is ignored silently; explicit `-config` / `-f` load failures are still reported.

Host settings (the Paths page directories and flags) persist in `winuae.ini`:
a `winuae.ini` next to the executable is used when present (portable mode,
matching Windows); otherwise `$XDG_CONFIG_HOME/winuae/winuae.ini` on Linux
(default `~/.config/winuae/winuae.ini`) and
`~/Library/Application Support/WinUAE/winuae.ini` on macOS. The `WINUAE_INI`
environment variable overrides the location. Configs loaded per session can
still override individual paths through the `unix.*_path` settings, which win
over the stored host defaults.

Unix path expansion is supported for `~/`, `$VAR`, and `${VAR}` in core config paths and Qt file/config boundaries. Relative config and media paths are resolved against the process working directory, matching the non-relative Windows save mode; the Windows-style relative-path save option is not enabled on Unix yet. The Qt Paths page saves runtime-visible target path settings such as `unix.screenshot_path`, `unix.rip_path`, `unix.video_path`, and `statefile_path`; legacy `unix.ui.*` path settings are still accepted when loading older configs. `~user` expansion is not implemented; use an absolute path for another user's home directory.

For SANA-II `uaenet.device` startup testing, add `-s sana2=true` or use the smoke target below. Guest TCP/IP stack validation is still pending.

## Audio

SDL3 audio uses the WinUAE `sound_output`, `sound_frequency`, `sound_channels`, `sound_volume*`, and floppy drive sound config keys. Unix playback device selection follows the same target-prefixed style as Windows:

```sh
/tmp/winuae_cmake_build/winuae \
  -s unix.soundcard=0 \
  -s 'unix.soundcardname=SDL:Default Audio Device'
```

The Qt Sound page lists SDL playback devices and writes both `unix.soundcard` and `unix.soundcardname` so saved configs can recover by name if the device index changes. Floppy click sounds are off by default, matching the shared WinUAE defaults; enable them per drive with `floppy0sound=1` through `floppy3sound=1`.

## Clipboard

Host clipboard text can be pasted into the emulated keyboard through `SPC_PASTE`. The default keyboard mapping follows Windows: the special qualifier plus Insert triggers paste. SDL3 builds use the native SDL clipboard text API and clipboard-change events; non-SDL/fallback paths use `pbpaste` on macOS or `wl-paste`, `xclip`, then `xsel` on Linux.

Transparent Amiga clipboard-device sharing can also be enabled with `clipboard_sharing=true`. The Unix backend supports text in both directions using SDL3 where available, with `pbpaste`/`pbcopy` on macOS or `wl-paste`/`wl-copy`, `xclip`, or `xsel` on Linux as fallbacks. SDL3 builds also exchange bitmap clipboard data through `image/bmp`, when libpng is linked `image/png`, and on macOS `image/tiff` / `public.tiff` through ImageIO, using Amiga IFF ILBM as the guest-side bridge. Additional Linux desktop image MIME variants still need native handling.

## Serial

The Unix serial backend follows the same target-prefixed config style as Windows. Use `unix.serial_port` for command-line overrides and saved configs:

```sh
# Real serial device
/tmp/winuae_cmake_build/winuae \
  -s unix.serial_port=/dev/cu.usbserial-0001 \
  -s serial_hardware_ctsrts=true

# Telnet-style TCP listener on all local interfaces, port 1234
/tmp/winuae_cmake_build/winuae \
  -s unix.serial_port=TCP://0.0.0.0:1234
```

`TCP:host:port`, `TCP://host:port`, and `TCP:port` are accepted. Add `/wait` to delay startup until a client connects, for example `TCP://0.0.0.0:1234/wait`. Connect locally with `telnet 127.0.0.1 1234` or `nc 127.0.0.1 1234`.

## Smoke Test

The repository includes a headless A1200 smoke test. It uses SDL dummy video/audio by default and checks that ROM loading, audio initialization, and hard reset reached the expected log points:

```sh
export WINUAE_KICKSTART_ROM=/path/to/A1200.47.115.rom
export WINUAE_FLOPPY0=/path/to/Install3.2.adf
tools/unix-smoke-a1200.sh
```

The same scripts are also available as CMake targets. The targets set `WINUAE_BUILD_DIR` to the active build directory and still read the same ROM/disk environment variables:

```sh
cmake --build "$WINUAE_BUILD_DIR" --target winuae_unix_smoke_a1200
cmake --build "$WINUAE_BUILD_DIR" --target winuae_unix_smoke_path_config
```

`winuae_unix_smoke_basic` runs the A1200 boot smoke and the path/config smoke.

The host-side unit tests can be built and run through one CMake target:

```sh
cmake --build "$WINUAE_BUILD_DIR" --target winuae_unix_check
```

`winuae_unix_tests` only builds the test executables. `ctest --output-on-failure` can be run directly from the build directory after that.

For the Unix host semaphore/event primitive test:

```sh
cmake --build "$WINUAE_BUILD_DIR" --target winuae_unix_threading_test
"$WINUAE_BUILD_DIR/winuae_unix_threading_test"
```

To include A2065 SLIRP autoconfig in the same smoke path:

```sh
tools/unix-smoke-a2065.sh
```

To include SANA-II `uaenet.device` startup and SLIRP unit enumeration in the same smoke path:

```sh
tools/unix-smoke-sana2.sh
```

To include Zorro III RTG RAM autoconfig in the same smoke path:

```sh
tools/unix-smoke-rtg-z3.sh
```

To cover the shared sound-board startup paths, provide A1200 and A4000 Kickstart ROMs:

```sh
export WINUAE_A1200_KICKSTART_ROM=/path/to/A1200.rom
export WINUAE_A4000_KICKSTART_ROM=/path/to/A4000.rom
tools/unix-smoke-sound-boards.sh
cmake --build "$WINUAE_BUILD_DIR" --target winuae_unix_smoke_sound_boards
```

That smoke checks Toccata, Prelude, Prelude 1200, UAESND Z2/Z3,
UAEBOARD Z2/Z3, and ES1370/FM801 behind Prometheus, Prometheus
FireStorm, Mediator 4000, and G-REX PCI bridges.

For manual A4091 autoconfig smoke tests, use an A4000/A4000T-style config, disable 24-bit CPU addressing, and provide a real A4091 ROM:

```sh
/tmp/winuae_cmake_build/winuae \
  -s use_gui=no \
  -s kickstart_rom_file=/path/to/A4000.rom \
  -s a4091_rom_file=/path/to/a4091.rom \
  -s chipset=aga \
  -s chipset_compatible=A4000 \
  -s cpu_model=68030 \
  -s cpu_24bit_addressing=false
```

For image-backed hardfiles on the A4091, choose `A4091 (SCSI)` in the Qt hardfile dialog or use a hardfile tail such as `scsi0_a4091`. A manual A4091 ROM plus HDF boot has been validated; other NCR/NCR9x boards still need real-ROM validation.

To automate the A4091 HDF smoke path:

```sh
export WINUAE_A4000_KICKSTART_ROM=/path/to/A4000.rom
export WINUAE_A4091_ROM=/path/to/a4091.rom
export WINUAE_A4091_HDF=/path/to/disk.hdf
tools/unix-smoke-a4091-hdf.sh
```

RIPPLE and AlfaPower/AT-Bus 2008 can be enabled without an external ROM path. In the Qt UI, enable the board on the Expansions page first, then choose `RIPPLE (IDE)` or `AlfaPower/AT-Bus 2008 (IDE)` in the hardfile dialog. The matching config tails are `ide0_ripple` and `ide0_alfapower`.

The IDE expansion smoke targets create a temporary blank HDF if `WINUAE_IDE_EXPANSION_HDF` or `WINUAE_HARDFILE0` is not set:

```sh
export WINUAE_A1200_KICKSTART_ROM=/path/to/A1200.rom
tools/unix-smoke-alfapower-hdf.sh
tools/unix-smoke-ripple-hdf.sh
```

To automate the TCP serial listener path, including the Windows-style `/wait` startup behavior:

```sh
export WINUAE_KICKSTART_ROM=/path/to/A1200.47.115.rom
export WINUAE_FLOPPY0=/path/to/Install3.2.adf
tools/unix-smoke-serial-tcp.sh
```

The default port is `51234`. Override it with `WINUAE_SERIAL_TCP_PORT` if that port is already in use.

To exercise Unix path expansion through a real config file and command-line override:

```sh
export WINUAE_KICKSTART_ROM=/path/to/A1200.47.115.rom
export WINUAE_FLOPPY0=/path/to/Install3.2.adf
tools/unix-smoke-path-config.sh
```

To let the boot continue long enough to verify Zorro III RTG RAM autoconfig and `uaegfx.card` installation:

```sh
tools/unix-smoke-uaegfx.sh
```

That smoke verifies the current Unix install-level RTG path: Z3 RTG RAM is mapped, `uaegfx.card` is installed, and P96 resolution memory is allocated. It does not prove a guest Picasso96 monitor driver has opened the board or switched to an RTG screen. For a Workbench/Picasso96 setup that should exercise those paths, enable stricter log checks and append any config or override arguments needed by that setup:

```sh
export WINUAE_SMOKE_UAEGFX_DRIVER=1
export WINUAE_SMOKE_UAEGFX_SCREEN=1
tools/unix-smoke-uaegfx.sh -f /path/to/p96-workbench.uae
```

The stricter guest P96 smoke targets require a copied Workbench/Picasso96 tree:

```sh
export WINUAE_KICKSTART_ROM=/path/to/A1200.47.115.rom
export WINUAE_A4000_KICKSTART_ROM=/path/to/A4000.47.115.rom
export WINUAE_P96_WORKBENCH_DIR=/path/to/Workbench
cmake --build /tmp/winuae_cmake_build --target winuae_unix_smoke_p96_guest_800x600
cmake --build /tmp/winuae_cmake_build --target winuae_unix_smoke_p96_guest_16bit_modes
cmake --build /tmp/winuae_cmake_build --target winuae_unix_smoke_p96_guest_15bit_open
cmake --build /tmp/winuae_cmake_build --target winuae_unix_smoke_p96_guest_16bit_open
cmake --build /tmp/winuae_cmake_build --target winuae_unix_smoke_p96_guest_32bit_open
cmake --build /tmp/winuae_cmake_build --target winuae_unix_smoke_p96_guest_draw_8bit
cmake --build /tmp/winuae_cmake_build --target winuae_unix_smoke_p96_guest_draw_16bit
```

The `16bit_modes` target verifies the Windows-compatible `rtg_modes=0x10` mode-list path. The `15bit_open`, `16bit_open`, `24bit_open`, and `32bit_open` targets additionally build a tiny Amiga-side Picasso96API helper with `m68k-amigaos-gcc` and verify direct `640x480x15` / `640x480x16` / `640x480x24` / `640x480x32` P96 screen opens through `SetGC`/`SetPanning`. The `draw_8bit` and `draw_16bit` targets enable Unix RTG trace logging and verify guest drawing calls through the CPU blitter callbacks. If that compiler is not installed, set `WINUAE_P96_OPEN_SCREEN_BINARY` to a prebuilt helper binary.

Optional overrides:

```sh
export WINUAE_BUILD_DIR=/tmp/winuae_cmake_build
export WINUAE_EXE=/tmp/winuae_cmake_build/winuae
export WINUAE_SMOKE_SECONDS=5
export WINUAE_SMOKE_LOG=/tmp/winuae_unix_smoke.log
```

## SDL Input

- Click inside the window to grab mouse input.
- Press `Esc` while grabbed to release the mouse.
- Press `Ctrl+G` or `Cmd+G` to release the mouse.
- Press `Ctrl+Q` or `Cmd+Q` to quit.
- SDL3 gamepads use the standard SDL layout: left stick and D-pad map to joystick directions, South/East/West/North map to the first four buttons, and CD32 mode follows the Windows default button order where possible.
- Non-gamepad SDL joysticks expose their native axes, hats, and buttons; hats also map to joystick directions by default.
- Press `F12` to open the integrated Qt settings UI during emulation. The button row matches the Windows GUI: `OK` applies the edited configuration and resumes (floppy/CD changes are picked up live, including ejects), `Reset` applies the configuration and hard-resets the Amiga, `Restart` quits the running Amiga and returns to the launcher, `Quit` exits the emulator, and `Cancel` resumes without applying changes.
- The screenshot file input event and integrated runtime Qt Output-page button save under the configured Screenshots directory. Unix uses PNG when libpng is found at configure time and falls back to BMP otherwise. On macOS, libpng must also pass the configured deployment-target check; a newer Homebrew libpng is skipped so release builds stay compatible with the selected minimum macOS. SDL3 builds can also copy the screenshot event output to the host clipboard as BMP data, and clipboard sharing can exchange PNG plus macOS TIFF image data when the matching native codecs are available. Unix screenshots now cover autoclip, palette-indexed PNG when the framebuffer has 256 or fewer colors, continuous screenshot directories, and the savestate thumbnail byte helper. The Output page can also start internal DIB RGB AVI capture, PCM-in-AVI audio capture, and WAV audio capture.
- SDL3 builds can enable an OpenGL shader presenter with `WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE`. When OpenGL is available, the Filter page's portable color, blur, noise, scanline, bilinear, and geometry controls are applied by a native GLSL path, and the Filter selector offers named GLSL scalers (currently Scale2x/EPX, stored as the `unix.gfx_shader` config option; it forces nearest-neighbour sampling and applies to the emulation frame only). Direct3D shader preset files, mask/overlay chains, HDR, and Metal/Vulkan backends are still future work.
- CHD hardfile and CD image support is built by default. CHD FLAC codecs require libFLAC that is compatible with the configured macOS deployment target; otherwise CHD remains enabled without FLAC-compressed CD codecs.
- Hold `End` and press `F1`-`F4` to change DF0:-DF3:, `F5` to change the CD image, and `F6` to restore a state. Hold `Shift` with those shortcuts to eject the matching floppy/CD image or save state, matching the Windows key map.
- On MacBook or compact Apple keyboards, `End` is usually `Fn`/Globe + `Right Arrow`. Depending on macOS keyboard settings, function keys may also need `Fn`/Globe, so the MacBook form is `Fn`/Globe + `Right Arrow`, then `F1`-`F6` or `Shift` + `F1`-`F6`. Enabling "Use F1, F2, etc. keys as standard function keys" in macOS makes these closer to the Windows chords.
- The SDL status strip at the bottom of the emulation window mirrors the Windows basics: left-click DF0:-DF3: or CD to choose media, right-click them to eject, left-click the power area for settings, right-click it for soft reset, and left-click the paused FPS area to resume.

## Useful CMake Options

```sh
-DWINUAE_UNIX_BUILD_EXECUTABLE=ON
-DWINUAE_UNIX_WITH_SDL3=ON
-DWINUAE_UNIX_WITH_SLIRP=ON
-DWINUAE_UNIX_WITH_SANA2=ON
-DWINUAE_UNIX_WITH_UAENET_PCAP=ON
-DWINUAE_UNIX_WITH_NCR_SCSI=ON
-DWINUAE_UNIX_WITH_BSDSOCKET=ON
-DWINUAE_UNIX_WITH_UAESCSI=ON
-DWINUAE_UNIX_WITH_UAESERIAL=ON
-DWINUAE_UNIX_WITH_NATIVE_CD=ON
-DWINUAE_UNIX_WITH_NATIVE_SCSI=ON
-DWINUAE_UNIX_WITH_CHD=ON
-DWINUAE_UNIX_WITH_CHD_FLAC=ON
-DWINUAE_LZMA_SDK_FETCH=ON
-DWINUAE_UNIX_WITH_JIT=ON
-DWINUAE_UNIX_WITH_PPC_QEMU=ON
-DWINUAE_UNIX_BUILD_QEMU_UAE_PLUGIN=ON
-DWINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE=ON
-DWINUAE_UNIX_WITH_SNDBOARD=ON
-DWINUAE_UNIX_WITH_PROWIZARD=ON
-DWINUAE_UNIX_WITH_QT_UI=ON
-DWINUAE_UNIX_WITH_INTEGRATED_QT_UI=ON
```

`WINUAE_UNIX_WITH_SDL3` is enabled by default. If SDL3 is not found through CMake package discovery or pkg-config, the build currently falls back to the null video presenter.
`WINUAE_UNIX_WITH_SLIRP` is enabled by default and builds the bundled SLIRP backend plus A2065 emulation.
`WINUAE_UNIX_WITH_SANA2` is enabled by default and builds `uaenet.device` on top of the Unix Ethernet backend when SLIRP is also enabled.
`WINUAE_UNIX_WITH_UAENET_PCAP` is enabled by default and builds native packet networking through libpcap plus direct TAP/TUN entries (`tap:<ifname>` / `tun:<ifname>`) where the host provides them.
`WINUAE_UNIX_WITH_NCR_SCSI` is enabled by default and builds the NCR/NCR9x SCSI controller emulation used by boards such as A4091. ROM-backed controller boards still need a valid board ROM path in the config.
`WINUAE_UNIX_WITH_BSDSOCKET`, `WINUAE_UNIX_WITH_UAESCSI`, and `WINUAE_UNIX_WITH_UAESERIAL` are enabled by default and build the shared Amiga-side devices against Unix host backends.
`WINUAE_UNIX_WITH_NATIVE_CD` is enabled by default and builds native CD/DVD access on macOS and Linux. `WINUAE_UNIX_WITH_NATIVE_SCSI` is enabled by default on macOS and Linux and adds macOS SCSITaskLib plus Linux SG_IO direct SCSI/tape passthrough through the same device slot Windows uses for SPTI.
`WINUAE_UNIX_WITH_CHD` is enabled by default and builds CHD hardfile/CD image support. `WINUAE_UNIX_WITH_CHD_FLAC` is also enabled by default, but macOS builds skip FLAC codecs if the available libFLAC was built for a newer deployment target.
`WINUAE_LZMA_SDK_FETCH` is enabled by default. When archive or CHD support needs the 7-Zip/LZMA SDK, CMake uses `WINUAE_LZMA_SDK_DIR`, defaulting to a build-tree `_deps/lzma-sdk/16.04` cache. If that cache is missing, CMake downloads `WINUAE_LZMA_SDK_URL` and verifies it against `WINUAE_LZMA_SDK_SHA256` before extraction.
`WINUAE_UNIX_WITH_JIT` is enabled by default where the Unix host backend is wired, including ARM64 and x86_64.
`WINUAE_UNIX_WITH_PPC_QEMU` is enabled by default and builds the WinUAE side of the PPC accelerator/QEMU plugin ABI. `WINUAE_UNIX_BUILD_QEMU_UAE_PLUGIN` is enabled by default when a sibling `qemu-uae-v11.0` tree is present and builds/copies `qemu-uae.so` for the executable, Linux package, or app bundle. `WINUAE_QEMU_UAE_PLUGIN_FILE` can point at a prebuilt plugin. App and CPack package targets require the plugin when PPC support is enabled.
`WINUAE_QEMU_UAE_DEPS_PREFIX` defaults to the private macOS dependency prefix and points the plugin helper at the deployment-target-compatible GLib build.
`WINUAE_UNIX_WITH_OPENGL_SHADER_PIPELINE` is enabled by default when SDL3 and OpenGL development files are available. Runtime OpenGL context or shader setup failure falls back to the SDL renderer.
`WINUAE_UNIX_WITH_SNDBOARD` is enabled by default and builds the shared Toccata/Prelude/UAESND sound-board backend. It also enables the shared PCI bridge layer needed to expose ES1370 and FM801 through the same expansion-board catalog as Windows, even when hardware RTG graphics boards are disabled.
`WINUAE_UNIX_WITH_PROWIZARD` is enabled by default and builds the same Pro Wizard source set used by the Windows project.
`WINUAE_UNIX_WITH_QT_UI` is enabled by default, but Qt UI targets are skipped when Qt Widgets is not installed.
`WINUAE_UNIX_WITH_INTEGRATED_QT_UI` is enabled by default. When Qt Widgets is not installed, the build continues without the integrated UI.
