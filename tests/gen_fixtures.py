#!/usr/bin/env python3
"""Generate deterministic test fixtures for the round-trip harness.

Writes into the directory given as argv[1] (default: tests/tmp):
  - cover.wav   : 200k-sample mono 16-bit PCM WAV (ample LSB capacity)
  - cover.bmp   : 128x128 24-bit noisy BMP (enough high-complexity BPCS blocks)
  - secret.bin  : 2 KiB pseudo-random-ish payload
No third-party deps (no PIL/numpy) so it runs on a bare Python 3.
"""
import os
import struct
import sys

out = sys.argv[1] if len(sys.argv) > 1 else "tests/tmp"
os.makedirs(out, exist_ok=True)


def write_wav(path, samples):
    with open(path, "wb") as f:
        data = b"".join(struct.pack("<h", (i * 37) % 20000 - 10000) for i in range(samples))
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + len(data)))
        f.write(b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 1, 1, 16000, 32000, 2, 16))
        f.write(b"data")
        f.write(struct.pack("<I", len(data)))
        f.write(data)


def write_bmp(path, w, h):
    row = w * 3
    pad = (4 - row % 4) % 4
    px = bytearray()
    for y in range(h):
        for x in range(w):
            px += bytes(((x * 7 + y * 13) % 256, (x * 5 + y * 3) % 256, (x ^ y) % 256))
        px += b"\x00" * pad
    size = 54 + len(px)
    hdr = b"BM" + struct.pack("<IHHI", size, 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(px), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + px)


write_wav(os.path.join(out, "cover.wav"), 200000)
write_bmp(os.path.join(out, "cover.bmp"), 128, 128)
# Small secret image for the image-in-image LSB case (its PNG encoding must fit
# in the 128x128 cover's capacity).
write_bmp(os.path.join(out, "secret_img.bmp"), 16, 16)
with open(os.path.join(out, "secret.bin"), "wb") as f:
    f.write(bytes((i * 97 + 13) % 256 for i in range(2048)))

print("fixtures written to", out)
