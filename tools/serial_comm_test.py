#!/usr/bin/env python3
"""BuddyBot serial communication tester for PC diagnostics.

Usage:
  python tools/serial_comm_test.py --list
  python tools/serial_comm_test.py --pico COM25
  python tools/serial_comm_test.py --mega COM9
  python tools/serial_comm_test.py --bridge COM25
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial required: pip install pyserial")
    sys.exit(1)

BAUD = 115200
KNOWN_VIDS = {
    0x2E8A: "Raspberry Pi Pico/W",
    0x1A86: "CH340 (Mega clone USB)",
    0x2341: "Arduino",
    0x10C4: "CP210x UART",
    0x0403: "FTDI",
}


def list_ports() -> None:
    print("Available serial ports:")
    for p in serial.tools.list_ports.comports():
        vid = f"0x{p.vid:04X}" if p.vid is not None else "----"
        pid = f"0x{p.pid:04X}" if p.pid is not None else "----"
        label = KNOWN_VIDS.get(p.vid or -1, p.description or "?")
        print(f"  {p.device:6}  {vid}:{pid}  {label}")
        if p.manufacturer:
            print(f"           manufacturer: {p.manufacturer}")


def open_port(port: str, reset: bool = False) -> serial.Serial:
    ser = serial.Serial(port, BAUD, timeout=0.25)
    ser.dtr = True
    ser.rts = True
    if reset:
        ser.dtr = False
        time.sleep(0.1)
        ser.dtr = True
        time.sleep(2.0)
    else:
        time.sleep(0.5)
    ser.reset_input_buffer()
    return ser


def drain(ser: serial.Serial, seconds: float) -> list[str]:
    lines: list[str] = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("utf-8", errors="replace").strip()
        if text:
            lines.append(text)
            print(f"  << {text}")
    return lines


def send(ser: serial.Serial, cmd: str) -> None:
    payload = cmd if cmd.endswith("\n") else cmd + "\n"
    print(f"  >> {payload.strip()}")
    ser.write(payload.encode())
    ser.flush()


def test_passive(port: str, seconds: float, reset: bool) -> int:
    print(f"\n=== Passive listen: {port} ({seconds}s) ===")
    try:
        ser = open_port(port, reset=reset)
        lines = drain(ser, seconds)
        ser.close()
        print(f"  Received {len(lines)} line(s)")
        return len(lines)
    except serial.SerialException as exc:
        print(f"  ERROR: {exc}")
        return -1


def test_commands(port: str, commands: list[str], listen: float = 3.0) -> int:
    print(f"\n=== Command test: {port} ===")
    try:
        ser = open_port(port)
        total = 0
        for cmd in commands:
            send(ser, cmd)
            time.sleep(0.4)
            lines = drain(ser, listen)
            total += len(lines)
        ser.close()
        print(f"  Received {total} line(s)")
        return total
    except serial.SerialException as exc:
        print(f"  ERROR: {exc}")
        return -1


def test_bridge(pico_port: str) -> None:
    print(f"\n=== Bridge test via Pico USB: {pico_port} ===")
    print("  Expect with new Pico firmware:")
    print("    PICO_ALIVE|Mega=Y|...  every 5s")
    print("    [S9->M] DIAG:RUN       when you send CMD:DIAG:RUN")
    print("    [M->S9] TELE:...       when Mega UART is wired and responding")
    test_passive(pico_port, 6, reset=False)
    test_commands(
        pico_port,
        ["CMD:DIAG:RUN", "DIAG:RUN", "MOTOR:S", "PING", "STATUS"],
        listen=4,
    )
    test_passive(pico_port, 8, reset=False)


def main() -> int:
    parser = argparse.ArgumentParser(description="BuddyBot serial comm tester")
    parser.add_argument("--list", action="store_true", help="List COM ports")
    parser.add_argument("--pico", metavar="COM", help="Test Pico W USB port")
    parser.add_argument("--mega", metavar="COM", help="Test Mega USB port")
    parser.add_argument("--bridge", metavar="COM", help="Bridge test via Pico USB")
    parser.add_argument("--passive", type=float, default=8, help="Passive listen seconds")
    args = parser.parse_args()

    if args.list or not any([args.pico, args.mega, args.bridge]):
        list_ports()
        if not any([args.pico, args.mega, args.bridge]):
            print("\nTip: Mega native USB should appear as CH340 (0x1A86) or Arduino (0x2341).")
            print("     Pico W appears as 0x2E8A. Bridge test uses the Pico COM port.")
            return 0

    if args.pico:
        test_passive(args.pico, args.passive, reset=False)
        test_commands(args.pico, ["CMD:DIAG:RUN", "MOTOR:S"], listen=4)

    if args.mega:
        test_passive(args.mega, args.passive, reset=False)
        test_commands(args.mega, ["DIAG:RUN", "CMD:DIAG:RUN", "MOTOR:S"], listen=4)

    if args.bridge:
        test_bridge(args.bridge)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())