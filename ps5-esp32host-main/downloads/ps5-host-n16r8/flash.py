"""Usage: python flash.py --port COM5 [--baud 460800]."""
import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser(description="Flash the PS5 host to an ESP32-S3 with 16 MB flash.")
parser.add_argument("--port", required=True, help="COM5 on Windows or /dev/ttyACM0 on Linux")
parser.add_argument("--baud", type=int, default=460800)
args = parser.parse_args()
folder = Path(__file__).resolve().parent
image = folder / "ps5-host-n16r8.bin"
if not image.is_file():
    parser.error("Run the copy of flash.py in the extracted firmware package.")
expected = (folder / "SHA256SUMS").read_text().split()[0]
if hashlib.sha256(image.read_bytes()).hexdigest() != expected:
    parser.error("Firmware checksum failed. Re-extract the package.")
base = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", args.port, "--baud", str(args.baud)]
probe = subprocess.run(base + ["flash_id"], capture_output=True, text=True)
print(probe.stdout)
if probe.returncode:
    print(probe.stderr, file=sys.stderr)
    raise SystemExit(probe.returncode)
if not re.search(r"(?:Detected )?[Ff]lash size:\s*16\s*MB", probe.stdout):
    parser.error("Expected 16 MB flash. Check that this board is the N16R8 model.")
print("Writing firmware and website to this board. Existing firmware/site will be replaced.")
subprocess.run(base + ["write_flash", "--flash_mode", "keep", "--flash_freq", "keep",
                      "--flash_size", "keep", "0x0", str(image)], check=True)
print("Flash complete. Reset the board, then join PS5-Host (password: ps5offline).")
