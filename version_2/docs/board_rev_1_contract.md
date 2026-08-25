# Rev-1 Board Firmware Contract

## Document information

- Status: Draft
- PCB revision: V2-Rev-1
- Schematic project:
  `version_2/hardware/pcb/rev_1/mainboard_v2_r1/`
- Compute platform: ESP32-S3 DevKitC
- Schematic revision/date: 2026-07-23
- Last updated: 2026-08-20

## 1. ESP32 peripheral assignments

| Function | ESP32 peripheral | Pins | Frequency/baud | DMA | Owner | Verified |
|---|---|---|---:|---|---|---|
| AD7779 SPI | SPI2 | SCLK:GPIO12, MOSI:GPIO12, MISO:GPIO14 | MAX:30MHz | Yes | task_acquisition | Yes |
| AD7779 interface | GPIO | ADC_RESET:, ADC_START:, ADC_RESET:, ADC_CONVST_SAR | - | No | task_acquisition | Yes |
| LSM6DSV SPI | SPI3 | SCLK:GPIO17, MOSI:GPIO18, CS:GPIO46, MISO:GPIO9 | MAX:30MHz | No | task_gnss | Yes |
| MAX-M10S UART | UART1 | GPS_RX:GPIO45, GPS_TX:GPIO48 | MAX:921600 (baud) | No | task_acquisition | [ ] |
| Card-slot 1 I²C / GPIO | I2C0 or GPIO | GPIO40, GPIO41 | - | No | TBD | Yes |
| Card-slot 2 I²C / GPIO | I2C1 or GPIO | GPIO38, GPIO39 | - | No | TBD | Yes |
| SDMMC | SDMMC0 | CMD:GPIO16, CLK:GPIO7, D0:GPIO5, D1:GPI4, D2:GPIO15, D3:GPIO6 | [TBD] | TBD | task_storage | Yes |
| Debug/host UART | UART0 | - | - | N/A | TBD | Yes |

## 2. Direct ESP32 signals

| Signal | GPIO | Direction | Active level | Physical pull | Boot state | Safe state | Owner | Timing/interrupt | Verified |
|---|---:|---|---|---|---|---|---|---|---|
| `ADC_DRDY` | GPIO10 | Output | [TBD] | None | Input | Input | task_acquisition | Falling-edge interrupt | [ ] |
| `ADC_RESET` | SH2_C | Output | [TBD] | Pull-up, 100k | Input | Input | task_acquisition | Falling-edge interrupt | [ ] |
| `ADC_START` | SH2_D | Output | [TBD] | Pull-down, 100k | Input | Input | task_acquisition | Falling-edge interrupt | [ ] |
| `ADC_CONVST_SAR` | SH2_F | Output | [TBD] | Pull-down, 100k | Input | Input | task_acquisition | Falling-edge interrupt | [ ] |
| `ADC_MCLK_EN` | SH2_E | Output | [TBD] | Pull-up, 100k | Input | Input | task_acquisition | Falling-edge interrupt | [ ] |
| `ADC_CS` | GPIO11 | Output | Low | [TBD] | Pull-up, 10k | High | task_acquisition |  | [ ] |
| `DEVICE_DETECT_1` | GPIO3 | Input (ADC) | [TBD] | [TBD] | Input | Input | Card detection | Debounce [TBD] | [ ] |
| `DEVICE_DETECT_2` | GPIO8 | Input (ADC) | [TBD] | [TBD] | Input | Input | Card detection | Debounce [TBD] | [ ] |
| `SR_SHIFT_CLK` | GPIO19 | Output | [TBD] | None | [TBD] | [TBD] | [TBD] | [TBD] | Yes |
| `SR_DATA` | GPIO47 | Output | [TBD] | None | [TBD] | [TBD] | [TBD] | [TBD] | Yes |
| `SR_OE_N` | GPIO21 | Output | [TBD] | Pull-up, 10k | [TBD] | [TBD] | [TBD] | [TBD] | Yes |
| `SR_LATCH` | GPIO20 | Output | [TBD] | None | [TBD] | [TBD] | [TBD] | [TBD] | Yes |
| `SOLAR_PRESENT` | GPIO1 | Input | Low | Pull-up, 100k | - | - | [TBD] | - | Yes |
| `5V_USB_PRESENT` | GPIO2 | Input | Low | Pull-up, 100k | - | - | [TBD] | - | Yes |
| `EN_SUPERCAP_CHARGE` | GPIO42 | Output | [TBD] | None | [TBD] | [TBD] | [TBD] | [TBD] | Yes |
| `SD_MUX_SEL` | SH1_A | Output | [TBD] | Pull-down, 10k | [TBD] | [TBD] | [TBD] | [TBD] | Yes |
| `EN_SD_MUX` | SH1_G | Output | [TBD] | Pull-up, 100k | [TBD] | [TBD] | [TBD] | [TBD] | Yes |
| `USB2641_nRESET` | SH1_F | Output | [TBD] | Pull-up, 10k | [TBD] | [TBD] | [TBD] | [TBD] | Yes |
| `EN_LED` | SH1_H | Output | [TBD] |  | [TBD] | [TBD] | [TBD] | [TBD] | Yes |


## 6. Power rails

| Rail | Control signal | Active level | Supplies | Default | Required before | Settling time | Fault indication |
|---|---|---|---|---|---|---|---|
| `3V3A` | SH1_B | [TBD] | [TBD] | Off | ADC/card init | [TBD] | [TBD] |
| `9VA` | SH1_C | [TBD] | Magnetic bridge | Off | Magnetic acquisition | [TBD] | [TBD] |
| `-5V` | SH1_E | [TBD] | Analog circuitry | Off | Magnetic acquisition | [TBD] | [TBD] |
| `10V` | SH1_C | [TBD] | [TBD] | Off | [TBD] | [TBD] | [TBD] |
| `18V` | SH1_D | [TBD] | Pulse circuitry | Off | SET/RESET | [TBD] | [TBD] |

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
