from __future__ import annotations

import asyncio
from pathlib import Path
import struct
import sys
import unittest

HOST_APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HOST_APP))

from geophys_host.ble_client import (  # noqa: E402
    BLE_RX_UUID,
    BLE_TX_UUID,
    BLE_WRITE_CHUNK_BYTES,
    BleHelloClient,
)
from geophys_host.protocol import (  # noqa: E402
    DIRECTION_TO_HOST,
    REPLY_DEVICE_INFO,
    encode_command,
)


class FakeBleakClient:
    def __init__(self, identifier: str) -> None:
        self.identifier = identifier
        self.is_connected = False
        self.notification = None
        self.writes: list[bytes] = []

    async def connect(self) -> None:
        self.is_connected = True

    async def disconnect(self) -> None:
        self.is_connected = False

    async def start_notify(self, uuid: str, callback) -> None:
        if uuid != BLE_TX_UUID:
            raise AssertionError("unexpected notification UUID")
        self.notification = callback

    async def write_gatt_char(self, uuid: str, data: bytes,
                              response: bool) -> None:
        if uuid != BLE_RX_UUID or not response:
            raise AssertionError("unexpected BLE write")
        self.writes.append(bytes(data))
        if sum(map(len, self.writes)) != 64:
            return
        payload = bytearray(16)
        payload[0] = 0
        payload[1:7] = bytes.fromhex("102030405060")
        struct.pack_into("<HHI", payload, 7, 2, 1, 0x12345678)
        payload[15] = 1
        reply = encode_command(
            REPLY_DEVICE_INFO, DIRECTION_TO_HOST, bytes(payload))
        self.notification(None, bytearray(reply[:17]))
        self.notification(None, bytearray(reply[17:]))


class BleHelloClientTests(unittest.IsolatedAsyncioTestCase):
    async def test_fragmented_hello_and_reply(self) -> None:
        fake = FakeBleakClient("device-id")
        client = BleHelloClient("device-id", client_factory=lambda _: fake)

        await client.connect()
        info = await client.hello(timeout_s=0.2)
        await client.disconnect()

        self.assertFalse(fake.is_connected)
        self.assertEqual(b"".join(fake.writes)[:4], b"\\CMD")
        self.assertTrue(all(
            len(chunk) <= BLE_WRITE_CHUNK_BYTES for chunk in fake.writes))
        self.assertEqual(info.mac_address, bytes.fromhex("102030405060"))
        self.assertEqual(info.hardware_version, 2)
        self.assertEqual(info.hardware_revision, 1)
        self.assertEqual(info.firmware_version, 0x12345678)

    async def test_hello_requires_connection(self) -> None:
        client = BleHelloClient("device-id", client_factory=FakeBleakClient)

        with self.assertRaisesRegex(RuntimeError, "not open"):
            await client.hello(timeout_s=0.01)


if __name__ == "__main__":
    unittest.main()
