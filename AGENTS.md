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
- `lib_deps` currently only has U8g2 (`olikraus/U8g2@^2.36.0`) for the OLED. BLE ships inside the espressif32 platform (ESP32 BLE Arduino), so no extra dep is needed for it.
- Code is organized per the architecture in `plan.md` (headers in `include/`, sources under `src/ble`, `src/drivers`, `src/behaviors`; free functions, no classes):
  - `src/main.cpp` is only the glue layer: `setup()` init + `loop()` heartbeat / BOOT-key debounce / `oledTick()` refresh.
  - `src/ble/BleService.*` — Nordic UART passthrough (device name `Glasses`, optional static pairing PIN via `PAIRING_PIN` in `include/BleService.h`). Phone data is delivered through a registered callback (`bleOnDataReceived`).
  - `src/drivers/DisplayDriver.*` — U8g2 SSD1306 scrolling-text terminal; text enters via `oledShowText()`, actual drawing happens in `oledTick()` called from `loop()` (BLE callbacks must not draw directly).
  - `src/drivers/ImuDriver.*` and `src/behaviors/*` are empty skeletons reserved for phase 2/3.

## Conventions

- Comments and docs are written in Chinese — follow that style in existing files.
