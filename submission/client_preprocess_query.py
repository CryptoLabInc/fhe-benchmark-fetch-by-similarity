#!/usr/bin/env python3
"""
client_preprocess_query.py - Convert harness query to the engine format.

Reads the harness-generated query.bin (float32) and overwrites the engine's
origin file with a single-vector file containing just the query.
This allows encrypt-query to encrypt only this query.

Since DB encryption has already completed by this point, overwriting
the origin file is safe.
"""
import sys
import numpy as np
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from submission_utils import (
    parse_harness_args, size_to_preset, get_data_dir, get_io_dir,
    get_record_dim, write_origin_file,
)


def main():
    args = parse_harness_args()
    preset = size_to_preset(args.size)
    dim = get_record_dim(args.size)

    # Read the harness query (float32, single vector)
    data_dir = get_data_dir(args.size)
    query = np.fromfile(data_dir / "query.bin", dtype=np.float32)
    assert len(query) == dim, f"Query dim mismatch: {len(query)} vs {dim}"

    # Write as engine origin file with size=1
    # This way encrypt-query will encrypt exactly one query vector.
    query_2d = query.reshape(1, dim)
    origin_path = get_io_dir(args.size) / "data" / f"task-{preset}_query.bin"
    write_origin_file(origin_path, query_2d)
    #print(f"  Wrote a query to {origin_path} ")


if __name__ == "__main__":
    main()
