#include "perf_harness.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(__linux__)
#include <sys/utsname.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hikari::perf {

double nearestRank(const std::vector<double> &sorted, double p)
{
    if (sorted.empty())
        return 0;
    const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
}

BenchmarkResult run(const BenchmarkSpec &spec)
{
    using clock = std::chrono::steady_clock;
    if (spec.runs < 1 || static_cast<std::size_t>(spec.runs) * spec.operationsPerRun < kMinimumOperations)
        throw std::invalid_argument(spec.name + ": fewer than 1,000 timed operations");
    if (spec.warmup < kWarmWarmup)
        throw std::invalid_argument(spec.name + ": warm workloads need ten seconds of warmup");
    BenchmarkResult result{spec.name, spec.warmup, {}, 0};
    if (spec.warmup.count() > 0) {
        const auto until = clock::now() + spec.warmup;
        while (clock::now() < until)
            spec.operation();
    }
    for (int r = 0; r < spec.runs; ++r) {
        std::vector<double> samples;
        samples.reserve(spec.operationsPerRun);
        for (std::size_t i = 0; i < spec.operationsPerRun; ++i) {
            const auto t0 = clock::now();
            spec.operation();
            samples.push_back(std::chrono::duration<double, std::micro>(clock::now() - t0).count());
        }
        std::ranges::sort(samples);
        RunStats s;
        s.operations = samples.size();
        s.p50Us = nearestRank(samples, 50);
        s.p95Us = nearestRank(samples, 95);
        s.p99Us = nearestRank(samples, 99);
        s.maxUs = samples.empty() ? 0 : samples.back();
        s.meanUs = samples.empty() ? 0 : std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
        result.runs.push_back(s);
    }
    if (!result.runs.empty()) {
        const auto [lo, hi] = std::ranges::minmax_element(result.runs, {}, &RunStats::p95Us);
        result.p95SpreadPercent = lo->p95Us > 0 ? (hi->p95Us - lo->p95Us) / lo->p95Us * 100.0 : 0;
    }
    return result;
}

std::string HostIdentity::fingerprint() const
{
    return cpu + "|" + std::to_string(logicalCores) + "|" + os + "|" + compiler;
}

HostIdentity currentHost()
{
    HostIdentity host;
    host.logicalCores = std::thread::hardware_concurrency();
#if defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    for (std::string line; std::getline(cpuinfo, line);)
        if (line.starts_with("model name")) {
            host.cpu = line.substr(line.find(':') + 2);
            break;
        }
    utsname u{};
    if (uname(&u) == 0)
        host.os = std::string(u.sysname) + " " + u.release;
#elif defined(_WIN32)
    auto reg = [](const wchar_t *key, const wchar_t *value) {
        wchar_t buffer[256] = {};
        DWORD size = sizeof buffer;
        if (::RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
            return std::string();
        char out[512] = {};
        ::WideCharToMultiByte(CP_UTF8, 0, buffer, -1, out, sizeof out, nullptr, nullptr);
        return std::string(out);
    };
    host.cpu = reg(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    const wchar_t *nt = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    host.os = "Windows " + reg(nt, L"DisplayVersion") + " build " + reg(nt, L"CurrentBuild");
#endif
#if defined(__clang__)
    host.compiler = "clang " __clang_version__;
#elif defined(__GNUC__)
    host.compiler = "gcc " __VERSION__;
#elif defined(_MSC_VER)
    host.compiler = "msvc " + std::to_string(_MSC_FULL_VER);
#endif
    return host;
}

bool isCalibrated(const HostIdentity &host, const std::string &referenceHostsPath)
{
    std::ifstream in(referenceHostsPath);
    if (!in)
        return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    // Minimal check: the exact fingerprint appears as a quoted JSON string.
    return buffer.str().find("\"" + host.fingerprint() + "\"") != std::string::npos;
}

namespace {

std::string escape(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    return out;
}

} // namespace

bool writeReport(const std::string &path, const HostIdentity &host, bool calibrated,
                 const std::vector<BenchmarkResult> &results)
{
    std::ofstream out(path);
    if (!out)
        return false;
    out << "{\n \"host\": {\"cpu\": \"" << escape(host.cpu) << "\", \"logicalCores\": " << host.logicalCores
        << ", \"os\": \"" << escape(host.os) << "\", \"compiler\": \"" << escape(host.compiler)
        << "\", \"fingerprint\": \"" << escape(host.fingerprint()) << "\"},\n"
        << " \"calibrated\": " << (calibrated ? "true" : "false") << ",\n"
        << " \"note\": \"" << (calibrated ? "reference host" : "uncalibrated host: observations only, not budget passes")
        << "\",\n \"benchmarks\": [\n";
    for (std::size_t b = 0; b < results.size(); ++b) {
        const auto &r = results[b];
        out << "  {\"name\": \"" << escape(r.name) << "\", \"warmupMs\": " << r.warmup.count()
            << ", \"p95SpreadPercent\": " << r.p95SpreadPercent
            << ", \"runs\": [";
        for (std::size_t i = 0; i < r.runs.size(); ++i) {
            const auto &s = r.runs[i];
            out << (i ? ", " : "") << "{\"operations\": " << s.operations << ", \"p50Us\": " << s.p50Us
                << ", \"p95Us\": " << s.p95Us << ", \"p99Us\": " << s.p99Us << ", \"maxUs\": " << s.maxUs
                << ", \"meanUs\": " << s.meanUs << "}";
        }
        out << "]}" << (b + 1 < results.size() ? "," : "") << "\n";
    }
    out << " ]\n}\n";
    return static_cast<bool>(out);
}

} // namespace hikari::perf
