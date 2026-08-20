# Version 2 Product Requirements

## Document information

- Status: Draft
- Product version: V2
- Primary use case: Magnetic-field acquisition
- Last updated: [YYYY-MM-DD]
- Author: [NAME]

## 1. Initial product scope

### Included in the first milestone

- Magnetic analog card
- Three synchronized magnetic axes
- One differential thermistor
- AD7779 acquisition
- Configurable sample rate
- SD recording
- UART/USB host communication
- On-demand magnetic SET/RESET control
- Reuse of validated V1 host-application behavior

### Deferred

- Geophysical accelerometer card
- Advanced signal processing
- Bluetooth: [DEFERRED / REQUIRED]
- Multiple simultaneous analog cards: [TBD]
- Other: [TBD]

## 2. ADC acquisition requirements

- ADC: AD7779
- Resolution: 24-bit signed
- Default output data rate: 1000 samples/s/channel
- Minimum required configurable rate: [TBD] samples/s/channel
- Maximum required configurable rate: [TBD] samples/s/channel
- Requested rates must report the actual applied rate: Yes
- Default active channels:
  - Channel [TBD]: Magnetic X
  - Channel [TBD]: Magnetic Y
  - Channel [TBD]: Magnetic Z
  - Channel [TBD]: Differential thermistor
- Unused channels stored: [YES / NO]
- Per-channel gain configurable: [YES / NO]
- Supported gains: [×1 / ×2 / ×4 / ×8]
- Data-loss policy: Never silently discard a frame
- Overrun reporting: Required
- CRC/header validation: Required

## 3. Analog bandwidth

- Expected analog cutoff: approximately 362 Hz
- Filter order: First order
- Primary signal frequency: approximately 2 Hz
- Square-wave harmonics important: [YES / NO / TBD]
- Aliasing validation required at 1 kSPS: Yes
- Measured cutoff frequency: [TBD]
- Validated ADC rates: [TBD]
- Additional filtering required: [TBD]

## 4. Synchronization and timing

- X, Y, and Z must come from the same AD7779 conversion frame.
- One timestamp represents the complete ADC frame.
- Nominal sample period at 1 kSPS: 1 ms
- Maximum acceptable inter-axis skew: Hardware simultaneous sampling
- Maximum pulse-to-sample timestamp uncertainty: [TBD, initial target 100 µs]
- Monotonic timestamp resolution: [TBD]
- Absolute GNSS time accuracy: [TBD]
- Behavior when GNSS time is unavailable: [TBD]

## 5. Magnetic SET/RESET requirements

- SET operation: On demand
- RESET operation: On demand
- Automatic periodic operation: Not required
- Combined SET-then-RESET diagnostic sequence: [YES / NO]
- Pulse width: [TBD µs/ms]
- Minimum interval between pulses: [TBD]
- Maximum number of consecutive pulses: [TBD]
- Required dead time between SET and RESET: [TBD]
- Post-pulse settling time: [TBD]
- Samples during pulse marked invalid/transient: [YES / NO]
- Samples during settling marked invalid/transient: [YES / NO]
- Pulse event timestamp recorded: Yes
- Pulse output safe state: Inactive

## 6. Recording requirements

- Raw ADC storage representation: Packed signed 24-bit
- Default stored channels: [TBD]
- Thermistor stored at full rate during validation: Yes
- Long-term thermistor rate: [TBD]
- Expected continuous recording duration: [TBD hours/days]
- Expected SD-card capacity: [TBD]
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
- USB export while recording: [PROHIBITED / PAUSE RECORDING / TBD]
- Required action before USB ownership:
  - Stop new writes
  - Flush buffers
  - Close recording
  - Unmount filesystem
  - Place SDMMC pins safely
  - Transfer mux ownership
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
  - Statistics
  - CSV export
  - Frame-gap detection
  - ADC gain controls
  - ADC-rate control
  - SET/RESET controls
- V1 binary-protocol compatibility required: [YES / TEMPORARY / NO]
- V2 protocol decoder required: Yes
- Protocol capability/version handshake required: [YES / NO]
- Captured V1 regression packets available: [TBD]
- Host operating systems: [TBD]

## 9. Fault behavior

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