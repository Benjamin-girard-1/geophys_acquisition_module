# Protocol Test-Vector Plan

No golden vectors are approved yet. This directory will contain byte-exact
inputs and machine-readable expected results after the draft formats are
reviewed.

Planned structure:

```text
test_vectors/
├── control/
│   ├── valid/
│   └── invalid/
├── adc_record/
│   ├── valid/
│   └── invalid/
└── manifest.json
```

Each binary vector will have a manifest entry describing its format version,
decoded fields, expected status, and purpose. Firmware host tests and the PC
decoder must consume the same vectors.

The first vector set will cover:

- Empty and populated control requests.
- Request/response ID correlation.
- Maximum control payload and canonical padding.
- Invalid control magic, version, payload length, padding, and CRC.
- Eight-channel 20-conversion ADC records.
- Four-channel 40-conversion ADC records.
- Zero, positive, negative, minimum, and maximum signed 24-bit samples.
- Partial ADC records and zero padding.
- Invalid ADC magic, channel mask, conversion count, and CRC.
- Sequence gaps and correlated rare-error events.
- Exact and explicitly reduced stream-profile command results.
- ADC records with a reduced channel mask.
- The approved source-sequence representation for a decimated stream.

The same application-record vectors are used for UART and Bluetooth. Separate
transport tests may split the bytes at different boundaries, but must reassemble
to these exact records.
