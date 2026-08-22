# AGENTS.md

PlatformIO Arduino firmware for a DIY AR star-glasses device (观星AR眼镜): an ESP32-S3 head unit that talks BLE (Nordic UART passthrough) to a phone app; later phases add BNO085 IMU, I2C OLED labels, and prism optics. `plan.md` (Chinese) is the roadmap and architecture source of truth — read it first.

## Commands

- Build: `pio run`
- Upload: `pio run -t upload`
- Serial monitor: `pio device monitor` (115200 baud, set in `platformio.ini`)
- Do NOT run `pio test`: `test/` contains no Unity tests. Verification = a clean `pio run`.

## Hardware / build config

- Single env `esp32-s3-devkitc-1`; platform is pinned (`espressif32@6.13.0`), framework `arduino`.
- Actual module is ESP32-S3-WROOM-1-N16R8 (16MB flash, 8MB OPI PSRAM). The generic devkitc-1 board definition defaults to N8 / no PSRAM, so `platformio.ini` MUST keep:
  - `board_build.arduino.memory_type = qio_opi`
  - `build_flags = -DBOARD_HAS_PSRAM`
  - `board_upload.flash_size = 16MB` + `board_build.partitions = default_16MB.csv`

## Gotchas

- `test/oled.cpp` is a reference snippet copied from another project; its includes (`pin_config.h`, `oled.h`, `pet_state.h`) don't exist here. Its header comment says to ignore it — do not try to fix, build, or delete it.
- Hard-earned I2C lesson (recorded in `test/oled.cpp` comments): if U8g2's constructor receives SCL/SDA pins, calling `Wire.begin()` separately triggers a duplicate bus init that hard-locks the ESP32-S3 I2C bus → blank OLED, no serial output. Init the bus exactly once.
- No `lib_deps` declared yet: BLE ships inside the espressif32 platform (ESP32 BLE Arduino). When OLED work starts, U8g2 must be added to `platformio.ini`.
- `src/main.cpp` is currently a standalone BLE test sketch (device name `TH-Insecure`, optional static pairing PIN); expect it to be rewritten as phases progress rather than extended indefinitely.

## Conventions

- Comments and docs are written in Chinese — follow that style in existing files.
