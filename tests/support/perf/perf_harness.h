#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hikari::perf {

struct RunStats {
    std::size_t operations = 0;
    double p50Us = 0, p95Us = 0, p99Us = 0, maxUs = 0, meanUs = 0;
};

// Minimums: warm (W) workloads warm up for 10 s; 1,000 timed operations across all runs.
inline constexpr std::chrono::milliseconds kWarmWarmup{10'000};
inline constexpr std::size_t kMinimumOperations = 1'000;

struct BenchmarkResult {
    std::string name;
    std::chrono::milliseconds warmup{0};
    std::vector<RunStats> runs;
    double p95SpreadPercent = 0; // (max p95 - min p95) / min p95 across runs
};

struct BenchmarkSpec {
    std::string name;
    int runs = 5;
    std::size_t operationsPerRun = 200;
    std::chrono::milliseconds warmup = kWarmWarmup;
    std::function<void()> operation;
};

// Nearest-rank percentile of sorted samples (p in (0, 100]).
double nearestRank(const std::vector<double> &sortedSamples, double p);

// Throws std::invalid_argument when the spec is below the contract minimums.
BenchmarkResult run(const BenchmarkSpec &spec);

struct HostIdentity {
    std::string cpu;
    unsigned logicalCores = 0;
    std::string os;
    std::string compiler;
    std::string fingerprint() const;
};

HostIdentity currentHost();

// `referenceHostsPath` holds a JSON array of fingerprints; a missing or empty list calibrates no host.
bool isCalibrated(const HostIdentity &host, const std::string &referenceHostsPath);

bool writeReport(const std::string &path, const HostIdentity &host, bool calibrated,
                 const std::vector<BenchmarkResult> &results);

}
