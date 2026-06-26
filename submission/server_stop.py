#!/usr/bin/env python3
"""
server_stop.py - Tear down the persistent query server.

Invoked by the harness after the query loop finishes. Sends "quit" to the
server started by server_preprocess_dataset.py (then SIGTERM/SIGKILL as a
fallback) so no GPU-resident process is left running. No-op if none exists.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from submission_utils import parse_harness_args, stop_query_server


def main():
    args = parse_harness_args()
    stop_query_server(args.size)
    print("  Query server stopped.")


if __name__ == "__main__":
    main()
