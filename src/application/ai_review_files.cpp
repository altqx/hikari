#include "hikari/application/ai_review.h"

#include <algorithm>

namespace hikari::application {

std::uint64_t AiReview::openTurn(const std::string &callerId, const std::string &callerLabel)
{
    const auto open = std::ranges::find(m_open, callerId, &std::pair<std::string, Open>::first);
    return open != m_open.end() ? open->second.turn : beginTurn(callerId, callerLabel);
}

AiReview::FileChange *AiReview::findFile(std::uint64_t id)
{
    const auto it = std::ranges::find(m_files, id, &FileChange::id);
    return it == m_files.end() ? nullptr : &*it;
}

const AiReview::FileChange *AiReview::file(std::uint64_t id) const
{
    const auto it = std::ranges::find(m_files, id, &FileChange::id);
    return it == m_files.end() ? nullptr : &*it;
}

std::uint64_t AiReview::stageFile(const std::string &callerId, const std::string &callerLabel, std::string path,
                                  std::string shown, std::optional<std::string> before, std::string after)
{
    const std::uint64_t turn = openTurn(callerId, callerLabel);
    const std::uint64_t id = m_nextId++;
    for (FileChange &earlier : m_files)
        if (earlier.path == path && !earlier.settled && !earlier.replacedBy) {
            earlier.replacedBy = id;
            before = earlier.before;
        }
    m_files.push_back(FileChange{id, turn, callerLabel, std::move(path), std::move(shown), std::move(before),
                                 std::move(after)});
    if (filesChanged)
        filesChanged();
    return id;
}

std::vector<const AiReview::FileChange *> AiReview::pendingFiles() const
{
    std::vector<const FileChange *> out;
    for (const FileChange &f : m_files)
        if (!f.settled && !f.replacedBy)
            out.push_back(&f);
    return out;
}

const std::string *AiReview::staged(const std::string &path) const
{
    for (const FileChange *f : pendingFiles())
        if (f->path == path)
            return &f->after;
    return nullptr;
}

bool AiReview::changedOnDisk(const FileChange &change, const Disk &disk) const
{
    return disk.read(change.path) != change.before;
}

namespace {

template <class Files, class Change>
void settleChain(Files &files, Change &newest, bool undone)
{
    for (Change *c = &newest; c;) {
        c->settled = true;
        c->undone = undone;
        const auto earlier = std::ranges::find(files, c->id, &Change::replacedBy);
        c = earlier == files.end() ? nullptr : &*earlier;
    }
}

}

AiReview::KeepFile AiReview::keepFile(std::uint64_t id, const Disk &disk, bool force)
{
    FileChange *f = findFile(id);
    if (!f || f->settled || f->replacedBy)
        return KeepFile::NotWaiting;
    if (!force && changedOnDisk(*f, disk))
        return KeepFile::ChangedOnDisk;
    if (!disk.write(f->path, f->after))
        return KeepFile::WriteFailed;
    settleChain(m_files, *f, false);
    if (filesChanged)
        filesChanged();
    return KeepFile::Kept;
}

bool AiReview::undoFile(std::uint64_t id)
{
    FileChange *f = findFile(id);
    if (!f || f->settled || f->replacedBy)
        return false;
    settleChain(m_files, *f, true);
    if (filesChanged)
        filesChanged();
    return true;
}

std::vector<std::string> AiReview::keepAllFiles(const Disk &disk, bool force)
{
    std::vector<std::string> stayed;
    std::vector<std::uint64_t> ids;
    for (const FileChange *f : pendingFiles())
        ids.push_back(f->id);
    for (const std::uint64_t id : ids)
        if (keepFile(id, disk, force) != KeepFile::Kept)
            stayed.push_back(file(id)->path);
    return stayed;
}

void AiReview::undoAllFiles()
{
    std::vector<std::uint64_t> ids;
    for (const FileChange *f : pendingFiles())
        ids.push_back(f->id);
    for (const std::uint64_t id : ids)
        undoFile(id);
}

namespace {

struct KeptPath {
    const AiReview::FileChange *oldest = nullptr;
    const AiReview::FileChange *newest = nullptr;
    std::optional<std::string> restore;
};

std::vector<KeptPath> keptFrom(const std::vector<AiReview::FileChange> &files, std::uint64_t turn)
{
    std::vector<KeptPath> out;
    for (const AiReview::FileChange &f : files) {
        if (f.turn < turn || !f.settled || f.undone)
            continue;
        auto here = std::ranges::find(out, f.path, [](const KeptPath &k) { return k.oldest->path; });
        if (here == out.end()) {
            out.push_back({&f, &f});
            continue;
        }
        if (f.id < here->oldest->id)
            here->oldest = &f;
        if (f.id > here->newest->id)
            here->newest = &f;
    }
    for (KeptPath &k : out) {
        const auto earlier = std::ranges::find(files, k.oldest->id, &AiReview::FileChange::replacedBy);
        k.restore = earlier != files.end() && earlier->turn < turn ? std::optional(earlier->after) : k.oldest->before;
    }
    return out;
}

}

AiReview::CheckpointFiles AiReview::checkpointFiles(std::uint64_t turn, const Disk &disk) const
{
    CheckpointFiles out;
    for (const FileChange *f : pendingFiles())
        if (f->turn >= turn)
            ++out.staged;
    for (const KeptPath &k : keptFrom(m_files, turn)) {
        ++out.kept;
        if (disk.read(k.newest->path) != std::optional(k.newest->after))
            out.changedSince.push_back(k.newest->path);
    }
    return out;
}

std::vector<std::string> AiReview::restoreFiles(std::uint64_t turn, const Disk &disk, bool force)
{
    std::vector<std::string> stayed;
    for (const KeptPath &k : keptFrom(m_files, turn)) {
        const std::string &path = k.newest->path;
        if (!force && disk.read(path) != std::optional(k.newest->after)) {
            stayed.push_back(path);
            continue;
        }
        if (!(k.restore ? disk.write(path, *k.restore) : disk.remove(path))) {
            stayed.push_back(path);
            continue;
        }
        for (FileChange &f : m_files)
            if (f.path == path && f.turn >= turn && f.settled && !f.undone)
                f.undone = true;
    }
    for (FileChange &f : m_files)
        if (f.turn >= turn && !f.settled) {
            f.settled = f.undone = true;
            for (FileChange &earlier : m_files)
                if (earlier.replacedBy == f.id && earlier.turn < turn)
                    earlier.replacedBy = 0;
        }
    if (filesChanged)
        filesChanged();
    return stayed;
}

}
