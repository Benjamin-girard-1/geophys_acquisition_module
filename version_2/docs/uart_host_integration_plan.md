# Host Communication Integration Scaffold and Review Plan

## Document status

- Status: Draft for review
- Scope: Milestone-1 UART control/live extraction and transport-compatible
  design for future Bluetooth
- Runtime implementation: Not started by this scaffold
- Related proposals: `shared/protocol/protocol_code.md`,
  `shared/protocol/protocol_frame.md`, `shared/protocol/protocol_types.md`,
  `shared/protocol/protocol_commands.md`, and `shared/protocol/adc_record.md`

This document explains the intended implementation sequence. It does not change
the frozen milestone scope and does not enable milestone-2 Bluetooth or SD
recording. The common protocol is being designed now so UART, future Bluetooth,
and future SD files can reuse tested command and record semantics.

## Proposed component boundaries

```text
ADC_DRDY ISR
    -> task_acquisition
        -> rich adc_frame_t blocks
            -> portable ADC-record encoder
                -> task_communication -> UART -> PC
                -> future Bluetooth adapter -> PC or mobile host
                -> future task_storage -> FatFs -> SD

host -> UART or future Bluetooth -> fixed control record -> protocol dispatcher
    -> validated application command queue
        -> task_acquisition
            -> result queue -> requesting connection
```

Responsibilities:

- `drivers/adc/ad7779.*`: AD7779 register behavior and raw conversion decoding.
- `app/task_acquisition.*`: ADC ownership, sequence/timestamps, frame validity,
  bounded buffers, and command serialization.
- `data_format/adc_record.*`: portable 512-byte sample-record encoding only.
- `protocol/protocol_frame.*`: fixed control-record encoding and incremental
  parsing only.
- `protocol/protocol_messages.*`: typed command/response payload codecs.
- `transports/transport_uart.*`: UART bytes, partial reads/writes, and timeouts.
- Future Bluetooth adapter: connection mechanics, packet fragmentation, and
  backpressure without a separate command protocol.
- `app/task_communication.*`: UART ownership, common parser dispatch, response
  priority, and ADC-record routing.
- `host_app/`: independent decoder, transport adapters, capture, and later UI.

Neither protocol nor data-format code calls UART, FreeRTOS, FatFs, board code,
or component drivers.

## Scaffold added for review

The scaffold establishes file placement and proposed constants but deliberately
does not add:

- CRC implementation.
- Integer or signed-24 serialization helpers.
- Record encoders or decoders.
- Stream parser state machine.
- Numeric magic values, message identifiers, or public status codes.
- UART task or queue behavior.
- Bluetooth service, task, or bandwidth policy.
- Synthetic or real ADC streaming.
- Host serial dependencies or executable commands.

## Implementation phases after approval

### Phase 1: Freeze bytes and vectors

1. Resolve every checkbox in the shared protocol proposals.
2. Assign stable magic values, versions, message identifiers, and status codes.
3. Freeze exact CRC-32C parameters and a standard check vector.
4. Freeze all control payload offsets and units.
5. Generate valid and invalid golden vectors.

Exit condition: independent firmware and host decoders can be judged against
byte-exact expected results without referring to C structure layout.

### Phase 2: Portable firmware codecs

1. Implement explicit little-endian integer helpers.
2. Implement signed 24-bit range checking, packing, and sign extension.
3. Implement CRC-32C.
4. Implement fixed control-record encode/decode.
5. Implement the incremental control RX parser.
6. Implement the 512-byte ADC-record builder and validator.
7. Run host-native tests against every shared vector.

Exit condition: portable code passes without ESP-IDF, FreeRTOS, UART, or ADC
hardware.

### Phase 3: Host reference implementation

1. Implement the same codecs independently in Python.
2. Implement a mixed-stream incremental parser.
3. Add deterministic synthetic streams with arbitrary read fragmentation.
4. Add a CLI for device info, status, configuration, start, stop, and capture.
5. Save validated 512-byte ADC records byte-for-byte.
6. Add scientific export only after raw capture is trustworthy.

Exit condition: the host passes shared vectors and can decode a fragmented
synthetic stream containing control and ADC records.

### Phase 4: Firmware UART vertical slice

1. Initialize UART0 at the reported achieved baud.
2. Create `task_communication` and fixed queues/buffers.
3. Implement `HELLO`, `DEVICE_INFO`, `GET_STATUS`, and explicit errors.
4. Add a deterministic synthetic ADC-record source.
5. Implement start/stop commands around the synthetic stream.
6. Verify partial UART writes and response priority between ADC records.

Exit condition: the PC performs a handshake, commands synthetic streaming,
detects corruption/gaps, and saves records without real ADC involvement.

### Phase 5: Real acquisition integration

1. Complete AD7779 start, frame-read, and stop transitions.
2. Implement the minimal DRDY ISR and timestamp ring.
3. Implement bounded acquisition blocks and counters.
4. Replace the synthetic source with encoded real acquisition frames.
5. Route stopped-state configuration and pulse requests through queues.
6. Verify that UART backpressure never blocks acquisition.

Exit condition: all eight channels stream at 1 kSPS with timestamps, sequences,
validity, counters, and command handling.

### Phase 6: Future Bluetooth reuse

1. Add the Bluetooth connection adapter without duplicating command handling.
2. Reuse the approved control and ADC-record codecs.
3. Measure usable bandwidth and transport fragmentation on the selected
   Bluetooth mode and peer devices.
4. Implement explicit stream-profile negotiation for channel selection and, if
   approved, decimation or a separate preview format.
5. Verify that every enabled command has the same meaning and result through
   UART and Bluetooth.

Exit condition: Bluetooth can issue the common command set and deliver its
reported stream profile without silent truncation or acquisition backpressure.

This phase remains milestone 2 unless the product requirements are explicitly
changed.

### Phase 7: Future SD reuse

1. Reuse the approved ADC-record encoder without changing its byte format.
2. Keep logical 512-byte records separate from acquisition block sizing.
3. Select multi-record filesystem write size from measurements.
4. Add session/file metadata and power-loss recovery.

This phase remains milestone 2 unless the product requirements are explicitly
changed.

## Initial verification matrix

| Area | First verification |
|---|---|
| Control codec | Golden vectors, invalid CRC, padding, and unknown type |
| ADC codec | Signed limits, channel masks, partial record, and invalid CRC |
| Parser | Fragmented, concatenated, corrupt, and boot-garbage streams |
| Request handling | Exactly one correlated response per accepted request |
| UART | Partial reads/writes and achieved baud report |
| Bluetooth | Packet fragmentation, reconnects, and command parity when implemented |
| Stream profile | Applied mask/rate/sequence step exactly match the command result |
| Backpressure | Slow/disconnected host without acquisition blocking |
| Data loss | Visible sequence gap and counters for every forced overflow |
| Long run | Eight channels at 1 kSPS for eight hours |

## Review gate

No codec or runtime implementation should begin until the following are
approved or revised:

- [ ] Fixed 128-byte control-record size.
- [ ] Fixed 512-byte ADC-record size and layout.
- [ ] Direct coexistence of both record classes on one UART byte stream.
- [ ] Common command semantics for UART and future Bluetooth.
- [ ] Explicit reduced-stream negotiation and decimated-sequence representation.
- [ ] Timestamp reconstruction policy.
- [ ] Rare-error event and missing-conversion policy.
- [ ] Message inventory and largest required control payload.
- [ ] Proposed module placement and dependency direction.
