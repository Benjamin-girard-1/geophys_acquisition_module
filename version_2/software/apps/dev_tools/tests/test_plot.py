from __future__ import annotations

import math
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np

DEV_TOOLS_ROOT = Path(__file__).resolve().parents[1]
SOFTWARE_ROOT = Path(__file__).resolve().parents[3]
RECORDING_PACKAGE_ROOT = SOFTWARE_ROOT / "packages" / "recording"
REPOSITORY_ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(DEV_TOOLS_ROOT))
sys.path.insert(0, str(RECORDING_PACKAGE_ROOT))

import geophys_dev_tools.plot as plot_module  # noqa: E402
from geophys_dev_tools.plot import (  # noqa: E402
    ADC_FULL_SCALE_COUNTS,
    amplitudes_to_dbfs,
    calculate_frequency_spectrum,
    calculate_frequency_spectra,
    default_data_directory,
    load_recording,
    magnetic_nt_per_count,
    thermistor_celsius_from_counts,
    RecordingPlotData,
    resolve_plot_channels,
    select_recording_file,
)
from geophys_recording import RecordingFileError  # noqa: E402


class PlotDataTests(unittest.TestCase):
    def setUp(self) -> None:
        vector_path = (
            REPOSITORY_ROOT / "shared" / "protocol" / "test_vectors" /
            "data_block" / "valid" / "adc_record_8ch.txt"
        )
        self.record = bytes.fromhex(vector_path.read_text(encoding="ascii"))

    def test_default_data_directory_finds_sibling_folder(self) -> None:
        self.assertEqual(
            default_data_directory(),
            REPOSITORY_ROOT.parent / "geophys_experiments",
        )

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
        self.assertAlmostEqual(data.sample_period_s, 0.001)
        self.assertIsInstance(data.timestamps_s, np.ndarray)
        self.assertEqual(data.timestamps_s.dtype, np.float64)
        self.assertTrue(data.timestamps_s.flags.c_contiguous)
        self.assertFalse(data.timestamps_s.flags.writeable)
        for values in data.samples.values():
            self.assertIsInstance(values, np.ndarray)
            self.assertEqual(values.dtype, np.int32)
            self.assertTrue(values.flags.c_contiguous)
            self.assertFalse(values.flags.writeable)
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
        self.assertEqual(resolve_plot_channels(data, "3"), (3,))

    def test_thermistor_bridge_conversion(self) -> None:
        self.assertAlmostEqual(
            float(thermistor_celsius_from_counts(0)), 25.0
        )
        self.assertLess(
            float(thermistor_celsius_from_counts(-550_000)), 30.0
        )
        self.assertGreater(
            float(thermistor_celsius_from_counts(-550_000)), 29.0
        )
        self.assertLess(
            float(thermistor_celsius_from_counts(100_000)), 25.0
        )

    @patch.object(plot_module, "show_plot")
    def test_main_plots_axes_without_thermistor_by_default(
            self, show_plot) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recording"
            path.write_bytes(self.record)

            result = plot_module.main([str(path)])

        self.assertEqual(result, 0)
        plotted_data, plotted_channels = show_plot.call_args.args
        self.assertEqual(plotted_data.channel_indices, tuple(range(8)))
        self.assertEqual(plotted_channels, (0, 1, 2))

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
        sample_count = 101_000
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
        self.assertEqual(
            spectrum.frequencies_hz.size,
            sample_count // 2 + 1,
        )
        self.assertEqual(spectrum.amplitudes.size, sample_count // 2 + 1)

    def test_batched_fft_matches_individual_fft_without_bin_reduction(
            self) -> None:
        sample_rate_hz = 2000
        sample_count = 4096
        timestamps = np.arange(sample_count) / sample_rate_hz
        data = RecordingPlotData(
            path=Path("two-channels"),
            channel_indices=(0, 1),
            timestamps_s=timestamps,
            samples={
                0: np.rint(
                    2000 * np.sin(2 * np.pi * 125 * timestamps)
                ),
                1: np.rint(
                    3000 * np.sin(2 * np.pi * 375 * timestamps)
                ),
            },
            record_count=1,
            status_counts={0: 1},
            payload_discontinuities=0,
            sequence_discontinuities=0,
            missing_conversions=0,
        )

        batched = calculate_frequency_spectra(
            data,
            (0, 1),
            start_trim_s=0.0,
        )
        individual = calculate_frequency_spectrum(
            data,
            1,
            start_trim_s=0.0,
        )

        np.testing.assert_array_equal(
            batched[1].frequencies_hz,
            individual.frequencies_hz,
        )
        np.testing.assert_allclose(
            batched[1].amplitudes,
            individual.amplitudes,
            rtol=1e-7,
            atol=1e-12,
        )
        self.assertEqual(
            batched[0].frequencies_hz.size,
            sample_count // 2 + 1,
        )

    def test_rectangular_window_keeps_event_spectrum_when_selection_moves(
            self) -> None:
        timestamps = np.arange(4000, dtype=np.float64)
        samples = np.zeros(4000, dtype=np.int32)
        samples[1000] = 100_000
        data = RecordingPlotData(
            Path("single-event"), (0,), timestamps, {0: samples},
            1, {0: 1}, 0, 0, 0, 1.0,
        )

        def spectrum(start: int, window_type: str):
            return calculate_frequency_spectrum(
                data, 0, start_trim_s=float(start),
                end_trim_s=float(3999 - (start + 2047)),
                window_type=window_type,
            )

        rectangular_a = spectrum(0, "rectangular")
        rectangular_b = spectrum(200, "rectangular")
        np.testing.assert_allclose(
            rectangular_a.amplitudes,
            rectangular_b.amplitudes,
            rtol=1e-12, atol=1e-12,
        )
        hann_a = spectrum(0, "hann")
        hann_b = spectrum(200, "hann")
        self.assertGreater(
            float(hann_a.amplitudes[100] / hann_b.amplitudes[100]), 1.05
        )

    def test_fft_rejects_nonuniform_timestamps_instead_of_mislabeling_bins(
            self) -> None:
        timestamps = np.arange(1000, dtype=float) / 1000.0
        timestamps[500:] += 0.001
        data = RecordingPlotData(
            path=Path("gap"),
            channel_indices=(0,),
            timestamps_s=timestamps,
            samples={0: np.arange(1000)},
            record_count=1,
            status_counts={0: 1},
            payload_discontinuities=0,
            sequence_discontinuities=1,
            missing_conversions=1,
        )

        with self.assertRaisesRegex(
                RecordingFileError, "uniformly sampled"):
            calculate_frequency_spectrum(
                data,
                0,
                start_trim_s=0.0,
            )

    def test_fft_dbfs_conversion(self) -> None:
        converted = amplitudes_to_dbfs(
            plot_module.np.asarray(
                (ADC_FULL_SCALE_COUNTS, ADC_FULL_SCALE_COUNTS / 2, 0.0)
            )
        )

        self.assertAlmostEqual(float(converted[0]), 0.0)
        self.assertAlmostEqual(float(converted[1]), -6.0206, places=3)
        self.assertEqual(float(converted[2]), plot_module.FFT_DBFS_FLOOR)

    def test_nominal_magnetic_scale_uses_per_channel_gain_and_axis_type(
            self) -> None:
        header = SimpleNamespace(
            adc_gains=(8, 4, 8, 1, 1, 1, 1, 1),
            card_types=(1, 0),
            channel_slots=(1, 1, 1, 1, 2, 2, 2, 2),
            channel_inputs=(0, 1, 2, 3, 0, 1, 2, 3),
        )
        scale = magnetic_nt_per_count(header, 0)
        expected = (
            2.5 * 100_000 /
            ((1 << 23) * 8 * 51 * 5.6 * 0.0032)
        )
        self.assertAlmostEqual(scale, expected)
        self.assertAlmostEqual(magnetic_nt_per_count(header, 1), 2 * scale)
        self.assertIsNone(magnetic_nt_per_count(header, 3))
        self.assertIsNone(magnetic_nt_per_count(header, 4))
        self.assertIsNone(magnetic_nt_per_count(None, 0))

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

    def test_file_selector_uses_qt(self) -> None:
        expected = default_data_directory() / "scratch" / "test_2"
        with patch.object(
                plot_module,
                "_select_recording_file_qt",
                return_value=str(expected),
        ) as selector:
            selected = select_recording_file()

        selector.assert_called_once_with(default_data_directory())
        self.assertEqual(selected, expected.resolve())


if __name__ == "__main__":
    unittest.main()
