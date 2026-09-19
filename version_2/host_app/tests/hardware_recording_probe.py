"""End-to-end SD recording command probe for a connected V2 Rev-1 device."""

from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path
import sys
import time

import serial

HOST_APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HOST_APP))

from geophys_host.protocol import (  # noqa: E402
    CommandStreamParser,
    RESULT_SUCCESS,
    decode_device_config,
    decode_device_info,
    decode_recording_delete_result,
    decode_recording_info,
    decode_recording_number,
    decode_recording_start_result,
    decode_recording_stop_result,
    decode_temp_recording_read,
    encode_device_get_config,
    encode_hello,
    encode_recording_delete,
    encode_recording_get_info,
    encode_recording_get_number,
    encode_recording_start,
    encode_recording_stop,
    encode_temp_recording_read,
)
from geophys_host.adc_record import decode_adc_record  # noqa: E402


def request(connection: serial.Serial, frame: bytes, timeout_s: float = 12.0):
    parser = CommandStreamParser()
    connection.reset_input_buffer()
    connection.write(frame)
    connection.flush()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        commands = parser.feed(connection.read(256))
        if commands:
            assert len(commands) == 1, "received multiple command replies"
            return commands[0]
    raise TimeoutError("device command reply was not received")


def result_must_succeed(label: str, result: int) -> None:
    assert result == RESULT_SUCCESS, f"{label} failed with result 0x{result:02x}"


def download_recording(
        connection: serial.Serial, name: str, expected_size: int) -> bytes:
    contents = bytearray()
    while len(contents) < expected_size:
        chunk = decode_temp_recording_read(request(
            connection, encode_temp_recording_read(name, len(contents))))
        result_must_succeed("TEMP_RECORDING_READ", chunk.result)
        assert chunk.file_size_bytes == expected_size, "file size changed"
        assert chunk.offset_bytes == len(contents), "chunk offset mismatch"
        assert chunk.data, "unexpected end of file"
        contents.extend(chunk.data)

    eof = decode_temp_recording_read(request(
        connection, encode_temp_recording_read(name, expected_size)))
    result_must_succeed("TEMP_RECORDING_READ at EOF", eof.result)
    assert eof.file_size_bytes == expected_size
    assert eof.offset_bytes == expected_size
    assert eof.data == b""
    return bytes(contents)


def validate_recording(
        contents: bytes) -> tuple[int, set[int], int, int, int]:
    assert contents and len(contents) % 512 == 0
    records = [
        decode_adc_record(contents[offset:offset + 512])
        for offset in range(0, len(contents), 512)
    ]
    missing_conversions = 0
    for previous, current in zip(records, records[1:]):
        assert current.payload_number == (previous.payload_number + 1) & 0xFFFFFFFF
        expected_sequence = (
            previous.first_conversion_sequence + len(previous.conversions)
        ) & 0xFFFFFFFF
        if current.first_conversion_sequence != expected_sequence:
            missing_conversions += (
                current.first_conversion_sequence - expected_sequence
            ) & 0xFFFFFFFF
            assert current.status in (1, 3), (
                "source sequence gap is not marked critical/timing-error"
            )
        assert current.first_monotonic_timestamp_100ns > \
            previous.first_monotonic_timestamp_100ns
    statuses = {record.status for record in records}
    minimum_sample = min(
        sample for record in records for conversion in record.conversions
        for sample in conversion)
    maximum_sample = max(
        sample for record in records for conversion in record.conversions
        for sample in conversion)
    return (
        len(records), statuses, minimum_sample, maximum_sample,
        missing_conversions,
    )


def main() -> int:
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("port")
    argument_parser.add_argument("--baud", type=int, default=921_600)
    argument_parser.add_argument("--duration", type=float, default=2.0)
    argument_parser.add_argument("--name")
    argument_parser.add_argument("--keep", action="store_true")
    argument_parser.add_argument("--output", type=Path)
    arguments = argument_parser.parse_args()
    if arguments.duration <= 0.0:
        argument_parser.error("--duration must be positive")

    name = arguments.name or (
        "codex_" + datetime.now().strftime("%m%d%H%M%S"))
    recording_started = False
    recording_created = False
    final_size = 0
    validation = None

    with serial.Serial(
        arguments.port,
        arguments.baud,
        timeout=0.02,
        write_timeout=2.0,
    ) as connection:
        time.sleep(0.2)
        info = decode_device_info(request(connection, encode_hello()))
        result_must_succeed("HELLO", info.result)

        config = decode_device_config(
            request(connection, encode_device_get_config()))
        result_must_succeed("DEVICE_GET_CONFIG", config.result)
        assert not config.recording_in_progress, (
            "device already has a recording in progress"
        )
        # RECORDING_GET_NUMBER refreshes the catalog and gives a previously
        # faulted/absent card a chance to mount again. Do not reject the
        # cached DEVICE_CONFIG state before exercising that recovery path.
        initial_number = decode_recording_number(
            request(connection, encode_recording_get_number()))
        result_must_succeed("RECORDING_GET_NUMBER", initial_number.result)

        try:
            start = decode_recording_start_result(
                request(connection, encode_recording_start(name)))
            if start.result != RESULT_SUCCESS:
                rolled_back_number = decode_recording_number(
                    request(connection, encode_recording_get_number()))
                result_must_succeed(
                    "RECORDING_GET_NUMBER", rolled_back_number.result)
                assert rolled_back_number.count == initial_number.count, (
                    "failed start left a recording file in the catalog"
                )
                raise AssertionError(
                    "RECORDING_START failed and rolled back cleanly: " +
                    repr(start)
                )
            assert start.recording_in_progress
            assert start.name == name
            recording_started = True
            recording_created = True

            deadline = time.monotonic() + arguments.duration
            while time.monotonic() < deadline:
                time.sleep(min(0.5, max(0.0, deadline - time.monotonic())))
                live_config = decode_device_config(
                    request(connection, encode_device_get_config()))
                result_must_succeed("DEVICE_GET_CONFIG", live_config.result)
                assert live_config.recording_in_progress, (
                    "recording stopped unexpectedly"
                )

            stop = decode_recording_stop_result(
                request(connection, encode_recording_stop()))
            recording_started = False
            result_must_succeed("RECORDING_STOP", stop.result)
            assert stop.name == name

            final_number = decode_recording_number(
                request(connection, encode_recording_get_number()))
            result_must_succeed("RECORDING_GET_NUMBER", final_number.result)
            assert final_number.count == initial_number.count + 1

            matching = []
            for index in range(final_number.count):
                recording = decode_recording_info(
                    request(connection, encode_recording_get_info(index)))
                result_must_succeed("RECORDING_GET_INFO", recording.result)
                if recording.name == name:
                    matching.append(recording)
            assert len(matching) == 1, "new recording was not cataloged once"
            recorded = matching[0]
            assert not recorded.recording_in_progress
            assert recorded.size_bytes > 0, "recording contains no DAT blocks"
            assert recorded.size_bytes % 512 == 0, (
                "recording size is not a multiple of 512 bytes"
            )
            final_size = recorded.size_bytes

            contents = download_recording(connection, name, final_size)
            if arguments.output is not None:
                arguments.output.write_bytes(contents)
            validation = validate_recording(contents)

            if not arguments.keep:
                deleted = decode_recording_delete_result(
                    request(connection, encode_recording_delete(name)))
                result_must_succeed("RECORDING_DELETE", deleted.result)
                assert not deleted.recording_in_progress
                assert deleted.name == name
                recording_created = False
                restored_number = decode_recording_number(
                    request(connection, encode_recording_get_number()))
                result_must_succeed(
                    "RECORDING_GET_NUMBER", restored_number.result)
                assert restored_number.count == initial_number.count
        finally:
            if recording_started:
                try:
                    request(connection, encode_recording_stop())
                except Exception:
                    pass
            if recording_created and not arguments.keep:
                try:
                    request(connection, encode_recording_delete(name))
                except Exception:
                    pass

    action = "kept" if arguments.keep else "created and deleted"
    print(
        "hardware recording test passed: "
        f"name={name} size={final_size} bytes ({final_size // 512} blocks), "
        f"statuses={sorted(validation[1])} "
        f"sample_range=[{validation[2]}, {validation[3]}], "
        f"missing_conversions={validation[4]}, "
        f"test file {action}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
