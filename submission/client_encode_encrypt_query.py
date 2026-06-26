#!/usr/bin/env python3
"""
client_encode_encrypt_query.py - Encrypt the query.

The origin file was replaced with a single query vector in the
preprocess step. Now encrypt it using task-encrypt-query.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from submission_utils import parse_harness_args, size_to_preset, update_config, run_engine


def main():
    args = parse_harness_args()
    preset = size_to_preset(args.size)
    update_config(preset, args.size)

    run_engine("task-encrypt-query", size=args.size, omp_threads=8)


if __name__ == "__main__":
    main()
