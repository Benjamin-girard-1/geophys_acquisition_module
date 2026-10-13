# Geophys Data

This flat-layout Python package contains the data-format code shared by the
host and developer diagnostics. It has no device-transport, GUI, or Matplotlib
dependency.

The authoritative byte layout remains `shared/protocol/protocol.md`. Readers
accept DAT-only files and containers with a 4096-byte padded ASCII JSON
descriptor. New containers reserve a 32 KiB header and begin ADC iteration at
byte 32768. The superseded binary `\GEO` descriptor is intentionally unsupported.
