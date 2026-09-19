"""Command-line entry point for V2 host/device integration."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
from pathlib import Path
import sys
import time

from .capture import RawRecordCapture
from .live import LiveStreamModel, MatplotlibLiveView
from .protocol import (
    REPLY_DEVICE_CONFIG,
    REPLY_DEVICE_INFO,
    REPLY_STREAMING_START_RESULT,
    REPLY_STREAMING_STOP_RESULT,
    RESULT_SUCCESS,
    ProtocolError,
    VALID_STREAM_DECIMATIONS,
    decode_device_config,
    decode_device_info,
    decode_streaming_start_result,
    decode_streaming_stop_result,
    encode_device_get_config,
    encode_hello,
    encode_streaming_start,
    encode_streaming_stop,
)
from .serial_client import SerialClient, request_device_info
from .stream_parser import AdcRecordMessage

CHANNEL_MASK_ARGUMENTS = {
    "0-3": 0x0F,
    "4-7": 0xF0,
    "all": 0xFF,
}


def _require_success(label: str, result: int) -> None:
    if result != RESULT_SUCCESS:
        raise RuntimeError(f"{label} failed with result 0x{result:02x}")


def _format_status(model: LiveStreamModel, invalid_records: int,
                   bytes_discarded: int) -> str:
    snapshot = model.snapshot()
    statuses = ",".join(
        f"{status}:{count}"
        for status, count in sorted(snapshot.status_counts.items())
    ) or "none"
    return (
        f"blocks={snapshot.blocks_received} "
        f"conversions={snapshot.conversions_received} "
        f"rate={snapshot.conversion_rate_hz:.1f}/s "
        f"missing={snapshot.missing_conversions} "
        f"payload_gaps={snapshot.payload_discontinuities} "
        f"bad_blocks={invalid_records} "
        f"discarded_bytes={bytes_discarded} "
        f"status={statuses}"
    )


def _run_live(arguments: argparse.Namespace) -> int:
    viewer = None
    stream_started = False
    terminal_line_active = False

    with ExitStack() as stack:
        capture = None
        client = stack.enter_context(SerialClient(
            arguments.port,
            arguments.baud,
            write_timeout_s=arguments.timeout,
        ))

        info = decode_device_info(client.request(
            encode_hello(), REPLY_DEVICE_INFO,
            timeout_s=arguments.timeout))
        _require_success("HELLO", info.result)
        config = decode_device_config(client.request(
            encode_device_get_config(), REPLY_DEVICE_CONFIG,
            timeout_s=arguments.timeout))
        _require_success("DEVICE_GET_CONFIG", config.result)

        requested_mask = (
            config.adc_channel_mask
            if arguments.channels is None
            else CHANNEL_MASK_ARGUMENTS[arguments.channels]
        )
        if requested_mask == 0:
            raise RuntimeError(
                "the device configuration has no active ADC channels; "
                "pass --channels or configure the device first")

        start = decode_streaming_start_result(client.request(
            encode_streaming_start(arguments.decimation, requested_mask),
            REPLY_STREAMING_START_RESULT,
            timeout_s=arguments.timeout,
        ))
        _require_success("STREAMING_START", start.result)
        if start.channel_mask == 0:
            raise RuntimeError("device accepted a stream with no channels")
        stream_started = True
        model = LiveStreamModel(
            window_s=arguments.window,
            source_sequence_step=start.decimation or 1,
        )

        def handle_record(message: AdcRecordMessage) -> None:
            model.append(message.record)
            if capture is not None:
                capture.append(message.raw)

        try:
            if arguments.capture is not None:
                capture = stack.enter_context(
                    RawRecordCapture(arguments.capture))
            if not arguments.terminal:
                viewer = MatplotlibLiveView(
                    start.channel_mask, arguments.window)

            print(
                f"stream started: channels=0x{start.channel_mask:02x} "
                f"decimation={start.decimation or 1} "
                f"recording={'yes' if start.recording_in_progress else 'no'}",
                file=sys.stderr,
            )
            started_at = time.monotonic()
            keepalive_at = started_at + 1.0
            draw_at = started_at
            try:
                while True:
                    now = time.monotonic()
                    if arguments.duration is not None and \
                            now - started_at >= arguments.duration:
                        break
                    if viewer is not None and not viewer.is_open:
                        break

                    message = client.read_message()
                    if isinstance(message, AdcRecordMessage):
                        handle_record(message)

                    now = time.monotonic()
                    if now >= keepalive_at:
                        live_config = decode_device_config(client.request(
                            encode_device_get_config(),
                            REPLY_DEVICE_CONFIG,
                            timeout_s=arguments.timeout,
                            on_record=handle_record,
                        ))
                        _require_success(
                            "DEVICE_GET_CONFIG", live_config.result)
                        keepalive_at = time.monotonic() + 1.0

                    if now >= draw_at:
                        if viewer is not None:
                            viewer.update(
                                model,
                                client.parser.invalid_records,
                                client.parser.bytes_discarded,
                            )
                        else:
                            print(
                                "\r" + _format_status(
                                    model,
                                    client.parser.invalid_records,
                                    client.parser.bytes_discarded,
                                ),
                                end="",
                                flush=True,
                            )
                            terminal_line_active = True
                        draw_at = now + arguments.refresh
            except KeyboardInterrupt:
                pass
        finally:
            try:
                if stream_started:
                    stop = decode_streaming_stop_result(client.request(
                        encode_streaming_stop(),
                        REPLY_STREAMING_STOP_RESULT,
                        timeout_s=arguments.timeout,
                        on_record=handle_record,
                    ))
                    _require_success("STREAMING_STOP", stop.result)
                    stream_started = False
            finally:
                if capture is not None:
                    capture.flush()
                if viewer is not None:
                    viewer.close()

        if terminal_line_active:
            print()
        print(_format_status(
            model,
            client.parser.invalid_records,
            client.parser.bytes_discarded,
        ))
        if capture is not None:
            print(
                f"capture={capture.path} "
                f"blocks={capture.blocks_written} "
                f"bytes={capture.bytes_written}"
            )
    return 0


def _positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0.0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def main() -> int:
    parser = argparse.ArgumentParser(prog="geophys-host")
    subparsers = parser.add_subparsers(dest="command", required=True)

    hello = subparsers.add_parser("hello", help="request DEVICE_INFO")
    hello.add_argument("port")
    hello.add_argument("--baud", type=int, default=921_600)
    hello.add_argument("--timeout", type=_positive_float, default=2.0)

    live = subparsers.add_parser(
        "live", help="plot or print the live ADC stream")
    live.add_argument("port")
    live.add_argument("--baud", type=int, default=921_600)
    live.add_argument("--timeout", type=_positive_float, default=2.0)
    live.add_argument(
        "--channels", choices=tuple(CHANNEL_MASK_ARGUMENTS),
        help="requested channels (default: current device mask)")
    live.add_argument(
        "--decimation", type=int, choices=VALID_STREAM_DECIMATIONS,
        default=0,
        help="stream decimation; 0 means no decimation")
    live.add_argument(
        "--window", type=_positive_float, default=5.0,
        help="visible plot window in seconds")
    live.add_argument(
        "--refresh", type=_positive_float, default=0.1,
        help="display refresh interval in seconds")
    live.add_argument(
        "--duration", type=_positive_float,
        help="stop automatically after this many seconds")
    live.add_argument(
        "--capture", type=Path,
        help="write validated 512-byte DAT blocks to a new file")
    live.add_argument(
        "--terminal", action="store_true",
        help="show counters in the terminal instead of opening a plot")

    arguments = parser.parse_args()
    if arguments.command == "hello":
        info = request_device_info(
            arguments.port, arguments.baud, arguments.timeout)
        print(f"result={info.result}")
        print("mac=" + ":".join(f"{byte:02x}" for byte in info.mac_address))
        print(f"hardware_version={info.hardware_version}")
        print(f"hardware_revision={info.hardware_revision}")
        print(f"firmware_version={info.firmware_version}")
        return 0
    if arguments.command == "live":
        return _run_live(arguments)
    return 2


def entrypoint() -> int:
    try:
        return main()
    except (OSError, ProtocolError, RuntimeError, TimeoutError) as error:
        print(f"geophys-host: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(entrypoint())
