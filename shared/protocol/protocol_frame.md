# VProtocol Framing Proposal

## Document status

- Status: Draft for review; not an approved wire contract
- Applies to: UART-to-USB and future Bluetooth protocol transports
- UART baseline: 921600 baud, 8-N-1, no hardware flow control
- Common rules: `protocol_code.md`

This document proposes how control traffic and ADC sample records are framed
independently of the transport that carries them. It intentionally specifies
structure and ownership before any encoder, decoder, or parser is implemented.

The proposal uses two fixed-size record classes:

| Record class | Proposed size | Purpose |
|---|---:|---|
| Control | 128 bytes | Commands, responses, status, and asynchronous events |
| ADC sample | 512 bytes | Packed conversions suitable for transport capture and later SD storage |

Fixed-size control records are proposed because command traffic is infrequent,
the maximum memory and processing cost is bounded, and a corrupted length field
cannot change how many bytes the parser collects. The 512-byte ADC record is
defined separately in `adc_record.md`.

## Transport and stream rules

- UART is a byte stream, and a Bluetooth stack may fragment or aggregate an
  application record according to its own packet size. No reader assumes that
  one transport callback contains exactly one protocol record.
- Each record class has a distinct four-byte magic value. The actual magic
  values remain unassigned pending review.
- The parser scans for either magic value, collects the corresponding fixed
  record size, and validates the version and CRC-32C.
- After a failed candidate, the parser advances by one byte and resumes its
  magic search. It does not assume that the next read begins on a boundary.
- Firmware finishes transmitting one record before beginning another. Bytes
  from command responses and ADC records are never interleaved.
- Command responses and critical events have priority between complete ADC
  records.
- ESP-IDF diagnostic text is not inserted into a binary protocol channel. The
  UART host still tolerates ROM boot bytes before the first valid record.
- Requests are answered through the connection that submitted them. Transport
  adapters do not reinterpret message types or maintain a private command set.
- Each live connection has its own transmit progress and negotiated stream
  profile. Reducing a Bluetooth stream never changes the framing or silently
  shortens a record.

## Proposed 128-byte control record

The layout below is a review proposal. Numeric message identifiers, flag bits,
and status codes remain unassigned.

| Offset | Size | Field | Notes |
|---:|---:|---|---|
| 0 | 4 | Magic | Control-record synchronization value |
| 4 | 1 | Protocol version | Compatibility decision before payload decoding |
| 5 | 1 | Message type | Stable value defined in `protocol_types.md` |
| 6 | 1 | Flags | Request, response, asynchronous, or error semantics |
| 7 | 1 | Payload length | Meaningful bytes in the 104-byte payload area |
| 8 | 4 | Message sequence | Monotonic per transmitted control record |
| 12 | 4 | Request identifier | Nonzero for a request/response pair; zero for unsolicited events |
| 16 | 2 | Status code | Stable protocol status; zero in requests |
| 18 | 2 | Reserved | Must be zero in version 1 |
| 20 | 104 | Payload and zero padding | Typed payload followed by canonical zero bytes |
| 124 | 4 | CRC-32C | Covers bytes 0 through 123 |

The payload-length field does not control framing; the parser always collects
128 bytes after recognizing a control-record candidate. It identifies which
payload bytes are meaningful and must be at most 104. All remaining payload
bytes must be zero so encoded records are deterministic and do not expose
uninitialized RAM.

The fixed size must be confirmed only after every required response and event
payload has been laid out. In particular, `PULSE_RESULT`, `DEVICE_INFO`, and
`CARD_INFO` must fit without undocumented fragmentation.

## Integrity proposal

- Algorithm: CRC-32C; exact reflected/non-reflected parameters and check value
  must be frozen before implementation.
- Control record coverage: bytes 0 through 123.
- ADC record coverage: defined independently in `adc_record.md`.
- CRC fields are encoded little-endian unless the final specification says
  otherwise.
- A valid CRC detects corruption; it does not provide authentication.

## Parser state model

The planned incremental parser has these conceptual states:

```text
SEARCH_MAGIC
    -> COLLECT_CONTROL_RECORD (128 bytes)
    -> COLLECT_ADC_RECORD     (512 bytes)
    -> VALIDATE
        -> EMIT_RECORD
        -> DISCARD_ONE_BYTE_AND_RESYNC
```

The parser uses fixed-capacity memory and performs no allocation while receiving
the stream. Each firmware transport RX path needs to accept control records.
The host RX path accepts both record classes after transport reassembly.

## Transmit scheduling

`task_communication` is the sole UART owner and the one application-protocol
dispatcher. A future Bluetooth adapter may own Bluetooth connection mechanics,
but it does not duplicate command behavior. Each output connection drains work
in this order whenever its current record has completed:

1. Command responses whose result capacity was reserved before acceptance.
2. Critical status and error events.
3. Ready ADC records.

Transport writes may be partial or packet-limited. The transport path continues
from the returned byte offset until the selected record is complete.
Acquisition never waits for that process.

## Decisions requiring approval

- [ ] Use fixed 128-byte control records.
- [ ] Send 512-byte ADC records directly on the mixed stream rather than adding
      a second outer envelope and CRC.
- [ ] Approve the same application record framing for UART and Bluetooth, with
      transport-specific fragmentation/reassembly below it.
- [ ] Assign distinct four-byte control and ADC magic values.
- [ ] Freeze the complete control header above.
- [ ] Freeze CRC-32C parameters and byte order.
- [ ] Confirm that 104 payload bytes fit every milestone-1 response and event.
- [ ] Define resynchronization limits and counters for rejected candidates.
- [ ] Define the Bluetooth service/channel binding and maximum fragment size
      without changing command semantics.
