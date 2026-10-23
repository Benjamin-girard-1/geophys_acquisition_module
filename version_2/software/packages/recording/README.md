# Geophys Recording Decoder

This flat-layout Python package contains the recording-format code shared by the
host and developer diagnostics. It has no device-transport, GUI, or Matplotlib
dependency.

The authoritative byte layout remains `shared/protocol/protocol.md`. Readers
accept DAT-only files and containers with a 4096-byte padded ASCII JSON
descriptor. New containers reserve a 32 KiB header and begin ADC iteration at
byte 32768. The superseded binary `\GEO` descriptor is intentionally unsupported.

## NumPy arrays for analysis

Install the NumPy extra from the repository root:

```sh
python3 -m pip install -e 'version_2/software/packages/recording[numpy]'
```

Then read raw signed ADC counts directly, without a CSV conversion:

```python
from geophys_recording import read_recording_numpy

data = read_recording_numpy("/path/to/recording", channels=2,
                            start_s=50, stop_s=300)
z_counts = data.channel(2)              # 1D NumPy int32 array
t_s = data.timestamps_s - data.timestamps_s[0]  # recorded time, starts at 0 s
fs = data.sample_rate_hz                 # samples per second
adc_gain = data.adc_gains[0]            # gain recorded for CH2
```

Use `channels=(0, 1, 2)` for multiple columns, or omit `channels` for every
recorded channel. `data.samples` is a two-dimensional array whose columns match
`data.channels`. Times are relative to the first ADC conversion, and the end
of the interval is excluded. `data.start_s` is the first returned sample's
actual time. `data.duration_s` is the complete recording duration. A V2 JSON
descriptor is returned as `data.header`; older DAT-only files return `None`.
`data.timestamps_s` is a NumPy array derived from the recorded monotonic
timestamp of each ADC block and its stored sample period. Subtract its first
element to start a plot at 0 s. The file has one timestamp per block, so
timing within a block uses the stored sample period.
The function checks block continuity throughout the file and checks CRC and
status for the selected interval. It does not convert counts to volts or a
magnetic unit.
