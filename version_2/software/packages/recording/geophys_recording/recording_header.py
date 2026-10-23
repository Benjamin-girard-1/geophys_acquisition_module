"""Decoder for the printable JSON descriptor at the start of a recording."""

from __future__ import annotations

from dataclasses import dataclass
import json
import re
import zlib

HEADER_MAGIC = b"{"
HEADER_FORMAT_VERSION = 2
STATIC_HEADER_SIZE = 4_096
TOTAL_HEADER_SIZE = 32_768
DATA_OFFSET = TOTAL_HEADER_SIZE
CHECKPOINT_OFFSET = STATIC_HEADER_SIZE
CHECKPOINT_SIZE = 32
CHECKPOINT_CAPACITY = 896
CHECKPOINT_INTERVAL_MS = 10_000
ADC_CHANNEL_COUNT = 8
_CRC_FIELD = re.compile(rb'"header_crc32": "([0-9A-F]{8})"')
_DEVICE_ID = re.compile(r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}\Z")
_RECORDING_NAME = re.compile(r"[a-z0-9_-]{1,31}\Z")


class RecordingHeaderError(ValueError):
    """The recording descriptor violates the storage contract."""


@dataclass(frozen=True)
class RecordingHeader:
    recording_name: str
    device_id: bytes
    hardware_version: int
    hardware_revision: int
    firmware_version: int
    protocol_version: int
    start_monotonic_timestamp_100ns: int
    start_unix_timestamp_us: int
    adc_sample_rate_sps: int
    adc_sample_period_100ns: int
    adc_channel_mask: int
    adc_channel_count: int
    adc_gains: tuple[int, ...]
    packed_gain: int
    card_types: tuple[int, int]
    gnss_state: int
    imu_state: int
    imu_averaging_time_ms: int
    power_rail_flags: int
    channel_slots: tuple[int, ...]
    channel_inputs: tuple[int, ...]
    checkpoint_interval_ms: int
    checkpoint_capacity: int
    tools_url: str


def _unique_fields(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise RecordingHeaderError(f"duplicate JSON field {key}")
        result[key] = value
    return result


def _integer(fields: dict[str, object], key: str, maximum: int) -> int:
    value = fields.get(key)
    if type(value) is not int or not 0 <= value <= maximum:
        raise RecordingHeaderError(f"invalid {key}")
    return value


def _numbers(fields: dict[str, object], key: str,
             length: int, maximum: int) -> tuple[int, ...]:
    values = fields.get(key)
    if not isinstance(values, list) or len(values) != length or any(
        type(value) is not int or not 0 <= value <= maximum
        for value in values
    ):
        raise RecordingHeaderError(f"invalid {key}")
    return tuple(values)


def decode_recording_header(header: bytes) -> RecordingHeader:
    """Validate and decode exactly the first 4096 padded ASCII JSON bytes."""
    if len(header) != STATIC_HEADER_SIZE:
        raise RecordingHeaderError(
            "recording descriptor must be exactly 4096 bytes"
        )
    if not header.startswith(HEADER_MAGIC):
        raise RecordingHeaderError("invalid recording-header prefix")
    if any(byte != 10 and not 32 <= byte <= 126 for byte in header):
        raise RecordingHeaderError("recording descriptor is not printable ASCII")
    crc_match = _CRC_FIELD.search(header)
    if crc_match is None:
        raise RecordingHeaderError("missing recording-header CRC")
    stored_crc = int(crc_match.group(1), 16)
    normalized = bytearray(header)
    normalized[crc_match.start(1):crc_match.end(1)] = b"00000000"
    if zlib.crc32(normalized) & 0xFFFFFFFF != stored_crc:
        raise RecordingHeaderError("invalid recording-header CRC")

    try:
        fields = json.loads(header.decode("ascii"), object_pairs_hook=_unique_fields)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise RecordingHeaderError("invalid recording-header JSON") from error
    if not isinstance(fields, dict) or fields.get("format") != "GEO_RECORDING":
        raise RecordingHeaderError("invalid recording format")
    if _integer(fields, "format_version", 255) != HEADER_FORMAT_VERSION:
        raise RecordingHeaderError("unsupported recording-header version")
    if _integer(fields, "descriptor_size_bytes", 32_768) != STATIC_HEADER_SIZE or \
            _integer(fields, "checkpoint_offset_bytes", 32_768) != \
            CHECKPOINT_OFFSET or \
            _integer(fields, "checkpoint_entry_size_bytes", 512) != \
            CHECKPOINT_SIZE or \
            _integer(fields, "checkpoint_capacity", 1_024) != \
            CHECKPOINT_CAPACITY or \
            _integer(fields, "checkpoint_interval_ms", 2**32 - 1) != \
            CHECKPOINT_INTERVAL_MS or \
            _integer(fields, "data_offset_bytes", 2**32 - 1) != DATA_OFFSET or \
            _integer(fields, "record_size_bytes", 2**16 - 1) != 512:
        raise RecordingHeaderError("invalid recording layout")
    if fields.get("sample_encoding") != "signed24_le":
        raise RecordingHeaderError("invalid sample encoding")

    name = fields.get("recording_name")
    if not isinstance(name, str) or not _RECORDING_NAME.fullmatch(name):
        raise RecordingHeaderError("invalid recording name")
    device_id = fields.get("device_id")
    if not isinstance(device_id, str) or not _DEVICE_ID.fullmatch(device_id):
        raise RecordingHeaderError("invalid device ID")
    tools_url = fields.get("tools_url")
    if not isinstance(tools_url, str) or not tools_url.startswith("https://"):
        raise RecordingHeaderError("invalid tools URL")

    gains = _numbers(fields, "adc_gains", ADC_CHANNEL_COUNT, 8)
    if any(gain not in (1, 2, 4, 8) for gain in gains):
        raise RecordingHeaderError("invalid per-channel ADC gain")
    channel_mask = _integer(fields, "adc_channel_mask", 255)
    if channel_mask not in (0x0F, 0xF0, 0xFF):
        raise RecordingHeaderError("invalid ADC channel mask")
    channel_count = _integer(fields, "adc_channel_count", 8)
    if channel_count != channel_mask.bit_count():
        raise RecordingHeaderError("ADC channel count does not match mask")
    gain_codes = {1: 0, 2: 1, 4: 2, 8: 3}
    packed_gain = _integer(fields, "packed_gain", 2**16 - 1)
    if packed_gain != sum(
        gain_codes[gain] << (2 * channel)
        for channel, gain in enumerate(gains)
    ):
        raise RecordingHeaderError("packed and per-channel gains disagree")
    sample_rate = _integer(fields, "adc_sample_rate_sps", 2**32 - 1)
    sample_period = _integer(fields, "adc_sample_period_100ns", 2**32 - 1)
    if sample_rate == 0 or 10_000_000 % sample_rate != 0 or \
            sample_period != 10_000_000 // sample_rate:
        raise RecordingHeaderError("invalid ADC sample rate or period")
    slots = _numbers(fields, "channel_slots", 8, 2)
    inputs = _numbers(fields, "channel_inputs", 8, 3)
    if slots != (1, 1, 1, 1, 2, 2, 2, 2) or \
            inputs != (0, 1, 2, 3, 0, 1, 2, 3):
        raise RecordingHeaderError("invalid channel mapping")
    cards = _numbers(fields, "card_types", 2, 255)

    return RecordingHeader(
        recording_name=name,
        device_id=bytes.fromhex(device_id.replace(":", "")),
        hardware_version=_integer(fields, "hardware_version", 2**16 - 1),
        hardware_revision=_integer(fields, "hardware_revision", 2**16 - 1),
        firmware_version=_integer(fields, "firmware_version", 2**32 - 1),
        protocol_version=_integer(fields, "protocol_version", 255),
        start_monotonic_timestamp_100ns=_integer(
            fields, "start_monotonic_timestamp_100ns", 2**64 - 1
        ),
        start_unix_timestamp_us=_integer(
            fields, "start_unix_timestamp_us", 2**64 - 1
        ),
        adc_sample_rate_sps=sample_rate,
        adc_sample_period_100ns=sample_period,
        adc_channel_mask=channel_mask,
        adc_channel_count=channel_count,
        adc_gains=gains,
        packed_gain=packed_gain,
        card_types=(cards[0], cards[1]),
        gnss_state=_integer(fields, "gnss_state", 255),
        imu_state=_integer(fields, "imu_state", 255),
        imu_averaging_time_ms=_integer(
            fields, "imu_averaging_time_ms", 2**16 - 1
        ),
        power_rail_flags=_integer(fields, "power_rail_flags", 2**16 - 1),
        channel_slots=slots,
        channel_inputs=inputs,
        checkpoint_interval_ms=CHECKPOINT_INTERVAL_MS,
        checkpoint_capacity=CHECKPOINT_CAPACITY,
        tools_url=tools_url,
    )
