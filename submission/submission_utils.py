"""
submission_utils.py - Shared utilities for the submission wrapper scripts.

Bridges the harness interface to the HEaaN-based FHE executables built
from the vendored `juvia` engine (prebuilt libjuvia.so under submission/lib,
headers under submission/include, task sources under submission/export).
"""
# Defer annotation evaluation so `np.ndarray` type hints don't require numpy at
# import time — numpy is imported lazily (only the write_*_file helpers use it).
# This keeps numpy-free steps (server_encrypted_compute, *_decrypt, *_encrypt_*,
# keygen, server start/stop) starting in ~5ms instead of ~45ms.
from __future__ import annotations

import os
import re
import sys
import json
import time
import errno
import signal
import struct
import subprocess
import argparse
from pathlib import Path

# Harness size → name mapping
SIZE_NAMES = {0: "toy", 1: "small", 2: "medium", 3: "large"}

# Only these presets are supported (no "toy")
SUPPORTED_PRESETS = {"small", "medium"}

# Threshold used by the benchmark (same as reference submission)
THRESHOLD = 0.8

# Number of payload channels per record exposed to the engine. Matches
# `PAYLOAD_DIM` in harness/params.py (16 int16 values per record).
NUM_PAYLOAD_CHANNELS = 16

# Single source of truth for the environment the juvia engine binaries run with.
# Edit this dict to relocate the engine's I/O directories or pass extra variables.
#
# Every entry is injected (via build_engine_env) into every engine executable —
# the per-step exes AND the persistent query server — so they all agree.
# The JUVIA_*_PATH entries also drive directory creation: engine_runtime_dirs()
# pre-creates each one in the engine cwd before a run (the binaries don't mkdir),
# and mirror submission/include/juvia/JuviaSettings.hpp's defaults. Each
# is a DIRECTORY the engine reads/writes into (it appends the filename itself).
#
# A value of None means "pass through from the harness's environment only if set"
# — not forced. ENGINE_ENV is overridden only by an explicit env_extra= on a
# single run_engine call.
ENGINE_ENV = {
    # Engine I/O directories (JUVIA_*_PATH) — created in the engine cwd per run.
    "JUVIA_RAW_DATA_PATH": "data",
    "JUVIA_SECRETKEY_PATH": "secret-keys",
    "JUVIA_PUBLICKEY_PATH": "keys",
    "JUVIA_QUERY_PATH": "ciphertexts_upload/query.bin",
    "JUVIA_ENCRYPT_DATA_PATH": "ciphertexts_upload",
    "JUVIA_RESULT_CIPHERTEXT_PATH_TASK1": "ciphertexts_download/results.bin",
    "JUVIA_RESULT_CIPHERTEXT_PATH_TASK2": "ciphertexts_download/results.bin",
    "JUVIA_DECRYPTED_DATA_PATH_TASK1": "task-decrypted-data/1",
    "JUVIA_DECRYPTED_DATA_PATH_TASK2": "task-decrypted-data/2",
    # Compute-binary timer logs. The engine defaults to task-times/task{1,2}-<preset>.log,
    # but server_compute_breakdown reads task-times/<exe_name>.log (get_timer_log_path),
    # so pin the engine's output to that exact file. These are FULL FILE paths (the
    # engine creates the task-times/ parent itself), keyed by the per-binary timer env
    # var — NOT *_PATH, so engine_runtime_dirs() correctly skips them.
    "JUVIA_TIMER_LOG_TASK1_COUNT_MATCHES": "task-times/task1-count-matches.log",
    "JUVIA_TIMER_LOG_TASK2_RETURN_K_MATCHES": "task-times/task2-return-k-matches.log",
}

# Engine drops decrypted slot values with |v| <= 0.25 when collecting matches,
# so payloads must be strictly positive. Offset before encrypt and undo after
# decrypt. Harness payloads are int16 in [0, 16), well within float64 range
# even after the +1 shift.
PAYLOAD_OFFSET = 0


def get_root_dir():
    """Get the project root directory."""
    return Path(os.environ.get("SUBMISSION_ROOT", Path.cwd()))


def get_engine_dir():
    """Code/build location of the vendored juvia engine (the submission dir).

    This is where the prebuilt library and the compiled task executables live;
    it is NOT the directory the executables run in. For the runtime working
    directory (cwd), see get_engine_workdir().
    """
    return get_root_dir() / "submission"


def get_engine_exe(name):
    """Get path to an engine executable (absolute, so cwd is free to vary)."""
    return get_engine_dir() / "build" / "export" / name


def engine_runtime_dirs():
    """Directories the engine executables expect to exist in their CWD.

    Derived from ENGINE_ENV: every JUVIA_*_PATH entry is a directory the engine
    reads/writes into, so we create exactly those (the binaries don't mkdir).
    Relocating a path in ENGINE_ENV moves the created dir with it — no stale
    default dir is left behind. Non-path engine vars in ENGINE_ENV are skipped.
    """
    dirs = []
    for key, path in ENGINE_ENV.items():
        if "PATH" not in key or not path:
            continue
        if path not in dirs:
            dirs.append(path)
    return dirs


def size_to_preset(size: int) -> str:
    """Convert harness size (0-3) to preset name."""
    name = SIZE_NAMES.get(size)
    if name not in SUPPORTED_PRESETS:
        print(f"Error: Size {size} ({name}) not supported.")
        print(f"  Supported: small(1), medium(2), large(3)")
        sys.exit(1)
    return name


def get_io_dir(size: int) -> Path:
    """Get the harness I/O directory for this size."""
    return get_root_dir() / "io" / SIZE_NAMES[size]


def get_engine_workdir(size: int) -> Path:
    """Directory the engine executables run in (their cwd).

    The juvia executables resolve every path — config.json and all the
    task-*/data directories (see JuviaSettings.hpp) — relative to their cwd.
    We run them inside the harness I/O directory (io/{instance}/) so that all
    client-local files (secret key, decrypted output) and client<->server wire
    artifacts (eval keys, encrypted DB/query, result ciphertexts) land exactly
    where the harness expects them. This is the same dir as get_io_dir(size).
    """
    return get_io_dir(size)


def get_data_dir(size: int) -> Path:
    """Get the harness dataset directory for this size."""
    return get_root_dir() / "datasets" / SIZE_NAMES[size]


def get_device_ids():
    """Get CUDA device IDs from environment or default."""
    ids_str = os.environ.get("DEVICE_IDS", "0")
    return [int(x.strip()) for x in ids_str.split(",")]


def update_config(preset: str, size: int):
    if preset not in SUPPORTED_PRESETS:
        print(f"Error: Preset {preset} not supported.")
        print(f"  Supported: small, medium, large")
        sys.exit(1)
    """Write the engine's config.json into the engine working dir (cwd)."""
    config = {
        "PRESET": preset,
        "DEVICE_IDS": get_device_ids(),
        "NUM_PAYLOAD_CHANNELS": NUM_PAYLOAD_CHANNELS,
    }
    workdir = get_engine_workdir(size)
    workdir.mkdir(parents=True, exist_ok=True)
    config_path = workdir / "config.json"
    with open(config_path, 'w') as f:
        json.dump(config, f, indent=2)


def build_engine_env(env_extra=None, omp_threads=None):
    """Environment for any juvia engine process (run_engine + query server).

    Applies the global ENGINE_ENV (e.g. JUVIA_*_PATH overrides) on top of the
    inherited environment so EVERY engine binary — the per-step executables AND
    the persistent query server — sees the same path layout. The juvia binaries
    read JUVIA_*_PATH once at static-init, so these must be set before launch.
    """
    env = os.environ.copy()
    # Variables passed to all engine executables (see ENGINE_ENV above).
    for k, v in ENGINE_ENV.items():
        if v is not None:
            env[k] = str(v)
    if omp_threads is not None:
        env["OMP_NUM_THREADS"] = str(omp_threads)
    # A per-call env_extra wins over the global ENGINE_ENV defaults.
    if env_extra:
        env.update(env_extra)
    return env


def run_engine(exe_name: str, *args, size: int, env_extra=None, omp_threads=None):
    """Run an engine executable with CWD set to the engine working dir.

    The exe is located by absolute path under submission/build/export, but it
    runs inside io/{instance}/ (get_engine_workdir) so every artifact it
    reads/writes via its cwd-relative paths lands in the harness I/O dir.
    """
    exe = get_engine_exe(exe_name)
    if not exe.exists():
        print(f"Error: Engine executable not found: {exe}")
        print(f"  Did you build the submission? Run: scripts/build_task.sh")
        sys.exit(1)

    workdir = get_engine_workdir(size)
    for d in engine_runtime_dirs():
        (workdir / d).mkdir(parents=True, exist_ok=True)

    cmd = [str(exe)] + [str(a) for a in args]
    env = build_engine_env(env_extra=env_extra, omp_threads=omp_threads)

    result = subprocess.run(cmd, cwd=str(workdir), env=env)
    if result.returncode != 0:
        print(f"Error: {exe_name} exited with code {result.returncode}")
        sys.exit(result.returncode)


# --- Engine per-step timer logs -------------------------------------------
#
# Each task executable appends JuviaTimer summaries to <workdir>/task-times/<exe>.log.
# In server mode the engine writes ONE summary for the one-time setup (eval-key /
# DB load, bootstrapper) and then ONE summary PER QUERY, so the LAST summary block
# is the most recent per-query work — which is what we report. The engine prefixes
# every per-query step with "**" (e.g. "** Scoring", "** Save result ciphertexts")
# to mark it as per-query server work; the setup-block steps are unmarked. We strip
# the marker for clean labels. A line looks like
# "** Compute matching total time = 13.0 ms".
COMPUTE_MARKER = "**"

_TIMER_UNIT_SECONDS = {"s": 1.0, "ms": 1e-3, "us": 1e-6, "µs": 1e-6, "μs": 1e-6, "ns": 1e-9}
_TIMER_LINE_RE = re.compile(
    r"^(?P<label>.+?)\s+total time\s*=\s*(?P<value>[\d.]+)\s*(?P<unit>ms|µs|μs|us|ns|s)\s*$"
)
_TIMER_SUMMARY_HEADER = "Timer Summary"


def get_timer_log_path(size: int, exe_name: str) -> Path:
    """Path of the per-exe JuviaTimer log (written relative to the engine cwd)."""
    return get_engine_workdir(size) / "task-times" / f"{exe_name}.log"


def parse_timer_log(log_path: Path):
    """Parse the most recent Timer Summary block into [(label, seconds), ...].

    Labels have the per-query "**" marker (see COMPUTE_MARKER) stripped. Each run
    appends a fresh summary, so we keep only the last block (the most recent
    per-query work in server mode).
    """
    text = Path(log_path).read_text(encoding="utf-8", errors="replace")
    block = text.rsplit(_TIMER_SUMMARY_HEADER, 1)[-1]
    steps = []
    for line in block.splitlines():
        m = _TIMER_LINE_RE.match(line.strip())
        if not m:
            continue
        label = m.group("label").lstrip("*").strip()
        seconds = float(m.group("value")) * _TIMER_UNIT_SECONDS[m.group("unit")]
        steps.append((label, seconds))
    return steps


def server_compute_breakdown(size: int, exe_name: str):
    """Per-query server breakdown of a compute exe, from the engine's timer log.

    Reports the last per-query summary block — the most recent query's full
    server-side work (query load, the homomorphic computation, payload gather,
    and result save). "[[Total compute time]]" is the sum of all its steps; each
    step is listed with a clean label. Returns None if the log is missing or
    empty, so callers fall back to the measured wall-clock.
    """
    log_path = get_timer_log_path(size, exe_name)
    if not log_path.exists():
        return None
    steps = parse_timer_log(log_path)
    if not steps:
        return None
    report = {"[[Total compute time]]": round(sum(sec for _, sec in steps), 6)}
    for label, sec in steps:
        report[label] = round(sec, 6)
    return report


# --- Persistent query server (juvia stdin "server mode") --------------------
#
# The compute executables (task1-count-matches / task2-return-k-matches) support
# a server mode: launched with no args they load keys/DB/bootstrapper ONCE, print
# "READY", then process one query per stdin line ("<QUERY_INDEX> <THRESHOLD>"),
# printing "DONE <idx> ..." / "ERROR ..." per query. We keep one such process
# alive across the harness's whole multi-query loop so the multi-second load is
# paid once instead of per query.
#
# The harness drives the pipeline as a sequence of short-lived per-step
# subprocesses, so to keep the server alive between them we detach it
# (start_new_session) and give it a FIFO opened O_RDWR as stdin — that keeps a
# writer reference open so the server never sees EOF between queries. Later steps
# push a query by opening the FIFO write-only, then tail the server's stdout log
# for the matching "DONE <idx> ..." / "ERROR ..." line. The engine re-reads the
# query ciphertext file on every query, so each iteration just overwrites it and
# sends index 0.
#
# (Server startup still prints "READY" once, which start_query_server waits for.)


def _server_paths(size):
    d = get_engine_workdir(size) / ".server"
    return {"dir": d, "fifo": d / "cmd.fifo", "out": d / "server.out",
            "err": d / "server.err", "info": d / "info.json"}


def _pid_alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _tail_until(path, start_offset, predicate, timeout, pid=None, poll=0.005):
    """Tail `path` (bytes) from start_offset until a line matches predicate.
    Returns the matching line, or None on timeout / server death."""
    deadline = time.monotonic() + timeout
    off, buf = start_offset, b""
    while time.monotonic() < deadline:
        try:
            with open(path, "rb") as f:
                f.seek(off)
                chunk = f.read()
                off += len(chunk)
        except FileNotFoundError:
            chunk = b""
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").rstrip("\r")
            if predicate(line):
                return line
        if pid is not None and not _pid_alive(pid):
            return None
        time.sleep(poll)
    return None


def start_query_server(size, exe_name, ready_timeout=600):
    """Launch the compute exe in server mode (detached), wait for READY.

    Kills any stale server for this instance first. Returns the server PID.
    """
    exe = get_engine_exe(exe_name)
    if not exe.exists():
        print(f"Error: Engine executable not found: {exe}")
        print(f"  Did you build the submission? Run: scripts/build_task.sh")
        sys.exit(1)

    stop_query_server(size)  # clear any stale server
    paths = _server_paths(size)
    workdir = get_engine_workdir(size)
    for d in engine_runtime_dirs():
        (workdir / d).mkdir(parents=True, exist_ok=True)
    paths["dir"].mkdir(parents=True, exist_ok=True)
    if paths["fifo"].exists():
        paths["fifo"].unlink()
    os.mkfifo(paths["fifo"])

    # O_RDWR so the server's stdin always has a writer ref -> never EOF.
    stdin_fd = os.open(str(paths["fifo"]), os.O_RDWR)
    out_f = open(paths["out"], "wb")
    err_f = open(paths["err"], "wb")
    # Same engine env as run_engine so the server's JUVIA_*_PATH layout matches
    # where the client steps wrote the keys/DB (otherwise it hangs on load).
    env = build_engine_env(omp_threads=4)
    proc = subprocess.Popen([str(exe)], cwd=str(workdir), stdin=stdin_fd,
                            stdout=out_f, stderr=err_f, start_new_session=True, env=env)
    os.close(stdin_fd)
    out_f.close()
    err_f.close()

    line = _tail_until(paths["out"], 0, lambda l: l.startswith("READY"),
                       ready_timeout, pid=proc.pid)
    if line is None:
        tail = ""
        try:
            tail = paths["err"].read_text(errors="replace")[-2000:]
        except OSError:
            pass
        print(f"Error: query server ({exe_name}) failed to become READY.\n{tail}")
        stop_query_server(size)
        sys.exit(1)

    paths["info"].write_text(json.dumps(
        {"pid": proc.pid, "exe": exe_name, "fifo": str(paths["fifo"]),
         "out": str(paths["out"])}))

    # Self-managed teardown: origin/main's run_submission.py never stops the
    # server, so launch a detached watchdog that tears it down when the harness
    # (our parent, run_submission.py) exits -- for ANY reason. os.getppid() here
    # is that harness process (start_query_server runs in the server_preprocess_
    # dataset step subprocess, whose parent is run_submission.py).
    _spawn_teardown_watchdog(size, os.getppid(), proc.pid)
    return proc.pid


def query_server_running(size):
    paths = _server_paths(size)
    if not paths["info"].exists():
        return False
    try:
        pid = json.loads(paths["info"].read_text()).get("pid")
    except (OSError, ValueError):
        return False
    return bool(pid) and _pid_alive(pid)


def send_query(size, query_index, threshold, timeout=600):
    """Stream one query to the running server and wait for its DONE line.

    The engine prints "DONE <idx> <path>" on success (or "ERROR <reason>") per
    query to its stdout, which we capture in the server log, so we sync on that
    line.

    Returns the "DONE ..." line, or None if no live server (the caller may fall
    back to a one-shot run). Raises on a server-reported ERROR or timeout.
    """
    paths = _server_paths(size)
    if not query_server_running(size):
        return None
    pid = json.loads(paths["info"].read_text())["pid"]
    off = paths["out"].stat().st_size if paths["out"].exists() else 0

    try:
        wfd = os.open(str(paths["fifo"]), os.O_WRONLY | os.O_NONBLOCK)
    except OSError as e:
        if e.errno == errno.ENXIO:   # no reader -> server gone
            return None
        raise
    try:
        os.write(wfd, f"{int(query_index)} {threshold}\n".encode())
    finally:
        os.close(wfd)

    line = _tail_until(paths["out"], off,
                       lambda l: l.startswith("DONE") or l.startswith("ERROR"),
                       timeout, pid=pid)
    if line is None:
        raise RuntimeError("query server died or timed out waiting for result")
    if line.startswith("ERROR"):
        raise RuntimeError(f"query server reported: {line}")
    return line


def stop_query_server(size):
    """Stop the persistent server for this instance (polite quit, then kill)."""
    paths = _server_paths(size)
    pid = None
    if paths["info"].exists():
        try:
            pid = json.loads(paths["info"].read_text()).get("pid")
        except (OSError, ValueError):
            pid = None
    if pid and _pid_alive(pid):
        try:
            wfd = os.open(str(paths["fifo"]), os.O_WRONLY | os.O_NONBLOCK)
            os.write(wfd, b"quit\n")
            os.close(wfd)
        except OSError:
            pass
        for _sig in (None, signal.SIGTERM, signal.SIGKILL):
            if _sig is not None and _pid_alive(pid):
                try:
                    os.kill(pid, _sig)
                except OSError:
                    pass
            for _ in range(50):
                if not _pid_alive(pid):
                    break
                time.sleep(0.1)
            if not _pid_alive(pid):
                break
    for key in ("fifo", "info"):
        try:
            if paths[key].exists():
                paths[key].unlink()
        except OSError:
            pass


def _spawn_teardown_watchdog(size, harness_pid, server_pid):
    """Launch a detached watchdog that stops the server when the harness exits.

    Keeps server lifecycle entirely inside the submission: no server_stop call
    is needed from run_submission.py, so the harness can be origin/main as-is.
    Detached (start_new_session) so it outlives the harness's process group and
    can still act after a Ctrl-C / SIGKILL that takes the harness down.
    """
    paths = _server_paths(size)
    log = open(paths["dir"] / "watchdog.log", "wb")
    try:
        subprocess.Popen(
            [sys.executable, os.path.abspath(__file__), "--teardown-watchdog",
             str(harness_pid), str(size), str(server_pid)],
            stdin=subprocess.DEVNULL, stdout=log, stderr=log,
            start_new_session=True,
        )
    finally:
        log.close()


def _run_teardown_watchdog(harness_pid, size, server_pid, poll=1.0):
    """Poll until the harness PID is gone, then tear down our server.

    Only stops the server if it is STILL the one we were started for (the live
    info.json pid matches server_pid), so a watchdog left over from a previous
    run can never kill a newer server for the same instance.
    """
    while _pid_alive(harness_pid):
        if not _pid_alive(server_pid):
            return  # our server already died; nothing to tear down
        time.sleep(poll)

    paths = _server_paths(size)
    cur = None
    if paths["info"].exists():
        try:
            cur = json.loads(paths["info"].read_text()).get("pid")
        except (OSError, ValueError):
            cur = None
    if cur == server_pid:
        stop_query_server(size)


# Dataset parameters (must match harness/params.py)
RECORD_DIMS = {0: 128, 1: 128, 2: 256, 3: 512}
DB_SIZES = {0: 1000, 1: 50000, 2: 1000000, 3: 20000000}



def get_record_dim(size: int) -> int:
    return RECORD_DIMS[size]


def get_db_size(size: int) -> int:
    return DB_SIZES[size]


def parse_harness_args():
    """Parse standard harness CLI arguments."""
    parser = argparse.ArgumentParser()
    parser.add_argument('size', type=int)
    parser.add_argument('--count_only', action='store_true')
    parser.add_argument('--seed', type=int)
    args, _ = parser.parse_known_args()
    return args


def write_origin_file(filepath: Path, vectors: np.ndarray):
    """
    Write vectors in the engine's origin binary format.
    Format: [uint32 data_size][uint32 vector_dim][float64 vectors...]
    """
    import numpy as np
    data_size = np.uint32(vectors.shape[0])
    vector_dim = np.uint32(vectors.shape[1])
    filepath.parent.mkdir(parents=True, exist_ok=True)
    with open(filepath, 'wb') as f:
        f.write(struct.pack('<I', int(data_size)))
        f.write(struct.pack('<I', int(vector_dim)))
        vectors.astype(np.float64).tofile(f)


def write_payload_file(filepath: Path, payloads: np.ndarray):
    """
    Write payloads in the engine's payload binary format.
    Format: [uint32 data_size][uint64 payloads...]
    """
    import numpy as np
    data_size = np.uint32(len(payloads))
    filepath.parent.mkdir(parents=True, exist_ok=True)
    with open(filepath, 'wb') as f:
        f.write(struct.pack('<I', int(data_size)))
        payloads.astype(np.uint64).tofile(f)


if __name__ == "__main__":
    # Internal entry point for the detached query-server teardown watchdog
    # (see _spawn_teardown_watchdog). Not part of the harness step interface.
    if len(sys.argv) == 5 and sys.argv[1] == "--teardown-watchdog":
        _run_teardown_watchdog(int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))
