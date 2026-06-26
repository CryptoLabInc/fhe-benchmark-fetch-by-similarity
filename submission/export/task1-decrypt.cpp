// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#include "JuviaPipeline.hpp"
#include "JuviaTimer.hpp"

#include "JuviaSettings.hpp"

#include <iostream>

using namespace JUVIA;

int main() {
    // CPU-only tool: no GPU sync (sync_gpu=true would spin up an idle CUDA context).
    const auto config = loadJson("config.json");
    const std::string preset = config["PRESET"];
    const std::string timer_log_path = "task-times/task1-" + preset + "-decrypt.log";
    JuviaTimer timer(/*sync_gpu=*/false, pathFromEnv("JUVIA_TIMER_LOG_TASK1_DECRYPT", timer_log_path.c_str()));

    // 1. Load the HEaaN2 secret key saved by task-keygen.
    timer.start("Load secret key");
    auto [sk, ci_sk] = loadCountSecretKeys(SECRETKEY_PATH + "/sk_hv.bin");
    timer.end();

    // 2. Load the result ciphertext produced by task1-count-matches (the count's
    //    redundant P1=1 graft limb was dropped at save; restored as zeros here).
    timer.start("Load result ciphertext");
    auto ctxt = loadCountResultDropP1(RESULT_CIPHERTEXT_PATH_TASK1 + "/result.bin");
    timer.end();

    // 3. Decrypt + decode + save inside juvia; returns the first value.
    decryptAndSaveCount(timer, ctxt, ci_sk, DECRYPTED_DATA_PATH_TASK1 + "/decrypted.bin");

    return 0;
}
