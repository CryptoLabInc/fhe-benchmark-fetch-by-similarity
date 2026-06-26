// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#include "JuviaNttMode.hpp"
#include "JuviaPipeline.hpp"
#include "JuviaTaskServer.hpp"
#include "JuviaTimer.hpp"

#include "JuviaCudaUtils.hpp"
#include "JuviaSettings.hpp"

#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace JUVIA;

// task1 count-matches.  See JuviaTaskServer.hpp for the one-shot CLI and the
// stdin server-mode protocol (load keys/DB/bootstrapper ONCE, then stream
// queries).  OUT_DIR overrides the result directory for that query (default:
// the path task1-decrypt reads).
int main(int argc, char *argv[]) {
    setNttMode(NttMode::DIRECT);
    try {
        auto config = loadJson("config.json");
        const std::string preset = config["PRESET"];
        const std::set<int> device_ids_set = config["DEVICE_IDS"];
        const std::vector<int> device_ids(device_ids_set.begin(), device_ids_set.end());

        // Banner to stderr so stdout carries ONLY the machine protocol
        // (READY / DONE / ERROR) for a wrapper reading stdout line by line.

        const uint32_t vector_dim = PRESET_MAP.at(preset).first;
        const uint32_t num_w = PRESET_MAP.at(preset).second;

        const std::string timer_log_path = "task-times/task1-" + preset + ".log";
        JuviaTimer timer(true,
                         pathFromEnv("JUVIA_TIMER_LOG_TASK1_COUNT_MATCHES", timer_log_path.c_str()));
        MatchPipeline ctx(device_ids, device_ids_set, vector_dim, num_w);
        ctx.setCleaner(cleanerForPreset(preset));

        // ---- One-time, query-independent setup ----
        timer.start("Load evaluation keys");
        ctx.loadEvalKeys(PUBLICKEY_PATH);
        timer.end();

        timer.start("Setup bootstrapper");
        ctx.setupBootstrapper();
        timer.end();

        timer.start("Load DB ciphertexts");
        ctx.loadDB(ENCRYPT_DATA_PATH + "/" + ENCRYPT_DATA_NAME + "_" + preset + ".bin");
        timer.end();

        timer.start("Setup IPEngine");
        ctx.setupInnerProduct();
        timer.end();

        timer.printAll(); // flush one-time setup timings as their own summary

        const std::string query_file = QUERY_PATH + "/" + ENCRYPT_DATA_NAME + "_" + preset + "_query.bin";
        const std::string default_result = RESULT_CIPHERTEXT_PATH_TASK1 + "/result.bin";

        // ---- Per-query work (re-runnable; setup state above is untouched) ----
        // Returns the result path to report in the DONE reply.
        const auto processQuery = [&](u64 query_index, Real threshold, const std::string &out_dir) -> std::string {
            const std::string result_path = out_dir.empty() ? default_result : (out_dir + "/result.bin");

            ctx.setThreshold(threshold);

            timer.start("** Load query ciphertext");
            ctx.loadQuery(query_file, query_index);
            timer.end();

            // IP -> rescale -> bootstrap -> threshold(clean).
            auto quant = ctx.runComputeLoop(timer, /*cleaner_target_level=*/0, DEBUG_PATH_TASK1);

            // Accumulate + rotation-sum + save, inside juvia (no HEaaN2 here).
            ctx.accumulateAndSaveCount(quant, timer, result_path);

            timer.printAll(); // flush this query's timings (truncate on first call)
            return result_path;
        };

        return runTaskMain(argc, argv, "task1 " + preset, processQuery);

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
