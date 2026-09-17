# Version 2 Host Application Scaffold

## Status

This directory contains organization-only scaffolding. There is no serial or
Bluetooth connection, protocol codec, capture path, command-line interface,
plot, or dependency configuration yet.

The first host milestone is a command-line reference implementation, not a
graphical application. It will validate the shared protocol vectors, exercise a
synthetic firmware stream, and save approved 512-byte ADC records byte-for-byte.

## Planned modules

```text
geophys_host/
├── protocol.py       Fixed 64-byte commands and mixed-stream parser
├── adc_record.py     Independent 512-byte `\DAT` decoder
├── serial_client.py  Serial connection, reader loop, and named-reply matching
├── bluetooth_client.py  Future Bluetooth adapter using the same protocol
├── capture.py        Raw validated-record capture and session metadata
└── cli.py            Device/config/diagnostic/streaming/recording interface

tests/
└── shared vectors, stream fragmentation, and named-reply tests
```

The host decoder is intentionally independent from the C encoder. Both sides
will consume the byte-exact vectors under `shared/protocol/test_vectors/`.

## Dependency boundary

- `protocol.py` and `adc_record.py` do not open serial ports or update a UI.
- Connection adapters move bytes and handle transport-specific fragmentation
  without redefining commands or interpreting scientific samples. The client
  permits one outstanding command and waits for its defined reply ID.
- UART and future Bluetooth clients use the same protocol and ADC-record codecs.
  A Bluetooth stream may use the explicit channel mask and decimation accepted
  by `STREAMING_START_RESULT`.
- `capture.py` persists only validated records and associated metadata/events.
- Plotting and scientific export consume decoded/captured data after the raw
  protocol path is proven.

See `version_2/docs/uart_host_integration_plan.md` for the proposed sequence and
review gates.
