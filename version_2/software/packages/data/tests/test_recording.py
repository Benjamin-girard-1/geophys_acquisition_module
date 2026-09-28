from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(PACKAGE_ROOT))

from geophys_data import RecordingFileError, iter_adc_records  # noqa: E402


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

    def test_rejects_partial_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(self.record + b"partial")

            with self.assertRaisesRegex(RecordingFileError, "multiple of 512"):
                list(iter_adc_records(path))


if __name__ == "__main__":
    unittest.main()
