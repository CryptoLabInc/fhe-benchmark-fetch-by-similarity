#!/usr/bin/env python3
"""
client_decrypt_decode.py - Decrypt the FHE results.

count_only mode: task1-decrypt (count ciphertext → integer).
payload mode:    task2-decrypt (k-matches ciphertexts → payload floats).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from submission_utils import (
    parse_harness_args, size_to_preset, update_config, run_engine,
)


def main():
    args = parse_harness_args()
    preset = size_to_preset(args.size)
    update_config(preset, args.size)

    if args.count_only:
        run_engine("task1-decrypt", size=args.size, omp_threads=1)
    else:
        run_engine("task2-decrypt", size=args.size, omp_threads=1)


if __name__ == "__main__":
    main()
