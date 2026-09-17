"""Optional HELLO smoke test against a connected V2 Rev-1 device."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time

import serial

HOST_APP = Path(__file__).resolve().parents[1]
REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(HOST_APP))

from geophys_host.protocol import (  # noqa: E402
    CommandStreamParser,
    REPLY_DEVICE_INFO,
    decode_device_info,
    encode_hello,
)


def read_commands(connection: serial.Serial, timeout_s: float):
    parser = CommandStreamParser()
    commands = []
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        commands.extend(parser.feed(connection.read(256)))
    return commands


def main() -> int:
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("port")
    argument_parser.add_argument("--baud", type=int, default=921_600)
    arguments = argument_parser.parse_args()

    bad_crc_path = (
        REPOSITORY / "shared" / "protocol" / "test_vectors" /
        "command" / "invalid" / "hello_bad_crc.txt"
    )
    bad_crc = bytes.fromhex(bad_crc_path.read_text(encoding="ascii"))
    hello = encode_hello()

    with serial.Serial(
        arguments.port,
        arguments.baud,
        timeout=0.02,
        write_timeout=1.0,
    ) as connection:
        time.sleep(0.1)
        connection.reset_input_buffer()

        connection.write(bad_crc)
        connection.flush()
        assert read_commands(connection, 0.3) == [], (
            "CRC-invalid HELLO unexpectedly produced a valid command reply"
        )

        for byte in hello:
            connection.write(bytes([byte]))
            connection.flush()
            time.sleep(0.001)
        fragmented_replies = read_commands(connection, 0.5)
        assert len(fragmented_replies) == 1
        info = decode_device_info(fragmented_replies[0])

        connection.write(hello + hello)
        connection.flush()
        concatenated_replies = read_commands(connection, 0.5)
        assert len(concatenated_replies) == 2
        assert all(
            reply.command_id == REPLY_DEVICE_INFO
            for reply in concatenated_replies
        )

    mac = ":".join(f"{byte:02x}" for byte in info.mac_address)
    print(
        "hardware HELLO tests passed: "
        f"mac={mac} hw={info.hardware_version} "
        f"rev={info.hardware_revision} fw={info.firmware_version} "
        f"protocol={info.protocol_version}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
