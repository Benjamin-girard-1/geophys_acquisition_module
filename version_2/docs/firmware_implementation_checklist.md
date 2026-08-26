# Version 2 Milestone-1 Firmware Implementation Checklist

## Document information

- Product version: V2
- Hardware target: V2-Rev-1 with ESP32-S3 DevKitC
- Scope: laboratory acquisition and validation
- Status: Active planning checklist
- Last updated: 2026-08-25

## How to use this checklist

This document tracks implementation progress; it does not replace the product requirements, board
contract, firmware interface contract, or architecture. Each item has only three tracking fields:

- **Complete:** the described code, document, measurement, or decision is finished and matches the
  stated acceptance condition.
- **Tested:** the strongest verification required for that item has passed. Hardware-dependent work
  requires a Rev-1 bench test; portable code may use builds or host tests; hardware facts may use
  schematic, datasheet, and measurement checks. Do not check this box for an intermediate test when
  stronger verification is still required.
- **Evidence:** record the test method and result, measurement, log/capture path, or other concise
  proof. Include the date when the result depends on a particular hardware assembly.

An item is finished for the milestone only when both Complete and Tested are checked and its
evidence is recorded.

## 0. Hardware and contract gates

| ID | Deliverable and acceptance condition | Complete |
|---|---|:---:|
| HW-01 | Resolve every SH1/SH2 active polarity and define the complete safe 16-bit image | [x] |
| HW-02 | Verify fixed ESP32 SD-mux levels and USB2641 reset/isolation state | [x] |
| HW-03 | Resolve analog-rail enable order, active levels, and conservative settling times | [x] |
| HW-04 | Resolve AD7779 reset, master-clock, START, and initial SPI timing | [x] |
| HW-05 | Define no-card and magnetic-card detection voltage windows and sampling policy | [x] |
| HW-06 | Define initial SET/RESET control width, recharge/dead time, and settling limits | [x] |
| HW-07 | Verify GPIO45/GPIO46 strapping does not prevent reliable boot or download | [x] |

GPIO46 and GPIO45 are not a concern.

## 1. ESP-IDF project scaffold

| ID | Deliverable and acceptance condition | Complete |
|---|---|:---:|
| FW-01 | Add the ESP-IDF project and component `CMakeLists.txt` files without breaking architecture boundaries | [x] |
| FW-02 | Add reproducible ESP32-S3 defaults in `sdkconfig.defaults`; keep generated `sdkconfig` local | [x] |
| FW-03 | Build the empty application with warnings enabled and no unexplained warnings | [x] |
| FW-04 | Flash Rev-1 and reach `app_main()` repeatedly without unsafe output activity | [ ] |
| FW-05 | Keep binary host UART output separate from diagnostic logging | [ ] |

## 2. Common types and ESP32-S3 platform services

| ID | Deliverable and acceptance condition | Complete | Tested | Evidence |
|---|---|:---:|:---:|---|
| FW-06 | Define the common status/error categories without exposing `esp_err_t` outside the platform layer | [ ] | [ ] | |
| FW-07 | Implement monotonic microsecond time and the explicitly ISR-safe timestamp operation | [ ] | [ ] | |
| FW-08 | Implement generic GPIO input/output and interrupt services | [ ] | [ ] | |
| FW-09 | Implement blocking portable SPI callbacks with timeout and atomic bus access | [ ] | [ ] | |
| FW-10 | Implement UART byte transport with partial-read/write and timeout handling | [ ] | [ ] | |
| FW-11 | Translate platform failures into the common error model with operation context | [ ] | [ ] | |

## 3. Shift register and Rev-1 safe board initialization

| ID | Deliverable and acceptance condition | Complete | Tested | Evidence |
|---|---|:---:|:---:|---|
| FW-12 | Implement the portable 74HC595 driver with a complete 16-bit shadow image | [ ] | [ ] | |
| FW-13 | Test bit order, latch behavior, unrelated-bit preservation, and invalid arguments with fake GPIO callbacks | [ ] | [ ] | |
| FW-14 | Encode all Rev-1 pins, active levels, bus limits, and safe values only in `boards/rev_1` | [ ] | [ ] | |
| FW-15 | Implement `board_init()`: safe direct GPIOs, outputs disabled, safe image latched, then outputs enabled | [ ] | [ ] | |
| FW-16 | Implement idempotent `board_enter_safe_state()` for startup and fatal failures | [ ] | [ ] | |
| FW-17 | Keep the SD mux fixed to the ESP32 and the USB2641 reset/isolated for the entire runtime | [ ] | [ ] | |
| FW-18 | Verify every power enable and pulse output during cold boot, reset, firmware download, and safe shutdown | [ ] | [ ] | |

## 4. AD7779 portable driver

| ID | Deliverable and acceptance condition | Complete | Tested | Evidence |
|---|---|:---:|:---:|---|
| FW-19 | Define private AD7779 registers, fields, commands, and supported limits from the datasheet | [ ] | [ ] | |
| FW-20 | Implement lifecycle, reset, identity/status verification, and idempotent stop | [ ] | [ ] | |
| FW-21 | Implement all-eight-channel default configuration and per-channel gains ×1, ×2, ×4, and ×8 | [ ] | [ ] | |
| FW-22 | Implement configurable output rate through 16 kSPS and report requested versus actual rate | [ ] | [ ] | |
| FW-23 | Decode one simultaneous frame into eight sign-extended `int32_t` samples | [ ] | [ ] | |
| FW-24 | Validate enabled AD7779 header/CRC/status information and normalize driver faults | [ ] | [ ] | |
| FW-25 | Run driver tests with fake SPI for transactions, timeouts, corrupt frames, and configuration readback | [ ] | [ ] | |
| FW-26 | Verify stable AD7779 communication at a conservative SPI clock before raising the clock | [ ] | [ ] | |

## 5. Card detection and magnetic-card integration

| ID | Deliverable and acceptance condition | Complete | Tested | Evidence |
|---|---|:---:|:---:|---|
| FW-27 | Implement averaged analog-ID measurement without exposing ESP32 ADC details to card modules | [ ] | [ ] | |
| FW-28 | Classify no-card, magnetic-card, and unknown/ambiguous voltage safely in either slot | [ ] | [ ] | |
| FW-29 | Expose stable slot/type/confidence and channel mappings through the board status API | [ ] | [ ] | |
| FW-30 | Keep pulse controls disabled for absent, unknown, or removed cards | [ ] | [ ] | |
| FW-31 | Verify both magnetic cards independently and simultaneously in both physical slots | [ ] | [ ] | |

## 6. Acquisition task and bounded data pipeline

| ID | Deliverable and acceptance condition | Complete | Tested | Evidence |
|---|---|:---:|:---:|---|
| FW-32 | Define ADC frame, block, status, flags, pulse request/result, and counter types from the interface contract | [ ] | [ ] | |
| FW-33 | Implement the 64-entry DRDY timestamp ring and minimal falling-edge ISR | [ ] | [ ] | |
| FW-34 | Implement eight fixed 32-frame blocks with free and ready queues; allocate everything before streaming | [ ] | [ ] | |
| FW-35 | Make `task_acquisition` the sole owner of ADC configuration, streaming, and pulse timing | [ ] | [ ] | |
| FW-36 | Assign one timestamp and monotonically increasing sequence to each simultaneous conversion frame | [ ] | [ ] | |
| FW-37 | Preserve invalid frames, dropped counts, sequence gaps, ISR overflow, and pool exhaustion visibly | [ ] | [ ] | |
| FW-38 | Accept gain, rate, and channel-mask changes only while acquisition is stopped and apply them atomically | [ ] | [ ] | |
| FW-39 | Demonstrate that acquisition continues servicing DRDY while UART output is blocked or disconnected | [ ] | [ ] | |

## 7. V2 protocol

| ID | Deliverable and acceptance condition | Complete | Tested | Evidence |
|---|---|:---:|:---:|---|
| FW-40 | Freeze frame fields, message identifiers, stable error codes, and payload layouts in `shared/protocol` | [ ] | [ ] | |
| FW-41 | Implement little-endian framing, 2048-byte payload limit, explicit serialization, and CRC-32C | [ ] | [ ] | |
| FW-42 | Implement incremental parsing and recovery from partial, concatenated, corrupt, and unknown frames | [ ] | [ ] | |
| FW-43 | Pack each selected ADC code into exactly three little-endian two's-complement bytes | [ ] | [ ] | |
| FW-44 | Implement the required Milestone-1 handshake, configuration, acquisition, status, pulse, and error messages | [ ] | [ ] | |
| FW-45 | Add shared valid/invalid golden vectors consumed independently by firmware and host tests | [ ] | [ ] | |
| FW-46 | Verify every accepted request receives one response and malformed requests cannot partially change state | [ ] | [ ] | |

## 8. UART streaming and host application

| ID | Deliverable and acceptance condition | Complete | Tested | Evidence |
|---|---|:---:|:---:|---|
| FW-47 | Configure UART0 on GPIO43/GPIO44 at the reported target baud without power-management clock changes | [ ] | [ ] | |
| FW-48 | Implement `task_communication` as the sole protocol/UART owner and return every consumed ADC block | [ ] | [ ] | |
| FW-49 | Negotiate/reject stream configurations that exceed measured link capacity; never silently thin data | [ ] | [ ] | |
| FW-50 | Adapt the proven V1 host workflow to the V2 protocol without importing an absolute local-path dependency | [ ] | [ ] | |
| FW-51 | Display both cards' axes and thermistors, validity, counters, and visible sequence gaps | [ ] | [ ] | |
| FW-52 | Support host gain/rate/channel controls, start/stop, status, and PC-side capture | [ ] | [ ] | |
| FW-53 | Reconnect and restart safely after host disconnect without requiring a reboot | [ ] | [ ] | |

## 9. On-demand magnetic SET/RESET

| ID | Deliverable and acceptance condition | Complete | Tested | Evidence |
|---|---|:---:|:---:|---|
| FW-54 | Implement explicit per-slot SET, RESET, and diagnostic SET-then-RESET requests; never run them periodically | [ ] | [ ] | |
| FW-55 | Enforce one outstanding pulse operation, SET/RESET mutual exclusion, and minimum dead time | [ ] | [ ] | |
| FW-56 | Enable the 18 V pulse path only for a known magnetic card and return every control to its safe state | [ ] | [ ] | |
| FW-57 | Record requested/actual timing, result, settling end, and first/last affected sequence | [ ] | [ ] | |
| FW-58 | Mark every pulse-active and settling frame transient/invalid without losing its timestamp | [ ] | [ ] | |
| FW-59 | Measure approximately 2–3 µs sensor-strap current pulse and establish safe recharge/settling values | [ ] | [ ] | |

## 10. Fault handling and Milestone-1 acceptance

| ID | Deliverable and acceptance condition | Complete | Tested | Evidence |
|---|---|:---:|:---:|---|
| FW-60 | Report stable error code, source, severity, timestamp, detail, and occurrence count | [ ] | [ ] | |
| FW-61 | Keep communication diagnosable when ADC startup fails, while all unsafe outputs remain inactive | [ ] | [ ] | |
| FW-62 | Inject ADC CRC, timeout, queue-overflow, missing-card, unsupported-command, and disconnect faults | [ ] | [ ] | |
| FW-63 | Acquire both magnetic cards as eight synchronized channels at the default 1 kSPS | [ ] | [ ] | |
| FW-64 | Verify configurable ADC rates through 16 kSPS while explicitly reporting any streaming limitation | [ ] | [ ] | |
| FW-65 | Stream all eight packed channels at 1 kSPS for eight hours with no unexplained gap or silent loss | [ ] | [ ] | |
| FW-66 | Complete all Milestone-1 product and interface acceptance criteria and attach evidence | [ ] | [ ] | |
| FW-67 | Update `README.md`, `ARCHITECTURE.md` implementation status, and affected contracts to match verified behavior | [ ] | [ ] | |

## Deferred work

Do not expand Milestone 1 to complete these items. Revisit them after FW-66 unless the frozen product
requirements are deliberately revised:

- SD-card recording and recovery.
- MAX-M10S GNSS parsing and UART-only inter-device time alignment.
- LSM6DSV orientation and movement acquisition.
- Geophysical accelerometer card support.
- Bluetooth transport.
- Any USB2641 mass-storage or runtime SD-ownership switching; these remain unsupported on V2 Rev-1.
