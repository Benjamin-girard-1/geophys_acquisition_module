"""Tk desktop application for device connection, recordings, and live data."""

from __future__ import annotations

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

from .live import LiveStreamModel
from .protocol import (
    REPLY_DEVICE_CONFIG,
    REPLY_DEVICE_INFO,
    REPLY_RECORDING_DELETE_RESULT,
    REPLY_RECORDING_INFO,
    REPLY_RECORDING_NUMBER,
    REPLY_RECORDING_START_RESULT,
    REPLY_RECORDING_STOP_RESULT,
    REPLY_STREAMING_START_RESULT,
    REPLY_STREAMING_STOP_RESULT,
    RESULT_SUCCESS,
    DeviceConfig,
    RecordingInfo,
    decode_device_config,
    decode_device_info,
    decode_recording_delete_result,
    decode_recording_info,
    decode_recording_number,
    decode_recording_start_result,
    decode_recording_stop_result,
    decode_streaming_start_result,
    decode_streaming_stop_result,
    encode_device_get_config,
    encode_hello,
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
        self._emit("config", config)
        return config

    def _refresh_recordings(self) -> None:
        self._emit("recordings_loading")
        try:
            number = decode_recording_number(self._request(
                encode_recording_get_number(),
                REPLY_RECORDING_NUMBER,
                self.STORAGE_TIMEOUT_S,
            ))
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
        self._emit("recording_started", result)
        self._read_config()
        self._refresh_recordings()

    def _stop_recording(self) -> None:
        result = decode_recording_stop_result(self._request(
            encode_recording_stop(),
            REPLY_RECORDING_STOP_RESULT,
            self.STORAGE_TIMEOUT_S,
        ))
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
        self.worker: DeviceWorker | None = None
        self.connected = False
        self.recording_in_progress = False
        self.live_active = False
        self.pending_action: str | None = None
        self.catalog_loading = False
        self.active_stream_generation = 0
        self.live_model: LiveStreamModel | None = None
        self.link_stats: dict[str, int] = {}
        self.recordings: dict[str, RecordingInfo] = {}
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
        frame.columnconfigure(1, weight=1)

        ttk.Label(frame, text="USB / COM port").grid(
            row=0, column=0, padx=(0, 8), sticky=tk.W)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(
            frame, textvariable=self.port_var, state="readonly", width=42)
        self.port_combo.grid(row=0, column=1, sticky=tk.EW)
        self.refresh_ports_button = ttk.Button(
            frame, text="Refresh", command=self.refresh_ports)
        self.refresh_ports_button.grid(row=0, column=2, padx=6)

        ttk.Label(frame, text="Baud").grid(row=0, column=3, padx=(12, 6))
        self.baud_var = tk.StringVar(value="921600")
        self.baud_combo = ttk.Combobox(
            frame,
            textvariable=self.baud_var,
            values=("921600", "460800", "115200"),
            width=10,
        )
        self.baud_combo.grid(row=0, column=4)
        self.connect_button = ttk.Button(
            frame, text="Connect", command=self._toggle_connection)
        self.connect_button.grid(row=0, column=5, padx=(8, 0))

    def _build_tabs(self) -> None:
        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True)
        self.recordings_tab = ttk.Frame(self.notebook, padding=10)
        self.live_tab = ttk.Frame(self.notebook, padding=10)
        self.notebook.add(self.recordings_tab, text="Recordings")
        self.notebook.add(self.live_tab, text="Live Stream")
        self._build_recordings_tab()
        self._build_live_tab()

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

    def _toggle_connection(self) -> None:
        if self.worker is not None:
            self.connection_status_var.set("Disconnecting…")
            self.worker.shutdown()
            self.connect_button.configure(state=tk.DISABLED)
            return

        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("Connection", "Select a USB / COM port.")
            return
        try:
            baud_rate = int(self.baud_var.get())
        except ValueError:
            messagebox.showerror("Connection", "Baud rate must be an integer.")
            return
        self.connection_status_var.set(f"Connecting to {port}…")
        self.connect_button.configure(state=tk.DISABLED)
        self.port_combo.configure(state=tk.DISABLED)
        self.refresh_ports_button.configure(state=tk.DISABLED)
        self.worker = DeviceWorker(port, baud_rate)
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
        ready = self.connected and self.pending_action is None
        selected = self._selected_recording() is not None
        self.refresh_recordings_button.configure(
            state=tk.NORMAL if self.connected and
            not self.catalog_loading else tk.DISABLED)
        self.delete_recording_button.configure(
            state=tk.NORMAL if ready and selected and
            not self.recording_in_progress and
            not self.catalog_loading else tk.DISABLED)
        self.recording_name_entry.configure(
            state=tk.NORMAL if ready and
            not self.recording_in_progress else tk.DISABLED)
        self.start_recording_button.configure(
            state=tk.NORMAL if ready and
            not self.recording_in_progress else tk.DISABLED)
        self.stop_recording_button.configure(
            state=tk.NORMAL if ready and
            self.recording_in_progress else tk.DISABLED)
        self.channels_combo.configure(
            state="readonly" if ready and not self.live_active else tk.DISABLED)
        self.decimation_combo.configure(
            state="readonly" if ready and not self.live_active else tk.DISABLED)
        self.start_live_button.configure(
            state=tk.NORMAL if ready and not self.live_active else tk.DISABLED)
        self.stop_live_button.configure(
            state=tk.NORMAL if ready and self.live_active else tk.DISABLED)

    def _apply_config(self, config: DeviceConfig) -> None:
        self.recording_in_progress = config.recording_in_progress
        if config.recording_in_progress:
            self.recordings_status_var.set("Recording in progress")
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
            self.connection_status_var.set(
                f"Connected to {self.worker.port}  •  device {mac}")
            self.connect_button.configure(text="Disconnect", state=tk.NORMAL)
            self._apply_config(event.payload["config"])
            self._refresh_recordings()
        elif event.name == "config":
            self._apply_config(event.payload)
        elif event.name == "recordings_loading":
            self.catalog_loading = True
            self.recordings_status_var.set("Reading recording catalog…")
            self._update_controls()
        elif event.name == "recordings":
            self.catalog_loading = False
            self._show_recordings(event.payload)
            self._update_controls()
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
            self._update_controls()
        elif event.name == "recording_stopped":
            self.pending_action = None
            self.recording_in_progress = False
            self.recordings_status_var.set(
                f"Stopped '{event.payload.name}'")
            self._update_controls()
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
            self.notebook.select(self.live_tab)
            self._update_controls()
        elif event.name == "stream_stopped":
            self.pending_action = None
            self.live_active = False
            self.live_status_var.set("Live stream stopped")
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
            self._update_controls()
            messagebox.showerror(action.title(), message)
        elif event.name == "connection_error":
            messagebox.showerror("Connection", event.payload)
        elif event.name == "disconnected":
            self.connected = False
            self.pending_action = None
            self.catalog_loading = False
            self.live_active = False
            self.recording_in_progress = False
            self.connection_status_var.set("Disconnected")
            self.recordings_status_var.set("Not connected")
            self.live_status_var.set("Not connected")
            self.connect_button.configure(text="Connect", state=tk.NORMAL)
            self.port_combo.configure(state="readonly")
            self.refresh_ports_button.configure(state=tk.NORMAL)
            self.worker = None
            self._update_controls()

    def _poll_worker(self) -> None:
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
