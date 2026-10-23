"""Checks for the analysis-oriented NumPy recording reader."""

from __future__ import annotations

import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

import numpy as np

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

from geophys_recording import (  # noqa: E402
    DATA_OFFSET,
    RecordingFileError,
    read_recording_numpy,
)


def make_record(record_number: int, timestamp_offset_ticks: int = 0) -> bytes:
    record = bytearray(512)
    record[:4] = b"\\DAT"
    record[4] = 0x0F
    struct.pack_into("<HIIQI", record, 6, 0x003F, record_number,
                     40 * record_number,
                     400_000 * record_number + timestamp_offset_ticks, 10_000)
    for sample in range(40):
        for channel in range(4):
            value = (100_000 * channel + 40 * record_number + sample)
            if channel == 2:
                value = -value
            offset = 28 + (sample * 4 + channel) * 3
            record[offset:offset + 3] = (value & 0xFFFFFF).to_bytes(3, "little")
    struct.pack_into("<I", record, 508, zlib.crc32(record[:508]))
    return bytes(record)


def make_header() -> bytes:
    fields = {
        "format": "GEO_RECORDING", "format_version": 2,
        "tools_url": "https://example.org/tools", "recording_name": "test",
        "device_id": "02:11:22:33:44:55", "hardware_version": 2,
        "hardware_revision": 1, "firmware_version": 1, "protocol_version": 1,
        "start_monotonic_timestamp_100ns": 0, "start_unix_timestamp_us": 0,
        "adc_sample_rate_sps": 1000, "adc_sample_period_100ns": 10_000,
        "adc_channel_mask": 15, "adc_channel_count": 4,
        "adc_gains": [8, 8, 8, 1, 1, 1, 1, 1], "packed_gain": 63,
        "card_types": [1, 0], "gnss_state": 0, "imu_state": 0,
        "imu_averaging_time_ms": 0, "power_rail_flags": 0,
        "channel_slots": [1, 1, 1, 1, 2, 2, 2, 2],
        "channel_inputs": [0, 1, 2, 3, 0, 1, 2, 3],
        "descriptor_size_bytes": 4096, "checkpoint_offset_bytes": 4096,
        "checkpoint_entry_size_bytes": 32, "checkpoint_capacity": 896,
        "checkpoint_interval_ms": 10_000, "data_offset_bytes": DATA_OFFSET,
        "record_size_bytes": 512, "sample_encoding": "signed24_le",
        "header_crc32": "00000000",
    }
    descriptor = bytearray(
        (json.dumps(fields, indent=2) + "\n").encode("ascii").ljust(4095, b" ")
        + b"\n"
    )
    checksum = zlib.crc32(descriptor)
    marker = b'"header_crc32": "00000000"'
    descriptor = descriptor.replace(
        marker, f'"header_crc32": "{checksum:08X}"'.encode("ascii")
    )
    return bytes(descriptor) + bytes(DATA_OFFSET - 4096)


class NumpyRecordingTests(unittest.TestCase):
    def test_reads_window_across_records_with_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(make_header() + make_record(0) +
                             make_record(1, timestamp_offset_ticks=500))
            data = read_recording_numpy(path, channels=(2, 0),
                                        start_s=0.035, stop_s=0.045)

        self.assertEqual(data.samples.shape, (10, 2))
        self.assertEqual(data.samples.dtype, np.int32)
        np.testing.assert_array_equal(data.channel(2),
                                      -(200_000 + np.arange(35, 45)))
        np.testing.assert_array_equal(data.channel(0), np.arange(35, 45))
        self.assertEqual(data.channels, (2, 0))
        self.assertEqual(data.adc_gains, (8, 8))
        self.assertEqual(data.sample_rate_hz, 1000)
        self.assertEqual(data.start_s, 0.035)
        self.assertEqual(data.duration_s, 0.08)
        self.assertEqual(data.timestamps_s.shape, (10,))
        self.assertAlmostEqual(data.timestamps_s[0], 0.035)
        self.assertAlmostEqual(data.timestamps_s[4], 0.039)
        self.assertAlmostEqual(data.timestamps_s[5], 0.04005)
        self.assertAlmostEqual(data.timestamps_s[-1], 0.04405)
        self.assertEqual(data.header.recording_name, "test")

    def test_reads_headerless_file_and_all_channels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(make_record(0))
            data = read_recording_numpy(path)

        self.assertEqual(data.samples.shape, (40, 4))
        self.assertEqual(data.channels, (0, 1, 2, 3))
        self.assertEqual(data.adc_gains, (8, 8, 8, 1))
        self.assertIsNone(data.header)
        self.assertEqual(data.channel(2)[0], -200_000)
        self.assertAlmostEqual(data.timestamps_s[-1], 0.039)

    def test_rejects_crc_error_and_sequence_gap(self) -> None:
        damaged = bytearray(make_record(1))
        damaged[30] ^= 1
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(make_record(0) + damaged)
            with self.assertRaisesRegex(RecordingFileError, "CRC error"):
                read_recording_numpy(path, channels=2)

            skipped = bytearray(make_record(1))
            struct.pack_into("<I", skipped, 12, 41)
            struct.pack_into("<I", skipped, 508, zlib.crc32(skipped[:508]))
            path.write_bytes(make_record(0) + skipped)
            with self.assertRaisesRegex(RecordingFileError,
                                        "missing or out-of-order"):
                read_recording_numpy(path, channels=2)


if __name__ == "__main__":
    unittest.main()
