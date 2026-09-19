r"""Incremental parser for interleaved ``\CMD`` and ``\DAT`` messages."""

from __future__ import annotations

from dataclasses import dataclass

from .adc_record import (
    AdcRecord,
    AdcRecordError,
    RECORD_MAGIC,
    RECORD_SIZE,
    decode_adc_record,
)
from .protocol import (
    COMMAND_MAGIC,
    COMMAND_SIZE,
    Command,
    ProtocolError,
    decode_command,
)


@dataclass(frozen=True)
class CommandMessage:
    raw: bytes
    command: Command


@dataclass(frozen=True)
class AdcRecordMessage:
    raw: bytes
    record: AdcRecord


StreamMessage = CommandMessage | AdcRecordMessage


class MixedStreamParser:
    """Recover valid messages from arbitrary serial fragmentation and noise."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.bytes_discarded = 0
        self.invalid_commands = 0
        self.invalid_records = 0

    def feed(self, data: bytes) -> list[StreamMessage]:
        self._buffer.extend(data)
        messages: list[StreamMessage] = []
        while True:
            command_start = self._buffer.find(COMMAND_MAGIC)
            record_start = self._buffer.find(RECORD_MAGIC)
            starts = tuple(
                start for start in (command_start, record_start)
                if start >= 0
            )
            if not starts:
                keep = min(3, len(self._buffer))
                self.bytes_discarded += len(self._buffer) - keep
                if keep:
                    self._buffer[:] = self._buffer[-keep:]
                else:
                    self._buffer.clear()
                break

            start = min(starts)
            if start:
                self.bytes_discarded += start
                del self._buffer[:start]

            if self._buffer.startswith(COMMAND_MAGIC):
                if len(self._buffer) < COMMAND_SIZE:
                    break
                raw = bytes(self._buffer[:COMMAND_SIZE])
                try:
                    command = decode_command(raw)
                except ProtocolError:
                    self.invalid_commands += 1
                    self.bytes_discarded += 1
                    del self._buffer[0]
                    continue
                del self._buffer[:COMMAND_SIZE]
                messages.append(CommandMessage(raw=raw, command=command))
                continue

            if len(self._buffer) < RECORD_SIZE:
                break
            raw = bytes(self._buffer[:RECORD_SIZE])
            try:
                record = decode_adc_record(raw)
            except AdcRecordError:
                self.invalid_records += 1
                self.bytes_discarded += 1
                del self._buffer[0]
                continue
            del self._buffer[:RECORD_SIZE]
            messages.append(AdcRecordMessage(raw=raw, record=record))

        return messages
