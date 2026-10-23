"""Portable readers for Geophys recording files."""

from .adc_record import (
    AdcRecord,
    AdcRecordError,
    RECORD_SIZE,
    decode_adc_record,
)
from .recording import (
    RecordingFileError,
    iter_adc_records,
    read_recording_header,
)
from .recording_header import (
    DATA_OFFSET,
    HEADER_MAGIC,
    STATIC_HEADER_SIZE,
    TOTAL_HEADER_SIZE,
    RecordingHeader,
    RecordingHeaderError,
    decode_recording_header,
)
from .numpy_recording import NumpyRecording, read_recording_numpy

__all__ = [
    "AdcRecord",
    "AdcRecordError",
    "DATA_OFFSET",
    "HEADER_MAGIC",
    "NumpyRecording",
    "RECORD_SIZE",
    "STATIC_HEADER_SIZE",
    "TOTAL_HEADER_SIZE",
    "RecordingFileError",
    "RecordingHeader",
    "RecordingHeaderError",
    "decode_adc_record",
    "decode_recording_header",
    "iter_adc_records",
    "read_recording_header",
    "read_recording_numpy",
]
