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
// --- Payload layout ------------------------------------------------------------------
// INPUT ("canonical" / on-disk) payload = inputPayloadDim() channels of inputPayloadBit()
// bits each (default 16 x 4 = 64 bits/record).  BOTH the channel count and the per-channel bit
// width come from config.json (INPUT_PAYLOAD_DIM / INPUT_PAYLOAD_BIT), read once by loadJson;
// the defaults below reproduce the historical 16 x 4 layout.  The external datagen writes this
// layout (it reads the same two config values), so one generated dataset serves every preset.
//
// INTERNAL (in-memory, pipeline-consumed) payload = payloadNumChannels(preset) channels of
// payloadBitSize(preset) bits each.  The per-channel bit width is FIXED per preset (a build
// constant): small uses wide PAYLOAD_BIT_SMALL-bit limbs (single-block FAST PATH), every other
// preset uses PAYLOAD_BIT_MAIN-bit nibbles (the hoist path needs <= 4 bit to fit the juviad12
// 53-bit prime).  The internal CHANNEL COUNT is DERIVED = ceil(total input bits / internal bit
// width), so it follows the configured input layout automatically.  payloadToPresetLayout repacks
// each record's input channels into internal channels at server load time (identity when the two
// layouts coincide, e.g. the default medium 16 x 4 input vs 16 x 4 internal); decrypt inverts with
// payloadUnpackFromInternal back to the input layout, so every preset reports the same format.
// The single-block FAST PATH emits payloadNumChannels("small") output ciphertexts (the FEWEST
// channels that cover the payload, e.g. ceil(64/22) = 3 -- the top limb uses only its low bits).
// The signed codec caps |v| at 2^(bit-1) (2^21 at 22-bit), so the OR-error amplification eta*|v|
// is large; this is safe because the synthetic DB has a wide threshold gap (nearest non-match
// ~0.49, nearest match ~0.89 at thr 0.8) so the cleaner residual eta ~ 1e-7 everywhere and
// 4*eta^3*2^21 << the 0.5 detection gate.  The decode floor payloadSmallMinval scales with the
// bit width to keep the spurious-rejection margin (see below).
//
// NOTE single-word packing: inputPayloadTotalBits() must be <= 64 (the pack concatenates all
// input channels into one uint64); loadJson enforces this.
constexpr uint64_t PAYLOAD_BIT_MAIN = 4;       // internal nibble width (hoist path); default input dim
constexpr uint64_t PAYLOAD_CHANNELS_MAIN = 16; // default input channel count
constexpr uint64_t PAYLOAD_BIT_SMALL = 22;     // internal limb width (small single-block fast path)

// Runtime input payload layout, populated by loadJson from config.json (defaults reproduce the
// historical 16 x 4).  bit = bits per input channel, dim = number of input channels.
inline uint64_t g_input_payload_bit = PAYLOAD_BIT_MAIN;
inline uint64_t g_input_payload_dim = PAYLOAD_CHANNELS_MAIN;
inline uint64_t inputPayloadBit() { return g_input_payload_bit; }
inline uint64_t inputPayloadDim() { return g_input_payload_dim; }
inline uint64_t inputPayloadTotalBits() { return g_input_payload_bit * g_input_payload_dim; }

inline bool isSmallPreset(const std::string &preset) { return preset == "small"; }
// Internal per-channel bit width (fixed per preset).
inline uint64_t payloadBitSize(const std::string &preset) {
    return isSmallPreset(preset) ? PAYLOAD_BIT_SMALL : PAYLOAD_BIT_MAIN;
}
// Internal channel count = ceil(total input bits / internal bit width) -- derived from the
// configured input layout, so the pipeline's num_payload_channels_ follows config.
inline uint64_t payloadNumChannels(const std::string &preset) {
    const uint64_t bits = payloadBitSize(preset);
    return (inputPayloadTotalBits() + bits - 1) / bits;
}

// Repack one record between the input channels (inputPayloadDim x inputPayloadBit bits, each
// in [0, 2^inputPayloadBit)) and num_internal_channels limbs of internal_bits each: concatenate
// the input channels LSB-first into one 64-bit word and slice it into limbs (and the inverse).
// Generalizes the former fixed 16 x 4 <-> 3 x 22 pack.  Limbs may be 0 (input channels can be 0);
// the signed codec below maps the whole limb range bijectively onto nonzero signed values, so a
// 0-valued limb still encodes to a detectable nonzero value.  Requires inputPayloadTotalBits()
// <= 64 (single-word packing; enforced in loadJson).
inline void payloadPackToInternal(const uint64_t *in_ch, uint64_t *limb, uint64_t internal_bits,
                                  uint64_t num_internal_channels) {
    const uint64_t in_bits = inputPayloadBit();
    const uint64_t in_channels = inputPayloadDim();
    const uint64_t imask = (in_bits >= 64) ? ~0ULL : ((1ULL << in_bits) - 1);
    const uint64_t lmask = (internal_bits >= 64) ? ~0ULL : ((1ULL << internal_bits) - 1);
    uint64_t packed = 0;
    for (uint64_t k = 0; k < in_channels; ++k)
        packed |= (in_ch[k] & imask) << (in_bits * k);
    for (uint64_t j = 0; j < num_internal_channels; ++j)
        limb[j] = (packed >> (internal_bits * j)) & lmask;
}
inline void payloadUnpackFromInternal(const uint64_t *limb, uint64_t *in_ch, uint64_t internal_bits,
                                      uint64_t num_internal_channels) {
    const uint64_t in_bits = inputPayloadBit();
    const uint64_t in_channels = inputPayloadDim();
    const uint64_t imask = (in_bits >= 64) ? ~0ULL : ((1ULL << in_bits) - 1);
    const uint64_t lmask = (internal_bits >= 64) ? ~0ULL : ((1ULL << internal_bits) - 1);
    uint64_t packed = 0;
    for (uint64_t j = 0; j < num_internal_channels; ++j)
        packed |= (limb[j] & lmask) << (internal_bits * j);
    for (uint64_t k = 0; k < in_channels; ++k)
        in_ch[k] = (packed >> (in_bits * k)) & imask;
}

// Convert the input-layout record-major payload (inputPayloadDim values/record, the on-disk
// format the datagen writes) into the per-preset IN-MEMORY layout the pipeline consumes: repack
// each record's input channels into payloadNumChannels(preset) limbs of payloadBitSize(preset)
// bits (payloadPackToInternal).  Identity when the input layout already equals the internal
// layout (e.g. the default medium 16 x 4 input vs 16 x 4 internal).  Called once at server load
// (RetrievalPipelineImpl::prepareResources) so num_payload_channels_ (= payloadNumChannels(preset))
// indexes the returned vector directly.
inline std::vector<uint64_t> payloadToPresetLayout(const std::vector<uint64_t> &canon,
                                                   const std::string &preset) {
    const uint64_t in_channels = inputPayloadDim();
    const uint64_t internal_bits = payloadBitSize(preset);
    const uint64_t internal_channels = payloadNumChannels(preset);
    if (in_channels == internal_channels && inputPayloadBit() == internal_bits)
        return canon; // input layout already equals the internal layout
    const uint64_t records = canon.size() / in_channels;
    std::vector<uint64_t> out(static_cast<size_t>(records) * internal_channels, 0);
    for (uint64_t r = 0; r < records; ++r)
        payloadPackToInternal(&canon[static_cast<size_t>(r) * in_channels],
                              &out[static_cast<size_t>(r) * internal_channels], internal_bits,
                              internal_channels);
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

    // Input payload layout: bits per channel (DIM) and number of channels (CHANNEL).  Drives the
    // datagen's on-disk format AND the pipeline's derived internal channel count
    // (payloadNumChannels = ceil(DIM*CHANNEL / internal_bits)).  Absent -> historical 16 x 4.
    if (config.contains("INPUT_PAYLOAD_BIT"))
        g_input_payload_bit = config["INPUT_PAYLOAD_BIT"].get<uint64_t>();
    if (config.contains("INPUT_PAYLOAD_DIM"))
        g_input_payload_dim = config["INPUT_PAYLOAD_DIM"].get<uint64_t>();
    const uint64_t input_total_bits = g_input_payload_bit * g_input_payload_dim;
    if (g_input_payload_bit == 0 || g_input_payload_dim == 0)
        throw std::runtime_error("Error: INPUT_PAYLOAD_BIT / INPUT_PAYLOAD_DIM must be > 0 in " + file_path);
    if (input_total_bits > 64)
        throw std::runtime_error("Error: INPUT_PAYLOAD_BIT * INPUT_PAYLOAD_DIM (" +
                                 std::to_string(input_total_bits) + ") exceeds the 64-bit single-word " +
                                 "packing limit in " + file_path);
    // The hoist (non-small) path packs the internal channels two-per-complex-slot, so its derived
    // channel count must be even (modPack's slots-per-oh = num_payload_channels_ / 2).
    if (target_preset != "small" && target_preset != "all") {
        const uint64_t internal_channels = (input_total_bits + PAYLOAD_BIT_MAIN - 1) / PAYLOAD_BIT_MAIN;
        if (internal_channels % 2 != 0)
            throw std::runtime_error("Error: derived internal payload channel count (" +
                                     std::to_string(internal_channels) + ") must be even for preset '" +
                                     target_preset + "'; pick INPUT_PAYLOAD_BIT*INPUT_PAYLOAD_DIM a multiple of " +
                                     std::to_string(2 * PAYLOAD_BIT_MAIN) + " in " + file_path);
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
