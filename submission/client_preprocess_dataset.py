#!/usr/bin/env python3
"""
client_preprocess_dataset.py - Convert harness dataset to the engine format.

Reads the harness-generated db.bin and payloads.bin (float32/int16),
and writes origin and payload files (float64/uint64) for the engine.
"""
import sys
import numpy as np
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from submission_utils import (
    parse_harness_args, size_to_preset, get_data_dir, get_io_dir,
    get_record_dim, get_db_size,
    update_config, write_origin_file, write_payload_file,
    PAYLOAD_DIM, PAYLOAD_OFFSET,
)


def main():
    args = parse_harness_args()
    preset = size_to_preset(args.size)

    # Update engine config for this preset
    update_config(preset, args.size)

    dim = get_record_dim(args.size)
    db_size = get_db_size(args.size)

    # Read harness data
    data_dir = get_data_dir(args.size)
    db = np.fromfile(data_dir / "db.bin", dtype=np.float32).reshape(db_size, dim)
    harness_payloads = np.fromfile(data_dir / "payloads.bin", dtype=np.int16).reshape(
        db_size, PAYLOAD_DIM)

    # Convert to engine format (written into the engine working dir = io/{instance})
    engine_data_dir = get_io_dir(args.size) / "data"
    engine_data_dir.mkdir(parents=True, exist_ok=True)

    # Write origin file (float64 vectors with header)
    origin_path = engine_data_dir / f"task-{preset}_origin.bin"
    write_origin_file(origin_path, db)
    print(f"  Converted {db_size} vectors ({dim}-dim) to {origin_path}")

    # Write payload file (PAYLOAD_DIM uint64 per record, record-major:
    # payload[i * num_channels + c]).  Offset by PAYLOAD_OFFSET so every channel
    # value is strictly positive — the engine's decrypt skips slots with
    # |value| <= 0.25, which would otherwise drop legitimate zero entries.
    engine_payloads = (harness_payloads.astype(np.int64) + PAYLOAD_OFFSET).astype(
        np.uint64).reshape(-1)
    payload_path = engine_data_dir / f"task-{preset}_payload.bin"
    write_payload_file(payload_path, engine_payloads)
    print(f"  Converted {db_size} x {PAYLOAD_DIM} payload channels "
          f"to {payload_path}")


if __name__ == "__main__":
    main()
