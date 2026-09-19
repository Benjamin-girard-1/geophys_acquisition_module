r"""UART-to-USB client for the mixed V2 command and ADC-record stream."""

from __future__ import annotations

from collections import deque
import time
from typing import Callable

from .protocol import (
    Command,
    DeviceInfo,
    REPLY_DEVICE_INFO,
    decode_device_info,
    encode_hello,
)
from .stream_parser import (
    AdcRecordMessage,
    CommandMessage,
    MixedStreamParser,
    StreamMessage,
)

RecordHandler = Callable[[AdcRecordMessage], None]


class SerialClient:
    """One-owner serial connection with exact named-reply matching."""

    def __init__(self, port: str, baud_rate: int = 921_600,
                 read_timeout_s: float = 0.02,
                 write_timeout_s: float = 2.0) -> None:
        self.port = port
        self.baud_rate = baud_rate
        self.read_timeout_s = read_timeout_s
        self.write_timeout_s = write_timeout_s
        self.parser = MixedStreamParser()
        self._connection = None
        self._messages: deque[StreamMessage] = deque()

    def __enter__(self) -> SerialClient:
        try:
            import serial
        except ImportError as error:
            raise RuntimeError(
                "pyserial is required for device access"
            ) from error

        self._connection = serial.Serial(
            port=self.port,
            baudrate=self.baud_rate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=self.read_timeout_s,
            write_timeout=self.write_timeout_s,
        )
        self._connection.reset_input_buffer()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        if self._connection is not None:
            self._connection.close()
            self._connection = None

    def _require_connection(self):
        if self._connection is None:
            raise RuntimeError("serial connection is not open")
        return self._connection

    def read_message(self) -> StreamMessage | None:
        if self._messages:
            return self._messages.popleft()
        connection = self._require_connection()
        data = connection.read(4096)
        if not data:
            return None
        self._messages.extend(self.parser.feed(data))
        if self._messages:
            return self._messages.popleft()
        return None

    def request(self, frame: bytes, expected_reply_id: int,
                timeout_s: float = 2.0,
                on_record: RecordHandler | None = None) -> Command:
        connection = self._require_connection()
        connection.write(frame)
        connection.flush()
        deadline = time.monotonic() + timeout_s
        deferred_records: list[AdcRecordMessage] = []
        try:
            while time.monotonic() < deadline:
                message = self.read_message()
                if message is None:
                    continue
                if isinstance(message, AdcRecordMessage):
                    if on_record is not None:
                        on_record(message)
                    else:
                        deferred_records.append(message)
                    continue
                if isinstance(message, CommandMessage) and \
                        message.command.command_id == expected_reply_id:
                    return message.command
            raise TimeoutError(
                f"reply 0x{expected_reply_id:04x} was not received")
        finally:
            self._messages.extendleft(reversed(deferred_records))


def request_device_info(port: str, baud_rate: int = 921_600,
                        timeout_s: float = 2.0) -> DeviceInfo:
    with SerialClient(port, baud_rate) as client:
        command = client.request(
            encode_hello(), REPLY_DEVICE_INFO, timeout_s=timeout_s)
        return decode_device_info(command)
