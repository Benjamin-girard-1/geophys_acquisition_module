# Host Tests

The tests cover shared golden vectors, arbitrary serial-read fragmentation,
concatenated records, corrupt candidates, boot garbage, exact named-reply
matching with interleaved asynchronous `\DAT` blocks, continuity counters,
byte-exact ADC capture, desktop-worker recording catalog retrieval, and
stream-start sequencing. BLE tests cover fragmented command writes and
fragmented `DEVICE_INFO` notifications without requiring a Bluetooth adapter.

They use Python's standard `unittest` framework and do not require hardware.
