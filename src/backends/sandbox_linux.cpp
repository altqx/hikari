#include "hikari/backends/sandbox.h"

#include <QFileInfo>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sched.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace hikari::backends::sandbox {

namespace {

// Landlock's UAPI, kept here because distribution headers trail the kernels the app runs on.
namespace ll {
constexpr std::uint32_t kCreateRulesetVersion = 1u << 0;
constexpr int kRulePathBeneath = 1;

constexpr std::uint64_t kExecute = 1ull << 0;
constexpr std::uint64_t kWriteFile = 1ull << 1;
constexpr std::uint64_t kReadFile = 1ull << 2;
constexpr std::uint64_t kReadDir = 1ull << 3;
constexpr std::uint64_t kRemoveDir = 1ull << 4;
constexpr std::uint64_t kRemoveFile = 1ull << 5;
constexpr std::uint64_t kMakeChar = 1ull << 6;
constexpr std::uint64_t kMakeDir = 1ull << 7;
constexpr std::uint64_t kMakeReg = 1ull << 8;
constexpr std::uint64_t kMakeSock = 1ull << 9;
constexpr std::uint64_t kMakeFifo = 1ull << 10;
constexpr std::uint64_t kMakeBlock = 1ull << 11;
constexpr std::uint64_t kMakeSym = 1ull << 12;
constexpr std::uint64_t kRefer = 1ull << 13;
constexpr std::uint64_t kTruncate = 1ull << 14;
constexpr std::uint64_t kIoctlDev = 1ull << 15;
constexpr std::uint64_t kResolveUnix = 1ull << 16;

constexpr std::uint64_t kBindTcp = 1ull << 0;
constexpr std::uint64_t kConnectTcp = 1ull << 1;
constexpr std::uint64_t kBindUdp = 1ull << 2;
constexpr std::uint64_t kConnectSendUdp = 1ull << 3;

constexpr std::uint64_t kScopeAbstractUnix = 1ull << 0;
constexpr std::uint64_t kScopeSignal = 1ull << 1;

constexpr std::uint64_t kFileRights = kExecute | kWriteFile | kReadFile | kTruncate | kIoctlDev;

struct RulesetAttr {
    std::uint64_t handledAccessFs;
    std::uint64_t handledAccessNet;
    std::uint64_t scoped;
};

struct [[gnu::packed]] PathBeneathAttr {
    std::uint64_t allowedAccess;
    std::int32_t parentFd;
};

#ifndef SYS_landlock_create_ruleset
constexpr long kCreateRuleset = 444;
constexpr long kAddRule = 445;
constexpr long kRestrictSelf = 446;
#else
constexpr long kCreateRuleset = SYS_landlock_create_ruleset;
constexpr long kAddRule = SYS_landlock_add_rule;
constexpr long kRestrictSelf = SYS_landlock_restrict_self;
#endif

int abi()
{
    const long version = ::syscall(kCreateRuleset, nullptr, 0, kCreateRulesetVersion);
    return version < 0 ? 0 : static_cast<int>(version);
}

std::uint64_t handledFs(int abi)
{
    std::uint64_t rights = kExecute | kWriteFile | kReadFile | kReadDir | kRemoveDir | kRemoveFile | kMakeChar |
                           kMakeDir | kMakeReg | kMakeSock | kMakeFifo | kMakeBlock | kMakeSym;
    if (abi >= 2)
        rights |= kRefer;
    if (abi >= 3)
        rights |= kTruncate;
    if (abi >= 5)
        rights |= kIoctlDev;
    if (abi >= 9)
        rights |= kResolveUnix;
    return rights;
}

std::uint64_t handledNet(int abi)
{
    std::uint64_t rights = 0;
    if (abi >= 4)
        rights |= kBindTcp | kConnectTcp;
    if (abi >= 10)
        rights |= kBindUdp | kConnectSendUdp;
    return rights;
}
}

struct PathRule {
    std::string path;
    std::uint64_t access = 0;
};

constexpr std::uint64_t kRead = ll::kReadFile | ll::kReadDir;
constexpr std::uint64_t kReadExecute = kRead | ll::kExecute;
constexpr std::uint64_t kReadWrite = kRead | ll::kWriteFile | ll::kRemoveDir | ll::kRemoveFile | ll::kMakeDir |
                                     ll::kMakeReg | ll::kMakeSym | ll::kMakeFifo | ll::kMakeSock | ll::kRefer |
                                     ll::kTruncate;

const QStringList kLibraries = {
    QStringLiteral("/usr/lib"), QStringLiteral("/usr/lib64"), QStringLiteral("/lib"), QStringLiteral("/lib64"),
};
const QStringList kSystemReads = {
    QStringLiteral("/etc/ld.so.cache"), QStringLiteral("/etc/ssl"), QStringLiteral("/etc/pki"),
    QStringLiteral("/etc/ca-certificates"), QStringLiteral("/usr/share/ca-certificates"),
    QStringLiteral("/etc/localtime"), QStringLiteral("/usr/share/zoneinfo"), QStringLiteral("/dev/urandom"),
    QStringLiteral("/dev/random"), QStringLiteral("/proc/self"), QStringLiteral("/proc/cpuinfo"),
    QStringLiteral("/proc/meminfo"), QStringLiteral("/proc/stat"), QStringLiteral("/sys/devices/system/cpu"),
    QStringLiteral("/sys/fs/cgroup"), QStringLiteral("/etc/resolv.conf"), QStringLiteral("/etc/hosts"),
    QStringLiteral("/etc/nsswitch.conf"), QStringLiteral("/etc/host.conf"), QStringLiteral("/etc/gai.conf"),
};
const QStringList kSystemReadWrites = {QStringLiteral("/dev/null"), QStringLiteral("/dev/zero")};

#if defined(__x86_64__)
constexpr bool kSeccomp = true;
#else
constexpr bool kSeccomp = false;
#endif

class Filter {
public:
    void arch()
    {
        load(offsetof(seccomp_data, arch));
        jumpIfNot(AUDIT_ARCH_X86_64, 1);
        retKill();
        load(offsetof(seccomp_data, nr));
        m_code.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1));
        ret(SECCOMP_RET_ERRNO | ENOSYS);
    }
    void deny(long nr, int error)
    {
        m_code.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<std::uint32_t>(nr), 0, 1));
        ret(SECCOMP_RET_ERRNO | static_cast<std::uint32_t>(error));
    }
    /// Denies `nr` when its first argument has any bit of `mask`; the syscall number stays loaded.
    void denyFlags(long nr, std::uint32_t mask, int error)
    {
        m_code.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, static_cast<std::uint32_t>(nr), 0, 4));
        load(offsetof(seccomp_data, args[0]));
        m_code.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, mask, 0, 1));
        ret(SECCOMP_RET_ERRNO | static_cast<std::uint32_t>(error));
        ret(SECCOMP_RET_ALLOW);
    }
    /// Allows `socket` only for the given address families; the syscall number stays loaded otherwise.
    void socketFamilies(std::initializer_list<std::uint32_t> families)
    {
        const auto n = static_cast<std::uint8_t>(families.size());
        m_code.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_socket, 0, static_cast<std::uint8_t>(n + 3)));
        load(offsetof(seccomp_data, args[0]));
        std::uint8_t left = n;
        for (std::uint32_t family : families) {
            m_code.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, family, left, 0));
            --left;
        }
        ret(SECCOMP_RET_ERRNO | EACCES);
        ret(SECCOMP_RET_ALLOW);
    }
    void allow() { ret(SECCOMP_RET_ALLOW); }
    const std::vector<sock_filter> &code() const { return m_code; }

private:
    void load(std::uint32_t offset) { m_code.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offset)); }
    void jumpIfNot(std::uint32_t value, std::uint8_t skip)
    {
        m_code.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, value, skip, 0));
    }
    void ret(std::uint32_t value) { m_code.push_back(BPF_STMT(BPF_RET | BPF_K, value)); }
    void retKill() { ret(SECCOMP_RET_KILL_PROCESS); }

    std::vector<sock_filter> m_code;
};

std::vector<sock_filter> filterFor(Network network)
{
    Filter f;
    f.arch();
    for (long nr : {SYS_io_uring_setup, SYS_io_uring_enter, SYS_io_uring_register, SYS_clone3})
        f.deny(nr, ENOSYS);
    for (long nr : {SYS_ptrace, SYS_process_vm_readv, SYS_process_vm_writev, SYS_unshare, SYS_setns, SYS_mount,
                    SYS_umount2, SYS_pivot_root, SYS_move_mount, SYS_open_tree, SYS_fsopen, SYS_fsmount, SYS_bpf,
                    SYS_perf_event_open, SYS_userfaultfd, SYS_keyctl, SYS_add_key, SYS_request_key})
        f.deny(nr, EPERM);
    if (network == Network::None)
        f.deny(SYS_socket, EACCES);
    else
        f.socketFamilies({AF_UNIX, AF_INET, AF_INET6, AF_NETLINK});
    f.denyFlags(SYS_clone, CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWNET | CLONE_NEWPID | CLONE_NEWIPC | CLONE_NEWUTS |
                               CLONE_NEWCGROUP,
                EPERM);
    f.allow();
    return f.code();
}

}

struct Launcher::Impl {
    std::optional<Profile> profile;
    Coverage coverage;
    int abi = 0;
    bool seccomp = false;
    std::vector<PathRule> rules;
    std::vector<sock_filter> filter;
    void (*inChild)() = nullptr;

    void add(const QStringList &paths, std::uint64_t access)
    {
        for (const QString &path : paths) {
            const QFileInfo info(path);
            if (!info.exists())
                continue;
            const std::uint64_t handled = ll::handledFs(abi);
            const std::uint64_t allowed = (info.isDir() ? access : access & ll::kFileRights) & handled;
            rules.push_back({QFile::encodeName(info.absoluteFilePath()).toStdString(), allowed});
        }
    }

    void enter(QProcess &process) const noexcept
    {
        if (inChild)
            inChild();
        if (!profile)
            return;
        if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
            process.failChildProcessModifier("no_new_privs", errno);
        if (abi >= 1) {
            const ll::RulesetAttr attr{ll::handledFs(abi), profile->network == Network::None ? ll::handledNet(abi) : 0,
                                       abi >= 6 ? ll::kScopeAbstractUnix | ll::kScopeSignal : 0};
            const int ruleset = static_cast<int>(::syscall(ll::kCreateRuleset, &attr, sizeof attr, 0));
            if (ruleset < 0)
                process.failChildProcessModifier("landlock_create_ruleset", errno);
            for (const PathRule &rule : rules) {
                const int fd = ::open(rule.path.c_str(), O_PATH | O_CLOEXEC);
                if (fd < 0)
                    continue;
                const ll::PathBeneathAttr beneath{rule.access, fd};
                const long added = ::syscall(ll::kAddRule, ruleset, ll::kRulePathBeneath, &beneath, 0);
                const int error = errno;
                ::close(fd);
                if (added != 0)
                    process.failChildProcessModifier("landlock_add_rule", error);
            }
            if (::syscall(ll::kRestrictSelf, ruleset, 0) != 0)
                process.failChildProcessModifier("landlock_restrict_self", errno);
            ::close(ruleset);
        }
        if (seccomp) {
            const sock_fprog program{static_cast<unsigned short>(filter.size()),
                                     const_cast<sock_filter *>(filter.data())};
            if (::syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &program) != 0)
                process.failChildProcessModifier("seccomp", errno);
        }
    }
};

Launcher::Launcher(std::optional<Profile> profile) : m(std::make_unique<Impl>())
{
    m->profile = std::move(profile);
    if (!m->profile) {
        m->coverage.missing = QStringLiteral("started without a sandbox");
        return;
    }
    const Profile &p = *m->profile;
    m->abi = ll::abi();
    m->seccomp = kSeccomp && ::prctl(PR_GET_SECCOMP, 0, 0, 0, 0) >= 0;
    if (m->abi >= 1) {
        m->add(kLibraries, kReadExecute);
        m->add(kSystemReads, kRead);
        m->add(kSystemReadWrites, kRead | ll::kWriteFile);
        m->add(p.readExecute, kReadExecute);
        m->add(p.read, kRead);
        m->add(p.readWrite, kReadWrite);
    }
    if (m->seccomp)
        m->filter = filterFor(p.network);

    Coverage &c = m->coverage;
    c.files = m->abi >= 1;
    c.network = p.network == Network::Any || m->seccomp;
    QStringList missing;
    if (!c.files)
        missing << QStringLiteral("Landlock isn't available in this kernel, so files aren't confined");
    if (!c.network)
        missing << QStringLiteral("seccomp isn't available, so the network isn't confined");
    if (p.network == Network::Any && m->abi < 9)
        missing << QStringLiteral("connections to UNIX sockets outside the sandbox aren't limited (Landlock ABI %1)")
                       .arg(m->abi);
    c.missing = missing.join(QStringLiteral("; "));
}

Launcher::~Launcher() = default;

const Coverage &Launcher::coverage() const
{
    return m->coverage;
}

void Launcher::prepare(QProcess &process, void (*inChild)())
{
    m->inChild = inChild;
    process.setChildProcessModifier([impl = m.get(), &process] { impl->enter(process); });
}

void Launcher::killTree() {}

}
