# Version 2 Host Application

## Status

This directory contains the independent Python reference host for the shared wire
protocol. It now includes the command and ADC-record codecs, a corruption-
tolerant mixed `\CMD`/`\DAT` parser, exact named-reply matching while ADC data
is in flight, raw validated-block capture, live continuity/error counters, and
a Tk desktop application with manual USB/COM selection. The desktop app has a
recording catalog, embedded raw-channel plots, and a functional configuration
tab. The configuration tab can change ADC sample rate, select slot 1 (channels
0–3), slot 2 (channels 4–7), or both, set each channel's gain, display the five
device-reported power-rail states, and include manually requested rail states
in `DEVICE_SET_CONFIG`. Serial work runs on a background thread so the window
remains responsive.

The connection selector also supports the first Bluetooth Low Energy slice.
It scans for `Geophys Acquisition`, connects, sends `HELLO`, and displays the
returned device identity. Recording controls and live streaming remain
disabled for BLE connections until those commands are implemented on that
transport.

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
python3 -m pip install -e version_2/software/packages/data \
  -e version_2/software/apps/host
```

Launch the desktop application from the repository root:

```sh
python3 version_2/software/apps/host/launch_host.py
```

Select the device's USB/COM port, leave the baud rate at `921600`, and click
**Connect**. The recordings catalog and current device configuration load
automatically. In the **Config** tab, select **Acquire this slot** for slot 1
to use only channels 0–3, choose any required sample rate and gains, and click
**Apply changes**. Configuration changes are available while recording and
live streaming are stopped. Power rails have separate **Reported** and
**Manual request** columns; the reported state from the device is always
authoritative. The +3.3 VA, +10 V / 9 VA, and -5 VA requests are applied by the
firmware in the Rev-1 power sequence. +5 VA is status-only because Rev-1 has no
software enable. +18 V is pulse-controlled: select a detected magnetic-card
slot and SET or RESET, then click **Send pulse**. The button is available only
while acquisition is stopped and no configuration edits are pending. IMU
fields remain status-only.
ADC settings are runtime state and return to firmware defaults after a device
reboot. Use the **Live Stream** tab to select channels and decimation, then
click **Start live streaming**.

The shared protocol and GUI require both live streaming and recording to be
stopped before applying configuration changes or sending a magnetic pulse.
Recording controls also require the device to report an SD card in the ready
state. If an active recording stops asynchronously, the GUI now reports the
stop explicitly, queries the retained failure through `RECORDING_STOP`, and
identifies conditions such as `storage full` instead of leaving a stale
recording status visible.

For Bluetooth, select **Bluetooth LE**, click **Refresh**, select the advertised
device, and click **Connect**. USB may remain plugged in for power or debugging;
BLE advertising and the `HELLO` exchange remain available at the same time.

The same application can also be launched as a module:

```sh
python3 -m geophys_host
```

The command-line connection probe remains available:

```sh
python3 -m geophys_host.cli hello /dev/cu.usbserial-PORT
```

Scan for BLE devices and perform the same identity request with:

```sh
python3 -m geophys_host.cli ble-scan
python3 -m geophys_host.cli ble-hello DEVICE-IDENTIFIER
```

The earlier command-line live view also remains available:

```sh
python3 -m geophys_host.cli live /dev/cu.usbserial-PORT
```

Capture every validated 512-byte block byte-for-byte while plotting:

```sh
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
python3 -m unittest discover -s version_2/software/apps/host/tests -v
```

## Modules

```text
geophys_host/
├── protocol.py       Fixed 64-byte command codecs
├── adc_record.py     Compatibility imports for the shared data decoder
├── stream_parser.py  Incremental mixed `\CMD`/`\DAT` recovery
├── serial_client.py  Serial connection and named-reply matching
├── ble_client.py     BLE discovery, connection, and HELLO exchange
├── capture.py        Raw validated-record capture
├── live.py           Stream counters, rolling data model, and plot
├── gui.py            Desktop connection, recordings, and live-stream tabs
└── cli.py            Command-line device and live-stream interface

tests/
└── shared vectors, stream fragmentation, and named-reply tests
```

The Python decoder lives in `software/packages/data/geophys_data/` and remains
independent from the C encoder. Both sides consume the byte-exact vectors under
`shared/protocol/test_vectors/`.

## Dependency boundary

- The shared `geophys_data` package and `stream_parser.py` do not open serial
  ports or update a UI.
- The GUI sends device operations to one background serial owner and updates
  Tk widgets only from the GUI thread.
- Connection adapters move bytes and handle transport-specific fragmentation
  without redefining commands or interpreting scientific samples. The client
  permits one outstanding command and waits for its defined reply ID.
- UART and Bluetooth clients use the same command codec. The current BLE client
  intentionally exposes only `HELLO`; recording and sample streaming remain
  USB/UART-only.
- `capture.py` persists only complete, validated records. Session metadata and
  scientific exports remain future work.
- Plotting consumes decoded data and never changes the raw capture bytes.

See `version_2/docs/uart_host_integration_plan.md` for the proposed sequence and
review gates.
