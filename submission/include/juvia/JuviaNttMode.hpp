// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#pragma once

namespace JUVIA {

enum class NttMode { MIN, DIRECT };

// Defined in NttMode.cpp — one definition shared across all translation units.
void setNttMode(NttMode mode);
NttMode getNttMode();

} // namespace JUVIA
