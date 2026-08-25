# Version 2 Product Requirements

## Document information

- Product version: V2
- Last updated: 2026-08-25
- Author: Benjamin Girard

## 1. Initial product scope

### Included in the first milestone

- Magnetic analog card
- Three synchronized magnetic axes
- One differential thermistor
- AD7779 acquisition
- Configurable sample rate
- SD recording
- UART to USB (via bridge) host communication
- On-demand magnetic SET/RESET control

### Second milestone

- Geophysical accelerometer card
- Bluetooth and PC interface

## 2. ADC acquisition requirements

- ADC: AD7779
- Resolution: 24-bit signed
- Default output data rate: 1kSps per channel
- Maximum required configurable rate: 16kSps per channel
- Default active channels:
  - Channel 0: X1
  - Channel 1: Y1
  - Channel 2: Z1
  - Channel 3: Differential thermistor 1
  - Channel 4: X2
  - Channel 5: Y2
  - Channel 6: Z2
  - Channel 7: Differential thermistor 2
- Unused channels stored: No
- Per-channel gain configurable: Yes
- Supported gains: ×1 / ×2 / ×4 / ×8
- Data-loss policy: Never silently discard a frame
- Overrun reporting: Required
- CRC/header validation: Required

## 3. Analog bandwidth
- RF filter: 18.7kHz
- Servo / bandwidth limiter: RC=14.1s
- AAF: 362 Hz
- Filter order: First order
- Primary signal frequency: approximately 2 Hz
- Square-wave harmonics important: Yes
- Aliasing validation required at 1 kSPS: Yes

## 4. Synchronization and timing

- X, Y, and Z must come from the same AD7779 conversion frame.
- One timestamp represents the complete ADC frame.
- Absolute GNSS time accuracy: Time pulse is unavailable for the V1 rev 1, we will do the best we can with UART and the GNSS
- Behavior when GNSS time is unavailable: [TBD]

## 5. Magnetic SET/RESET requirements

- SET operation: On demand; NEVER BOTH SET AND RESET AT THE SAME TIME
- RESET operation: On demand; NEVER BOTH SET AND RESET AT THE SAME TIME this would short a 220 ohms with +18V and GND.
- Combined SET-then-RESET diagnostic sequence: Yes
- Pulse width of the SET AND RESET: few decades µs to a hundred of us (the set reset pulse through the strap should be 2-3us)
- Minimum interval between pulses: No requirement determined in the V1
- Maximum number of consecutive pulses: No requirement determined in the V1
- Required dead time between SET and RESET: The pulses are so short we dont need to worry about that, worst case it shows a pulse on the data, we will deal with that later.
- Post-pulse settling time: Same as above
- Samples during pulse marked invalid/transient: Same as above
- Samples during settling marked invalid/transient: Same as above
- Pulse event timestamp recorded: Yes
- Pulse output safe state: Inactive

## 6. Recording requirements

- Raw ADC storage representation: Packed signed 24-bit
- Default stored channels: Selectable by user via the host app
- Thermistor stored at full rate during validation: No, selectable with the user host app, default every 10 seconds, also we should add the temperature of the ADC and the IMU and ESP32 if it has a temperature sensor.
- Long-term thermistor rate: every 10s
- Expected continuous recording duration: 15 min to 8h,
- Expected SD-card capacity: TBD, the current SD cards are 8GB
- File segmentation rule:
  - Maximum duration: [TBD]
  - Maximum size: [TBD]
- File naming convention: [TBD]
- Filesystem: [TBD]
- Required metadata:
  - Firmware version
  - Hardware revision
  - Card type and revision
  - Active-channel mask
  - Requested and actual sample rate
  - ADC gain per channel
  - ADC reference configuration
  - Start time
  - GNSS validity
  - Calibration identifier
- Power-loss recovery requirement: [TBD]
- Recording resume requirement: [TBD]

## 7. SD and USB behavior

- ESP32 and USB2641 may never own the SD card simultaneously.
- USB export while recording: TBD
- Required action before USB ownership: There is no USB owenership in V2 rev1.
- Behavior when USB disconnects: [TBD]
- Behavior when SD card is removed: [TBD]
- Behavior when SD card is full: [TBD]

## 8. Host-application compatibility

- Existing reference:
  `magnetometer-node/.../tools/serial_scope.py`
- Reuse:
  - Serial-port selection
  - Live plotting
  - FFT
  - ADC gain controls
  - ADC-rate control
  - SET/RESET controls
- V1 binary-protocol compatibility required: No
- V2 protocol decoder required: Yes
- Protocol capability/version handshake required: No
- Captured V1 regression packets available: No
- Host operating systems: MacOS 15 default, it needs to be portable to windows later

## 9. Fault behavior

We dont need fault behavior now, we should just output the relevent error so we can see it.

| Fault | Required response | User-visible indication | Automatic recovery |
|---|---|---|---|
| ADC not detected | [TBD] | [TBD] | [TBD] |
| ADC CRC error | [TBD] | [TBD] | [TBD] |
| ADC overrun | [TBD] | [TBD] | [TBD] |
| Magnetic card removed | [TBD] | [TBD] | [TBD] |
| SD card missing | [TBD] | [TBD] | [TBD] |
| SD card full | [TBD] | [TBD] | [TBD] |
| SD write failure | [TBD] | [TBD] | [TBD] |
| GNSS unavailable | [TBD] | [TBD] | [TBD] |
| Brownout/reset | [TBD] | [TBD] | [TBD] |

## 10. First-milestone acceptance criteria

- [ ] Firmware boots into safe hardware state.
- [ ] Magnetic card is detected.
- [ ] AD7779 initializes successfully.
- [ ] X/Y/Z and thermistor acquire at 1 kSPS.
- [ ] No unexplained dropped frames during [TBD] hours.
- [ ] Sample-rate changes are validated and reported.
- [ ] Manual SET and RESET operations work safely.
- [ ] Pulse-affected samples are identified.
- [ ] Packed 24-bit data can be recorded and decoded.
- [ ] V2 data can be displayed in the reused host application.
