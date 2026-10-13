# Geophys Developer Tools

These developer-only diagnostics validate a raw SD recording, print a short
continuity summary, and open an interactive waveform and FFT viewer. They are
separate from the future production MTH5 builder.

New recordings start with a 4096-byte printable JSON descriptor. It includes
the gain for each channel and the repository URL, and can be inspected at the
start of the file in a text editor. Checkpoints and ADC samples after the
descriptor are binary. The tools also accept older DAT-only recordings such
as `field021`; the superseded binary `\GEO` header is unsupported.

Set up the local environment from the repository root:

```sh
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -e version_2/software/packages/data \
  -e version_2/software/apps/dev_tools
```

From the repository root, launch the tool:

```sh
.venv/bin/python version_2/software/apps/dev_tools/launch_plot.py
```

A native Qt file chooser opens in the sibling `geophys_acquisition_data/`
directory. Canceling it exits without opening a plot.

Pass a recording path to plot a different file:

```sh
.venv/bin/python version_2/software/apps/dev_tools/launch_plot.py /path/to/recording
```

Channels 0 through 3 are plotted by default:

```sh
.venv/bin/python version_2/software/apps/dev_tools/launch_plot.py \
  /path/to/recording
```

The `--channels` choices are `all`, `0-3`, and `4-7`. This option chooses the
initial selection for both plots. Use `--channels all` to initially show every
recorded channel. When the recording path is omitted, the native file chooser
opens and channels 0–3 are initially selected.

The Qt viewer gives most of the window to the waveform and shows the FFT below
it. Both plots have their own zoom and channel checkboxes. The highlighted
region on the waveform is the exact input to the FFT; drag either edge to trim
it, or edit **Trim start (s)** and **Trim end (s)**. The defaults exclude 50
seconds from the start and 10 seconds from the end. A recording shorter than
60 seconds uses the full range and says so in the status line. **FFT window**
defaults to Hann, which tapers the selected interval to reduce leakage from
tones that do not complete an exact number of cycles. Choose **Rectangular
(equal weight)** when comparing a short event while moving a fixed-width
selection: every included sample then has the same weight. A rectangular
window can spread strong tones into neighboring frequencies. Both modes remove
the window-weighted mean before SciPy's real FFT and display single-sided peak
amplitude. For recordings whose JSON header identifies a magnetic card, the
waveform and FFT default to nominal nT. **Waveform units** can switch to raw ADC
counts; **FFT amplitude** can switch to raw ADC counts or dBFS. Only the three
magnetic axes on each magnetic card use the nT scale. The fourth channel is a
thermistor and is disabled in nT mode; its checkbox selection returns when
switching back to counts or dBFS. DAT-only recordings without gain/card metadata
remain in ADC counts and dBFS.
**Waveform Y fit** starts with the selected FFT region so
startup transients do not flatten the rest of the waveform; choose **Full
recording** to include every sample in the vertical scale. The heading reports
non-good record-status counts.

Every FFT bin is retained for analysis, including 0 Hz. The FFT uses exactly
the selected sample count: it does not decimate, average, smooth, interpolate,
truncate, or zero-pad the spectrum. Both plots draw each dense group's
minimum and maximum, preserving brief events, narrow peaks, and spectral
valleys. At close zoom they draw the exact samples or FFT bins.
The status line reports
the exact FFT bin count and frequency resolution. A trim recomputes FFTs in a
worker after the drag finishes; frequency zoom remains in place. Recordings
with large timestamp gaps or sample-rate changes are rejected for FFT analysis
instead of being assigned misleading uniformly spaced bins.

The recording is read into contiguous NumPy arrays but never modified. The
nominal magnetic scale uses the recorded ADC gain for each channel, the AD7779
2.5 V reference and signed 24-bit range, the magnetic board's 51× INA851 gain,
the assumed 5.6 V bridge excitation, and the sensor's 3.2 mV/V/G sensitivity:

```text
nT/count = (2.5 V × 100,000 nT/G) /
           (2^23 × ADC gain × 51 × 5.6 V × 0.0032 V/V/G)
```

At ADC gain 8 this is about 0.004076 nT/count. This is a nominal
field-equivalent scale, not an absolute magnetic field calibration. The
magnetic board's balancing servo removes DC and very slow field components;
sensor offsets, axis polarity, analog gain tolerance, actual bridge voltage,
and frequency response still need measurement. The FFT nT option is
single-sided *peak* amplitude of the selected interval after mean removal.

The waveform plot shows the full recording, including the samples outside the
highlighted FFT region. Display points are selected again whenever the time or
frequency view changes size or zoom. FFT calculations use every sample in the
selected time range. Only selected FFT channels are calculated, and multiple
selected channels are transformed together.

## Convert a binary recording to CSV

Convert a validated recording from the portable 512-byte block format:

```sh
.venv/bin/python version_2/software/apps/dev_tools/launch_convert.py \
  ../geophys_acquisition_data/scratch/V2_noise_test
```

By default, the CSV is written to the external
`geophys_acquisition_data/scratch/` directory and is therefore kept outside
the source repository. Existing files are not overwritten. Use `--force` to
replace one, or `--output PATH` to choose another destination:

```sh
.venv/bin/python version_2/software/apps/dev_tools/launch_convert.py \
  /path/to/recording --output /path/to/output.csv
```

The CSV has one row per simultaneous ADC conversion. It preserves the relative
and raw monotonic timestamps, conversion sequence, record and payload numbers,
record status, channel mask, packed gain, sample period, and signed raw counts
for channels 0 through 7. Unrecorded channels are left empty. Counts are not
converted to volts because no physical calibration is defined yet.
