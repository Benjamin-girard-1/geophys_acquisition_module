from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
import zlib

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(PACKAGE_ROOT))

from geophys_recording import (  # noqa: E402
    DATA_OFFSET,
    RecordingFileError,
    STATIC_HEADER_SIZE,
    iter_adc_records,
    read_recording_header,
)


def make_header() -> bytes:
    descriptor = {
        "format": "GEO_RECORDING",
        "format_version": 2,
        "tools_url": "https://github.com/Benjamin-girard-1/geophys_acquisition_module",
        "recording_name": "field007",
        "device_id": "02:11:22:33:44:55",
        "hardware_version": 2,
        "hardware_revision": 1,
        "firmware_version": 0x01020304,
        "protocol_version": 1,
        "start_monotonic_timestamp_100ns": 123_456,
        "start_unix_timestamp_us": 0,
        "adc_sample_rate_sps": 1000,
        "adc_sample_period_100ns": 10_000,
        "adc_channel_mask": 15,
        "adc_channel_count": 4,
        "adc_gains": [8, 8, 8, 1, 1, 1, 1, 1],
        "packed_gain": 63,
        "card_types": [1, 0],
        "gnss_state": 0,
        "imu_state": 0,
        "imu_averaging_time_ms": 0,
        "power_rail_flags": 0,
        "channel_slots": [1, 1, 1, 1, 2, 2, 2, 2],
        "channel_inputs": [0, 1, 2, 3, 0, 1, 2, 3],
        "descriptor_size_bytes": STATIC_HEADER_SIZE,
        "checkpoint_offset_bytes": STATIC_HEADER_SIZE,
        "checkpoint_entry_size_bytes": 32,
        "checkpoint_capacity": 896,
        "checkpoint_interval_ms": 10_000,
        "data_offset_bytes": DATA_OFFSET,
        "record_size_bytes": 512,
        "sample_encoding": "signed24_le",
        "header_crc32": "00000000",
    }
    text = (json.dumps(descriptor, indent=2) + "\n").encode("ascii")
    static = bytearray(text.ljust(STATIC_HEADER_SIZE - 1, b" ") + b"\n")
    checksum = zlib.crc32(static) & 0xFFFFFFFF
    marker = b'"header_crc32": "00000000"'
    assert marker in static
    static = static.replace(marker, f'"header_crc32": "{checksum:08X}"'.encode())
    return bytes(static) + bytes(DATA_OFFSET - STATIC_HEADER_SIZE)


class RecordingTests(unittest.TestCase):
    def setUp(self) -> None:
        vector_path = (
            REPOSITORY_ROOT / "shared" / "protocol" / "test_vectors" /
            "data_block" / "valid" / "adc_record_8ch.txt"
        )
        self.record = bytes.fromhex(vector_path.read_text(encoding="ascii"))

    def test_iterates_concatenated_records(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(self.record * 2)

            records = list(iter_adc_records(path))

        self.assertEqual(len(records), 2)
        self.assertEqual(records[0].channel_indices, tuple(range(8)))
        self.assertEqual(len(records[0].conversions), 20)

    def test_reads_container_header_and_skips_reserved_region(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(make_header() + self.record * 2)

            header = read_recording_header(path)
            records = list(iter_adc_records(path))

        self.assertIsNotNone(header)
        assert header is not None
        self.assertEqual(header.recording_name, "field007")
        self.assertEqual(header.adc_gains, (8, 8, 8, 1, 1, 1, 1, 1))
        self.assertEqual(header.card_types, (1, 0))
        self.assertEqual(header.start_unix_timestamp_us, 0)
        self.assertEqual(header.checkpoint_capacity, 896)
        self.assertIn("github.com", header.tools_url)
        self.assertEqual(len(records), 2)

    def test_descriptor_is_printable_json(self) -> None:
        descriptor = make_header()[:STATIC_HEADER_SIZE]
        self.assertNotIn(b"\0", descriptor)
        self.assertTrue(all(byte == 10 or 32 <= byte <= 126
                            for byte in descriptor))
        parsed = json.loads(descriptor.decode("ascii"))
        self.assertEqual(parsed["adc_gains"], [8, 8, 8, 1, 1, 1, 1, 1])
        self.assertEqual(parsed["data_offset_bytes"], 32768)

    def test_rejects_corrupt_container_header(self) -> None:
        corrupt = bytearray(make_header())
        corrupt[140] ^= 1
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(corrupt + self.record)

            with self.assertRaisesRegex(RecordingFileError, "header CRC"):
                list(iter_adc_records(path))

    def test_rejects_old_binary_header(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(b"\\GEO" + bytes(DATA_OFFSET - 4))
            with self.assertRaisesRegex(RecordingFileError,
                                        "unknown recording format"):
                list(iter_adc_records(path))

    def test_header_only_container_is_valid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(make_header())

            records = list(iter_adc_records(path))

        self.assertEqual(records, [])

    def test_rejects_partial_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(self.record + b"partial")

            with self.assertRaisesRegex(RecordingFileError, "multiple of 512"):
                list(iter_adc_records(path))


if __name__ == "__main__":
    unittest.main()
