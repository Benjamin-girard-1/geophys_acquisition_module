from __future__ import annotations

import csv
from pathlib import Path
import sys
import tempfile
import unittest

DEV_TOOLS_ROOT = Path(__file__).resolve().parents[1]
SOFTWARE_ROOT = Path(__file__).resolve().parents[3]
DATA_PACKAGE_ROOT = SOFTWARE_ROOT / "packages" / "data"
REPOSITORY_ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(DEV_TOOLS_ROOT))
sys.path.insert(0, str(DATA_PACKAGE_ROOT))

from geophys_dev_tools.convert import (  # noqa: E402
    CSV_COLUMNS,
    convert_recording_to_csv,
    default_output_path,
)


class ConvertTests(unittest.TestCase):
    def setUp(self) -> None:
        vector_path = (
            REPOSITORY_ROOT / "shared" / "protocol" / "test_vectors" /
            "data_block" / "valid" / "adc_record_8ch.txt"
        )
        self.record = bytes.fromhex(vector_path.read_text(encoding="ascii"))

    def test_converts_valid_record_to_wide_csv(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "recording"
            destination = root / "recording.csv"
            source.write_bytes(self.record)

            summary = convert_recording_to_csv(source, destination)
            with destination.open(newline="", encoding="utf-8") as stream:
                rows = list(csv.DictReader(stream))

        self.assertEqual(tuple(rows[0]), CSV_COLUMNS)
        self.assertEqual(len(rows), 20)
        self.assertEqual(rows[0]["time_s"], "0.0000000")
        self.assertEqual(rows[0]["monotonic_timestamp_100ns"],
                         "72623859790382856")
        self.assertEqual(rows[0]["conversion_sequence"], "287454020")
        self.assertEqual(rows[0]["record_index"], "0")
        self.assertEqual(rows[0]["payload_number"], "16909060")
        self.assertEqual(rows[0]["status_name"], "good")
        self.assertEqual(rows[0]["channel_mask"], "0xff")
        self.assertEqual(rows[0]["packed_gain"], "0xe4e4")
        self.assertEqual(rows[0]["sample_period_100ns"], "10000")
        self.assertEqual(rows[0]["ch0_counts"], "-80")
        self.assertEqual(rows[0]["ch7_counts"], "-73")
        self.assertEqual(rows[-1]["ch0_counts"], "72")
        self.assertEqual(rows[-1]["ch7_counts"], "79")
        self.assertEqual(summary.record_count, 1)
        self.assertEqual(summary.conversion_count, 20)
        self.assertEqual(summary.status_counts, {0: 1})

    def test_refuses_to_replace_existing_csv_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "recording"
            destination = root / "recording.csv"
            source.write_bytes(self.record)
            destination.write_text("keep me", encoding="utf-8")

            with self.assertRaisesRegex(FileExistsError, "--force"):
                convert_recording_to_csv(source, destination)

            self.assertEqual(
                destination.read_text(encoding="utf-8"),
                "keep me",
            )

    def test_force_replaces_existing_csv(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "recording"
            destination = root / "recording.csv"
            source.write_bytes(self.record)
            destination.write_text("old", encoding="utf-8")

            convert_recording_to_csv(source, destination, overwrite=True)

            self.assertTrue(
                destination.read_text(encoding="utf-8").startswith("time_s,")
            )

    def test_default_output_is_in_supplied_scratch_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            scratch = Path(directory) / "scratch"
            output = default_output_path("capture.bin", scratch)

        self.assertEqual(output, scratch / "capture.csv")


if __name__ == "__main__":
    unittest.main()
