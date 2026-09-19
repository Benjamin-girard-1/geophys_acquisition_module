r"""Independent decoder for the protocol's 512-byte ``\DAT`` block."""

from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib

RECORD_MAGIC = b"\\DAT"
RECORD_SIZE = 512
PAYLOAD_OFFSET = 28
CRC_OFFSET = 508
VALID_CHANNEL_MASKS = (0x0F, 0xF0, 0xFF)
VALID_STATUS_CODES = (0, 1, 2, 3)


class AdcRecordError(ValueError):
    """A complete ADC record violates the V2 wire/storage contract."""


@dataclass(frozen=True)
class AdcRecord:
    channel_mask: int
    status: int
    packed_gain: int
    payload_number: int
    first_conversion_sequence: int
    first_monotonic_timestamp_100ns: int
    sample_period_100ns: int
    channel_indices: tuple[int, ...]
    conversions: tuple[tuple[int, ...], ...]


def _unpack_i24_le(data: bytes) -> int:
    value = int.from_bytes(data, "little")
    return value - (1 << 24) if value & (1 << 23) else value


def decode_adc_record(record: bytes) -> AdcRecord:
    if len(record) != RECORD_SIZE:
        raise AdcRecordError("ADC record must be exactly 512 bytes")
    if record[:4] != RECORD_MAGIC:
        raise AdcRecordError("invalid ADC record magic")
    expected_crc = struct.unpack_from("<I", record, CRC_OFFSET)[0]
    if zlib.crc32(record[:CRC_OFFSET]) & 0xFFFFFFFF != expected_crc:
        raise AdcRecordError("invalid ADC record CRC")

    channel_mask = record[4]
    status = record[5]
    sample_period = struct.unpack_from("<I", record, 24)[0]
    if channel_mask not in VALID_CHANNEL_MASKS:
        raise AdcRecordError("invalid ADC channel mask")
    if status not in VALID_STATUS_CODES:
        raise AdcRecordError("invalid ADC record status")
    if sample_period == 0:
        raise AdcRecordError("ADC sample period is zero")

    channel_indices = tuple(
        channel for channel in range(8)
        if channel_mask & (1 << channel))
    conversion_count = 480 // (3 * len(channel_indices))
    offset = PAYLOAD_OFFSET
    conversions: list[tuple[int, ...]] = []
    for _ in range(conversion_count):
        conversion = []
        for _channel in channel_indices:
            conversion.append(_unpack_i24_le(record[offset:offset + 3]))
            offset += 3
        conversions.append(tuple(conversion))
    if offset != CRC_OFFSET:
        raise AdcRecordError("ADC payload does not end at the CRC field")

    return AdcRecord(
        channel_mask=channel_mask,
        status=status,
        packed_gain=struct.unpack_from("<H", record, 6)[0],
        payload_number=struct.unpack_from("<I", record, 8)[0],
        first_conversion_sequence=struct.unpack_from("<I", record, 12)[0],
        first_monotonic_timestamp_100ns=struct.unpack_from(
            "<Q", record, 16)[0],
        sample_period_100ns=sample_period,
        channel_indices=channel_indices,
        conversions=tuple(conversions),
    )
