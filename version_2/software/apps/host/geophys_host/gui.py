"""Tk desktop application for device connection, recordings, and live data."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from datetime import datetime, timezone
import queue
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk
from typing import Any

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
from serial.tools import list_ports

from .ble_client import BleDevice, BleHelloClient, discover_ble_devices
from .live import LiveStreamModel
from .protocol import (
    ADC_SAMPLE_RATE_1000_SPS,
    ADC_SAMPLE_RATE_16000_SPS,
    ADC_SAMPLE_RATE_2000_SPS,
    ADC_SAMPLE_RATE_4000_SPS,
    ADC_SAMPLE_RATE_500_SPS,
    ADC_SAMPLE_RATE_8000_SPS,
    MAGNETIC_CARD_SLOT_1,
    MAGNETIC_CARD_SLOT_2,
    MAGNETIC_PULSE_RESET,
    MAGNETIC_PULSE_SET,
    REPLY_DEVICE_CONFIG,
    REPLY_DEVICE_INFO,
    REPLY_MAGNETIC_PULSE_RESULT,
    REPLY_RECORDING_DELETE_RESULT,
    REPLY_RECORDING_INFO,
    REPLY_RECORDING_NUMBER,
    REPLY_RECORDING_START_RESULT,
    REPLY_RECORDING_STOP_RESULT,
    REPLY_STREAMING_START_RESULT,
    REPLY_STREAMING_STOP_RESULT,
    RESULT_STORAGE_MEDIA_ABSENT,
    RESULT_STORAGE_FULL,
    RESULT_SUCCESS,
    DeviceConfig,
    DeviceConfigUpdate,
    RecordingInfo,
    decode_device_config,
    decode_device_info,
    decode_magnetic_pulse_result,
    decode_recording_delete_result,
    decode_recording_info,
    decode_recording_number,
    decode_recording_start_result,
    decode_recording_stop_result,
    decode_streaming_start_result,
    decode_streaming_stop_result,
    encode_device_get_config,
    encode_device_set_config,
    encode_hello,
    encode_magnetic_pulse,
    encode_recording_delete,
    encode_recording_get_info,
    encode_recording_get_number,
    encode_recording_start,
    encode_recording_stop,
    encode_streaming_start,
    encode_streaming_stop,
)
from .serial_client import SerialClient
from .stream_parser import AdcRecordMessage


@dataclass(frozen=True)
class UiEvent:
    name: str
    payload: Any = None


SAMPLE_RATE_OPTIONS = {
    "0.5 kS/s": ADC_SAMPLE_RATE_500_SPS,
    "1 kS/s": ADC_SAMPLE_RATE_1000_SPS,
    "2 kS/s": ADC_SAMPLE_RATE_2000_SPS,
    "4 kS/s": ADC_SAMPLE_RATE_4000_SPS,
    "8 kS/s": ADC_SAMPLE_RATE_8000_SPS,
    "16 kS/s": ADC_SAMPLE_RATE_16000_SPS,
}
SAMPLE_RATE_LABELS = {
    code: label for label, code in SAMPLE_RATE_OPTIONS.items()
}
GAIN_OPTIONS = {
    "×1": 1,
    "×2": 2,
    "×4": 4,
    "×8": 8,
}
GAIN_LABELS = {gain: label for label, gain in GAIN_OPTIONS.items()}
GAIN_ENCODING = {1: 0, 2: 1, 4: 2, 8: 3}
CARD_TYPE_LABELS = {
    0: "Absent",
    1: "Magnetic",
    2: "Geophysical accelerometer",
}
GNSS_STATE_LABELS = {
    0: "Disabled / absent",
    1: "Ready",
    2: "Faulted",
    3: "Searching",
}
IMU_STATE_LABELS = {
    0: "Disabled / absent",
    1: "Ready",
    2: "Faulted",
}
SD_STATE_LABELS = {
    0: "Absent",
    1: "Present",
    2: "Faulted",
}
SD_STATE_PRESENT = 1
SD_STATE_FAULTED = 2
POWER_RAIL_FIELDS = (
    ("rail_3v3", "rail_3v3_enabled", "+3.3 VA"),
    ("rail_5v", "rail_5v_enabled", "+5 VA"),
    ("rail_9v", "rail_9v_enabled", "+10 V / 9 VA"),
    ("rail_negative_5v", "rail_negative_5v_enabled", "−5 VA"),
    ("rail_18v", "rail_18v_enabled", "+18 V"),
)
MANUAL_POWER_RAIL_KEYS = (
    "rail_3v3",
    "rail_9v",
    "rail_negative_5v",
)
PULSE_SLOT_OPTIONS = {
    "Slot 1": MAGNETIC_CARD_SLOT_1,
    "Slot 2": MAGNETIC_CARD_SLOT_2,
}
PULSE_OPERATION_OPTIONS = {
    "SET": MAGNETIC_PULSE_SET,
    "RESET": MAGNETIC_PULSE_RESET,
}


def pack_adc_gains(gains: tuple[int, ...]) -> int:
    """Pack eight ADC gain factors into the protocol's two-bit fields."""
    if len(gains) != 8:
        raise ValueError("exactly eight ADC gains are required")
    packed = 0
    for channel, gain in enumerate(gains):
        try:
            encoded = GAIN_ENCODING[gain]
        except KeyError as error:
            raise ValueError(f"invalid gain for channel {channel}: {gain}") \
                from error
        packed |= encoded << (2 * channel)
    return packed


def unpack_adc_gains(packed: int) -> tuple[int, ...]:
    """Return eight ADC gain factors from the packed protocol field."""
    if not 0 <= packed <= 0xFFFF:
        raise ValueError("ADC gain field is outside uint16")
    factors = (1, 2, 4, 8)
    return tuple(
        factors[(packed >> (2 * channel)) & 0x03]
        for channel in range(8)
    )


def build_device_config_update(
        current: DeviceConfig,
        adc_sample_rate: int,
        adc_channel_mask: int,
        gains: tuple[int, ...],
        rail_states: tuple[bool, ...] | None = None) -> DeviceConfigUpdate:
    """Build an update from editable fields and preserve hidden fields."""
    if adc_channel_mask not in (0x0F, 0xF0, 0xFF):
        raise ValueError("select at least one four-channel slot")
    requested_rails = {
        key: getattr(current, attribute)
        for key, attribute, _label in POWER_RAIL_FIELDS
    }
    if rail_states is not None:
        if len(rail_states) != len(MANUAL_POWER_RAIL_KEYS):
            raise ValueError(
                "exactly three acquisition-rail states are required")
        requested_rails.update(zip(MANUAL_POWER_RAIL_KEYS, rail_states))
    return DeviceConfigUpdate(
        adc_sample_rate=adc_sample_rate,
        adc_channel_mask=adc_channel_mask,
        adc_gain=pack_adc_gains(gains),
        rail_3v3_enabled=requested_rails["rail_3v3"],
        rail_5v_enabled=requested_rails["rail_5v"],
        rail_9v_enabled=requested_rails["rail_9v"],
        rail_negative_5v_enabled=requested_rails["rail_negative_5v"],
        rail_18v_enabled=requested_rails["rail_18v"],
        imu_averaging_time_ms=current.imu_averaging_time_ms,
    )


def format_uptime(timestamp_100ns: int) -> str:
    total_seconds = timestamp_100ns / 10_000_000
    days, remainder = divmod(int(total_seconds), 86_400)
    hours, remainder = divmod(remainder, 3_600)
    minutes, seconds = divmod(remainder, 60)
    prefix = f"{days} d " if days else ""
    return f"{prefix}{hours:02d}:{minutes:02d}:{seconds:02d}"


def format_centi_value(value: int, unit: str) -> str:
    """Format implemented telemetry; zero is unavailable in current firmware."""
    return "Unavailable" if value == 0 else f"{value / 100:.2f} {unit}"


def format_enabled(enabled: bool) -> str:
    return "On" if enabled else "Off"


def unexpected_recording_stop_message(
        sd_card_state: int,
        result: int | None = None) -> str:
    if result == RESULT_STORAGE_FULL:
        return "Recording stopped — the SD card is full"
    if result == RESULT_STORAGE_MEDIA_ABSENT:
        return "Recording stopped — the SD card is absent"
    if sd_card_state == SD_STATE_FAULTED:
        return "Recording stopped unexpectedly — the SD card reported a fault"
    if sd_card_state == 0:
        return "Recording stopped unexpectedly — the SD card is absent"
    return "Recording stopped unexpectedly"


def _require_success(label: str, result: int) -> None:
    if result != RESULT_SUCCESS:
        raise RuntimeError(f"{label} failed with result 0x{result:02x}")


def format_size(size_bytes: int) -> str:
    if size_bytes < 1024:
        return f"{size_bytes} B"
    if size_bytes < 1024 * 1024:
        return f"{size_bytes / 1024:.1f} KiB"
    return f"{size_bytes / (1024 * 1024):.1f} MiB"


def format_timestamp(timestamp_us: int) -> str:
    if timestamp_us == 0:
        return "—"
    try:
        value = datetime.fromtimestamp(
            timestamp_us / 1_000_000, tz=timezone.utc)
    except (OverflowError, OSError, ValueError):
        return str(timestamp_us)
    return value.strftime("%Y-%m-%d %H:%M:%S UTC")


class DeviceWorker:
    """Own the serial connection and execute GUI requests off the Tk thread."""

    STORAGE_TIMEOUT_S = 12.0
    ACQUISITION_TIMEOUT_S = 12.0

    def __init__(self, port: str, baud_rate: int, timeout_s: float = 2.0) -> None:
        self.port = port
        self.baud_rate = baud_rate
        self.timeout_s = timeout_s
        self.events: queue.Queue[UiEvent] = queue.Queue()
        self.records: queue.Queue[tuple[int, AdcRecordMessage]] = \
            queue.Queue(maxsize=512)
        self._commands: queue.Queue[tuple[str, dict[str, Any]]] = queue.Queue()
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run,
            name="geophys-device",
            daemon=True,
        )
        self._client: SerialClient | None = None
        self._streaming = False
        self._recording_in_progress: bool | None = None
        self._stream_generation = 0
        self._host_dropped_blocks = 0
        self._next_keepalive = 0.0
        self._next_stats = 0.0

    def start(self) -> None:
        self._thread.start()

    def submit(self, action: str, **payload: Any) -> None:
        self._commands.put((action, payload))

    def shutdown(self) -> None:
        self._stop.set()

    def join(self, timeout: float | None = None) -> None:
        self._thread.join(timeout)

    def _emit(self, name: str, payload: Any = None) -> None:
        self.events.put(UiEvent(name, payload))

    def _emit_record(self, message: AdcRecordMessage) -> None:
        try:
            self.records.put_nowait((self._stream_generation, message))
        except queue.Full:
            self._host_dropped_blocks += 1

    def _request(self, frame: bytes, reply_id: int,
                 timeout_s: float | None = None):
        if self._client is None:
            raise RuntimeError("device is not connected")
        reply = self._client.request(
            frame,
            reply_id,
            timeout_s=self.timeout_s if timeout_s is None else timeout_s,
            on_record=self._emit_record if self._streaming else None,
        )
        self._next_keepalive = time.monotonic() + 1.0
        return reply

    def _read_config(self) -> DeviceConfig:
        config = decode_device_config(self._request(
            encode_device_get_config(), REPLY_DEVICE_CONFIG))
        _require_success("DEVICE_GET_CONFIG", config.result)
        recording_failed = (
            self._recording_in_progress is True and
            not config.recording_in_progress
        )
        self._recording_in_progress = config.recording_in_progress
        if recording_failed:
            failure = decode_recording_stop_result(self._request(
                encode_recording_stop(),
                REPLY_RECORDING_STOP_RESULT,
                self.STORAGE_TIMEOUT_S,
            ))
            self._emit("recording_failed", failure)
        self._emit("config", config)
        return config

    def _set_config(self, update: DeviceConfigUpdate) -> None:
        config = decode_device_config(self._request(
            encode_device_set_config(update),
            REPLY_DEVICE_CONFIG,
            self.ACQUISITION_TIMEOUT_S,
        ))
        _require_success("DEVICE_SET_CONFIG", config.result)
        self._emit("config_applied", config)

    def _magnetic_pulse(self, card_slot: int, operation: int) -> None:
        result = decode_magnetic_pulse_result(self._request(
            encode_magnetic_pulse(card_slot, operation),
            REPLY_MAGNETIC_PULSE_RESULT,
            self.ACQUISITION_TIMEOUT_S,
        ))
        _require_success("MAGNETIC_PULSE", result.result)
        self._emit("pulse_completed", result)
        self._read_config()

    def _refresh_recordings(self) -> None:
        self._emit("recordings_loading")
        try:
            number = decode_recording_number(self._request(
                encode_recording_get_number(),
                REPLY_RECORDING_NUMBER,
                self.STORAGE_TIMEOUT_S,
            ))
            if number.result == RESULT_STORAGE_MEDIA_ABSENT:
                self._emit("storage_media_absent")
                return
            _require_success("RECORDING_GET_NUMBER", number.result)
            recordings = []
            for index in range(number.count):
                recording = decode_recording_info(self._request(
                    encode_recording_get_info(index),
                    REPLY_RECORDING_INFO,
                    self.STORAGE_TIMEOUT_S,
                ))
                _require_success("RECORDING_GET_INFO", recording.result)
                recordings.append(recording)
        except Exception as error:
            self._emit("recordings_error", str(error))
            return
        self._emit("recordings", recordings)

    def _start_recording(self, name: str) -> None:
        result = decode_recording_start_result(self._request(
            encode_recording_start(name),
            REPLY_RECORDING_START_RESULT,
            self.ACQUISITION_TIMEOUT_S,
        ))
        _require_success("RECORDING_START", result.result)
        self._recording_in_progress = True
        self._emit("recording_started", result)
        config = self._read_config()
        if config.recording_in_progress:
            self._refresh_recordings()

    def _stop_recording(self) -> None:
        result = decode_recording_stop_result(self._request(
            encode_recording_stop(),
            REPLY_RECORDING_STOP_RESULT,
            self.STORAGE_TIMEOUT_S,
        ))
        self._recording_in_progress = False
        if result.result != RESULT_SUCCESS:
            self._emit("recording_failed", result)
            self._read_config()
            return
        _require_success("RECORDING_STOP", result.result)
        if self._streaming:
            self._streaming = False
            self._emit("stream_stopped")
        self._emit("recording_stopped", result)
        self._read_config()
        self._refresh_recordings()

    def _delete_recording(self, name: str) -> None:
        result = decode_recording_delete_result(self._request(
            encode_recording_delete(name),
            REPLY_RECORDING_DELETE_RESULT,
            self.STORAGE_TIMEOUT_S,
        ))
        _require_success("RECORDING_DELETE", result.result)
        self._emit("recording_deleted", result)
        self._refresh_recordings()

    def _start_stream(self, decimation: int, channel_mask: int) -> None:
        self._read_config()
        result = decode_streaming_start_result(self._request(
            encode_streaming_start(decimation, channel_mask),
            REPLY_STREAMING_START_RESULT,
            self.ACQUISITION_TIMEOUT_S,
        ))
        _require_success("STREAMING_START", result.result)
        if result.channel_mask == 0:
            raise RuntimeError("device accepted a stream with no channels")
        self._stream_generation += 1
        self._streaming = True
        self._next_keepalive = time.monotonic() + 1.0
        self._emit("stream_started", {
            "result": result,
            "generation": self._stream_generation,
        })

    def _stop_stream(self) -> None:
        result = decode_streaming_stop_result(self._request(
            encode_streaming_stop(),
            REPLY_STREAMING_STOP_RESULT,
            self.ACQUISITION_TIMEOUT_S,
        ))
        _require_success("STREAMING_STOP", result.result)
        self._streaming = False
        self._emit("stream_stopped", result)

    def _dispatch(self, name: str, payload: dict[str, Any]) -> None:
        if name == "refresh_recordings":
            self._refresh_recordings()
        elif name == "start_recording":
            self._start_recording(payload["name"])
        elif name == "stop_recording":
            self._stop_recording()
        elif name == "delete_recording":
            self._delete_recording(payload["name"])
        elif name == "start_stream":
            self._start_stream(
                payload["decimation"], payload["channel_mask"])
        elif name == "stop_stream":
            self._stop_stream()
        elif name == "set_config":
            self._set_config(payload["update"])
        elif name == "magnetic_pulse":
            self._magnetic_pulse(
                payload["card_slot"], payload["operation"])
        else:
            raise RuntimeError(f"unknown GUI action: {name}")

    def _emit_link_stats(self) -> None:
        if self._client is None:
            return
        self._emit("link_stats", {
            "invalid_records": self._client.parser.invalid_records,
            "invalid_commands": self._client.parser.invalid_commands,
            "bytes_discarded": self._client.parser.bytes_discarded,
            "host_dropped_blocks": self._host_dropped_blocks,
        })

    def _run(self) -> None:
        try:
            with SerialClient(
                self.port,
                self.baud_rate,
                write_timeout_s=self.timeout_s,
            ) as client:
                self._client = client
                info = decode_device_info(self._request(
                    encode_hello(), REPLY_DEVICE_INFO))
                _require_success("HELLO", info.result)
                config = self._read_config()
                self._emit("connected", {"info": info, "config": config})
                self._next_stats = time.monotonic() + 1.0

                while not self._stop.is_set():
                    try:
                        name, payload = self._commands.get_nowait()
                    except queue.Empty:
                        name = ""
                        payload = {}

                    if name:
                        try:
                            self._dispatch(name, payload)
                        except Exception as error:
                            self._emit("action_error", {
                                "action": name,
                                "message": str(error),
                            })

                    now = time.monotonic()
                    if now >= self._next_keepalive:
                        self._read_config()

                    message = client.read_message()
                    if isinstance(message, AdcRecordMessage):
                        self._emit_record(message)

                    now = time.monotonic()
                    if now >= self._next_stats:
                        self._emit_link_stats()
                        self._next_stats = now + 1.0

                if self._streaming:
                    try:
                        self._stop_stream()
                    except Exception:
                        pass
        except Exception as error:
            self._emit("connection_error", str(error))
        finally:
            self._client = None
            self._emit("disconnected")


class BleHelloWorker:
    """Own a persistent BLE connection for the initial HELLO-only slice."""

    def __init__(self, device: BleDevice, timeout_s: float = 3.0) -> None:
        self.device = device
        self.port = device.display_name
        self.timeout_s = timeout_s
        self.events: queue.Queue[UiEvent] = queue.Queue()
        self.records: queue.Queue[tuple[int, AdcRecordMessage]] = queue.Queue()
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._run,
            name="geophys-ble-device",
            daemon=True,
        )

    def start(self) -> None:
        self._thread.start()

    def shutdown(self) -> None:
        self._stop.set()

    def join(self, timeout: float | None = None) -> None:
        self._thread.join(timeout)

    async def _run_async(self) -> None:
        client = BleHelloClient(self.device.identifier)
        try:
            await client.connect()
            info = await client.hello(self.timeout_s)
            self.events.put(UiEvent("connected", {
                "info": info,
                "connection_label": self.device.display_name,
                "ble_hello_only": True,
            }))
            while not self._stop.is_set() and client.is_connected:
                await asyncio.sleep(0.1)
        finally:
            await client.disconnect()

    def _run(self) -> None:
        try:
            asyncio.run(self._run_async())
        except Exception as error:
            self.events.put(UiEvent("connection_error", str(error)))
        finally:
            self.events.put(UiEvent("disconnected"))


class EmbeddedLivePlot(ttk.Frame):
    """Eight raw-channel plots embedded in the Live Stream tab."""

    def __init__(self, parent: tk.Misc) -> None:
        super().__init__(parent)
        self.figure = Figure(figsize=(10, 7), dpi=100)
        self.axes = []
        self.lines = {}
        for channel in range(8):
            axis = self.figure.add_subplot(4, 2, channel + 1)
            line, = axis.plot([], [], linewidth=0.8)
            axis.set_title(f"CH{channel}", loc="left", fontsize=9)
            axis.grid(True, alpha=0.25)
            self.axes.append(axis)
            self.lines[channel] = line
        self.axes[6].set_xlabel("Device monotonic time (s)")
        self.axes[7].set_xlabel("Device monotonic time (s)")
        self.figure.tight_layout()
        self.canvas = FigureCanvasTkAgg(self.figure, master=self)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        self.channel_mask = 0xFF

    def configure_channels(self, channel_mask: int) -> None:
        self.channel_mask = channel_mask
        for channel, axis in enumerate(self.axes):
            axis.set_visible(bool(channel_mask & (1 << channel)))
        self.figure.tight_layout()
        self.canvas.draw_idle()

    def update_plot(self, model: LiveStreamModel, link_stats: dict[str, int]) -> None:
        for channel, axis in enumerate(self.axes):
            if not (self.channel_mask & (1 << channel)):
                continue
            timestamps, samples = model.plot_data(channel)
            self.lines[channel].set_data(timestamps, samples)
            if timestamps:
                right = timestamps[-1]
                axis.set_xlim(
                    max(0.0, right - model.window_s),
                    max(model.window_s, right),
                )
                axis.relim()
                axis.autoscale_view(scalex=False, scaley=True)

        snapshot = model.snapshot()
        statuses = ",".join(
            f"{status}:{count}"
            for status, count in sorted(snapshot.status_counts.items())
        ) or "none"
        self.figure.suptitle(
            f"{snapshot.conversion_rate_hz:.1f} conversions/s   "
            f"blocks {snapshot.blocks_received}   "
            f"missing {snapshot.missing_conversions}   "
            f"bad blocks {link_stats.get('invalid_records', 0)}   "
            f"host drops {link_stats.get('host_dropped_blocks', 0)}   "
            f"status {statuses}",
            fontsize=10,
        )
        self.canvas.draw_idle()


class GeophysHostApp(ttk.Frame):
    """Main desktop window."""

    PORT_POLL_MS = 50
    PLOT_REFRESH_S = 0.1
    CHANNELS = {
        "All channels": 0xFF,
        "Channels 0–3": 0x0F,
        "Channels 4–7": 0xF0,
    }
    DECIMATIONS = {
        "None": 0,
        "2": 2,
        "4": 4,
        "5": 5,
        "10": 10,
        "20": 20,
    }

    def __init__(self, root: tk.Tk) -> None:
        super().__init__(root, padding=10)
        self.root = root
        self.worker: DeviceWorker | BleHelloWorker | None = None
        self.connected = False
        self.ble_hello_only = False
        self.recording_in_progress = False
        self.live_active = False
        self.pending_action: str | None = None
        self.catalog_loading = False
        self.current_config: DeviceConfig | None = None
        self.config_dirty = False
        self._config_loading = False
        self.active_stream_generation = 0
        self.live_model: LiveStreamModel | None = None
        self.link_stats: dict[str, int] = {}
        self.recordings: dict[str, RecordingInfo] = {}
        self._ble_devices: dict[str, BleDevice] = {}
        self._ble_scan_events: queue.Queue[UiEvent] = queue.Queue()
        self._ble_scan_running = False
        self._last_plot_update = 0.0

        root.title("Geophysical Acquisition Host")
        root.geometry("1180x820")
        root.minsize(900, 650)
        root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.pack(fill=tk.BOTH, expand=True)

        self._build_connection_bar()
        self._build_tabs()
        self._build_status_bar()
        self.refresh_ports()
        self._update_controls()
        self.after(self.PORT_POLL_MS, self._poll_worker)

    def _build_connection_bar(self) -> None:
        frame = ttk.LabelFrame(self, text="Device connection", padding=8)
        frame.pack(fill=tk.X, pady=(0, 8))
        frame.columnconfigure(3, weight=1)

        ttk.Label(frame, text="Connection").grid(
            row=0, column=0, padx=(0, 6), sticky=tk.W)
        self.connection_type_var = tk.StringVar(value="USB / COM")
        self.connection_type_combo = ttk.Combobox(
            frame,
            textvariable=self.connection_type_var,
            values=("USB / COM", "Bluetooth LE"),
            state="readonly",
            width=13,
        )
        self.connection_type_combo.grid(row=0, column=1, padx=(0, 12))
        self.connection_type_combo.bind(
            "<<ComboboxSelected>>", self._connection_type_changed)

        self.device_label = ttk.Label(frame, text="USB / COM port")
        self.device_label.grid(
            row=0, column=2, padx=(0, 8), sticky=tk.W)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(
            frame, textvariable=self.port_var, state="readonly", width=42)
        self.port_combo.grid(row=0, column=3, sticky=tk.EW)
        self.refresh_ports_button = ttk.Button(
            frame, text="Refresh", command=self.refresh_devices)
        self.refresh_ports_button.grid(row=0, column=4, padx=6)

        self.baud_label = ttk.Label(frame, text="Baud")
        self.baud_label.grid(row=0, column=5, padx=(12, 6))
        self.baud_var = tk.StringVar(value="921600")
        self.baud_combo = ttk.Combobox(
            frame,
            textvariable=self.baud_var,
            values=("921600", "460800", "115200"),
            width=10,
        )
        self.baud_combo.grid(row=0, column=6)
        self.connect_button = ttk.Button(
            frame, text="Connect", command=self._toggle_connection)
        self.connect_button.grid(row=0, column=7, padx=(8, 0))

    def _build_tabs(self) -> None:
        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True)
        self.recordings_tab = ttk.Frame(self.notebook, padding=10)
        self.live_tab = ttk.Frame(self.notebook, padding=10)
        self.config_tab = ttk.Frame(self.notebook)
        self.notebook.add(self.recordings_tab, text="Recordings")
        self.notebook.add(self.live_tab, text="Live Stream")
        self.notebook.add(self.config_tab, text="Config")
        self._build_recordings_tab()
        self._build_live_tab()
        self._build_config_tab()

    def _build_recordings_tab(self) -> None:
        self.recordings_tab.columnconfigure(0, weight=1)
        self.recordings_tab.rowconfigure(0, weight=1)
        columns = ("index", "name", "size", "started", "state")
        self.recordings_tree = ttk.Treeview(
            self.recordings_tab,
            columns=columns,
            show="headings",
            selectmode="browse",
        )
        headings = {
            "index": "Index",
            "name": "Name",
            "size": "Size",
            "started": "Start time",
            "state": "State",
        }
        widths = {
            "index": 70,
            "name": 260,
            "size": 120,
            "started": 220,
            "state": 120,
        }
        for column in columns:
            self.recordings_tree.heading(column, text=headings[column])
            self.recordings_tree.column(
                column, width=widths[column], anchor=tk.W)
        self.recordings_tree.grid(row=0, column=0, sticky=tk.NSEW)
        scrollbar = ttk.Scrollbar(
            self.recordings_tab,
            orient=tk.VERTICAL,
            command=self.recordings_tree.yview,
        )
        scrollbar.grid(row=0, column=1, sticky=tk.NS)
        self.recordings_tree.configure(yscrollcommand=scrollbar.set)
        self.recordings_tree.bind(
            "<<TreeviewSelect>>", lambda _event: self._update_controls())

        actions = ttk.Frame(self.recordings_tab)
        actions.grid(row=1, column=0, columnspan=2, sticky=tk.EW, pady=(8, 0))
        self.refresh_recordings_button = ttk.Button(
            actions, text="Refresh list", command=self._refresh_recordings)
        self.refresh_recordings_button.pack(side=tk.LEFT)
        self.delete_recording_button = ttk.Button(
            actions, text="Delete selected", command=self._delete_recording)
        self.delete_recording_button.pack(side=tk.LEFT, padx=6)

        recorder = ttk.LabelFrame(
            self.recordings_tab, text="SD recording", padding=8)
        recorder.grid(
            row=2, column=0, columnspan=2, sticky=tk.EW, pady=(10, 0))
        recorder.columnconfigure(1, weight=1)
        ttk.Label(recorder, text="Name").grid(row=0, column=0, padx=(0, 6))
        self.recording_name_var = tk.StringVar()
        self.recording_name_entry = ttk.Entry(
            recorder, textvariable=self.recording_name_var)
        self.recording_name_entry.grid(row=0, column=1, sticky=tk.EW)
        self.start_recording_button = ttk.Button(
            recorder, text="Start recording", command=self._start_recording)
        self.start_recording_button.grid(row=0, column=2, padx=6)
        self.stop_recording_button = ttk.Button(
            recorder, text="Stop recording", command=self._stop_recording)
        self.stop_recording_button.grid(row=0, column=3)
        self.recordings_status_var = tk.StringVar(value="Not connected")
        ttk.Label(
            recorder, textvariable=self.recordings_status_var
        ).grid(row=1, column=0, columnspan=4, sticky=tk.W, pady=(6, 0))

    def _build_live_tab(self) -> None:
        self.live_tab.columnconfigure(0, weight=1)
        self.live_tab.rowconfigure(1, weight=1)
        controls = ttk.Frame(self.live_tab)
        controls.grid(row=0, column=0, sticky=tk.EW, pady=(0, 8))

        ttk.Label(controls, text="Channels").pack(side=tk.LEFT)
        self.channels_var = tk.StringVar(value="All channels")
        self.channels_combo = ttk.Combobox(
            controls,
            textvariable=self.channels_var,
            values=tuple(self.CHANNELS),
            state="readonly",
            width=16,
        )
        self.channels_combo.pack(side=tk.LEFT, padx=(6, 14))
        ttk.Label(controls, text="Decimation").pack(side=tk.LEFT)
        self.decimation_var = tk.StringVar(value="None")
        self.decimation_combo = ttk.Combobox(
            controls,
            textvariable=self.decimation_var,
            values=tuple(self.DECIMATIONS),
            state="readonly",
            width=8,
        )
        self.decimation_combo.pack(side=tk.LEFT, padx=(6, 14))
        self.start_live_button = ttk.Button(
            controls, text="Start live streaming", command=self._start_live)
        self.start_live_button.pack(side=tk.LEFT)
        self.stop_live_button = ttk.Button(
            controls, text="Stop", command=self._stop_live)
        self.stop_live_button.pack(side=tk.LEFT, padx=6)
        self.live_status_var = tk.StringVar(value="Not connected")
        ttk.Label(controls, textvariable=self.live_status_var).pack(
            side=tk.LEFT, padx=(12, 0))

        self.live_plot = EmbeddedLivePlot(self.live_tab)
        self.live_plot.grid(row=1, column=0, sticky=tk.NSEW)

    @staticmethod
    def _add_config_value(parent: ttk.Frame, row: int, label: str,
                          value: str = "—") -> tk.StringVar:
        variable = tk.StringVar(parent, value=value)
        ttk.Label(parent, text=label).grid(
            row=row, column=0, sticky=tk.W, padx=(0, 16), pady=4)
        ttk.Label(parent, textvariable=variable).grid(
            row=row, column=1, sticky=tk.E, pady=4)
        return variable

    def _build_config_slot(self, parent: ttk.Frame, row: int,
                           slot_number: int, first_channel: int) -> None:
        slot = ttk.LabelFrame(
            parent, text=f"Slot {slot_number}", padding=10)
        slot.grid(row=row, column=0, sticky=tk.EW, pady=(0, 10))
        slot.columnconfigure(1, weight=1)

        self.config_card_vars[slot_number] = self._add_config_value(
            slot, 0, "Detected card")
        enabled_variable = tk.BooleanVar(slot, value=False)
        self.config_slot_enabled_vars[slot_number] = enabled_variable
        checkbutton = ttk.Checkbutton(
            slot,
            text="Acquire this slot",
            variable=enabled_variable,
            command=self._mark_config_dirty,
            state=tk.DISABLED,
        )
        checkbutton.grid(
            row=1, column=0, columnspan=2, sticky=tk.W, pady=(5, 8))
        self.config_slot_checkbuttons[slot_number] = checkbutton

        for offset in range(4):
            channel = first_channel + offset
            ttk.Label(slot, text=f"Channel {channel} gain").grid(
                row=2 + offset, column=0, sticky=tk.W,
                padx=(16, 12), pady=3)
            gain_variable = tk.StringVar(slot)
            self.config_gain_vars[channel] = gain_variable
            gain = ttk.Combobox(
                slot,
                textvariable=gain_variable,
                values=tuple(GAIN_OPTIONS),
                state=tk.DISABLED,
                width=8,
            )
            gain.grid(row=2 + offset, column=1, sticky=tk.E, pady=3)
            gain.bind(
                "<<ComboboxSelected>>", self._mark_config_dirty)
            self.config_gain_combos[channel] = gain

    def _build_config_tab(self) -> None:
        """Build status display and stopped-device configuration controls."""
        self.config_tab.columnconfigure(0, weight=1)
        self.config_tab.rowconfigure(0, weight=1)
        self.config_value_vars: dict[str, tk.StringVar] = {}
        self.config_card_vars: dict[int, tk.StringVar] = {}
        self.config_slot_enabled_vars: dict[int, tk.BooleanVar] = {}
        self.config_slot_checkbuttons: dict[int, ttk.Checkbutton] = {}
        self.config_gain_vars: dict[int, tk.StringVar] = {}
        self.config_gain_combos: dict[int, ttk.Combobox] = {}
        self.config_rail_enabled_vars: dict[str, tk.BooleanVar] = {}
        self.config_rail_checkbuttons: dict[str, ttk.Checkbutton] = {}

        canvas = tk.Canvas(
            self.config_tab, borderwidth=0, highlightthickness=0)
        scrollbar = ttk.Scrollbar(
            self.config_tab, orient=tk.VERTICAL, command=canvas.yview)
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.grid(row=0, column=0, sticky=tk.NSEW)
        scrollbar.grid(row=0, column=1, sticky=tk.NS)

        content = ttk.Frame(canvas, padding=12)
        content.columnconfigure(0, weight=1)
        content_window = canvas.create_window(
            (0, 0), window=content, anchor=tk.NW)

        def resize_content(event) -> None:
            canvas.itemconfigure(content_window, width=event.width)

        def update_scroll_region(_event=None) -> None:
            canvas.configure(scrollregion=canvas.bbox("all"))

        def scroll(event) -> str:
            if event.num == 4:
                direction = -1
            elif event.num == 5:
                direction = 1
            else:
                direction = -1 if event.delta > 0 else 1
            canvas.yview_scroll(direction, "units")
            return "break"

        canvas.bind("<Configure>", resize_content)
        content.bind("<Configure>", update_scroll_region)
        canvas.bind("<MouseWheel>", scroll)
        canvas.bind("<Button-4>", scroll)
        canvas.bind("<Button-5>", scroll)

        ttk.Label(
            content,
            text="Device status and configuration",
            font=("TkDefaultFont", 16, "bold"),
        ).grid(row=0, column=0, sticky=tk.W)
        ttk.Label(
            content,
            text=("ADC settings and manual power-rail requests can be sent "
                  "while acquisition is stopped. The reported values always "
                  "show the state echoed by the device."),
            wraplength=650,
        ).grid(row=1, column=0, sticky=tk.W, pady=(2, 12))

        device = ttk.LabelFrame(content, text="Device", padding=10)
        device.grid(row=2, column=0, sticky=tk.EW, pady=(0, 10))
        device.columnconfigure(1, weight=1)
        self.config_value_vars["uptime"] = self._add_config_value(
            device, 0, "Uptime")
        self.config_value_vars["utc"] = self._add_config_value(
            device, 1, "UTC time", "Unavailable")
        self.config_value_vars["recording"] = self._add_config_value(
            device, 2, "Recording", "Unknown")
        self.config_value_vars["sd_card"] = self._add_config_value(
            device, 3, "SD card", "Unknown")
        self.config_value_vars["usb_5v"] = self._add_config_value(
            device, 4, "USB 5 V", "Unknown")
        self.config_value_vars["solar"] = self._add_config_value(
            device, 5, "Solar input", "Unknown")
        self.config_value_vars["esp32_temperature"] = self._add_config_value(
            device, 6, "ESP32 temperature")
        self.config_value_vars["error"] = self._add_config_value(
            device, 7, "Error status", "Unknown")

        power = ttk.LabelFrame(content, text="Power rails", padding=10)
        power.grid(row=3, column=0, sticky=tk.EW, pady=(0, 10))
        power.columnconfigure(1, weight=1)
        ttk.Label(power, text="Reported").grid(
            row=0, column=1, sticky=tk.E, padx=(0, 16))
        ttk.Label(power, text="Manual request").grid(
            row=0, column=2, sticky=tk.E)
        for row, (key, _attribute, label) in enumerate(
                POWER_RAIL_FIELDS, start=1):
            self.config_value_vars[key] = self._add_config_value(
                power, row, label, "Unknown")
            if key in MANUAL_POWER_RAIL_KEYS:
                enabled_variable = tk.BooleanVar(power, value=False)
                self.config_rail_enabled_vars[key] = enabled_variable
                checkbutton = ttk.Checkbutton(
                    power,
                    text="On",
                    variable=enabled_variable,
                    command=self._mark_config_dirty,
                    state=tk.DISABLED,
                )
                checkbutton.grid(row=row, column=2, sticky=tk.E)
                self.config_rail_checkbuttons[key] = checkbutton
            else:
                ttk.Label(
                    power,
                    text=("Not switchable" if key == "rail_5v"
                          else "Pulse-controlled"),
                ).grid(row=row, column=2, sticky=tk.E)
        ttk.Label(
            power,
            text=("Acquisition rails are applied in the required sequence. "
                  "The +18 V rail is energized only during a pulse."),
            wraplength=600,
        ).grid(row=6, column=0, columnspan=3, sticky=tk.W, pady=(8, 0))

        pulse = ttk.LabelFrame(power, text="Magnetic pulse", padding=8)
        pulse.grid(row=7, column=0, columnspan=3, sticky=tk.EW, pady=(10, 0))
        ttk.Label(pulse, text="Card").pack(side=tk.LEFT)
        self.pulse_slot_var = tk.StringVar(pulse, value="Slot 1")
        self.pulse_slot_combo = ttk.Combobox(
            pulse,
            textvariable=self.pulse_slot_var,
            values=tuple(PULSE_SLOT_OPTIONS),
            state=tk.DISABLED,
            width=8,
        )
        self.pulse_slot_combo.pack(side=tk.LEFT, padx=(6, 12))
        self.pulse_slot_combo.bind(
            "<<ComboboxSelected>>", lambda _event: self._update_controls())
        ttk.Label(pulse, text="Operation").pack(side=tk.LEFT)
        self.pulse_operation_var = tk.StringVar(pulse, value="SET")
        self.pulse_operation_combo = ttk.Combobox(
            pulse,
            textvariable=self.pulse_operation_var,
            values=tuple(PULSE_OPERATION_OPTIONS),
            state=tk.DISABLED,
            width=8,
        )
        self.pulse_operation_combo.pack(side=tk.LEFT, padx=(6, 12))
        self.pulse_button = ttk.Button(
            pulse,
            text="Send pulse",
            command=self._send_magnetic_pulse,
            state=tk.DISABLED,
        )
        self.pulse_button.pack(side=tk.LEFT)
        self.pulse_status_var = tk.StringVar(pulse, value="Not connected")
        ttk.Label(pulse, textvariable=self.pulse_status_var).pack(
            side=tk.LEFT, padx=(12, 0))

        navigation = ttk.LabelFrame(
            content, text="GNSS and IMU", padding=10)
        navigation.grid(row=4, column=0, sticky=tk.EW, pady=(0, 10))
        navigation.columnconfigure(1, weight=1)
        self.config_value_vars["gnss_state"] = self._add_config_value(
            navigation, 0, "GNSS state", "Unknown")
        self.config_value_vars["gnss_satellites"] = self._add_config_value(
            navigation, 1, "Satellites")
        self.config_value_vars["imu_state"] = self._add_config_value(
            navigation, 2, "IMU state", "Unknown")
        self.config_value_vars["imu_averaging"] = self._add_config_value(
            navigation, 3, "IMU averaging")
        self.config_value_vars["imu_roll"] = self._add_config_value(
            navigation, 4, "Roll")
        self.config_value_vars["imu_pitch"] = self._add_config_value(
            navigation, 5, "Pitch")
        self.config_value_vars["imu_temperature"] = self._add_config_value(
            navigation, 6, "IMU temperature")

        acquisition = ttk.LabelFrame(
            content, text="Acquisition", padding=10)
        acquisition.grid(row=5, column=0, sticky=tk.EW, pady=(0, 10))
        acquisition.columnconfigure(1, weight=1)
        ttk.Label(acquisition, text="Sampling rate").grid(
            row=0, column=0, sticky=tk.W, padx=(0, 16), pady=4)
        self.config_sample_rate_var = tk.StringVar(acquisition)
        self.config_sample_rate_combo = ttk.Combobox(
            acquisition,
            textvariable=self.config_sample_rate_var,
            values=tuple(SAMPLE_RATE_OPTIONS),
            state=tk.DISABLED,
            width=12,
        )
        self.config_sample_rate_combo.grid(
            row=0, column=1, sticky=tk.E, pady=4)
        self.config_sample_rate_combo.bind(
            "<<ComboboxSelected>>", self._mark_config_dirty)
        self.config_value_vars["adc_temperature"] = self._add_config_value(
            acquisition, 1, "ADC temperature")
        self.config_value_vars["channel_mask"] = self._add_config_value(
            acquisition, 2, "Active channel mask")

        slots = ttk.Frame(content)
        slots.grid(row=6, column=0, sticky=tk.EW)
        slots.columnconfigure(0, weight=1)
        self._build_config_slot(slots, 0, 1, 0)
        self._build_config_slot(slots, 1, 2, 4)

        self.config_apply_button = ttk.Button(
            content,
            text="Apply changes",
            command=self._apply_config_changes,
            state=tk.DISABLED,
        )
        self.config_apply_button.grid(
            row=7, column=0, sticky=tk.EW, pady=(2, 6))
        self.config_status_var = tk.StringVar(
            content, value="Not connected")
        ttk.Label(
            content,
            textvariable=self.config_status_var,
            wraplength=650,
        ).grid(row=8, column=0, sticky=tk.W, pady=(0, 12))

    def _mark_config_dirty(self, _event=None) -> None:
        if self._config_loading:
            return
        self.config_dirty = True
        self.config_status_var.set("Unsaved configuration changes")
        self._update_controls()

    def _config_update_from_controls(self) -> DeviceConfigUpdate:
        if self.current_config is None:
            raise ValueError("device configuration has not been loaded")
        try:
            sample_rate = SAMPLE_RATE_OPTIONS[
                self.config_sample_rate_var.get()]
        except KeyError as error:
            raise ValueError("select a valid ADC sampling rate") from error

        channel_mask = 0
        if self.config_slot_enabled_vars[1].get():
            channel_mask |= 0x0F
        if self.config_slot_enabled_vars[2].get():
            channel_mask |= 0xF0

        try:
            gains = tuple(
                GAIN_OPTIONS[self.config_gain_vars[channel].get()]
                for channel in range(8)
            )
        except KeyError as error:
            raise ValueError("select a valid gain for every channel") \
                from error
        return build_device_config_update(
            self.current_config,
            sample_rate,
            channel_mask,
            gains,
            tuple(
                self.config_rail_enabled_vars[key].get()
                for key in MANUAL_POWER_RAIL_KEYS
            ),
        )

    def _apply_config_changes(self) -> None:
        try:
            update = self._config_update_from_controls()
        except ValueError as error:
            messagebox.showerror("Configuration", str(error))
            return
        self.config_status_var.set("Applying configuration…")
        self._submit("set_config", update=update)

    def _send_magnetic_pulse(self) -> None:
        card_slot = PULSE_SLOT_OPTIONS[self.pulse_slot_var.get()]
        operation = PULSE_OPERATION_OPTIONS[self.pulse_operation_var.get()]
        slot_label = self.pulse_slot_var.get()
        operation_label = self.pulse_operation_var.get()
        if not messagebox.askyesno(
                "Magnetic pulse",
                f"Send {operation_label} pulse to {slot_label}?\n\n"
                "The +18 V rail will be energized briefly."):
            return
        self.pulse_status_var.set(
            f"Sending {operation_label} pulse to {slot_label}…")
        self._submit(
            "magnetic_pulse",
            card_slot=card_slot,
            operation=operation,
        )

    def _sync_live_channel_options(self, channel_mask: int) -> None:
        choices = tuple(
            label for label, mask in self.CHANNELS.items()
            if mask & channel_mask == mask
        )
        self.channels_combo.configure(values=choices)
        if not choices:
            self.channels_var.set("")
            return
        if self.channels_var.get() not in choices:
            exact = next(
                (label for label, mask in self.CHANNELS.items()
                 if mask == channel_mask),
                choices[0],
            )
            self.channels_var.set(exact)

    def _reset_config_display(self) -> None:
        for variable in self.config_value_vars.values():
            variable.set("—")
        self.config_value_vars["utc"].set("Unavailable")
        for variable in self.config_card_vars.values():
            variable.set("Unknown")
        self.config_sample_rate_var.set("")
        for variable in self.config_slot_enabled_vars.values():
            variable.set(False)
        for variable in self.config_gain_vars.values():
            variable.set("")
        for variable in self.config_rail_enabled_vars.values():
            variable.set(False)
        self.config_status_var.set("Not connected")
        self.pulse_status_var.set("Not connected")
        self.current_config = None
        self.config_dirty = False
        self._sync_live_channel_options(0)

    def _build_status_bar(self) -> None:
        self.connection_status_var = tk.StringVar(value="Disconnected")
        ttk.Label(
            self,
            textvariable=self.connection_status_var,
            anchor=tk.W,
            relief=tk.SUNKEN,
            padding=(6, 3),
        ).pack(fill=tk.X, pady=(8, 0))

    def refresh_ports(self) -> None:
        current = self.port_var.get()
        ports = sorted(
            list(list_ports.comports()),
            key=lambda port: port.device.lower(),
        )
        devices = [port.device for port in ports]
        self.port_combo.configure(values=devices)
        if current in devices:
            self.port_var.set(current)
            return
        preferred = next((
            port.device for port in ports
            if "usb" in (port.device + " " + port.description).lower()
        ), "")
        self.port_var.set(preferred or (devices[0] if devices else ""))

    def refresh_devices(self) -> None:
        if self.connection_type_var.get() == "Bluetooth LE":
            self._start_ble_scan()
        else:
            self.refresh_ports()

    def _connection_type_changed(self, _event=None) -> None:
        if self.connection_type_var.get() == "Bluetooth LE":
            self.device_label.configure(text="Bluetooth device")
            self.baud_combo.configure(state=tk.DISABLED)
            self.port_var.set("")
            self.port_combo.configure(values=())
            self._start_ble_scan()
        else:
            self.device_label.configure(text="USB / COM port")
            self.baud_combo.configure(state=tk.NORMAL)
            self._ble_devices.clear()
            self.refresh_ports()

    def _start_ble_scan(self) -> None:
        if self._ble_scan_running or self.worker is not None:
            return
        self._ble_scan_running = True
        self.port_var.set("")
        self.port_combo.configure(values=())
        self.refresh_ports_button.configure(state=tk.DISABLED)
        self.connection_status_var.set("Scanning for Bluetooth devices…")

        def scan() -> None:
            try:
                devices = asyncio.run(discover_ble_devices())
                self._ble_scan_events.put(UiEvent("ble_scan", devices))
            except Exception as error:
                self._ble_scan_events.put(
                    UiEvent("ble_scan_error", str(error)))

        threading.Thread(
            target=scan,
            name="geophys-ble-scan",
            daemon=True,
        ).start()

    def _poll_ble_scan(self) -> None:
        while True:
            try:
                event = self._ble_scan_events.get_nowait()
            except queue.Empty:
                return
            self._ble_scan_running = False
            self.refresh_ports_button.configure(
                state=tk.NORMAL if self.worker is None else tk.DISABLED)
            if self.connection_type_var.get() != "Bluetooth LE":
                continue
            if event.name == "ble_scan_error":
                self.connection_status_var.set("Bluetooth scan failed")
                messagebox.showerror("Bluetooth scan", event.payload)
                continue

            devices = event.payload
            self._ble_devices = {
                device.display_name: device for device in devices
            }
            labels = tuple(self._ble_devices)
            self.port_combo.configure(values=labels)
            self.port_var.set(labels[0] if labels else "")
            self.connection_status_var.set(
                f"Found {len(labels)} Bluetooth device(s)")

    def _toggle_connection(self) -> None:
        if self.worker is not None:
            self.connection_status_var.set("Disconnecting…")
            self.worker.shutdown()
            self.connect_button.configure(state=tk.DISABLED)
            return

        selected = self.port_var.get().strip()
        if not selected:
            messagebox.showerror("Connection", "Select a device.")
            return

        if self.connection_type_var.get() == "Bluetooth LE":
            device = self._ble_devices.get(selected)
            if device is None:
                messagebox.showerror(
                    "Connection", "Refresh and select a Bluetooth device.")
                return
            self.connection_status_var.set(
                f"Connecting to {device.display_name}…")
            self.worker = BleHelloWorker(device)
        else:
            try:
                baud_rate = int(self.baud_var.get())
            except ValueError:
                messagebox.showerror(
                    "Connection", "Baud rate must be an integer.")
                return
            self.connection_status_var.set(f"Connecting to {selected}…")
            self.worker = DeviceWorker(selected, baud_rate)

        self.connect_button.configure(state=tk.DISABLED)
        self.connection_type_combo.configure(state=tk.DISABLED)
        self.port_combo.configure(state=tk.DISABLED)
        self.refresh_ports_button.configure(state=tk.DISABLED)
        self.baud_combo.configure(state=tk.DISABLED)
        self.worker.start()

    def _submit(self, action: str, **payload: Any) -> None:
        if self.worker is None or not self.connected:
            messagebox.showerror("Device", "Connect to the device first.")
            return
        if self.pending_action is not None:
            return
        self.pending_action = action
        self.worker.submit(action, **payload)
        self._update_controls()

    def _refresh_recordings(self) -> None:
        if self.worker is None or not self.connected:
            messagebox.showerror("Device", "Connect to the device first.")
            return
        if self.catalog_loading:
            return
        self.catalog_loading = True
        self.recordings_status_var.set("Reading recording catalog…")
        self.worker.submit("refresh_recordings")
        self._update_controls()

    def _start_recording(self) -> None:
        name = self.recording_name_var.get().strip()
        if not name:
            messagebox.showerror("Recording", "Enter a recording name.")
            return
        if (self.current_config is None or
                self.current_config.sd_card_state != SD_STATE_PRESENT):
            messagebox.showerror(
                "Recording", "The SD card is not ready for recording.")
            return
        self.recordings_status_var.set("Starting recording…")
        self._submit("start_recording", name=name)

    def _stop_recording(self) -> None:
        self.recordings_status_var.set("Stopping recording…")
        self._submit("stop_recording")

    def _selected_recording(self) -> RecordingInfo | None:
        selection = self.recordings_tree.selection()
        if not selection:
            return None
        return self.recordings.get(selection[0])

    def _delete_recording(self) -> None:
        recording = self._selected_recording()
        if recording is None:
            return
        if not messagebox.askyesno(
                "Delete recording",
                f"Delete '{recording.name}' from the SD card?"):
            return
        self.recordings_status_var.set(f"Deleting {recording.name}…")
        self._submit("delete_recording", name=recording.name)

    def _start_live(self) -> None:
        channel_mask = self.CHANNELS[self.channels_var.get()]
        decimation = self.DECIMATIONS[self.decimation_var.get()]
        self.live_status_var.set("Starting live stream…")
        self._submit(
            "start_stream",
            channel_mask=channel_mask,
            decimation=decimation,
        )

    def _stop_live(self) -> None:
        self.live_status_var.set("Stopping live stream…")
        self._submit("stop_stream")

    def _update_controls(self) -> None:
        command_access = self.connected and not getattr(
            self, "ble_hello_only", False)
        ready = command_access and self.pending_action is None
        configuration_ready = (
            ready and self.current_config is not None and
            not self.recording_in_progress and not self.live_active
        )
        configured_channels = (
            self.current_config is not None and
            self.current_config.adc_channel_mask != 0
        )
        storage_ready = (
            self.current_config is not None and
            self.current_config.sd_card_state == SD_STATE_PRESENT
        )
        acquisition_ready = (
            ready and configured_channels and not self.config_dirty
        )
        selected = self._selected_recording() is not None
        self.refresh_recordings_button.configure(
            state=tk.NORMAL if command_access and
            not self.catalog_loading else tk.DISABLED)
        self.delete_recording_button.configure(
            state=tk.NORMAL if ready and selected and
            not self.recording_in_progress and
            not self.catalog_loading else tk.DISABLED)
        self.recording_name_entry.configure(
            state=tk.NORMAL if ready and
            not self.recording_in_progress else tk.DISABLED)
        self.start_recording_button.configure(
            state=tk.NORMAL if acquisition_ready and storage_ready and
            not self.recording_in_progress else tk.DISABLED)
        self.stop_recording_button.configure(
            state=tk.NORMAL if ready and
            self.recording_in_progress else tk.DISABLED)
        self.channels_combo.configure(
            state="readonly" if acquisition_ready and
            not self.live_active else tk.DISABLED)
        self.decimation_combo.configure(
            state="readonly" if acquisition_ready and
            not self.live_active else tk.DISABLED)
        self.start_live_button.configure(
            state=tk.NORMAL if acquisition_ready and
            not self.live_active else tk.DISABLED)
        self.stop_live_button.configure(
            state=tk.NORMAL if ready and self.live_active else tk.DISABLED)

        editor_state = "readonly" if configuration_ready else tk.DISABLED
        self.config_sample_rate_combo.configure(state=editor_state)
        for checkbutton in self.config_slot_checkbuttons.values():
            checkbutton.configure(
                state=tk.NORMAL if configuration_ready else tk.DISABLED)
        for combo in self.config_gain_combos.values():
            combo.configure(state=editor_state)
        for checkbutton in self.config_rail_checkbuttons.values():
            checkbutton.configure(
                state=tk.NORMAL if configuration_ready else tk.DISABLED)
        self.pulse_slot_combo.configure(
            state="readonly" if configuration_ready else tk.DISABLED)
        self.pulse_operation_combo.configure(
            state="readonly" if configuration_ready else tk.DISABLED)
        selected_slot = PULSE_SLOT_OPTIONS.get(self.pulse_slot_var.get())
        selected_card = None
        if self.current_config is not None:
            selected_card = (
                self.current_config.card_slot_1
                if selected_slot == MAGNETIC_CARD_SLOT_1
                else self.current_config.card_slot_2
            )
        pulse_ready = (
            configuration_ready and not self.config_dirty and
            selected_card == 1
        )
        self.pulse_button.configure(
            state=tk.NORMAL if pulse_ready else tk.DISABLED)
        self.config_apply_button.configure(
            state=tk.NORMAL if configuration_ready and
            self.config_dirty else tk.DISABLED)

    def _apply_config(
            self,
            config: DeviceConfig,
            *,
            force_editors: bool = False,
            status_message: str | None = None) -> None:
        recording_was_in_progress = self.recording_in_progress
        self.current_config = config
        self.recording_in_progress = config.recording_in_progress
        self.config_value_vars["uptime"].set(
            format_uptime(config.timestamp_100ns))
        self.config_value_vars["recording"].set(
            "In progress" if config.recording_in_progress else "Stopped")
        self.config_value_vars["sd_card"].set(
            SD_STATE_LABELS.get(config.sd_card_state, "Unknown"))
        self.config_value_vars["usb_5v"].set(
            "Present" if config.usb_5v_present else "Absent")
        self.config_value_vars["solar"].set(
            "Present" if config.solar_present else "Absent")
        self.config_value_vars["esp32_temperature"].set(
            format_centi_value(config.esp32_temperature_centi_c, "°C"))
        self.config_value_vars["error"].set(
            "Attention required" if config.error_pending else "None")
        self.config_value_vars["rail_3v3"].set(
            format_enabled(config.rail_3v3_enabled))
        self.config_value_vars["rail_5v"].set(
            format_enabled(config.rail_5v_enabled))
        self.config_value_vars["rail_9v"].set(
            format_enabled(config.rail_9v_enabled))
        self.config_value_vars["rail_negative_5v"].set(
            format_enabled(config.rail_negative_5v_enabled))
        self.config_value_vars["rail_18v"].set(
            format_enabled(config.rail_18v_enabled))
        self.config_value_vars["gnss_state"].set(
            GNSS_STATE_LABELS.get(config.gnss_state, "Unknown"))
        self.config_value_vars["gnss_satellites"].set(
            str(config.gnss_satellite_count))
        self.config_value_vars["imu_state"].set(
            IMU_STATE_LABELS.get(config.imu_state, "Unknown"))
        self.config_value_vars["imu_averaging"].set(
            f"{config.imu_averaging_time_ms} ms"
            if config.imu_averaging_time_ms else "Unavailable")
        self.config_value_vars["imu_roll"].set(
            format_centi_value(config.imu_roll_centi_degrees, "°"))
        self.config_value_vars["imu_pitch"].set(
            format_centi_value(config.imu_pitch_centi_degrees, "°"))
        self.config_value_vars["imu_temperature"].set(
            format_centi_value(config.imu_temperature_centi_c, "°C"))
        self.config_value_vars["adc_temperature"].set(
            format_centi_value(config.adc_temperature_centi_c, "°C"))
        self.config_value_vars["channel_mask"].set(
            f"0x{config.adc_channel_mask:02X}")
        self.config_card_vars[1].set(
            CARD_TYPE_LABELS.get(config.card_slot_1, "Unknown"))
        self.config_card_vars[2].set(
            CARD_TYPE_LABELS.get(config.card_slot_2, "Unknown"))

        if force_editors or not self.config_dirty:
            self._config_loading = True
            try:
                self.config_sample_rate_var.set(
                    SAMPLE_RATE_LABELS[config.adc_sample_rate])
                self.config_slot_enabled_vars[1].set(
                    bool(config.adc_channel_mask & 0x0F))
                self.config_slot_enabled_vars[2].set(
                    bool(config.adc_channel_mask & 0xF0))
                for channel, gain in enumerate(
                        unpack_adc_gains(config.adc_gain)):
                    self.config_gain_vars[channel].set(GAIN_LABELS[gain])
                for key, attribute, _label in POWER_RAIL_FIELDS:
                    if key not in self.config_rail_enabled_vars:
                        continue
                    self.config_rail_enabled_vars[key].set(
                        getattr(config, attribute))
                self.config_dirty = False
            finally:
                self._config_loading = False

        self._sync_live_channel_options(config.adc_channel_mask)
        if status_message is not None:
            self.config_status_var.set(status_message)
        if config.recording_in_progress:
            self.recordings_status_var.set("Recording in progress")
        elif recording_was_in_progress:
            message = unexpected_recording_stop_message(
                config.sd_card_state)
            self.recordings_status_var.set(message)
            messagebox.showerror("Recording stopped", message)
        self._update_controls()

    def _show_recordings(self, recordings: list[RecordingInfo]) -> None:
        self.recordings_tree.delete(*self.recordings_tree.get_children())
        self.recordings.clear()
        for recording in recordings:
            item_id = str(recording.index)
            self.recordings[item_id] = recording
            self.recordings_tree.insert(
                "",
                tk.END,
                iid=item_id,
                values=(
                    recording.index,
                    recording.name,
                    format_size(recording.size_bytes),
                    format_timestamp(recording.start_unix_timestamp_us),
                    "Recording" if recording.recording_in_progress else "Closed",
                ),
            )
        self.recordings_status_var.set(
            f"{len(recordings)} recording(s) on the SD card")

    def _handle_event(self, event: UiEvent) -> None:
        if event.name == "connected":
            self.connected = True
            self.pending_action = None
            info = event.payload["info"]
            mac = ":".join(f"{byte:02x}" for byte in info.mac_address)
            self.connect_button.configure(text="Disconnect", state=tk.NORMAL)
            self.ble_hello_only = event.payload.get(
                "ble_hello_only", False)
            if self.ble_hello_only:
                label = event.payload["connection_label"]
                self.connection_status_var.set(
                    f"BLE connected to {label}  •  device {mac}")
                self.recordings_status_var.set(
                    "Bluetooth connected — HELLO only")
                self.live_status_var.set(
                    "Bluetooth connected — live streaming not implemented")
                self.config_status_var.set(
                    "USB connection required for configuration")
                self._update_controls()
            else:
                self.connection_status_var.set(
                    f"Connected to {self.worker.port}  •  device {mac}")
                self._apply_config(
                    event.payload["config"],
                    force_editors=True,
                    status_message="Configuration loaded",
                )
                self._refresh_recordings()
        elif event.name == "config":
            self._apply_config(event.payload)
        elif event.name == "config_applied":
            self.pending_action = None
            self._apply_config(
                event.payload,
                force_editors=True,
                status_message="Configuration applied",
            )
        elif event.name == "pulse_completed":
            self.pending_action = None
            slot = f"Slot {event.payload.card_slot}"
            operation = (
                "SET" if event.payload.operation == MAGNETIC_PULSE_SET
                else "RESET")
            self.pulse_status_var.set(
                f"{operation} pulse completed on {slot}")
            self._update_controls()
        elif event.name == "recordings_loading":
            self.catalog_loading = True
            self.recordings_status_var.set("Reading recording catalog…")
            self._update_controls()
        elif event.name == "recordings":
            self.catalog_loading = False
            self._show_recordings(event.payload)
            self._update_controls()
        elif event.name == "storage_media_absent":
            self.catalog_loading = False
            self.recordings_tree.delete(
                *self.recordings_tree.get_children())
            self.recordings.clear()
            self.recordings_status_var.set("SD card is not connected")
            self._update_controls()
            messagebox.showinfo("SD card", "SD card is not connected.")
        elif event.name == "recordings_error":
            self.catalog_loading = False
            self.recordings_status_var.set("Could not read recording catalog")
            self._update_controls()
            messagebox.showerror("Recordings", event.payload)
        elif event.name == "recording_started":
            self.pending_action = None
            self.recording_in_progress = True
            self.recordings_status_var.set(
                f"Recording '{event.payload.name}'")
            self.config_status_var.set(
                "Stop recording before changing ADC configuration")
            self._update_controls()
        elif event.name == "recording_stopped":
            self.pending_action = None
            self.recording_in_progress = False
            self.recordings_status_var.set(
                f"Stopped '{event.payload.name}'")
            if not self.config_dirty:
                self.config_status_var.set("Configuration loaded")
            self._update_controls()
        elif event.name == "recording_failed":
            self.pending_action = None
            self.recording_in_progress = False
            sd_card_state = (
                self.current_config.sd_card_state
                if self.current_config is not None else SD_STATE_FAULTED
            )
            message = unexpected_recording_stop_message(
                sd_card_state, event.payload.result)
            self.recordings_status_var.set(message)
            if not self.config_dirty:
                self.config_status_var.set("Configuration loaded")
            self._update_controls()
            messagebox.showerror("Recording stopped", message)
        elif event.name == "recording_deleted":
            self.pending_action = None
            self.recordings_status_var.set(
                f"Deleted '{event.payload.name}'")
            self._update_controls()
        elif event.name == "stream_started":
            self.pending_action = None
            result = event.payload["result"]
            self.active_stream_generation = event.payload["generation"]
            self.live_model = LiveStreamModel(
                window_s=5.0,
                source_sequence_step=result.decimation or 1,
            )
            self.live_plot.configure_channels(result.channel_mask)
            self.live_active = True
            self.live_status_var.set(
                f"Streaming channels 0x{result.channel_mask:02x}")
            self.config_status_var.set(
                "Stop live streaming before changing ADC configuration")
            self.notebook.select(self.live_tab)
            self._update_controls()
        elif event.name == "stream_stopped":
            self.pending_action = None
            self.live_active = False
            self.live_status_var.set("Live stream stopped")
            if not self.config_dirty:
                self.config_status_var.set("Configuration loaded")
            self._update_controls()
        elif event.name == "link_stats":
            self.link_stats = event.payload
        elif event.name == "action_error":
            if event.payload["action"] == "refresh_recordings":
                self.catalog_loading = False
            elif self.pending_action == event.payload["action"]:
                self.pending_action = None
            action = event.payload["action"].replace("_", " ")
            message = event.payload["message"]
            if event.payload["action"] == "start_stream":
                self.live_status_var.set("Live stream did not start")
            elif event.payload["action"] == "set_config":
                self.config_status_var.set(
                    f"Configuration was not applied: {message}")
            elif event.payload["action"] == "magnetic_pulse":
                self.pulse_status_var.set(f"Pulse failed: {message}")
            self._update_controls()
            messagebox.showerror(action.title(), message)
        elif event.name == "connection_error":
            messagebox.showerror("Connection", event.payload)
        elif event.name == "disconnected":
            self.connected = False
            self.ble_hello_only = False
            self.pending_action = None
            self.catalog_loading = False
            self.live_active = False
            self.recording_in_progress = False
            self.connection_status_var.set("Disconnected")
            self.recordings_status_var.set("Not connected")
            self.live_status_var.set("Not connected")
            self._reset_config_display()
            self.connect_button.configure(text="Connect", state=tk.NORMAL)
            self.connection_type_combo.configure(state="readonly")
            self.port_combo.configure(state="readonly")
            self.refresh_ports_button.configure(state=tk.NORMAL)
            self.baud_combo.configure(
                state=tk.DISABLED
                if self.connection_type_var.get() == "Bluetooth LE"
                else tk.NORMAL)
            self.worker = None
            self._update_controls()

    def _poll_worker(self) -> None:
        self._poll_ble_scan()
        worker = self.worker
        if worker is not None:
            while True:
                try:
                    event = worker.events.get_nowait()
                except queue.Empty:
                    break
                self._handle_event(event)

            processed = 0
            while processed < 256:
                try:
                    generation, message = worker.records.get_nowait()
                except queue.Empty:
                    break
                if generation == self.active_stream_generation and \
                        self.live_model is not None:
                    try:
                        self.live_model.append(message.record)
                    except ValueError as error:
                        self.live_status_var.set(str(error))
                processed += 1

            now = time.monotonic()
            if self.live_model is not None and \
                    now - self._last_plot_update >= self.PLOT_REFRESH_S:
                self.live_plot.update_plot(self.live_model, self.link_stats)
                self._last_plot_update = now

        self.after(self.PORT_POLL_MS, self._poll_worker)

    def _on_close(self) -> None:
        if self.worker is not None:
            self.worker.shutdown()
            self.worker.join(0.5)
        self.root.destroy()


def main() -> int:
    root = tk.Tk()
    GeophysHostApp(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
