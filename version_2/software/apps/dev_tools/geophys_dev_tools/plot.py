"""Developer plot of the raw ADC channels in one recording."""

from __future__ import annotations

import argparse
from collections import Counter
from collections.abc import Callable
from dataclasses import dataclass
import math
from pathlib import Path
import sys

import numpy as np
from numpy.typing import NDArray
from scipy import fft as scipy_fft
from scipy.signal import windows

from geophys_recording import (
    RecordingFileError, RecordingHeader, iter_adc_records,
    read_recording_header,
)
from .paths import acquisition_data_directory

DEFAULT_START_TRIM_S = 1.0
DEFAULT_END_TRIM_S = 0.0
ADC_FULL_SCALE_COUNTS = (1 << 23) - 1
ADC_BIPOLAR_CODES = 1 << 23
ADC_REFERENCE_V = 2.5
MAGNETIC_BRIDGE_V = 5.6
MAGNETIC_SENSITIVITY_V_PER_V_PER_G = 0.0032
MAGNETIC_INA_GAIN = 51.0
NANOTESLA_PER_GAUSS = 100_000.0
THERMISTOR_R25_OHM = 100_000.0
THERMISTOR_B25_50_K = 4250.0
THERMISTOR_BRIDGE_RESISTOR_OHM = 100_000.0
THERMISTOR_BRIDGE_SUPPLY_V = 3.3
FFT_DBFS_FLOOR = -180.0
CHANNEL_SELECTIONS = {
    "all": None,
    "0-2": (0, 1, 2),
    "3": (3,),
    "0-3": (0, 1, 2, 3),
    "4-7": (4, 5, 6, 7),
}
STATUS_NAMES = {
    0: "good",
    1: "critical",
    2: "conversion",
    3: "timing",
}
_qt_application = None


@dataclass(frozen=True)
class RecordingPlotData:
    path: Path
    channel_indices: tuple[int, ...]
    timestamps_s: NDArray[np.float64]
    samples: dict[int, NDArray[np.int32]]
    record_count: int
    status_counts: dict[int, int]
    payload_discontinuities: int
    sequence_discontinuities: int
    missing_conversions: int
    sample_period_s: float | None = None
    recording_header: RecordingHeader | None = None

    def __post_init__(self) -> None:
        """Normalize recording data into compact, immutable NumPy arrays."""
        timestamps_s = np.ascontiguousarray(self.timestamps_s, dtype=np.float64)
        if timestamps_s.ndim != 1:
            raise ValueError("timestamps_s must be one-dimensional")
        if timestamps_s.size and not np.all(np.isfinite(timestamps_s)):
            raise ValueError("timestamps_s must contain only finite values")
        if timestamps_s.size > 1 and np.any(np.diff(timestamps_s) < 0.0):
            raise ValueError("timestamps_s must be monotonically increasing")
        if self.sample_period_s is not None and (
                not math.isfinite(self.sample_period_s) or
                self.sample_period_s <= 0.0):
            raise ValueError("sample_period_s must be positive and finite")

        normalized_samples: dict[int, NDArray[np.int32]] = {}
        for channel in self.channel_indices:
            if channel not in self.samples:
                raise ValueError(f"samples are missing CH{channel}")
            values = np.ascontiguousarray(
                self.samples[channel],
                dtype=np.int32,
            )
            if values.ndim != 1:
                raise ValueError(f"CH{channel} samples must be one-dimensional")
            if values.size != timestamps_s.size:
                raise ValueError(
                    f"CH{channel} has {values.size} samples but "
                    f"there are {timestamps_s.size} timestamps"
                )
            values.setflags(write=False)
            normalized_samples[channel] = values

        timestamps_s.setflags(write=False)
        object.__setattr__(self, "timestamps_s", timestamps_s)
        object.__setattr__(self, "samples", normalized_samples)


@dataclass(frozen=True)
class FrequencySpectrum:
    frequencies_hz: NDArray[np.float64]
    amplitudes: NDArray[np.float64]
    sample_rate_hz: float
    sample_count: int

    @property
    def bin_width_hz(self) -> float:
        if self.frequencies_hz.size < 2:
            return 0.0
        return float(self.frequencies_hz[1] - self.frequencies_hz[0])


def default_data_directory() -> Path:
    return acquisition_data_directory()


def _get_qt_application():
    """Keep one Qt application alive for the file dialog and plot window."""
    global _qt_application
    try:
        from PySide6.QtWidgets import QApplication
    except ImportError as error:
        raise RuntimeError(
            "PySide6-Essentials is required for the recording viewer"
        ) from error
    _qt_application = QApplication.instance() or QApplication(sys.argv[:1])
    return _qt_application


def _select_recording_file_qt(data_directory: Path) -> str:
    from PySide6.QtWidgets import QFileDialog

    _get_qt_application()
    selected, _filter = QFileDialog.getOpenFileName(
        None,
        "Select a Geophys recording",
        str(data_directory),
        "All files (*)",
    )
    return selected


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
    else:
        selected = _select_recording_file_qt(data_directory)

    if not selected:
        return None
    return Path(selected).expanduser().resolve()


def load_recording(path: str | Path) -> RecordingPlotData:
    recording_path = Path(path).expanduser().resolve()
    recording_header = read_recording_header(recording_path)
    channel_indices: tuple[int, ...] | None = None
    first_timestamp_100ns: int | None = None
    timestamp_blocks: list[NDArray[np.float64]] = []
    sample_blocks: list[NDArray[np.int32]] = []
    statuses: Counter[int] = Counter()
    record_count = 0
    payload_discontinuities = 0
    sequence_discontinuities = 0
    missing_conversions = 0
    expected_payload: int | None = None
    expected_sequence: int | None = None
    sample_period_100ns: int | None = None

    for record in iter_adc_records(recording_path):
        if channel_indices is None:
            channel_indices = record.channel_indices
        elif record.channel_indices != channel_indices:
            raise RecordingFileError(
                f"channel mask changed at record {record_count}"
            )

        if first_timestamp_100ns is None:
            first_timestamp_100ns = record.first_monotonic_timestamp_100ns
        if sample_period_100ns is None:
            sample_period_100ns = record.sample_period_100ns
        elif record.sample_period_100ns != sample_period_100ns:
            raise RecordingFileError(
                f"sample period changed at record {record_count}"
            )
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

        conversion_count = len(record.conversions)
        timestamp_blocks.append(
            (
                record.first_monotonic_timestamp_100ns -
                first_timestamp_100ns +
                np.arange(conversion_count, dtype=np.float64) *
                record.sample_period_100ns
            ) / 10_000_000.0
        )
        conversion_block = np.asarray(record.conversions, dtype=np.int32)
        if conversion_block.shape != (
                conversion_count, len(record.channel_indices)):
            raise RecordingFileError(
                f"invalid conversion shape at record {record_count}: "
                f"{conversion_block.shape}"
            )
        # Channel-major storage makes each final channel array contiguous.
        sample_blocks.append(conversion_block.T)

        record_count += 1
        statuses[record.status] += 1
        expected_payload = (record.payload_number + 1) & 0xFFFFFFFF
        expected_sequence = (
            record.first_conversion_sequence + len(record.conversions)
        ) & 0xFFFFFFFF

    if channel_indices is None:
        raise RecordingFileError("recording contains no complete records")
    assert sample_period_100ns is not None

    timestamps_s = np.concatenate(timestamp_blocks)
    sample_matrix = np.concatenate(sample_blocks, axis=1)

    return RecordingPlotData(
        path=recording_path,
        channel_indices=channel_indices,
        timestamps_s=timestamps_s,
        samples={
            channel: sample_matrix[index]
            for index, channel in enumerate(channel_indices)
        },
        record_count=record_count,
        status_counts=dict(statuses),
        payload_discontinuities=payload_discontinuities,
        sequence_discontinuities=sequence_discontinuities,
        missing_conversions=missing_conversions,
        sample_period_s=sample_period_100ns / 10_000_000.0,
        recording_header=recording_header,
    )


def magnetic_nt_per_count(
        header: RecordingHeader | None, channel: int) -> float | None:
    """Nominal field-equivalent scale for an HMC100x magnetic axis.

    The bridge balancing servo removes DC and very slow fields, so this is
    only an approximate scale for field changes in the measurement band.
    """
    if header is None or not 0 <= channel < len(header.adc_gains):
        return None
    slot = header.channel_slots[channel]
    if not 1 <= slot <= len(header.card_types) or \
            header.card_types[slot - 1] != 1 or \
            header.channel_inputs[channel] >= 3:
        return None
    return (
        ADC_REFERENCE_V * NANOTESLA_PER_GAUSS /
        (ADC_BIPOLAR_CODES * header.adc_gains[channel] *
         MAGNETIC_INA_GAIN * MAGNETIC_BRIDGE_V *
         MAGNETIC_SENSITIVITY_V_PER_V_PER_G)
    )


def thermistor_celsius_from_counts(
        counts: np.ndarray | float, gain: int = 1) -> np.ndarray:
    """Nominal Rev-1 NCU18WF104D60RB bridge temperature from AD7779 codes.

    TH+ is the R23/NTC divider, TH- is the R21/R22 half-supply divider.
    The 1 k series resistors do not alter the ideal DC bridge voltage.
    """
    if gain <= 0:
        raise ValueError("ADC gain must be positive")
    voltage = np.asarray(counts, dtype=np.float64) * (
        ADC_REFERENCE_V / (ADC_BIPOLAR_CODES * gain)
    )
    divider_fraction = 0.5 + voltage / THERMISTOR_BRIDGE_SUPPLY_V
    with np.errstate(divide="ignore", invalid="ignore"):
        resistance = (THERMISTOR_BRIDGE_RESISTOR_OHM * divider_fraction /
                      (1.0 - divider_fraction))
        kelvin = 1.0 / (
            1.0 / 298.15 +
            np.log(resistance / THERMISTOR_R25_OHM) / THERMISTOR_B25_50_K
        )
    return np.where((divider_fraction > 0.0) &
                    (divider_fraction < 1.0), kelvin - 273.15, np.nan)


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

    valid_start = int(np.searchsorted(
        data.timestamps_s,
        start_trim_s,
        side="left",
    ))
    end_time_s = data.timestamps_s[-1] - end_trim_s
    valid_stop = int(np.searchsorted(
        data.timestamps_s,
        end_time_s,
        side="right",
    ))
    if valid_stop - valid_start < 2:
        raise RecordingFileError(
            "trim settings leave fewer than two samples to plot"
        )
    return valid_start, valid_stop


def _uniform_sample_interval_s(
        timestamps_s: NDArray[np.float64],
        nominal_interval_s: float | None = None) -> float:
    """Return the sample interval, rejecting data invalid for a regular FFT."""
    sample_intervals = np.diff(timestamps_s)
    if sample_intervals.size == 0 or np.any(sample_intervals <= 0.0):
        raise RecordingFileError(
            "FFT requires strictly increasing recording timestamps"
        )

    sample_interval_s = (
        float(np.median(sample_intervals))
        if nominal_interval_s is None
        else nominal_interval_s
    )
    floating_point_tolerance = max(
        sample_interval_s * 0.1,
        16.0 * np.finfo(np.float64).eps *
        max(1.0, float(np.max(np.abs(timestamps_s)))),
    )
    if np.any(
            np.abs(sample_intervals - sample_interval_s) >
            floating_point_tolerance):
        raise RecordingFileError(
            "FFT requires uniformly sampled data; the selected range "
            "contains a large timestamp gap or sample-rate change"
        )
    return sample_interval_s


def calculate_frequency_spectra(
        data: RecordingPlotData,
        channels: tuple[int, ...],
        start_trim_s: float = DEFAULT_START_TRIM_S,
        end_trim_s: float = DEFAULT_END_TRIM_S,
        window_type: str = "hann") -> dict[int, FrequencySpectrum]:
    """Return full-bin spectra using SciPy's real FFT.

    Channels are transformed as one batch so the window, frequency bins, and
    worker pool are shared. The transform length always equals the selected
    sample count: there is no zero padding, truncation, bin averaging, or
    display-oriented FFT decimation.
    """
    if window_type not in ("hann", "rectangular"):
        raise ValueError(f"unsupported FFT window: {window_type}")
    unavailable = tuple(
        channel for channel in channels
        if channel not in data.channel_indices
    )
    if unavailable:
        formatted = ", ".join(f"CH{channel}" for channel in unavailable)
        raise RecordingFileError(
            f"selected FFT channels are not present: {formatted}"
        )
    if not channels:
        return {}

    valid_start, valid_stop = valid_sample_bounds(
        data,
        start_trim_s,
        end_trim_s,
    )
    timestamps = data.timestamps_s[valid_start:valid_stop]
    sample_interval_s = _uniform_sample_interval_s(
        timestamps,
        data.sample_period_s,
    )
    sample_count = timestamps.size
    window = (
        windows.hann(sample_count, sym=True)
        if window_type == "hann"
        else np.ones(sample_count, dtype=np.float64)
    )
    window_sum = float(np.sum(window))
    if window_sum == 0.0:
        window = np.ones(sample_count, dtype=np.float64)
        window_sum = float(sample_count)

    working_samples = np.empty(
        (len(channels), sample_count),
        dtype=np.float64,
    )
    for row, channel in enumerate(channels):
        np.copyto(
            working_samples[row],
            data.samples[channel][valid_start:valid_stop],
            casting="safe",
        )

    weighted_means = working_samples @ window / window_sum
    working_samples -= weighted_means[:, np.newaxis]
    working_samples *= window
    transformed = scipy_fft.rfft(
        working_samples,
        axis=1,
        overwrite_x=True,
        workers=-1,
    )
    amplitudes = np.abs(transformed)
    amplitudes *= 2.0 / window_sum
    amplitudes[:, 0] *= 0.5
    if sample_count % 2 == 0:
        amplitudes[:, -1] *= 0.5
    frequencies_hz = scipy_fft.rfftfreq(
        sample_count,
        d=sample_interval_s,
    )
    frequencies_hz.setflags(write=False)

    spectra: dict[int, FrequencySpectrum] = {}
    sample_rate_hz = 1.0 / sample_interval_s
    for row, channel in enumerate(channels):
        channel_amplitudes = amplitudes[row]
        channel_amplitudes.setflags(write=False)
        spectra[channel] = FrequencySpectrum(
            frequencies_hz=frequencies_hz,
            amplitudes=channel_amplitudes,
            sample_rate_hz=sample_rate_hz,
            sample_count=sample_count,
        )
    return spectra


def calculate_frequency_spectrum(
        data: RecordingPlotData, channel: int,
        start_trim_s: float = DEFAULT_START_TRIM_S,
        end_trim_s: float = DEFAULT_END_TRIM_S,
        window_type: str = "hann") -> FrequencySpectrum:
    """Return one full-bin, single-sided spectrum."""
    return calculate_frequency_spectra(
        data,
        (channel,),
        start_trim_s,
        end_trim_s,
        window_type,
    )[channel]


def amplitudes_to_dbfs(amplitudes: np.ndarray) -> np.ndarray:
    """Convert peak amplitudes in ADC counts to decibels full scale."""
    minimum_ratio = 10.0 ** (FFT_DBFS_FLOOR / 20.0)
    ratios = np.maximum(amplitudes / ADC_FULL_SCALE_COUNTS, minimum_ratio)
    return 20.0 * np.log10(ratios)


def show_plot(data: RecordingPlotData,
              channel_indices: tuple[int, ...] | None = None,
              fft_channel_indices: tuple[int, ...] | None = None) -> None:
    try:
        from .qt_viewer import RecordingPlotWindow as QtRecordingPlotWindow
    except ImportError as error:
        raise RuntimeError(
            "PySide6-Essentials and pyqtgraph are required to plot recordings"
        ) from error

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
    application = _get_qt_application()
    window = QtRecordingPlotWindow(
        data,
        waveform_channels,
        fft_channels,
    )
    window.showMaximized()
    application.exec()


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
        default="0-2",
        help="initial channel selection for both plots (default: 0-2; thermistor off)",
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
