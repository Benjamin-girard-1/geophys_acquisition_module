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
    CommandStreamParser,
    ProtocolError,
    decode_command,
    decode_device_info,
    encode_hello,
)

VECTORS = REPOSITORY / "shared" / "protocol" / "test_vectors" / "command"


def load_hex(path: Path) -> bytes:
    return bytes.fromhex(path.read_text(encoding="ascii"))


class ProtocolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.hello = load_hex(VECTORS / "valid" / "hello_request.txt")
        self.info = load_hex(VECTORS / "valid" / "device_info_reply.txt")
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
