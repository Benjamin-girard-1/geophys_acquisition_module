# Rev-1 Board Firmware Contract

## Document information

- Status: Draft
- PCB revision: Rev-1
- Schematic project:
  `version_2/hardware/pcb/rev_1/mainboard_v2_r1/`
- Compute platform: ESP32-S3 DevKit [EXACT MODEL TBD]
- Schematic revision/date: [TBD]
- Last updated: [YYYY-MM-DD]

## 1. Contract rules

- This document is the firmware interpretation of the Rev-1 schematic.
- GPIO assignments come from the schematic, not from source-code guesses.
- Unknown information is marked `TBD`.
- Electrical behavior must be verified before being marked `VERIFIED`.
- Safe states take priority over normal functional states.

## 2. ESP32 peripheral assignments

| Function | ESP32 peripheral | Pins | Frequency/baud | DMA | Owner | Verified |
|---|---|---|---:|---|---|---|
| AD7779 control SPI | [TBD] | [TBD] | [TBD] | [YES/NO] | Board/ADC | [ ] |
| AD7779 data interface | [TBD] | [TBD] | [TBD] | [YES/NO] | Acquisition | [ ] |
| LSM6DSV SPI | [TBD] | [TBD] | [TBD] | [YES/NO] | Acquisition | [ ] |
| MAX-M10S UART | [TBD] | [TBD] | [TBD] | N/A | GNSS | [ ] |
| Card-slot 1 I²C | [TBD] | [TBD] | [TBD] | N/A | Board/card | [ ] |
| Card-slot 2 I²C | [TBD] | [TBD] | [TBD] | N/A | Board/card | [ ] |
| SDMMC | [TBD] | [TBD] | [TBD] | [YES/NO] | Storage | [ ] |
| Debug/host UART | [TBD] | [TBD] | [TBD] | N/A | Transport | [ ] |

## 3. Direct ESP32 signals

| Signal | GPIO | Direction | Active level | Physical pull | Boot state | Safe state | Owner | Timing/interrupt | Verified |
|---|---:|---|---|---|---|---|---|---|---|
| `ADC_DRDY` | [TBD] | Input | [TBD] | [TBD] | Input | Input | Acquisition | Falling-edge interrupt | [ ] |
| `ADC_CS` | [TBD] | Output | Low | [TBD] | [TBD] | High | Board/ADC | [TBD] | [ ] |
| `DEVICE_DETECT_1` | [TBD] | Input | [TBD] | [TBD] | Input | Input | Card detection | Debounce [TBD] | [ ] |
| `DEVICE_DETECT_2` | [TBD] | Input | [TBD] | [TBD] | Input | Input | Card detection | Debounce [TBD] | [ ] |
| `ESP32_GPS_RX` | [TBD] | Input | N/A | [TBD] | Input | Input | GNSS | UART | [ ] |
| `ESP32_GPS_TX` | [TBD] | Output | N/A | [TBD] | [TBD] | Idle high | GNSS | UART | [ ] |
| [ADD SIGNAL] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |

## 4. Shift-register control signals

### Shift-register interface

| Signal | GPIO | Direction | Active level | Boot state | Safe state | Verified |
|---|---:|---|---|---|---|---|
| `SR_DATA` | [TBD] | Output | [TBD] | [TBD] | [TBD] | [ ] |
| `SR_SHIFT_CLK` | [TBD] | Output | [TBD] | [TBD] | [TBD] | [ ] |
| `SR_LATCH` | [TBD] | Output | [TBD] | [TBD] | [TBD] | [ ] |
| `SR_OE_N` | [TBD] | Output | Low enables | [TBD] | Disabled/high | [ ] |
| `SRCLR` | [TBD] | Output | [TBD] | [TBD] | Clear asserted | [ ] |

### Shift-register outputs

| Bit | Schematic signal | Active level | Reset value | Safe value | Owner | Purpose | Verified |
|---:|---|---|---:|---:|---|---|---|
| 0 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 1 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 2 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 3 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 4 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 5 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 6 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 7 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |

Repeat for the second shift register.

## 5. ADC channel mapping

| ADC channel | Card slot | Measurement | Differential inputs | Default gain | Stored by default | Verified |
|---:|---:|---|---|---:|---|---|
| 0 | [TBD] | Magnetic [X/Y/Z] | [TBD] | [TBD] | Yes | [ ] |
| 1 | [TBD] | Magnetic [X/Y/Z] | [TBD] | [TBD] | Yes | [ ] |
| 2 | [TBD] | Magnetic [X/Y/Z] | [TBD] | [TBD] | Yes | [ ] |
| 3 | [TBD] | Thermistor | [TBD] | [TBD] | Yes | [ ] |
| 4 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 5 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 6 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |
| 7 | [TBD] | [TBD] | [TBD] | [TBD] | [TBD] | [ ] |

## 6. Power rails

| Rail | Control signal | Active level | Supplies | Default | Required before | Settling time | Fault indication |
|---|---|---|---|---|---|---|---|
| `3V3A` | [TBD] | [TBD] | [TBD] | Off | ADC/card init | [TBD] | [TBD] |
| `9VA` | [TBD] | [TBD] | Magnetic bridge | Off | Magnetic acquisition | [TBD] | [TBD] |
| `-5V` | [TBD] | [TBD] | Analog circuitry | Off | Magnetic acquisition | [TBD] | [TBD] |
| `10V` | [TBD] | [TBD] | [TBD] | Off | [TBD] | [TBD] | [TBD] |
| `18V` | [TBD] | [TBD] | Pulse circuitry | Off | SET/RESET | [TBD] | [TBD] |

## 7. Power-up sequence

| Step | Action | Delay/condition | Verification | Failure response |
|---:|---|---|---|---|
| 1 | Configure direct control GPIOs safely | Immediate | [TBD] | Remain safe |
| 2 | Disable shift-register outputs | Immediate | `SR_OE_N` inactive | Remain safe |
| 3 | Load safe shift-register image | [TBD] | Readback/shadow state | Remain safe |
| 4 | Enable required digital rail | [TBD] | [TBD] | Disable rails |
| 5 | Enable required analog rails | [TBD] | [TBD] | Disable rails |
| 6 | Enable ADC master clock | [TBD] | Clock detected | Disable ADC |
| 7 | Reset and initialize AD7779 | [TBD] | Status/identity checks | Report ADC fault |
| 8 | Detect and initialize analog card | [TBD] | Card detected | Mark unavailable |
| 9 | Start acquisition when requested | Command | DRDY active | Report failure |

## 8. Power-down sequence

| Step | Action | Delay/condition | Verification |
|---:|---|---|---|
| 1 | Stop acquisition | [TBD] | No pending ADC transaction |
| 2 | Place pulse controls inactive | [TBD] | Safe-state image |
| 3 | Stop ADC/clock | [TBD] | [TBD] |
| 4 | Disable analog rails | [TBD] | [TBD] |
| 5 | Disable card power | [TBD] | [TBD] |
| 6 | Retain required keepalive state | [TBD] | [TBD] |

## 9. SD/USB ownership states

| State | ESP32 SDMMC | USB2641 | Mux enabled | Mux selection | Filesystem |
|---|---|---|---|---|---|
| Safe idle | High impedance | Reset/disabled | [TBD] | [TBD] | Unmounted |
| ESP32 owns SD | Enabled | Reset/isolated | Enabled | ESP32 | Mounted |
| Transitioning | Disabled | Reset/isolated | Disabled | [TBD] | Unmounted |
| USB owns SD | High impedance | Enabled | Enabled | USB2641 | Unmounted by ESP32 |
| Fault | High impedance | Reset/disabled | Disabled | [TBD] | Unmounted |

## 10. ESP32-to-USB transition

| Step | Action | Required delay/condition |
|---:|---|---|
| 1 | Stop accepting storage writes | Queue drained |
| 2 | Flush and close current recording | Successful close |
| 3 | Unmount filesystem | Successful unmount |
| 4 | Stop SDMMC and release pins | [TBD] |
| 5 | Disable SD mux | [TBD] |
| 6 | Select USB2641 side | [TBD] |
| 7 | Enable mux | [TBD] |
| 8 | Release/reset USB2641 | [TBD] |
| 9 | Report USB ownership | State confirmed |

## 11. USB-to-ESP32 transition

[CREATE THE REVERSE SEQUENCE USING THE SAME TABLE FORMAT]

## 12. Analog-card detection

- Detect signal active level: [TBD]
- Physical pull resistor: [TBD]
- Detection at startup: [YES / NO]
- Hot-plug supported: [YES / NO]
- Debounce interval: [TBD]
- Card identification method: [TBD]
- Power allowed before identification: [TBD]
- Behavior when removed during acquisition: [TBD]
- Behavior for unknown card: [TBD]

## 13. Magnetic SET/RESET sequence

| Parameter | SET | RESET |
|---|---:|---:|
| Control signal | [TBD] | [TBD] |
| Active level | [TBD] | [TBD] |
| Pulse width | [TBD] | [TBD] |
| Pre-pulse delay | [TBD] | [TBD] |
| Post-pulse settling | [TBD] | [TBD] |
| Required rail | [TBD] | [TBD] |
| Maximum repetition | On demand | On demand |

Sequence:

1. Confirm magnetic card is present.
2. Confirm acquisition/pulse resource is available.
3. Enable required pulse rail, if necessary.
4. Wait for rail settling.
5. Mark acquisition state as pulse/transient.
6. Generate the firmware-timed pulse.
7. Return pulse output inactive.
8. Wait for analog settling.
9. Resume normal sample-valid state.
10. Record pulse result and timestamp.

## 14. Open hardware questions

| Question | Responsible person | Source | Status |
|---|---|---|---|
| [TBD] | [TBD] | Schematic/datasheet/test | Open |

## 15. Verification record

| Requirement | Test method | Result | Date | Evidence |
|---|---|---|---|---|
| Safe boot outputs | Oscilloscope/logic analyzer | [TBD] | [TBD] | [TBD] |
| ADC DRDY timing | Logic analyzer | [TBD] | [TBD] | [TBD] |
| Power sequence | Oscilloscope | [TBD] | [TBD] | [TBD] |
| SD mux isolation | Logic analyzer/multimeter | [TBD] | [TBD] | [TBD] |
| SET pulse width | Oscilloscope | [TBD] | [TBD] | [TBD] |