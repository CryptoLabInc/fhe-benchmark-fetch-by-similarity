// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#pragma once

// PUBLIC, HEaaN2-free API for the export task binaries: the shared search facade
// (MatchPipeline), return-k-matches orchestration (RetrievalPipeline), and the
// keygen/encrypt/decrypt helpers.  HEaaN2 state lives in JUVIA::hidden::* (not
// shipped); the public surface uses opaque handles + deb types, so consumers
// build with no HEaaN2 header.

#include "JuviaTimer.hpp"
#include "JuviaTypes.hpp"

#include "deb/CKKSTypes.hpp"
#include "deb/Preset.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace JUVIA {

namespace hidden {
class MatchPipelineImpl;
class RetrievalPipelineImpl;
} // namespace hidden

// ============================================================================
// Shared search facade (task1 + task2) -- each method is one timed phase.
// ============================================================================
class MatchPipeline {
public:
    MatchPipeline(std::vector<int> device_ids, std::set<int> device_ids_set, uint32_t vector_dim, uint32_t num_w);
    ~MatchPipeline();
    MatchPipeline(MatchPipeline &&) noexcept;
    MatchPipeline &operator=(MatchPipeline &&) noexcept;
    MatchPipeline(const MatchPipeline &) = delete;
    MatchPipeline &operator=(const MatchPipeline &) = delete;

    void loadEvalKeys(const std::string &publickey_path, bool skip_count_rot_keys = false);
    void setupBootstrapper();
    void setThreshold(Real input_threshold);
    // Select the match-indicator cleaner variant for the active preset (from config).
    void setCleaner(const std::string &cleaner);
    void loadQuery(const std::string &query_path, uint64_t query_index);
    void loadDB(const std::string &db_path);
    void setupInnerProduct();

    // Main compute loop -> opaque batch of match-indicator ciphertexts.
    CiphertextBatch runComputeLoop(JuviaTimer &timer, u64 cleaner_target_level, const std::string &debug_path);

    // task1 post-processing (accumulate -> rotation-sum -> save).
    void accumulateAndSaveCount(CiphertextBatch &quant, JuviaTimer &timer, const std::string &result_path);

private:
    std::unique_ptr<hidden::MatchPipelineImpl> impl_;
    friend class RetrievalPipeline; // builds an internal RetrievalPipeline over impl_
};

// ============================================================================
// Return-k-matches orchestration on a shared MatchPipeline (task2) --
// each method is one timed phase.
// ============================================================================
class RetrievalPipeline {
public:
    static constexpr u32 kCleanerTargetLevel = 2;
    static constexpr u32 kExtractOneHotLevel = 3;
    static constexpr u32 kDirectSharpenLevel = 3;

    // num_payload_channels is derived from the preset (payloadNumChannels), so the preset is
    // the single source of truth for the per-preset payload layout.
    RetrievalPipeline(MatchPipeline &ctx, std::string preset);
    ~RetrievalPipeline();
    RetrievalPipeline(RetrievalPipeline &&) noexcept;
    RetrievalPipeline &operator=(RetrievalPipeline &&) noexcept;
    RetrievalPipeline(const RetrievalPipeline &) = delete;
    RetrievalPipeline &operator=(const RetrievalPipeline &) = delete;

    void setupPostBootEval();
    void prepareResources();
    void dryRunWarmup();
    void moveToRoot(CiphertextBatch &quant);
    bool isSingleBlock() const;
    CiphertextVec computePayload(CiphertextBatch &quant);
    // Empty out_dir -> default result directory (task2-decrypt's input); pass a
    // per-query directory in server mode to keep overlapping results apart.
    void saveResults(CiphertextVec &result_los, const std::string &out_dir = "");

private:
    std::unique_ptr<hidden::RetrievalPipelineImpl> impl_;
};

// ============================================================================
// Per-phase work helpers (keygen / encrypt / decrypt binaries)
// ============================================================================

// --- Shared --------------------------------------------------------------
// deb::SecretKey is non-copyable/non-movable, so fill a caller-owned instance.
void loadDebSecretKey(const std::string &path, deb::SecretKey &sk_deb);

// --- task-keygen --------------------------------------------------
// Opaque facade over JuviaResourceManager (holds all evaluation/boot keys).
class KeygenContext {
public:
    KeygenContext(SecretKeyHandle &sk, deb::SecretKey &sk_deb);
    ~KeygenContext();
    KeygenContext(KeygenContext &&) noexcept;
    KeygenContext &operator=(KeygenContext &&) noexcept;
    KeygenContext(const KeygenContext &) = delete;
    KeygenContext &operator=(const KeygenContext &) = delete;

    void generateAndSaveIPKeys(const deb::SecretKey &sk_deb, const std::string &pk_path,
                               const std::vector<uint32_t> &rank_list);

    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Generate (reuse.empty()) or reload (reuse == "reuse") the deb + HEaaN2 secret
// keys; fills the opaque sk handle.  HEaaN2 params are built internally.
void genOrLoadSecretKeys(const std::string &reuse, const std::string &secretkey_path, deb::SecretKey &sk_deb,
                         SecretKeyHandle &sk);

// Save the full public-key bundle under publickey_path.
void saveAllPublicKeys(const KeygenContext &hrm, const std::string &publickey_path);

// --- task-encrypt-data --------------------------------------------
struct OriginData {
    std::ifstream file; // positioned just past the header
    uint32_t vector_size = 0;
    uint32_t db_size = 0;
    std::vector<uint64_t> payloads;
};
OriginData openOriginData(const std::string &origin_path, const std::string &payload_path);
// Encrypt the DB vectors from the origin file into ctxt_path.
void encryptDataToFile(std::ifstream &file, uint32_t vector_size, uint64_t num_w, const deb::SecretKey &sk_deb,
                       const std::string &ctxt_path);

// --- task-encrypt-query -------------------------------------------
struct QueryData {
    std::vector<std::vector<double>> queries;
    uint32_t vector_size = 0;
    uint32_t db_size = 0;
};
QueryData readQueryVectors(const std::string &query_path);
void encryptQueriesToFile(const std::vector<std::vector<double>> &queries, uint32_t vector_size, uint64_t queries_size,
                          const deb::SecretKey &sk_deb, const std::string &ctxt_path);

// --- task1-decrypt -------------------------------------------------
// {sk, ci_sk} as opaque handles (move-only).
struct CountKeys {
    SecretKeyHandle sk;
    SecretKeyHandle ci_sk;
};
CountKeys loadCountSecretKeys(const std::string &sk_path);
Ciphertext loadCountResultCiphertext(const std::string &path);
// Load a count result saved by saveCountResultDropP1 (the dropped P1=1 limb is
// restored as zeros; decrypt ignores it).  Inverse of the producer's save.
Ciphertext loadCountResultDropP1(const std::string &path);
// Decrypt+save the task1 result and return the first decoded real value.
double decryptAndSaveCount(JuviaTimer &timer, const Ciphertext &ctxt, const SecretKeyHandle &ci_sk,
                           const std::string &out_path);

// --- task2-decrypt -------------------------------------------------
// {sk, ci_sk, sk_lo} as opaque handles (move-only).
struct RetrievalKeys {
    SecretKeyHandle sk;
    SecretKeyHandle ci_sk;
    SecretKeyHandle sk_lo;
};
RetrievalKeys loadRetrievalSecretKeys(const std::string &sk_hv_path, const std::string &sk_deb_path);
std::vector<std::filesystem::path> collectRetrievalResultFiles(const std::string &dir, const std::string &preset);
struct RetrievalDecryptResult {
    std::vector<double> non_zero_values;
    double max_abs_value = 0.0;
};
RetrievalDecryptResult decryptRetrievalResult(const std::vector<std::filesystem::path> &files,
                                              const RetrievalKeys &keys);
void saveRetrievalDecrypted(const std::string &out_path, const std::vector<double> &non_zero_values);

} // namespace JUVIA
