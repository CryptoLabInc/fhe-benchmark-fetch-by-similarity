// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#pragma once

// Foundation header for juvia's public API: HEaaN2-free scalar typedefs,
// the opaque move-only handle types that hide HEaaN2 ciphertext / key
// objects behind a pimpl, plus the OpenMP pragma helpers and JUVIA_API macro.
// (Merges the former JuviaTypes.hpp + OMPMacro.hpp + Export.hpp + JuviaCrypto.hpp.)

#include <complex>
#include <cstdint>
#include <memory>
#include <omp.h>

namespace JUVIA {

// --- Scalar typedefs -----------------------------------------------------
using u128 = unsigned __int128;
using i128 = __int128;
using u64 = std::uint64_t;
using i64 = std::int64_t;
using u32 = std::uint32_t;
using i32 = std::int32_t;
using u8 = std::uint8_t;
using Real = double;
using Complex = std::complex<double>;

// --- Opaque, move-only handle types --------------------------------------
// Hide HEaaN2 ciphertext / secret-key objects behind a pimpl.  Consumers
// receive these from the juvia API and only forward them between calls --
// they never inspect the contents, so no HEaaN2 header is needed.  Special
// members are declared here and defined out-of-line in libjuvia.so
// (JuviaCryptoImpl.cpp), so std::unique_ptr<Impl> compiles against an incomplete
// Impl on the consumer side.
#define JUVIA_OPAQUE_VALUE(Name)                                                                                       \
    class Name {                                                                                                       \
    public:                                                                                                            \
        Name();                                                                                                        \
        ~Name();                                                                                                       \
        Name(Name &&) noexcept;                                                                                        \
        Name &operator=(Name &&) noexcept;                                                                             \
        Name(const Name &) = delete;                                                                                   \
        Name &operator=(const Name &) = delete;                                                                        \
        struct Impl;                                                                                                   \
        std::unique_ptr<Impl> p_;                                                                                      \
    }

// One HEaaN2 ciphertext.
JUVIA_OPAQUE_VALUE(Ciphertext);
// A flat vector of HEaaN2 ciphertexts.
JUVIA_OPAQUE_VALUE(CiphertextVec);
// A batch (vector of vectors) of HEaaN2 ciphertexts (the runComputeLoop result).
JUVIA_OPAQUE_VALUE(CiphertextBatch);
// One HEaaN2 secret key.
JUVIA_OPAQUE_VALUE(SecretKeyHandle);

#undef JUVIA_OPAQUE_VALUE

} // namespace JUVIA

// --- OpenMP pragma helpers -----------------------------------------------
#define STR(x) #x
#define STRINGIFY(x) STR(x)
#define CONCATENATE(X, Y) X(Y)
#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#define PRAGMA(x) __pragma(x)
#else
#define PRAGMA(x) _Pragma(STRINGIFY(x))
#endif

// --- Shared-library export macro -----------------------------------------
#ifndef JUVIA_API
#ifdef JUVIA_EXPORTS
/* We are building this library */
#ifdef _WIN32
#define JUVIA_API __declspec(dllexport)
#else
#define JUVIA_API __attribute__((visibility("default")))
#endif
#else // JUVIA_EXPORTS
/* We are using this library */
#ifdef _WIN32
#define JUVIA_API __declspec(dllimport)
#else
#define JUVIA_API __attribute__((visibility("default")))
#endif
#endif // JUVIA_EXPORTS
#endif // JUVIA_API
