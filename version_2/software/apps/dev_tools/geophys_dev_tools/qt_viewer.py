"""Qt viewer for an offline recording and its exact selected-region FFT."""

from __future__ import annotations

import math

import numpy as np
import pyqtgraph as pg
from PySide6.QtCore import QEvent, QObject, QRunnable, QSignalBlocker, QThreadPool, QTimer, Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from geophys_recording import RecordingFileError

from .plot import (
    RecordingPlotData,
    STATUS_NAMES,
    amplitudes_to_dbfs,
    calculate_frequency_spectra,
    magnetic_nt_per_count,
    thermistor_celsius_from_counts,
    valid_sample_bounds,
)
from .viewport import select_visible_extrema


class _FftSignals(QObject):
    finished = Signal(int, object)
    failed = Signal(int, str)


class _FftJob(QRunnable):
    def __init__(self, generation: int, data: RecordingPlotData,
                 channels: tuple[int, ...], start_trim_s: float,
                 end_trim_s: float, window_type: str) -> None:
        super().__init__()
        self.generation = generation
        self.data = data
        self.channels = channels
        self.start_trim_s = start_trim_s
        self.end_trim_s = end_trim_s
        self.window_type = window_type
        self.signals = _FftSignals()

    def run(self) -> None:
        try:
            spectra = calculate_frequency_spectra(
                self.data,
                self.channels,
                self.start_trim_s,
                self.end_trim_s,
                self.window_type,
            )
        except (RecordingFileError, ValueError, MemoryError) as error:
            self.signals.failed.emit(self.generation, str(error))
        else:
            self.signals.finished.emit(self.generation, spectra)


class RecordingPlotWindow(QWidget):
    """Large waveform with an editable FFT selection and a zoomable spectrum."""

    def __init__(self, data: RecordingPlotData,
                 waveform_channels: tuple[int, ...],
                 fft_channels: tuple[int, ...]) -> None:
        super().__init__()
        self.data = data
        self._duration_s = float(data.timestamps_s[-1])
        self._fft_generation = 0
        self._fft_busy = False
        self._pending_fft_request = None
        self._fft_jobs: dict[int, _FftJob] = {}
        self._spectra = {}
        self._fft_display_values = {}
        self._fft_range_initialized = False
        self._magnetic_scales = {
            channel: scale
            for channel in data.channel_indices
            if (scale := magnetic_nt_per_count(data.recording_header, channel))
            is not None
        }
        header = data.recording_header
        self._thermistor_channels = {
            channel for channel in data.channel_indices
            if header is not None and
            header.hardware_version == 2 and header.hardware_revision == 1 and
            0 < header.channel_slots[channel] <= len(header.card_types) and
            header.card_types[header.channel_slots[channel] - 1] == 1 and
            header.channel_inputs[channel] == 3
        }
        self._thread_pool = QThreadPool(self)
        self._thread_pool.setMaxThreadCount(1)
        self.setWindowTitle(f"Geophys recording — {data.path.name}")
        self.resize(1500, 950)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(8)
        status_issues = ", ".join(
            f"{STATUS_NAMES.get(status, str(status))} {count} records"
            for status, count in sorted(data.status_counts.items())
            if status != 0
        )
        hours, remainder = divmod(int(self._duration_s), 3600)
        minutes, seconds = divmod(remainder, 60)
        heading_text = (
            f"<b>{data.path.name}</b> · {data.timestamps_s.size:,} samples · "
            f"{self._duration_s:.3f} s "
            f"({hours:02d}:{minutes:02d}:{seconds:02d}) · "
            f"{data.missing_conversions:,} missing conversions"
        )
        if status_issues:
            heading_text += f" · {status_issues}"
        heading = QLabel(heading_text)
        layout.addWidget(heading)

        controls = QGridLayout()
        controls.setHorizontalSpacing(12)
        self._waveform_checks = self._add_channel_checks(
            controls, 0, "Waveform", waveform_channels, self._on_waveform_changed
        )
        self._fft_checks = self._add_channel_checks(
            controls, 1, "FFT", fft_channels, self._on_fft_channels_changed
        )
        layout.addLayout(controls)

        trim_controls = QHBoxLayout()
        self.start_trim_box = self._make_trim_box()
        self.end_trim_box = self._make_trim_box()
        trim_controls.addWidget(QLabel("Trim start (s)"))
        trim_controls.addWidget(self.start_trim_box)
        trim_controls.addWidget(QLabel("Trim end (s)"))
        trim_controls.addWidget(self.end_trim_box)
        trim_controls.addSpacing(16)
        trim_controls.addWidget(QLabel("Waveform units"))
        self.waveform_scale_box = QComboBox()
        self.waveform_scale_box.addItem("ADC counts", "counts")
        if (waveform_channels and self._magnetic_scales and
                all(channel in self._magnetic_scales or
                    channel in self._thermistor_channels
                    for channel in waveform_channels)):
            self.waveform_scale_box.addItem("Nominal nT", "nt")
            self.waveform_scale_box.setCurrentIndex(1)
        elif self._magnetic_scales:
            self.waveform_scale_box.addItem("Nominal nT", "nt")
        self.waveform_scale_box.setToolTip(
            "Nominal field-equivalent scale for magnetic axes only. The bridge "
            "servo removes DC and very slow fields; absolute field and offset "
            "need calibration."
        )
        self.waveform_scale_box.currentIndexChanged.connect(
            self._on_waveform_scale_changed
        )
        trim_controls.addWidget(self.waveform_scale_box)
        trim_controls.addSpacing(16)
        trim_controls.addWidget(QLabel("Waveform Y fit"))
        self.waveform_y_box = QComboBox()
        self.waveform_y_box.addItems(("FFT selection", "Full recording"))
        self.waveform_y_box.currentIndexChanged.connect(self._fit_waveform_y)
        trim_controls.addWidget(self.waveform_y_box)
        trim_controls.addSpacing(16)
        trim_controls.addWidget(QLabel("FFT amplitude"))
        self.fft_scale_box = QComboBox()
        self.fft_scale_box.addItem("dBFS", "dbfs")
        self.fft_scale_box.addItem("ADC counts", "counts")
        if (fft_channels and
                all(channel in self._magnetic_scales
                    for channel in fft_channels)):
            self.fft_scale_box.addItem("Nominal nT peak", "nt")
            self.fft_scale_box.setCurrentIndex(2)
        elif self._magnetic_scales:
            self.fft_scale_box.addItem("Nominal nT peak", "nt")
            if any(channel not in self._magnetic_scales
                   for channel in fft_channels):
                self.fft_scale_box.setCurrentIndex(1)
        self.fft_scale_box.setToolTip(
            "Single-sided peak amplitude. Nominal nT uses the magnetic sensor, "
            "analog amplifier, and recorded ADC gain."
        )
        self.fft_scale_box.currentIndexChanged.connect(self._on_fft_scale_changed)
        trim_controls.addWidget(self.fft_scale_box)
        trim_controls.addSpacing(16)
        trim_controls.addWidget(QLabel("FFT window"))
        self.fft_window_box = QComboBox()
        self.fft_window_box.addItem("Hann (reduce leakage)", "hann")
        self.fft_window_box.addItem("Rectangular (equal weight)", "rectangular")
        self.fft_window_box.setToolTip(
            "Hann reduces leakage from tones but weights the middle of the "
            "selection more. Rectangular weights every selected sample equally."
        )
        self.fft_window_box.currentIndexChanged.connect(
            lambda _index: self._request_fft()
        )
        trim_controls.addWidget(self.fft_window_box)
        trim_controls.addStretch(1)
        layout.addLayout(trim_controls)
        self.calibration_note = QLabel(
            "Nominal nT shows field-equivalent changes; the bridge servo "
            "removes DC and very slow fields. Calibration is pending."
        )
        layout.addWidget(self.calibration_note)
        self._update_calibration_note()

        self._splitter = QSplitter(Qt.Orientation.Vertical)
        self.waveform_plot = pg.PlotWidget()
        self.fft_plot = pg.PlotWidget()
        self._configure_plot(
            self.waveform_plot,
            "Nominal field-equivalent (nT)"
            if self.waveform_scale_box.currentData() == "nt"
            else "Raw ADC counts",
            "Time since first sample (s)",
        )
        self._temperature_view = pg.ViewBox()
        waveform_item = self.waveform_plot.getPlotItem()
        waveform_item.showAxis("right")
        waveform_item.getAxis("right").setLabel("Nominal board temperature (°C)")
        waveform_item.getAxis("right").linkToView(self._temperature_view)
        waveform_item.scene().addItem(self._temperature_view)
        self._temperature_view.setXLink(self.waveform_plot.getViewBox())
        self._temperature_view.setMouseEnabled(x=False, y=False)
        self._temperature_view.setMenuEnabled(False)
        waveform_item.getViewBox().sigResized.connect(self._align_temperature_view)
        self._align_temperature_view()
        self._configure_plot(
            self.fft_plot,
            "Nominal peak amplitude (nT)"
            if self.fft_scale_box.currentData() == "nt"
            else "Amplitude (dBFS)",
            "Frequency (Hz)",
        )
        self._splitter.addWidget(self.waveform_plot)
        self._splitter.addWidget(self.fft_plot)
        self._splitter.setSizes((650, 300))
        layout.addWidget(self._splitter, 1)

        self.status_label = QLabel("")
        layout.addWidget(self.status_label)
        self.waveform_plot.addLegend(offset=(10, 10))
        self.fft_plot.addLegend(offset=(10, 10))
        self._waveform_curves = {}
        self._fft_curves = {}
        for color_index, channel in enumerate(data.channel_indices):
            pen = pg.mkPen(pg.intColor(color_index, hues=8), width=1)
            if channel in self._thermistor_channels:
                curve = pg.PlotDataItem(pen=pen)
                self._temperature_view.addItem(curve)
                waveform_item.legend.addItem(curve, f"CH{channel} Temp")
                self._waveform_curves[channel] = curve
            else:
                self._waveform_curves[channel] = self.waveform_plot.plot(
                    pen=pen, name=f"CH{channel}"
                )
            self._fft_curves[channel] = self.fft_plot.plot(
                pen=pen, name=f"CH{channel}"
            )
        self._sync_magnetic_channel_checks(
            self._waveform_checks, self.waveform_scale_box.currentData() == "nt"
        )
        self._sync_magnetic_channel_checks(
            self._fft_checks, self.fft_scale_box.currentData() == "nt"
        )

        if self._duration_s > 60.0:
            region_start, region_stop = 50.0, self._duration_s - 10.0
            self._default_short_recording = False
        else:
            region_start, region_stop = 0.0, self._duration_s
            self._default_short_recording = True
        self.fft_region = pg.LinearRegionItem(
            values=(region_start, region_stop),
            bounds=(0.0, self._duration_s),
            brush=pg.mkBrush(60, 130, 220, 45),
        )
        self.waveform_plot.addItem(self.fft_region)
        self._sync_trim_fields()
        self.fft_region.sigRegionChanged.connect(self._sync_trim_fields)
        self.fft_region.sigRegionChangeFinished.connect(self._on_region_finished)
        self.start_trim_box.editingFinished.connect(self._on_trim_fields_edited)
        self.end_trim_box.editingFinished.connect(self._on_trim_fields_edited)

        self.waveform_plot.setXRange(0.0, max(self._duration_s, 0.001), padding=0)
        self.waveform_plot.getViewBox().sigXRangeChanged.connect(
            self._refresh_waveform_view
        )
        self.fft_plot.getViewBox().sigXRangeChanged.connect(
            self._refresh_fft_view
        )
        self.waveform_plot.installEventFilter(self)
        self.fft_plot.installEventFilter(self)
        self._refresh_waveform_view()
        self._fit_waveform_y()
        QTimer.singleShot(0, self._request_fft)

    def _add_channel_checks(self, layout: QGridLayout, row: int, title: str,
                            selected: tuple[int, ...], callback) -> dict[int, QCheckBox]:
        layout.addWidget(QLabel(f"{title} channels"), row, 0)
        checks = {}
        for column, channel in enumerate(self.data.channel_indices, start=1):
            label = (
                f"CH{channel} Temp" if channel in self._thermistor_channels
                else f"CH{channel}"
            )
            check = QCheckBox(label)
            check.setChecked(channel in selected)
            if channel in self._thermistor_channels:
                check.setToolTip(
                    "Waveform uses the separate nominal °C axis; FFT uses "
                    "raw ADC counts or dBFS."
                )
            check.toggled.connect(callback)
            layout.addWidget(check, row, column)
            checks[channel] = check
        layout.setColumnStretch(len(self.data.channel_indices) + 1, 1)
        return checks

    def _make_trim_box(self) -> QDoubleSpinBox:
        box = QDoubleSpinBox()
        box.setDecimals(6)
        box.setRange(0.0, self._duration_s)
        box.setSingleStep(1.0)
        box.setSuffix(" s")
        box.setMinimumWidth(125)
        return box

    @staticmethod
    def _configure_plot(widget: pg.PlotWidget, left_label: str,
                        bottom_label: str) -> None:
        widget.setLabel("left", left_label)
        widget.setLabel("bottom", bottom_label)
        widget.showGrid(x=True, y=True, alpha=0.2)
        widget.getPlotItem().getViewBox().setMouseMode(pg.ViewBox.RectMode)

    def _align_temperature_view(self) -> None:
        self._temperature_view.setGeometry(
            self.waveform_plot.getViewBox().sceneBoundingRect()
        )

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if event.type() == QEvent.Type.Resize:
            if watched is self.waveform_plot:
                QTimer.singleShot(0, self._refresh_waveform_view)
            elif watched is self.fft_plot:
                QTimer.singleShot(0, self._refresh_fft_view)
        return super().eventFilter(watched, event)

    def _selected_channels(self, checks: dict[int, QCheckBox]) -> tuple[int, ...]:
        return tuple(channel for channel, check in checks.items() if check.isChecked())

    def _sync_magnetic_channel_checks(
            self, checks: dict[int, QCheckBox], magnetic_only: bool) -> None:
        for channel, check in checks.items():
            if channel in self._magnetic_scales:
                continue
            if channel in self._thermistor_channels and checks is self._waveform_checks:
                check.setToolTip("Waveform uses the separate nominal °C axis.")
                continue
            if magnetic_only:
                with QSignalBlocker(check):
                    check.setChecked(False)
                tooltip = "Select to switch this plot to raw ADC counts."
            elif channel in self._thermistor_channels:
                tooltip = (
                    "Thermistor FFT uses raw ADC counts or dBFS."
                )
            else:
                tooltip = ""
            check.setToolTip(tooltip)

    def _on_waveform_scale_changed(self, _index: int) -> None:
        magnetic_only = self.waveform_scale_box.currentData() == "nt"
        self._sync_magnetic_channel_checks(
            self._waveform_checks, magnetic_only,
        )
        self.waveform_plot.setLabel(
            "left", "Nominal field-equivalent (nT)" if magnetic_only
            else "Raw ADC counts"
        )
        self._refresh_waveform_view()
        self._fit_waveform_y()
        self._update_calibration_note()

    def _update_calibration_note(self) -> None:
        magnetic = (self.waveform_scale_box.currentData() == "nt" or
                    self.fft_scale_box.currentData() == "nt")
        temperature = bool(
            self._thermistor_channels &
            set(self._selected_channels(self._waveform_checks))
        )
        notes = []
        if magnetic:
            notes.append("nT is a nominal field-equivalent scale; the bridge "
                         "servo removes DC and very slow fields")
        if temperature:
            notes.append("°C assumes nominal 3.3 V and bridge resistors; "
                         "board temperature is not calibrated")
        self.calibration_note.setText(". ".join(notes) + ("." if notes else ""))
        self.calibration_note.setVisible(bool(notes))

    def _refresh_waveform_view(self, *_args) -> None:
        left, right = self.waveform_plot.viewRange()[0]
        width = max(1, int(self.waveform_plot.getViewBox().width()))
        selected = self._selected_channels(self._waveform_checks)
        for channel, curve in self._waveform_curves.items():
            if channel not in selected:
                curve.setData([], [])
                continue
            x, y = select_visible_extrema(
                self.data.timestamps_s, self.data.samples[channel],
                left, right, width,
            )
            if channel in self._thermistor_channels:
                y = thermistor_celsius_from_counts(
                    y, self.data.recording_header.adc_gains[channel]
                )
            elif self.waveform_scale_box.currentData() == "nt":
                y = y * self._magnetic_scales[channel]
            curve.setData(x, y)
        self.waveform_plot.getPlotItem().getAxis("right").setVisible(
            bool(self._thermistor_channels & set(selected))
        )

    def _refresh_fft_view(self, *_args) -> None:
        left, right = self.fft_plot.viewRange()[0]
        width = max(1, int(self.fft_plot.getViewBox().width()))
        selected = self._selected_channels(self._fft_checks)
        for channel, curve in self._fft_curves.items():
            spectrum = self._spectra.get(channel)
            if spectrum is None or channel not in selected:
                curve.setData([], [])
                continue
            x, y = select_visible_extrema(
                spectrum.frequencies_hz,
                self._fft_display_values[channel],
                left, right, width,
            )
            curve.setData(x, y)

    def _sync_trim_fields(self, *_args) -> None:
        start, stop = self.fft_region.getRegion()
        with QSignalBlocker(self.start_trim_box), QSignalBlocker(self.end_trim_box):
            self.start_trim_box.setValue(start)
            self.end_trim_box.setValue(max(0.0, self._duration_s - stop))

    def _on_trim_fields_edited(self) -> None:
        start = self.start_trim_box.value()
        stop = self._duration_s - self.end_trim_box.value()
        try:
            valid_sample_bounds(self.data, start, self.end_trim_box.value())
        except RecordingFileError as error:
            self.status_label.setText(f"Invalid trim: {error}")
            return
        self.fft_region.setRegion((start, stop))

    def _on_region_finished(self, *_args) -> None:
        self._sync_trim_fields()
        self._fit_waveform_y()
        self._request_fft()

    def _on_waveform_changed(self, _checked: bool) -> None:
        if (self.waveform_scale_box.currentData() == "nt" and
                any(channel not in self._magnetic_scales and
                    channel not in self._thermistor_channels for channel in
                    self._selected_channels(self._waveform_checks))):
            self.waveform_scale_box.setCurrentIndex(0)
            return
        self._refresh_waveform_view()
        self._fit_waveform_y()
        self._update_calibration_note()

    def _fit_waveform_y(self, *_args) -> None:
        channels = self._selected_channels(self._waveform_checks)
        if not channels:
            return
        if self.waveform_y_box.currentIndex() == 0:
            start, stop = self.fft_region.getRegion()
            try:
                first, last = valid_sample_bounds(
                    self.data, start, max(0.0, self._duration_s - stop)
                )
            except RecordingFileError:
                return
        else:
            first, last = 0, self.data.timestamps_s.size
        thermistors = tuple(channel for channel in channels
                            if channel in self._thermistor_channels)
        if thermistors:
            bounds = []
            for channel in thermistors:
                samples = self.data.samples[channel][first:last]
                gain = self.data.recording_header.adc_gains[channel]
                values = thermistor_celsius_from_counts(
                    np.array((samples.min(), samples.max())), gain
                )
                if not np.all(np.isfinite(values)):
                    valid = thermistor_celsius_from_counts(samples, gain)
                    valid = valid[np.isfinite(valid)]
                    if not valid.size:
                        continue
                    values = np.array((valid.min(), valid.max()))
                bounds.append(values)
            if bounds:
                minimum_t = min(float(np.min(values)) for values in bounds)
                maximum_t = max(float(np.max(values)) for values in bounds)
                margin_t = max(0.01, (maximum_t - minimum_t) * 0.05)
                self._temperature_view.setYRange(
                    minimum_t - margin_t, maximum_t + margin_t, padding=0
                )
        channels = tuple(channel for channel in channels
                         if channel not in self._thermistor_channels)
        if not channels:
            return
        field_units = self.waveform_scale_box.currentData() == "nt"
        minimum = min(
            int(self.data.samples[channel][first:last].min()) *
            (self._magnetic_scales[channel] if field_units else 1.0)
            for channel in channels
        )
        maximum = max(
            int(self.data.samples[channel][first:last].max()) *
            (self._magnetic_scales[channel] if field_units else 1.0)
            for channel in channels
        )
        margin = max(1e-6 if field_units else 1.0,
                     (maximum - minimum) * 0.05)
        self.waveform_plot.setYRange(
            minimum - margin, maximum + margin, padding=0
        )

    def _on_fft_channels_changed(self, _checked: bool) -> None:
        if (self.fft_scale_box.currentData() == "nt" and
                any(channel not in self._magnetic_scales for channel in
                    self._selected_channels(self._fft_checks))):
            self.fft_scale_box.setCurrentIndex(1)
            return
        self._request_fft()

    def _on_fft_scale_changed(self, _index: int) -> None:
        self._update_calibration_note()
        magnetic_only = self.fft_scale_box.currentData() == "nt"
        self._sync_magnetic_channel_checks(
            self._fft_checks, magnetic_only,
        )
        label = {
            "dbfs": "Amplitude (dBFS)",
            "counts": "Amplitude (ADC counts)",
            "nt": "Nominal peak amplitude (nT)",
        }[self.fft_scale_box.currentData()]
        self.fft_plot.setLabel(
            "left", label
        )
        if set(self._selected_channels(self._fft_checks)) != set(self._spectra):
            self._request_fft()
            return
        self._update_fft_display_values()
        self._refresh_fft_view()

    def _selection_note(self) -> str:
        start, stop = self.fft_region.getRegion()
        if (self._default_short_recording and start == 0.0 and
                math.isclose(stop, self._duration_s, abs_tol=1e-6)):
            return (
                "Recording is too short for the 50 s / 10 s default; "
                "using the full recording. "
            )
        return ""

    def _request_fft(self) -> None:
        self._fft_generation += 1
        generation = self._fft_generation
        self._pending_fft_request = None
        channels = self._selected_channels(self._fft_checks)
        if not channels:
            self._spectra = {}
            self._fft_display_values = {}
            self._refresh_fft_view()
            self.status_label.setText("Select an FFT channel.")
            return
        start, stop = self.fft_region.getRegion()
        end_trim = max(0.0, self._duration_s - stop)
        try:
            valid_sample_bounds(self.data, start, end_trim)
        except RecordingFileError as error:
            self.status_label.setText(f"Invalid FFT selection: {error}")
            return
        self.status_label.setText(self._selection_note() + "Calculating FFT…")
        self._pending_fft_request = (
            generation, channels, start, end_trim,
            self.fft_window_box.currentData(),
        )
        self._launch_pending_fft()

    def _launch_pending_fft(self) -> None:
        if self._fft_busy or self._pending_fft_request is None:
            return
        generation, channels, start, end_trim, window_type = self._pending_fft_request
        self._pending_fft_request = None
        self._fft_busy = True
        job = _FftJob(generation, self.data, channels, start, end_trim, window_type)
        job.signals.finished.connect(self._on_fft_ready)
        job.signals.failed.connect(self._on_fft_failed)
        self._fft_jobs[generation] = job
        self._thread_pool.start(job)

    def _on_fft_ready(self, generation: int, spectra: object) -> None:
        self._fft_jobs.pop(generation, None)
        self._fft_busy = False
        if generation != self._fft_generation:
            self._launch_pending_fft()
            return
        self._spectra = spectra
        self._update_fft_display_values()
        spectrum = next(iter(self._spectra.values()))
        if not self._fft_range_initialized:
            self.fft_plot.setXRange(
                0.0, float(spectrum.frequencies_hz[-1]), padding=0
            )
            self._fft_range_initialized = True
        self._refresh_fft_view()
        self.status_label.setText(
            self._selection_note() +
            f"FFT: {spectrum.frequencies_hz.size:,} exact bins/channel · "
            f"{spectrum.bin_width_hz:.6g} Hz/bin · "
            f"{self.fft_window_box.currentData()} window"
        )

    def _on_fft_failed(self, generation: int, message: str) -> None:
        self._fft_jobs.pop(generation, None)
        self._fft_busy = False
        if generation != self._fft_generation:
            self._launch_pending_fft()
            return
        self._spectra = {}
        self._fft_display_values = {}
        self._refresh_fft_view()
        self.status_label.setText(f"FFT unavailable: {message}")

    def _update_fft_display_values(self) -> None:
        mode = self.fft_scale_box.currentData()
        if mode == "dbfs":
            self._fft_display_values = {
                channel: amplitudes_to_dbfs(spectrum.amplitudes)
                for channel, spectrum in self._spectra.items()
            }
        elif mode == "nt":
            self._fft_display_values = {
                channel: spectrum.amplitudes * self._magnetic_scales[channel]
                for channel, spectrum in self._spectra.items()
            }
        else:
            self._fft_display_values = {
                channel: spectrum.amplitudes
                for channel, spectrum in self._spectra.items()
            }

    def closeEvent(self, event) -> None:
        self._thread_pool.clear()
        super().closeEvent(event)
