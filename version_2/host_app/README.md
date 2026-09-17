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
├── protocol.py       Fixed control records and mixed-stream parser
├── adc_record.py     Independent 512-byte ADC-record decoder
├── serial_client.py  Serial connection, reader loop, and command correlation
├── bluetooth_client.py  Future Bluetooth adapter using the same protocol
├── capture.py        Raw validated-record capture and session metadata
└── cli.py            Initial device-info/status/config/start/stop interface

tests/
└── shared vectors, stream fragmentation, and command-correlation tests
```

The host decoder is intentionally independent from the C encoder. Both sides
will consume the byte-exact vectors under `shared/protocol/test_vectors/`.

## Dependency boundary

- `protocol.py` and `adc_record.py` do not open serial ports or update a UI.
- Connection adapters move bytes, handle transport-specific fragmentation, and
  correlate requests without redefining commands or interpreting scientific
  samples.
- UART and future Bluetooth clients use the same protocol and ADC-record codecs.
  A Bluetooth stream may use a negotiated reduced profile that remains explicit
  in status and capture metadata.
- `capture.py` persists only validated records and associated metadata/events.
- Plotting and scientific export consume decoded/captured data after the raw
  protocol path is proven.

See `version_2/docs/uart_host_integration_plan.md` for the proposed sequence and
review gates.
