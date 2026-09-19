from __future__ import annotations

from collections import deque
from pathlib import Path
import struct
import sys
import time
from types import SimpleNamespace
import unittest

HOST_APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HOST_APP))

from geophys_host.gui import (  # noqa: E402
    DeviceWorker,
    GeophysHostApp,
    format_size,
    format_timestamp,
)
from geophys_host.protocol import (  # noqa: E402
    DIRECTION_TO_HOST,
    REPLY_DEVICE_CONFIG,
    REPLY_DEVICE_INFO,
    REPLY_RECORDING_INFO,
    REPLY_RECORDING_NUMBER,
    REPLY_STREAMING_START_RESULT,
    decode_command,
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

    def configure(self, **options) -> None:
        if "state" in options:
            self.state = options["state"]


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


class GuiSupportTests(unittest.TestCase):
    def test_display_formatters(self) -> None:
        self.assertEqual(format_size(512), "512 B")
        self.assertEqual(format_size(1536), "1.5 KiB")
        self.assertEqual(format_size(2 * 1024 * 1024), "2.0 MiB")
        self.assertEqual(format_timestamp(0), "—")

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

    def test_successful_request_postpones_keepalive(self) -> None:
        client = FakeClient([
            reply(REPLY_DEVICE_INFO, bytes(16)),
        ])
        worker = DeviceWorker("unused", 921_600)
        worker._client = client
        before = time.monotonic()

        worker._request(encode_hello(), REPLY_DEVICE_INFO)

        self.assertGreaterEqual(worker._next_keepalive, before + 1.0)

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
            )
        }
        app = SimpleNamespace(
            connected=True,
            pending_action=None,
            catalog_loading=True,
            recording_in_progress=False,
            live_active=False,
            _selected_recording=lambda: None,
            **controls,
        )

        GeophysHostApp._update_controls(app)

        self.assertEqual(controls["refresh_recordings_button"].state,
                         "disabled")
        self.assertEqual(controls["start_recording_button"].state, "normal")
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
