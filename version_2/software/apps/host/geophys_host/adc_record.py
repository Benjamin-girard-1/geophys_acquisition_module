r"""Compatibility imports for the shared 512-byte ``\DAT`` decoder."""

from geophys_data.adc_record import (
    AdcRecord,
    AdcRecordError,
    CRC_OFFSET,
    PAYLOAD_OFFSET,
    RECORD_MAGIC,
    RECORD_SIZE,
    VALID_CHANNEL_MASKS,
    VALID_STATUS_CODES,
    decode_adc_record,
)

__all__ = [
    "AdcRecord",
    "AdcRecordError",
    "CRC_OFFSET",
    "PAYLOAD_OFFSET",
    "RECORD_MAGIC",
    "RECORD_SIZE",
    "VALID_CHANNEL_MASKS",
    "VALID_STATUS_CODES",
    "decode_adc_record",
]
