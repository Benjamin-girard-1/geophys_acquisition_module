"""Optional HELLO and DEVICE_CONFIG smoke test on a V2 Rev-1 device."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time

import serial

HOST_APP = Path(__file__).resolve().parents[1]
REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(HOST_APP))

from geophys_host.protocol import (  # noqa: E402
    ADC_SAMPLE_RATE_2000_SPS,
    CommandStreamParser,
    DeviceConfigUpdate,
    REPLY_DEVICE_CONFIG,
    REPLY_DEVICE_INFO,
    RESULT_UNSUPPORTED,
    decode_device_config,
    decode_device_info,
    encode_device_get_config,
    encode_device_set_config,
    encode_hello,
)


def read_commands(connection: serial.Serial, timeout_s: float):
    parser = CommandStreamParser()
    commands = []
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        commands.extend(parser.feed(connection.read(256)))
    return commands


def main() -> int:
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("port")
    argument_parser.add_argument("--baud", type=int, default=921_600)
    arguments = argument_parser.parse_args()

    bad_crc_path = (
        REPOSITORY / "shared" / "protocol" / "test_vectors" /
        "command" / "invalid" / "hello_bad_crc.txt"
    )
    bad_crc = bytes.fromhex(bad_crc_path.read_text(encoding="ascii"))
    hello = encode_hello()

    with serial.Serial(
        arguments.port,
        arguments.baud,
        timeout=0.02,
        write_timeout=1.0,
    ) as connection:
        time.sleep(0.1)
        connection.reset_input_buffer()

        connection.write(bad_crc)
        connection.flush()
        assert read_commands(connection, 0.3) == [], (
            "CRC-invalid HELLO unexpectedly produced a valid command reply"
        )

        for byte in hello:
            connection.write(bytes([byte]))
            connection.flush()
            time.sleep(0.001)
        fragmented_replies = read_commands(connection, 0.5)
        assert len(fragmented_replies) == 1
        info = decode_device_info(fragmented_replies[0])

        connection.write(hello + hello)
        connection.flush()
        concatenated_replies = read_commands(connection, 0.5)
        assert len(concatenated_replies) == 2
        assert all(
            reply.command_id == REPLY_DEVICE_INFO
            for reply in concatenated_replies
        )

        connection.write(encode_device_get_config())
        connection.flush()
        config_replies = read_commands(connection, 0.5)
        assert len(config_replies) == 1
        assert config_replies[0].command_id == REPLY_DEVICE_CONFIG
        original_config = decode_device_config(config_replies[0])
        assert original_config.result == 0

        changed_update = DeviceConfigUpdate(
            adc_sample_rate=ADC_SAMPLE_RATE_2000_SPS,
            adc_channel_mask=0x0F,
            adc_gain=0xE4E4,
            rail_3v3_enabled=original_config.rail_3v3_enabled,
            rail_5v_enabled=original_config.rail_5v_enabled,
            rail_9v_enabled=original_config.rail_9v_enabled,
            rail_negative_5v_enabled=(
                original_config.rail_negative_5v_enabled),
            rail_18v_enabled=original_config.rail_18v_enabled,
            imu_averaging_time_ms=original_config.imu_averaging_time_ms,
        )

        unsupported_update = DeviceConfigUpdate(
            adc_sample_rate=changed_update.adc_sample_rate,
            adc_channel_mask=changed_update.adc_channel_mask,
            adc_gain=changed_update.adc_gain,
            rail_3v3_enabled=original_config.rail_3v3_enabled,
            rail_5v_enabled=original_config.rail_5v_enabled,
            rail_9v_enabled=original_config.rail_9v_enabled,
            rail_negative_5v_enabled=(
                original_config.rail_negative_5v_enabled),
            rail_18v_enabled=not original_config.rail_18v_enabled,
            imu_averaging_time_ms=original_config.imu_averaging_time_ms,
        )
        connection.write(encode_device_set_config(unsupported_update))
        connection.flush()
        unsupported_replies = read_commands(connection, 0.5)
        assert len(unsupported_replies) == 1
        unsupported_config = decode_device_config(unsupported_replies[0])
        assert unsupported_config.result == RESULT_UNSUPPORTED
        assert unsupported_config.adc_sample_rate == original_config.adc_sample_rate
        assert unsupported_config.adc_channel_mask == original_config.adc_channel_mask
        assert unsupported_config.adc_gain == original_config.adc_gain
        assert unsupported_config.rail_18v_enabled == (
            original_config.rail_18v_enabled)

        connection.write(encode_device_set_config(changed_update))
        connection.flush()
        changed_replies = read_commands(connection, 0.5)
        assert len(changed_replies) == 1
        changed_config = decode_device_config(changed_replies[0])
        assert changed_config.result == 0
        assert changed_config.adc_sample_rate == ADC_SAMPLE_RATE_2000_SPS
        assert changed_config.adc_channel_mask == 0x0F
        assert changed_config.adc_gain == 0xE4E4

        restore_update = DeviceConfigUpdate(
            adc_sample_rate=original_config.adc_sample_rate,
            adc_channel_mask=original_config.adc_channel_mask,
            adc_gain=original_config.adc_gain,
            rail_3v3_enabled=original_config.rail_3v3_enabled,
            rail_5v_enabled=original_config.rail_5v_enabled,
            rail_9v_enabled=original_config.rail_9v_enabled,
            rail_negative_5v_enabled=(
                original_config.rail_negative_5v_enabled),
            rail_18v_enabled=original_config.rail_18v_enabled,
            imu_averaging_time_ms=original_config.imu_averaging_time_ms,
        )
        connection.write(encode_device_set_config(restore_update))
        connection.flush()
        restored_replies = read_commands(connection, 0.5)
        assert len(restored_replies) == 1
        restored_config = decode_device_config(restored_replies[0])
        assert restored_config.result == 0
        assert restored_config.adc_sample_rate == original_config.adc_sample_rate
        assert restored_config.adc_channel_mask == original_config.adc_channel_mask
        assert restored_config.adc_gain == original_config.adc_gain

    mac = ":".join(f"{byte:02x}" for byte in info.mac_address)
    print(
        "hardware HELLO/DEVICE_CONFIG tests passed: "
        f"mac={mac} hw={info.hardware_version} "
        f"rev={info.hardware_revision} fw={info.firmware_version} "
        f"protocol={info.protocol_version}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
