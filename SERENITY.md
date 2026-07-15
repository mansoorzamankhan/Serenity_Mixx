# Serenity_Mixx

A fork of [Mixxx](https://mixxx.org) DJ software, customised as the audio mixing core of the **Serenity DJ System** — a portable, hardware-integrated DJ platform running on Raspberry Pi 5.

---

## What Is This

Serenity_Mixx is the DJ engine component of the Serenity system. It runs as a standalone binary launched by the **Serenity_App** launcher. The launcher handles the system UI (Bluetooth management, EQ controls, app navigation); Serenity_Mixx handles all audio mixing.

The fork tracks the upstream Mixxx `main` branch. Serenity-specific additions are layered on top:

- **Dockerfile.rpi** — ARM64 Docker build image for cross-compiling to Raspberry Pi
- **build_rpi.sh** — Docker-based cross-compile + deploy script
- **SERENITY.md** — this file

---

## System Context

```
Serenity_App (Qt6 QML launcher)
        │
        └── QProcess → Serenity_Mixx binary
```

The Serenity_App launcher starts and stops this binary via `QProcess`. The two repos are built and deployed independently.

Full architecture: see `DOCS/Architecture.md` in the Serenity project root.

---

## Repositories

| Repo | Purpose |
|---|---|
| [Serenity_App](https://github.com/mansoorzamankhan/Serenity_App) | Qt6 QML launcher + Bluetooth management |
| [Serenity_Mixx](https://github.com/mansoorzamankhan/Serenity_Mixx) | Mixxx fork — DJ audio engine (this repo) |

---

## Build Instructions

### Native Build (PC or Raspberry Pi)

Both x86-64 (Ubuntu) and ARM64 (Raspberry Pi) use the same native build process.

**1. Install dependencies** (one-time)

On Raspberry Pi or any Ubuntu 24.04 ARM64 machine:

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake ninja-build git pkg-config \
    qt6-base-dev qt6-base-private-dev \
    qt6-declarative-dev qt6-declarative-private-dev \
    qt6-multimedia-dev qt6-tools-dev \
    libqt6core5compat6-dev libqt6opengl6-dev libqt6svg6-dev \
    libjack-dev portaudio19-dev libasound2-dev \
    libsndfile1-dev libsoundtouch-dev \
    libid3tag0-dev libmad0-dev libogg-dev libvorbis-dev \
    libavformat-dev libopusfile-dev libtag1-dev \
    libmodplug-dev libmp3lame-dev libwavpack-dev \
    librubberband-dev libchromaprint-dev \
    libebur128-dev libfftw3-dev \
    libusb-1.0-0-dev libhidapi-dev libudev-dev \
    liblilv-dev libssl-dev libprotobuf-dev protobuf-compiler \
    libsqlite3-dev libgtest-dev \
    libgpiod-dev
```

`libgpiod-dev` must be **v2.0 or later** (the GPIO jog wheel bridge uses the libgpiod v2 character-device API). If the distro-packaged `libgpiod-dev` is older than 2.0, build libgpiod v2 from source: https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git

**2. Clone and build**

```bash
git clone https://github.com/mansoorzamankhan/Serenity_Mixx.git
cd Serenity_Mixx
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja -DBUILD_TESTING=OFF
cmake --build build --parallel $(nproc)
```

Binary output: `build/mixxx`

**3. Run**

```bash
./build/mixxx
```

### Cross-Compile for Raspberry Pi (Docker, from PC)

Use this when you want to build an ARM64 binary on your x86-64 PC without native hardware available.

```bash
./build_rpi.sh
```

This script builds the `Dockerfile.rpi` image (ARM64 Ubuntu 24.04 with all dependencies) and produces the Mixxx ARM64 binary inside the container.

---

## Development Workflow

```
PC (Ubuntu, x86-64)              Raspberry Pi 5 (Ubuntu 24.04, ARM64)
────────────────────             ─────────────────────────────────────
git clone <repo>                 git clone <repo>          (one-time)
# make changes                   # (native build only)
git commit && git push  ──────►  git pull
                                 cmake -B build -G Ninja -DBUILD_TESTING=OFF
                                 cmake --build build --parallel $(nproc)
                                 ./build/mixxx
```

---

## Hardware Integration

Serenity_Mixx receives MIDI control input from two optical rotary encoders wired directly to the Raspberry Pi GPIO — acting as jog wheels for Deck A and Deck B.

| Component | Detail |
|---|---|
| Encoder type | Optical quadrature, 128 PPR |
| Interface | Direct RPi 5 GPIO (no ADC required), via `/dev/gpiochip0` |
| Signal processing | libgpiod v2 → `src/hardware/serenitygpioencoder.cpp` → `src/hardware/serenitygpiojogwheelservice.cpp` → `src/hardware/serenitymidibridge.cpp` |
| MIDI transport | ALSA virtual MIDI port ("Serenity Jog Wheels"), opened in-process at startup |
| Mixxx mapping | `res/controllers/Serenity Jog Wheels.midi.xml` |
| Build option | `SERENITY_GPIO_JOGWHEELS` (CMake, defaults ON on Linux when libgpiod >= 2.0 and ALSA dev headers are found) |

| Encoder | GPIO Pins (line offsets) | MIDI CC | Mixxx Function |
|---|---|---|---|
| Deck A | GPIO 17, 27 | CC 1 Ch. 1 | Jog wheel — Deck A |
| Deck B | GPIO 22, 23 | CC 2 Ch. 1 | Jog wheel — Deck B |

`SerenityGpioJogWheelService` is started in `main.cpp` before `ControllerManager::setUpDevices()`, so the virtual MIDI port already exists by the time Mixxx enumerates MIDI devices. In Mixxx's Controller preferences, select "Serenity Jog Wheels" and load the `Serenity Jog Wheels` mapping to activate the encoders as Deck A/B jog wheels. Each encoder detent is sent as a single MIDI CC message using the `<diff/>` (7-bit two's complement) convention: value `1` = +1 tick, value `127` = -1 tick.

On non-Linux platforms, or Linux builds without libgpiod >= 2.0 / ALSA dev headers, `SERENITY_GPIO_JOGWHEELS` is off and this code is not compiled in.

---

## Deployment Path on Raspberry Pi

```
/home/ubuntu/
└── Serenity_Mixx/
    └── build/mixxx        ← binary launched by Serenity_App
```

The Serenity_App launcher hardcodes this path. See `Serenity_App/src/AppLauncher.cpp`.

---

## Notes

- `hidapi/` is intentionally excluded from git (listed in `.gitignore`). Mixxx's bundled hidapi source is not needed — the system package `libhidapi-hidraw0` satisfies `find_package(hidapi)` at build time.
- The Deere skin (`res/skins/`) is the default UI skin used by Serenity_Mixx. Its colour palette (`#006596` primary blue, `#000000` background) is matched by the Serenity_App launcher UI for visual consistency.
