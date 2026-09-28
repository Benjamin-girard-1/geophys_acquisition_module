"""Sequential reader for raw SD-card recording files."""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

from .adc_record import AdcRecord, AdcRecordError, RECORD_SIZE, decode_adc_record


class RecordingFileError(ValueError):
    """A recording file violates the V2 storage contract."""


def iter_adc_records(path: str | Path) -> Iterator[AdcRecord]:
    """Yield validated records without loading the complete file into memory."""
    recording_path = Path(path)
    size_bytes = recording_path.stat().st_size
    if size_bytes % RECORD_SIZE != 0:
        raise RecordingFileError(
            f"recording size {size_bytes} is not a multiple of {RECORD_SIZE}"
        )

    with recording_path.open("rb") as stream:
        for record_index in range(size_bytes // RECORD_SIZE):
            raw_record = stream.read(RECORD_SIZE)
            try:
                yield decode_adc_record(raw_record)
            except AdcRecordError as error:
                raise RecordingFileError(
                    f"invalid record {record_index}: {error}"
                ) from error
