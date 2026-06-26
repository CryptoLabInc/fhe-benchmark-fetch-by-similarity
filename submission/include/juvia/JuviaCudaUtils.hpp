// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#pragma once
#include <iostream>
#include <random>

namespace JUVIA {
void mallocGpu(std::uint64_t **pointer, std::uint64_t size);
void memcpyHostToGpu(std::uint64_t *dest, const std::uint64_t *src, std::uint64_t size);
void memcpyGpuToHost(std::uint64_t *dest, const std::uint64_t *src, std::uint64_t size);

} // namespace JUVIA
