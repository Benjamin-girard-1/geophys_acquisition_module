# Rev-1 Board Firmware Contract

## Document information

- Status: Draft
- PCB revision: V2-Rev-1
- Schematic project:
  `version_2/hardware/pcb/rev_1/mainboard_v2_r1/`
- Compute platform: ESP32-S3 DevKitC
- Schematic revision/date: 2026-07-23
- Last updated: 2026-08-25

## 1. ESP32 peripheral assignments

| Function | ESP32 peripheral | Pins | Frequency/baud | DMA | Owner | Verification |
|---|---|---|---:|---|---|---|
| AD7779 SPI | SPI2 | SCLK:GPIO12, MOSI/SDI:GPIO13, MISO/SDO:GPIO14, CS:GPIO11 | Operating: [TBD]; reads ≤20 MHz; writes ≤30 MHz | Yes | `task_acquisition` | Schematic + datasheet |
| AD7779 control | 74HC595 #2 | RESET:SH2_C, START:SH2_D, MCLK_EN:SH2_E, CONVST_SAR:SH2_F | - | No | `task_acquisition` through `board` | Schematic |
| LSM6DSV SPI | SPI3 | SCLK:GPIO17, MOSI:GPIO18, CS:GPIO46, MISO:GPIO9 | ≤10 MHz | No | `task_imu` | Schematic + datasheet |
| MAX-M10S UART | UART1 | ESP32 RX:GPIO45, ESP32 TX:GPIO48 | Target: 921600 baud | No | `task_gnss` | Schematic; bench open |
| Card-slot 1 I²C / GPIO | I2C0 or GPIO | GPIO40, GPIO41 | [TBD] | No | `card_detect` / selected card driver | Schematic |
| Card-slot 2 I²C / GPIO | I2C1 or GPIO | GPIO38, GPIO39 | [TBD] | No | `card_detect` / selected card driver | Schematic |
| SD card | SDMMC host | CMD:GPIO16, CLK:GPIO7, D0:GPIO5, D1:GPIO4, D2:GPIO15, D3:GPIO6 | [TBD] | TBD | `task_storage` | Schematic |
| Debug/host UART | UART0 | TX:GPIO43, RX:GPIO44 (DevKit default) | Target: 921600 baud | N/A | `task_communication` | DevKit assignment; bench open |

## 2. Direct ESP32 signals

| Signal | GPIO | Direction | Active level | Physical pull | Boot state | Safe state | Owner | Timing/interrupt | Verification |
|---|---:|---|---|---|---|---|---|---|---|
| `ADC_DRDY` | GPIO10 | Input | Low | None | Input | Input | `task_acquisition` | Falling-edge interrupt | Schematic + datasheet |
| `ADC_CS` | GPIO11 | Output | Low | Pull-up, 10 kΩ | High | High | `task_acquisition` | Assert only for an ADC transaction | Schematic |
| `DEVICE_DETECT_1` | GPIO3 | Analog input | Analog ID voltage | [TBD] | Input; strapping sampled | Input | `card_detect` | Sample and average; thresholds [TBD] | Schematic |
| `DEVICE_DETECT_2` | GPIO8 | Analog input | Analog ID voltage | [TBD] | Input | Input | `card_detect` | Sample and average; thresholds [TBD] | Schematic |
| `SR_SHIFT_CLK` | GPIO19 | Output | Rising edge | None | USB Serial/JTAG default | Low after claimed | `board` | No clock edges while changing ownership | Schematic |
| `SR_DATA` | GPIO47 | Output | N/A | None | Input/high impedance | Low after claimed | `board` | Stable before shift-clock edge | Schematic |
| `SR_OE_N` | GPIO21 | Output | Low enables outputs | Pull-up, 10 kΩ | High | High until safe image is latched | `board` | Enable last; disable first | Schematic |
| `SR_LATCH` | GPIO20 | Output | Rising edge | None | USB Serial/JTAG default | Low after claimed | `board` | Pulse only after complete 16-bit image | Schematic |
| `SOLAR_PRESENT` | GPIO1 | Input | Low | Pull-up, 100 kΩ | Input | Input | `board` | - | Schematic |
| `5V_USB_PRESENT` | GPIO2 | Input | Low | Pull-up, 100 kΩ | Input | Input | `board` | Debounce [TBD] | Schematic |
| `EN_SUPERCAP_CHARGE` | GPIO42 | Output | [TBD] | None | Input/high impedance | Inactive level [TBD] | `board` | Power-policy controlled | Schematic; polarity open |

## 3. Shift-register output assignments

The two 74HC595 devices form one shared 16-bit resource. Only the `board` module may write the
physical chain or its shadow image. Tasks and card drivers request logical output changes through
the board API so unrelated bits cannot overwrite one another.

| Output | Signal | Active level | Physical pull | Electrical boot state while `SR_OE_N` is high | Required safe state | Logical owner | Verification |
|---|---|---|---|---|---|---|---|
| SH1_A | `SD_MUX_SEL` | [TBD] | Pull-down, 10 kΩ | Low | ESP32 selection; exact level [TBD] | `board` | Schematic; truth level open |
| SH1_B | `EN_LDO_3V3` | [TBD] | [TBD] | [TBD] | Inactive | `board` | Schematic; polarity open |
| SH1_C | `EN_BST_10V` | [TBD] | [TBD] | [TBD] | Inactive | `board` | Schematic; polarity open |
| SH1_D | `EN_BST_18V` | [TBD] | [TBD] | [TBD] | Inactive | `board` | Schematic; polarity open |
| SH1_E | `EN_INV_-5V` | [TBD] | [TBD] | [TBD] | Inactive | `board` | Schematic; polarity open |
| SH1_F | `USB2641_nRESET` | Low | Pull-up, 10 kΩ | High | Low; USB2641 held reset/isolated | `board` | Schematic |
| SH1_G | `EN_SD_MUX` | [TBD] | Pull-up, 100 kΩ | High | Enabled for ESP32 path; exact level [TBD] | `board` | Schematic; polarity open |
| SH1_H | `EN_LED` | [TBD] | None | High impedance | Inactive | `board` | Schematic; polarity open |
| SH2_A | `RESET_2` | [TBD] | [TBD] | [TBD] | Inactive | Selected card driver | Schematic; polarity open |
| SH2_B | `SET_2` | [TBD] | [TBD] | [TBD] | Inactive | Selected card driver | Schematic; polarity open |
| SH2_C | `ADC_RESET` | Low | Pull-up, 100 kΩ | High | Low while ADC is held in reset | `task_acquisition` | Schematic + datasheet |
| SH2_D | `ADC_START` | [TBD] | Pull-down, 100 kΩ | Low | Inactive | `task_acquisition` | Schematic; polarity open |
| SH2_E | `ADC_MCLK_EN` | [TBD] | Pull-up, 100 kΩ | High | Inactive level [TBD] | `task_acquisition` | Schematic; polarity open |
| SH2_F | `ADC_CONVST_SAR` | [TBD] | Pull-down, 100 kΩ | Low | Inactive | `task_acquisition` | Schematic; polarity open |
| SH2_G | `RESET_1` | [TBD] | [TBD] | [TBD] | Inactive | Selected card driver | Schematic; polarity open |
| SH2_H | `SET_1` | [TBD] | [TBD] | [TBD] | Inactive | Selected card driver | Schematic; polarity open |

## 4. ESP32 pin restrictions and Rev-1 limitations

- GPIO3, GPIO45, and GPIO46 are ESP32-S3 strapping pins used by this board.
- GPIO45 is the MAX-M10S-to-ESP32 receive line and also selects the ESP32-S3 `VDD_SPI`
  strapping state. Verify the voltage on GPIO45 through power-up and reset while the GNSS board
  is connected and powered.
- GPIO46 is the LSM6DSV chip-select output and participates in boot/download-mode selection.
  Verify that its external state does not prevent firmware download.
- GPIO19 and GPIO20 are used for shift-register clock/latch, so the ESP32 USB Serial/JTAG
  function is unavailable once these pins are claimed by firmware.
- GPIO39 through GPIO42 overlap the default JTAG pins. Card-slot signals and supercapacitor
  control therefore make hardware JTAG unavailable in normal operation.
- MAX-M10S TIMEPULSE is not connected in Rev-1. Rev-1 uses UART-only GNSS synchronization,
  targets approximately 5 ms or better inter-device alignment, and requires bench verification.
  Sub-millisecond synchronization is not guaranteed. A dedicated non-strapping TIMEPULSE GPIO
  is reserved as a Rev-2 improvement.

## 5. Verification terminology

- **Schematic:** the net or assignment was checked in the KiCad schematic only.
- **Datasheet:** polarity, limits, or timing were checked against the component datasheet.
- **Bench:** behavior was measured on assembled hardware.
- **Open:** the item has not yet been resolved.

`Schematic` verification does not imply that the signal has been electrically tested.

### Milestone-1 blocking decisions

Not every open value prevents the first firmware work. The following gates identify when each
hardware decision becomes mandatory.

| Gate | Values that must be resolved | Blocks |
|---|---|---|
| Before enabling 74HC595 outputs | Active polarity and safe level of every SH1/SH2 signal; complete safe 16-bit image; fixed `SD_MUX_SEL`, `EN_SD_MUX`, and `USB2641_nRESET` levels | Rev-1 `board_init()` and any powered hardware test |
| Before driving `EN_SUPERCAP_CHARGE` | Active polarity, boot-safe level, and allowed power policy | Supercapacitor-charge control; keep GPIO high impedance until resolved |
| Before starting AD7779 acquisition | Analog-rail enable polarities/order/settling; `ADC_MCLK_EN` and `ADC_START` levels; reset/clock timing; conservative SPI clock | ADC initialization and acquisition bench test |
| Before accepting card detection | Detect resistor network, no-card and magnetic-card voltage windows, sampling/averaging policy, and hot-plug policy | Automatic card identification and slot availability |
| Before enabling SET/RESET commands | SET/RESET active levels, firmware control width, 18 V settling/recharge limits, minimum interval, and post-pulse settling | Magnetic pulse feature; acquisition may proceed with pulses disabled |
| Before claiming reliable Rev-1 boot | GPIO45 and GPIO46 strapping behavior with attached GNSS/IMU hardware | Hardware-validation sign-off; these milestone-2 devices remain uninitialized in milestone 1 |

SDMMC clock/DMA settings, GNSS timing, IMU configuration, and SD recording behavior do not block
milestone-1 UART acquisition. They remain milestone-2 work.


## 6. Power rails

| Rail | Control signal | Active level | Supplies | Default | Required before | Settling time | Fault indication |
|---|---|---|---|---|---|---|---|
| `3V3A` | SH1_B / `EN_LDO_3V3` | [TBD] | ADC/card analog circuitry [TBD] | Off | ADC/card init | [TBD] | [TBD] |
| `10V` | SH1_C / `EN_BST_10V` | [TBD] | Upstream supply for 9VA [verify] | Off | 9VA | [TBD] | [TBD] |
| `9VA` | Derived from 10V; no independent shift-register output | N/A | ADC/reference and magnetic bridge [TBD] | Follows 10V path | Magnetic acquisition | [TBD] | [TBD] |
| `-5VA` | SH1_E / `EN_INV_-5V` | [TBD] | Analog circuitry | Off | Magnetic acquisition | [TBD] | [TBD] |
| `18V` | SH1_D / `EN_BST_18V` | [TBD] | Pulse circuitry | Off | SET/RESET | [TBD] | [TBD] |

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

## 9. Fixed SD/USB policy

V2 Rev-1 has no runtime SD ownership transitions. The board initializes this path once and does
not expose an API for selecting the USB2641 side.

| State | ESP32 SDMMC | USB2641 | Mux selection | Filesystem |
|---|---|---|---|---|
| Boot/safe initialization | High impedance | Assert reset as soon as the safe image is applied | Select ESP32 before enabling the mux; exact levels [TBD] | Unmounted |
| Milestone-1 runtime | Not initialized | Reset/isolated | Fixed to ESP32 | Unmounted |
| Milestone-2 recording | Enabled when storage starts | Reset/isolated | Fixed to ESP32 | Mounted by ESP32 |
| Storage fault | Disabled or stopped safely | Reset/isolated | Remains fixed to ESP32 | Unmounted when possible |

Initialization requirements:

1. Keep shift-register outputs disabled while loading the safe image.
2. Put `SD_MUX_SEL` in the ESP32-selection state.
3. Assert `USB2641_nRESET`.
4. Put `EN_SD_MUX` in the state required to connect the ESP32 path.
5. Enable shift-register outputs only after all other SH1/SH2 bits are safe.
6. Never change these three SD/USB control bits during normal runtime.

The exact logic levels and any required initial settling delay remain to be verified from the mux
and USB2641 circuitry. Break-before-make timing is not a firmware requirement because runtime
ownership switching is unsupported.

## 10. Analog-card detection

- Detection signals: `DEVICE_DETECT_1` on GPIO3 and `DEVICE_DETECT_2` on GPIO8.
- Signal type: analog identification voltage; binary active level is not applicable.
- Physical pull/resistor network: [TBD from schematic].
- Detection at startup: [YES / NO].
- Hot-plug supported: [YES / NO].
- Sampling/averaging interval: [TBD].
- Power allowed before identification: [TBD].
- Behavior when removed during acquisition: [TBD].
- Behavior for unknown or ambiguous voltage: keep card controls inactive; detailed policy [TBD].

| Card state/type | Nominal detect voltage | Accepted minimum | Accepted maximum | Required rails | Driver |
|---|---:|---:|---:|---|---|
| No card | [TBD] | [TBD] | [TBD] | None | None |
| Magnetic card | [TBD] | [TBD] | [TBD] | [TBD] | `card_magnetic` |
| Unknown/invalid | Outside valid ranges | N/A | N/A | None | None |

## 11. Magnetic SET/RESET sequence

| Parameter | SET | RESET |
|---|---:|---:|
| Control signal | `SET_1` (SH2_H) or `SET_2` (SH2_B), according to slot | `RESET_1` (SH2_G) or `RESET_2` (SH2_A), according to slot |
| Active level | [TBD] | [TBD] |
| Pulse width | [TBD] | [TBD] |
| Pre-pulse delay | [TBD] | [TBD] |
| Post-pulse settling | [TBD] | [TBD] |
| Required rail | 18V pulse rail; verify final requirement | 18V pulse rail; verify final requirement |
| Repetition policy | On demand only; minimum interval [TBD] | On demand only; minimum interval [TBD] |

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

SET and RESET must never be active simultaneously. Samples acquired during the pulse and
post-pulse settling interval remain timestamped but are marked transient/invalid for scientific use.

## 12. Open hardware questions

| Question | Responsible person | Source | Status |
|---|---|---|---|
| Does the MAX-M10S TX level on GPIO45 preserve the required ESP32 `VDD_SPI` strap during every reset condition? | Hardware | Schematic + oscilloscope | Open |
| Does the GPIO46/LSM6DSV CS state allow normal boot and firmware download? | Hardware/firmware | Schematic + boot test | Open |
| What are the active polarities and boot levels of every SH1/SH2 output? | Hardware | Schematic + datasheets | Open |
| What are the exact analog-card identification voltage ranges and tolerances? | Hardware | Resistor network + ADC test | Open |
| What are the safe SET/RESET pulse widths, minimum interval, and analog settling time? | Hardware | Magnetic-card design + oscilloscope | Open |
| What exact `SD_MUX_SEL` and `EN_SD_MUX` levels establish the fixed ESP32 path, and what startup settling delay is required? | Hardware | Schematic + mux datasheet + logic analyzer | Open |
| What is the safe boot level and policy for `EN_SUPERCAP_CHARGE`? | Hardware | Schematic + power test | Open |
| Which AD7779 SPI clock is reliable on the assembled board? | Firmware/hardware | Logic analyzer + ADC test | Open |

## 13. Verification record

| Requirement | Test method | Result | Date | Evidence |
|---|---|---|---|---|
| Safe boot outputs | Oscilloscope/logic analyzer | [TBD] | [TBD] | [TBD] |
| ADC DRDY timing | Logic analyzer | [TBD] | [TBD] | [TBD] |
| Power sequence | Oscilloscope | [TBD] | [TBD] | [TBD] |
| Fixed ESP32 SD mux selection and USB2641 reset/isolation | Logic analyzer/multimeter | [TBD] | [TBD] | [TBD] |
| SET pulse width | Oscilloscope | [TBD] | [TBD] | [TBD] |
| ESP32 strapping with GNSS/IMU connected | Repeated cold boot and download test | [TBD] | [TBD] | [TBD] |
| GNSS UART inter-device alignment | Common-event comparison between two units | [TBD] | [TBD] | [TBD] |
