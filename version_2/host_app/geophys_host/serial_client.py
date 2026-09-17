r"""UART-to-USB client for the V2 command protocol."""

from __future__ import annotations

import time

from .protocol import (
    CommandStreamParser,
    DeviceInfo,
    REPLY_DEVICE_INFO,
    decode_device_info,
    encode_hello,
)


def request_device_info(port: str, baud_rate: int = 921_600,
                        timeout_s: float = 2.0) -> DeviceInfo:
    try:
        import serial
    except ImportError as error:
        raise RuntimeError("pyserial is required for device access") from error

    parser = CommandStreamParser()
    deadline = time.monotonic() + timeout_s
    with serial.Serial(
        port=port,
        baudrate=baud_rate,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,
        write_timeout=timeout_s,
    ) as connection:
        connection.reset_input_buffer()
        connection.write(encode_hello())
        connection.flush()
        while time.monotonic() < deadline:
            data = connection.read(256)
            for command in parser.feed(data):
                if command.command_id == REPLY_DEVICE_INFO:
                    return decode_device_info(command)
    raise TimeoutError("DEVICE_INFO reply was not received")
