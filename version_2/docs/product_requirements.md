# Version 2 Product Requirements

## Document information

- Product version: V2
- Status: Frozen for milestone 1
- Last updated: 2026-08-25
- Author: Benjamin Girard

## 1. Product scope and milestones

### Milestone 1: laboratory acquisition and validation

- Support both magnetic analog-card slots.
- Acquire three synchronized magnetic axes and one differential thermistor from each card.
- Acquire all eight AD7779 channels by default.
- Provide configurable ADC sample rate and per-channel gain.
- Stream live data to a PC through the ESP32 DevKit UART-to-USB connection.
- Reuse and adapt the V1 host application for visualization, validation, and host-side capture.
- Provide on-demand magnetic SET and RESET control for either card.
- Report protocol, acquisition, and hardware errors to the host.
- Boot and return to a safe hardware state.

Milestone 1 does not require SD recording, GNSS synchronization, IMU acquisition, Bluetooth, or
USB mass-storage access.

### Milestone 2: field recording and contextual sensors

- SD-card recording.
- MAX-M10S GNSS acquisition and inter-device time alignment.
- LSM6DSV IMU acquisition for orientation and movement detection.
- Geophysical accelerometer analog card.
- Bluetooth, if it remains useful after the wired workflow is validated.
- Power-loss recovery and field-oriented fault handling.

### Explicitly unsupported in V2 Rev-1 firmware

- The USB2641 and USB connector will not provide SD-card mass-storage access.
- Firmware will not switch SD-card ownership between the ESP32 and USB2641.
- The SD mux will remain fixed to the ESP32 side. The USB2641 will remain reset/isolated.
- GNSS TIMEPULSE/PPS is not connected. A dedicated non-strapping GPIO is a future board-revision
  improvement.

## 2. ADC acquisition requirements

- ADC: AD7779.
- Resolution: signed 24-bit.
- Default output data rate: 1 kSPS per channel.
- Supported high-resolution output-rate presets: approximately 500 SPS, 1 kSPS, 2 kSPS, 4 kSPS,
  8 kSPS, and 16 kSPS per channel. Other rates and low-power mode are unsupported.
- Default active-channel mask: all eight channels.
- Channel assignment:
  - Channel 0: magnetic card 1, X axis.
  - Channel 1: magnetic card 1, Y axis.
  - Channel 2: magnetic card 1, Z axis.
  - Channel 3: magnetic card 1, differential thermistor.
  - Channel 4: magnetic card 2, X axis.
  - Channel 5: magnetic card 2, Y axis.
  - Channel 6: magnetic card 2, Z axis.
  - Channel 7: magnetic card 2, differential thermistor.
- All enabled channels from one AD7779 conversion form one indivisible sample frame.
- Unused or disabled channels are not transmitted or recorded.
- Per-channel gain is configurable.
- Supported gains: ×1, ×2, ×4, and ×8.
- Every frame carries a monotonically increasing sequence number.
- A frame may be rejected or marked invalid, but it must never disappear silently.
- ADC overrun and queue overflow counters are required.
- AD7779 status/header and CRC validation are required when enabled by the selected interface mode.
- The selected applied sample-rate preset must be reported.

The 16 kSPS requirement applies to ADC acquisition capability. The wired host interface is only
required to stream all eight raw 24-bit channels continuously at the default 1 kSPS rate. At higher
rates, the firmware may limit the streamed channel mask or send a decimated preview, but it must
report that behavior explicitly and must continue to detect acquisition overruns.

## 3. Analog bandwidth

- Input RF-filter cutoff: approximately 18.7 kHz.
- Servo time constant: approximately 14.1 s; its exact transfer function must be documented with
  the magnetic-card design.
- Anti-alias/filter cutoff: approximately 362 Hz.
- Anti-alias/filter order: first order.
- Primary signal frequency: approximately 2 Hz.
- Square-wave harmonics are scientifically relevant.
- Aliasing behavior at the default 1 kSPS rate must be measured during validation.

## 4. Synchronization and timestamps

### Milestone 1

- All eight channels in an ADC frame share one monotonic timestamp.
- X, Y, Z, and thermistor data from both magnetic cards come from the same simultaneous AD7779
  conversion frame.
- The timestamp represents the ADC conversion frame, not the time at which the host receives it.
- Absolute UTC time is not required for laboratory validation.

### Milestone 2

- MAX-M10S UART messages provide UTC and GNSS validity information.
- Rev-1 targets approximately 5 ms or better inter-device alignment using identical GNSS and UART
  configuration; this must be verified between two complete devices.
- Sub-millisecond absolute synchronization is not guaranteed without TIMEPULSE/PPS.
- When GNSS time is unavailable, acquisition continues using monotonic timestamps, absolute time
  is marked invalid, and a GNSS-loss event is recorded.
- When GNSS becomes valid again, the monotonic-to-UTC mapping is restored without changing or
  discontinuously rewriting existing sample timestamps.

## 5. Magnetic SET/RESET requirements

- SET and RESET operations are initiated only by an explicit user or diagnostic command.
- SET and RESET are never periodic background operations.
- SET and RESET for the same card must never be active simultaneously.
- The board API and magnetic-card driver both enforce mutual exclusion.
- Card 1 and card 2 can be addressed independently.
- A combined SET-then-RESET diagnostic sequence is supported only as an explicit on-demand
  command.
- Firmware control-pulse duration is a configurable board parameter initially constrained to the
  tens-of-microseconds to 100 µs range.
- The resulting current pulse through the sensor strap targets approximately 2–3 µs and must be
  verified with an oscilloscope before the operation is considered validated.
- A nonzero dead time is mandatory between SET and RESET. Use a conservative 100 ms default
  until bench measurements establish a lower safe value.
- Maximum consecutive operations and pulse-rail recharge time remain bench-derived limits. Until
  validated, firmware permits only one outstanding pulse operation at a time.
- Samples acquired during the firmware pulse, sensor-strap pulse, and post-pulse settling interval
  remain timestamped but are marked transient/invalid for scientific use.
- Post-pulse settling is configurable; use a conservative 10 ms initial value until measured.
- Every request records card slot, operation, command timestamp, actual pulse timestamp, result,
  and affected sample-sequence range.
- Pulse outputs remain inactive during boot, shutdown, unknown-card state, and fault handling.

## 6. Host streaming and application requirements

- Milestone 1 transport: ESP32 DevKit UART-to-USB connection.
- Default link baud rate: 921600 baud, subject to bench reliability.
- Required continuous stream: all eight packed 24-bit channels at 1 kSPS, plus framing and status.
- The host can select an alternate channel mask and supported ADC rate.
- When the requested stream exceeds transport capacity, firmware rejects it or negotiates an
  explicit reduced channel/preview configuration; it never silently drops data to fit the link.
- The host application may save received laboratory data to the PC.
- Host-side data capture includes frame sequence numbers, timestamps, validity flags, and metadata.

Existing reference:

`magnetometer-node/firmware/esp32/ESP32-S3-WROOM-1/Mainboard/geophys-daq/tools/serial_scope.py`

Behavior to reuse:

- Serial-port selection and connection state.
- Live plotting for all magnetic axes and thermistors.
- FFT and signal statistics.
- ADC gain and rate controls.
- Channel selection.
- SET and RESET controls for either card.
- Host-side recording and gap indication.

Compatibility requirements:

- V1 binary-protocol compatibility is not required.
- A V2 protocol decoder is required.
- A protocol-version and capability handshake is required.
- The handshake reports firmware version, protocol version, hardware revision, detected cards,
  available channels, supported gains/rates, and enabled features.
- Initial supported host OS: macOS 15.
- Windows portability is a milestone-2 goal; platform-specific logic should remain isolated.

## 7. Data representation and metadata

- Raw ADC values are represented on persistent or bandwidth-sensitive interfaces as packed signed
  24-bit samples.
- Byte order is fixed by the V2 protocol specification.
- The ESP32 may sign-extend samples to `int32_t` internally.
- Default transmitted and, in milestone 2, recorded channel mask: all eight channels.
- Thermistor channels are acquired and transmitted at the same rate as their corresponding magnetic
  axes by default.
- AD7779, IMU, and ESP32 internal temperature readings are separate low-rate housekeeping records,
  nominally produced every 10 seconds when their source is available.
- Required metadata:
  - Firmware and protocol version.
  - Hardware revision.
  - Card type and revision for each slot.
  - Active-channel mask and channel-to-sensor mapping.
  - Requested and actual sample rate.
  - ADC gain per channel.
  - ADC reference configuration.
  - Start monotonic timestamp.
  - UTC start time and GNSS validity when available.
  - Calibration identifier.
  - Reset reason and error counters.

## 8. Milestone-2 SD recording requirements

- Default recorded channels: all eight ADC channels.
- Expected continuous recording duration: 15 minutes to 8 hours at the default 1 kSPS rate.
- Current development SD-card capacity: 8 GB.
- The required eight-channel 1 kSPS packed payload is approximately 86.4 MB/hour before framing
  and metadata, and therefore fits comfortably for eight hours on an 8 GB card.
- Eight channels at 16 kSPS require approximately 1.38 GB/hour before overhead and are not required
  to fit for eight hours on an 8 GB card.
- Filesystem: FAT32.
- Segmentation: create a new file after one hour or 1 GiB, whichever occurs first.
- File names use UTC start time when valid, plus device identifier and sequence number. Before UTC
  is available, use boot identifier and sequence number.
- On orderly shutdown, queued data is flushed, the file is closed, and the filesystem is unmounted.
- Following power loss, completed segments remain readable. On the next boot, firmware detects and
  reports an incomplete final segment and applies a documented recovery/truncation procedure.
- Recording resumes in a new segment rather than appending to a potentially damaged segment.
- If the SD card is missing, removed, full, or fails, acquisition may continue for host streaming but
  recording stops and an error event is reported.

## 9. SD mux and USB2641 policy

- The SD mux is configured once for ESP32 ownership and is not switched at runtime.
- The USB2641 remains reset/isolated and never owns the SD card.
- USB mass-storage export is unsupported on V2 Rev-1.
- USB serial host communication through the ESP32 DevKit remains supported and is independent of
  the USB2641/SD path.
- Firmware does not implement ESP32-to-USB2641 or USB2641-to-ESP32 transition state machines.

## 10. Fault and error behavior

All errors have a stable error code, a timestamp, a severity, and relevant counters or context. Errors
are emitted through the V2 protocol when the host is connected and are included in milestone-2
recordings.

| Fault | Required response | User-visible indication | Automatic recovery |
|---|---|---|---|
| ADC not detected or initialization fails | Keep acquisition stopped and pulse outputs inactive | Error event and status | Retry only by explicit reinitialize command or reboot |
| ADC CRC/header error | Mark affected frame invalid; increment counter | Frame flag and error counter | Continue after isolated errors; reinitialize the ADC if frame synchronization is lost |
| ADC or queue overrun | Preserve sequence discontinuity; increment dropped-frame count | Gap event and counter | Continue if buffers recover; otherwise stop stream cleanly |
| Magnetic card missing or removed | Disable that slot's SET/RESET controls; invalidate its channel mapping | Card-state event | Re-detect only while acquisition is stopped |
| Unsupported stream request | Reject without altering current configuration | Command error containing supported limits | Host may submit a supported request |
| Host disconnect | Stop acquisition cleanly and clear any pending pulse command | Disconnect event when detectable; reconnect status includes prior counters | Reconnect and restart without reboot when safe |
| SD missing/full/write failure (milestone 2) | Stop recording, close/unmount when possible, continue acquisition if possible | Error event | Explicit user action required |
| GNSS unavailable (milestone 2) | Continue monotonic acquisition; absolute time invalid | GNSS validity event | Restore mapping when valid data returns |
| Brownout or unexpected reset | Hardware returns to safe outputs; previous reset reason retained | Startup status event | Normal safe initialization |

Errors are never reported by silently corrupting, reordering, or omitting the ADC frame sequence.

## 11. Milestone-1 acceptance criteria

- [ ] Firmware boots with power rails, pulse controls, and shared outputs in their documented safe
      states.
- [ ] Each magnetic card is independently detected in either slot.
- [ ] Both magnetic cards can be present and operated simultaneously.
- [ ] AD7779 initializes and reports its configuration successfully.
- [ ] All eight channels acquire synchronously at the default 1 kSPS rate.
- [ ] Each frame has one monotonic timestamp, sequence number, validity flags, and AD7779 status.
- [ ] The ADC can be configured for each supported preset through 16 kSPS and reports the selected
      applied preset.
- [ ] All eight channels stream continuously to the host at 1 kSPS for eight hours with no unexplained
      gaps or silent frame loss.
- [ ] Intentional errors, CRC failures, and overruns are visible through counters or protocol events.
- [ ] Manual SET and RESET work independently for either card and can never overlap.
- [ ] Sensor-strap pulse width is measured at approximately 2–3 µs.
- [ ] Pulse-affected and settling samples are identified by their frame-sequence range.
- [ ] The V2 handshake and data stream are decoded by the reused host application.
- [ ] The host displays both cards, reports gaps, changes gain/rate, and saves laboratory captures.
