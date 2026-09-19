# Protocol Test-Vector Plan

This directory contains byte-exact examples derived from the authoritative
layout in `../protocol.md`. Firmware and host tests consume the same files.

The checked-in vectors currently cover `HELLO`, `DEVICE_INFO`,
`DEVICE_GET_CONFIG`, `DEVICE_SET_CONFIG`, `DEVICE_CONFIG`, CRC-invalid
`HELLO`, the temporary `TEMP_RECORDING_READ` request/reply, and one complete
eight-channel `\DAT` block. Additional command and data-block vectors remain
planned below.

Structure:

```text
test_vectors/
├── command/
│   ├── valid/
│   └── invalid/
├── data_block/
│   ├── valid/
│   └── invalid/
└── manifest.json
```

Each manifest entry will identify the command or data-block type, decoded
fields, expected validation result, and purpose. It will not define values that
are absent from `protocol.md`.

The complete command-vector set will cover:

- Every request and named reply currently defined in `protocol.md`.
- Empty and populated 48-byte payload areas.
- Canonical zero padding and little-endian multi-byte fields.
- Invalid `\CMD` magic, direction, payload length, reserved bytes, padding, and
  CRC-32/ISO-HDLC.
- The standard CRC check value `CRC("123456789") = 0xCBF43926`.

The first data-block vector set will cover:

- Eight-channel blocks containing 20 conversions.
- Four-channel blocks containing 40 conversions for masks `0x0F` and `0xF0`.
- Every defined data-block status value.
- Zero, positive, negative, minimum, and maximum signed 24-bit samples.
- Conversion-major ordering and least-significant-byte-first sample packing.
- Payload number, source-conversion sequence, 100 ns timestamp, and 100 ns
  sample-period fields.
- Decimated blocks whose source sequence and stored sample period remain
  coherent.
- Invalid `\DAT` magic, channel mask, status, and CRC-32/ISO-HDLC.

No partial 512-byte data block is valid. Transport tests may split a valid
64-byte command or 512-byte data block across arbitrary reads and must
reassemble it before validation. A host-side mixed-stream test must also verify
that asynchronous `\DAT` blocks do not complete the one outstanding command.
