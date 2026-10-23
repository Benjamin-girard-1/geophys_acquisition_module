from __future__ import annotations

import os
from pathlib import Path
import sys
import time
from types import SimpleNamespace
import unittest

import numpy as np

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
DEV_TOOLS_ROOT = Path(__file__).resolve().parents[1]
SOFTWARE_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(DEV_TOOLS_ROOT))
sys.path.insert(0, str(SOFTWARE_ROOT / "packages" / "recording"))

from PySide6.QtWidgets import QApplication  # noqa: E402
from geophys_dev_tools.plot import (  # noqa: E402
    RecordingPlotData, magnetic_nt_per_count, valid_sample_bounds,
)
from geophys_dev_tools.qt_viewer import RecordingPlotWindow  # noqa: E402


class QtViewerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app = QApplication.instance() or QApplication([])

    def _wait_for_fft(self, window: RecordingPlotWindow) -> None:
        deadline = time.monotonic() + 5.0
        while "FFT:" not in window.status_label.text():
            self.app.processEvents()
            if time.monotonic() >= deadline:
                self.fail(f"FFT did not finish: {window.status_label.text()}")
            time.sleep(0.01)
        self.app.processEvents()

    def test_default_region_and_zoom_use_more_original_points(self) -> None:
        timestamps = np.arange(70_000, dtype=np.float64) / 1000
        samples = np.zeros(timestamps.size, dtype=np.int32)
        samples[100] = 8_000_000
        samples[55_123] = 25_000
        data = RecordingPlotData(
            Path("long-recording"), (0,), timestamps, {0: samples},
            1, {0: 1}, 0, 0, 0, 0.001,
        )
        window = RecordingPlotWindow(data, (0,), (0,))
        window.show()
        self._wait_for_fft(window)
        try:
            self.assertEqual(window.fft_window_box.currentData(), "hann")
            hann_amplitude = float(window._spectra[0].amplitudes[100])
            window.fft_window_box.setCurrentIndex(1)
            self._wait_for_fft(window)
            self.assertIn("rectangular window", window.status_label.text())
            self.assertNotAlmostEqual(
                float(window._spectra[0].amplitudes[100]), hann_amplitude
            )
            start, stop = window.fft_region.getRegion()
            self.assertAlmostEqual(start, 50.0)
            self.assertAlmostEqual(stop, timestamps[-1] - 10.0)
            self.assertIn(25_000, window._waveform_curves[0].yData)
            wide_count = len(window._waveform_curves[0].xData)
            self.assertLess(wide_count, len(timestamps))
            self.assertLess(window.waveform_plot.viewRange()[1][1], 100_000)
            window.waveform_y_box.setCurrentIndex(1)
            self.assertGreater(window.waveform_plot.viewRange()[1][1], 8_000_000)
            window.waveform_y_box.setCurrentIndex(0)

            window.waveform_plot.setXRange(55.0, 56.0, padding=0)
            self.app.processEvents()
            close_count = len(window._waveform_curves[0].xData)
            self.assertEqual(close_count, 1001)

            window.fft_plot.setXRange(40.0, 60.0, padding=0)
            self.app.processEvents()
            zoomed_bins = len(window._fft_curves[0].xData)
            self.assertGreater(zoomed_bins, 0)
            self.assertLess(zoomed_bins, len(window._spectra[0].frequencies_hz))

            window.fft_region.setRegion((52.0, 58.0))
            self._wait_for_fft(window)
            expected_start, expected_stop = valid_sample_bounds(
                data, 52.0, timestamps[-1] - 58.0
            )
            self.assertEqual(
                window._spectra[0].sample_count,
                expected_stop - expected_start,
            )
            left, right = window.fft_plot.viewRange()[0]
            self.assertAlmostEqual(left, 40.0)
            self.assertAlmostEqual(right, 60.0)
        finally:
            window.close()

    def test_short_recording_uses_full_region(self) -> None:
        timestamps = np.arange(2000, dtype=np.float64) / 1000
        data = RecordingPlotData(
            Path("short-recording"), (0,), timestamps,
            {0: np.ones(timestamps.size, dtype=np.int32)},
            1, {0: 1}, 0, 0, 0, 0.001,
        )
        window = RecordingPlotWindow(data, (0,), (0,))
        window.show()
        self._wait_for_fft(window)
        try:
            self.assertEqual(window.fft_region.getRegion(), (0.0, 1.999))
            self.assertEqual(window._spectra[0].sample_count, len(timestamps))
            self.assertIn("too short", window.status_label.text())
        finally:
            window.close()

    def test_waveform_and_fft_channels_are_independent(self) -> None:
        timestamps = np.arange(3000, dtype=np.float64) / 1000
        data = RecordingPlotData(
            Path("two-channels"), (0, 1), timestamps,
            {
                0: np.zeros(timestamps.size, dtype=np.int32),
                1: np.ones(timestamps.size, dtype=np.int32),
            },
            1, {0: 1}, 0, 0, 0, 0.001,
        )
        window = RecordingPlotWindow(data, (0,), (1,))
        window.show()
        self._wait_for_fft(window)
        try:
            self.assertGreater(len(window._waveform_curves[0].xData), 0)
            self.assertIsNone(window._waveform_curves[1].xData)
            self.assertIsNone(window._fft_curves[0].xData)
            self.assertGreater(len(window._fft_curves[1].xData), 0)

            window._waveform_checks[1].setChecked(True)
            self.assertGreater(len(window._waveform_curves[1].xData), 0)
            self.assertIsNone(window._fft_curves[0].xData)

            window._fft_checks[0].setChecked(True)
            self._wait_for_fft(window)
            self.assertIn(0, window._spectra)
            self.assertIn(1, window._spectra)
        finally:
            window.close()

    def test_thermistor_can_be_selected_from_nominal_magnetic_view(self) -> None:
        header = SimpleNamespace(
            hardware_version=2,
            hardware_revision=1,
            adc_gains=(8, 8, 8, 1, 1, 1, 1, 1),
            card_types=(1, 0),
            channel_slots=(1, 1, 1, 1, 2, 2, 2, 2),
            channel_inputs=(0, 1, 2, 3, 0, 1, 2, 3),
        )
        timestamps = np.arange(3000, dtype=np.float64) / 1000
        samples = np.rint(10_000 * np.sin(2 * np.pi * 50 * timestamps))
        data = RecordingPlotData(
            Path("magnetic"), (0, 3), timestamps,
            {0: samples.astype(np.int32),
             3: np.ones(timestamps.size, dtype=np.int32)},
            1, {0: 1}, 0, 0, 0, 0.001, header,
        )
        window = RecordingPlotWindow(data, (0,), (0,))
        window.show()
        self._wait_for_fft(window)
        try:
            scale = magnetic_nt_per_count(header, 0)
            self.assertEqual(window.waveform_scale_box.currentData(), "nt")
            self.assertEqual(window.fft_scale_box.currentData(), "nt")
            self.assertTrue(window._waveform_checks[3].isEnabled())
            self.assertTrue(window._fft_checks[3].isEnabled())
            self.assertFalse(window._waveform_checks[3].isChecked())
            self.assertFalse(window._fft_checks[3].isChecked())
            self.assertEqual(window._waveform_checks[3].text(), "CH3 Temp")
            self.assertEqual(set(window._spectra), {0})
            self.assertAlmostEqual(
                float(np.max(window._waveform_curves[0].yData)),
                10_000 * scale, places=3,
            )
            self.assertAlmostEqual(
                float(window._fft_display_values[0][150]),
                float(window._spectra[0].amplitudes[150]) * scale,
            )
            window._waveform_checks[3].setChecked(True)
            self.assertEqual(window.waveform_scale_box.currentData(), "nt")
            self.assertEqual(window.fft_scale_box.currentData(), "nt")
            self.assertTrue(window._waveform_checks[3].isChecked())
            self.assertAlmostEqual(
                float(np.max(window._waveform_curves[0].yData)),
                10_000 * scale, places=3,
            )
            self.assertGreater(len(window._waveform_curves[3].xData), 0)
            self.assertAlmostEqual(
                float(window._waveform_curves[3].yData[0]), 25.0,
                places=3,
            )
            self.assertTrue(
                window.waveform_plot.getPlotItem().getAxis("right").isVisible()
            )

            window._fft_checks[3].setChecked(True)
            self._wait_for_fft(window)
            self.assertEqual(window.fft_scale_box.currentData(), "counts")
            self.assertEqual(set(window._spectra), {0, 3})
            self.assertGreater(len(window._fft_curves[3].xData), 0)
        finally:
            window.close()

        mixed = RecordingPlotWindow(data, (0, 3), (0, 3))
        mixed.show()
        self._wait_for_fft(mixed)
        try:
            self.assertEqual(mixed.waveform_scale_box.currentData(), "nt")
            self.assertEqual(mixed.fft_scale_box.currentData(), "counts")
            self.assertTrue(mixed._waveform_checks[3].isChecked())
            self.assertEqual(set(mixed._spectra), {0, 3})
        finally:
            mixed.close()


if __name__ == "__main__":
    unittest.main()
