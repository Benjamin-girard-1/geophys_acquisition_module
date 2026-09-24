"""Live ADC stream state, continuity checks, and visualization."""

from __future__ import annotations

from collections import Counter, deque
from dataclasses import dataclass
import math
import time

from .adc_record import AdcRecord


@dataclass(frozen=True)
class LiveSnapshot:
    blocks_received: int
    conversions_received: int
    missing_conversions: int
    payload_discontinuities: int
    sequence_discontinuities: int
    status_counts: dict[int, int]
    elapsed_s: float
    conversion_rate_hz: float


class LiveStreamModel:
    """Accumulate a bounded plot window while retaining stream counters."""

    def __init__(self, window_s: float = 5.0,
                 source_sequence_step: int = 1) -> None:
        if window_s <= 0.0:
            raise ValueError("plot window must be positive")
        if source_sequence_step <= 0:
            raise ValueError("source sequence step must be positive")
        self.window_s = window_s
        self.source_sequence_step = source_sequence_step
        self.timestamps_s: deque[float] = deque()
        self.samples = {channel: deque() for channel in range(8)}
        self.blocks_received = 0
        self.conversions_received = 0
        self.missing_conversions = 0
        self.payload_discontinuities = 0
        self.sequence_discontinuities = 0
        self.status_counts: Counter[int] = Counter()
        self.channel_mask: int | None = None
        self._first_timestamp_100ns: int | None = None
        self._expected_payload: int | None = None
        self._expected_sequence: int | None = None
        self._started_at: float | None = None

    def append(self, record: AdcRecord) -> None:
        if self._started_at is None:
            self._started_at = time.monotonic()
        if self.channel_mask is None:
            self.channel_mask = record.channel_mask
        elif record.channel_mask != self.channel_mask:
            raise ValueError(
                "ADC channel mask changed within one live stream")

        if self._expected_payload is not None and \
                record.payload_number != self._expected_payload:
            self.payload_discontinuities += 1
        if self._expected_sequence is not None and \
                record.first_conversion_sequence != self._expected_sequence:
            self.sequence_discontinuities += 1
            distance = (
                record.first_conversion_sequence - self._expected_sequence
            ) & 0xFFFFFFFF
            if distance <= 0x7FFFFFFF:
                self.missing_conversions += max(
                    1, distance // self.source_sequence_step)

        if self._first_timestamp_100ns is None:
            self._first_timestamp_100ns = \
                record.first_monotonic_timestamp_100ns

        for conversion_index, conversion in enumerate(record.conversions):
            timestamp_100ns = (
                record.first_monotonic_timestamp_100ns +
                conversion_index * record.sample_period_100ns
            )
            timestamp_s = (
                timestamp_100ns - self._first_timestamp_100ns
            ) / 10_000_000.0
            self.timestamps_s.append(timestamp_s)
            sample_by_channel = dict(zip(record.channel_indices, conversion))
            for channel in range(8):
                self.samples[channel].append(sample_by_channel.get(channel))

        conversion_count = len(record.conversions)
        self.blocks_received += 1
        self.conversions_received += conversion_count
        self.status_counts[record.status] += 1
        self._expected_payload = (record.payload_number + 1) & 0xFFFFFFFF
        self._expected_sequence = (
            record.first_conversion_sequence +
            conversion_count * self.source_sequence_step
        ) & 0xFFFFFFFF
        self._trim()

    def _trim(self) -> None:
        if not self.timestamps_s:
            return
        cutoff = self.timestamps_s[-1] - self.window_s
        while self.timestamps_s and self.timestamps_s[0] < cutoff:
            self.timestamps_s.popleft()
            for samples in self.samples.values():
                samples.popleft()

    def snapshot(self) -> LiveSnapshot:
        elapsed_s = (
            0.0 if self._started_at is None
            else max(time.monotonic() - self._started_at, 1e-9)
        )
        return LiveSnapshot(
            blocks_received=self.blocks_received,
            conversions_received=self.conversions_received,
            missing_conversions=self.missing_conversions,
            payload_discontinuities=self.payload_discontinuities,
            sequence_discontinuities=self.sequence_discontinuities,
            status_counts=dict(self.status_counts),
            elapsed_s=elapsed_s,
            conversion_rate_hz=(
                0.0 if elapsed_s == 0.0
                else self.conversions_received / elapsed_s
            ),
        )

    def plot_data(
            self, channel: int,
            max_points: int = 2_000) -> tuple[list[float], list[int]]:
        values = self.samples[channel]
        if not values:
            return [], []
        stride = max(1, math.ceil(len(values) / max_points))
        points = [
            (timestamp, value)
            for timestamp, value in zip(self.timestamps_s, values)
            if value is not None
        ][::stride]
        return (
            [point[0] for point in points],
            [point[1] for point in points],
        )


class MatplotlibLiveView:
    """Small optional GUI for raw channel counts and stream health."""

    def __init__(self, channel_mask: int, window_s: float) -> None:
        try:
            import matplotlib.pyplot as plt
        except ImportError as error:
            raise RuntimeError(
                "matplotlib is required for the graphical live view"
            ) from error

        self._plt = plt
        self._channels = [
            channel for channel in range(8)
            if channel_mask & (1 << channel)
        ]
        self._figure, axes = plt.subplots(
            len(self._channels), 1,
            sharex=True,
            figsize=(11, max(5, 1.45 * len(self._channels))),
            squeeze=False,
        )
        if getattr(
                self._figure.canvas,
                "required_interactive_framework",
                None) is None:
            plt.close(self._figure)
            raise RuntimeError(
                "the active Matplotlib backend is not interactive; "
                "run the live command with --terminal"
            )
        self._axes = [row[0] for row in axes]
        self._lines = {}
        for axis, channel in zip(self._axes, self._channels):
            line, = axis.plot([], [], linewidth=0.8)
            axis.set_ylabel(f"CH{channel}")
            axis.grid(True, alpha=0.25)
            self._lines[channel] = line
        self._axes[-1].set_xlabel("Device monotonic time (s)")
        self._figure.suptitle("Geophysical acquisition live stream")
        self._figure.tight_layout()
        self._window_s = window_s
        plt.show(block=False)

    @property
    def is_open(self) -> bool:
        return self._plt.fignum_exists(self._figure.number)

    def update(
            self, model: LiveStreamModel,
            invalid_records: int,
            bytes_discarded: int) -> None:
        for axis, channel in zip(self._axes, self._channels):
            timestamps, samples = model.plot_data(channel)
            self._lines[channel].set_data(timestamps, samples)
            if timestamps:
                right = timestamps[-1]
                axis.set_xlim(max(0.0, right - self._window_s),
                              max(self._window_s, right))
                axis.relim()
                axis.autoscale_view(scalex=False, scaley=True)

        snapshot = model.snapshot()
        statuses = ",".join(
            f"{status}:{count}"
            for status, count in sorted(snapshot.status_counts.items())
        ) or "none"
        self._figure.suptitle(
            "Geophysical acquisition live stream  |  "
            f"blocks {snapshot.blocks_received}  "
            f"rate {snapshot.conversion_rate_hz:.1f} conv/s  "
            f"missing {snapshot.missing_conversions}  "
            f"bad blocks {invalid_records}  "
            f"discarded bytes {bytes_discarded}  "
            f"status {statuses}"
        )
        self._figure.canvas.draw_idle()
        self._figure.canvas.flush_events()
        self._plt.pause(0.001)

    def close(self) -> None:
        self._plt.close(self._figure)
