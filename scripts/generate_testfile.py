#!/usr/bin/env python3
"""Generate deterministic binary test payloads for BLG337E Project 1."""

import hashlib
import os
import random


def generate_file(path: str, size_bytes: int, seed: int) -> str:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    rng = random.Random(seed)
    data = rng.randbytes(size_bytes)
    with open(path, "wb") as f:
        f.write(data)
    digest = hashlib.sha256(data).hexdigest()
    print(f"Generated {path} ({size_bytes} bytes) sha256={digest}")
    return digest


def main() -> None:
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    generate_file(os.path.join(repo_root, "data", "testfile_256k.bin"), 256 * 1024, seed=337)
    generate_file(os.path.join(repo_root, "data", "testfile_16k.bin"), 16 * 1024, seed=338)


if __name__ == "__main__":
    main()
