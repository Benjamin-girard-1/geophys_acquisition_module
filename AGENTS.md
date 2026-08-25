# Codex repository guide

This file gives Codex the minimum context needed to work safely and consistently
in this repository. Keep it concise; detailed requirements belong in the linked
project documents.

## Read before changing anything

For every task, first read:

1. `README.md` for the current repository overview and implementation status.
2. `ARCHITECTURE.md` for module responsibilities, dependency direction, and
   where new files belong.
3. `version_2/docs/product_requirements.md` for the frozen milestone scope and
   product behavior.

Then read the documents relevant to the task:

- Firmware or Rev-1 hardware integration:
  `version_2/docs/board_rev_1_contract.md` and
  `version_2/docs/firmware_interfaces.md`.
- Firmware implementation or progress review:
  `version_2/docs/firmware_implementation_checklist.md`.
- Protocol or host-interface work: `shared/protocol/protocol_frame.md` and
  `shared/protocol/protocol_types.md`.
- Component-driver work: the relevant local datasheet and schematic sheet.
  Vendor PDF datasheets are intentionally untracked under
  `version_2/docs/datasheets/`.

Do not rely on this recap instead of reading the applicable source documents.

## Repository recap

This project is a modular ESP32-S3 geophysical acquisition system. The ESP32-S3
DevKit is an immutable platform component. The custom carrier PCB, its wiring,
and its revision-specific behavior belong under `version_2/firmware/boards/`
and `version_2/hardware/pcb/`.

The Version 2 firmware is currently scaffolding and is not yet buildable or
hardware-verified. Preserve the existing layered structure:

- `main/`: composition and startup only.
- `app/`: product behavior, tasks, queues, and resource ownership.
- `boards/rev_1/`: Rev-1 GPIO/peripheral mapping, polarities, safe states,
  power sequencing, and board-level operations.
- `analog_cards/`: removable-card behavior without ESP32 GPIO numbers.
- `drivers/`: portable IC register behavior without ESP-IDF or FreeRTOS.
- `platform/esp32s3_devkit/`: generic ESP-IDF mechanisms and immutable DevKit
  integration.
- `protocol/`: transport-independent firmware framing and serialization.
- `transports/`: byte movement only; transports do not interpret commands.
- `shared/protocol/`: wire contract shared by firmware and host software.
- `version_2/host_app/`: host-only acquisition and validation software.

## Current product boundary

Milestone 1 is laboratory validation of both magnetic-card slots using all
eight synchronized AD7779 channels by default at 1 kSPS. It includes configurable
sample rate and gain, UART-to-USB PC streaming, host visualization/capture,
on-demand SET/RESET, explicit faults, and safe startup/shutdown behavior.

Milestone 1 does not initialize or create runtime resources for SD recording,
GNSS, IMU, Bluetooth, processing, or USB mass storage. Those are milestone-2 or
later work unless the user explicitly changes the scope. The SD mux stays fixed
to the ESP32 and the USB2641 stays reset/isolated; do not create runtime SD/USB
ownership switching.

## Non-negotiable firmware rules

- Maintain one writer/owner for every stateful hardware resource.
- `task_acquisition` owns AD7779 configuration/streaming and magnetic pulse
  timing. `task_communication` owns UART transport and protocol handling. Only
  the board module writes the 74HC595 image or direct Rev-1 control GPIOs.
- Application code uses board-level operations. It does not manipulate IC
  registers, ESP32 GPIO registers, or shift-register bits directly.
- Keep the ADC data-ready ISR minimal: timestamp, enqueue/notify, and count
  overflow. Never perform SPI, allocation, blocking, or logging in the ISR.
- Acquisition must never block on UART or future storage operations.
- Treat one AD7779 conversion as one indivisible simultaneous eight-channel
  frame with one timestamp and sequence number.
- Store sign-extended ADC samples as signed 32-bit values in memory, but pack
  each selected ADC sample into exactly three bytes on the wire.
- Never hide data loss. Preserve sequence gaps and expose overflow/error
  counters.
- SET and RESET are on-demand operations only and must never be active
  simultaneously. Mark pulse and settling samples transient/invalid.
- Never insert debug text into the binary data stream.
- Do not invent GPIO assignments, active levels, voltage thresholds, delays,
  pulse widths, or bus limits. Resolve them from the board contract, schematic,
  datasheet, or bench evidence; report unresolved values instead of guessing.

## Document authority and conflicts

- `product_requirements.md` defines product scope and required behavior.
- `board_rev_1_contract.md` defines the firmware-visible Rev-1 hardware
  contract.
- `firmware_interfaces.md` defines ownership, interfaces, data, queues, and
  protocol boundaries.
- `ARCHITECTURE.md` defines placement and allowed dependencies.
- The Rev-1 schematic is the electrical source of truth.

If these sources disagree in a way that affects behavior or safety, stop and
report the conflict. Do not silently choose one. A new user instruction may
change a requirement, but update the affected contract document with the code
so that the repository remains self-consistent.

## Working agreement

- Inspect existing files and `git status` before editing. Preserve unrelated
  user changes.
- Make the smallest coherent change that fits the architecture; do not fill
  scaffolds merely because they exist.
- Update documentation and implementation-status records when an interface,
  requirement, or verified hardware fact changes.
- Verify changes in proportion to their risk. Until the ESP-IDF project is
  buildable, clearly distinguish static checks from compiled or hardware-tested
  results.
- Do not modify ignored vendor datasheets or generated build outputs.
- Do not commit, push, pull, or rewrite Git history. The repository owner does
  those operations manually unless they explicitly request otherwise.
