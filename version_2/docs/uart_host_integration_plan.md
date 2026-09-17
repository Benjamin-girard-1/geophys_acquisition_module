# UART-to-USB Host Integration Plan

## Document status

- Status: Active implementation plan
- Runtime implementation: discovery and device-configuration UART slices
  verified on Rev-1; remaining commands and streaming not started
- Wire authority: `shared/protocol/protocol.md`

This document describes how to implement the approved protocol without
redefining its fields, identifiers, results, or behavior. UART-to-USB and future
Bluetooth connections carry the same 64-byte commands and 512-byte data blocks.

## Component boundaries

```text
ADC_DRDY ISR
    -> task_acquisition
        -> adc_frame_t buffers
            -> data_format/adc_record
                -> task_communication -> UART-to-USB -> host
                -> future Bluetooth transport -> host
                -> task_storage -> SD card

host -> byte transport -> task_communication -> validated command
    -> owning application task -> named reply -> requesting transport
```

- `drivers/adc/ad7779.*` handles AD7779 registers and raw conversion decoding.
- `app/task_acquisition.*` owns ADC configuration, acquisition sequence,
  timestamps, and data validity.
- `data_format/adc_record.*` builds and validates complete 512-byte `\DAT`
  blocks.
- `protocol/protocol_frame.*` handles fixed 64-byte `\CMD` framing and stream
  resynchronization.
- `protocol/protocol_messages.*` handles typed command and reply payloads.
- `transports/transport_uart.*` moves bytes and handles partial reads/writes.
- `app/task_communication.*` owns the UART session, parser, dispatch, reply
  scheduling, and `\DAT` routing.
- `host_app/` independently implements the same codecs and connection policy.

Protocol and data-format code do not directly operate UART, FreeRTOS, FatFs,
board GPIO, or component drivers.

## Phase 1: Shared vectors

1. Create byte-exact vectors from `shared/protocol/protocol.md`.
2. Cover every defined request and named reply.
3. Cover four-channel and eight-channel `\DAT` blocks.
4. Verify CRC-32/ISO-HDLC with `CRC("123456789") = 0xCBF43926`.
5. Add invalid magic, direction, length, reserved-byte, padding, and CRC cases.

Exit condition: firmware and host codecs can be tested against the same bytes.

## Phase 2: Portable firmware codecs

1. Implement explicit little-endian integer helpers.
2. Implement fixed 64-byte command encoding and validation.
3. Implement incremental `\CMD` search and collection without allocation.
4. Implement signed 24-bit sample packing and sign extension.
5. Implement complete 512-byte data-block construction and validation.
6. Use the platform IEEE CRC wrapper with the protocol initial and final XOR.
7. Pass the shared vectors in host-native tests.

The firmware receives only `\CMD` requests. It silently discards a request with
an invalid CRC. It never executes a partially validated command.

## Phase 3: Host reference implementation

1. Implement independent Python encoders and decoders.
2. Implement a mixed-stream parser for 64-byte `\CMD` replies and 512-byte
   asynchronous `\DAT` blocks.
3. Test arbitrary fragmentation, concatenation, corrupt candidates, and boot
   garbage.
4. Allow one outstanding command and wait for its specific named reply ID.
5. Continue processing `\DAT` blocks while waiting for that reply.
6. Implement the `HELLO`, configuration, diagnostic, streaming, and recording
   workflows defined by the protocol.
7. Save validated `\DAT` blocks byte-for-byte.

Exit condition: the host can validate a fragmented synthetic mixed stream and
can identify a timeout without retrying a state-changing command blindly.

## Phase 4: Firmware UART vertical slice

1. Initialize the UART-to-USB bridge at the selected tested baud rate.
2. Create fixed parser, reply, and data-block buffers.
3. Implement `HELLO` and the five-second USB-session activity rule.
4. Implement `DEVICE_GET_CONFIG`, `DEVICE_SET_CONFIG`, and
   `DEVICE_GET_DIAGNOSTIC` using snapshots and atomic updates from the
   resource-owning tasks.
5. Implement streaming start/stop around a deterministic synthetic data source.
6. Verify that command replies are sent between complete `\DAT` blocks and that
   partial UART writes resume from the correct byte.

Exit condition: the host establishes a session, polls configuration, controls a
synthetic stream, detects corruption and sequence gaps, and captures valid
blocks.

## Phase 5: Real acquisition

1. Complete the minimal DRDY ISR and timestamp path.
2. Complete bounded acquisition queues and counters.
3. Replace synthetic conversions with AD7779 conversions.
4. Build exactly 20-conversion eight-channel blocks or 40-conversion
   four-channel blocks.
5. Apply the requested stream decimation without changing the recording
   acquisition configuration.
6. Verify that UART backpressure never blocks acquisition.

## Phase 6: Recording and Bluetooth

1. Route recording commands to `task_storage`, the sole filesystem owner.
2. Store the same complete `\DAT` block representation used by live streaming.
3. Add Bluetooth as another byte transport without changing command meanings.
4. Preserve USB priority and the five-second USB inactivity rule.
5. Fragment and reassemble complete protocol records below the application
   protocol when required by Bluetooth.

## Verification matrix

| Area | Required verification |
|---|---|
| Command codec | Every command vector, invalid CRC, reserved bytes, length, and padding |
| Data codec | Both supported masks, signed limits, all defined status values, and invalid CRC |
| Parser | Fragmented, concatenated, corrupt, and boot-garbage streams |
| Command handling | One outstanding command and exact named reply matching |
| Retry handling | Read-only retry and state reconciliation before a state-changing retry |
| UART | Partial reads/writes and measured achieved baud rate |
| Streaming | Correct mask, decimation, sample period, timestamps, and source sequence |
| Backpressure | Slow or disconnected host without acquisition blocking |
| Recording | Same validated 512-byte blocks recovered from the SD card |
| Bluetooth | Command parity, fragmentation, USB priority, and reconnect behavior |

No implementation phase may add a command ID, result value, diagnostic value,
or alternate frame without first updating and approving `protocol.md`.
