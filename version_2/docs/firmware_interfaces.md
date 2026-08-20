# Version 2 Firmware Interface Contract

## Document information

- Status: Draft
- Product version: V2
- Initial target: Magnetic acquisition
- Last updated: [YYYY-MM-DD]

## 1. Goals

- Keep component drivers independent of ESP-IDF and board wiring.
- Provide consistent initialization and error behavior.
- Give every stateful resource one owner.
- Preserve simultaneous ADC-frame semantics.
- Support compact 24-bit storage and transport encoding.
- Support a versioned host protocol.
- Permit reuse of validated V1 host behavior.

## 2. Non-goals for the first milestone

- Accelerometer-card implementation
- Bluetooth transport: [TBD]
- Advanced processing: [TBD]
- Multiple concurrent host transports: [TBD]
- Other: [TBD]

## 3. Common error model

| Error category | Meaning | Retryable | Logged locally | Host-visible |
|---|---|---|---|---|
| Success | Operation completed | N/A | No | Yes |
| Invalid argument | Caller supplied invalid configuration | No | Optional | Yes |
| Not initialized | Resource is not ready | Possibly | Yes | Yes |
| Not found | Expected component absent | Possibly | Yes | Yes |
| I/O failure | Bus or peripheral operation failed | Possibly | Yes | Yes |
| Timeout | Operation did not complete in time | Possibly | Yes | Yes |
| CRC/data integrity | Corrupt transfer or frame | Possibly | Counted | Yes |
| Busy | Resource owned by another operation | Yes | Optional | Yes |
| Overflow/data loss | Queue or acquisition overrun | Possibly | Counted | Yes |
| Unsupported | Feature unavailable | No | Optional | Yes |
| Hardware fault | Device or board fault | Depends | Yes | Yes |

Rules:

- Portable drivers do not expose `esp_err_t`.
- Platform code translates ESP-IDF errors.
- Board code adds resource context.
- Application code translates failures into product operations.
- Protocol exposes stable public status codes.
- Low-level numeric error codes are not transmitted directly.

## 4. Platform callback contracts

### SPI

| Operation | Inputs | Output | Blocking | Timeout | Called from |
|---|---|---|---|---|---|
| Transfer | Context, TX, RX, length | Status | [YES/NO] | [TBD] | Task only |
| Write | Context, data, length | Status | [YES/NO] | [TBD] | Task only |
| Read | Context, data, length | Status | [YES/NO] | [TBD] | Task only |

Requirements:

- Chip-select ownership: [ESP-IDF HARDWARE / BOARD CALLBACK]
- Maximum transfer size: [TBD]
- DMA-safe buffer requirements: [TBD]
- Thread-safety policy: [TBD]

### I²C

| Operation | Inputs | Output | Timeout | Retry policy |
|---|---|---|---|---|
| Register write | Context, address, register, data | Status | [TBD] | [TBD] |
| Register read | Context, address, register, buffer | Status | [TBD] | [TBD] |
| Raw transfer | Context, buffers | Status | [TBD] | [TBD] |

### UART

| Operation | Inputs | Output | Blocking | Timeout |
|---|---|---|---|---|
| Write | Context, data, length | Bytes/status | [TBD] | [TBD] |
| Read | Context, buffer, capacity | Bytes/status | [TBD] | [TBD] |
| Flush | Context | Status | Yes | [TBD] |

### GPIO

| Operation | Inputs | Output | ISR-safe |
|---|---|---|---|
| Set output | Context, level | Status | [YES/NO] |
| Read input | Context | Level/status | [YES/NO] |
| Configure interrupt | Context, edge, callback | Status | No |
| Enable/disable interrupt | Context | Status | [YES/NO] |

### Time

| Operation | Unit | Clock domain | Monotonic |
|---|---|---|---|
| Current acquisition time | [µs/ns] | Monotonic | Yes |
| Delay | µs | N/A | N/A |
| GNSS UTC | [TBD] | UTC | No |
| Map monotonic to UTC | [TBD] | Both | [TBD] |

## 5. Application data types

### ADC frame

| Field | Meaning | Unit/format |
|---|---|---|
| Sequence | Monotonic frame counter | Unsigned integer |
| Timestamp | Common time for all ADC channels | Monotonic [TBD] |
| Channel mask | Channels containing valid samples | Bit mask |
| Samples | Sign-extended 24-bit ADC codes | Signed 32-bit in RAM |
| ADC status | Aggregated header/error information | Bit flags |
| Pulse state | Normal, pulse, or settling | Enumeration |
| Data-valid flags | Validity by channel/frame | Bit flags |
| Dropped-before | Missing frames before this frame | Count |

Rules:

- All magnetic axes in a frame have the same timestamp.
- No floating-point conversion is required in the acquisition task.
- Storage packs valid ADC values into three bytes.
- Calibration and physical-unit conversion happen outside the driver.

### Pulse event

| Field | Meaning |
|---|---|
| Event type | SET, RESET, or diagnostic sequence |
| Requested timestamp | When the command was accepted |
| Actual pulse timestamp | When the pulse became active |
| Pulse width | Measured/configured duration |
| Result | Success or failure |
| Settling end | Time normal samples become valid again |

### GNSS time mapping

| Field | Meaning |
|---|---|
| Monotonic reference | Local timestamp |
| UTC reference | Corresponding GNSS UTC |
| Validity | Mapping currently valid |
| Age | Time since last valid update |
| Estimated uncertainty | Expected mapping error |

## 6. Board-level API responsibilities

| Operation | Caller | Owner | Synchronous/asynchronous | Failure behavior |
|---|---|---|---|---|
| Initialize board | `app_main` | Board | Synchronous | Enter safe state |
| Enter safe state | Any fatal path | Board | Synchronous | Best effort |
| Detect card | Application | Card detection | [TBD] | Mark unavailable |
| Start ADC hardware | Acquisition | Board/ADC | [TBD] | Report failure |
| Stop ADC hardware | Acquisition | Board/ADC | [TBD] | Safe shutdown |
| Request magnetic SET | Application command | Magnetic card | Asynchronous | Return result |
| Request magnetic RESET | Application command | Magnetic card | Asynchronous | Return result |
| Select ESP32 SD ownership | Storage | Board | Asynchronous | Remain isolated |
| Select USB SD ownership | Application/storage | Board | Asynchronous | Remain isolated |
| Read power status | Diagnostics | Board | Synchronous | Return unavailable |

The application must not directly set GPIOs or shift-register bits.

## 7. Driver lifecycle

Every component driver follows this lifecycle:

1. Create zeroed driver instance.
2. Supply configuration and portable callbacks.
3. Initialize communication state.
4. Reset component when required.
5. Verify identity/status.
6. Apply configuration.
7. Start operation.
8. Read data or process events.
9. Stop operation.
10. Deinitialize safely.

### Driver configuration checklist

- [ ] Bus callback/context
- [ ] GPIO callbacks/context
- [ ] Delay/time callbacks
- [ ] Timeout values
- [ ] Sample rate
- [ ] Channel mask
- [ ] Gain/range
- [ ] CRC policy
- [ ] Interrupt/data-ready behavior

## 8. Task and resource ownership

| Resource | Owning task/module | Other users | Access mechanism |
|---|---|---|---|
| AD7779 streaming | `task_acquisition` | Diagnostics | Request queue |
| Magnetic SET/RESET | [Acquisition/magnetic owner TBD] | Communication | Request queue |
| ADC sample buffers | `task_acquisition` | Storage/processing | Buffer queue |
| Filesystem | `task_storage` | Communication/export | Storage requests |
| SD/USB ownership | `task_storage` + board state machine | USB export | Request queue |
| Protocol state | `task_communication` | Transports | Byte queues |
| UART transport | [TBD] | Communication | Transport interface |
| USB transport | [TBD] | Communication | Transport interface |
| GNSS parser/time | [TBD] | Acquisition/status | Snapshot/message |

## 9. Queue contracts

| Queue | Producer | Consumer | Item | Capacity | Full behavior |
|---|---|---|---|---:|---|
| ADC blocks | Acquisition | Storage | Sample block reference | [TBD] | Count/report loss |
| Processing input | Acquisition | Processing | Sample block reference | [TBD] | [TBD] |
| Commands | Communication | Application owners | Command request | [TBD] | Reject busy |
| Responses | Application owners | Communication | Command result | [TBD] | [TBD] |
| Status/events | All owners | Communication/logging | Event | [TBD] | Coalesce/drop policy |

Rules:

- Acquisition must not block on SD writes or host communication.
- Buffer ownership transfers must be explicit.
- Dropped ADC data is always counted and reported.
- Queue capacity is justified using worst-case latency.

## 10. Protocol frame requirements

The detailed wire format belongs in:

- `shared/protocol/protocol_frame.md`
- `shared/protocol/protocol_types.md`

Required frame fields:

| Field | Requirement |
|---|---|
| Synchronization | Detect frame boundary after corrupt/lost bytes |
| Version | Distinguish V1 and V2 |
| Message type | Identify payload |
| Payload length | Permit variable-size messages |
| Sequence | Detect dropped/repeated messages |
| Timestamp | Required for acquisition data/events |
| Payload | Explicitly serialized |
| Integrity | CRC/checksum [TBD] |
| Byte order | [LITTLE/BIG ENDIAN TBD] |

## 11. Initial message types

| Message | Direction | Purpose | Required for milestone |
|---|---|---|---|
| Device information | Device → host | Firmware, hardware, protocol versions | Yes |
| Capabilities | Device → host | Cards, channels, rates, transports | Yes |
| Get configuration | Host → device | Read current acquisition settings | Yes |
| Set configuration | Host → device | Rate, mask, gain | Yes |
| Start acquisition | Host → device | Begin ADC acquisition | Yes |
| Stop acquisition | Host → device | Stop acquisition | Yes |
| ADC sample block | Device → host | Packed 24-bit frames | Yes |
| Status | Device → host | Health and counters | Yes |
| SET request/result | Both | On-demand magnetic SET | Yes |
| RESET request/result | Both | On-demand magnetic RESET | Yes |
| Start recording | Host → device | Begin SD recording | [TBD] |
| Stop recording | Host → device | Finalize SD recording | [TBD] |
| Storage status | Device → host | Capacity/ownership/faults | [TBD] |
| Error response | Device → host | Stable operation failure | Yes |

## 12. V1 host compatibility

- Existing V1 frame:
  `A5 5A length frame_u32 8 × sample_i32 status checksum`
- V2 packed 24-bit frame required: Yes
- Firmware must emit V1 frames: [YES / TEMPORARILY / NO]
- Host supports both decoders: [YES / NO]
- Capability handshake selects decoder: [YES / NO]
- Existing UI behavior preserved: Yes
- Existing captured packets used as regression tests: [TBD]

## 13. Open interface decisions

| Decision | Options | Selected | Reason |
|---|---|---|---|
| Protocol CRC | [TBD] | [TBD] | [TBD] |
| Byte order | Little/big | [TBD] | [TBD] |
| Timestamp width/unit | [TBD] | [TBD] | [TBD] |
| ADC block size | [TBD] | [TBD] | [TBD] |
| Buffer ownership model | Pool/copy | [TBD] | [TBD] |
| Pulse owner | Acquisition/separate module | [TBD] | [TBD] |
| V1 compatibility period | [TBD] | [TBD] | [TBD] |

## 14. Interface acceptance criteria

- [ ] Portable drivers compile without ESP-IDF headers.
- [ ] Drivers can run against fake host callbacks.
- [ ] Board GPIO numbers do not leak into the application.
- [ ] All three magnetic axes share one frame timestamp.
- [ ] Acquisition does not block on storage or communication.
- [ ] Buffer-full behavior is defined and counted.
- [ ] Pulse requests cannot overlap.
- [ ] SD ownership cannot be granted to both sides.
- [ ] Protocol parsing accepts partial and multiple frames.
- [ ] V1 host behavior has regression coverage.