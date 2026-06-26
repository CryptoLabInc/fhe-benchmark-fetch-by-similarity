// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#include "JuviaNttMode.hpp"
#include "JuviaPipeline.hpp"
#include "JuviaTimer.hpp"
#include "deb/Preset.hpp"

#include "JuviaSettings.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace JUVIA;

int main(int argc, char *argv[]) {
    std::string reuse;
    if (argc > 1)
        reuse = std::string(argv[1]);

    setNttMode(NttMode::DIRECT);
    try {
        // CPU-only tool: no GPU sync (sync_gpu=true would spin up an idle CUDA context).
        JuviaTimer timer(/*sync_gpu=*/false, pathFromEnv("JUVIA_TIMER_LOG_TASK_KEYGEN", "task-times/keygen.log"));
        deb::SecretKey sk_deb(deb::PRESET_JUVIAD12L0);
        SecretKeyHandle sk;

        timer.start("Generate and save SecretKeys ...");
        genOrLoadSecretKeys(reuse, SECRETKEY_PATH, sk_deb, sk);
        timer.end();

        timer.start("Generating public keys...");
        KeygenContext hrm(sk, sk_deb);
        timer.end();

        std::vector<uint32_t> rank_list;
        for (const auto &[key, value_pair] : PRESET_MAP)
            rank_list.push_back(value_pair.first);

        timer.start("Save public keys to " + PUBLICKEY_PATH);
        saveAllPublicKeys(hrm, PUBLICKEY_PATH);
        timer.end();

        const std::string PK_PATH_FR = PUBLICKEY_PATH + "/FR";
        std::filesystem::create_directories(PK_PATH_FR);

        timer.start("Generate and save scoring keys to " + PK_PATH_FR);
        hrm.generateAndSaveIPKeys(sk_deb, PK_PATH_FR, rank_list);
        timer.end();

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
