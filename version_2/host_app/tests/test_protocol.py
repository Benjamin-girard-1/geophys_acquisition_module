from __future__ import annotations

from pathlib import Path
import struct
import sys
import unittest
import zlib

HOST_APP = Path(__file__).resolve().parents[1]
REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(HOST_APP))

from geophys_host.protocol import (  # noqa: E402
    ADC_SAMPLE_RATE_2000_SPS,
    CommandStreamParser,
    DeviceConfigUpdate,
    DIRECTION_TO_HOST,
    ProtocolError,
    REPLY_RECORDING_DELETE_RESULT,
    REPLY_RECORDING_INFO,
    REPLY_RECORDING_NUMBER,
    REPLY_RECORDING_START_RESULT,
    REPLY_RECORDING_STOP_RESULT,
    REPLY_STREAMING_START_RESULT,
    REPLY_STREAMING_STOP_RESULT,
    REPLY_TEMP_RECORDING_READ,
    decode_command,
    decode_device_config,
    decode_device_info,
    decode_recording_delete_result,
    decode_recording_info,
    decode_recording_number,
    decode_recording_start_result,
    decode_recording_stop_result,
    decode_streaming_start_result,
    decode_streaming_stop_result,
    decode_temp_recording_read,
    encode_command,
    encode_device_get_config,
    encode_device_set_config,
    encode_hello,
    encode_recording_delete,
    encode_recording_get_info,
    encode_recording_get_number,
    encode_recording_start,
    encode_recording_stop,
    encode_streaming_start,
    encode_streaming_stop,
    encode_temp_recording_read,
)

VECTORS = REPOSITORY / "shared" / "protocol" / "test_vectors" / "command"


def load_hex(path: Path) -> bytes:
    return bytes.fromhex(path.read_text(encoding="ascii"))


class ProtocolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.hello = load_hex(VECTORS / "valid" / "hello_request.txt")
        self.info = load_hex(VECTORS / "valid" / "device_info_reply.txt")
        self.get_config = load_hex(
            VECTORS / "valid" / "device_get_config_request.txt")
        self.set_config = load_hex(
            VECTORS / "valid" / "device_set_config_request.txt")
        self.config = load_hex(
            VECTORS / "valid" / "device_config_reply.txt")
        self.temp_read_request = load_hex(
            VECTORS / "valid" / "temp_recording_read_request.txt")
        self.temp_read_reply = load_hex(
            VECTORS / "valid" / "temp_recording_read_reply.txt")
        self.bad_crc = load_hex(VECTORS / "invalid" / "hello_bad_crc.txt")

    def test_hello_matches_shared_vector(self) -> None:
        self.assertEqual(encode_hello(), self.hello)

    def test_device_info_vector(self) -> None:
        info = decode_device_info(decode_command(self.info))
        self.assertEqual(info.mac_address, bytes.fromhex("020000123456"))
        self.assertEqual(info.hardware_version, 2)
        self.assertEqual(info.hardware_revision, 1)
        self.assertEqual(info.firmware_version, 0x01020304)
        self.assertEqual(info.protocol_version, 1)

    def test_device_config_request_vectors(self) -> None:
        self.assertEqual(encode_device_get_config(), self.get_config)
        update = DeviceConfigUpdate(
            adc_sample_rate=ADC_SAMPLE_RATE_2000_SPS,
            adc_channel_mask=0x0F,
            adc_gain=0xE4E4,
        )
        self.assertEqual(encode_device_set_config(update), self.set_config)

    def test_device_config_reply_vector(self) -> None:
        config = decode_device_config(decode_command(self.config))
        self.assertEqual(config.result, 0)
        self.assertEqual(config.timestamp_100ns, 0x0102030405060708)
        self.assertEqual(config.card_slot_1, 1)
        self.assertEqual(config.card_slot_2, 2)
        self.assertEqual(config.adc_sample_rate, ADC_SAMPLE_RATE_2000_SPS)
        self.assertEqual(config.adc_channel_mask, 0x0F)
        self.assertEqual(config.adc_gain, 0xE4E4)
        self.assertEqual(config.adc_temperature_centi_c, -1234)
        self.assertEqual(config.imu_averaging_time_ms, 250)
        self.assertEqual(config.imu_roll_centi_degrees, -123)
        self.assertEqual(config.imu_pitch_centi_degrees, 456)
        self.assertEqual(config.imu_temperature_centi_c, 2500)
        self.assertEqual(config.esp32_temperature_centi_c, 4200)
        self.assertTrue(config.error_pending)

    def test_invalid_device_set_config_is_rejected_locally(self) -> None:
        with self.assertRaisesRegex(ValueError, "channel mask"):
            encode_device_set_config(DeviceConfigUpdate(1, 0x03, 0))

    def test_bad_crc_rejected(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "CRC"):
            decode_command(self.bad_crc)

    def test_noncanonical_fields_rejected(self) -> None:
        cases = []
        for offset, value in ((6, 2), (7, 1), (11, 49), (12, 1)):
            candidate = bytearray(self.hello)
            candidate[offset] = value
            struct.pack_into(
                "<I", candidate, 60,
                zlib.crc32(candidate[:60]) & 0xFFFFFFFF)
            cases.append(bytes(candidate))
        for candidate in cases:
            with self.assertRaises(ProtocolError):
                decode_command(candidate)

    def test_recording_request_and_reply_codecs(self) -> None:
        start = decode_command(encode_recording_start("Field_Test-01"))
        self.assertEqual(start.command_id, 0x0007)
        self.assertEqual(start.payload[:14], b"field_test-01\0")
        self.assertEqual(len(start.payload), 32)
        self.assertEqual(decode_command(encode_recording_stop()).command_id,
                         0x0008)
        self.assertEqual(
            decode_command(encode_recording_get_number()).command_id, 0x0009)
        self.assertEqual(
            decode_command(encode_recording_get_info(42)).payload,
            struct.pack("<H", 42))
        self.assertEqual(
            decode_command(encode_recording_delete("FIELD_TEST-01")).payload,
            start.payload)

        name = b"field_test-01\0" + bytes(18)
        start_reply = decode_recording_start_result(decode_command(
            encode_command(REPLY_RECORDING_START_RESULT, DIRECTION_TO_HOST,
                           bytes((0, 1)) + name)))
        self.assertTrue(start_reply.recording_in_progress)
        self.assertEqual(start_reply.name, "field_test-01")

        stop_reply = decode_recording_stop_result(decode_command(
            encode_command(REPLY_RECORDING_STOP_RESULT, DIRECTION_TO_HOST,
                           bytes((0,)) + name)))
        self.assertEqual(stop_reply.name, "field_test-01")

        number = decode_recording_number(decode_command(
            encode_command(REPLY_RECORDING_NUMBER, DIRECTION_TO_HOST,
                           b"\0\x03\0")))
        self.assertEqual(number.count, 3)

        info_payload = bytearray(48)
        struct.pack_into("<BHB", info_payload, 0, 0, 2, 1)
        info_payload[4:36] = name
        struct.pack_into("<QI", info_payload, 36, 0, 1024)
        info = decode_recording_info(decode_command(
            encode_command(REPLY_RECORDING_INFO, DIRECTION_TO_HOST,
                           bytes(info_payload))))
        self.assertEqual(info.index, 2)
        self.assertTrue(info.recording_in_progress)
        self.assertEqual(info.size_bytes, 1024)

        deleted = decode_recording_delete_result(decode_command(
            encode_command(REPLY_RECORDING_DELETE_RESULT, DIRECTION_TO_HOST,
                           bytes((0, 0)) + name)))
        self.assertEqual(deleted.name, "field_test-01")

    def test_streaming_request_and_reply_codecs(self) -> None:
        start_request = decode_command(encode_streaming_start(5, 0xFF))
        self.assertEqual(start_request.command_id, 0x0005)
        self.assertEqual(start_request.payload, b"\x05\xff")
        self.assertEqual(
            decode_command(encode_streaming_stop()).command_id, 0x0006)

        start = decode_streaming_start_result(decode_command(encode_command(
            REPLY_STREAMING_START_RESULT,
            DIRECTION_TO_HOST,
            b"\x00\x05\xff\x01",
        )))
        self.assertEqual(start.result, 0)
        self.assertEqual(start.decimation, 5)
        self.assertEqual(start.channel_mask, 0xFF)
        self.assertTrue(start.recording_in_progress)

        stop = decode_streaming_stop_result(decode_command(encode_command(
            REPLY_STREAMING_STOP_RESULT,
            DIRECTION_TO_HOST,
            b"\x00\x00",
        )))
        self.assertEqual(stop.result, 0)
        self.assertFalse(stop.recording_in_progress)

        for decimation in (1, 3, 8):
            with self.assertRaisesRegex(ValueError, "decimation"):
                encode_streaming_start(decimation, 0xFF)

    def test_invalid_recording_names_are_rejected(self) -> None:
        for name in ("", "has space", "dot.name", "x" * 32, "é"):
            with self.assertRaises(ValueError, msg=name):
                encode_recording_start(name)

    def test_temporary_recording_read_codec(self) -> None:
        encoded_request = encode_temp_recording_read("Field_01", 512)
        self.assertEqual(encoded_request, self.temp_read_request)
        request = decode_command(encoded_request)
        self.assertEqual(request.command_id, 0xF000)
        self.assertEqual(len(request.payload), 36)
        self.assertEqual(request.payload[:9], b"field_01\0")
        self.assertEqual(struct.unpack_from("<I", request.payload, 32)[0], 512)

        data = bytes(range(38))
        payload = bytearray(48)
        struct.pack_into("<BII B", payload, 0, 0, 1024, 512, len(data))
        payload[10:48] = data
        result = decode_temp_recording_read(decode_command(
            encode_command(REPLY_TEMP_RECORDING_READ, DIRECTION_TO_HOST,
                           bytes(payload))))
        self.assertEqual(result.file_size_bytes, 1024)
        self.assertEqual(result.offset_bytes, 512)
        self.assertEqual(result.data, data)
        self.assertEqual(
            decode_temp_recording_read(decode_command(self.temp_read_reply)),
            result)

        with self.assertRaisesRegex(ValueError, "uint32"):
            encode_temp_recording_read("field_01", 1 << 32)

    def test_every_two_fragment_split(self) -> None:
        for split in range(len(self.hello) + 1):
            parser = CommandStreamParser()
            commands = parser.feed(self.hello[:split])
            commands += parser.feed(self.hello[split:])
            self.assertEqual(len(commands), 1, split)

    def test_garbage_bad_crc_and_concatenation(self) -> None:
        parser = CommandStreamParser()
        stream = b"\x00\xff\\Cx\\" + self.bad_crc + self.hello + self.hello
        commands = []
        for byte in stream:
            commands.extend(parser.feed(bytes([byte])))
        self.assertEqual(len(commands), 2)


if __name__ == "__main__":
    unittest.main()
