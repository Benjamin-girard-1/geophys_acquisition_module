# Geophysical Acquisition Module

Hardware, embedded firmware, protocol, and host-side software for a modular
geophysical data-acquisition system.

Version 2 uses an ESP32-S3 DevKit as its immutable compute platform and a custom
carrier PCB organized by hardware revision. The current hardware design includes
an AD7779 ADC, an LSM6DSV IMU, MAX-M10S GNSS, SD storage shared with a USB2641,
74HC/HCT595 GPIO expansion, and removable analog acquisition cards.

## Current status

The Version 2 PCB and firmware directory structure are present. Firmware source
files, public interfaces, protocol definitions, build configuration, and tests
are still implementation scaffolds. The firmware is not yet buildable or
hardware-tested.

See [ARCHITECTURE.md](ARCHITECTURE.md) for module responsibilities, dependency
rules, initialization order, and the detailed implementation-status table.
Milestone-1 execution and verification are tracked in
[version_2/docs/firmware_implementation_checklist.md](version_2/docs/firmware_implementation_checklist.md).

## Repository layout

```text
.
├── ARCHITECTURE.md              Firmware architecture and dependency rules
├── shared/
│   ├── kicad-libraries/         Shared KiCad symbols and footprints
│   ├── protocol/                Firmware/host wire-protocol specification
│   ├── third_party/             Pinned external dependencies
│   └── tools/                   Repository-wide development utilities
├── version_1/                   Earlier product generation
└── version_2/
    ├── docs/                    Datasheet links and supporting documentation
    ├── firmware/
    │   ├── main/                ESP-IDF entry point and composition root
    │   ├── app/                 Product behavior and FreeRTOS tasks
    │   ├── boards/              Custom carrier-board integration by revision
    │   ├── analog_cards/        Removable analog-card integration
    │   ├── drivers/             Portable component-specific drivers
    │   ├── protocol/            Firmware framing and message encoding
    │   ├── transports/          UART, USB, and future byte transports
    │   ├── platform/
    │   │   └── esp32s3_devkit/  Immutable DevKit and ESP-IDF integration
    │   └── cmake/               Shared build helpers when required
    ├── hardware/
    │   ├── pcb/                 KiCad projects organized by revision
    │   └── mechanical/          Mechanical design files
    └── host_app/                Host acquisition and calibration software
```

## Firmware boundaries

- `main/` initializes the platform, selected board, and application.
- `app/` owns acquisition, processing, storage, communication, and product state.
- `boards/` maps the custom PCB wiring to portable drivers and platform services, including the
  Rev-1 mainboard's 18 V rail and physical shift-register outputs.
- `analog_cards/` describes complete removable card assemblies. Magnetic SET/RESET pulse generation
  belongs in `analog_cards/magnetic/` and is not shared with `analog_cards/acc_geoph/`.
- `drivers/` implements individual components without depending on ESP-IDF,
  FreeRTOS, or board wiring.
- `platform/esp32s3_devkit/` isolates ESP-IDF and immutable DevKit details.
- `protocol/` implements transport-independent framing and messages.
- `transports/` move bytes without interpreting application commands.
- `shared/protocol/` defines the wire contract used by firmware and host tools.

## Version 2 firmware components

The current scaffolds cover:

- AD7779 eight-channel ADC
- LSM6DSV accelerometer and gyroscope
- MAX-M10S GNSS receiver
- 74HC/HCT595 shift register
- Magnetic analog acquisition card
- Geophysical accelerometer card
- Analog-card detection
- Acquisition, processing, storage, communication, and Bluetooth tasks
- UART and USB transports
- Transport-independent protocol framing and messages

## Building

The ESP-IDF project files have not been created yet, so there is currently no
valid firmware build command. The intended build root is `version_2/firmware/`.
Once its top-level and component `CMakeLists.txt` files and `sdkconfig.defaults`
exist, the normal workflow will be:

```sh
cd version_2/firmware
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Generated `build/`, `sdkconfig`, managed components, binaries, KiCad local state,
editor files, and locally stored vendor datasheet PDFs are excluded by the root
`.gitignore`. Datasheet source links remain tracked in
`version_2/docs/datasheet.txt`.

## Development guidance

Before adding a module or function, consult the “Where new code belongs” table
in [ARCHITECTURE.md](ARCHITECTURE.md#where-new-code-belongs). Keep component
drivers portable, keep physical Rev-1 wiring in `boards/rev_1`, and keep protocol
encoding independent of UART, USB, or Bluetooth.

Update the architecture implementation-status table when an interface becomes
usable, an implementation passes its software tests, or functionality is
verified on hardware.
