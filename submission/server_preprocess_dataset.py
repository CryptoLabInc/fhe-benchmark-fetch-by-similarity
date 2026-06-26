#!/usr/bin/env python3
"""
server_preprocess_dataset.py - Start the persistent query server.

Launches the compute executable (task1-count-matches / task2-return-k-matches)
in its stdin "server mode": it loads the evaluation keys, encrypted DB and
bootstrapper ONCE and then waits for queries. Subsequent per-query steps
(server_encrypted_compute) stream queries to this running process, so the
multi-second setup is paid once here instead of on every query.

The server is torn down after the harness query loop by server_stop.py.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from submission_utils import (
    parse_harness_args, size_to_preset, update_config, start_query_server,
)


def main():
    args = parse_harness_args()
    preset = size_to_preset(args.size)
    update_config(preset, args.size)

    exe = "task1-count-matches" if args.count_only else "task2-return-k-matches"
    print(f"  Starting server ({exe}): loading keys/DB/bootstrapper once...")
    start_query_server(args.size, exe)
    print("  Server READY.")


if __name__ == "__main__":
    main()
