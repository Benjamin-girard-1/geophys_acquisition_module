from __future__ import annotations

from pathlib import Path
import struct
import sys
import unittest
import zlib

HOST_APP = Path(__file__).resolve().parents[1]
REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(HOST_APP))

from geophys_host.adc_record import (  # noqa: E402
    AdcRecordError,
    decode_adc_record,
)


class AdcRecordTests(unittest.TestCase):
    def setUp(self) -> None:
        path = (
            REPOSITORY / "shared" / "protocol" / "test_vectors" /
            "data_block" / "valid" / "adc_record_8ch.txt"
        )
        self.record = bytes.fromhex(path.read_text(encoding="ascii"))

    def test_shared_eight_channel_vector(self) -> None:
        decoded = decode_adc_record(self.record)
        self.assertEqual(decoded.channel_mask, 0xFF)
        self.assertEqual(decoded.status, 0)
        self.assertEqual(decoded.packed_gain, 0xE4E4)
        self.assertEqual(decoded.payload_number, 0x01020304)
        self.assertEqual(decoded.first_conversion_sequence, 0x11223344)
        self.assertEqual(
            decoded.first_monotonic_timestamp_100ns,
            0x0102030405060708)
        self.assertEqual(decoded.sample_period_100ns, 10_000)
        self.assertEqual(decoded.channel_indices, tuple(range(8)))
        self.assertEqual(len(decoded.conversions), 20)
        self.assertEqual(decoded.conversions[0], tuple(range(-80, -72)))
        self.assertEqual(decoded.conversions[-1], tuple(range(72, 80)))

    def test_corrupt_crc_is_rejected(self) -> None:
        corrupt = bytearray(self.record)
        corrupt[100] ^= 1
        with self.assertRaisesRegex(AdcRecordError, "CRC"):
            decode_adc_record(bytes(corrupt))

    def test_partial_record_is_rejected(self) -> None:
        with self.assertRaisesRegex(AdcRecordError, "512"):
            decode_adc_record(self.record[:-1])

    def test_four_channel_record_contains_forty_conversions(self) -> None:
        raw = bytearray(512)
        raw[:4] = b"\\DAT"
        raw[4] = 0x0F
        raw[5] = 0
        struct.pack_into("<HIIQI", raw, 6, 0, 7, 100, 1_000_000, 10_000)
        offset = 28
        expected = []
        for conversion_index in range(40):
            conversion = tuple(
                conversion_index * 4 + channel - 80
                for channel in range(4)
            )
            expected.append(conversion)
            for sample in conversion:
                raw[offset:offset + 3] = (
                    sample & 0xFFFFFF
                ).to_bytes(3, "little")
                offset += 3
        struct.pack_into(
            "<I", raw, 508, zlib.crc32(raw[:508]) & 0xFFFFFFFF)

        decoded = decode_adc_record(bytes(raw))

        self.assertEqual(decoded.channel_indices, (0, 1, 2, 3))
        self.assertEqual(decoded.conversions, tuple(expected))


if __name__ == "__main__":
    unittest.main()
