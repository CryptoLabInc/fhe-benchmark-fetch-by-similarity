// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

// Writes ctxt_<preset>.bin, consumed by task1/task2.

#include "JuviaPipeline.hpp"
#include "JuviaTimer.hpp"

#include "JuviaNttMode.hpp"
#include "JuviaSettings.hpp"
#include "deb/Preset.hpp"

#include <iostream>

using namespace JUVIA;

int main() {
    // CPU-only tool: no GPU sync (sync_gpu=true would spin up an idle CUDA context).
    JuviaTimer timer(/*sync_gpu=*/false,
                     pathFromEnv("JUVIA_TIMER_LOG_TASK_ENCRYPT_DATA", "task-times/encrypt-data.log"));
    const auto config = loadJson("config.json");
    const std::string preset = config["PRESET"];

    setNttMode(NttMode::DIRECT);

    timer.start("Read data from the file");
    auto src = openOriginData(RAW_DATA_PATH + "/task-" + preset + "_origin.bin",
                              RAW_DATA_PATH + "/task-" + preset + "_payload.bin");
    timer.end();

    deb::SecretKey sk_deb(deb::PRESET_JUVIAD12L0);
    loadDebSecretKey(SECRETKEY_PATH + "/sk_deb.bin", sk_deb);

    const std::string ctxt_path = ENCRYPT_DATA_PATH + "/" + ENCRYPT_DATA_NAME + "_" + preset + ".bin";

    timer.start("Encode and encrypt into ciphertext file: " + ctxt_path);
    encryptDataToFile(src.file, src.vector_size, src.db_size, sk_deb, ctxt_path);
    timer.end();
    return 0;
}
