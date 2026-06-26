// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#include "JuviaPipeline.hpp"
#include "JuviaTimer.hpp"

#include "JuviaSettings.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace JUVIA;

int main() {
    // CPU-only tool: no GPU sync (sync_gpu=true would spin up an idle CUDA context).
    const auto config = loadJson("config.json");
    const std::string preset = config["PRESET"];
    const std::string timer_log_path = "task-times/task2-" + preset + "-decrypt.log";
    JuviaTimer timer(/*sync_gpu=*/false, pathFromEnv("JUVIA_TIMER_LOG_TASK2_DECRYPT", timer_log_path.c_str()));

    timer.start("Load secret keys");
    auto keys = loadRetrievalSecretKeys(SECRETKEY_PATH + "/sk_hv.bin", SECRETKEY_PATH + "/sk_deb.bin");
    timer.end();

    timer.start("Collect result ciphertexts");
    const auto files = collectRetrievalResultFiles(RESULT_CIPHERTEXT_PATH_TASK2, preset);
    timer.end();

    timer.start("Decrypt and decode result ciphertexts");
    auto dec = decryptRetrievalResult(files, keys);
    timer.end();


    timer.start("Save decrypted data");
    saveRetrievalDecrypted(DECRYPTED_DATA_PATH_TASK2 + "/decrypted.bin", dec.non_zero_values);
    timer.end();

    return 0;
}
