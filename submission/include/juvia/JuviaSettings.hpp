// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#pragma once

// Common scalar typedefs, numeric parameters, filesystem paths, and config
// helpers shared by the task pipeline and the export binaries.
// (Merges the former CommonCnst.hpp + JuviaSettings.hpp.)

#include "JuviaTypes.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>

namespace JUVIA {

// --- Common numeric parameters -------------------------------------------
constexpr Real VEC_SCALER = 0.5;

constexpr u64 LOG_DEGREE12 = 12;
constexpr u64 LOG_DEGREE16 = 16;
constexpr u64 DEGREE12 = 1UL << LOG_DEGREE12;
constexpr u64 DEGREE16 = 1UL << LOG_DEGREE16;
constexpr u64 NUM_COMPOSE = 16;
constexpr u64 DECOMPOSE_BATCH_SIZE = 32;
constexpr u64 COMPUTE_RESULT_BATCH_SIZE = 8;

// Hoist decompose payload scales (log2): the matched slot value p * 2^S1 * 2^S2 must
// fit the juviad12 53-bit prime; tuned 35/14 (~17x readout amplification, vs the old
// 30/10's ~342x).  Selected by the small-payload (PAYLOAD_BIT_SIZE <= 4) hoist path.
constexpr u64 HOIST_INDICATOR_LOG_SCALE = 35;
constexpr u64 HOIST_PAYLOAD_LOG_SCALE = 14;

const Real LOG_SCALE_APPROX = 42.0384;
const Real SCALE_DB_APPROX = std::pow(2.0, 19) * VEC_SCALER;
const Real SCALE_QR_APPROX = std::pow(2.0, LOG_SCALE_APPROX - 19) * VEC_SCALER;

// --- Filesystem paths ----------------------------------------------------
// Every path can be overridden by the environment variable of the same name
// with a JUVIA_ prefix (e.g. JUVIA_SECRETKEY_PATH); unset or empty falls back
// to the default literal.  Read once at static-initialization time, so the
// override must be in the environment before the process starts.
inline std::string pathFromEnv(const char *env_name, const char *default_path) {
    const char *value = std::getenv(env_name);
    return (value != nullptr && *value != '\0') ? value : default_path;
}

const std::string SECRETKEY_PATH = pathFromEnv("JUVIA_SECRETKEY_PATH", "task-sk");
const std::string QUERY_PATH = pathFromEnv("JUVIA_QUERY_PATH", "task-queries");
const std::string PUBLICKEY_PATH = pathFromEnv("JUVIA_PUBLICKEY_PATH", "task-pk");
const std::string RAW_DATA_PATH = pathFromEnv("JUVIA_RAW_DATA_PATH", "data");

const std::string ENCRYPT_DATA_PATH = pathFromEnv("JUVIA_ENCRYPT_DATA_PATH", "task-encrypted");
const std::string ENCRYPT_DATA_NAME = "ctxt";

const std::string RESULT_CIPHERTEXT_PATH_TASK1 = pathFromEnv("JUVIA_RESULT_CIPHERTEXT_PATH_TASK1", "task-results/1");
const std::string RESULT_CIPHERTEXT_PATH_TASK2 = pathFromEnv("JUVIA_RESULT_CIPHERTEXT_PATH_TASK2", "task-results/2");
const std::string DECRYPTED_DATA_PATH_TASK1 = pathFromEnv("JUVIA_DECRYPTED_DATA_PATH_TASK1", "task-decrypted-data/1");
const std::string DECRYPTED_DATA_PATH_TASK2 = pathFromEnv("JUVIA_DECRYPTED_DATA_PATH_TASK2", "task-decrypted-data/2");
const std::string CLEAR_DATA_PATH_TASK1 = pathFromEnv("JUVIA_CLEAR_DATA_PATH_TASK1", "task-clear-data/1");

// DEBUG_START
const std::string DEBUG_PATH_TASK1 = pathFromEnv("JUVIA_DEBUG_PATH_TASK1", "task-debug/1");
const std::string DEBUG_PATH_TASK2 = pathFromEnv("JUVIA_DEBUG_PATH_TASK2", "task-debug/2");
// DEBUG_END

// --- Presets / config ----------------------------------------------------
const std::map<std::string, std::pair<uint32_t, uint32_t>> PRESET_MAP = {
    {"small", {128, 50000}}, {"medium", {256, 1000000}},
    //{"large", {512, 20000000}}, // Not supported yey
};
// --- Per-preset payload layout -------------------------------------------------------
// CANONICAL (general / "main") payload = PAYLOAD_CHANNELS_MAIN channels of PAYLOAD_BIT_MAIN
// bits (16 x 4 = 64 bits/record).  The external datagen ALWAYS writes this canonical layout,
// for EVERY preset, so one generated dataset serves all presets.  The multi-block hoist path
// (medium) consumes it directly.  The small preset's single-block FAST PATH emits only
// PAYLOAD_CHANNELS_SMALL ciphertexts, so the SERVER repacks the same 64-bit payload into
// PAYLOAD_CHANNELS_SMALL channels of PAYLOAD_BIT_SMALL bits (3 x 22 = 66 >= 64, the FEWEST
// channels that cover 64 bits -- the top limb uses only 20 of its 22 bits) IN MEMORY at load
// time (payloadToPresetLayout -> payloadPackToSmall); decrypt recovers it with
// payloadUnpackFromSmall back to the canonical 16 x 4 -- so both presets report the same format.
// payloadBitSize / payloadNumChannels pick the in-memory layout from the preset, so ONE build
// serves both.  3 channels (vs 4 x 16) emits ONE FEWER output ciphertext (-25% files).  The
// 22-bit signed codec caps |v| at 2^21, so the OR-error amplification eta*|v| is 2^6 = 64x larger
// than 16-bit; this is safe because the synthetic DB has a wide threshold gap (nearest non-match
// ~0.49, nearest match ~0.89 at thr 0.8) so the cleaner residual eta ~ 1e-7 everywhere and
// 4*eta^3*2^21 << the 0.5 detection gate.  The decode floor payloadSmallMinval scales with the
// bit width to keep the spurious-rejection margin (see below).  NOTE the top limb (bits[44:64],
// only 20 bits, value < 2^20 < 2^21=half) ALWAYS signed-encodes to [-2^21,-2^20], so it is a
// guaranteed-large self-reference channel: a true match's slot_max >= ~2^20 regardless of payload.
constexpr uint64_t PAYLOAD_BIT_MAIN = 4;
constexpr uint64_t PAYLOAD_CHANNELS_MAIN = 16;
constexpr uint64_t PAYLOAD_BIT_SMALL = 22;
constexpr uint64_t PAYLOAD_CHANNELS_SMALL = 3;

inline bool isSmallPreset(const std::string &preset) { return preset == "small"; }
inline uint64_t payloadBitSize(const std::string &preset) {
    return isSmallPreset(preset) ? PAYLOAD_BIT_SMALL : PAYLOAD_BIT_MAIN;
}
inline uint64_t payloadNumChannels(const std::string &preset) {
    return isSmallPreset(preset) ? PAYLOAD_CHANNELS_SMALL : PAYLOAD_CHANNELS_MAIN;
}

// Repack one record between the canonical 16 x PAYLOAD_BIT_MAIN nibbles (each in
// [0, 2^PAYLOAD_BIT_MAIN), full 4-bit range) and PAYLOAD_CHANNELS_SMALL limbs of
// PAYLOAD_BIT_SMALL bits: concatenate the nibbles into one 64-bit word and slice it into limbs.
// Limbs may be 0 (nibbles can be 0); the signed codec below maps the whole limb range
// [0, 2^PAYLOAD_BIT_SMALL) bijectively onto nonzero signed values, so a 0-valued limb still
// encodes to a detectable nonzero value.  4 x 16 = 64 is an EXACT cover of the 64-bit payload
// (each limb is exactly 4 nibbles; no spare/unused bits).
inline void payloadPackToSmall(const uint64_t *nib, uint64_t *limb) {
    const uint64_t nmask = (1ULL << PAYLOAD_BIT_MAIN) - 1;
    const uint64_t lmask = (1ULL << PAYLOAD_BIT_SMALL) - 1;
    uint64_t packed = 0;
    for (uint64_t k = 0; k < PAYLOAD_CHANNELS_MAIN; ++k)
        packed |= (nib[k] & nmask) << (PAYLOAD_BIT_MAIN * k);
    for (uint64_t j = 0; j < PAYLOAD_CHANNELS_SMALL; ++j)
        limb[j] = (packed >> (PAYLOAD_BIT_SMALL * j)) & lmask;
}
inline void payloadUnpackFromSmall(const uint64_t *limb, uint64_t *nib) {
    const uint64_t nmask = (1ULL << PAYLOAD_BIT_MAIN) - 1;
    const uint64_t lmask = (1ULL << PAYLOAD_BIT_SMALL) - 1;
    uint64_t packed = 0;
    for (uint64_t j = 0; j < PAYLOAD_CHANNELS_SMALL; ++j)
        packed |= (limb[j] & lmask) << (PAYLOAD_BIT_SMALL * j);
    for (uint64_t k = 0; k < PAYLOAD_CHANNELS_MAIN; ++k)
        nib[k] = (packed >> (PAYLOAD_BIT_MAIN * k)) & nmask;
}

// Convert the canonical record-major MAIN payload (PAYLOAD_CHANNELS_MAIN nibbles/record, the
// uniform on-disk format the datagen writes for every preset) into the per-preset IN-MEMORY
// layout the pipeline consumes: the small preset packs each record's PAYLOAD_CHANNELS_MAIN
// nibbles into PAYLOAD_CHANNELS_SMALL limbs (payloadPackToSmall); every other preset keeps the
// canonical layout unchanged.  Called once at server load (RetrievalPipelineImpl::prepareResources)
// so num_payload_channels_ (= payloadNumChannels(preset)) indexes the returned vector directly.
inline std::vector<uint64_t> payloadToPresetLayout(const std::vector<uint64_t> &canon,
                                                   const std::string &preset) {
    if (!isSmallPreset(preset))
        return canon;
    const uint64_t records = canon.size() / PAYLOAD_CHANNELS_MAIN;
    std::vector<uint64_t> out(static_cast<size_t>(records) * PAYLOAD_CHANNELS_SMALL, 0);
    for (uint64_t r = 0; r < records; ++r)
        payloadPackToSmall(&canon[static_cast<size_t>(r) * PAYLOAD_CHANNELS_MAIN],
                           &out[static_cast<size_t>(r) * PAYLOAD_CHANNELS_SMALL]);
    return out;
}

// Signed payload codec for the single-block FAST PATH (small preset).  BIJECTIVELY maps a limb
// p in [0, 2^bit_size) onto a NONZERO signed value in [-2^(bit-1), 2^(bit-1)] \ {0} -- the SIGNED
// value 0 is reserved as the non-match marker (never produced by a real limb):
//     0 <= p < 2^(bit-1)  ->  p - 2^(bit-1)      (negative half: -2^(bit-1) .. -1; p=0 -> -2^(bit-1))
//     p >= 2^(bit-1)      ->  p - 2^(bit-1) + 1  (positive half: 1 .. 2^(bit-1))
// 2^bit_size limb values <-> 2^bit_size nonzero signed values (bijection).  max|v| = 2^(bit-1),
// 1 bit below a +1 offset (~1 bit of both value precision and self-marking detection margin).
// EVERY limb -- including 0 -- maps to a nonzero value, so a matched slot always reads
// |value| >= 1 (self-marking, no reference channel) for ANY payload; a non-match slot reads ~0
// (OR ~ 0) = the reserved marker.  The deg-4 sharpener in computeResultsDirectFast shrinks the
// cleaner residual on top of this.  bit_size = payloadBitSize(preset).
inline double payloadEncodeSigned(double p, uint64_t bit_size) {
    const double half = static_cast<double>(1ULL << (bit_size - 1));
    return (p < half) ? (p - half) : (p - half + 1.0);
}
inline double payloadDecodeSigned(double v, uint64_t bit_size) {
    const double half = static_cast<double>(1ULL << (bit_size - 1));
    return (v < 0.0) ? (v + half) : (v + half - 1.0);
}

inline double payloadSmallMinval(uint64_t bit_size) {
    return 2.0 * std::pow(2.0, static_cast<double>(bit_size) - 16.0);
}

const uint64_t MAX_QUERY_ID = 10000;

const uint64_t K = 32;
const uint64_t B = 32; // upper bound of the number of matches, should be in [5, 32]

inline nlohmann::json loadJson(const std::string &file_path) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Error: Could not open " + file_path);
    }
    nlohmann::json config;
    ifs >> config;

    const std::string target_preset = config["PRESET"];
    const std::set<int> device_ids(config["DEVICE_IDS"].begin(), config["DEVICE_IDS"].end());

    if (device_ids.empty()) {
        throw std::runtime_error("Error: device_ids is empty in " + file_path);
    }

    if ((target_preset != "small" && target_preset != "medium" && target_preset != "large" && target_preset != "all")) {
        std::cerr << "Available presets: small, medium, large, all" << std::endl;
        throw std::runtime_error("Error: Invalid data preset in " + file_path);
    }

    return config;
}

// Per-preset cleaner variant.  The cleaner that sharpens the match indicator is
// preset-dependent: the medium (d=256) preset uses the gap-minimax STEP cleaner
// ("stepg"); small (d=128) and large (d=512) use the W5∘H∘H composite ("w5hh")
// whose STEP coeffs are not yet refit for those dimensions.  This is a build-time
// constant (moved out of config.json) -- it is fixed per dimension, not a tunable.
// cleanerForPreset returns "" for an unknown preset so the pipeline keeps its
// built-in dimension-based default.
const std::map<std::string, std::string> PRESET_CLEANER_MAP = {
    {"small", "w5hh"},
    {"medium", "stepg"},
    {"large", "w5hh"},
};
inline std::string cleanerForPreset(const std::string &preset) {
    const auto it = PRESET_CLEANER_MAP.find(preset);
    return (it != PRESET_CLEANER_MAP.end()) ? it->second : "";
}

// Read the payload .bin (uint32 count, then that many uint64 payloads).  Shared
// by the encrypt (JuviaPipeline.cpp) and retrieval-decrypt (RetrievalPipelineImpl.cpp) paths.
inline std::vector<uint64_t> readPayloadFile(const std::string &filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    std::vector<uint64_t> payloads;
    uint32_t data_size;
    file.read(reinterpret_cast<char *>(&data_size), sizeof(uint32_t));
    payloads.resize(data_size);
    file.read(reinterpret_cast<char *>(payloads.data()), static_cast<std::streamsize>(data_size) * sizeof(uint64_t));
    file.close();
    return payloads;
}
} // namespace JUVIA
