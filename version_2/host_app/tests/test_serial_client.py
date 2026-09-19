from __future__ import annotations

from collections import deque
from pathlib import Path
import sys
import unittest

HOST_APP = Path(__file__).resolve().parents[1]
REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(HOST_APP))

from geophys_host.protocol import (  # noqa: E402
    DIRECTION_TO_HOST,
    REPLY_DEVICE_CONFIG,
    REPLY_DEVICE_INFO,
    encode_command,
    encode_hello,
)
from geophys_host.serial_client import SerialClient  # noqa: E402
from geophys_host.stream_parser import AdcRecordMessage  # noqa: E402


class FakeConnection:
    def __init__(self, chunks: list[bytes]) -> None:
        self.chunks = deque(chunks)
        self.writes: list[bytes] = []
        self.flush_count = 0

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def flush(self) -> None:
        self.flush_count += 1

    def read(self, size: int) -> bytes:
        if self.chunks:
            return self.chunks.popleft()
        return b""


class SerialClientTests(unittest.TestCase):
    def test_named_reply_wait_processes_interleaved_records(self) -> None:
        path = (
            REPOSITORY / "shared" / "protocol" / "test_vectors" /
            "data_block" / "valid" / "adc_record_8ch.txt"
        )
        record = bytes.fromhex(path.read_text(encoding="ascii"))
        unrelated = encode_command(
            REPLY_DEVICE_CONFIG, DIRECTION_TO_HOST, bytes(40))
        expected = encode_command(
            REPLY_DEVICE_INFO, DIRECTION_TO_HOST, bytes(16))
        connection = FakeConnection([
            unrelated[:31],
            unrelated[31:] + record + expected + record,
        ])
        client = SerialClient("unused")
        client._connection = connection
        received_records = []

        reply = client.request(
            encode_hello(),
            REPLY_DEVICE_INFO,
            timeout_s=0.1,
            on_record=received_records.append,
        )

        self.assertEqual(reply.command_id, REPLY_DEVICE_INFO)
        self.assertEqual(len(received_records), 1)
        self.assertEqual(received_records[0].raw, record)
        self.assertEqual(connection.writes, [encode_hello()])
        self.assertEqual(connection.flush_count, 1)
        remaining = client.read_message()
        self.assertIsInstance(remaining, AdcRecordMessage)
        self.assertEqual(remaining.raw, record)

    def test_request_defers_records_when_no_handler_is_registered(self) -> None:
        path = (
            REPOSITORY / "shared" / "protocol" / "test_vectors" /
            "data_block" / "valid" / "adc_record_8ch.txt"
        )
        record = bytes.fromhex(path.read_text(encoding="ascii"))
        expected = encode_command(
            REPLY_DEVICE_INFO, DIRECTION_TO_HOST, bytes(16))
        client = SerialClient("unused")
        client._connection = FakeConnection([record + expected])

        reply = client.request(
            encode_hello(), REPLY_DEVICE_INFO, timeout_s=0.1)

        self.assertEqual(reply.command_id, REPLY_DEVICE_INFO)
        deferred = client.read_message()
        self.assertIsInstance(deferred, AdcRecordMessage)
        self.assertEqual(deferred.raw, record)


if __name__ == "__main__":
    unittest.main()
