#!/usr/bin/env python3
"""
client_postprocess.py - Convert decrypted results to harness format.

count_only mode:
  - Reads task-decrypted-data/1/decrypted.bin (float64 coefficients)
  - Extracts the count (first coefficient, rounded to integer)
  - Writes io/{size}/results.bin as a long integer

payload mode:
  - Reads task-decrypted-data/2/decrypted.bin
      uint64 data_size
      float64 values[data_size]   (== match_count * NUM_PAYLOAD_CHANNELS)
  - Rounds each value to int, subtracts PAYLOAD_OFFSET (undoes preprocess shift),
    reshapes to (match_count, NUM_PAYLOAD_CHANNELS), lex-sorts to match the
    harness's expected ordering (see cleartext_impl.py), and writes int16 to
    io/{size}/results.bin.
"""
import sys
import numpy as np
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from submission_utils import (
    parse_harness_args, size_to_preset, get_io_dir,
    NUM_PAYLOAD_CHANNELS, PAYLOAD_OFFSET,
)


def postprocess_count(engine_dir: Path, io_dir: Path):
    """Convert task1 decrypted result to harness count format."""
    dec_path = engine_dir / "task-decrypted-data" / "1" / "decrypted.bin"
    if not dec_path.exists():
        print(f"Error: Decrypted result not found: {dec_path}")
        sys.exit(1)

    # Read the decrypted coefficients (array of float64)
    coeffs = np.fromfile(dec_path, dtype=np.float64)

    # The count is in the first coefficient
    count = int(round(coeffs[0]))
    print(f"  Decrypted count: {count}")

    # Write as a long integer (matching harness verify_result.py expectations)
    io_dir.mkdir(parents=True, exist_ok=True)
    result = np.array([count], dtype=np.int_)
    result.tofile(io_dir / "results.bin")
    print(f"  Wrote count result to {io_dir / 'results.bin'}")


def postprocess_payload(engine_dir: Path, io_dir: Path):
    """Convert task2 decrypted result to harness payload format."""
    dec_path = engine_dir / "task-decrypted-data" / "2" / "decrypted.bin"
    if not dec_path.exists():
        print(f"Error: Decrypted result not found: {dec_path}")
        sys.exit(1)

    with open(dec_path, 'rb') as f:
        header = np.fromfile(f, dtype=np.uint64, count=1)
        if len(header) != 1:
            print(f"Error: Malformed decrypted file (no size header): {dec_path}")
            sys.exit(1)
        data_size = int(header[0])
        values = np.fromfile(f, dtype=np.float64, count=data_size)
    if len(values) != data_size:
        print(f"Error: Short read in {dec_path}: header={data_size}, got={len(values)}")
        sys.exit(1)

    if data_size % NUM_PAYLOAD_CHANNELS != 0:
        print(f"Error: Decrypted value count {data_size} is not a multiple of "
              f"NUM_PAYLOAD_CHANNELS={NUM_PAYLOAD_CHANNELS}; payload grouping ambiguous.")
        sys.exit(1)

    match_count = data_size // NUM_PAYLOAD_CHANNELS
    print(f"  Decrypted matches: {match_count}")

    # Round to nearest int, undo the +PAYLOAD_OFFSET shift applied at preprocess.
    rounded = np.rint(values).astype(np.int64) - PAYLOAD_OFFSET
    payloads = rounded.reshape(match_count, NUM_PAYLOAD_CHANNELS).astype(np.int16)

    # Harness's expected.bin (see harness/cleartext_impl.py) sorts matching
    # payload rows lexicographically.  Match that ordering so verify_result.py
    # can compare row-by-row.
    if match_count > 0:
        payloads = payloads[np.lexsort(payloads.T[::-1])]

    io_dir.mkdir(parents=True, exist_ok=True)
    payloads.tofile(io_dir / "results.bin")
    #print(f"  Wrote {match_count} x {NUM_PAYLOAD_CHANNELS} payload result to "
    #      f"{io_dir / 'results.bin'}")


def main():
    args = parse_harness_args()
    preset = size_to_preset(args.size)
    # The engine ran with cwd = io/{instance}, so its decrypted output and the
    # harness result file live in the same directory.
    io_dir = get_io_dir(args.size)
    engine_dir = io_dir

    if args.count_only:
        postprocess_count(engine_dir, io_dir)
    else:
        postprocess_payload(engine_dir, io_dir)


if __name__ == "__main__":
    main()
