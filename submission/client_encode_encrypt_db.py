#!/usr/bin/env python3
"""
client_encode_encrypt_db.py - Encrypt the database.

Calls task-encrypt-data to encrypt the preprocessed DB vectors
using HEaaN coefficient encoding.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from submission_utils import parse_harness_args, size_to_preset, update_config, run_engine


def main():
    args = parse_harness_args()
    preset = size_to_preset(args.size)
    update_config(preset, args.size)

    run_engine("task-encrypt-data", size=args.size, omp_threads=8)


if __name__ == "__main__":
    main()
