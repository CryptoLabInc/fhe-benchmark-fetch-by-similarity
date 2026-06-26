// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#include "JuviaPipeline.hpp"
#include "JuviaTimer.hpp"

#include "JuviaNttMode.hpp"
#include "JuviaSettings.hpp"
#include "deb/Preset.hpp"

#include <algorithm>
#include <iostream>

using namespace JUVIA;

int main() {
    // CPU-only tool: no GPU sync (sync_gpu=true would spin up an idle CUDA context).
    JuviaTimer timer(/*sync_gpu=*/false,
                     pathFromEnv("JUVIA_TIMER_LOG_TASK_ENCRYPT_QUERY", "task-times/encrypt-query.log"));
    const auto config = loadJson("config.json");
    const std::string preset = config["PRESET"];

    setNttMode(NttMode::DIRECT);

    deb::SecretKey sk_deb(deb::PRESET_JUVIAD12L0);
    loadDebSecretKey(SECRETKEY_PATH + "/sk_deb.bin", sk_deb);

    timer.start("Read queries from the target file");
    auto qd = readQueryVectors(RAW_DATA_PATH + "/task-" + preset + "_query.bin");
    timer.end();

    const u64 queries_size = std::min(static_cast<u64>(qd.queries.size()), MAX_QUERY_ID);
    const std::string ctxt_path = QUERY_PATH + "/" + ENCRYPT_DATA_NAME + "_" + preset + "_query.bin";

    timer.start("Encrypt queries and save to: " + ctxt_path);
    encryptQueriesToFile(qd.queries, qd.vector_size, queries_size, sk_deb, ctxt_path);
    timer.end();

    return 0;
}
