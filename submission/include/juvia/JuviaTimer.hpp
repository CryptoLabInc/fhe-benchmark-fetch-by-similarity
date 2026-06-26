// Copyright (C) 2026 CryptoLab, Inc.
// All rights reserved.
//
// This source code is protected under international copyright law.  All rights
// reserved and protected by the copyright holders.

#pragma once

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifdef BUILD_WITH_CUDA
#include <cuda_runtime.h>
#endif

namespace JUVIA {

// Wall-clock timer for named sections (std::chrono).  With sync_gpu=true and a
// CUDA device present, wraps each measurement in cudaDeviceSynchronize().
// Repeated start/end with the same name accumulate; printAll() writes the
// summary in first-encounter order to the log file given at construction
// (default "task-timings.log", relative to the process cwd) with a
// program-name + timestamp header.  Each task binary resolves its own
// JUVIA_TIMER_LOG_* environment variable via pathFromEnv() and passes the
// result here.  The file is rewritten on every run: the timer's first summary
// truncates it, later summaries from the same timer append.  Falls back to
// stdout with a warning when the file cannot be opened.  No HEaaN2 dependency
// (GPU sync uses the CUDA runtime directly, under BUILD_WITH_CUDA only).
class JuviaTimer {
public:
    explicit JuviaTimer(bool sync_gpu = false, std::string log_path = "task-timings.log")
        : sync_gpu_(sync_gpu && cudaIsAvailable()), log_path_(std::move(log_path)) {
        // A bare filename (e.g. the "task-timings.log" default) has an empty
        // parent_path; create_directories("") throws EINVAL on libstdc++, so
        // only create the directory when there actually is one.
        const auto parent = std::filesystem::path(log_path_).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
    }

    ~JuviaTimer() {
        if (count_ > 0) {
            printAll();
        }
    }
    void start(std::string name) {
        name_ = std::move(name);
        if (sync_gpu_)
            deviceSync();
        beg_[name_] = clock::now();
    }

    void end() {
        if (sync_gpu_)
            deviceSync();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - beg_[name_]).count();
        auto it = dur_.find(name_);
        if (it == dur_.end()) {
            dur_[name_] = elapsed;
            order_[name_] = count_++;
        } else {
            it->second += elapsed;
        }
    }

    void reset() {
        dur_.clear();
        order_.clear();
        beg_.clear();
        count_ = 0;
    }

    void printAll() {
        // Collect and sort by insertion order.
        std::vector<std::pair<std::string, int64_t>> sorted(dur_.begin(), dur_.end());
        std::sort(sorted.begin(), sorted.end(),
                  [&](const auto &a, const auto &b) { return order_.at(a.first) < order_.at(b.first); });

        std::ostringstream out;
        out << "\n------- Timer Summary [" << programName() << " " << timestamp() << "] -------\n";
        for (const auto &[name, us] : sorted) {
            out << name << " total time = ";
            if (us < 1000)
                out << us << " \xce\xbcs\n";
            else if (us < 1'000'000)
                out << us / 1000.0 << " ms\n";
            else {
                out.precision(3);
                out << us / 1'000'000.0 << " s\n";
            }
        }
        out << "-----------------------------\n";

        // Fresh file per run: truncate on this timer's first summary so each
        // execution leaves only its own timings, but don't lose earlier
        // summaries if the same timer prints more than once.
        std::ofstream log(log_path_, truncated_ ? std::ios::app : std::ios::trunc);
        truncated_ = true;
        if (log) {
            log << out.str();
        } else {
            std::cerr << "JuviaTimer: cannot open " << log_path_ << ", printing to stdout" << std::endl;
            std::cout << out.str();
        }
        reset();
    }

private:
    using clock = std::chrono::high_resolution_clock;

    // Native CUDA helpers (no HEaaN2).  Both are no-ops without BUILD_WITH_CUDA.
    static bool cudaIsAvailable() {
#ifdef BUILD_WITH_CUDA
        int count = 0;
        return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
#else
        return false;
#endif
    }
    static void deviceSync() {
#ifdef BUILD_WITH_CUDA
        cudaDeviceSynchronize();
#endif
    }

    // Header context so appended summaries from consecutive task runs stay
    // attributable.  program_invocation_short_name is glibc (this project is
    // Linux-only).
    static const char *programName() {
#ifdef __GLIBC__
        return program_invocation_short_name;
#else
        return "unknown";
#endif
    }
    static std::string timestamp() {
        std::time_t now = std::time(nullptr);
        std::tm tm_buf{};
        localtime_r(&now, &tm_buf);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%F %T", &tm_buf);
        return buf;
    }

    bool sync_gpu_;
    std::string log_path_;
    bool truncated_ = false;
    std::string name_;
    uint64_t count_ = 0;
    std::map<std::string, std::chrono::time_point<clock>> beg_;
    std::map<std::string, int64_t> dur_;
    std::map<std::string, uint64_t> order_;
};

} // namespace JUVIA
