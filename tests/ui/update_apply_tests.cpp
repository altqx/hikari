#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest>

#ifdef _WIN32
#include "hikari/app/update_apply.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <shellapi.h>

#include <map>
#include <optional>
#include <vector>

using namespace hikari::app;
using hikari::application::ApplyRequest;

namespace {

const std::string kVersion = "0.2.0";
const std::string kAsset = "HikariSub-0.2.0-windows-x64-setup.exe";
const QByteArray kOfficial = "the release's own installer";
const QByteArray kForged = "an installer someone else made";

std::string sha(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

std::string listed(const QByteArray &bytes, const std::string &name = kAsset)
{
    return sha(bytes) + "  " + name + "\n" + std::string(64, 'b') + "  hikarisub-0.2.0-linux-amd64.deb\n";
}

std::wstring native(const QString &path)
{
    return QDir::toNativeSeparators(path).toStdWString();
}

QByteArray readAll(const std::wstring &path)
{
    QFile f(QString::fromStdWString(path));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

std::wstring parentOf(const std::wstring &path)
{
    return path.substr(0, path.find_last_of(L'\\'));
}

struct Ran {
    int count = 0;
    std::wstring installer, arguments;
    QByteArray content;
    bool writable = false, renamable = false, folderProtected = false;
    std::wstring folderDacl;
};


struct FakeDisk {
    std::map<std::wstring, unsigned long> rights;
    std::map<std::wstring, std::vector<update_apply::Entry>> folders;

    bool isProtected(const std::wstring &dir) const
    {
        return update_apply::isProtected(
            dir,
            [this](const std::wstring &path) -> std::optional<unsigned long> {
                const auto found = rights.find(path);
                return found == rights.end() ? std::nullopt : std::optional<unsigned long>(found->second);
            },
            [this](const std::wstring &folder) -> std::optional<std::vector<update_apply::Entry>> {
                const auto found = folders.find(folder);
                return found == folders.end() ? std::nullopt : std::optional(found->second);
            });
    }
};

// Program Files' rights as Windows sets them up.
FakeDisk programFiles()
{
    const unsigned long read = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
    FakeDisk disk;
    disk.rights[L"C:\\"] = read | FILE_ADD_SUBDIRECTORY;
    disk.rights[L"C:\\Program Files"] = read;
    disk.rights[L"C:\\Program Files\\HikariSub"] = read;
    disk.rights[L"C:\\Program Files\\HikariSub\\HikariSub.exe"] = read;
    disk.rights[L"C:\\Program Files\\HikariSub\\Qt6Core.dll"] = read;
    disk.rights[L"C:\\Program Files\\HikariSub\\platforms"] = read;
    disk.rights[L"C:\\Program Files\\HikariSub\\platforms\\qwindows.dll"] = read;
    disk.folders[L"C:\\Program Files\\HikariSub"] = {
        {L"HikariSub.exe", false, false}, {L"Qt6Core.dll", false, false}, {L"platforms", true, false}};
    disk.folders[L"C:\\Program Files\\HikariSub\\platforms"] = {{L"qwindows.dll", false, false}};
    return disk;
}

}
#endif

class UpdateApplyTest : public QObject {
    Q_OBJECT
#ifdef _WIN32
    QTemporaryDir dir;
    int fixtures = 0;

    struct Fixture {
        std::wstring downloads, staging, file;
        std::optional<std::string> sums;
        int fetches = 0;
        Ran ran;

        update_apply::Environment environment(bool allUsers)
        {
            update_apply::Environment env;
            env.installed = "0.1.0";
            env.installDir = L"C:\\Program Files\\HikariSub";
            env.allUsers = allUsers;
            env.stagingParent = staging;
            env.fetchSums = [this](const std::string &version) {
                ++fetches;
                return version == kVersion ? sums : std::nullopt;
            };
            env.run = [this](const std::wstring &installer, const std::wstring &arguments) {
                ++ran.count;
                ran.installer = installer;
                ran.arguments = arguments;
                ran.content = readAll(installer);
                HANDLE write = CreateFileW(installer.c_str(), GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                ran.writable = write != INVALID_HANDLE_VALUE;
                if (ran.writable)
                    CloseHandle(write);
                const std::wstring moved = installer + L".moved";
                ran.renamable = MoveFileW(installer.c_str(), moved.c_str()) != 0;
                if (ran.renamable)
                    MoveFileW(moved.c_str(), installer.c_str());
                PACL dacl = nullptr;
                PSECURITY_DESCRIPTOR descriptor = nullptr;
                if (GetNamedSecurityInfoW(parentOf(installer).c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                          nullptr, nullptr, &dacl, nullptr, &descriptor) == ERROR_SUCCESS) {
                    LPWSTR text = nullptr;
                    if (ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, SDDL_REVISION_1,
                                                                             DACL_SECURITY_INFORMATION, &text, nullptr)) {
                        ran.folderDacl = text;
                        LocalFree(text);
                    }
                    SECURITY_DESCRIPTOR_CONTROL control = 0;
                    DWORD revision = 0;
                    GetSecurityDescriptorControl(descriptor, &control, &revision);
                    ran.folderProtected = (control & SE_DACL_PROTECTED) != 0;
                    LocalFree(descriptor);
                }
                return true;
            };
            return env;
        }

        ApplyRequest request() const
        {
            ApplyRequest r;
            r.version = kVersion;
            r.file = QString::fromStdWString(file).toStdString();
            r.pid = 4242;
            r.relaunch = true;
            return r;
        }

        int stagedFolders() const
        {
            return int(QDir(QString::fromStdWString(staging))
                           .entryList({QStringLiteral("HikariSub-update-*")}, QDir::Dirs | QDir::Hidden | QDir::System)
                           .size());
        }
    };

    Fixture fixture(const QByteArray &content)
    {
        const QString root = dir.filePath(QString::number(++fixtures));
        QDir().mkpath(root + QStringLiteral("/downloads"));
        QDir().mkpath(root + QStringLiteral("/staging"));
        Fixture f;
        f.downloads = native(root + QStringLiteral("/downloads"));
        f.staging = native(root + QStringLiteral("/staging"));
        f.file = f.downloads + L"\\" + QString::fromStdString(kAsset).toStdWString();
        QFile out(QString::fromStdWString(f.file));
        if (out.open(QIODevice::WriteOnly))
            out.write(content);
        return f;
    }
#endif

private slots:
    void initTestCase()
    {
#ifndef _WIN32
        QSKIP("the installing side is Windows'; the Linux helper is tested in the package containers");
#endif
    }

    // Not _WIN32: moc under MSVC sees no compiler-predefined macros, so those slots never ran.
#if HIKARI_UPDATE_APPLY_WINDOWS_TESTS
    void theCommandLineReadsBack()
    {
        ApplyRequest request;
        request.version = "0.2.0-beta.1";
        request.file = "C:\\Users\\a b\\AppData\\Local\\HikariSub\\Updates\\0.2.0-beta.1\\setup.exe";
        request.pid = 7;
        const std::wstring line = L"HikariSub.exe " + update_apply::commandLine(request);
        int count = 0;
        LPWSTR *argv = CommandLineToArgvW(line.c_str(), &count);
        QVERIFY(argv);
        QCOMPARE(count, 6);
        QCOMPARE(QString::fromWCharArray(argv[1]), QStringLiteral("--hikari-apply-update"));
        std::vector<std::string> rest;
        for (int i = 2; i < count; ++i)
            rest.push_back(QString::fromWCharArray(argv[i]).toStdString());
        LocalFree(argv);
        const auto parsed = hikari::application::parseApplyArguments(rest);
        QVERIFY(parsed);
        QCOMPARE(parsed->version, request.version);
        QCOMPARE(parsed->file, request.file);
        QCOMPARE(parsed->pid, 7ul);
        QVERIFY(!parsed->relaunch);
    }

    void allUsersRunsAVerifiedCopyFromAPrivateFolder()
    {
        Fixture f = fixture(kOfficial);
        f.sums = listed(kOfficial);
        QCOMPARE(update_apply::apply(f.request(), f.environment(true)), 0);
        QCOMPARE(f.ran.count, 1);
        QVERIFY(f.ran.installer != f.file);
        QVERIFY(parentOf(parentOf(f.ran.installer)) == f.staging);
        QCOMPARE(f.ran.content, kOfficial);
        QVERIFY(!f.ran.writable);
        QVERIFY(!f.ran.renamable);
        QVERIFY(f.ran.folderProtected);
        for (const wchar_t *sid : {L";;;BU)", L";;;WD)", L";;;AU)", L";;;IU)"})
            QVERIFY2(f.ran.folderDacl.find(sid) == std::wstring::npos, qPrintable(QString::fromStdWString(f.ran.folderDacl)));
        QVERIFY(f.ran.folderDacl.find(L";;;SY)") != std::wstring::npos);
        QVERIFY(f.ran.folderDacl.find(L";;;BA)") != std::wstring::npos);
        QCOMPARE(QString::fromStdWString(f.ran.arguments),
                 QStringLiteral("/S /UPDATE /WAITPID=4242 /RELAUNCH /ALLUSERS /D=C:\\Program Files\\HikariSub"));
    }

    void aForgedInstallerIsRefusedBeforeItRuns()
    {
        Fixture f = fixture(kForged);
        f.sums = listed(kOfficial);
        QCOMPARE(update_apply::apply(f.request(), f.environment(true)), 3);
        QCOMPARE(update_apply::apply(f.request(), f.environment(false)), 3);
        QCOMPARE(f.ran.count, 0);
        QCOMPARE(f.stagedFolders(), 0);
    }

    void aDigestListedUnderAnotherNameIsRefused()
    {
        Fixture f = fixture(kForged);
        f.sums = listed(kOfficial) + sha(kForged) + "  HikariSub-0.2.0-windows-x64.zip\n";
        QCOMPARE(update_apply::apply(f.request(), f.environment(true)), 3);
        f.sums = sha(kForged) + "  HikariSub-0.3.0-windows-x64-setup.exe\n";
        QCOMPARE(update_apply::apply(f.request(), f.environment(true)), 3);
        QCOMPARE(f.ran.count, 0);
        QCOMPARE(f.stagedFolders(), 0);
    }

    void nothingRunsWithoutTheReleaseSums()
    {
        Fixture f = fixture(kOfficial);
        QCOMPARE(update_apply::apply(f.request(), f.environment(true)), 6);
        QCOMPARE(f.ran.count, 0);
        QCOMPARE(f.stagedFolders(), 0);
    }

    void aVersionThatIsNotNewerIsRefused()
    {
        Fixture f = fixture(kOfficial);
        f.sums = listed(kOfficial);
        auto env = f.environment(true);
        env.installed = "0.2.0";
        QCOMPARE(update_apply::apply(f.request(), env), 2);
        env.installed = "0.3.0";
        QCOMPARE(update_apply::apply(f.request(), env), 2);
        auto request = f.request();
        request.version = "0.2.0+evil";
        QCOMPARE(update_apply::apply(request, f.environment(true)), 2);
        QCOMPARE(f.fetches, 0);
        QCOMPARE(f.ran.count, 0);
    }

    void userScopeVerifiesTheDownloadWhereItIs()
    {
        Fixture f = fixture(kOfficial);
        f.sums = listed(kOfficial);
        QCOMPARE(update_apply::apply(f.request(), f.environment(false)), 0);
        QCOMPARE(f.ran.count, 1);
        QVERIFY(f.ran.installer == f.file);
        QCOMPARE(f.ran.content, kOfficial);
        QVERIFY(!f.ran.writable);
        QVERIFY(!f.ran.renamable);
        QCOMPARE(QString::fromStdWString(f.ran.arguments),
                 QStringLiteral("/S /UPDATE /WAITPID=4242 /RELAUNCH /CURRENTUSER /D=C:\\Program Files\\HikariSub"));
        QCOMPARE(f.stagedFolders(), 0);
    }

    void anInstallerThatDoesNotStartIsReported()
    {
        Fixture f = fixture(kOfficial);
        f.sums = listed(kOfficial);
        auto env = f.environment(true);
        env.run = [](const std::wstring &, const std::wstring &) { return false; };
        QCOMPARE(update_apply::apply(f.request(), env), 4);
        QCOMPARE(f.stagedFolders(), 0);
    }

    void elevatesOnlyAProgramTheUserCannotChange()
    {
        const std::wstring dir = L"C:\\Program Files\\HikariSub";
        QVERIFY(programFiles().isProtected(dir));
        QVERIFY(programFiles().isProtected(dir + L"\\"));
        const unsigned long changes[] = {FILE_WRITE_DATA, FILE_APPEND_DATA, DELETE, WRITE_DAC, WRITE_OWNER};
        for (unsigned long change : changes) {
            FakeDisk dll = programFiles();
            dll.rights[dir + L"\\Qt6Core.dll"] |= change;
            QVERIFY(!dll.isProtected(dir));
            FakeDisk plugin = programFiles();
            plugin.rights[dir + L"\\platforms\\qwindows.dll"] |= change;
            QVERIFY(!plugin.isProtected(dir));
            FakeDisk folder = programFiles();
            folder.rights[dir + L"\\platforms"] |= change;
            QVERIFY(!folder.isProtected(dir));
        }
        const unsigned long replaces[] = {FILE_DELETE_CHILD, DELETE, WRITE_DAC, WRITE_OWNER};
        for (unsigned long replace : replaces) {
            FakeDisk way = programFiles();
            way.rights[L"C:\\Program Files"] |= replace;
            QVERIFY(!way.isProtected(dir));
            FakeDisk root = programFiles();
            root.rights[L"C:\\"] |= replace;
            QVERIFY(!root.isProtected(dir));
        }
        FakeDisk unknown = programFiles();
        unknown.rights.erase(dir + L"\\Qt6Core.dll");
        QVERIFY(!unknown.isProtected(dir));
        FakeDisk unlisted = programFiles();
        unlisted.folders.erase(dir + L"\\platforms");
        QVERIFY(!unlisted.isProtected(dir));
        FakeDisk link = programFiles();
        link.folders[dir].push_back({L"qml", true, true});
        QVERIFY(!link.isProtected(dir));
        const FakeDisk disk = programFiles();
        QVERIFY(!disk.isProtected(L"\\\\server\\share\\HikariSub"));
        QVERIFY(!disk.isProtected(L"\\\\?\\C:\\Program Files\\HikariSub"));
        QVERIFY(!disk.isProtected(L"C:\\Program Files\\..\\Program Files\\HikariSub"));
        QVERIFY(!disk.isProtected(L"C:\\"));
        QVERIFY(!disk.isProtected(L""));
    }

    // GitHub redirects release downloads to its asset hosts.
    void theRealFetchFollowsOnlyAllowedHosts()
    {
        if (qEnvironmentVariable("HIKARI_TEST_NETWORK") != QLatin1String("1"))
            QSKIP("HIKARI_TEST_NETWORK=1 fetches from GitHub");
        const std::string url = hikari::application::sumsUrl("0.0.1");
        const auto anyGitHub = [](const std::string &u) {
            for (const char *prefix : {"https://github.com/", "https://objects.githubusercontent.com/",
                                       "https://release-assets.githubusercontent.com/"})
                if (u.rfind(prefix, 0) == 0)
                    return true;
            return false;
        };
        const auto sums = update_apply::fetchHttps(url, anyGitHub, 65536);
        QVERIFY(sums);
        QVERIFY(QByteArray::fromStdString(*sums).contains("HikariSub-0.0.1-windows-x64.zip"));
        const auto githubOnly = [](const std::string &u) { return u.rfind("https://github.com/", 0) == 0; };
        QVERIFY(!update_apply::fetchHttps(url, githubOnly, 65536));
        QVERIFY(!update_apply::fetchHttps(url, [](const std::string &) { return false; }, 65536));
    }

    void theUsersOwnFolderIsChangeable()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile exe(dir.filePath(QStringLiteral("HikariSub.exe")));
        QVERIFY(exe.open(QIODevice::WriteOnly));
        exe.close();
        QVERIFY(!update_apply::isProtectedFromUser(native(dir.path())));
    }

    void aStartDuringTheSetupWaitsForIt()
    {
        QVERIFY(!update_apply::setupRunning());
        HANDLE held = CreateMutexW(nullptr, TRUE, update_apply::kSetupMutex);
        QVERIFY(held != nullptr);
        QVERIFY(update_apply::setupRunning());
        ReleaseMutex(held);
        CloseHandle(held);
        QVERIFY(!update_apply::setupRunning());

        QVERIFY(!update_apply::askStartAfterSetup());
        HANDLE request = CreateEventW(nullptr, TRUE, FALSE, update_apply::kStartAfterSetup);
        QVERIFY(request != nullptr);
        QCOMPARE(WaitForSingleObject(request, 0), DWORD(WAIT_TIMEOUT));
        QVERIFY(update_apply::askStartAfterSetup());
        QCOMPARE(WaitForSingleObject(request, 0), DWORD(WAIT_OBJECT_0));
        CloseHandle(request);

        const std::wstring helper = native(QStringLiteral(HIKARI_SETUP_MUTEX_HELPER));
        QVERIFY(update_apply::launch(helper, std::wstring(update_apply::kSetupMutex) + L" 1000 8000"));
        QVERIFY(update_apply::setupRunning());
        QTRY_VERIFY_WITH_TIMEOUT(!update_apply::setupRunning(), 20000);
    }
#endif
};

QTEST_GUILESS_MAIN(UpdateApplyTest)
#include "update_apply_tests.moc"
