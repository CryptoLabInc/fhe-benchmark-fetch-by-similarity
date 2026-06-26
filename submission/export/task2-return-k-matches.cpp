// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#include "JuviaNttMode.hpp"
#include "JuviaPipeline.hpp"
#include "JuviaTaskServer.hpp"
#include "JuviaTimer.hpp"

#include "JuviaSettings.hpp"

#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace JUVIA;

// task2 return-k-matches.  See JuviaTaskServer.hpp for the one-shot CLI and the
// stdin server-mode protocol (load keys/DB/bootstrapper/task2 resources ONCE,
// then stream queries).  OUT_DIR overrides the result directory for that query
// (default: the path task2-decrypt reads).
int main(int argc, char *argv[]) {
    setNttMode(NttMode::DIRECT);
    try {
        auto config = loadJson("config.json");
        const std::string preset = config["PRESET"];
        const std::set<int> device_ids_set = config["DEVICE_IDS"];
        const std::vector<int> device_ids(device_ids_set.begin(), device_ids_set.end());
        // Banner to stderr so stdout carries ONLY the machine protocol
        // (READY / DONE / ERROR) for a wrapper reading stdout line by line.

        const std::string timer_log_path = "task-times/task2-" + preset + ".log";
        JuviaTimer timer(true, pathFromEnv("JUVIA_TIMER_LOG_TASK2_RETURN_K_MATCHES", timer_log_path.c_str()));
        MatchPipeline ctx(device_ids, device_ids_set, PRESET_MAP.at(preset).first, PRESET_MAP.at(preset).second);
        ctx.setCleaner(cleanerForPreset(preset));
        RetrievalPipeline t2(ctx, preset);

        // ---- One-time, query-independent setup ----
        timer.start("Load evaluation keys");
        ctx.loadEvalKeys(PUBLICKEY_PATH, /*skip_count_rot_keys=*/true); // task1-only
        timer.end();

        timer.start("Setup bootstrapper");
        ctx.setupBootstrapper();
        t2.setupPostBootEval();
        timer.end();

        timer.start("Load DB ciphertexts");
        const std::string db_file = ENCRYPT_DATA_PATH + "/" + ENCRYPT_DATA_NAME + "_" + preset + ".bin";
        ctx.loadDB(db_file);
        timer.end();

        timer.start("Setup resources");
        ctx.setupInnerProduct();
        t2.prepareResources();
        timer.end();

        timer.start("Dry-run warmup");
        t2.dryRunWarmup();
        timer.end();

        timer.printAll(); // flush one-time setup timings as their own summary

        const std::string query_file = QUERY_PATH + "/" + ENCRYPT_DATA_NAME + "_" + preset + "_query.bin";

        // ---- Per-query work (re-runnable; setup state above is untouched) ----
        // Returns the result directory to report in the DONE reply.
        const auto processQuery = [&](u64 query_index, Real threshold, const std::string &out_dir) -> std::string {
            ctx.setThreshold(threshold);

            timer.start("** Load query ciphertext");
            ctx.loadQuery(query_file, query_index);
            timer.end();

            // Single-block fast path runs the cleaner ONE level deeper so
            // computeResultsDirectFast can spend the threshold's leftover levels on the deg-4
            // sharpener (precision) before the payload multiply; the full path keeps the
            // standard target level.
            const u64 cleaner_target = (payloadBitSize(preset) > 4 && t2.isSingleBlock())
                                           ? RetrievalPipeline::kDirectSharpenLevel
                                           : RetrievalPipeline::kCleanerTargetLevel;
            auto quant = ctx.runComputeLoop(timer, cleaner_target, DEBUG_PATH_TASK1);

            timer.start("** Copy intermediate ciphertexts to root device");
            t2.moveToRoot(quant);
            timer.end();

            // Whole HE compute.
            timer.start("** Compute payload");
            auto result = t2.computePayload(quant);
            timer.end();

            // Disk I/O — NOT part of the HE computation benchmark.
            timer.start("** Save result ciphertexts");
            t2.saveResults(result, out_dir);
            timer.end();

            timer.printAll(); // flush this query's timings (truncate on first call)
            return out_dir.empty() ? RESULT_CIPHERTEXT_PATH_TASK2 : out_dir;
        };

        return runTaskMain(argc, argv, "task2 " + preset, processQuery);

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
