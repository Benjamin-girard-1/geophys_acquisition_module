from __future__ import annotations

from collections import deque
from pathlib import Path
import struct
import sys
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import matplotlib

HOST_APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HOST_APP))

from geophys_host.gui import (  # noqa: E402
    DEFAULT_LIVE_PLOT_SELECTIONS,
    DeviceWorker,
    GeophysHostApp,
    UiEvent,
    active_live_plot_channels,
    build_device_config_update,
    format_size,
    format_timestamp,
    format_uptime,
    pack_adc_gains,
    parse_live_display_settings,
    subtract_display_constant,
    unexpected_recording_stop_message,
    unpack_adc_gains,
)
from geophys_host.protocol import (  # noqa: E402
    ADC_SAMPLE_RATE_1000_SPS,
    ADC_SAMPLE_RATE_2000_SPS,
    DIRECTION_TO_HOST,
    MAGNETIC_CARD_SLOT_1,
    MAGNETIC_PULSE_SET,
    REPLY_DEVICE_CONFIG,
    REPLY_DEVICE_INFO,
    REPLY_MAGNETIC_PULSE_RESULT,
    REPLY_RECORDING_INFO,
    REPLY_RECORDING_NUMBER,
    REPLY_RECORDING_STOP_RESULT,
    REPLY_STREAMING_START_RESULT,
    RESULT_STORAGE_FULL,
    DeviceConfigUpdate,
    decode_command,
    decode_device_config,
    encode_command,
    encode_hello,
)


class FakeClient:
    def __init__(self, replies) -> None:
        self.replies = deque(replies)
        self.requests = []

    def request(self, frame, reply_id, timeout_s, on_record):
        self.requests.append((decode_command(frame), reply_id, on_record))
        reply = self.replies.popleft()
        if reply.command_id != reply_id:
            raise AssertionError("fake reply ID mismatch")
        return reply


class FakeControl:
    def __init__(self) -> None:
        self.state = None
        self.values = None

    def configure(self, **options) -> None:
        if "state" in options:
            self.state = options["state"]
        if "values" in options:
            self.values = options["values"]


class FakeVariable:
    def __init__(self, value=None) -> None:
        self.value = value

    def get(self):
        return self.value

    def set(self, value) -> None:
        self.value = value


class FakeWorker:
    def __init__(self) -> None:
        self.submissions = []

    def submit(self, action: str, **payload) -> None:
        self.submissions.append((action, payload))


def reply(command_id: int, payload: bytes):
    return decode_command(encode_command(
        command_id, DIRECTION_TO_HOST, payload))


def recording_info_payload(index: int, name: str, size: int) -> bytes:
    payload = bytearray(48)
    struct.pack_into("<BHB", payload, 0, 0, index, 0)
    encoded_name = name.encode("ascii") + b"\0"
    payload[4:4 + len(encoded_name)] = encoded_name
    struct.pack_into("<QI", payload, 36, 0, size)
    return bytes(payload)


def device_config_payload(
        *, sample_rate: int = ADC_SAMPLE_RATE_1000_SPS,
        channel_mask: int = 0xFF,
        packed_gain: int = 0,
        rail_3v3: bool = False,
        rail_5v: bool = False,
        rail_9v: bool = False,
        rail_negative_5v: bool = False,
        rail_18v: bool = False,
        imu_averaging_ms: int = 0) -> bytes:
    payload = bytearray(40)
    payload[12] = sample_rate
    payload[13] = channel_mask
    struct.pack_into("<H", payload, 14, packed_gain)
    payload[18] = rail_3v3
    payload[19] = rail_5v
    payload[20] = rail_9v
    payload[21] = rail_negative_5v
    payload[22] = rail_18v
    struct.pack_into("<H", payload, 28, imu_averaging_ms)
    return bytes(payload)


class GuiSupportTests(unittest.TestCase):
    def test_desktop_gui_uses_only_the_tk_matplotlib_backend(self) -> None:
        self.assertEqual(matplotlib.get_backend().lower(), "tkagg")
        self.assertNotIn("matplotlib.backends._macosx", sys.modules)

    def test_display_formatters(self) -> None:
        self.assertEqual(format_size(512), "512 B")
        self.assertEqual(format_size(1536), "1.5 KiB")
        self.assertEqual(format_size(2 * 1024 * 1024), "2.0 MiB")
        self.assertEqual(format_timestamp(0), "—")
        self.assertEqual(format_uptime(90_061 * 10_000_000),
                         "1 d 01:01:01")
        self.assertEqual(
            unexpected_recording_stop_message(2, RESULT_STORAGE_FULL),
            "Recording stopped — the SD card is full",
        )
        self.assertEqual(
            unexpected_recording_stop_message(2),
            "Recording stopped unexpectedly — the SD card reported a fault",
        )

    def test_adc_gain_pack_and_unpack(self) -> None:
        gains = (1, 2, 4, 8, 1, 2, 4, 8)

        packed = pack_adc_gains(gains)

        self.assertEqual(packed, 0xE4E4)
        self.assertEqual(unpack_adc_gains(packed), gains)

    def test_live_display_defaults_hide_channels_three_and_seven(self) -> None:
        assignments, constants = parse_live_display_settings(
            DEFAULT_LIVE_PLOT_SELECTIONS,
            ("0",) * 8,
        )

        self.assertEqual(assignments, (1, 1, 1, 0, 2, 2, 2, 0))
        self.assertEqual(constants, (0.0,) * 8)
        self.assertEqual(
            active_live_plot_channels(0xFF, assignments),
            {1: (0, 1, 2), 2: (4, 5, 6)},
        )
        self.assertEqual(
            active_live_plot_channels(0x0F, assignments),
            {1: (0, 1, 2)},
        )
        self.assertEqual(active_live_plot_channels(0, assignments), {})

    def test_hidden_channel_can_be_routed_to_a_second_graph(self) -> None:
        assignments = (1, 1, 1, 2, 0, 0, 0, 0)

        self.assertEqual(
            active_live_plot_channels(0x0F, assignments),
            {1: (0, 1, 2), 2: (3,)},
        )

    def test_live_display_subtracts_independent_channel_constant(self) -> None:
        selections = list(DEFAULT_LIVE_PLOT_SELECTIONS)
        selections[3] = "Graph 2"
        constants = ["0"] * 8
        constants[1] = "12.5"

        assignments, offsets = parse_live_display_settings(
            tuple(selections), tuple(constants))

        self.assertEqual(assignments[3], 2)
        self.assertEqual(offsets[1], 12.5)
        self.assertEqual(
            subtract_display_constant([10, 20], offsets[1]),
            [-2.5, 7.5],
        )

    def test_live_display_rejects_nonfinite_constant(self) -> None:
        constants = ["0"] * 8
        constants[6] = "nan"

        with self.assertRaisesRegex(ValueError, "CH6.*finite"):
            parse_live_display_settings(
                DEFAULT_LIVE_PLOT_SELECTIONS, tuple(constants))

    def test_config_update_preserves_unedited_device_fields(self) -> None:
        current = decode_device_config(reply(
            REPLY_DEVICE_CONFIG,
            device_config_payload(
                rail_3v3=True,
                rail_9v=True,
                imu_averaging_ms=250,
            ),
        ))

        update = build_device_config_update(
            current,
            ADC_SAMPLE_RATE_2000_SPS,
            0x0F,
            (1, 2, 4, 8, 1, 2, 4, 8),
        )

        self.assertEqual(update.adc_sample_rate, ADC_SAMPLE_RATE_2000_SPS)
        self.assertEqual(update.adc_channel_mask, 0x0F)
        self.assertEqual(update.adc_gain, 0xE4E4)
        self.assertTrue(update.rail_3v3_enabled)
        self.assertTrue(update.rail_9v_enabled)
        self.assertEqual(update.imu_averaging_time_ms, 250)

    def test_config_update_uses_manual_power_rail_states(self) -> None:
        current = decode_device_config(reply(
            REPLY_DEVICE_CONFIG,
            device_config_payload(
                rail_3v3=True,
                rail_5v=True,
                rail_9v=True,
                rail_18v=True,
            ),
        ))

        update = build_device_config_update(
            current,
            ADC_SAMPLE_RATE_1000_SPS,
            0xFF,
            (1,) * 8,
            (False, True, False),
        )

        self.assertFalse(update.rail_3v3_enabled)
        self.assertTrue(update.rail_5v_enabled)
        self.assertTrue(update.rail_9v_enabled)
        self.assertFalse(update.rail_negative_5v_enabled)
        self.assertTrue(update.rail_18v_enabled)

    def test_config_update_requires_at_least_one_slot(self) -> None:
        current = decode_device_config(reply(
            REPLY_DEVICE_CONFIG, device_config_payload()))

        with self.assertRaisesRegex(ValueError, "at least one"):
            build_device_config_update(
                current, ADC_SAMPLE_RATE_1000_SPS, 0, (1,) * 8)

    def test_config_controls_build_channels_zero_to_three_update(self) -> None:
        current = decode_device_config(reply(
            REPLY_DEVICE_CONFIG,
            device_config_payload(rail_3v3=True, imu_averaging_ms=100),
        ))
        gain_labels = ("×1", "×2", "×4", "×8") * 2
        app = SimpleNamespace(
            current_config=current,
            config_sample_rate_var=FakeVariable("2 kS/s"),
            config_slot_enabled_vars={
                1: FakeVariable(True),
                2: FakeVariable(False),
            },
            config_gain_vars={
                channel: FakeVariable(label)
                for channel, label in enumerate(gain_labels)
            },
            config_rail_enabled_vars={
                "rail_3v3": FakeVariable(False),
                "rail_9v": FakeVariable(True),
                "rail_negative_5v": FakeVariable(False),
            },
        )

        update = GeophysHostApp._config_update_from_controls(app)

        self.assertEqual(update.adc_sample_rate, ADC_SAMPLE_RATE_2000_SPS)
        self.assertEqual(update.adc_channel_mask, 0x0F)
        self.assertEqual(update.adc_gain, 0xE4E4)
        self.assertFalse(update.rail_3v3_enabled)
        self.assertFalse(update.rail_5v_enabled)
        self.assertTrue(update.rail_9v_enabled)
        self.assertFalse(update.rail_negative_5v_enabled)
        self.assertFalse(update.rail_18v_enabled)
        self.assertEqual(update.imu_averaging_time_ms, 100)

    def test_four_channel_config_limits_live_channel_choices(self) -> None:
        channels = FakeControl()
        selected = FakeVariable("All channels")
        app = SimpleNamespace(
            CHANNELS=GeophysHostApp.CHANNELS,
            channels_combo=channels,
            channels_var=selected,
        )

        GeophysHostApp._sync_live_channel_options(app, 0x0F)

        self.assertEqual(channels.values, ("Channels 0–3",))
        self.assertEqual(selected.get(), "Channels 0–3")

    def test_worker_reads_complete_recording_catalog(self) -> None:
        client = FakeClient([
            reply(REPLY_RECORDING_NUMBER, b"\0\x02\0"),
            reply(
                REPLY_RECORDING_INFO,
                recording_info_payload(0, "first", 512)),
            reply(
                REPLY_RECORDING_INFO,
                recording_info_payload(1, "second", 1024)),
        ])
        worker = DeviceWorker("unused", 921_600)
        worker._client = client

        worker._refresh_recordings()

        loading_event = worker.events.get_nowait()
        event = worker.events.get_nowait()
        self.assertEqual(loading_event.name, "recordings_loading")
        self.assertEqual(event.name, "recordings")
        self.assertEqual(
            [(item.name, item.size_bytes) for item in event.payload],
            [("first", 512), ("second", 1024)],
        )
        self.assertEqual(len(client.requests), 3)

    def test_worker_reports_absent_sd_card_separately(self) -> None:
        client = FakeClient([
            reply(REPLY_RECORDING_NUMBER, b"\x09\0\0"),
        ])
        worker = DeviceWorker("unused", 921_600)
        worker._client = client

        worker._refresh_recordings()

        self.assertEqual(worker.events.get_nowait().name,
                         "recordings_loading")
        self.assertEqual(worker.events.get_nowait().name,
                         "storage_media_absent")
        self.assertTrue(worker.events.empty())

    @patch("geophys_host.gui.messagebox.showinfo")
    def test_gui_notifies_when_sd_card_is_absent(self, showinfo) -> None:
        recordings_tree = Mock()
        recordings_tree.get_children.return_value = ("0",)
        recordings_status = Mock()
        update_controls = Mock()
        app = SimpleNamespace(
            catalog_loading=True,
            recordings={"0": object()},
            recordings_tree=recordings_tree,
            recordings_status_var=recordings_status,
            _update_controls=update_controls,
        )

        GeophysHostApp._handle_event(
            app, UiEvent("storage_media_absent"))

        self.assertFalse(app.catalog_loading)
        self.assertEqual(app.recordings, {})
        recordings_tree.delete.assert_called_once_with("0")
        recordings_status.set.assert_called_once_with(
            "SD card is not connected")
        update_controls.assert_called_once_with()
        showinfo.assert_called_once_with(
            "SD card", "SD card is not connected.")

    def test_successful_request_postpones_keepalive(self) -> None:
        client = FakeClient([
            reply(REPLY_DEVICE_INFO, bytes(16)),
        ])
        worker = DeviceWorker("unused", 921_600)
        worker._client = client
        before = time.monotonic()

        worker._request(encode_hello(), REPLY_DEVICE_INFO)

        self.assertGreaterEqual(worker._next_keepalive, before + 1.0)

    def test_worker_applies_device_configuration(self) -> None:
        client = FakeClient([
            reply(
                REPLY_DEVICE_CONFIG,
                device_config_payload(
                    sample_rate=ADC_SAMPLE_RATE_2000_SPS,
                    channel_mask=0x0F,
                    packed_gain=0xE4E4,
                ),
            ),
        ])
        worker = DeviceWorker("unused", 921_600)
        worker._client = client
        update = DeviceConfigUpdate(
            adc_sample_rate=ADC_SAMPLE_RATE_2000_SPS,
            adc_channel_mask=0x0F,
            adc_gain=0xE4E4,
            rail_3v3_enabled=True,
            rail_5v_enabled=False,
            rail_9v_enabled=True,
            rail_negative_5v_enabled=True,
            rail_18v_enabled=False,
        )

        worker._set_config(update)

        event = worker.events.get_nowait()
        self.assertEqual(event.name, "config_applied")
        self.assertEqual(event.payload.adc_channel_mask, 0x0F)
        command, reply_id, _on_record = client.requests[0]
        self.assertEqual(command.command_id, 0x0003)
        self.assertEqual(command.payload[18:23], b"\x01\x00\x01\x01\x00")
        self.assertEqual(reply_id, REPLY_DEVICE_CONFIG)

    def test_worker_sends_magnetic_pulse(self) -> None:
        client = FakeClient([
            reply(
                REPLY_MAGNETIC_PULSE_RESULT,
                bytes((0, MAGNETIC_CARD_SLOT_1, MAGNETIC_PULSE_SET)),
            ),
            reply(REPLY_DEVICE_CONFIG, device_config_payload()),
        ])
        worker = DeviceWorker("unused", 921_600)
        worker._client = client

        worker._magnetic_pulse(MAGNETIC_CARD_SLOT_1, MAGNETIC_PULSE_SET)

        completed = worker.events.get_nowait()
        config = worker.events.get_nowait()
        self.assertEqual(completed.name, "pulse_completed")
        self.assertEqual(completed.payload.card_slot, MAGNETIC_CARD_SLOT_1)
        self.assertEqual(config.name, "config")
        command, reply_id, _on_record = client.requests[0]
        self.assertEqual(command.command_id, 0x000C)
        self.assertEqual(command.payload, b"\x01\x01")
        self.assertEqual(reply_id, REPLY_MAGNETIC_PULSE_RESULT)

    def test_worker_recovers_asynchronous_recording_failure(self) -> None:
        failure_payload = bytes((RESULT_STORAGE_FULL,)) + \
            b"failed_recording\0" + bytes(15)
        client = FakeClient([
            reply(
                REPLY_DEVICE_CONFIG,
                device_config_payload(channel_mask=0x0F),
            ),
            reply(REPLY_RECORDING_STOP_RESULT, failure_payload),
        ])
        worker = DeviceWorker("unused", 921_600)
        worker._client = client
        worker._recording_in_progress = True

        config = worker._read_config()

        self.assertFalse(config.recording_in_progress)
        failure_event = worker.events.get_nowait()
        config_event = worker.events.get_nowait()
        self.assertEqual(failure_event.name, "recording_failed")
        self.assertEqual(failure_event.payload.result, RESULT_STORAGE_FULL)
        self.assertEqual(config_event.name, "config")
        self.assertEqual(client.requests[1][0].command_id, 0x0008)

    def test_catalog_refresh_does_not_disable_unrelated_controls(self) -> None:
        controls = {
            name: FakeControl()
            for name in (
                "refresh_recordings_button",
                "delete_recording_button",
                "recording_name_entry",
                "start_recording_button",
                "stop_recording_button",
                "channels_combo",
                "decimation_combo",
                "start_live_button",
                "stop_live_button",
                "config_sample_rate_combo",
                "config_apply_button",
            )
        }
        config_slot_checkbuttons = {
            slot: FakeControl() for slot in (1, 2)
        }
        config_gain_combos = {
            channel: FakeControl() for channel in range(8)
        }
        config_rail_checkbuttons = {
            rail: FakeControl() for rail in (
                "rail_3v3", "rail_9v", "rail_negative_5v")
        }
        app = SimpleNamespace(
            connected=True,
            ble_hello_only=False,
            pending_action=None,
            catalog_loading=True,
            recording_in_progress=False,
            live_active=False,
            current_config=SimpleNamespace(
                adc_channel_mask=0xFF,
                sd_card_state=1,
                card_slot_1=1,
                card_slot_2=0,
            ),
            config_dirty=False,
            config_slot_checkbuttons=config_slot_checkbuttons,
            config_gain_combos=config_gain_combos,
            config_rail_checkbuttons=config_rail_checkbuttons,
            pulse_slot_combo=FakeControl(),
            pulse_operation_combo=FakeControl(),
            pulse_button=FakeControl(),
            pulse_slot_var=FakeVariable("Slot 1"),
            _selected_recording=lambda: None,
            **controls,
        )

        GeophysHostApp._update_controls(app)

        self.assertEqual(controls["refresh_recordings_button"].state,
                         "disabled")
        self.assertEqual(controls["start_recording_button"].state, "normal")
        self.assertEqual(controls["start_live_button"].state, "normal")
        self.assertEqual(controls["config_sample_rate_combo"].state,
                         "readonly")
        self.assertTrue(all(
            control.state == "normal"
            for control in config_rail_checkbuttons.values()))
        self.assertEqual(app.pulse_button.state, "normal")
        self.assertEqual(controls["config_apply_button"].state, "disabled")

    def test_sd_fault_disables_recording_but_keeps_live_available(self) -> None:
        controls = {
            name: FakeControl()
            for name in (
                "refresh_recordings_button",
                "delete_recording_button",
                "recording_name_entry",
                "start_recording_button",
                "stop_recording_button",
                "channels_combo",
                "decimation_combo",
                "start_live_button",
                "stop_live_button",
                "config_sample_rate_combo",
                "config_apply_button",
            )
        }
        app = SimpleNamespace(
            connected=True,
            ble_hello_only=False,
            pending_action=None,
            catalog_loading=False,
            recording_in_progress=False,
            live_active=False,
            current_config=SimpleNamespace(
                adc_channel_mask=0x0F,
                sd_card_state=2,
                card_slot_1=1,
                card_slot_2=0,
            ),
            config_dirty=False,
            config_slot_checkbuttons={
                slot: FakeControl() for slot in (1, 2)
            },
            config_gain_combos={
                channel: FakeControl() for channel in range(8)
            },
            config_rail_checkbuttons={
                rail: FakeControl() for rail in (
                    "rail_3v3", "rail_9v", "rail_negative_5v")
            },
            pulse_slot_combo=FakeControl(),
            pulse_operation_combo=FakeControl(),
            pulse_button=FakeControl(),
            pulse_slot_var=FakeVariable("Slot 1"),
            _selected_recording=lambda: None,
            **controls,
        )

        GeophysHostApp._update_controls(app)

        self.assertEqual(controls["start_recording_button"].state,
                         "disabled")
        self.assertEqual(controls["start_live_button"].state, "normal")

    def test_worker_queue_accepts_recording_name_payload(self) -> None:
        worker = DeviceWorker("unused", 921_600)

        worker.submit("start_recording", name="field_test")

        self.assertEqual(
            worker._commands.get_nowait(),
            ("start_recording", {"name": "field_test"}),
        )

    def test_gui_submit_accepts_recording_name_payload(self) -> None:
        worker = FakeWorker()
        app = SimpleNamespace(
            worker=worker,
            connected=True,
            pending_action=None,
            _update_controls=lambda: None,
        )

        GeophysHostApp._submit(
            app, "start_recording", name="field_test")

        self.assertEqual(app.pending_action, "start_recording")
        self.assertEqual(
            worker.submissions,
            [("start_recording", {"name": "field_test"})],
        )

    def test_worker_starts_decimated_stream_after_config_read(self) -> None:
        config_payload = bytearray(40)
        config_payload[13] = 0xFF
        client = FakeClient([
            reply(REPLY_DEVICE_CONFIG, bytes(config_payload)),
            reply(REPLY_STREAMING_START_RESULT, b"\0\x05\xff\0"),
        ])
        worker = DeviceWorker("unused", 921_600)
        worker._client = client

        worker._start_stream(decimation=5, channel_mask=0xFF)

        config_event = worker.events.get_nowait()
        started_event = worker.events.get_nowait()
        self.assertEqual(config_event.name, "config")
        self.assertEqual(started_event.name, "stream_started")
        self.assertEqual(started_event.payload["generation"], 1)
        self.assertEqual(started_event.payload["result"].decimation, 5)
        self.assertTrue(worker._streaming)


if __name__ == "__main__":
    unittest.main()
