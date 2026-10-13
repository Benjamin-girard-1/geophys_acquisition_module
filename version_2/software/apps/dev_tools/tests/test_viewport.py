from __future__ import annotations

from pathlib import Path
import sys
import unittest

import numpy as np

DEV_TOOLS_ROOT = Path(__file__).resolve().parents[1]
SOFTWARE_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(DEV_TOOLS_ROOT))
sys.path.insert(0, str(SOFTWARE_ROOT / "packages" / "data"))

from geophys_dev_tools.viewport import (  # noqa: E402
    select_visible_extrema,
)


class ViewportTests(unittest.TestCase):
    def test_wide_waveform_retains_a_short_positive_and_negative_event(self) -> None:
        x = np.arange(100_000, dtype=np.float64) / 1000
        y = np.zeros(x.size, dtype=np.int32)
        y[12_345] = 700
        y[12_346] = -900

        visible_x, visible_y = select_visible_extrema(x, y, 0, x[-1], 800)

        self.assertLessEqual(visible_x.size, 1602)
        self.assertEqual(float(visible_x[0]), float(x[0]))
        self.assertEqual(float(visible_x[-1]), float(x[-1]))
        self.assertEqual(visible_y[np.searchsorted(visible_x, x[12_345])], 700)
        self.assertEqual(visible_y[np.searchsorted(visible_x, x[12_346])], -900)

    def test_zoom_returns_every_original_point(self) -> None:
        x = np.arange(10_000, dtype=np.float64) / 1000
        y = np.arange(x.size, dtype=np.int32)

        visible_x, visible_y = select_visible_extrema(x, y, 1.0, 1.1, 200)

        np.testing.assert_array_equal(visible_x, x[1000:1101])
        np.testing.assert_array_equal(visible_y, y[1000:1101])
        self.assertTrue(np.shares_memory(visible_y, y))

    def test_wide_fft_retains_peak_and_local_floor(self) -> None:
        frequencies = np.arange(200_000, dtype=np.float64) * 0.01
        amplitudes = np.ones(frequencies.size, dtype=np.float64)
        amplitudes[41_321] = 1_000_000
        amplitudes[41_322] = 0.001

        visible_frequencies, visible_amplitudes = select_visible_extrema(
            frequencies, amplitudes, 0, frequencies[-1], 1200
        )

        self.assertLessEqual(visible_frequencies.size, 2402)
        peak = int(np.argmax(visible_amplitudes))
        self.assertEqual(visible_frequencies[peak], frequencies[41_321])
        self.assertEqual(visible_amplitudes[peak], amplitudes[41_321])
        floor = int(np.argmin(visible_amplitudes))
        self.assertEqual(visible_frequencies[floor], frequencies[41_322])
        close_x, close_y = select_visible_extrema(
            frequencies, amplitudes, 413.0, 413.5, 1200
        )
        self.assertEqual(close_x.size, 51)
        self.assertEqual(float(np.max(close_y)), 1_000_000)

    def test_empty_view(self) -> None:
        x = np.arange(10, dtype=np.float64)
        y = np.arange(10, dtype=np.int32)

        visible_x, visible_y = select_visible_extrema(x, y, 20, 30, 100)

        self.assertEqual(visible_x.size, 0)
        self.assertEqual(visible_y.size, 0)


if __name__ == "__main__":
    unittest.main()
