# syntax=docker/dockerfile:1
#
# Docker image for running the Fetch-by-Similarity submission (harness + the
# vendored `juvia` engine).
#
# IMPORTANT: build with the REPOSITORY ROOT as the build context (the image
# needs harness/, datasets/, scripts/ in addition to submission/):
#
#   docker build -f submission/Dockerfile -t fhe-fetch-by-similarity .
#
# BuildKit reads submission/Dockerfile.dockerignore (the per-Dockerfile ignore
# file) for this build, NOT a submission/.dockerignore.
#
# Base: NVIDIA CUDA 13.0 *devel* on Ubuntu 24.04 — matches the host
# (CUDA 13.0.2, driver 580.x, Blackwell sm_120 / RTX 5090). The `-devel` flavor
# ships nvcc + CUDA Toolkit, needed to build the task executables and to link
# against the prebuilt libjuvia. Adjust the tag to an available nvidia/cuda
# 13.0.x-devel-ubuntu24.04 image if 13.0.1 is not published.
FROM nvidia/cuda:13.0.1-devel-ubuntu24.04

ENV DEBIAN_FRONTEND=noninteractive

# Build toolchain for scripts/build_task.sh (CUDA Toolkit already in the base):
#   - build-essential : g++ + OpenMP (libgomp)  -> find_package(OpenMP)
#   - cmake (>=3.19)   : Ubuntu 24.04 ships 3.28
#   - git, ca-certs    : CPM fetches nlohmann/json at configure time
# Python + numpy to run the harness wrappers (requirements.txt: numpy>=1.20;
# Ubuntu 24.04 python3-numpy is 1.26, which satisfies it).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
        libgomp1 \
        python3 \
        python3-pip \
        python3-numpy \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/fhe-benchmark-fetch-by-similarity

# Copy the whole project (build context = repo root) into the image. See
# submission/Dockerfile.dockerignore for what is excluded (regenerated io/,
# .git, build caches) to keep the image small.
COPY . .

# Pre-build the task executables from the vendored engine (submission/lib +
# submission/include + submission/export). No GPU needed to compile/link; CMake
# fetches nlohmann/json via CPM, so network access is required during `docker build`.
RUN ./scripts/build_task.sh

# Run with GPU access, e.g.:
#   docker run --rm --gpus all fhe-fetch-by-similarity \
#       python3 harness/run_submission.py 1 --count_only
#   docker run --rm --gpus all fhe-fetch-by-similarity \
#       python3 harness/run_submission.py 2 --num_runs 3
CMD ["/bin/bash"]
