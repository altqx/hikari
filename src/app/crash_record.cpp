#include "hikari/app/crash_record.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QRegularExpression>

#include <atomic>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <csignal>
#else
#include <csignal>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace hikari::app::crash {

namespace {

// Everything the handler needs, set up before any crash: it allocates
// nothing and calls nothing that could take a lock it might hold.
constexpr std::size_t kPathRoom = 4096;
constexpr std::size_t kTextRoom = 64;
constexpr std::size_t kRecordRoom = 8192;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_writing{false};
char g_version[kTextRoom] = {};
char g_record[kRecordRoom] = {};
#ifdef _WIN32
wchar_t g_path[kPathRoom] = {};
wchar_t g_program[kPathRoom] = {};
wchar_t g_system[kPathRoom] = {};
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
using SignalHandler = void (*)(int);
SignalHandler g_previousAbort = SIG_DFL;
#else
char g_path[kPathRoom] = {};
char g_program[kPathRoom] = {};
// The executable (this code is linked into it): its load address and file name.
const void *g_executableBase = nullptr;
char g_executable[256] = {};
struct sigaction g_previous[5];
constexpr int kSignals[5] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
char g_altStack[64 * 1024];
#endif

// `value` as "0x…" in `out` (room for 19).
void hex(char *out, quint64 value)
{
    char reversed[16];
    int n = 0;
    do {
        reversed[n++] = "0123456789abcdef"[value & 0xF];
        value >>= 4;
    } while (value && n < 16);
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < n; ++i)
        out[2 + i] = reversed[n - 1 - i];
    out[2 + n] = 0;
}

// A record being written in g_record.
struct Writer {
    std::size_t at = 0;
    void put(const char *text)
    {
        while (*text && at + 1 < kRecordRoom)
            g_record[at++] = *text++;
    }
    void putHex(quint64 value)
    {
        char digits[19];
        hex(digits, value);
        put(digits);
    }
    void begin(const char *type, const char *mechanism)
    {
        put("hikari-crash 1\nversion ");
        put(g_version);
        put("\ntype ");
        put(type);
        put("\nmechanism ");
        put(mechanism);
        put("\n");
    }
    void frame(const char *module, quint64 offset)
    {
        put("frame ");
        // Only the characters the reader takes; a space would split the line.
        for (const char *p = module; *p && at + 1 < kRecordRoom; ++p) {
            const char c = *p;
            const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                               c == '.' || c == '+' || c == '-';
            g_record[at++] = plain ? c : '_';
        }
        put(" ");
        putHex(offset);
        put("\n");
    }
};

// Copies `text` into `out` (room `room`) as a C string, cut to fit.
template <typename Char>
void copyInto(Char *out, std::size_t room, const Char *text, std::size_t length)
{
    const std::size_t n = length < room - 1 ? length : room - 1;
    std::memcpy(out, text, n * sizeof(Char));
    out[n] = 0;
}

#ifdef _WIN32

const char *codeName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN";
    case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
    case 0xE06D7363: return "CPP_EXCEPTION";
    default: return nullptr;
    }
}

bool startsWith(const wchar_t *path, const wchar_t *folder)
{
    const std::size_t n = wcslen(folder);
    return n > 0 && _wcsnicmp(path, folder, n) == 0 && (path[n] == L'\\' || path[n] == L'/');
}

// The module holding `address` by file name (own or system), else "other";
// its base in `base`.
void moduleOf(const void *address, char *name, std::size_t room, quint64 &base)
{
    copyInto(name, room, "other", 5);
    base = 0;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) ||
        !module)
        return;
    base = reinterpret_cast<quint64>(module);
    wchar_t path[kPathRoom];
    const DWORD n = GetModuleFileNameW(module, path, DWORD(kPathRoom));
    if (n == 0 || n >= kPathRoom || !(startsWith(path, g_program) || startsWith(path, g_system)))
        return;
    const wchar_t *file = path;
    for (const wchar_t *p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/')
            file = p + 1;
    std::size_t i = 0;
    for (; file[i] && i + 1 < room; ++i)
        name[i] = file[i] < 128 ? char(file[i]) : '_'; // Writer::frame keeps the plain ones
    name[i] = 0;
}

void addFrame(Writer &w, quint64 address)
{
    char name[128];
    quint64 base = 0;
    moduleOf(reinterpret_cast<const void *>(address), name, sizeof name, base);
    w.frame(name, base ? address - base : 0);
}

void writeOut(const Writer &w)
{
    const HANDLE file = CreateFileW(g_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, g_record, DWORD(w.at), &written, nullptr);
    CloseHandle(file);
}

LONG WINAPI onException(EXCEPTION_POINTERS *info)
{
    if (!g_writing.exchange(true)) {
        Writer w;
        char code[19];
        const DWORD value = info->ExceptionRecord->ExceptionCode;
        const char *name = codeName(value);
        if (!name) {
            hex(code, value);
            name = code;
        }
        w.begin(name, "seh");
#if defined(_M_X64)
        int frames = 0;
        // Unwinds from the faulting frame (the record's context), not the
        // filter's own stack.
        CONTEXT context = *info->ContextRecord;
        while (frames < kMaxFrames && context.Rip) {
            addFrame(w, context.Rip);
            ++frames;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
            if (!function) {
                // A leaf: its return address is on top of the stack.
                context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
                context.Rsp += 8;
                continue;
            }
            void *handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher,
                             nullptr);
        }
#else
        addFrame(w, reinterpret_cast<quint64>(info->ExceptionRecord->ExceptionAddress));
#endif
        writeOut(w);
    }
    return g_previousFilter ? g_previousFilter(info) : EXCEPTION_EXECUTE_HANDLER;
}

void onAbort(int)
{
    if (!g_writing.exchange(true)) {
        Writer w;
        w.begin("SIGABRT", "signal");
        void *stack[kMaxFrames];
        const USHORT n = RtlCaptureStackBackTrace(1, kMaxFrames, stack, nullptr);
        for (USHORT i = 0; i < n; ++i)
            addFrame(w, reinterpret_cast<quint64>(stack[i]));
        writeOut(w);
    }
    signal(SIGABRT, SIG_DFL);
    raise(SIGABRT);
}

#else

const char *signalName(int signal)
{
    switch (signal) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    default: return "SIGNAL";
    }
}

bool startsWith(const char *path, const char *folder)
{
    const std::size_t n = std::strlen(folder);
    return n > 0 && std::strncmp(path, folder, n) == 0 && path[n] == '/';
}

// The module holding `address` by file name (own or system), else "other";
// the offset into it in `offset`.
const char *moduleOf(const void *address, quint64 &offset)
{
    Dl_info info{};
    offset = 0;
    if (!dladdr(address, &info) || !info.dli_fbase)
        return "other";
    offset = reinterpret_cast<quint64>(address) - reinterpret_cast<quint64>(info.dli_fbase);
    const char *path = info.dli_fname ? info.dli_fname : "";
    // The executable comes back as argv[0], which may be relative.
    if (info.dli_fbase == g_executableBase)
        return g_executable;
    if (!(startsWith(path, g_program) || startsWith(path, "/usr") || startsWith(path, "/lib") ||
          startsWith(path, "/lib64")))
        return "other";
    const char *module = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/')
            module = p + 1;
    return module;
}

void onSignal(int signal, siginfo_t *, void *)
{
    if (!g_writing.exchange(true)) {
        Writer w;
        w.begin(signalName(signal), "signal");
        void *stack[kMaxFrames + 2];
        const int n = backtrace(stack, kMaxFrames + 2);
        // The first two are this handler and the signal trampoline.
        for (int i = 2; i < n; ++i) {
            quint64 offset = 0;
            const char *module = moduleOf(stack[i], offset);
            w.frame(module, offset);
        }
        const int fd = open(g_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd >= 0) {
            [[maybe_unused]] const ssize_t written = ::write(fd, g_record, w.at);
            close(fd);
        }
    }
    // The default action now: the core dump, the exit status.
    struct sigaction plain {};
    plain.sa_handler = SIG_DFL;
    sigemptyset(&plain.sa_mask);
    sigaction(signal, &plain, nullptr);
    raise(signal);
}

#endif

} // namespace

void install(const QString &recordPath, const QString &programFolder, const QString &version)
{
    if (g_installed.exchange(true))
        return;
    const QByteArray v = version.toLatin1();
    copyInto(g_version, kTextRoom, v.constData(), std::size_t(v.size()));
#ifdef _WIN32
    const std::wstring path = QDir::toNativeSeparators(recordPath).toStdWString();
    copyInto(g_path, kPathRoom, path.c_str(), path.size());
    const std::wstring program = QDir::toNativeSeparators(QDir::cleanPath(programFolder)).toStdWString();
    copyInto(g_program, kPathRoom, program.c_str(), program.size());
    const UINT n = GetSystemWindowsDirectoryW(g_system, UINT(kPathRoom));
    if (n == 0 || n >= kPathRoom)
        g_system[0] = 0;
    g_previousFilter = SetUnhandledExceptionFilter(onException);
    g_previousAbort = signal(SIGABRT, onAbort);
#else
    const QByteArray path = QFile::encodeName(recordPath);
    copyInto(g_path, kPathRoom, path.constData(), std::size_t(path.size()));
    const QByteArray program = QFile::encodeName(QDir::cleanPath(programFolder));
    copyInto(g_program, kPathRoom, program.constData(), std::size_t(program.size()));
    Dl_info self{};
    if (dladdr(reinterpret_cast<const void *>(&install), &self))
        g_executableBase = self.dli_fbase;
    const QByteArray executable = QFileInfo(QCoreApplication::applicationFilePath()).fileName().toLocal8Bit();
    copyInto(g_executable, sizeof g_executable, executable.constData(), std::size_t(executable.size()));
    // backtrace() loads libgcc on its first call: never first in the handler.
    void *warm[1];
    backtrace(warm, 1);
    // A stack overflow still finds room to write the record.
    stack_t stack{};
    stack.ss_sp = g_altStack;
    stack.ss_size = sizeof g_altStack;
    sigaltstack(&stack, nullptr);
    struct sigaction action {};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int i = 0; i < 5; ++i)
        sigaction(kSignals[i], &action, &g_previous[i]);
#endif
}

void uninstall()
{
    if (!g_installed.exchange(false))
        return;
#ifdef _WIN32
    SetUnhandledExceptionFilter(g_previousFilter);
    signal(SIGABRT, g_previousAbort);
#else
    for (int i = 0; i < 5; ++i)
        sigaction(kSignals[i], &g_previous[i], nullptr);
#endif
}

bool installed()
{
    return g_installed;
}

std::optional<Record> read(const QString &path)
{
    QFile file(path);
    if (file.size() > qint64(kRecordRoom) || !file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QStringList lines = QString::fromLatin1(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.value(0) != QLatin1String("hikari-crash 1"))
        return std::nullopt;
    static const QRegularExpression version(QStringLiteral("^[0-9A-Za-z.+-]{1,40}$"));
    static const QRegularExpression type(QStringLiteral("^(SIG[A-Z]{2,6}|[A-Z_]{4,40}|0x[0-9a-f]{1,8})$"));
    static const QRegularExpression module(QStringLiteral("^[A-Za-z0-9_.+-]{1,80}$"));
    static const QRegularExpression offset(QStringLiteral("^0x[0-9a-f]{1,16}$"));
    static const QRegularExpression seconds(QStringLiteral("^[0-9]{1,6}$"));
    Record record;
    for (const QString &line : lines.mid(1)) {
        const int space = line.indexOf(QLatin1Char(' '));
        const QString key = line.left(space);
        const QString value = space < 0 ? QString() : line.mid(space + 1);
        if (key == QLatin1String("version") && version.match(value).hasMatch())
            record.version = value;
        else if (key == QLatin1String("type") && type.match(value).hasMatch())
            record.type = value;
        else if (key == QLatin1String("mechanism") &&
                 (value == QLatin1String("signal") || value == QLatin1String("seh") || value == QLatin1String("watchdog")))
            record.mechanism = value;
        else if (key == QLatin1String("seconds") && seconds.match(value).hasMatch())
            record.seconds = value.toInt();
        else if (key == QLatin1String("frame") && record.frames.size() < kMaxFrames) {
            const QStringList parts = value.split(QLatin1Char(' '));
            if (parts.size() != 2 || !module.match(parts[0]).hasMatch() || !offset.match(parts[1]).hasMatch())
                return std::nullopt;
            record.frames.append({parts[0], parts[1].mid(2).toULongLong(nullptr, 16)});
        } else
            return std::nullopt;
    }
    if (record.type.isEmpty() || record.mechanism.isEmpty() || record.version.isEmpty())
        return std::nullopt;
    // A freeze is APP_HANG and only a freeze is; only a freeze has seconds.
    if (record.freeze() != (record.type == QLatin1String(kFreezeType)) || (!record.freeze() && record.seconds))
        return std::nullopt;
    return record;
}

bool write(const QString &path, const Record &record)
{
    QByteArray text = "hikari-crash 1\nversion " + record.version.toLatin1() + "\ntype " + record.type.toLatin1() +
                      "\nmechanism " + record.mechanism.toLatin1() + '\n';
    if (record.freeze())
        text += "seconds " + QByteArray::number(record.seconds) + '\n';
    for (const Frame &frame : record.frames.mid(0, kMaxFrames))
        text += "frame " + frame.module.toLatin1() + " 0x" + QByteArray::number(frame.offset, 16) + '\n';
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(text);
    return file.commit();
}

QList<Frame> framesOf(const quintptr *addresses, int count)
{
    QList<Frame> frames;
    for (int i = 0; i < count && frames.size() < kMaxFrames; ++i) {
#ifdef _WIN32
        char name[128];
        quint64 base = 0;
        moduleOf(reinterpret_cast<const void *>(addresses[i]), name, sizeof name, base);
        Frame frame{QString::fromLatin1(name), base ? quint64(addresses[i]) - base : 0};
#else
        quint64 offset = 0;
        const char *module = moduleOf(reinterpret_cast<const void *>(addresses[i]), offset);
        Frame frame{QString::fromLatin1(module), offset};
#endif
        // As Writer::frame keeps it, so read() takes what write() wrote.
        for (QChar &c : frame.module)
            if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != u'_' && c != u'.' && c != u'+' && c != u'-')
                c = u'_';
        frames.append(frame);
    }
    return frames;
}

} // namespace hikari::app::crash
