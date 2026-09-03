# Version 2 Firmware Interface Contract

## Document information

- Status: Frozen for milestone 1
- Product version: V2
- Initial target: two magnetic cards, eight synchronized AD7779 channels
- Last updated: 2026-08-27

## 1. Purpose and milestone-1 scope

This document defines ownership and interface rules between firmware modules. It is intentionally
small: an interface exists only when it prevents duplicated hardware access, hidden coupling, or
ambiguous data.

Milestone 1 runs:

- `task_acquisition`
- `task_communication`
- Rev-1 `board`
- AD7779, 74HC595, card-detection, and magnetic-card modules
- UART transport and V2 protocol

Milestone 1 does not create storage, GNSS, IMU, processing, Bluetooth, or USB-mass-storage tasks.
Their scaffolds may compile, but they do not initialize hardware or allocate runtime resources.

## 2. Dependency and access rules

- Component drivers contain no ESP-IDF, FreeRTOS, board GPIO, application, or protocol types.
- The ESP32 platform layer implements generic SPI, I²C, UART, GPIO, interrupt, and monotonic-time
  mechanisms.
- `boards/rev_1` contains all Rev-1 wiring, active levels, safe states, power sequencing, shift-register
  mapping, and component instances.
- Analog-card modules describe a card as a product component but never manipulate raw GPIO numbers
  or shift-register bits.
- `analog_cards/magnetic` owns the magnetic SET/RESET pulse-generation sequence and its card-specific
  timing. It requests 18 V rail and logical SET/RESET output changes through callbacks supplied by
  `boards/rev_1`.
- `analog_cards/acc_geoph` does not implement or expose magnetic SET/RESET operations.
- Application tasks use board, card, and protocol interfaces. They do not call portable IC drivers
  directly.
- Protocol and transport code contain no product hardware control.
- Public headers expose opaque handles where internal state would otherwise leak across layers.
- A stateful hardware resource has exactly one writer. Other modules send requests to its owner.
- No application module writes an ESP32 GPIO or a 74HC595 image directly.

## 3. Common status and error model

Portable modules return one status category:

| Status | Meaning | Typical handling |
|---|---|---|
| `OK` | Operation completed | Continue |
| `INVALID_ARGUMENT` | Input is outside the interface contract | Reject request |
| `INVALID_STATE` | Operation is not allowed in the current state | Reject request |
| `NOT_INITIALIZED` | Required initialization has not completed | Stop operation |
| `NOT_FOUND` | Expected device or card is absent | Mark unavailable |
| `BUSY` | Owner is processing an incompatible request | Retry or reject |
| `TIMEOUT` | Required event did not occur before its deadline | Count and recover/stop |
| `IO` | Bus or peripheral operation failed | Add resource context |
| `INTEGRITY` | CRC, header, or framing validation failed | Mark data invalid and count |
| `OVERFLOW` | Queue, pool, or device overrun caused data loss | Preserve a visible gap |
| `UNSUPPORTED` | Feature or configuration is unavailable | Return supported capabilities |
| `HARDWARE_FAULT` | Board or component is unsafe/unusable | Enter the defined safe response |
| `INTERNAL` | Firmware invariant was violated | Report and enter a safe state |

Rules:

- Portable drivers never expose `esp_err_t`.
- Platform adapters translate ESP-IDF results into the common status model and may return an
  optional `fw_error_context_t` containing the normalized status, resource category, operation,
  resource instance, and portable operation-specific detail.
- Passing a null error-context output requests status-only handling. A provided context is cleared
  on success, and its `detail` field never contains a raw ESP-IDF error value.
- Board/card layers preserve useful lower-layer context or replace it with the product resource and
  operation at their own boundary.
- Public protocol errors use stable V2 error codes, not ESP-IDF or private driver numbers.
- The per-call error context is not itself an error event. A reported error event additionally
  contains source, severity, monotonic timestamp, and occurrence count.
- Repeated errors may be coalesced, but their count is never lost.
- ADC data loss is represented by sequence discontinuity and counters, never only by a text log.

## 4. Platform service contracts

Task-context platform and transport callbacks carry an opaque resource pointer and may accept an
optional error-context output. Unless explicitly named ISR-safe, callbacks are called only from task
context.

### SPI

- One synchronous, DMA-backed full-duplex transfer operation is sufficient for portable drivers.
- Synchronous means only the calling task waits; the scheduler, interrupts, and other SPI buses
  continue to run. Continuous acquisition does not use CPU busy-polling.
- The context binds a configured SPI device, including bus, chip select, mode, and clock.
- A transfer is atomic relative to other devices on the same bus.
- The caller supplies transmit and/or receive buffers, byte length, and timeout.
- Null TX produces filler bytes; null RX discards received bytes.
- The platform validates DMA suitability or copies through an internal DMA-safe buffer.
- DMA buffers are allocated before acquisition and never allocated during steady-state transfers.
- SPI clock changes are applied only while the owning device is stopped. The board supplies the
  initial and maximum clocks, and the platform reports the achieved hardware clock.
- No SPI transfer occurs in a GPIO ISR.
- AD7779 and LSM6DSV clock limits come from `board_config.h`, not from application code.

### I²C

- Register read, register write, and combined raw transaction operations are reserved for milestone 2.
- The context binds the controller, address, bus speed, and timeout.
- Automatic retry is disabled in the generic callback; the owning driver decides whether retry is
  safe for a specific operation.

### UART and transport

- UART byte movement belongs to `transport_uart`; protocol parsing belongs to
  `task_communication`.
- Reads and writes may block `task_communication` up to a configured timeout but can never block
  `task_acquisition`.
- Partial reads and writes are normal and are handled by the transport/protocol boundary.
- A read waits for its first byte, then returns the bytes already available without waiting to fill
  the caller's buffer. A write returns the number of bytes accepted by the UART and the caller
  continues from that offset.
- The byte count remains valid when an operation times out, so progress is never ambiguous.
- Baud rate is supplied during initialization and the platform reports the actual applied rate.
  Changing rates requires stopping and reinitializing the transport; live switching is not part of
  milestone 1.
- Debug text is never inserted into the binary protocol stream.
- Milestone-1 target is 921600 baud, subject to the board reliability test.

### GPIO and interrupts

- Task-context operations configure a pin, set an output, and read an input.
- Only explicitly declared `_isr` operations may be called from an ISR.
- The `ADC_DRDY` ISR captures monotonic time, places it in a fixed ISR-safe timestamp ring, and
  notifies `task_acquisition`.
- The ISR does not perform SPI transfers, allocate memory, take a blocking lock, or log text.
- Timestamp-ring overflow increments an ISR-safe counter that becomes a visible acquisition gap.

### Time

- The application time type is an unsigned 64-bit count of microseconds since boot.
- Monotonic time never moves backward and does not represent UTC.
- The `ADC_DRDY` falling edge is the milestone-1 timestamp reference for an ADC frame.
- Delay operations are for initialization and short hardware sequencing only; application tasks use
  notifications/queues rather than polling delays.
- UTC mapping is a separate milestone-2 service and never changes stored monotonic timestamps.

## 5. Board-level interface

`boards/rev_1/board.h` is the only entry to Rev-1 hardware. It exposes product operations and the
callbacks bound to card modules without exposing GPIO numbers, active levels, shift-register
positions, or ESP-IDF handles.

Required milestone-1 operations:

| Operation | Caller | Behavior |
|---|---|---|
| Initialize board | `app_main` | Establish direct GPIO states, disable shift outputs, latch safe image, then enable outputs |
| Enter safe state | Initialization failure or fatal path | Stop acquisition, disable pulses and optional rails, preserve debug communication if safe |
| Read board status | Application/communication | Return card presence, rail state, ADC state, and sticky faults |
| Detect cards | Initialization or stopped acquisition | Sample both analog ID inputs and return slot/type/confidence |
| Initialize/configure ADC | `task_acquisition` | Apply channel mask, gains, sample rate, CRC/header policy |
| Start/stop ADC | `task_acquisition` | Perform the required board and AD7779 sequence |
| Read one ADC frame | `task_acquisition` | Return one simultaneous frame and validation status |
| Set 18 V pulse-rail state | Magnetic-card module through a bound callback | Enable or disable the Rev-1 mainboard rail without exposing SH1_D |
| Set one logical card pulse output | Magnetic-card module through a bound callback | Safely drive the selected slot's SET or RESET output without exposing SH2 positions |
| Read/update safe shift image | `board` only | Maintain one 16-bit shadow and prevent unrelated-bit overwrite |

Board rules:

- `board_init()` is synchronous and leaves hardware safe on every failure path.
- Safe-state and stop operations are idempotent.
- Pulse outputs are inactive until a known magnetic card is present.
- SET and RESET can never be asserted simultaneously, including during failures.
- Only `board` calls the 74HC595 driver.
- The SD mux is initialized once toward the ESP32 and is never switched at runtime.
- The USB2641 remains reset/isolated; there is no USB2641 ownership API.

### Magnetic-card pulse interface

`analog_cards/magnetic/card_magnetic.*` implements the magnetic-card pulse operation. It owns the
SET/RESET sequence, pulse width, inter-pulse dead time, and post-pulse settling requirement. The
module uses abstract callbacks for the monotonic clock/delay, the mainboard 18 V rail, and the
selected slot's logical SET/RESET outputs.

The Rev-1 board implements the rail and output callbacks because the 18 V converter, 74HC595 chain,
and output-to-slot mapping are on the mainboard. The magnetic module must not contain `EN_BST_18V`,
SH1/SH2 positions, ESP32 GPIO numbers, or 74HC595 calls.

`task_acquisition` accepts and serializes pulse commands, invokes the magnetic-card operation, and
marks the affected ADC frames transient. It does not implement the electrical pulse waveform. The
accelerometer-card module has no corresponding SET/RESET operation.

## 6. Driver lifecycle

Every stateful component follows:

1. Zero/create instance.
2. Bind configuration and portable callbacks.
3. Initialize communication state.
4. Reset hardware when required.
5. Verify identity/status.
6. Apply configuration and read back critical fields.
7. Enter ready state.
8. Start operation.
9. Stop operation.
10. Deinitialize into a safe state.

Lifecycle rules:

- Configuration is copied into driver-owned state; callers do not retain mutable configuration
  pointers.
- `start` is valid only from ready/stopped state.
- `stop` and safe-state operations may be called more than once.
- Drivers never create tasks or queues.
- Drivers never allocate memory during streaming.
- Register definitions remain private to the component driver.
- Calibration and physical-unit conversion do not occur inside the AD7779 driver.

## 7. Application data contracts

### ADC frame

The in-memory frame contains:

| Field | Type/meaning |
|---|---|
| `sequence` | Unsigned 64-bit conversion-attempt counter |
| `timestamp_us` | Unsigned 64-bit monotonic timestamp captured from `ADC_DRDY` |
| `channel_mask` | Eight-bit mask of configured channels |
| `valid_mask` | Eight-bit mask of scientifically valid channel samples |
| `samples[8]` | Signed 32-bit values containing sign-extended 24-bit ADC codes |
| `adc_status` | Normalized AD7779 header/CRC/overrun flags |
| `sample_flags` | Normal, pulse-active, settling, configuration-change, or invalid |
| `dropped_before` | Number of conversion frames missing before this frame |

Rules:

- One frame represents one simultaneous AD7779 conversion.
- All eight sample positions remain stable in RAM; masks determine which values are meaningful.
- The AD7779 driver always validates the channel ID in each conversion header. In status-header
  mode it normalizes reset, modulator/filter saturation, analog-input, and device-alert indications;
  in CRC-header mode it validates the four even/odd channel-pair CRC values.
- Channel-ID or ADC data-CRC failures return `INTEGRITY`. Valid status indications and unresolved
  device alerts return `HARDWARE_FAULT`, with normalized fault flags and an affected-channel mask;
  the acquisition owner decides whether to invalidate, count, or stop after reading full device
  status when needed.
- Sequence increments for every expected conversion, including invalid or dropped conversions.
- No floating-point work occurs in `task_acquisition`.
- Conversion to volts, temperature, or calibrated magnetic units occurs on the host in milestone 1.

### ADC block

- Frames move between tasks in fixed-capacity blocks, not one queue item per sample.
- Initial block size: 32 ADC frames.
- A block carries frame count, channel mask, first sequence, and the individual frame metadata needed
  to preserve gaps and transient flags.
- The protocol encoder packs each selected sample into exactly three little-endian two's-complement
  bytes.

### Pulse request and result

| Field | Meaning |
|---|---|
| Request identifier | Correlates command and response |
| Card slot | Slot 1 or slot 2 |
| Operation | SET, RESET, or on-demand SET-then-RESET diagnostic |
| Requested timestamp | Time command was accepted |
| Configured control width | Magnetic-card command-pulse duration |
| Actual timestamp | Time the pulse operation began |
| Settling end | First time normal samples may be valid |
| First/last affected sequence | Inclusive transient frame range |
| Result | Common status and hardware detail |

## 8. Task and resource ownership

| Resource | Sole owner | Other users | Access mechanism |
|---|---|---|---|
| AD7779 configuration and streaming | `task_acquisition` | Communication/status | Acquisition command queue and status snapshot |
| ADC frame/block pool | `task_acquisition` | `task_communication` | Free-block and ready-block queues |
| Pulse-command serialization and ADC validity window | `task_acquisition` | Communication | Acquisition command queue |
| Magnetic SET/RESET sequence and card-specific timing | `analog_cards/magnetic` | `task_acquisition` | Magnetic-card operation interface and bound callbacks |
| Rev-1 GPIO, rails, and shift-register image | `board` module | Acquisition/application | Board API |
| UART transport and protocol parser | `task_communication` | Acquisition/application | Queues and immutable snapshots |
| Card-detection state | `board` / `card_detect` | Acquisition/communication | Board status snapshot |
| Application lifecycle | `app` | Both tasks | Startup calls and control events |

Milestone-1 task rules:

- `task_acquisition` has higher priority than `task_communication`.
- `task_acquisition` blocks on DRDY notification or its command queue, never on UART transmission.
- `task_communication` may block on UART and returns consumed ADC blocks to the free pool.
- Gain, sample-rate, and channel-mask changes are accepted only while acquisition is stopped.
- Pulse commands may execute during acquisition so affected frames can be marked.
- Only one acquisition reconfiguration or pulse command is active at a time.
- `task_processing`, `task_storage`, `task_bluetooth`, `task_gnss`, and `task_imu` are not created in
  milestone 1.

## 9. Queue and buffer contracts

Initial milestone-1 sizing:

| Queue/pool | Producer | Consumer | Capacity | Full/empty behavior |
|---|---|---|---:|---|
| DRDY timestamp ring | `ADC_DRDY` ISR | Acquisition | 64 timestamps | Increment overflow count; preserve visible sequence gap |
| Free ADC block pool | Communication returns blocks | Acquisition | 8 blocks × 32 frames | If empty, count frames until a block returns and set `dropped_before` |
| Ready ADC blocks | Acquisition | Communication | 8 block references | Never block acquisition; preserve visible overflow count |
| Acquisition commands | Communication | Acquisition | 8 requests | Reject new command as `BUSY` when full |
| Command results | Acquisition | Communication | 8 results | Communication reserves a result slot before accepting a command |
| Status/error events | All owners | Communication | 32 events | Coalesce repeats into sticky code + count; never lose critical state |

Buffer rules:

- Buffers are allocated before acquisition starts.
- No heap allocation occurs in DRDY handling or steady-state acquisition.
- Ownership transfers only through the free/ready queues.
- A producer does not access a block after queueing it.
- A consumer returns every accepted block exactly once.
- An acquisition command is not queued unless capacity for its result has already been reserved.
- Queue sizing is validated by the eight-hour 1 kSPS host-stream test.
- At rates that exceed the negotiated transport configuration, raw frames may be intentionally
  excluded by the explicit preview policy; this is not reported as accidental data loss.

## 10. V2 protocol contract

The authoritative byte layouts and test vectors belong in:

- `shared/protocol/protocol_frame.md`
- `shared/protocol/protocol_types.md`
- `shared/protocol/test_vectors/`

Interface-level decisions:

- Binary framing only; debug logs use a different console/path.
- Fixed byte order: little-endian.
- Frame integrity: CRC-32C.
- Maximum payload: 2048 bytes.
- Parser accepts partial frames, multiple frames per read, corrupt bytes, and resynchronization.
- Every protocol frame contains magic, protocol version, message type, flags, payload length,
  message sequence, monotonic timestamp when relevant, payload, and CRC.
- Every host request contains a request identifier and receives exactly one response or a connection
  reset.
- Unknown message types return `UNSUPPORTED`.
- Malformed messages return a protocol error without changing device configuration.
- Configuration changes are validated completely before any state is modified.
- V1 frames are neither emitted nor accepted.

### Required milestone-1 messages

| Message | Direction | Purpose |
|---|---|---|
| `HELLO/DEVICE_INFO` | Both | Negotiate protocol and report firmware/hardware identity |
| `CAPABILITIES` | Device → host | Report cards, channels, gains, rates, and enabled features |
| `GET_CONFIG` | Host → device | Read the selected applied acquisition configuration |
| `SET_CONFIG` | Host → device | Set stopped-state rate, channel mask, and gains |
| `START_ACQUISITION` | Host → device | Start ADC and streaming |
| `STOP_ACQUISITION` | Host → device | Stop streaming and return ADC to ready state |
| `ADC_BLOCK` | Device → host | Carry packed 24-bit samples and per-frame validity |
| `GET_STATUS/STATUS` | Both | Report state, card presence, counters, and sticky faults |
| `PULSE_REQUEST/PULSE_RESULT` | Both | Execute on-demand SET, RESET, or diagnostic pulse |
| `ERROR` | Device → host | Report asynchronous stable error/event information |

Storage, GNSS, IMU, Bluetooth, and USB-mass-storage messages are not part of milestone 1. New
message types extend V2 without changing the existing frame envelope.

## 11. Initialization and shutdown order

Startup:

1. Initialize logging and monotonic time.
2. Initialize required platform GPIO, SPI, and UART services.
3. Initialize Rev-1 board direct GPIOs and hold 74HC595 outputs disabled.
4. Shift and latch the safe 16-bit image, then enable shift outputs.
5. Fix the SD mux toward the ESP32 and hold USB2641 reset.
6. Detect both card slots.
7. Create fixed buffer pools and milestone-1 queues.
8. Start `task_acquisition`; it initializes/verifies AD7779 and reports ready or failed state.
9. Start `task_communication` and wait for host handshake.

Orderly stop:

1. Reject new configuration and pulse commands.
2. Complete or safely abort the current pulse.
3. Stop ADC acquisition and drain/return owned blocks.
4. Emit final counters if the host remains connected.
5. Return board outputs to the documented safe image.

Any startup failure enters the board safe state and keeps communication available when doing so is
electrically safe.

## 12. Milestone-2 extension rules

- `task_storage` becomes the sole filesystem owner; acquisition still never blocks on SD writes.
- SD remains permanently connected to the ESP32. No USB2641 transition API is added.
- `task_gnss` owns MAX-M10S UART parsing and publishes monotonic-to-UTC mappings.
- `task_imu` owns LSM6DSV sampling and publishes low-rate orientation/movement records.
- GNSS, IMU, housekeeping, ADC, and system events remain separate timestamped streams.
- `task_processing` is created only if measured CPU work or latency justifies a separate task.
- Bluetooth reuses the V2 message layer and does not create a second command protocol.

## 13. Interface acceptance criteria

- [ ] Portable drivers compile without ESP-IDF or FreeRTOS headers.
- [ ] AD7779 and 74HC595 drivers run against fake platform callbacks in host tests.
- [ ] Board GPIO numbers and shift-register bit positions do not appear in application code.
- [ ] Only `board` writes the physical 74HC595 chain.
- [ ] Only `task_acquisition` configures or reads streaming AD7779 data.
- [ ] All eight channels share one conversion-frame timestamp and sequence.
- [ ] The DRDY ISR performs no SPI, allocation, blocking, or logging.
- [ ] Acquisition continues servicing DRDY while UART is blocked or disconnected.
- [ ] Pool exhaustion produces a visible sequence gap and counter.
- [ ] SET and RESET requests cannot overlap and affected frames are marked.
- [ ] Configuration requests are atomic and rejected while acquisition is active.
- [ ] Protocol parsing handles fragmented, concatenated, corrupt, and unknown frames.
- [ ] V2 CRC and serialization match shared protocol test vectors.
- [ ] Eight raw channels stream at 1 kSPS for eight hours without unexplained loss.
