# Version 2 Host Application

## Status

This directory contains the independent Python reference host for the shared wire
protocol. It now includes the command and ADC-record codecs, a corruption-
tolerant mixed `\CMD`/`\DAT` parser, exact named-reply matching while ADC data
is in flight, raw validated-block capture, live continuity/error counters, and
a Tk desktop application with manual USB/COM selection. The desktop app has a
recording catalog and controls in one tab and embedded raw-channel plots with
live Start/Stop controls in a separate tab. Serial work runs on a background
thread so the window remains responsive.

The firmware and host now support live start/stop and asynchronous `\DAT`
delivery over the USB/UART link. Rev-1 hardware returned CRC-valid eight-channel
blocks at 1 kSPS and four-channel blocks with decimation by 5. Streaming was
also exercised while SD recording was active, and reconnect succeeded after
the firmware's five-second session timeout. The received blocks still expose
the known ADC critical status and startup sequence loss, so scientific-data
validation remains open.

## Setup

From the repository root:

```sh
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r version_2/host_app/requirements.txt
```

Launch the desktop application from the repository root:

```sh
python3 version_2/host_app/launch_host_app.py
```

Select the device's USB/COM port, leave the baud rate at `921600`, and click
**Connect**. The recordings catalog loads automatically. Use the **Live
Stream** tab to select channels and decimation, then click **Start live
streaming**.

The same application can also be launched as a module:

```sh
PYTHONPATH=version_2/host_app python3 -m geophys_host
```

The command-line connection probe remains available:

```sh
PYTHONPATH=version_2/host_app \
python3 -m geophys_host.cli hello /dev/cu.usbserial-PORT
```

The earlier command-line live view also remains available:

```sh
PYTHONPATH=version_2/host_app \
python3 -m geophys_host.cli live /dev/cu.usbserial-PORT
```

Capture every validated 512-byte block byte-for-byte while plotting:

```sh
PYTHONPATH=version_2/host_app \
python3 -m geophys_host.cli live /dev/cu.usbserial-PORT \
  --channels all --decimation 2 --capture capture.dat
```

Use `--terminal` instead of opening its separate graph, or `--duration 30` for
a bounded run. The desktop app sends the required once-per-second
`DEVICE_GET_CONFIG` keepalive for the entire connection. Catalog refreshes run
in the serial worker and disable only catalog-specific controls.

The live display currently labels inputs `CH0` through `CH7` and shows raw
signed 24-bit ADC counts. Physical card-axis/thermistor names and calibrated
units will be added only after their channel mapping and calibration are
defined and verified.

Run the portable tests with:

```sh
PYTHONPATH=version_2/host_app \
python3 -m unittest discover -s version_2/host_app/tests -v
```

## Modules

```text
geophys_host/
├── protocol.py       Fixed 64-byte command codecs
├── adc_record.py     Independent 512-byte `\DAT` decoder
├── stream_parser.py  Incremental mixed `\CMD`/`\DAT` recovery
├── serial_client.py  Serial connection and named-reply matching
├── capture.py        Raw validated-record capture
├── live.py           Stream counters, rolling data model, and plot
├── gui.py            Desktop connection, recordings, and live-stream tabs
└── cli.py            Command-line device and live-stream interface

tests/
└── shared vectors, stream fragmentation, and named-reply tests
```

The host decoder is intentionally independent from the C encoder. Both sides
will consume the byte-exact vectors under `shared/protocol/test_vectors/`.

## Dependency boundary

- `protocol.py`, `adc_record.py`, and `stream_parser.py` do not open serial
  ports or update a UI.
- The GUI sends device operations to one background serial owner and updates
  Tk widgets only from the GUI thread.
- Connection adapters move bytes and handle transport-specific fragmentation
  without redefining commands or interpreting scientific samples. The client
  permits one outstanding command and waits for its defined reply ID.
- UART and future Bluetooth clients use the same protocol and ADC-record codecs.
  A Bluetooth stream may use the explicit channel mask and decimation accepted
  by `STREAMING_START_RESULT`.
- `capture.py` persists only complete, validated records. Session metadata and
  scientific exports remain future work.
- Plotting consumes decoded data and never changes the raw capture bytes.

See `version_2/docs/uart_host_integration_plan.md` for the proposed sequence and
review gates.
