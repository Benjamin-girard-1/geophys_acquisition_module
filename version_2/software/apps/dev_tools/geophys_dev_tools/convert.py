"""Convert portable 512-byte Geophys recordings to analysis-ready CSV."""

from __future__ import annotations

import argparse
from collections import Counter
import csv
from dataclasses import dataclass
import os
from pathlib import Path
import sys
import tempfile

from geophys_data import RecordingFileError, iter_adc_records


STATUS_NAMES = {
    0: "good",
    1: "critical",
    2: "conversion",
    3: "timing",
}
CHANNEL_COUNT = 8
TIMESTAMP_TICKS_PER_SECOND = 10_000_000
CSV_COLUMNS = (
    "time_s",
    "monotonic_timestamp_100ns",
    "conversion_sequence",
    "record_index",
    "payload_number",
    "conversion_index",
    "status_code",
    "status_name",
    "channel_mask",
    "packed_gain",
    "sample_period_100ns",
    *(f"ch{channel}_counts" for channel in range(CHANNEL_COUNT)),
)


@dataclass(frozen=True)
class ConversionSummary:
    input_path: Path
    output_path: Path
    record_count: int
    conversion_count: int
    status_counts: dict[int, int]
    payload_discontinuities: int
    sequence_discontinuities: int
    missing_conversions: int


def default_scratch_directory() -> Path:
    """Return the external, non-repository directory for disposable data."""
    repository_root = Path(__file__).resolve().parents[5]
    return repository_root.parent / "geophys_acquisition_data" / "scratch"


def default_output_path(
        input_path: str | Path,
        scratch_directory: str | Path | None = None) -> Path:
    """Choose a CSV name in the external scratch directory."""
    source = Path(input_path).expanduser()
    output_name = (
        f"{source.stem}.csv" if source.suffix else f"{source.name}.csv"
    )
    directory = (
        default_scratch_directory()
        if scratch_directory is None
        else Path(scratch_directory).expanduser()
    )
    return directory / output_name


def convert_recording_to_csv(
        input_path: str | Path,
        output_path: str | Path,
        *,
        overwrite: bool = False) -> ConversionSummary:
    """Validate and stream one binary recording into a wide CSV file."""
    source = Path(input_path).expanduser().resolve()
    destination = Path(output_path).expanduser().resolve()
    if source == destination:
        raise ValueError("input and output paths must be different")
    if destination.exists() and not overwrite:
        raise FileExistsError(
            f"output already exists: {destination}; use --force to replace it"
        )

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    record_count = 0
    conversion_count = 0
    statuses: Counter[int] = Counter()
    payload_discontinuities = 0
    sequence_discontinuities = 0
    missing_conversions = 0
    first_timestamp_100ns: int | None = None
    expected_payload: int | None = None
    expected_sequence: int | None = None

    try:
        with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                newline="",
                dir=destination.parent,
                prefix=f".{destination.name}.",
                suffix=".tmp",
                delete=False) as stream:
            temporary_path = Path(stream.name)
            writer = csv.writer(stream)
            writer.writerow(CSV_COLUMNS)

            for record_index, record in enumerate(iter_adc_records(source)):
                if first_timestamp_100ns is None:
                    first_timestamp_100ns = (
                        record.first_monotonic_timestamp_100ns
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

                for conversion_index, conversion in enumerate(
                        record.conversions):
                    timestamp_100ns = (
                        record.first_monotonic_timestamp_100ns +
                        conversion_index * record.sample_period_100ns
                    )
                    relative_time_s = (
                        timestamp_100ns - first_timestamp_100ns
                    ) / TIMESTAMP_TICKS_PER_SECOND
                    channel_values: list[int | str] = [""] * CHANNEL_COUNT
                    for channel, value in zip(
                            record.channel_indices, conversion):
                        channel_values[channel] = value

                    writer.writerow((
                        f"{relative_time_s:.7f}",
                        timestamp_100ns,
                        (
                            record.first_conversion_sequence +
                            conversion_index
                        ) & 0xFFFFFFFF,
                        record_index,
                        record.payload_number,
                        conversion_index,
                        record.status,
                        STATUS_NAMES.get(record.status, "unknown"),
                        f"0x{record.channel_mask:02x}",
                        f"0x{record.packed_gain:04x}",
                        record.sample_period_100ns,
                        *channel_values,
                    ))
                    conversion_count += 1

                record_count += 1
                statuses[record.status] += 1
                expected_payload = (record.payload_number + 1) & 0xFFFFFFFF
                expected_sequence = (
                    record.first_conversion_sequence +
                    len(record.conversions)
                ) & 0xFFFFFFFF

        os.replace(temporary_path, destination)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)

    return ConversionSummary(
        input_path=source,
        output_path=destination,
        record_count=record_count,
        conversion_count=conversion_count,
        status_counts=dict(statuses),
        payload_discontinuities=payload_discontinuities,
        sequence_discontinuities=sequence_discontinuities,
        missing_conversions=missing_conversions,
    )


def _format_status_counts(status_counts: dict[int, int]) -> str:
    return ", ".join(
        f"{STATUS_NAMES.get(status, str(status))}={count}"
        for status, count in sorted(status_counts.items())
    ) or "none"


def print_summary(summary: ConversionSummary) -> None:
    print(f"Input: {summary.input_path}")
    print(f"CSV: {summary.output_path}")
    print(
        f"Records: {summary.record_count} | "
        f"Conversions: {summary.conversion_count}"
    )
    print(f"Record status: {_format_status_counts(summary.status_counts)}")
    print(
        f"Payload discontinuities: {summary.payload_discontinuities} | "
        f"Sequence discontinuities: {summary.sequence_discontinuities} | "
        f"Missing conversions: {summary.missing_conversions}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Convert a validated Geophys 512-byte-block recording to CSV"
        )
    )
    parser.add_argument("recording", type=Path, help="binary recording")
    parser.add_argument(
        "--output",
        type=Path,
        help=(
            "output CSV; defaults to geophys_acquisition_data/scratch/"
            "<recording>.csv"
        ),
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="replace an existing output CSV",
    )
    arguments = parser.parse_args(argv)
    output_path = arguments.output or default_output_path(arguments.recording)
    summary = convert_recording_to_csv(
        arguments.recording,
        output_path,
        overwrite=arguments.force,
    )
    print_summary(summary)
    return 0


def entrypoint() -> int:
    try:
        return main()
    except (OSError, RecordingFileError, ValueError) as error:
        print(f"geophys-bin-to-csv: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(entrypoint())
