"""Byte-exact capture of validated ADC records."""

from __future__ import annotations

from pathlib import Path
from typing import BinaryIO

from .adc_record import RECORD_SIZE, decode_adc_record


class RawRecordCapture:
    r"""Write only complete, CRC-valid ``\DAT`` records to a new file."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.blocks_written = 0
        self.bytes_written = 0
        self._file: BinaryIO | None = None

    def __enter__(self) -> RawRecordCapture:
        self._file = self.path.open("xb")
        return self

    def append(self, raw_record: bytes) -> None:
        if self._file is None:
            raise RuntimeError("capture is not open")
        decode_adc_record(raw_record)
        if len(raw_record) != RECORD_SIZE:
            raise ValueError("capture accepts only complete ADC records")
        self._file.write(raw_record)
        self.blocks_written += 1
        self.bytes_written += len(raw_record)

    def flush(self) -> None:
        if self._file is not None:
            self._file.flush()

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        if self._file is not None:
            self._file.close()
            self._file = None
