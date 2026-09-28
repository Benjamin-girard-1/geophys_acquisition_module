"""Developer plot of the raw ADC channels in one recording."""

from __future__ import annotations

import argparse
from bisect import bisect_left, bisect_right
from collections import Counter
from collections.abc import Callable
from dataclasses import dataclass
import math
from pathlib import Path
import sys

import numpy as np

from geophys_data import RecordingFileError, iter_adc_records

MAX_PLOT_POINTS = 100_000
DEFAULT_START_TRIM_S = 1.0
DEFAULT_END_TRIM_S = 0.0
ADC_FULL_SCALE_COUNTS = (1 << 23) - 1
FFT_DBFS_FLOOR = -180.0
CHANNEL_SELECTIONS = {
    "all": None,
    "0-3": (0, 1, 2, 3),
    "4-7": (4, 5, 6, 7),
}
STATUS_NAMES = {
    0: "good",
    1: "critical",
    2: "conversion",
    3: "timing",
}


@dataclass(frozen=True)
class RecordingPlotData:
    path: Path
    channel_indices: tuple[int, ...]
    timestamps_s: tuple[float, ...]
    samples: dict[int, tuple[int, ...]]
    record_count: int
    status_counts: dict[int, int]
    payload_discontinuities: int
    sequence_discontinuities: int
    missing_conversions: int


@dataclass(frozen=True)
class FrequencySpectrum:
    frequencies_hz: np.ndarray
    amplitudes: np.ndarray


def default_data_directory() -> Path:
    repository_root = Path(__file__).resolve().parents[5]
    return repository_root.parent / "geophys_acquisition_data"


def _select_recording_file_macos(data_directory: Path) -> str:
    """Use Cocoa directly to avoid Tk file-dialog crashes on macOS."""
    try:
        from AppKit import NSModalResponseOK, NSOpenPanel
        from Foundation import NSURL
    except ImportError as error:
        raise RuntimeError(
            "pyobjc-framework-Cocoa is required for file selection on macOS"
        ) from error

    panel = NSOpenPanel.openPanel()
    panel.setTitle_("Select a Geophys recording")
    panel.setPrompt_("Open")
    panel.setCanChooseFiles_(True)
    panel.setCanChooseDirectories_(False)
    panel.setAllowsMultipleSelection_(False)
    panel.setResolvesAliases_(True)
    panel.setDirectoryURL_(NSURL.fileURLWithPath_(str(data_directory)))

    if panel.runModal() != NSModalResponseOK:
        return ""
    selected_url = panel.URL()
    return "" if selected_url is None else str(selected_url.path())


def _select_recording_file_tk(data_directory: Path) -> str:
    """Cross-platform fallback used when Cocoa is unavailable."""
    try:
        import tkinter
        from tkinter import filedialog
    except ImportError as error:
        raise RuntimeError(
            "Tk is required for the recording file selector"
        ) from error

    root = None
    try:
        root = tkinter.Tk()
        root.withdraw()
        return filedialog.askopenfilename(
            parent=root,
            title="Select a Geophys recording",
            initialdir=str(data_directory),
            filetypes=(("All files", "*"),),
        )
    except tkinter.TclError as error:
        raise RuntimeError(
            f"could not open the recording file selector: {error}"
        ) from error
    finally:
        if root is not None:
            root.destroy()


def select_recording_file(
        dialog: Callable[..., str] | None = None) -> Path | None:
    """Open a file chooser rooted at the external acquisition-data folder."""
    data_directory = default_data_directory()
    if not data_directory.is_dir():
        raise FileNotFoundError(
            f"acquisition data directory not found: {data_directory}"
        )

    if dialog is not None:
        selected = dialog(
            title="Select a Geophys recording",
            initialdir=str(data_directory),
            filetypes=(("All files", "*"),),
        )
    elif sys.platform == "darwin":
        selected = _select_recording_file_macos(data_directory)
    else:
        selected = _select_recording_file_tk(data_directory)

    if not selected:
        return None
    return Path(selected).expanduser().resolve()


def load_recording(path: str | Path) -> RecordingPlotData:
    recording_path = Path(path).expanduser().resolve()
    channel_indices: tuple[int, ...] | None = None
    first_timestamp_100ns: int | None = None
    timestamps_s: list[float] = []
    samples: dict[int, list[int]] = {}
    statuses: Counter[int] = Counter()
    record_count = 0
    payload_discontinuities = 0
    sequence_discontinuities = 0
    missing_conversions = 0
    expected_payload: int | None = None
    expected_sequence: int | None = None

    for record in iter_adc_records(recording_path):
        if channel_indices is None:
            channel_indices = record.channel_indices
            samples = {channel: [] for channel in channel_indices}
        elif record.channel_indices != channel_indices:
            raise RecordingFileError(
                f"channel mask changed at record {record_count}"
            )

        if first_timestamp_100ns is None:
            first_timestamp_100ns = record.first_monotonic_timestamp_100ns
        if expected_payload is not None and \
                record.payload_number != expected_payload:
            payload_discontinuities += 1
        if expected_sequence is not None and \
                record.first_conversion_sequence != expected_sequence:
            sequence_discontinuities += 1
            distance = (
                record.first_conversion_sequence - expected_sequence
            ) & 0xFFFFFFFF
            if distance <= 0x7FFFFFFF:
                missing_conversions += distance

        for conversion_index, conversion in enumerate(record.conversions):
            timestamp_100ns = (
                record.first_monotonic_timestamp_100ns +
                conversion_index * record.sample_period_100ns
            )
            timestamps_s.append(
                (timestamp_100ns - first_timestamp_100ns) / 10_000_000.0
            )
            for channel, value in zip(record.channel_indices, conversion):
                samples[channel].append(value)

        record_count += 1
        statuses[record.status] += 1
        expected_payload = (record.payload_number + 1) & 0xFFFFFFFF
        expected_sequence = (
            record.first_conversion_sequence + len(record.conversions)
        ) & 0xFFFFFFFF

    if channel_indices is None:
        raise RecordingFileError("recording contains no complete records")

    return RecordingPlotData(
        path=recording_path,
        channel_indices=channel_indices,
        timestamps_s=tuple(timestamps_s),
        samples={
            channel: tuple(values) for channel, values in samples.items()
        },
        record_count=record_count,
        status_counts=dict(statuses),
        payload_discontinuities=payload_discontinuities,
        sequence_discontinuities=sequence_discontinuities,
        missing_conversions=missing_conversions,
    )


def format_status_counts(status_counts: dict[int, int]) -> str:
    return ", ".join(
        f"{STATUS_NAMES.get(status, str(status))}={count}"
        for status, count in sorted(status_counts.items())
    )


def print_summary(data: RecordingPlotData) -> None:
    print(f"File: {data.path}")
    print(
        f"Records: {data.record_count} | "
        f"Conversions: {len(data.timestamps_s)} | "
        f"Duration: {data.timestamps_s[-1]:.3f} s"
    )
    print(f"Record status: {format_status_counts(data.status_counts)}")
    print(
        f"Payload discontinuities: {data.payload_discontinuities} | "
        f"Sequence discontinuities: {data.sequence_discontinuities} | "
        f"Missing conversions: {data.missing_conversions}"
    )


def resolve_plot_channels(
        data: RecordingPlotData, selection: str) -> tuple[int, ...]:
    requested = CHANNEL_SELECTIONS[selection]
    if requested is None:
        return data.channel_indices

    unavailable = tuple(
        channel for channel in requested
        if channel not in data.channel_indices
    )
    if unavailable:
        formatted = ", ".join(f"CH{channel}" for channel in unavailable)
        raise RecordingFileError(
            f"selected channel group {selection} is not present in the "
            f"recording: {formatted}"
        )
    return requested


def valid_sample_bounds(
        data: RecordingPlotData,
        start_trim_s: float = DEFAULT_START_TRIM_S,
        end_trim_s: float = DEFAULT_END_TRIM_S) -> tuple[int, int]:
    """Return the half-open sample range left after time trimming."""
    if not math.isfinite(start_trim_s) or start_trim_s < 0.0:
        raise RecordingFileError("start trim must be a non-negative number")
    if not math.isfinite(end_trim_s) or end_trim_s < 0.0:
        raise RecordingFileError("end trim must be a non-negative number")

    valid_start = bisect_left(data.timestamps_s, start_trim_s)
    end_time_s = data.timestamps_s[-1] - end_trim_s
    valid_stop = bisect_right(data.timestamps_s, end_time_s)
    if valid_stop - valid_start < 2:
        raise RecordingFileError(
            "trim settings leave fewer than two samples to plot"
        )
    return valid_start, valid_stop


def calculate_frequency_spectrum(
        data: RecordingPlotData, channel: int,
        start_trim_s: float = DEFAULT_START_TRIM_S,
        end_trim_s: float = DEFAULT_END_TRIM_S) -> FrequencySpectrum:
    """Return a Hann-windowed, single-sided amplitude spectrum."""
    if channel not in data.channel_indices:
        raise RecordingFileError(
            f"CH{channel} is not present in the recording"
        )
    valid_start, valid_stop = valid_sample_bounds(
        data,
        start_trim_s,
        end_trim_s,
    )
    timestamps = np.asarray(
        data.timestamps_s[valid_start:valid_stop],
        dtype=float,
    )
    samples = np.asarray(
        data.samples[channel][valid_start:valid_stop],
        dtype=float,
    )
    sample_intervals = np.diff(timestamps)
    positive_intervals = sample_intervals[sample_intervals > 0.0]
    if positive_intervals.size == 0:
        raise RecordingFileError(
            "recording timestamps do not contain a positive sample interval"
        )

    sample_interval_s = float(np.median(positive_intervals))
    window = np.hanning(samples.size)
    window_sum = float(np.sum(window))
    if window_sum == 0.0:
        window = np.ones(samples.size)
        window_sum = float(samples.size)
    weighted_mean = float(np.sum(samples * window) / window_sum)
    centered_samples = samples - weighted_mean

    amplitudes = (
        2.0 * np.abs(np.fft.rfft(centered_samples * window)) / window_sum
    )
    amplitudes[0] *= 0.5
    if samples.size % 2 == 0:
        amplitudes[-1] *= 0.5
    frequencies_hz = np.fft.rfftfreq(
        samples.size,
        d=sample_interval_s,
    )
    return FrequencySpectrum(frequencies_hz, amplitudes)


def amplitudes_to_dbfs(amplitudes: np.ndarray) -> np.ndarray:
    """Convert peak amplitudes in ADC counts to decibels full scale."""
    minimum_ratio = 10.0 ** (FFT_DBFS_FLOOR / 20.0)
    ratios = np.maximum(amplitudes / ADC_FULL_SCALE_COUNTS, minimum_ratio)
    return 20.0 * np.log10(ratios)


class RecordingPlotWindow:
    """Interactive waveform and FFT viewer for one recording."""

    def __init__(self, data: RecordingPlotData,
                 waveform_channels: tuple[int, ...],
                 fft_channels: tuple[int, ...],
                 start_trim_s: float = DEFAULT_START_TRIM_S,
                 end_trim_s: float = DEFAULT_END_TRIM_S) -> None:
        from matplotlib import pyplot as plt
        from matplotlib.widgets import (
            Button,
            CheckButtons,
            RadioButtons,
            TextBox,
        )

        self.data = data
        valid_sample_bounds(data, start_trim_s, end_trim_s)
        self.start_trim_s = start_trim_s
        self.end_trim_s = end_trim_s
        self._plt = plt
        self.figure = plt.figure(figsize=(14, 8.5))
        self.figure.subplots_adjust(
            left=0.075,
            right=0.75,
            top=0.91,
            bottom=0.08,
            hspace=0.34,
        )
        grid = self.figure.add_gridspec(2, 1)
        self.waveform_axis = self.figure.add_subplot(grid[0, 0])
        self.fft_axis = self.figure.add_subplot(grid[1, 0])

        try:
            self.figure.canvas.manager.set_window_title(
                f"Geophys recording - {data.path.name}"
            )
        except AttributeError:
            pass

        self._channel_labels = tuple(
            f"CH{channel}" for channel in data.channel_indices
        )
        self._label_channels = dict(
            zip(self._channel_labels, data.channel_indices)
        )
        self._waveform_lines: dict[int, object] = {}
        self._fft_lines: dict[int, object] = {}
        self._fft_spectra: dict[int, FrequencySpectrum] = {}
        self._fft_scale = "dbfs"
        self._plot_all_channels(waveform_channels, fft_channels)

        self._waveform_empty_text = self.waveform_axis.text(
            0.5,
            0.5,
            "Select a waveform channel",
            ha="center",
            va="center",
            transform=self.waveform_axis.transAxes,
        )
        self._fft_empty_text = self.fft_axis.text(
            0.5,
            0.5,
            "Select an FFT channel",
            ha="center",
            va="center",
            transform=self.fft_axis.transAxes,
        )

        self._add_information_panel()
        waveform_box_axis = self.figure.add_axes(
            (0.79, 0.59, 0.17, 0.16),
            title="Waveform channels",
        )
        fft_box_axis = self.figure.add_axes(
            (0.79, 0.31, 0.17, 0.16),
            title="FFT channels",
        )
        waveform_states = tuple(
            channel in waveform_channels for channel in data.channel_indices
        )
        fft_states = tuple(
            channel in fft_channels for channel in data.channel_indices
        )
        self.waveform_checkboxes = CheckButtons(
            waveform_box_axis,
            self._channel_labels,
            waveform_states,
        )
        self.fft_checkboxes = CheckButtons(
            fft_box_axis,
            self._channel_labels,
            fft_states,
        )
        self.waveform_checkboxes.on_clicked(self._toggle_waveform)
        self.fft_checkboxes.on_clicked(self._toggle_fft)

        self._waveform_all_button = Button(
            self.figure.add_axes((0.79, 0.525, 0.078, 0.04)),
            "All",
        )
        self._waveform_none_button = Button(
            self.figure.add_axes((0.882, 0.525, 0.078, 0.04)),
            "None",
        )
        self._fft_all_button = Button(
            self.figure.add_axes((0.79, 0.245, 0.078, 0.04)),
            "All",
        )
        self._fft_none_button = Button(
            self.figure.add_axes((0.882, 0.245, 0.078, 0.04)),
            "None",
        )
        self._waveform_all_button.on_clicked(
            lambda _event: self._set_all(self.waveform_checkboxes, True)
        )
        self._waveform_none_button.on_clicked(
            lambda _event: self._set_all(self.waveform_checkboxes, False)
        )
        self._fft_all_button.on_clicked(
            lambda _event: self._set_all(self.fft_checkboxes, True)
        )
        self._fft_none_button.on_clicked(
            lambda _event: self._set_all(self.fft_checkboxes, False)
        )
        self.fft_scale_selector = RadioButtons(
            self.figure.add_axes(
                (0.79, 0.135, 0.17, 0.075),
                title="FFT scale",
            ),
            ("dBFS (log)", "Linear"),
            active=0,
        )
        self.fft_scale_selector.on_clicked(self._set_fft_scale)
        self.trim_start_box = TextBox(
            self.figure.add_axes((0.88, 0.075, 0.08, 0.032)),
            "Trim start (s) ",
            initial=f"{self.start_trim_s:g}",
        )
        self.trim_end_box = TextBox(
            self.figure.add_axes((0.88, 0.035, 0.08, 0.032)),
            "Trim end (s) ",
            initial=f"{self.end_trim_s:g}",
        )
        self._trim_status = self.figure.text(
            0.79,
            0.01,
            "Press Enter to apply trim",
            fontsize=7.5,
            color="0.35",
        )
        self.trim_start_box.on_submit(self._apply_trim)
        self.trim_end_box.on_submit(self._apply_trim)

        self._refresh_axis(
            self.waveform_axis,
            self._waveform_lines,
            self._waveform_empty_text,
        )
        self._refresh_axis(
            self.fft_axis,
            self._fft_lines,
            self._fft_empty_text,
        )
        self.figure.suptitle(
            f"{data.path.name} — recording analysis",
            fontsize=14,
            fontweight="bold",
        )

    def _plot_all_channels(
            self, waveform_channels: tuple[int, ...],
            fft_channels: tuple[int, ...]) -> None:
        colors = self._plt.get_cmap("tab10")

        for color_index, channel in enumerate(self.data.channel_indices):
            color = colors(color_index % 10)
            waveform_line, = self.waveform_axis.plot(
                (),
                (),
                color=color,
                linewidth=0.8,
                label=f"CH{channel}",
                visible=channel in waveform_channels,
            )
            fft_line, = self.fft_axis.plot(
                (),
                (),
                color=color,
                linewidth=0.9,
                label=f"CH{channel}",
                visible=channel in fft_channels,
            )
            self._waveform_lines[channel] = waveform_line
            self._fft_lines[channel] = fft_line

        self.waveform_axis.set_xlabel(
            "Device monotonic time since first sample (s)"
        )
        self.waveform_axis.set_ylabel("Raw ADC counts")
        self.waveform_axis.grid(True, alpha=0.25)
        self.fft_axis.set_xlabel("Frequency (Hz)")
        self.fft_axis.set_ylabel("Amplitude (dBFS)")
        self.fft_axis.grid(True, alpha=0.25)
        self._update_trimmed_plots(redraw=False)

    def _update_trimmed_plots(self, redraw: bool = True) -> None:
        valid_start, valid_stop = valid_sample_bounds(
            self.data,
            self.start_trim_s,
            self.end_trim_s,
        )
        point_count = valid_stop - valid_start
        stride = max(1, math.ceil(point_count / MAX_PLOT_POINTS))
        timestamps = self.data.timestamps_s[
            valid_start:valid_stop:stride
        ]
        trim_description = (
            f"trim {self.start_trim_s:g} s start / "
            f"{self.end_trim_s:g} s end"
        )

        for channel in self.data.channel_indices:
            self._waveform_lines[channel].set_data(
                timestamps,
                self.data.samples[channel][valid_start:valid_stop:stride],
            )
            spectrum = calculate_frequency_spectrum(
                self.data,
                channel,
                self.start_trim_s,
                self.end_trim_s,
            )
            self._fft_spectra[channel] = spectrum
            self._fft_lines[channel].set_data(
                spectrum.frequencies_hz[1:],
                amplitudes_to_dbfs(spectrum.amplitudes[1:])
                if self._fft_scale == "dbfs"
                else spectrum.amplitudes[1:],
            )

        self.waveform_axis.set_title(f"Waveforms ({trim_description})")
        self.fft_axis.set_title(
            f"Frequency spectrum ({trim_description}, Hann window)"
        )
        if redraw:
            self._refresh_axis(
                self.waveform_axis,
                self._waveform_lines,
                self._waveform_empty_text,
            )
            self._refresh_axis(
                self.fft_axis,
                self._fft_lines,
                self._fft_empty_text,
            )

    def _add_information_panel(self) -> None:
        duration = self.data.timestamps_s[-1]
        status_lines = "\n".join(
            f"  {STATUS_NAMES.get(status, str(status))}: {count}"
            for status, count in sorted(self.data.status_counts.items())
        )
        information = (
            f"Records: {self.data.record_count}\n"
            f"Conversions: {len(self.data.timestamps_s)}\n"
            f"Duration: {duration:.3f} s\n"
            f"Status:\n{status_lines}\n"
            f"Missing: {self.data.missing_conversions}"
        )
        self.figure.text(
            0.79,
            0.94,
            information,
            va="top",
            fontsize=8.5,
            linespacing=1.15,
        )

    def _toggle_waveform(self, label: str) -> None:
        channel = self._label_channels[label]
        index = self.data.channel_indices.index(channel)
        self._waveform_lines[channel].set_visible(
            self.waveform_checkboxes.get_status()[index]
        )
        self._refresh_axis(
            self.waveform_axis,
            self._waveform_lines,
            self._waveform_empty_text,
        )

    def _toggle_fft(self, label: str) -> None:
        channel = self._label_channels[label]
        index = self.data.channel_indices.index(channel)
        self._fft_lines[channel].set_visible(
            self.fft_checkboxes.get_status()[index]
        )
        self._refresh_axis(
            self.fft_axis,
            self._fft_lines,
            self._fft_empty_text,
        )

    def _set_fft_scale(self, label: str) -> None:
        self._fft_scale = "dbfs" if label == "dBFS (log)" else "linear"
        for channel, line in self._fft_lines.items():
            amplitudes = self._fft_spectra[channel].amplitudes[1:]
            if self._fft_scale == "dbfs":
                line.set_ydata(amplitudes_to_dbfs(amplitudes))
            else:
                line.set_ydata(amplitudes)
        self.fft_axis.set_ylabel(
            "Amplitude (dBFS)"
            if self._fft_scale == "dbfs"
            else "Amplitude (ADC counts)"
        )
        self._refresh_axis(
            self.fft_axis,
            self._fft_lines,
            self._fft_empty_text,
        )

    def _apply_trim(self, _submitted_text: str) -> None:
        try:
            start_trim_s = float(self.trim_start_box.text.strip())
            end_trim_s = float(self.trim_end_box.text.strip())
            valid_sample_bounds(self.data, start_trim_s, end_trim_s)
        except (ValueError, RecordingFileError) as error:
            self._trim_status.set_text(f"Invalid trim: {error}")
            self._trim_status.set_color("crimson")
            self.figure.canvas.draw_idle()
            return

        self.start_trim_s = start_trim_s
        self.end_trim_s = end_trim_s
        self._update_trimmed_plots()
        self._trim_status.set_text(
            f"Applied: {start_trim_s:g} s start, {end_trim_s:g} s end"
        )
        self._trim_status.set_color("darkgreen")
        self.figure.canvas.draw_idle()

    def _set_all(self, checkboxes: object, selected: bool) -> None:
        for index, current in enumerate(checkboxes.get_status()):
            if current != selected:
                checkboxes.set_active(index)

    def _refresh_axis(self, axis: object, lines: dict[int, object],
                      empty_text: object) -> None:
        visible_lines = [line for line in lines.values() if line.get_visible()]
        empty_text.set_visible(not visible_lines)
        current_legend = axis.get_legend()
        if current_legend is not None:
            current_legend.remove()
        if visible_lines:
            axis.legend(handles=visible_lines, loc="upper right", ncols=2)
            axis.relim(visible_only=True)
            axis.autoscale_view(scalex=True, scaley=True)
        self.figure.canvas.draw_idle()


def show_plot(data: RecordingPlotData,
              channel_indices: tuple[int, ...] | None = None,
              fft_channel_indices: tuple[int, ...] | None = None) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise RuntimeError("matplotlib is required to plot recordings") from error

    waveform_channels = (
        data.channel_indices if channel_indices is None else channel_indices
    )
    fft_channels = (
        waveform_channels
        if fft_channel_indices is None
        else fft_channel_indices
    )
    if not waveform_channels:
        raise RecordingFileError("no channels selected for plotting")
    unavailable = tuple(
        channel for channel in waveform_channels + fft_channels
        if channel not in data.channel_indices
    )
    if unavailable:
        formatted = ", ".join(f"CH{channel}" for channel in unavailable)
        raise RecordingFileError(
            f"selected channels are not present in the recording: {formatted}"
        )
    window = RecordingPlotWindow(
        data,
        waveform_channels,
        fft_channels,
    )
    window.figure._geophys_plot_window = window
    plt.show()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Plot a raw Geophys SD-card recording"
    )
    parser.add_argument(
        "recording",
        nargs="?",
        type=Path,
        help="recording to plot; omit to select one from the data directory",
    )
    parser.add_argument(
        "--channels",
        choices=tuple(CHANNEL_SELECTIONS),
        default="0-3",
        help="initial channel selection for both plots (default: 0-3)",
    )
    arguments = parser.parse_args(argv)
    path = arguments.recording or select_recording_file()
    if path is None:
        print("No recording selected.")
        return 0
    data = load_recording(path)
    print_summary(data)
    show_plot(data, resolve_plot_channels(data, arguments.channels))
    return 0


def entrypoint() -> int:
    try:
        return main()
    except (OSError, RecordingFileError, RuntimeError) as error:
        print(f"geophys-plot: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(entrypoint())
