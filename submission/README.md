# Fetch-by-Similarity Submission

This document describes CryptoLab's submission to the Fetch-by-Similarity FHE benchmarking workload, built on the HEaaN2 homomorphic encryption library.

The submission consists of thin Python wrappers under `submission/` that bridge the harness interface to a set of GPU-accelerated C++ executables built from the vendored `juvia` engine (prebuilt `libjuvia.so` under `submission/lib`, headers under `submission/include`, task sources under `submission/export`). All performance numbers reported here correspond to an end-to-end execution of the harness.

The server-side compute executable runs in a persistent **server mode**: it is launched once, loads the evaluation keys and encrypted database a single time, and then processes each query streamed to it over stdin. So when the harness runs multiple queries against the same database, the multi-second key/DB load is paid **once** and every subsequent query only pays for the homomorphic computation itself.

---

## Database Configuration

* **Mode:** SMALL / MEDIUM
* **Database size:** 50,000 / 1,000,000 records
* **Record dimension:** 128 / 256
* **Payload dimension:** 7 (harness payload of 7 int16 channels)

The submission evaluates encrypted similarity queries against a fixed encrypted database. In `--count_only` mode it returns the number of matches above the threshold (0.8); without `--count_only` it returns the matching payloads (up to `K = 32` per query).

The harness `toy` preset (size 0) and `large` preset (size 3) are **not** supported — this submission targets small and medium with GPU workloads.

Assume that the similarity score value is not within [0.703, 0.845] when dim = 128, and is not within [0.538, 0.866] when dim = 256.

---

## Execution environment

**Client / Server (single machine)**

* CPU: x86\_64
* GPU: NVIDIA Blackwell architectures (single GPU sufficient for small(VRAM over 11GB) and medium(VRAM over 17GB) preset)
* RAM: 64 GB+
* CUDA toolkit + OpenMP

**Supported GPU architectures.** The prebuilt `libjuvia` is compiled for the `sm-120` CUDA compute capabilities. (GeForce RTX 50-series and RTX Pro 6000 Blackwell)

The engine executables run the client and server stages in the same process tree. Each executable runs with its working directory set to the harness I/O directory for the instance (`io/{instance}/`, e.g. `io/small`), so every file it reads or writes lands in one place: the client-local artifacts (secret key under `secret-keys/`, decrypted output under `task-decrypted-data/`) and the client↔server wire data (evaluation keys under `keys/`, encrypted database and query under `encrypted/`, result ciphertexts under `results/`). The Python wrappers only translate between the harness data formats and the engine's formats — reading the harness dataset/query from `datasets/{instance}/` and writing the final `results.bin` back into `io/{instance}/`. The compiled executables and the prebuilt library stay in `submission/` (`submission/build/export/`, `submission/lib/`); they are located by absolute path, so their working directory is free to be the I/O directory.

**Persistent query server.** `server_preprocess_dataset.py` starts the compute executable in server mode (detached) before the query loop, so the evaluation keys, encrypted DB and bootstrapper are loaded exactly once. Each `server_encrypted_compute.py` invocation then streams the current query (`<index> <threshold>`) to that running process — through a FIFO opened read-write so the server never sees EOF between queries — and waits for its `DONE` line. After the last query, `server_stop.py` shuts the server down. The engine re-reads the encrypted-query file on every query, so each iteration simply overwrites it (index 0) and signals the server.

---

## Cryptographic parameters and security

The submission uses **CKKS** (Cheon-Kim-Kim-Song) as provided by the HEaaN2 library.


### Parameters

We use the following parameters for encrypting the input (Parameter #1), data and decrypting the result ciphertext (Parameter #2) for fetch-payload, Medium configuration, and decryption for rest of the workloads and main computations (Parameter #3). The other one (Parameter #4) is only used in the bootstrapping when the technique called "Sparse Secret Encapsulation (SSE)" is applied. 

| No. | Degree       | Modulus budget (bits) | Hamming weight | Used for                        | Security level (bits) |
|----:|:-------------|----------------------:|---------------:|-------------------------------- | ---------------------:|  
| 1   | 2¹² (4096)   |                   106 |           2730 | Encryption                      |                 128.9 |
| 2   | 2¹² (4096)   |                   106 |           2730 | Decryption                      |                 128.9 |
| 3   | 2¹⁶ (65536)  |                  1721 |           1024 | Decryption and Main Computation |                 128.1 |
| --- | -----------  | --------------------- | -------------- | ------------------------------- | --------------------- |
| 4   | 2¹⁴ (16384)  |                    82 |             32 | Bootstrapping (SSE)             |                 128.1 |

All the security level results are measured with the lattice estimator (https://github.com/malb/lattice-estimator) with the most up-to-date commit `27a581b` at the moment of the writting (June 19, 2026).


---

## Pipeline structure

The benchmark is decomposed into the standard harness stages. Each Python wrapper in `submission/` invokes the corresponding C++ binary built under the `submission/build/export/` directory:

| Harness stage                     | Python wrapper                         | Engine executable                 |
|-----------------------------------|----------------------------------------|-----------------------------------|
| Dataset preprocessing             | `client_preprocess_dataset.py`         | (format conversion only)          |
| Key generation                    | `client_key_generation.py`             | `task-keygen`              |
| DB encoding & encryption          | `client_encode_encrypt_db.py`          | `task-encrypt-data`        |
| Server setup (start query server) | `server_preprocess_dataset.py`         | launches `task1-count-matches` / `task2-return-k-matches` in **server mode** — loads keys/DB/bootstrapper once, then waits for queries |
| Query preprocessing               | `client_preprocess_query.py`           | (format conversion only)          |
| Query encryption                  | `client_encode_encrypt_query.py`       | `task-encrypt-query`       |
| Encrypted computation (per query) | `server_encrypted_compute.py`          | streams `<query> <threshold>` to the running server and waits for the result |
| Decryption                        | `client_decrypt_decode.py`             | `task1-decrypt` (count_only) / `task2-decrypt` (payload) |
| Postprocessing                    | `client_postprocess.py`                | (result format conversion)        |
| Server teardown (after last query)| `server_stop.py`                       | stops the query server             |

### Encrypted-search algorithm

The keys, bootstrapper and encrypted database are loaded once when the server starts. Then, for each streamed query, the server pipeline performs:

1. **Inner product / cosine similarity** — encrypted matrix-vector product between the encrypted DB and the encrypted query, exploiting CKKS slot packing.
2. **Threshold comparison** — polynomial approximation of an indicator function around the user-supplied threshold (default `0.8`).
3. **Running sum / aggregation** — reduces per-record indicators to a single ciphertext encoding the count of matches.
4. **Output compression** — re-encodes the result into a compact ciphertext for return to the client.

---

## Build & run

```bash
# Prerequisites: CUDA toolkit, OpenMP, CMake 3.19+

# Run the benchmark — small preset, count-only mode
python3 harness/run_submission.py 1 --count_only

# Payload mode (returns up to K=32 matching payloads), medium preset, 3 iteration
python3 harness/run_submission.py 2 --num_runs 3

```

Environment variables:

* `DEVICE_IDS`: comma-separated CUDA device IDs (default `0`)
* `SUBMISSION_ROOT`: project root override (defaults to CWD)

### Docker

A `submission/Dockerfile` builds a self-contained image (harness + the vendored `juvia` engine, prebuilt task executables, Python + numpy).

**Host requirements**

* Docker with BuildKit (Docker 23+; the build uses a per-Dockerfile `.dockerignore`).
* NVIDIA driver + [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/) (for `--gpus`).
* A CUDA Blackwell GPU (*Supported GPU architectures* above).
* Network access **during the build** (CMake fetches `nlohmann/json` via CPM).

**Image contents** (installed on top of `nvidia/cuda:13.0.1-devel-ubuntu24.04`): `build-essential`, `cmake`, `git`, OpenMP, `python3` + `python3-numpy` (numpy ≥ 1.20). The task executables are pre-built during the image build, so the first run starts immediately.

**Build** — run from the **repository root** (the build context must include `harness/`, `datasets/`, `scripts/`, not just `submission/`):

```bash
docker build -f submission/Dockerfile -t fhe-fetch-by-similarity .
```

**Run** (GPU required):

```bash
# count-only, small
docker run --rm --gpus all fhe-fetch-by-similarity \
    python3 harness/run_submission.py 1 --count_only

# payload mode, medium, 3 queries
docker run --rm --gpus all fhe-fetch-by-similarity \
    python3 harness/run_submission.py 2 --num_runs 3
```

Notes:

* The build context is the repo root; `submission/Dockerfile.dockerignore` (the per-Dockerfile ignore that BuildKit reads for `-f submission/Dockerfile`) excludes `.git`, build caches, and the harness-regenerated `io/` (~13 GB, recreated on every run). Delete the `io/` line in it to ship `io/` as-is.
* If the `13.0.1-devel-ubuntu24.04` base tag is unavailable, change it to any published `nvidia/cuda:13.0.x-devel-ubuntu24.04`.
* `--gpus all` is mandatory at run time; without it the container fails once the engine touches CUDA.

---

## Performance characteristics

### Reference server

All numbers below were collected on the following machine:

* **CPU:** AMD Ryzen Threadripper PRO 9955WX (16 cores / 32 threads)
* **GPU:** NVIDIA GeForce RTX 5090 (32 GB) — single GPU used
* **RAM:** 256 GB
* **OS:** Ubuntu Linux 6.8

### Thread configuration

Every step sets `OMP_NUM_THREADS` explicitly:

| Step                                                          | OMP threads |
|---------------------------------------------------------------|------------:|
| Key generation · DB encoding & encryption · query encryption  | 8           |
| Persistent query server (one-time load + per-query compute)   | 4           |
| Decryption (`task1-decrypt` / `task2-decrypt`)                | 1           |

The persistent compute server is launched with 4 OMP threads, so both its one-time load and every per-query computation run at 4 threads; the CPU-only decrypt step runs single-threaded.

### Count only mode (`task1-count-matches`)

End-to-end harness stages — the **last query** of a 3-query run (`measurements/count_small/results-3.json`, `measurements/count_medium/results-3.json`); the first query of a run is slower (cold caches), so the steady-state last query is reported. The compute exe is launched once in server mode, so the key/DB load is a one-time *Server setup* cost and each query's *Encrypted computation* stage is just the streamed homomorphic work:

| Stage                               | OMP | SMALL | MEDIUM |
|-------------------------------------|----:|------:|-------:|
| Key generation                      | 8   | 3.60 s | 3.70 s |
| DB encoding & encryption            | 8   | 0.57 s | 14.75 s |
| Server setup (load keys/DB once)    | 4   | 3.06 s | 4.28 s |
| Query encryption                    | 8   | 0.02 s | 0.02 s |
| **Encrypted computation (server)**  | 4   | **0.040 s** | **0.193 s** |
| Result decryption & postprocessing  | 1   | 0.08 s | 0.08 s |
| **End-to-end total**                |     | **7.88 s** | **28.97 s** |
| Public & evaluation keys            |     | 3.0 GB | 3.0 GB |
| Encrypted database                  |     | 128 MB | 4.0 GB |
| Encrypted query                     |     | 64 KB | 64 KB |
| Encrypted results (output)          |     | 1.0 MB | 1.0 MB |

**Server per-query breakdown** (4 threads; from the engine's `Server Reported` timer, last query — the engine times the whole per-query summary). The homomorphic-compute steps are *Prepare query* (compose the query onto the device), *Scoring* (encrypted inner-product / cosine similarity), and *Compute matching* (match to the 0/1 indicator); *Count matches* reduces the indicators to the match count, and *Load query* / *Save result* are per-query I/O:

| Sub-step               | SMALL | MEDIUM |
|------------------------|------:|-------:|
| Load query ciphertext  | 0.12 ms | 0.12 ms |
| Prepare query          | 0.61 ms | 1.13 ms |
| Scoring                | 0.73 ms | 10.42 ms |
| Compute matching       | 12.27 ms | 152.56 ms |
| Count matches          | 1.39 ms | 1.50 ms |
| Save result            | 1.53 ms | 1.43 ms |
| **Server per-query total** | **16.64 ms** | **167.17 ms** |

> **Why the "Encrypted computation" stage ≠ the server per-query total.** The harness *Encrypted computation* stage (0.040 s / 0.193 s) is larger than the engine's per-query total above (16.64 ms / 167.17 ms) by **≈ 23–26 ms**. That gap is per-query **Python wrapper overhead**, not server time: each harness step is a fresh Python process that imports numpy, then the wrapper writes `config.json`, signals the running server over a FIFO, polls for its `DONE` line, and writes the measurement JSON. It is roughly constant across presets.

### Payload mode (`task2-return-k-matches`) 

Payload mode runs `task2-return-k-matches` on the server and `task2-decrypt` on the client, returning up to `K = 32` matching payloads per query. Each match carries `NUM_PAYLOAD_CHANNELS = 16` payload channels (the harness payload columns, values in `[0, 16)`). The engine's signed payload codec marks every channel — including zero-valued ones — so none are dropped by the decrypt's non-zero filter.

Like count mode, the compute exe runs in **server mode** — launched once (one-time *Server setup*), then each *Encrypted computation* stage streams one query and returns the payloads of the records matching it. Packing the payload columns alongside the record vectors does **not enlarge the encrypted database** (the SMALL DB is 128 MB in both modes — see below).

End-to-end harness stages — the **last query** of a 3-query run (`measurements/small/results-3.json`, `measurements/medium/results-3.json`):

| Stage                               | OMP | SMALL | MEDIUM |
|-------------------------------------|----:|------:|-------:|
| Key generation                      | 8   | 3.59 s | 3.63 s |
| DB encoding & encryption            | 8   | 0.49 s | 14.43 s |
| Server setup (load keys/DB once)    | 4   | 3.04 s | 5.51 s |
| Query encryption                    | 8   | 0.02 s | 0.02 s |
| **Encrypted computation (server)**  | 4   | **0.040 s** | **0.299 s** |
| Result decryption & postprocessing  | 1   | 0.09 s | 0.07 s |
| **End-to-end total**                |     | **7.79 s** | **29.95 s** |
| Public & evaluation keys            |     | 3.0 GB | 3.0 GB |
| Encrypted database                  |     | 128 MB | 4.0 GB |
| Encrypted query                     |     | 64 KB | 64 KB |
| Encrypted results (output)          |     | 3.0 MB | 128 KB |

> Note the output ciphertext sizes: SMALL's fast path emits per-channel ciphertexts at the high degree (≈ 3.0 MB), whereas MEDIUM's full path packs the matches into compact low-degree RLWE result files (≈ 128 KB).

**Server per-query breakdown** (4 threads; from the engine's `Server Reported` timer, last query). The path depends on the DB block count: SMALL (≤ 65,536 records — a single DB block) takes the **single-block fast path** (`computeResultsDirectFast`), multiplying the threshold indicator by the payload directly and skipping the OR re-bootstrap, one-hot extraction, and decompose/modPack; MEDIUM (multiple DB blocks) runs the **full path** (one-hot extraction → payload multiply → LWE decompose/unpack → modPack). In the breakdown, *Prepare query*, *Scoring*, and *Compute matching* are the homomorphic-compute steps shared with count mode; *Compute payload* is the task2-specific payload gather (the fast-path direct multiply, or the full-path one-hot/decompose/modPack pipeline); *Load query* and *Save result ciphertexts* are per-query I/O:

| Sub-step                          | SMALL (single-block fast path) | MEDIUM (multi-block full path) |
|-----------------------------------|------:|-------:|
| Load query ciphertext             | 0.13 ms | 0.13 ms |
| Prepare query                     | 0.62 ms | 1.13 ms |
| Scoring                           | 0.74 ms | 10.45 ms |
| Compute matching                  | 13.00 ms | 155.54 ms |
| Compute payload                   | 0.58 ms | 107.54 ms |
| Save result ciphertexts           | 4.01 ms | 0.38 ms |
| **Server per-query total**        | **19.08 ms** | **275.18 ms** |

As in count mode, the harness *Encrypted computation* stage (0.040 s / 0.299 s) exceeds the engine's per-query total (19.08 ms / 275.18 ms) by **≈ 21–24 ms** of per-query Python wrapper overhead (fresh Python process + numpy import + FIFO poll + config / result / JSON I/O), independent of the server work. The dominant cost is `Compute matching` at SMALL; at MEDIUM it is `Compute matching` followed by the `Compute payload` gather (≈ 108 ms — the multi-block one-hot/decompose/modPack pipeline).

---

## Mode support summary

| Mode                                  | Status                                |
|---------------------------------------|---------------------------------------|
| `--count_only` (count matches)        | Fully supported                       |
| Payload return (task2)                | Supported (up to K=32 matches/query)  |
| Sizes 1 / 2 (small / medium)          | Supported                             |
| Size 0 (toy) / Size 3 (large)         | Not supported                         |

---

## Contact

* Security Development Team, CryptoLab, Inc. — `sec@cryptolab.co.kr`
