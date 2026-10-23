"""Sequential reader for raw SD-card recording files."""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

from .adc_record import (
    AdcRecord,
    AdcRecordError,
    RECORD_SIZE,
    decode_adc_record,
)
from .recording_header import (
    DATA_OFFSET,
    HEADER_MAGIC,
    STATIC_HEADER_SIZE,
    RecordingHeader,
    RecordingHeaderError,
    decode_recording_header,
)


class RecordingFileError(ValueError):
    """A recording file violates the V2 storage contract."""


def read_recording_header(path: str | Path) -> RecordingHeader | None:
    """Return the JSON descriptor, or ``None`` for a DAT-only file."""
    recording_path = Path(path)
    with recording_path.open("rb") as stream:
        prefix = stream.read(4)
        if prefix == b"\\DAT":
            return None
        if not prefix.startswith(HEADER_MAGIC):
            raise RecordingFileError("unknown recording format")
        remainder = stream.read(STATIC_HEADER_SIZE - len(prefix))
    try:
        return decode_recording_header(prefix + remainder)
    except RecordingHeaderError as error:
        raise RecordingFileError(
            f"invalid recording header: {error}"
        ) from error


def iter_adc_records(path: str | Path) -> Iterator[AdcRecord]:
    """Yield validated records without loading the complete file into memory."""
    recording_path = Path(path)
    size_bytes = recording_path.stat().st_size
    header = read_recording_header(recording_path)
    data_offset = DATA_OFFSET if header is not None else 0
    if size_bytes < data_offset:
        raise RecordingFileError(
            f"recording size {size_bytes} is smaller than its header"
        )
    data_size_bytes = size_bytes - data_offset
    if data_size_bytes % RECORD_SIZE != 0:
        raise RecordingFileError(
            f"ADC data size {data_size_bytes} is not a multiple of {RECORD_SIZE}"
        )

    with recording_path.open("rb") as stream:
        stream.seek(data_offset)
        for record_index in range(data_size_bytes // RECORD_SIZE):
            raw_record = stream.read(RECORD_SIZE)
            try:
                yield decode_adc_record(raw_record)
            except AdcRecordError as error:
                raise RecordingFileError(
                    f"invalid record {record_index}: {error}"
                ) from error
