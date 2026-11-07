"""Merge a successful ESP-IDF build into a single USB-flashable image."""
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

project = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else project / "build"
dist = project / "dist"
dist.mkdir(exist_ok=True)
config = json.loads((build / "flasher_args.json").read_text())
image = dist / "ps5-host-n16r8.bin"
settings = config["flash_settings"]
if settings["flash_size"] != "16MB" or config["extra_esptool_args"]["chip"] != "esp32s3":
    raise SystemExit("This packager requires an ESP32-S3 / 16 MB build.")
args = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge_bin",
        "--output", str(image), "--flash_mode", settings["flash_mode"],
        "--flash_freq", settings["flash_freq"], "--flash_size", settings["flash_size"]]
for offset, filename in sorted(config["flash_files"].items(), key=lambda item: int(item[0], 16)):
    args.extend([offset, str(build / filename)])
subprocess.run(args, check=True)
if image.stat().st_size > 16 * 1024 * 1024:
    raise SystemExit("Merged image exceeds 16 MB.")
checksum = hashlib.sha256(image.read_bytes()).hexdigest()
(dist / "SHA256SUMS").write_text(f"{checksum}  {image.name}\n", encoding="ascii")
shutil.copyfile(project / "tools" / "flash.py", dist / "flash.py")
shutil.copyfile(project / "build-manifest.json", dist / "build-manifest.json")
shutil.copyfile(project / "FLASHING.md", dist / "FLASHING.md")
# Explicit members keep reruns from including an earlier archive inside itself.
import zipfile
with zipfile.ZipFile(dist / "ps5-host-n16r8.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    for name in [image.name, "SHA256SUMS", "flash.py", "build-manifest.json", "FLASHING.md"]:
        archive.write(dist / name, name)
print(f"Firmware: {image}\nSHA256: {checksum}")
