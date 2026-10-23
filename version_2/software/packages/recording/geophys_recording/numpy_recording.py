"""Read selected ADC samples from a V2 recording into NumPy arrays."""

from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
from typing import TYPE_CHECKING
import zlib

from .adc_record import RECORD_SIZE, decode_adc_record
from .recording import RecordingFileError, read_recording_header
from .recording_header import DATA_OFFSET, RecordingHeader

if TYPE_CHECKING:
    from numpy.typing import NDArray
    import numpy as np


@dataclass(frozen=True)
class NumpyRecording:
    """Raw ADC counts and recorded timing for a selected interval."""

    samples: NDArray[np.int32]
    timestamps_s: NDArray[np.float64]
    channels: tuple[int, ...]
    sample_rate_hz: float
    adc_gains: tuple[int, ...]
    start_s: float
    duration_s: float
    header: RecordingHeader | None

    def channel(self, number: int) -> NDArray[np.int32]:
        """Return the one-dimensional array for an ADC channel number."""
        try:
            column = self.channels.index(number)
        except ValueError as error:
            raise ValueError(f"CH{number} was not requested") from error
        return self.samples[:, column]


def _fields(raw: NDArray[np.uint8], start: int, width: int,
            dtype: str) -> NDArray:
    """Read a little-endian record-header field across all blocks."""
    import numpy as np

    return np.frombuffer(raw[:, start:start + width].tobytes(), dtype=dtype)


def read_recording_numpy(
    path: str | Path,
    *,
    channels: int | tuple[int, ...] | None = None,
    start_s: float = 0.0,
    stop_s: float | None = None,
) -> NumpyRecording:
    """Decode a recording interval into an ``(samples, channels)`` int32 array.

    Selection times are relative to the first ADC conversion, rounded to the
    nearest sample. ``timestamps_s`` uses each block's recorded monotonic
    timestamp and sample period, relative to the first conversion in the file.
    The interval is half-open. A missing ``channels`` selects every recorded
    channel. The entire file's block headers and continuity are checked; CRC
    and status are checked for the selected interval.
    """
    import numpy as np

    recording_path = Path(path)
    header = read_recording_header(recording_path)
    data_offset = DATA_OFFSET if header is not None else 0
    size = recording_path.stat().st_size
    if size < data_offset or (size - data_offset) % RECORD_SIZE:
        raise RecordingFileError("recording ends with an incomplete ADC block")
    record_count = (size - data_offset) // RECORD_SIZE

    if not record_count and header is None:
        raise RecordingFileError("recording contains no ADC blocks")
    if record_count:
        raw = np.memmap(recording_path, dtype=np.uint8, mode="r",
                        offset=data_offset, shape=(record_count, RECORD_SIZE))
        try:
            first = decode_adc_record(raw[0].tobytes())
        except ValueError as error:
            raise RecordingFileError(f"invalid record 0: {error}") from error
        available = first.channel_indices
        mask = first.channel_mask
        period = first.sample_period_100ns
        packed_gain = first.packed_gain
        samples_per_record = len(first.conversions)
    else:
        raw = None
        assert header is not None
        available = tuple(i for i in range(8)
                          if header.adc_channel_mask & (1 << i))
        mask = header.adc_channel_mask
        period = header.adc_sample_period_100ns
        packed_gain = header.packed_gain
        samples_per_record = 480 // (3 * len(available))

    if header is not None and (
        header.adc_channel_mask != mask or
        header.adc_sample_period_100ns != period or
        header.packed_gain != packed_gain
    ):
        raise RecordingFileError("ADC blocks disagree with recording header")

    if channels is None:
        selected = available
    elif isinstance(channels, int):
        selected = (channels,)
    else:
        selected = tuple(channels)
    if not selected or len(set(selected)) != len(selected) or any(
        channel not in available for channel in selected
    ):
        raise ValueError(f"channels must be distinct members of {available}")

    if not math.isfinite(start_s) or start_s < 0 or (
        stop_s is not None and
        (not math.isfinite(stop_s) or stop_s < start_s)
    ):
        raise ValueError("invalid time interval")
    sample_rate_hz = 10_000_000 / period
    total_samples = record_count * samples_per_record
    first_sample = round(start_s * sample_rate_hz)
    last_sample = (total_samples if stop_s is None else
                   round(stop_s * sample_rate_hz))
    if first_sample > total_samples or last_sample > total_samples or \
            last_sample < first_sample:
        raise ValueError(f"time interval exceeds {total_samples / sample_rate_hz:g} s")

    timestamps = None
    if raw is not None:
        if np.any(raw[:, :4] != np.frombuffer(b"\\DAT", dtype=np.uint8)):
            raise RecordingFileError("invalid ADC block magic")
        if np.any(raw[:, 4] != mask) or np.any(raw[:, 5] > 3):
            raise RecordingFileError("ADC channel mask or status code changed")
        if np.any(_fields(raw, 6, 2, "<u2") != packed_gain) or np.any(
            _fields(raw, 24, 4, "<u4") != period
        ):
            raise RecordingFileError("ADC gain or sample period changed")
        timestamps = _fields(raw, 16, 8, "<u8")
        if record_count > 1:
            payload_numbers = _fields(raw, 8, 4, "<u4")
            sequences = _fields(raw, 12, 4, "<u4")
            if np.any(np.diff(payload_numbers) != np.uint32(1)) or np.any(
                np.diff(sequences) != np.uint32(samples_per_record)
            ):
                raise RecordingFileError("missing or out-of-order ADC conversions")
            expected_ticks = samples_per_record * period
            if np.any(timestamps[1:] < timestamps[:-1]) or np.any(
                np.abs(np.diff(timestamps).astype(np.int64) - expected_ticks) > period
            ):
                raise RecordingFileError("ADC timestamp gap exceeds one sample")

    gains = (header.adc_gains if header is not None else
             tuple(1 << ((packed_gain >> (2 * channel)) & 3)
                   for channel in range(8)))
    result = np.empty((last_sample - first_sample, len(selected)), dtype=np.int32)
    sample_times_s = np.empty(last_sample - first_sample, dtype=np.float64)
    if last_sample > first_sample:
        assert raw is not None and timestamps is not None
        first_record = first_sample // samples_per_record
        last_record = (last_sample + samples_per_record - 1) // samples_per_record
        for chunk_start in range(first_record, last_record, 4096):
            chunk_end = min(chunk_start + 4096, last_record)
            chunk = raw[chunk_start:chunk_end]
            if np.any(chunk[:, 5] != 0):
                raise RecordingFileError("non-good ADC status in selected interval")
            for record_index, record in enumerate(chunk, chunk_start):
                expected_crc = int.from_bytes(record[508:512].tobytes(), "little")
                if zlib.crc32(record[:508].tobytes()) & 0xFFFFFFFF != expected_crc:
                    raise RecordingFileError(f"CRC error at record {record_index}")

            payload = chunk[:, 28:508].reshape(
                len(chunk), samples_per_record, len(available), 3
            )
            chunk_first = max(first_sample, chunk_start * samples_per_record)
            chunk_last = min(last_sample, chunk_end * samples_per_record)
            local = slice(chunk_first - chunk_start * samples_per_record,
                          chunk_last - chunk_start * samples_per_record)
            destination = slice(chunk_first - first_sample, chunk_last - first_sample)
            ticks = (timestamps[chunk_start:chunk_end, None] - timestamps[0] +
                     np.arange(samples_per_record, dtype=np.uint64)[None, :] *
                     np.uint64(period))
            sample_times_s[destination] = ticks.reshape(-1)[local] * 1e-7
            for column, channel in enumerate(selected):
                triplets = payload[:, :, available.index(channel), :]
                values = (triplets[:, :, 0].astype(np.int32) |
                          (triplets[:, :, 1].astype(np.int32) << 8) |
                          (triplets[:, :, 2].astype(np.int32) << 16))
                values[values >= 0x800000] -= 1 << 24
                result[destination, column] = values.reshape(-1)[local]

    return NumpyRecording(
        samples=result,
        timestamps_s=sample_times_s,
        channels=selected,
        sample_rate_hz=sample_rate_hz,
        adc_gains=tuple(gains[channel] for channel in selected),
        start_s=first_sample / sample_rate_hz,
        duration_s=total_samples / sample_rate_hz,
        header=header,
    )
