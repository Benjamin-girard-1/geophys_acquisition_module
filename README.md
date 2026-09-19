# Geophysical Acquisition Module

Hardware, embedded firmware, protocol, and host-side software for a modular
geophysical data-acquisition system.

Version 2 uses an ESP32-S3 DevKit as its immutable compute platform and a custom
carrier PCB organized by hardware revision. The current hardware design includes
an AD7779 ADC, an LSM6DSV IMU, MAX-M10S GNSS, SD storage shared with a USB2641,
74HC/HCT595 GPIO expansion, and removable analog acquisition cards.

## Current status

The Version 2 firmware builds with ESP-IDF 5.5. Board safe-state and power
control, portable GPIO/SPI/UART, calibrated analog-input, and ROM-backed CRC
mechanisms, the 74HC/HCT595 driver, bounded analog-card ID measurement, and the
AD7779 register, lifecycle, channel/gain, fixed output-rate, signed sample
decoding, and conversion-frame validation foundations are implemented.
The acquisition task, SD recording owner, and recording controller now provide
the first end-to-end recording implementation and UART live-stream path, while
the remaining protocol inventory is still in progress. Hardware verification
is being completed incrementally.
The authoritative protocol defines fixed 64-byte command messages and portable
512-byte ADC data blocks shared by live capture and SD recording. Fixed command
framing, incremental stream recovery, CRC validation, and the
`HELLO`/`DEVICE_INFO` plus `DEVICE_GET_CONFIG`/`DEVICE_SET_CONFIG` UART slices
are implemented and verified on Rev-1 hardware. Live start/stop, complete
512-byte UART data delivery, the five SD-recording commands,
the portable `\DAT` encoder/decoder, fixed recording-buffer pool, and FatFs
writer are implemented; a clean Rev-1 build mounts and catalogs the SD card,
and now creates, syncs, closes, catalogs, and deletes ADC data files on Rev-1
hardware. A temporary test-only `0xf000` command can read closed recording
files in 38-byte UART chunks; it is implemented in the firmware and host codec
and was used to retrieve and CRC-validate a stored 512-byte block. A two-second
protocol probe produced and cleanly closed 404 fixed
512-byte records while configuration commands remained responsive. The probe
also exposed and led to correction of acquisition-task starvation and a
FreeRTOS queue-set accounting error that rebooted the device after several SD
records. Comparison with the working V1 driver identified and corrected
premature post-reset polling failure, missing reference-output power, missing
SPI software reset, and missing filter synchronization. The earlier all-zero
ADC response was a bench power issue: the main +5 V supply was absent, so 3V3A
was unavailable. Restoring +5 V allowed AD7779 initialization and recording to
proceed. The two expendable recordings that filled the card were enumerated
and deleted through the recording protocol, leaving an empty catalog. A
subsequent fresh probe recorded, downloaded, validated, and deleted ten
512-byte records. All record framing and CRCs passed. The capture is not yet
scientifically valid: all records carried critical status, several channels
reached the signed 24-bit rails, and a visible sequence gap represented 32
missing conversions. Its timestamps show roughly 16 kSPS during the first 80
conversions and 1 kSPS during the final 120 conversions, so the configured
rate now settles correctly but the startup transient and loss must still be
resolved. Configuration currently applies the stopped-device ADC rate, channel
mask, and per-channel gains; runtime rail and IMU changes remain unsupported
until their owning subsystems exist. The Python host now has streaming
start/stop codecs, an incremental mixed command/data parser, named-reply
matching during live data, byte-exact validated capture, continuity counters,
and a desktop GUI with manual USB/COM selection, recording management, and a
separate live-plot tab. The GUI connection and recording catalog were exercised
against Rev-1, including creation, stop/close, catalog, and deletion of a GUI
test recording. Firmware live delivery was then exercised at 921600 baud with
eight channels at 1 kSPS and with four-channel decimation-by-5: start/stop
replies and all received block CRCs passed, streaming continued while an SD
recording was opened, and reconnect succeeded after the five-second session
timeout. The live blocks still expose the known ADC critical status and startup
sequence loss, so scientific-data validation remains open.

The complete wire contract is
[shared/protocol/protocol.md](shared/protocol/protocol.md). It is the sole
authority for framing, identifiers, results, command payloads, and ADC data
blocks.

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
    │   ├── common/              Portable types shared across firmware layers
    │   ├── main/                ESP-IDF entry point and composition root
    │   ├── app/                 Product behavior and FreeRTOS tasks
    │   ├── boards/              Custom carrier-board integration by revision
    │   ├── analog_cards/        Removable analog-card integration
    │   ├── drivers/             Portable component-specific drivers
    │   ├── data_format/         Portable serialized acquisition records
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

- `common/` defines dependency-free status categories and other truly
  cross-layer portable types.
- `main/` initializes the platform, selected board, and application.
- `app/` owns acquisition, processing, storage, communication, and product state.
- `boards/` maps the custom PCB wiring to portable drivers and platform services, including the
  Rev-1 mainboard's 18 V rail and physical shift-register outputs.
- `analog_cards/` describes complete removable card assemblies. Magnetic SET/RESET pulse generation
  belongs in `analog_cards/magnetic/` and is not shared with `analog_cards/acc_geoph/`.
- `drivers/` implements individual components without depending on ESP-IDF,
  FreeRTOS, or board wiring.
- `data_format/` defines transport- and storage-independent acquisition-record
  serialization.
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
- Portable 512-byte ADC-record format

## Building

Activate an ESP-IDF 5.5 environment, then build from `version_2/firmware/`:

```sh
cd version_2/firmware
idf.py build
idf.py flash monitor
```

Run `idf.py set-target esp32s3` first only when creating a fresh local build
configuration or changing targets.

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
