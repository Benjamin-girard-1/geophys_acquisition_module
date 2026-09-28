from __future__ import annotations

import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

DEV_TOOLS_ROOT = Path(__file__).resolve().parents[1]
SOFTWARE_ROOT = Path(__file__).resolve().parents[3]
DATA_PACKAGE_ROOT = SOFTWARE_ROOT / "packages" / "data"
REPOSITORY_ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(DEV_TOOLS_ROOT))
sys.path.insert(0, str(DATA_PACKAGE_ROOT))

import geophys_dev_tools.plot as plot_module  # noqa: E402
from geophys_dev_tools.plot import (  # noqa: E402
    ADC_FULL_SCALE_COUNTS,
    amplitudes_to_dbfs,
    calculate_frequency_spectrum,
    default_data_directory,
    load_recording,
    RecordingPlotData,
    RecordingPlotWindow,
    resolve_plot_channels,
    select_recording_file,
)
from geophys_data import RecordingFileError  # noqa: E402


class PlotDataTests(unittest.TestCase):
    def setUp(self) -> None:
        vector_path = (
            REPOSITORY_ROOT / "shared" / "protocol" / "test_vectors" /
            "data_block" / "valid" / "adc_record_8ch.txt"
        )
        self.record = bytes.fromhex(vector_path.read_text(encoding="ascii"))

    def test_loads_samples_for_each_channel(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(self.record)

            data = load_recording(path)

        self.assertEqual(data.record_count, 1)
        self.assertEqual(data.channel_indices, tuple(range(8)))
        self.assertEqual(len(data.timestamps_s), 20)
        self.assertEqual(data.samples[0][0], -80)
        self.assertEqual(data.samples[7][-1], 79)
        self.assertEqual(data.payload_discontinuities, 0)
        self.assertEqual(data.sequence_discontinuities, 0)

    def test_selects_channels_zero_through_three(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(self.record)
            data = load_recording(path)

        self.assertEqual(
            resolve_plot_channels(data, "0-3"),
            (0, 1, 2, 3),
        )

    @patch.object(plot_module, "show_plot")
    def test_main_plots_channels_zero_through_three_by_default(
            self, show_plot) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(self.record)

            result = plot_module.main([str(path)])

        self.assertEqual(result, 0)
        plotted_data, plotted_channels = show_plot.call_args.args
        self.assertEqual(plotted_data.channel_indices, tuple(range(8)))
        self.assertEqual(plotted_channels, (0, 1, 2, 3))

    def test_rejects_channel_group_missing_from_recording(self) -> None:
        # This test only exercises selection and therefore can construct the
        # minimal plot-data shape directly without rewriting a record CRC.
        data = plot_module.RecordingPlotData(
            path=Path("recording"),
            channel_indices=(0, 1, 2, 3),
            timestamps_s=(0.0,),
            samples={channel: (0,) for channel in range(4)},
            record_count=1,
            status_counts={0: 1},
            payload_discontinuities=0,
            sequence_discontinuities=0,
            missing_conversions=0,
        )

        with self.assertRaisesRegex(
                RecordingFileError, "CH4, CH5, CH6, CH7"):
            resolve_plot_channels(data, "4-7")

    def test_fft_finds_a_tone_frequency(self) -> None:
        sample_rate_hz = 1000
        valid_tone_hz = 50
        invalid_start_tone_hz = 200
        invalid_end_tone_hz = 300
        sample_count = 3000
        timestamps = tuple(
            index / sample_rate_hz for index in range(sample_count)
        )
        samples = tuple(
            round(
                (1000 if 1.0 <= timestamp < 2.0 else 5000) * math.sin(
                    2 * math.pi *
                    (
                        invalid_start_tone_hz
                        if timestamp < 1.0
                        else (
                            valid_tone_hz
                            if timestamp < 2.0
                            else invalid_end_tone_hz
                        )
                    ) * timestamp
                )
            )
            for timestamp in timestamps
        )
        data = RecordingPlotData(
            path=Path("tone"),
            channel_indices=(0,),
            timestamps_s=timestamps,
            samples={0: samples},
            record_count=1,
            status_counts={0: 1},
            payload_discontinuities=0,
            sequence_discontinuities=0,
            missing_conversions=0,
        )

        spectrum = calculate_frequency_spectrum(
            data,
            0,
            start_trim_s=1.0,
            end_trim_s=1.0,
        )
        peak_index = int(spectrum.amplitudes[1:].argmax()) + 1

        self.assertAlmostEqual(
            float(spectrum.frequencies_hz[peak_index]),
            valid_tone_hz,
        )

    def test_fft_does_not_decimate_or_alias_long_recordings(self) -> None:
        sample_rate_hz = 1000
        tone_hz = 450
        sample_count = plot_module.MAX_PLOT_POINTS + 1000
        timestamps = tuple(
            index / sample_rate_hz for index in range(sample_count)
        )
        samples = tuple(
            round(1000 * math.sin(2 * math.pi * tone_hz * timestamp))
            for timestamp in timestamps
        )
        data = RecordingPlotData(
            path=Path("long-tone"),
            channel_indices=(0,),
            timestamps_s=timestamps,
            samples={0: samples},
            record_count=1,
            status_counts={0: 1},
            payload_discontinuities=0,
            sequence_discontinuities=0,
            missing_conversions=0,
        )

        spectrum = calculate_frequency_spectrum(
            data,
            0,
            start_trim_s=0.0,
            end_trim_s=0.0,
        )
        peak_index = int(spectrum.amplitudes[1:].argmax()) + 1

        self.assertAlmostEqual(
            float(spectrum.frequencies_hz[peak_index]),
            tone_hz,
            places=2,
        )
        self.assertAlmostEqual(float(spectrum.frequencies_hz[-1]), 500.0)

    def test_fft_dbfs_conversion(self) -> None:
        converted = amplitudes_to_dbfs(
            plot_module.np.asarray(
                (ADC_FULL_SCALE_COUNTS, ADC_FULL_SCALE_COUNTS / 2, 0.0)
            )
        )

        self.assertAlmostEqual(float(converted[0]), 0.0)
        self.assertAlmostEqual(float(converted[1]), -6.0206, places=3)
        self.assertEqual(float(converted[2]), plot_module.FFT_DBFS_FLOOR)

    def test_waveform_and_fft_channel_selections_are_independent(self) -> None:
        timestamps = tuple(index / 1000 for index in range(3000))
        data = RecordingPlotData(
            path=Path("recording"),
            channel_indices=tuple(range(8)),
            timestamps_s=timestamps,
            samples={
                channel: tuple(
                    index + channel for index in range(len(timestamps))
                )
                for channel in range(8)
            },
            record_count=100,
            status_counts={0: 100},
            payload_discontinuities=0,
            sequence_discontinuities=0,
            missing_conversions=0,
        )

        with patch("matplotlib.pyplot.draw"):
            window = RecordingPlotWindow(data, (0,), (1,))
            self.assertGreaterEqual(
                float(window._waveform_lines[0].get_xdata()[0]),
                1.0,
            )
            self.assertTrue(window._waveform_lines[0].get_visible())
            self.assertFalse(window._waveform_lines[1].get_visible())
            self.assertFalse(window._fft_lines[0].get_visible())
            self.assertTrue(window._fft_lines[1].get_visible())
            self.assertEqual(
                window.fft_axis.get_ylabel(),
                "Amplitude (dBFS)",
            )
            self.assertGreater(
                float(window._fft_lines[1].get_xdata()[0]),
                0.0,
            )

            window.fft_scale_selector.set_active(1)

            self.assertEqual(
                window.fft_axis.get_ylabel(),
                "Amplitude (ADC counts)",
            )
            self.assertTrue(window._fft_lines[1].get_visible())

            window.trim_start_box.set_val("0.5")
            window.trim_end_box.set_val("0.25")

            waveform_times = window._waveform_lines[1].get_xdata()
            self.assertGreaterEqual(float(waveform_times[0]), 0.5)
            self.assertLessEqual(float(waveform_times[-1]), 2.749)
            self.assertEqual(window.start_trim_s, 0.5)
            self.assertEqual(window.end_trim_s, 0.25)

            window.waveform_checkboxes.set_active(0)

            self.assertFalse(window._waveform_lines[0].get_visible())
            self.assertTrue(window._fft_lines[1].get_visible())
            window._plt.close(window.figure)

    def test_short_recording_has_no_valid_plot_data(self) -> None:
        data = RecordingPlotData(
            path=Path("short-recording"),
            channel_indices=(0,),
            timestamps_s=(0.0, 0.5),
            samples={0: (10, 20)},
            record_count=1,
            status_counts={0: 1},
            payload_discontinuities=0,
            sequence_discontinuities=0,
            missing_conversions=0,
        )

        with self.assertRaisesRegex(
                RecordingFileError, "trim settings"):
            calculate_frequency_spectrum(data, 0)

    def test_default_selector_starts_in_external_data_directory(self) -> None:
        calls = []

        def fake_dialog(**options: object) -> str:
            calls.append(options)
            return str(default_data_directory() / "scratch" / "test_2")

        selected = select_recording_file(fake_dialog)

        self.assertEqual(
            calls[0]["initialdir"], str(default_data_directory())
        )
        self.assertEqual(selected.name, "test_2")

    def test_canceling_selector_returns_none(self) -> None:
        self.assertIsNone(select_recording_file(lambda **_options: ""))

    def test_macos_uses_cocoa_file_selector(self) -> None:
        expected = default_data_directory() / "scratch" / "test_2"
        with patch.object(plot_module.sys, "platform", "darwin"), \
                patch.object(
                    plot_module,
                    "_select_recording_file_macos",
                    return_value=str(expected),
                ) as selector:
            selected = select_recording_file()

        selector.assert_called_once_with(default_data_directory())
        self.assertEqual(selected, expected.resolve())


if __name__ == "__main__":
    unittest.main()
