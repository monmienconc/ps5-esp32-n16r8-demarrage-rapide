"""Stage this repository's static site and generate a local TLS identity."""
import hashlib
import ipaddress
import json
import shutil
from datetime import datetime, timedelta, timezone
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
REPO = PROJECT.parents[1]
PAYLOADS = ["elfldr-ps5-1360.elf", "kexp_2026_05_25.bin", "nanodns.elf",
            "shadowmountplus.elf", "kstuff.elf"]


def prepare():
    sources = [REPO / "index.html"]
    for folder in ("src", "offsets"):
        sources.extend(sorted((REPO / folder).rglob("*.js")))
    sources.extend(REPO / "payloads" / name for name in PAYLOADS)
    for path in sources:
        if not path.is_file() or path.is_symlink():
            raise SystemExit("Missing or symlinked source: " + str(path))
        if path.suffix == ".elf" and path.read_bytes()[:4] != b"\x7fELF":
            raise SystemExit("Invalid ELF: " + str(path))
        if len(path.relative_to(REPO).as_posix().encode()) + 2 >= 128:
            raise SystemExit("SPIFFS filename too long: " + str(path))
    size = sum(path.stat().st_size for path in sources)
    # Leave ample filesystem metadata/headroom; firmware is in another partition.
    if size > 0xCF0000 * 0.70:
        raise SystemExit("Static files exceed the conservative SPIFFS storage budget.")
    stage = PROJECT / "site"
    if stage.is_symlink() or stage.resolve().parent != PROJECT.resolve():
        raise SystemExit("Unsafe staging directory")
    if stage.exists():
        shutil.rmtree(stage)  # Only the verified, generated project/site directory.
    stage.mkdir()
    manifest = []
    for path in sources:
        relative = path.relative_to(REPO)
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        manifest.append({"path": relative.as_posix(), "bytes": path.stat().st_size,
                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    (PROJECT / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    generate_certificate()
    print(f"Staged {len(sources)} files, {size:,} bytes. Certificate ready.")


def generate_certificate():
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID

    certs = PROJECT / "certs"
    certs.mkdir(exist_ok=True)
    key_file, cert_file = certs / "server.key", certs / "server.crt"
    if key_file.exists() and cert_file.exists():
        return
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "manuals.playstation.net")])
    now = datetime.now(timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - timedelta(days=1)).not_valid_after(now + timedelta(days=3650))
            .add_extension(x509.SubjectAlternativeName([
                x509.DNSName("manuals.playstation.net"), x509.DNSName("ps5.host"),
                x509.IPAddress(ipaddress.ip_address("192.168.4.1"))]), critical=False)
            .sign(key, hashes.SHA256()))
    key_file.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                         serialization.PrivateFormat.TraditionalOpenSSL,
                         serialization.NoEncryption()))
    key_file.chmod(0o600)
    cert_file.write_bytes(cert.public_bytes(serialization.Encoding.PEM))


if __name__ == "__main__":
    prepare()
