# Geophys Developer Tools

These developer-only diagnostics validate a raw SD recording, print a short
continuity summary, and open an interactive waveform and FFT viewer. They are
separate from the future production MTH5 builder.

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

A native file chooser opens in the sibling `geophys_acquisition_data/`
directory. On macOS it uses Cocoa directly instead of Tk so directory
navigation is reliable. Canceling it exits without opening a plot.

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

The viewer has separate channel checkbox groups for the waveform and FFT, so
each plot can display a different channel combination. The **All** and **None**
buttons below either group provide quick selection. FFTs use a Hann window and
display single-sided amplitude. The FFT defaults to a logarithmic dBFS scale,
omits the exact 0 Hz bin, and can be switched back to linear ADC counts with
the **FFT scale** control. The editable **Trim start (s)** and **Trim end (s)**
fields control how much data is excluded from both plots and the FFT. Press
Enter in either field to apply the values. They default to 1 second at the
start and 0 seconds at the end.

The recording is read but never modified. The plot displays raw signed 24-bit
ADC counts because physical channel names and calibration are not defined yet.

The waveform is reduced to at most 100,000 displayed points for interactive
performance. FFT calculations always use every sample in the selected time
range; display reduction is never applied to spectral analysis.

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
