# Communication Protocol Core

## Document status

- Status: Draft for review; no wire values are approved
- Scope: Rules shared by every command transport and host implementation
- Related documents: `protocol_frame.md`, `protocol_types.md`,
  `protocol_commands.md`, and `adc_record.md`

This document defines the basis of the Version 2 communication protocol: what
is common across transports, which guarantees the device and host rely on, and
where the detailed byte and command definitions live. It does not implement the
protocol and does not assign magic values, message identifiers, or status codes.

## One application protocol, multiple transports

UART-to-USB and Bluetooth carry the same application commands, responses,
status, and error meanings. A transport adapter moves bytes or packets; it does
not create its own command set or directly operate the ADC, magnetic cards, or
storage.

The firmware have one protocol dispatcher and one application-command
path. UART and Bluetooth adapters feed that path and return the resulting
responses through the requesting connection.

## Acquisition, streaming, and recording are separate concepts

These operations must not be collapsed into one state:

| Concept | Meaning |
|---|---|
| Acquisition | The AD7779 produces synchronized conversions using the applied ADC configuration. |
| Streaming | A selected live view of acquired data is delivered through one transport. |
| Recording | Acquired data is committed to a storage session. |

The ADC configuration contains scientific acquisition choices such as sample
rate, active channels, and gain. A stream profile contains delivery choices
such as the channels and rate that a particular connection receives.

A limited Bluetooth link must not silently change the ADC acquisition that an
SD recording or another consumer relies on. It may deliver a reduced view only
after the requested stream profile is explicitly accepted or negotiated.


## Request and response rules

- A host-generated request identifier is nonzero and identifies one logical
  request on one connection.
- Every syntactically valid request produces exactly one correlated response,
  including rejected, unsupported, and invalid-state requests.
- Read-only requests use their named response type. State-changing commands use
  `COMMAND_RESULT`, except operations with a richer named result such as
  `PULSE_RESULT`.
- Unsolicited status or error events use request identifier zero.
- A duplicate request policy must be defined before reconnect/retry behavior is
  implemented; requests must not accidentally repeat a pulse or other physical
  action.
- Unknown message types and unsupported features return stable public protocol
  results. Raw C, ESP-IDF, or FreeRTOS error values never appear on the wire.

## Validation and state changes

- The complete command is framed, integrity-checked, version-checked, and
  validated before it reaches an application resource owner.
- A malformed or rejected command makes no partial state change.
- Configuration is applied atomically and only in an allowed device state.
- The task that owns a resource performs the operation. Protocol code does not
  access AD7779 registers, GPIOs, UART hardware, Bluetooth APIs, or the
  filesystem directly.
- Accepted asynchronous work reserves a result path before the physical action
  starts, so completion or failure remains observable.

## Versioning and encoding rules

- Multi-byte integers use explicit little-endian serialization.
- Raw compiler-dependent C structures, enumerations, bit fields, pointers, and
  padding are never serialized.
- Units and signedness are specified for every field.
- Numeric message, result, error, state, and capability codes become stable
  public values once approved.
- Reserved fields and padding have canonical values and are validated.
- CRC detects accidental corruption; it does not authenticate a peer or encrypt
  data.
- Protocol-version compatibility is decided before a message payload is acted
  upon.

## Document boundaries

| Document | Authority |
|---|---|
| `protocol_core.md` | Common principles, transport parity, lifecycle, and invariants |
| `protocol_frame.md` | Record boundaries, headers, CRC coverage, parsing, and resynchronization |
| `protocol_types.md` | Reusable wire types and the stable message/code registry |
| `protocol_commands.md` | Detailed request, response, state, and error semantics for each command |
| `adc_record.md` | Packed scientific sample-record representation |
| `test_vectors/` | Byte-exact valid and invalid examples used by firmware and host tests |

If these documents conflict, the conflict must be resolved before codecs are
implemented rather than choosing one silently.

## Decisions requiring approval

- [ ] Approve one application command set for UART and Bluetooth.
- [ ] Approve the separation of ADC acquisition configuration from per-link
      stream delivery profiles.
- [ ] Choose exact-only negotiation, host-authorized fallback, or both.
- [ ] Define duplicate-request and reconnect behavior for physical actions.
- [ ] Decide which simultaneous streaming and recording combinations are
      supported.
- [ ] Approve the document responsibilities above.
