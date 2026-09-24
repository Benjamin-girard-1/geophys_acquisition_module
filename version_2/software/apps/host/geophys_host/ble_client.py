"""Bluetooth Low Energy discovery and HELLO client."""

from __future__ import annotations

import asyncio
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from .protocol import (
    REPLY_DEVICE_INFO,
    DeviceInfo,
    decode_device_info,
    encode_hello,
)
from .stream_parser import CommandMessage, MixedStreamParser

BLE_DEVICE_NAME = "Geophys Acquisition"
BLE_SERVICE_UUID = "3809d383-3dc8-4280-b537-2bfeab1b8acb"
BLE_RX_UUID = "181481bc-cb34-43be-864f-54d9453afe22"
BLE_TX_UUID = "7201ba3a-7799-4409-9f59-bd3711612354"
BLE_WRITE_CHUNK_BYTES = 20


@dataclass(frozen=True)
class BleDevice:
    identifier: str
    name: str

    @property
    def display_name(self) -> str:
        return f"{self.name} — {self.identifier}"


async def discover_ble_devices(timeout_s: float = 3.0) -> list[BleDevice]:
    """Discover peripherals advertising the geophysical service."""
    try:
        from bleak import BleakScanner
    except ImportError as error:
        raise RuntimeError(
            "bleak is required for Bluetooth device access"
        ) from error

    found: dict[str, BleDevice] = {}

    def detected(device: Any, advertisement: Any) -> None:
        service_uuids = {
            value.lower() for value in (advertisement.service_uuids or [])
        }
        local_name = advertisement.local_name or device.name or ""
        if BLE_SERVICE_UUID not in service_uuids and \
                not local_name.startswith("Geophys"):
            return
        name = local_name or BLE_DEVICE_NAME
        found[device.address] = BleDevice(device.address, name)

    scanner = BleakScanner(detection_callback=detected)
    await scanner.start()
    try:
        await asyncio.sleep(timeout_s)
    finally:
        await scanner.stop()
    return sorted(found.values(), key=lambda device: device.display_name.lower())


class BleHelloClient:
    """Persistent BLE connection exposing only the HELLO exchange."""

    def __init__(self, identifier: str,
                 client_factory: Callable[..., Any] | None = None) -> None:
        self.identifier = identifier
        self._client_factory = client_factory
        self._client = None
        self._parser = MixedStreamParser()
        self._messages: asyncio.Queue[CommandMessage] = asyncio.Queue()

    @property
    def is_connected(self) -> bool:
        return bool(self._client is not None and self._client.is_connected)

    def _handle_notification(self, _sender: Any, data: bytearray) -> None:
        for message in self._parser.feed(bytes(data)):
            if isinstance(message, CommandMessage):
                self._messages.put_nowait(message)

    async def connect(self) -> None:
        if self._client is not None:
            raise RuntimeError("Bluetooth client is already open")
        if self._client_factory is None:
            try:
                from bleak import BleakClient
            except ImportError as error:
                raise RuntimeError(
                    "bleak is required for Bluetooth device access"
                ) from error
            factory = BleakClient
        else:
            factory = self._client_factory

        client = factory(self.identifier)
        try:
            await client.connect()
            await client.start_notify(BLE_TX_UUID, self._handle_notification)
        except Exception:
            if client.is_connected:
                await client.disconnect()
            raise
        self._client = client

    async def disconnect(self) -> None:
        client = self._client
        self._client = None
        if client is not None and client.is_connected:
            await client.disconnect()

    async def hello(self, timeout_s: float = 3.0) -> DeviceInfo:
        if not self.is_connected:
            raise RuntimeError("Bluetooth connection is not open")
        frame = encode_hello()
        for offset in range(0, len(frame), BLE_WRITE_CHUNK_BYTES):
            await self._client.write_gatt_char(
                BLE_RX_UUID,
                frame[offset:offset + BLE_WRITE_CHUNK_BYTES],
                response=True,
            )

        deadline = asyncio.get_running_loop().time() + timeout_s
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0.0:
                raise TimeoutError("Bluetooth DEVICE_INFO was not received")
            try:
                message = await asyncio.wait_for(
                    self._messages.get(), timeout=remaining)
            except asyncio.TimeoutError as error:
                raise TimeoutError(
                    "Bluetooth DEVICE_INFO was not received"
                ) from error
            if message.command.command_id == REPLY_DEVICE_INFO:
                return decode_device_info(message.command)
