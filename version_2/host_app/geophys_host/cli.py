"""Command-line entry point for V2 host/device integration."""

from __future__ import annotations

import argparse

from .serial_client import request_device_info


def main() -> int:
    parser = argparse.ArgumentParser(prog="geophys-host")
    subparsers = parser.add_subparsers(dest="command", required=True)
    hello = subparsers.add_parser("hello", help="request DEVICE_INFO")
    hello.add_argument("port")
    hello.add_argument("--baud", type=int, default=921_600)
    hello.add_argument("--timeout", type=float, default=2.0)
    arguments = parser.parse_args()

    if arguments.command == "hello":
        info = request_device_info(
            arguments.port, arguments.baud, arguments.timeout)
        print(f"result={info.result}")
        print("mac=" + ":".join(f"{byte:02x}" for byte in info.mac_address))
        print(f"hardware_version={info.hardware_version}")
        print(f"hardware_revision={info.hardware_revision}")
        print(f"firmware_version={info.firmware_version}")
        print(f"protocol_version={info.protocol_version}")
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
