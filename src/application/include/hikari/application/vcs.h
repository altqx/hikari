#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hikari::application {

enum class VcsProblem { NoGit, TimedOut, NoRepository, UnsafeRepository, NoRef, NoFile, TooLarge, NotText, Failed };

struct VcsFailure {
    VcsProblem problem = VcsProblem::Failed;
    std::string message;
};

struct VcsRef {
    std::string name, commit;
    bool current = false;
};

struct VcsCommit {
    std::string commit, summary, author;
    std::int64_t time = 0; // seconds since the epoch, the committer's
};

struct VcsRefs {
    std::string root;                // the work tree, UTF-8
    std::string prefix;              // where `at` sits in it, '/'-separated, empty or ending in '/'
    std::optional<std::string> head; // the current branch; nullopt: detached or no commit yet
    std::string headCommit;          // empty: no commit yet
    std::vector<VcsRef> branches, tags;
    std::vector<VcsCommit> commits;
};

struct VcsFile {
    std::string commit, path, text;
};

// Reads git without writing or running anything the repository configures.
class VcsPort {
public:
    virtual ~VcsPort() = default;
    // The repository holding the folder `at`; `done` runs once, later.
    virtual void refs(const std::string &at, int commits, std::function<void(std::expected<VcsRefs, VcsFailure>)> done) = 0;
    // The file `file` as `ref` has it, as UTF-8; `done` runs once, later.
    virtual void show(const std::string &file, const std::string &ref,
                      std::function<void(std::expected<VcsFile, VcsFailure>)> done) = 0;
};

}
