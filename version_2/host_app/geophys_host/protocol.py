"""Independent host codec for fixed 64-byte V2 command messages."""

from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib

COMMAND_MAGIC = b"\\CMD"
COMMAND_SIZE = 64
PAYLOAD_SIZE = 48
CRC_OFFSET = 60

DIRECTION_TO_DEVICE = 0
DIRECTION_TO_HOST = 1

COMMAND_HELLO = 0x0001
REPLY_DEVICE_INFO = 0x00A1


class ProtocolError(ValueError):
    """A complete command candidate violates the V2 wire contract."""


@dataclass(frozen=True)
class Command:
    command_id: int
    direction: int
    payload: bytes


@dataclass(frozen=True)
class DeviceInfo:
    result: int
    mac_address: bytes
    hardware_version: int
    hardware_revision: int
    firmware_version: int
    protocol_version: int


def _crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def encode_command(command_id: int, direction: int, payload: bytes = b"") -> bytes:
    if not 0 <= command_id <= 0xFFFF:
        raise ValueError("command ID is outside uint16")
    if direction not in (DIRECTION_TO_DEVICE, DIRECTION_TO_HOST):
        raise ValueError("invalid command direction")
    if len(payload) > PAYLOAD_SIZE:
        raise ValueError("payload exceeds 48 bytes")

    frame = bytearray(COMMAND_SIZE)
    frame[0:4] = COMMAND_MAGIC
    struct.pack_into("<H", frame, 4, command_id)
    frame[6] = direction
    frame[11] = len(payload)
    frame[12 : 12 + len(payload)] = payload
    struct.pack_into("<I", frame, CRC_OFFSET, _crc32(frame[:CRC_OFFSET]))
    return bytes(frame)


def decode_command(frame: bytes) -> Command:
    if len(frame) != COMMAND_SIZE:
        raise ProtocolError("command must be exactly 64 bytes")
    if frame[:4] != COMMAND_MAGIC:
        raise ProtocolError("invalid command magic")
    expected_crc = struct.unpack_from("<I", frame, CRC_OFFSET)[0]
    if _crc32(frame[:CRC_OFFSET]) != expected_crc:
        raise ProtocolError("invalid command CRC")
    if frame[6] not in (DIRECTION_TO_DEVICE, DIRECTION_TO_HOST):
        raise ProtocolError("invalid command direction")
    if any(frame[7:11]):
        raise ProtocolError("reserved bytes are nonzero")
    payload_length = frame[11]
    if payload_length > PAYLOAD_SIZE:
        raise ProtocolError("payload length exceeds 48 bytes")
    if any(frame[12 + payload_length : CRC_OFFSET]):
        raise ProtocolError("payload padding is nonzero")
    return Command(
        command_id=struct.unpack_from("<H", frame, 4)[0],
        direction=frame[6],
        payload=bytes(frame[12 : 12 + payload_length]),
    )


def encode_hello() -> bytes:
    return encode_command(COMMAND_HELLO, DIRECTION_TO_DEVICE)


def decode_device_info(command: Command) -> DeviceInfo:
    if command.command_id != REPLY_DEVICE_INFO:
        raise ProtocolError("reply is not DEVICE_INFO")
    if command.direction != DIRECTION_TO_HOST:
        raise ProtocolError("DEVICE_INFO has the wrong direction")
    if len(command.payload) != 16:
        raise ProtocolError("DEVICE_INFO payload must be 16 bytes")
    return DeviceInfo(
        result=command.payload[0],
        mac_address=command.payload[1:7],
        hardware_version=struct.unpack_from("<H", command.payload, 7)[0],
        hardware_revision=struct.unpack_from("<H", command.payload, 9)[0],
        firmware_version=struct.unpack_from("<I", command.payload, 11)[0],
        protocol_version=command.payload[15],
    )


class CommandStreamParser:
    """Recover complete valid commands from fragmented input and boot garbage."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[Command]:
        self._buffer.extend(data)
        commands: list[Command] = []
        while True:
            start = self._buffer.find(COMMAND_MAGIC)
            if start < 0:
                self._buffer[:] = self._buffer[-3:]
                break
            if start:
                del self._buffer[:start]
            if len(self._buffer) < COMMAND_SIZE:
                break
            candidate = bytes(self._buffer[:COMMAND_SIZE])
            try:
                command = decode_command(candidate)
            except ProtocolError:
                del self._buffer[0]
                continue
            del self._buffer[:COMMAND_SIZE]
            commands.append(command)
        return commands
