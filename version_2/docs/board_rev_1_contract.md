# Rev-1 Board Firmware Contract

## Document information

- Status: Draft
- PCB revision: V2-Rev-1
- Schematic project:
  `version_2/hardware/pcb/rev_1/mainboard_v2_r1/`
- Compute platform: ESP32-S3 DevKitC
- Schematic revision/date: 2026-07-23
- Last updated: 2026-09-18

## 1. ESP32 peripheral assignments

| Function | ESP32 peripheral | Pins | Frequency/baud | DMA | Owner | Verification |
|---|---|---|---:|---|---|---|
| AD7779 SPI | SPI2 | SCLK:GPIO12, MOSI/SDI:GPIO13, MISO/SDO:GPIO14, CS:GPIO11 | Initial: 8 MHz, mode 0; reads ≤20 MHz; writes ≤30 MHz; external MCLK: 8.192 MHz | Yes | `task_acquisition` | Schematic + datasheet + working V1 reference; Rev-1 bench verified at 8 MHz |
| AD7779 control | 74HC595 #2 | RESET:SH2_C, START:SH2_D, MCLK_EN:SH2_E, CONVST_SAR:SH2_F | - | No | `task_acquisition` through `board` | Schematic |
| 74HC595 chain | GPIO bit-bang | SHCP:GPIO19, STCP:GPIO20, DS:GPIO47, OE_N:GPIO21 | Initial 1 us between driver transitions; conservative limit ≤4 MHz | No | `board` | Schematic + Nexperia 74HC595 Rev. 12 datasheet |
| LSM6DSV SPI | SPI3 | SCLK:GPIO17, MOSI:GPIO18, CS:GPIO46, MISO:GPIO9 | ≤10 MHz | No | `task_imu` | Schematic + datasheet |
| MAX-M10S UART | UART1 | ESP32 RX:GPIO45, ESP32 TX:GPIO48 | Target: 921600 baud | No | `task_gnss` | Schematic; bench open |
| Card-slot 1 I²C / GPIO | I2C0 or GPIO | GPIO40, GPIO41 | [TBD] | No | `card_detect` / selected card driver | Schematic |
| Card-slot 2 I²C / GPIO | I2C1 or GPIO | GPIO38, GPIO39 | [TBD] | No | `card_detect` / selected card driver | Schematic |
| SD card | SDMMC host | CMD:GPIO16, CLK:GPIO7, D0:GPIO5, D1:GPIO4, D2:GPIO15, D3:GPIO6 | 20 MHz, 4-bit | Yes | `task_storage` | Schematic; Rev-1 mount/catalog bench pass |
| Debug/host UART | UART0 | TX:GPIO43, RX:GPIO44 (DevKit default) | Default: 921600 baud, 8-N-1, no flow control; higher rates configurable while stopped | N/A | `task_communication` | DevKit assignment; bench open |

## 2. Direct ESP32 signals

| Signal | GPIO | Direction | Active level | Physical pull | Boot state | Safe state | Owner | Timing/interrupt | Verification |
|---|---:|---|---|---|---|---|---|---|---|
| `ADC_DRDY` | GPIO10 | Input | Low | None | Input | Input | `task_acquisition` | Falling-edge interrupt | Schematic + datasheet |
| `ADC_CS` | GPIO11 | Output | Low | Pull-up, 10 kΩ | High | High | `task_acquisition` | Assert only for an ADC transaction | Schematic |
| `DEVICE_DETECT_1` | GPIO3 | Analog input | Analog ID voltage | Pull-up, 10 kΩ to 3.3 V | Input; strapping sampled | Input | `card_detect` | Sample and average at startup | Schematic |
| `DEVICE_DETECT_2` | GPIO8 | Analog input | Analog ID voltage | Pull-up, 10 kΩ to 3.3 V | Input | Input | `card_detect` | Sample and average at startup | Schematic |
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
| SH1_A | `SD_MUX_SEL` | Selector: Low = ESP32; High = USB2641 | Pull-down, 10 kΩ | Low; ESP32 selected | Low; ESP32 selected | `board` | Schematic + mux datasheet |
| SH1_B | `EN_LDO_3V3` | High enables | Device-internal pull-down | Low; disabled | Low; disabled | `board` | Schematic + regulator datasheet |
| SH1_C | `EN_BST_10V` | High enables | Pull-down, 100 kΩ | Low; disabled | Low; disabled | `board` | Schematic + regulator datasheet |
| SH1_D | `EN_BST_18V` | High enables | Pull-down, 100 kΩ | Low; disabled | Low; disabled | `board` | Schematic + regulator datasheet |
| SH1_E | `EN_INV_-5V` | High enables | None external | Undefined if the regulator input supply is present | Low; disabled | `board` | Schematic + regulator datasheet; boot level not bench tested |
| SH1_F | `USB2641_nRESET` | Low asserts reset | Pull-up, 10 kΩ | High; reset released | Low; USB2641 held reset/isolated | `board` | Schematic + USB2641 datasheet |
| SH1_G | `EN_SD_MUX` | Low enables mux; High disconnects all channels | Pull-up, 100 kΩ | High; mux disabled | Low; mux enabled on ESP32 path | `board` | Schematic + mux datasheet |
| SH1_H | `EN_LED` | High enables | Gate pull-down, 100 kΩ | Low; disabled | Low; disabled | `board` | Schematic |
| SH2_A | `RESET_2` | High pulse request | Pull-down, 100 kΩ | Low; inactive | Low; inactive | Selected card driver | Schematic + reused magnetic-card design; bench open |
| SH2_B | `SET_2` | High pulse request | Pull-down, 100 kΩ | Low; inactive | Low; inactive | Selected card driver | Schematic + reused magnetic-card design; bench open |
| SH2_C | `ADC_RESET` | Low asserts reset | Pull-up, 100 kΩ | High; reset released | Low; reset asserted | `task_acquisition` | Schematic + AD7779 datasheet |
| SH2_D | `ADC_START` | Low synchronization pulse; idle High | Pull-down, 100 kΩ | Low; synchronization asserted | High; inactive | `task_acquisition` | Schematic + AD7779 datasheet |
| SH2_E | `ADC_MCLK_EN` | High or floating enables clock output | Pull-up, 100 kΩ | High; clock enabled | Low; clock disabled | `task_acquisition` | Schematic + oscillator datasheet |
| SH2_F | `ADC_CONVST_SAR` | Rising edge/High pulse requests conversion | Pull-down, 100 kΩ | Low; inactive | Low; inactive because SAR is unused | `task_acquisition` | Schematic + AD7779 datasheet |
| SH2_G | `RESET_1` | High pulse request | Pull-down, 100 kΩ | Low; inactive | Low; inactive | Selected card driver | Schematic + reused magnetic-card design; bench open |
| SH2_H | `SET_1` | High pulse request | Pull-down, 100 kΩ | Low; inactive | Low; inactive | Selected card driver | Schematic + reused magnetic-card design; bench open |

### Safe 16-bit image

The serialized image uses SH1_A through SH1_H as bits 15 through 8 and SH2_A through SH2_H as
bits 7 through 0. Bit 0 is shifted first because it must travel through both 74HC595 devices.

The canonical safe image is `0x0010`. Only SH2_D / `ADC_START` is High. This image:

- selects and enables the fixed ESP32 SD path;
- holds the USB2641 in reset;
- disables the analog rails, pulse supply, ADC master clock, SAR request, and LED;
- holds the AD7779 in reset with `ADC_START` inactive; and
- holds every SET/RESET request inactive.

The resistor-defined states while `SR_OE_N` is High are not equivalent to this safe image. In
particular, the USB2641 reset is released, the ADC reset is released, `ADC_START` is Low, and the
ADC oscillator is enabled until the safe image is latched and the 74HC595 outputs are enabled.

## 4. ESP32 pin restrictions and Rev-1 limitations

- GPIO3, GPIO45, and GPIO46 are ESP32-S3 strapping pins used by this board.
- GPIO3 is also `DEVICE_DETECT_1`. Its card-divider voltage is present while the ESP32 samples its
  strapping pins, before firmware can configure or average the ADC input. Verify cold boot and
  firmware download with no card and with each defined identification resistor in slot 1.
- GPIO45 is the MAX-M10S-to-ESP32 receive line and also selects the ESP32-S3 `VDD_SPI`
  strapping state. Verify the voltage on GPIO45 through power-up and reset while the GNSS board
  is connected and powered.
- GPIO46 is the LSM6DSV chip-select output and participates in boot/download-mode selection.
  The LSM6DSV `CS` input has an internal 30-50 kOhm pull-up that opposes the ESP32-S3 weak
  pull-down during reset. Rev-1 bench testing confirmed unreliable firmware download as a result.
  The Rev-1 workaround is to hold GPIO46 Low while the boot straps are sampled; firmware cannot
  correct this because it starts afterward.
- GPIO19 and GPIO20 are used for shift-register clock/latch, so the ESP32 USB Serial/JTAG
  function is unavailable once these pins are claimed by firmware.
- GPIO39 through GPIO42 overlap the default JTAG pins. Card-slot signals and supercapacitor
  control therefore make hardware JTAG unavailable in normal operation.
- MAX-M10S TIMEPULSE is not connected in Rev-1. Rev-1 uses UART-only GNSS synchronization,
  targets approximately 5 ms or better inter-device alignment, and requires bench verification.
  Sub-millisecond synchronization is not guaranteed. A dedicated non-strapping TIMEPULSE GPIO
  is reserved as a Rev-2 improvement.
- Rev-2 must move `CS_LSM` from GPIO46 to a non-strapping GPIO. A permanent strong pull-down on
  GPIO46 is only a Rev-1 rework option, not the preferred Rev-2 correction.

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
| `3V3A` | SH1_B / `EN_LDO_3V3` | High | ADC/card analog circuitry [TBD] | Off | ADC/card init | 100 ms initial value | [TBD] |
| `10V` | SH1_C / `EN_BST_10V` | High | Upstream supply for 9VA [verify] | Off | 9VA | 100 ms initial value | [TBD] |
| `9VA` | Derived from 10V; no independent shift-register output | N/A | ADC/reference and magnetic bridge [TBD] | Follows 10V path | Magnetic acquisition | Included in 10 V wait | [TBD] |
| `-5VA` | SH1_E / `EN_INV_-5V` | High | Analog circuitry | Off | Magnetic acquisition | 100 ms initial value | [TBD] |
| `18V` | SH1_D / `EN_BST_18V` | High | Pulse circuitry | Off; enabled on demand only | SET/RESET | 100 ms before pulse | [TBD] |

The initial delays are deliberately conservative. Their purpose is to avoid simultaneous rail startup
and large combined inrush, not to meet a known tight device requirement. Rev-1 bench measurements may
shorten them later without changing the sequence or ownership model.

## 7. Power-up sequence

| Step | Action | Delay/condition | Verification | Failure response |
|---:|---|---|---|---|
| 1 | Configure direct control GPIOs safely | Immediate | [TBD] | Remain safe |
| 2 | Disable shift-register outputs | Immediate | `SR_OE_N` inactive | Remain safe |
| 3 | Load safe shift-register image `0x0010` | Before enabling outputs | Confirm 16-bit shadow state | Remain safe |
| 4 | Enable shift-register outputs | Safe image already latched | `SR_OE_N` active | Remain safe |
| 5 | Detect both analog cards | Average each ID input for the startup detection window | Stable classification | Keep card controls inactive |
| 6 | Enable `3V3A` when required | Wait 100 ms | [TBD] | Disable controlled rails |
| 7 | Enable `10V`, producing the derived 9VA path | Wait 100 ms | [TBD] | Disable controlled rails |
| 8 | Enable `-5VA` | Wait 100 ms | [TBD] | Disable controlled rails |
| 9 | Enable ADC master clock | Wait at least 5 ms | Clock detected | Disable ADC |
| 10 | Reset and initialize AD7779 | Use the initial sequence below | Status/identity checks | Report ADC fault |
| 11 | Initialize the detected magnetic card | After required rails are stable | Card available | Mark unavailable |
| 12 | Start acquisition when requested | Command | DRDY active | Report failure |

The 18 V rail is not part of normal acquisition startup. It remains off until an on-demand SET or
RESET request, then is enabled and allowed to settle for 100 ms before the pulse.

### Initial AD7779 control and SPI baseline

The first Rev-1 implementation uses the electrically proven V1 settings as a conservative baseline:

| Parameter | Initial Rev-1 value |
|---|---|
| SPI host and format | SPI2, full duplex, mode 0, MSB first |
| SPI clock | 8 MHz for register access and conversion-frame reads |
| Chip select | GPIO11, software controlled, active Low |
| CS timing margin | 1 µs after asserting CS and 1 µs before releasing CS |
| Transfer context | Synchronous DMA-backed transfer from `task_acquisition`; never busy-polled or called from the DRDY ISR |
| Master clock | Set `ADC_MCLK_EN` High, then wait at least 5 ms |
| ADC power mode | High-resolution mode only |
| ADC reference | External REF1/REF2 selection using the board-filtered internal `REF_OUT`; keep the reference-output buffer enabled |
| Hardware reset | Hold `ADC_RESET` Low for 2 ms, release High, then wait at least 10 ms |
| START | Hold Low through clock/reset startup, then drive High and leave High when inactive |
| CONVST_SAR | Keep Low because milestone 1 does not use the SAR ADC |
| Reset method | Hardware `ADC_RESET` pulse followed by the datasheet-defined SPI software reset (SDI High for 64 SCLKs), then a 5 ms settling margin inherited from the working V1 implementation |
| Initialization check | Poll `INIT_COMPLETE` for up to 500 ms; tolerate malformed/stale register responses during that window, but require a valid `0x20` response header before proceeding |
| Configuration synchronization | Toggle `SPI_SYNC` Low, wait 10 us, return it High, then discard 5 ms of settling time before enabling SPI conversion readback; margins inherited from the working V1 implementation and pending Rev-1 waveform verification |

The V1 AD7779 register definitions, CRC handling, configuration sequence, and frame decoding are
working references. They must be adapted to the V2 architecture: the portable AD7779 driver remains
free of ESP-IDF and FreeRTOS, while the ESP32 platform supplies the SPI transaction and GPIO/interrupt
mechanisms. The V1 dedicated streaming task inside its hardware adapter is not an architectural
precedent for V2.

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
| Boot/safe initialization | High impedance | Assert reset as soon as the safe image is applied | `SD_MUX_SEL` Low and `EN_SD_MUX` Low | Unmounted |
| Milestone-1 runtime | Not initialized | Reset/isolated | Fixed to ESP32 | Unmounted |
| Milestone-2 recording | Enabled when storage starts | Reset/isolated | Fixed to ESP32 | Mounted by ESP32 |
| Storage fault | Disabled or stopped safely | Reset/isolated | Remains fixed to ESP32 | Unmounted when possible |

Initialization requirements:

1. Keep shift-register outputs disabled while loading the safe image.
2. Set `SD_MUX_SEL` Low to select the ESP32 normally closed path.
3. Set `USB2641_nRESET` Low to assert reset.
4. Set `EN_SD_MUX` Low to enable the selected ESP32 path.
5. Enable shift-register outputs only after all other SH1/SH2 bits are safe.
6. Never change these three SD/USB control bits during normal runtime.

Firmware implementation requirements:

- Encode these three fixed values in the Rev-1 board configuration and in the canonical safe image;
  application code must not know their shift-register positions or electrical levels.
- `board_init()` applies the complete safe image before enabling the 74HC595 outputs.
- Every later shift-register update preserves `SD_MUX_SEL = Low`, `EN_SD_MUX = Low`, and
  `USB2641_nRESET = Low`; `board_enter_safe_state()` reasserts the same values.
- Do not expose a board API for changing SD ownership, disabling the ESP32 mux path, or releasing
  the USB2641 reset.
- The recording firmware initializes SDMMC and mounts FatFs without formatting.
  The USB2641 remains reset and isolated throughout.

The fixed logic levels are resolved from the schematic and mux/USB2641 datasheets. Any required
initial settling delay before milestone-2 SDMMC initialization remains a bench item. Break-before-make
timing is not a firmware requirement because runtime ownership switching is unsupported.

## 10. Analog-card detection

- Detection signals: `DEVICE_DETECT_1` on GPIO3 and `DEVICE_DETECT_2` on GPIO8.
- Signal type: analog identification voltage; binary active level is not applicable.
- Physical resistor network: 10 kΩ pull-up to 3.3 V on the mainboard and one card-identification
  resistor from the detect signal to ground on the expansion card.
- Detection at startup: Yes, before enabling the controlled analog rails.
- Hot-plug supported: No for milestone 1; changing a card requires a restart.
- Sampling/averaging interval: initial target is 2 seconds at 100 samples/s per slot. Use a median or
  trimmed mean so a small number of startup outliers cannot determine the card type. Configure the
  ESP32 ADC input range to cover 3.3 V and use its calibration support when converting to millivolts.
- Power allowed before identification: fixed 3.3 V digital supply only; controlled analog and pulse
  rails remain off.
- Behavior when removed during acquisition: unsupported in milestone 1; detailed fault detection is
  deferred because the ID inputs are not monitored continuously.
- Behavior for unknown or ambiguous voltage: keep card controls and controlled rails inactive for
  that slot and report the measured voltage to the host.

For a 3.3 V pull-up supply, the expected divider voltage is
`3.3 V × R_card / (10 kΩ + R_card)`. Initial classification boundaries are the midpoints between
the nominal voltages. These thresholds are suitable for first firmware but remain subject to calibrated
ESP32 ADC and assembled-card measurements.

| Card state/type | Card resistor to ground | Nominal detect voltage | Initial accepted minimum | Initial accepted maximum | Milestone-1 handling |
|---|---:|---:|---:|---:|---|
| No card | Open circuit | 3.30 V | 2.750 V | ADC full scale | Leave slot inactive |
| Magnetic card | 20 kΩ | 2.20 V | 1.925 V | 2.750 V | Initialize `card_magnetic` |
| Accelerometer card | 10 kΩ | 1.65 V | 1.375 V | 1.925 V | Identify and report; driver deferred |
| Resistivity card | 5 kΩ | 1.10 V | 0.550 V | 1.375 V | Identify and report; driver deferred |
| Unknown/invalid | Short, out-of-range, or unstable | N/A | Outside accepted ranges | Outside accepted ranges | Keep slot inactive and report voltage |

## 11. Magnetic SET/RESET sequence

| Parameter | SET | RESET |
|---|---:|---:|
| Control signal | `SET_1` (SH2_H) or `SET_2` (SH2_B), according to slot | `RESET_1` (SH2_G) or `RESET_2` (SH2_A), according to slot |
| Active level | High pulse; Low inactive | High pulse; Low inactive |
| Pulse width | Initial value: 200 us; bench verification required | Initial value: 200 us; bench verification required |
| Pre-pulse delay | 100 ms after enabling the 18 V rail | 100 ms after enabling the 18 V rail |
| Post-pulse settling | [TBD by ADC observation] | [TBD by ADC observation] |
| Required rail | 18 V pulse rail, enabled only for the request | 18 V pulse rail, enabled only for the request |
| Repetition policy | On demand only; no periodic operation; minimum interval [TBD] | On demand only; no periodic operation; minimum interval [TBD] |

The initial timing values come from the working V1 `hmc100x` implementation. They are starting
values, not Rev-1 verification evidence. V1 used a 10 ms interval in its combined diagnostic, in
the order RESET then SET. The V2 interface currently describes that diagnostic in the opposite
order, so a combined operation must not be implemented until its required order and measurement
purpose are resolved. Independent on-demand SET and RESET requests are unaffected by this question.

The V1 default left the 9VA magnetic-bridge supply enabled during a pulse. V2 follows that behavior:
the pulse sequence does not cycle the normal acquisition rails. The V1 periodic task, direct access
to shift-register bits, simultaneous selection of both slots, and policy of leaving the 18 V rail on
are not architectural precedents for V2.

Sequence:

1. Confirm that a magnetic card is present in the requested slot.
2. Acquire the acquisition/pulse resource and reject overlapping pulse requests.
3. Through the board API, force SET and RESET inactive for both slots.
4. Mark acquisition state as pulse/transient before switching the pulse rail.
5. Enable the 18 V pulse rail and wait 100 ms.
6. Assert only the requested slot and operation for an initial 200 us.
7. Return the pulse output inactive, then disable the 18 V rail.
8. Wait for the bench-defined analog settling interval while samples remain transient.
9. Resume normal sample-valid state.
10. Record the requested operation, actual timing, affected frames, and result.

SET and RESET must never be active simultaneously. Samples acquired during the pulse and
post-pulse settling interval remain timestamped but are marked transient/invalid for scientific use.
Every success, failure, timeout, and abort path must leave all SET/RESET outputs Low and the 18 V
rail disabled. `task_acquisition` owns command serialization and sample-validity timing.
`analog_cards/magnetic` owns the card-specific pulse generation, including pulse width, ordering,
dead time, and settling policy. It requests the 18 V rail and logical SET/RESET state through bound
callbacks. Only the Rev-1 `board` module controls the mainboard 18 V rail and translates those
logical requests into shift-register changes. `analog_cards/acc_geoph` has no SET/RESET behavior.

## 12. Open hardware questions

| Question | Responsible person | Source | Status |
|---|---|---|---|
| Does the MAX-M10S TX level on GPIO45 preserve the required ESP32 `VDD_SPI` strap during every reset condition? | Hardware | Schematic + oscilloscope | Open |
| GPIO46/LSM6DSV CS pull-up prevents reliable firmware download without holding GPIO46 Low during strap sampling | Hardware/firmware | Schematic + LSM6DSV datasheet + boot test | Confirmed Rev-1 issue; move `CS_LSM` to a non-strapping GPIO in Rev-2 |
| Do the no-card, 20 kΩ, 10 kΩ, and 5 kΩ slot-1 detection states preserve the required GPIO3 strap behavior? | Hardware/firmware | Repeated cold boot and download test | Open |
| Do the assembled-board SH1/SH2 boot and safe levels match the schematic/datasheet-derived contract, especially floating `EN_INV_-5V`? | Hardware | Oscilloscope/logic analyzer | Open |
| Does the assembled unit's shift-register control wiring match the checked-in Rev-1 netlist (`DATA=GPIO47`, `OE_N=GPIO21`, `LATCH=GPIO20`, `SHIFT_CLK=GPIO19`)? The V1 firmware port swaps these four assignments. | Hardware/firmware | Continuity test plus shift-clock/latch logic-analyzer capture | Open; resolve before treating the V1 image as a definitive ADC comparison |
| Do the assembled-card divider voltages fall inside the initial identification windows with adequate margin? | Hardware | Calibrated ESP32 ADC + multimeter | Open |
| Does the initial 200 us SET/RESET pulse provide the required current and duration, and what minimum request interval and post-pulse analog settling time are safe? | Hardware | V1 reference + magnetic-card design + oscilloscope/ADC data | Open |
| What startup settling delay, if any, is required before SDMMC initialization? | Hardware | Repeated cold-start mount test + logic analyzer if failures occur | Open; one clean-build mount passed without an explicit delay |
| What is the safe boot level and policy for `EN_SUPERCAP_CHARGE`? | Hardware | Schematic + power test | Open |
| Does the initial 8 MHz, mode-0, manual-CS AD7779 SPI baseline operate reliably on the assembled Rev-1 board? | Firmware/hardware | ADC register and recording tests | Previously confirmed at 8 MHz with configuration readback, reset-default readback, and 50,000 consecutive status-register reads. With main +5 V restored on 2026-09-18, V2 again passed initialization and repeatedly read conversion frames into SD records. Record status/data inspection and the excessive observed DRDY rate remain open. |

## 13. Verification record

| Requirement | Test method | Result | Date | Evidence |
|---|---|---|---|---|
| Safe boot outputs | Oscilloscope/logic analyzer | [TBD] | [TBD] | [TBD] |
| ADC DRDY timing | Logic analyzer | [TBD] | [TBD] | [TBD] |
| Power sequence | Oscilloscope | [TBD] | [TBD] | [TBD] |
| Fixed ESP32 SD mux selection and USB2641 reset/isolation | Logic analyzer/multimeter | [TBD] | [TBD] | [TBD] |
| SDMMC mount and `/recordings` catalog | Protocol probe on Rev-1 at 20 MHz, 4-bit | Pass; file open was rolled back after the separate ADC startup failure | 2026-09-17 | `hardware_recording_probe.py` console result and `RECORDING_START` response |
| Recording ADC startup | Two consecutive `RECORDING_START` attempts on Rev-1 | Blocked: first `STATUS_REG_3` read returned SPI header `0x00` instead of `0x20`; each attempt returned an integrity error, released resources for retry, and retained no recording file | 2026-09-17 | Temporary error-context capture: ADC/read detail `0x00005F00`; protocol probe catalog checks |
| V1/V2 ADC comparison | Flash current V2, then the preserved June V1 image and its `ADC DIAG`, then restore V2 | Both implementations received `00 00 00` for every ADC register transaction after hardware and SPI resets; current V2 `HELLO`/`DEVICE_CONFIG` passed after restoration. V1 evidence is qualified because its shift-register GPIO map conflicts with the Rev-1 netlist. | 2026-09-18 | V1 UART diagnostic output, schematic/PCB netlist inspection, and V2 hardware probes on `/dev/cu.usbserial-114620` |
| ADC power-fault diagnosis | Restore the missing main +5 V supply and retry V2 ADC initialization | The prior all-zero ADC SPI response was caused by absent main +5 V, which left 3V3A unavailable. With +5 V restored, the response header, `INIT_COMPLETE`, reset defaults, configuration, and recording startup passed. | 2026-09-18 | Bench power correction and repeated protocol recording probes |
| ADC communication recovery | Retry V2 `RECORDING_START`, inspect initialization faults, correct expected-reset handling, rebuild, flash, and retry with valid rails | The deliberate reset-detected flag is now acknowledged rather than reported as an unexpected hardware reset. AD7779 initialization and recording startup pass repeatedly with the main +5 V supply present. | 2026-09-18 | Protocol probe results, driver control-flow inspection, successful ESP-IDF build/flash, and passing host tests |
| End-to-end ADC-to-SD recording | Start recording, poll `DEVICE_GET_CONFIG` while active, stop, inspect catalog size, delete only the test file | Pass: a two-second 1 kSPS-configured probe produced 206,848 bytes (404 complete 512-byte records), remained command-responsive, crossed the 64-record sync interval, stopped and cataloged cleanly, and deleted the test file. Record contents were not read back; the block count implies a DRDY rate above the configured ODR and remains under investigation. | 2026-09-18 | `hardware_recording_probe.py` result on `/dev/cu.usbserial-114620` |
| Temporary UART recording extraction | Read a closed SD recording through command `0xf000` and validate the returned block | Pass: retrieved the first 512 bytes of `scope_continuous`; command framing, chunk offsets, file size, `\DAT` magic, and record CRC validated. The stored block reported critical status and sample range -3,212,801 to 8,388,607, so it is not evidence of valid scientific data. | 2026-09-19 | Host decoder and live UART query on `/dev/cu.usbserial-114620` |
| Fresh SD recording after extraction implementation | Enumerate and delete the two expendable existing files, start a named capture, wait 0.1 s, stop, inspect and download the file, validate every record, then delete the test file | Storage/protocol pass: count/info/delete left an empty catalog; the fresh capture contained ten complete 512-byte records (5,120 bytes), every record CRC passed, and deletion restored the empty catalog. Acquisition quality remains open: every record had critical status, samples reached both 24-bit rails, and the sequence exposed 32 missing conversions. | 2026-09-19 | Live recording/catalog commands and `hardware_recording_probe.py` on `/dev/cu.usbserial-114620`; downloaded image `/private/tmp/codex_clean2_0919.bin` |
| Recording task scheduling and storage queueing | Exercise recording beyond eight records while issuing control commands | Pre-fix, a zero-tick acquisition poll starved lower-priority tasks and mismatched FreeRTOS queue-set draining asserted in `prvNotifyQueueSetContainer`, rebooting after several records. Notification-driven acquisition/storage, bounded batches, a 1 kHz tick, and control-plane scheduling now pass the 404-record probe without reset. | 2026-09-18 | Captured panic backtrace plus post-fix protocol probe |
| Configured ADC output rate | Measure `ADC_DRDY` and inspect stored conversion timestamps while the device reports the 1 kSPS preset | Partial: an earlier oscilloscope test measured approximately 61.5 us (16.26 kSPS). With the current post-synchronization SRC re-latch, a fresh file's first 80 conversions remained near 16 kSPS, followed by 120 conversions at approximately 1 kSPS. The steady-state rate is now consistent with configuration, but the startup transient and a 32-conversion gap remain unresolved and require a confirming scope trace. | 2026-09-19 | User oscilloscope measurement plus timestamps/sequences from `/private/tmp/codex_clean2_0919.bin` |
| SET pulse width | Oscilloscope | [TBD] | [TBD] | [TBD] |
| ESP32 strapping with GNSS/IMU connected | Repeated cold boot and download test | [TBD] | [TBD] | [TBD] |
| GNSS UART inter-device alignment | Common-event comparison between two units | [TBD] | [TBD] | [TBD] |
