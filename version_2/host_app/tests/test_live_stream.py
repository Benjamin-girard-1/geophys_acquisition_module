from __future__ import annotations

from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
import unittest
import zlib

HOST_APP = Path(__file__).resolve().parents[1]
REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(HOST_APP))

from geophys_host.capture import RawRecordCapture  # noqa: E402
from geophys_host.live import LiveStreamModel  # noqa: E402
from geophys_host.protocol import (  # noqa: E402
    Command,
    encode_command,
    encode_hello,
)
from geophys_host.stream_parser import (  # noqa: E402
    AdcRecordMessage,
    CommandMessage,
    MixedStreamParser,
)


def updated_record(source: bytes, *, payload_number: int,
                   sequence: int, timestamp_100ns: int) -> bytes:
    record = bytearray(source)
    struct.pack_into("<I", record, 8, payload_number)
    struct.pack_into("<I", record, 12, sequence)
    struct.pack_into("<Q", record, 16, timestamp_100ns)
    struct.pack_into("<I", record, 508,
                     zlib.crc32(record[:508]) & 0xFFFFFFFF)
    return bytes(record)


class LiveStreamTests(unittest.TestCase):
    def setUp(self) -> None:
        path = (
            REPOSITORY / "shared" / "protocol" / "test_vectors" /
            "data_block" / "valid" / "adc_record_8ch.txt"
        )
        self.record = bytes.fromhex(path.read_text(encoding="ascii"))

    def test_mixed_parser_handles_fragmentation_and_corruption(self) -> None:
        corrupt = bytearray(self.record)
        corrupt[100] ^= 1
        stream = b"boot noise" + encode_hello() + bytes(corrupt) + self.record
        parser = MixedStreamParser()
        messages = []
        for offset in range(0, len(stream), 7):
            messages.extend(parser.feed(stream[offset:offset + 7]))

        self.assertEqual(len(messages), 2)
        self.assertIsInstance(messages[0], CommandMessage)
        self.assertIsInstance(messages[0].command, Command)
        self.assertIsInstance(messages[1], AdcRecordMessage)
        self.assertEqual(messages[1].raw, self.record)
        self.assertEqual(parser.invalid_records, 1)
        self.assertGreaterEqual(parser.bytes_discarded, len(b"boot noise"))

    def test_model_exposes_sequence_gaps_and_bounded_plot_data(self) -> None:
        first = updated_record(
            self.record,
            payload_number=10,
            sequence=100,
            timestamp_100ns=1_000_000,
        )
        second = updated_record(
            self.record,
            payload_number=12,
            sequence=125,
            timestamp_100ns=1_250_000,
        )
        parser = MixedStreamParser()
        records = [
            message.record for message in parser.feed(first + second)
            if isinstance(message, AdcRecordMessage)
        ]
        model = LiveStreamModel(window_s=5.0)
        for record in records:
            model.append(record)

        snapshot = model.snapshot()
        self.assertEqual(snapshot.blocks_received, 2)
        self.assertEqual(snapshot.conversions_received, 40)
        self.assertEqual(snapshot.payload_discontinuities, 1)
        self.assertEqual(snapshot.sequence_discontinuities, 1)
        self.assertEqual(snapshot.missing_conversions, 5)
        timestamps, samples = model.plot_data(0, max_points=5)
        self.assertLessEqual(len(timestamps), 5)
        self.assertEqual(len(timestamps), len(samples))

    def test_model_accounts_for_intentional_stream_decimation(self) -> None:
        first = updated_record(
            self.record,
            payload_number=20,
            sequence=1_000,
            timestamp_100ns=2_000_000,
        )
        second = updated_record(
            self.record,
            payload_number=21,
            sequence=1_100,
            timestamp_100ns=3_000_000,
        )
        parser = MixedStreamParser()
        records = [
            message.record for message in parser.feed(first + second)
            if isinstance(message, AdcRecordMessage)
        ]
        model = LiveStreamModel(source_sequence_step=5)
        for record in records:
            model.append(record)

        snapshot = model.snapshot()
        self.assertEqual(snapshot.sequence_discontinuities, 0)
        self.assertEqual(snapshot.missing_conversions, 0)

    def test_capture_preserves_validated_records_byte_for_byte(self) -> None:
        with TemporaryDirectory() as directory:
            path = Path(directory) / "capture.dat"
            with RawRecordCapture(path) as capture:
                capture.append(self.record)
                self.assertEqual(capture.blocks_written, 1)
                self.assertEqual(capture.bytes_written, 512)
            self.assertEqual(path.read_bytes(), self.record)
            with self.assertRaises(FileExistsError):
                with RawRecordCapture(path):
                    pass


if __name__ == "__main__":
    unittest.main()
