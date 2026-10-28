#pragma once

// Performance measurement harness (E2-perf; docs/qt/performance.md). Each
// benchmark runs several independent repetitions; statistics are reported per
// run (nearest-rank p95/p99 and maximum), never pooled across runs. Results
// carry the host's identity and whether that host is a calibrated reference;
// an uncalibrated host's numbers are observations, never budget passes.

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

// Contract minimums for latency workloads: ten seconds of warmup for warm (W)
// workloads and at least 1,000 timed operations across all runs.
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
    int runs = 5;                     // contract: five repetitions
    std::size_t operationsPerRun = 200; // contract: at least 1,000 operations in total
    std::chrono::milliseconds warmup = kWarmWarmup;
    std::function<void()> operation;  // one timed operation
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

// True when the host fingerprint appears in the reference-host list (JSON
// array of fingerprints); a missing or empty list means no host is calibrated.
bool isCalibrated(const HostIdentity &host, const std::string &referenceHostsPath);

// Writes one JSON document with the host, calibration state and every result.
bool writeReport(const std::string &path, const HostIdentity &host, bool calibrated,
                 const std::vector<BenchmarkResult> &results);

} // namespace hikari::perf
