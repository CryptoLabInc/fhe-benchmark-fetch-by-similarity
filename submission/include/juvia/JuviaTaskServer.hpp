// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#pragma once

// Shared driver for the task1/task2 compute binaries: one-shot CLI plus the
// stdin "server" mode (load once, stream many queries).  Both binaries differ
// only in the per-query work and how OUT_DIR maps to the reported result path,
// so that work is supplied as a callback and the protocol/dispatch lives here.
//
// Protocol (server mode, no args): print "READY <label>", then one query per
// stdin line: "<QUERY_INDEX> <THRESHOLD> [OUT_DIR]".  Blank lines and lines
// starting with '#' are ignored; "quit"/"exit"/EOF stop the loop.  Each line
// gets exactly one reply on stdout -- "DONE <QUERY_INDEX> <RESULT_LOCATION>" or
// "ERROR <reason>" -- so a wrapper can sync line by line.

#include "JuviaSettings.hpp" // MAX_QUERY_ID + JUVIA scalar types

#include <functional>
#include <iostream>
#include <sstream>
#include <string>

namespace JUVIA {

inline std::string trim(const std::string &s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return "";
    return s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}

// Returns the rejection reason, or "" when (query_index, threshold) are valid.
inline std::string validateQuery(long long query_index, long double threshold) {
    if (query_index < 0 || static_cast<u64>(query_index) >= MAX_QUERY_ID)
        return "QUERY_INDEX must be in [0, " + std::to_string(MAX_QUERY_ID) + ")";
    if (threshold < 0 || threshold > 1)
        return "THRESHOLD must be in [0, 1]";
    return "";
}

// Runs one query and returns the result path/dir to report in the DONE reply;
// throws on a query failure (caught in server mode -> ERROR, loop continues).
using QueryFn = std::function<std::string(u64 query_index, Real threshold, const std::string &out_dir)>;

// Dispatch argc: one query then exit when args are given (backward compatible),
// otherwise the stdin server loop.  ready_label is appended to "READY " (e.g.
// "task2 small").  Returns the process exit code.
inline int runTaskMain(int argc, char *argv[], const std::string &ready_label, const QueryFn &processQuery) {
    if (argc >= 3) {
        // One-shot: process a single query, then exit.
        const long long query_index = std::stoll(argv[1]);
        const long double threshold = std::stold(argv[2]);
        const std::string reason = validateQuery(query_index, threshold);
        if (!reason.empty()) {
            std::cerr << "Error: " << reason << std::endl;
            return 1;
        }
        const std::string out_dir = (argc >= 4) ? argv[3] : "";
        processQuery(static_cast<u64>(query_index), static_cast<Real>(threshold), out_dir);
        return 0;
    }

    if (argc == 1) {
        // Server mode: stream queries from stdin until EOF / quit.
        std::cout << "READY " << ready_label << std::endl;
        std::cout.flush();

        std::string line;
        while (std::getline(std::cin, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            if (line == "quit" || line == "exit")
                break;

            std::istringstream iss(line);
            long long query_index = 0;
            long double threshold = 0;
            if (!(iss >> query_index >> threshold)) {
                std::cout << "ERROR expected '<QUERY_INDEX> <THRESHOLD> [OUT_DIR]', got: " << line << std::endl;
                std::cout.flush();
                continue;
            }
            std::string out_dir;
            iss >> out_dir; // optional

            const std::string reason = validateQuery(query_index, threshold);
            if (!reason.empty()) {
                std::cout << "ERROR " << reason << std::endl;
                std::cout.flush();
                continue;
            }

            try {
                const std::string location =
                    processQuery(static_cast<u64>(query_index), static_cast<Real>(threshold), out_dir);
                std::cout << "DONE " << query_index << " " << location << std::endl;
            } catch (const std::exception &e) {
                // Keep the server alive: report the failed query and continue.
                std::cout << "ERROR " << e.what() << std::endl;
            }
            std::cout.flush();
        }
        return 0;
    }

    std::cerr << "Usage: " << argv[0] << " [<QUERY_INDEX> <THRESHOLD> [OUT_DIR]]\n"
              << "       (no args = stdin server mode: one '<QUERY_INDEX> <THRESHOLD> [OUT_DIR]' per line)"
              << std::endl;
    return 1;
}

} // namespace JUVIA
