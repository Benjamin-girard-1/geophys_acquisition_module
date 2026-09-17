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
COMMAND_DEVICE_GET_CONFIG = 0x0002
COMMAND_DEVICE_SET_CONFIG = 0x0003
REPLY_DEVICE_INFO = 0x00A1
REPLY_DEVICE_CONFIG = 0x00A2

RESULT_SUCCESS = 0x00
RESULT_UNSUPPORTED = 0x04

ADC_SAMPLE_RATE_500_SPS = 0x00
ADC_SAMPLE_RATE_1000_SPS = 0x01
ADC_SAMPLE_RATE_2000_SPS = 0x02
ADC_SAMPLE_RATE_4000_SPS = 0x03
ADC_SAMPLE_RATE_8000_SPS = 0x04
ADC_SAMPLE_RATE_16000_SPS = 0x05

VALID_ADC_CHANNEL_MASKS = (0x00, 0x0F, 0xF0, 0xFF)


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


@dataclass(frozen=True)
class DeviceConfigUpdate:
    adc_sample_rate: int
    adc_channel_mask: int
    adc_gain: int
    rail_3v3_enabled: bool = False
    rail_5v_enabled: bool = False
    rail_9v_enabled: bool = False
    rail_negative_5v_enabled: bool = False
    rail_18v_enabled: bool = False
    imu_averaging_time_ms: int = 0


@dataclass(frozen=True)
class DeviceConfig:
    result: int
    timestamp_100ns: int
    recording_in_progress: bool
    card_slot_1: int
    card_slot_2: int
    adc_sample_rate: int
    adc_channel_mask: int
    adc_gain: int
    adc_temperature_centi_c: int
    rail_3v3_enabled: bool
    rail_5v_enabled: bool
    rail_9v_enabled: bool
    rail_negative_5v_enabled: bool
    rail_18v_enabled: bool
    solar_present: bool
    usb_5v_present: bool
    gnss_state: int
    gnss_satellite_count: int
    imu_state: int
    imu_averaging_time_ms: int
    imu_roll_centi_degrees: int
    imu_pitch_centi_degrees: int
    imu_temperature_centi_c: int
    sd_card_state: int
    esp32_temperature_centi_c: int
    error_pending: bool


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


def encode_device_get_config() -> bytes:
    return encode_command(COMMAND_DEVICE_GET_CONFIG, DIRECTION_TO_DEVICE)


def encode_device_set_config(update: DeviceConfigUpdate) -> bytes:
    if not ADC_SAMPLE_RATE_500_SPS <= update.adc_sample_rate <= \
            ADC_SAMPLE_RATE_16000_SPS:
        raise ValueError("invalid ADC sample-rate code")
    if update.adc_channel_mask not in VALID_ADC_CHANNEL_MASKS:
        raise ValueError("invalid ADC channel mask")
    if not 0 <= update.adc_gain <= 0xFFFF:
        raise ValueError("ADC gain field is outside uint16")
    if not 0 <= update.imu_averaging_time_ms <= 0xFFFF:
        raise ValueError("IMU averaging time is outside uint16")

    payload = bytearray(39)
    payload[12] = update.adc_sample_rate
    payload[13] = update.adc_channel_mask
    struct.pack_into("<H", payload, 14, update.adc_gain)
    payload[18] = update.rail_3v3_enabled
    payload[19] = update.rail_5v_enabled
    payload[20] = update.rail_9v_enabled
    payload[21] = update.rail_negative_5v_enabled
    payload[22] = update.rail_18v_enabled
    struct.pack_into("<H", payload, 28, update.imu_averaging_time_ms)
    return encode_command(
        COMMAND_DEVICE_SET_CONFIG, DIRECTION_TO_DEVICE, bytes(payload))


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


def decode_device_config(command: Command) -> DeviceConfig:
    if command.command_id != REPLY_DEVICE_CONFIG:
        raise ProtocolError("reply is not DEVICE_CONFIG")
    if command.direction != DIRECTION_TO_HOST:
        raise ProtocolError("DEVICE_CONFIG has the wrong direction")
    if len(command.payload) != 40:
        raise ProtocolError("DEVICE_CONFIG payload must be 40 bytes")

    payload = command.payload
    boolean_offsets = (9, 18, 19, 20, 21, 22, 23, 24, 39)
    if any(payload[offset] not in (0, 1) for offset in boolean_offsets):
        raise ProtocolError("DEVICE_CONFIG contains an invalid boolean")
    if payload[10] > 2 or payload[11] > 2:
        raise ProtocolError("DEVICE_CONFIG contains an invalid card type")
    if payload[12] > ADC_SAMPLE_RATE_16000_SPS:
        raise ProtocolError("DEVICE_CONFIG contains an invalid sample rate")
    if payload[13] not in VALID_ADC_CHANNEL_MASKS:
        raise ProtocolError("DEVICE_CONFIG contains an invalid channel mask")
    if payload[25] > 3 or payload[27] > 2 or payload[36] > 2:
        raise ProtocolError("DEVICE_CONFIG contains an invalid subsystem state")

    return DeviceConfig(
        result=payload[0],
        timestamp_100ns=struct.unpack_from("<Q", payload, 1)[0],
        recording_in_progress=bool(payload[9]),
        card_slot_1=payload[10],
        card_slot_2=payload[11],
        adc_sample_rate=payload[12],
        adc_channel_mask=payload[13],
        adc_gain=struct.unpack_from("<H", payload, 14)[0],
        adc_temperature_centi_c=struct.unpack_from("<h", payload, 16)[0],
        rail_3v3_enabled=bool(payload[18]),
        rail_5v_enabled=bool(payload[19]),
        rail_9v_enabled=bool(payload[20]),
        rail_negative_5v_enabled=bool(payload[21]),
        rail_18v_enabled=bool(payload[22]),
        solar_present=bool(payload[23]),
        usb_5v_present=bool(payload[24]),
        gnss_state=payload[25],
        gnss_satellite_count=payload[26],
        imu_state=payload[27],
        imu_averaging_time_ms=struct.unpack_from("<H", payload, 28)[0],
        imu_roll_centi_degrees=struct.unpack_from("<h", payload, 30)[0],
        imu_pitch_centi_degrees=struct.unpack_from("<h", payload, 32)[0],
        imu_temperature_centi_c=struct.unpack_from("<h", payload, 34)[0],
        sd_card_state=payload[36],
        esp32_temperature_centi_c=struct.unpack_from("<h", payload, 37)[0],
        error_pending=bool(payload[39]),
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
