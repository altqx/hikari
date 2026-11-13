#include "hikari/backends/git_reader.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <optional>

using hikari::application::VcsFailure;
using hikari::application::VcsFile;
using hikari::application::VcsProblem;
using hikari::application::VcsRefs;
using hikari::backends::GitReader;

namespace {

template <typename T> std::expected<T, VcsFailure> waitFor(std::optional<std::expected<T, VcsFailure>> &answer)
{
    QElapsedTimer t;
    t.start();
    while (!answer && t.elapsed() < 30'000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!answer)
        return std::unexpected(VcsFailure{VcsProblem::Failed, "no answer"});
    return *answer;
}

class EnvironmentVariable {
public:
    EnvironmentVariable(const char *name, const QByteArray &value) : m_name(name), m_had(qEnvironmentVariableIsSet(name))
    {
        m_old = qgetenv(name);
        qputenv(name, value);
    }
    ~EnvironmentVariable()
    {
        if (m_had)
            qputenv(m_name, m_old);
        else
            qunsetenv(m_name);
    }

private:
    const char *m_name;
    bool m_had;
    QByteArray m_old;
};

constexpr char kOld[] = "[Script Info]\nTitle: old\n";
constexpr char kNew[] = "[Script Info]\nTitle: new\n";

class GitReaderTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        static int argc = 1;
        static char name[] = "git_reader_tests";
        static char *argv[] = {name, nullptr};
        if (!QCoreApplication::instance())
            new QCoreApplication(argc, argv);
    }

    void SetUp() override
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            GTEST_SKIP() << "git isn't installed";
        ASSERT_TRUE(QDir().mkpath(home()));
        m_home.emplace("HOME", home().toUtf8());
        m_xdg.emplace("XDG_CONFIG_HOME", (home() + QStringLiteral("/.config")).toUtf8());
        m_system.emplace("GIT_CONFIG_NOSYSTEM", "1");
        ASSERT_TRUE(QDir().mkpath(repo() + QStringLiteral("/season1")));
        git({QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main")});
        write(QStringLiteral("season1/a.ass"), kOld);
        git({QStringLiteral("add"), QStringLiteral(".")});
        commit(QStringLiteral("First pass"));
        git({QStringLiteral("tag"), QStringLiteral("-a"), QStringLiteral("v1"), QStringLiteral("-m"), QStringLiteral("One")});
        git({QStringLiteral("branch"), QStringLiteral("draft")});
        write(QStringLiteral("season1/a.ass"), kNew);
        const QString text = QString::fromUtf8("Title: \xC3\xA9t\xC3\xA9");
        const QByteArray utf16 = QByteArray("\xFF\xFE", 2) + QByteArray(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
        write(QStringLiteral("season1/u16.ass"), utf16);
        write(QStringLiteral("season1/blob.ass"), QByteArray("\x00\x01\x02", 3));
        git({QStringLiteral("add"), QStringLiteral(".")});
        commit(QStringLiteral("Time the OP"));
    }

    QString home() const { return m_dir.filePath(QStringLiteral("home")); }
    QString repo() const { return m_dir.filePath(QStringLiteral("repo")); }
    QString file(const QString &name = QStringLiteral("a.ass")) const { return repo() + QStringLiteral("/season1/") + name; }
    QString marker() const { return m_dir.filePath(QStringLiteral("ran")); }

    void write(const QString &path, const QByteArray &bytes)
    {
        QFile f(path.startsWith(u'/') || QDir::isAbsolutePath(path) ? path : repo() + u'/' + path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(bytes);
    }

    QByteArray git(const QStringList &args, int *exitCode = nullptr)
    {
        QProcess p;
        p.setWorkingDirectory(repo());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const char *name : {"GIT_AUTHOR_NAME", "GIT_COMMITTER_NAME"})
            env.insert(QString::fromLatin1(name), QStringLiteral("Ana"));
        for (const char *name : {"GIT_AUTHOR_EMAIL", "GIT_COMMITTER_EMAIL"})
            env.insert(QString::fromLatin1(name), QStringLiteral("ana@example.com"));
        p.setProcessEnvironment(env);
        p.start(QStandardPaths::findExecutable(QStringLiteral("git")), args);
        p.closeWriteChannel();
        EXPECT_TRUE(p.waitForFinished(30'000));
        if (exitCode)
            *exitCode = p.exitCode();
        else
            EXPECT_EQ(p.exitCode(), 0) << args.join(u' ').toStdString() << ": " << p.readAllStandardError().toStdString();
        return p.readAllStandardOutput().trimmed();
    }

    void commit(const QString &message)
    {
        git({QStringLiteral("-c"), QStringLiteral("commit.gpgSign=false"), QStringLiteral("commit"), QStringLiteral("-q"),
             QStringLiteral("-m"), message});
    }

    std::expected<VcsRefs, VcsFailure> refs(const QString &at, int commits = 50)
    {
        std::optional<std::expected<VcsRefs, VcsFailure>> answer;
        reader.refs(at.toStdString(), commits, [&](std::expected<VcsRefs, VcsFailure> r) { answer = std::move(r); });
        return waitFor(answer);
    }

    std::expected<VcsFile, VcsFailure> show(const QString &path, const std::string &ref)
    {
        std::optional<std::expected<VcsFile, VcsFailure>> answer;
        reader.show(path.toStdString(), ref, [&](std::expected<VcsFile, VcsFailure> r) { answer = std::move(r); });
        return waitFor(answer);
    }

    VcsProblem problemOf(const std::expected<VcsFile, VcsFailure> &r)
    {
        EXPECT_FALSE(r);
        return r ? VcsProblem::Failed : r.error().problem;
    }

    QString scriptNotingItRan()
    {
        const QString path = m_dir.filePath(QStringLiteral("hostile.sh"));
        write(path, QStringLiteral("#!/bin/sh\necho \"$0 $*\" >> '%1'\ncat > /dev/null 2>&1\nexit 0\n").arg(marker()).toUtf8());
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

    void giveHeadAPgpSignature()
    {
        QByteArray body = git({QStringLiteral("cat-file"), QStringLiteral("commit"), QStringLiteral("HEAD")}) + '\n';
        const qsizetype end = body.indexOf('\n', body.indexOf("committer "));
        body.insert(end + 1, "gpgsig -----BEGIN PGP SIGNATURE-----\n \n iQEzBAABCAAdFiEE\n -----END PGP SIGNATURE-----\n");
        const QString object = m_dir.filePath(QStringLiteral("signed"));
        write(object, body);
        const QByteArray signedCommit = git({QStringLiteral("hash-object"), QStringLiteral("-t"), QStringLiteral("commit"),
                                             QStringLiteral("-w"), object});
        git({QStringLiteral("update-ref"), QStringLiteral("refs/heads/main"), QString::fromLatin1(signedCommit)});
    }

    QString hostileConfig()
    {
        const QString script = scriptNotingItRan();
        const QString hooks = m_dir.filePath(QStringLiteral("hooks"));
        QDir().mkpath(hooks);
        for (const char *hook : {"post-checkout", "pre-commit", "reference-transaction", "post-index-change", "pre-auto-gc"})
            QFile::copy(script, hooks + u'/' + QString::fromLatin1(hook));
        return QStringLiteral("[core]\n\tpager = '%1'\n\tfsmonitor = %1\n\thooksPath = %2\n"
                              "[diff \"x\"]\n\ttextconv = '%1'\n[filter \"x\"]\n\tsmudge = '%1'\n\tclean = '%1'\n"
                              "[gpg]\n\tprogram = %1\n[log]\n\tshowSignature = true\n")
            .arg(script, hooks);
    }

    void armTheTraps()
    {
        write(QStringLiteral(".gitattributes"), "* diff=x filter=x\n");
        int code = 0;
        for (const QStringList &naive :
             {QStringList{QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--show-signature")},
              QStringList{QStringLiteral("cat-file"), QStringLiteral("--filters"), QStringLiteral("HEAD:season1/a.ass")},
              QStringList{QStringLiteral("-c"), QStringLiteral("log.showSignature=false"), QStringLiteral("log"),
                          QStringLiteral("-p"), QStringLiteral("-1"), QStringLiteral("--"),
                          QStringLiteral("season1/a.ass")}}) {
            QFile::remove(marker());
            git(naive, &code);
            EXPECT_TRUE(QFile::exists(marker())) << "the trap didn't go off for git " << naive.join(u' ').toStdString();
        }
        QFile::remove(marker());
    }

    void readEverything()
    {
        const auto listed = refs(repo() + QStringLiteral("/season1"));
        ASSERT_TRUE(listed) << listed.error().message;
        EXPECT_EQ(listed->commits.size(), 2u);
        for (const char *ref : {"HEAD", "v1", "draft", "main~1"}) {
            const auto shown = show(file(), ref);
            ASSERT_TRUE(shown) << ref << ": " << shown.error().message;
        }
        QFile ran(marker());
        EXPECT_FALSE(ran.exists()) << (ran.open(QIODevice::ReadOnly) ? ran.readAll().toStdString() : std::string());
    }

    QTemporaryDir m_dir;
    std::optional<EnvironmentVariable> m_home, m_xdg, m_system;
    GitReader reader;
};

TEST_F(GitReaderTest, RefsListBranchesTagsAndTheNewestCommits)
{
    const auto listed = refs(repo() + QStringLiteral("/season1"), 1);
    ASSERT_TRUE(listed) << listed.error().message;
    EXPECT_EQ(QDir(QString::fromStdString(listed->root)).canonicalPath(), QDir(repo()).canonicalPath());
    EXPECT_EQ(listed->prefix, "season1/");
    EXPECT_EQ(listed->head, "main");
    EXPECT_EQ(listed->headCommit, git({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}).toStdString());
    ASSERT_EQ(listed->branches.size(), 2u);
    for (const auto &branch : listed->branches)
        EXPECT_EQ(branch.current, branch.name == "main") << branch.name;
    ASSERT_EQ(listed->tags.size(), 1u);
    EXPECT_EQ(listed->tags[0].name, "v1");
    EXPECT_EQ(listed->tags[0].commit, git({QStringLiteral("rev-parse"), QStringLiteral("HEAD~1")}).toStdString());
    ASSERT_EQ(listed->commits.size(), 1u);
    EXPECT_EQ(listed->commits[0].summary, "Time the OP");
    EXPECT_EQ(listed->commits[0].author, "Ana");
    EXPECT_GT(listed->commits[0].time, 0);

    git({QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--detach"), QStringLiteral("v1")});
    const auto detached = refs(repo());
    ASSERT_TRUE(detached);
    EXPECT_FALSE(detached->head);
    EXPECT_EQ(detached->prefix, "");
    EXPECT_EQ(detached->commits.size(), 1u);
}

TEST_F(GitReaderTest, AnEmptyRepositoryHasNoHead)
{
    const QString empty = m_dir.filePath(QStringLiteral("empty"));
    QDir().mkpath(empty);
    QProcess::execute(QStandardPaths::findExecutable(QStringLiteral("git")), {QStringLiteral("init"), QStringLiteral("-q"), empty});
    const auto listed = refs(empty);
    ASSERT_TRUE(listed) << listed.error().message;
    EXPECT_TRUE(listed->headCommit.empty());
    EXPECT_TRUE(listed->commits.empty());
}

TEST_F(GitReaderTest, ShowReadsTheFileAtEachRef)
{
    for (const auto &[ref, text] : {std::pair{"HEAD", kNew}, std::pair{"v1", kOld}, std::pair{"draft", kOld},
                                    std::pair{"HEAD~1", kOld}, std::pair{"main", kNew}}) {
        const auto shown = show(file(), ref);
        ASSERT_TRUE(shown) << ref << ": " << shown.error().message;
        EXPECT_EQ(shown->text, text) << ref;
        EXPECT_EQ(shown->path, "season1/a.ass");
        EXPECT_EQ(shown->commit.size(), 40u);
    }
}

TEST_F(GitReaderTest, Utf16IsReadAsUtf8WithoutItsByteOrderMark)
{
    const auto shown = show(file(QStringLiteral("u16.ass")), "HEAD");
    ASSERT_TRUE(shown) << shown.error().message;
    EXPECT_EQ(shown->text, "Title: \xC3\xA9t\xC3\xA9");
    EXPECT_EQ(problemOf(show(file(QStringLiteral("blob.ass")), "HEAD")), VcsProblem::NotText);
}

TEST_F(GitReaderTest, AMissingRefFileOrRepositorySaysWhich)
{
    EXPECT_EQ(problemOf(show(file(), "nope")), VcsProblem::NoRef);
    EXPECT_EQ(problemOf(show(file(QStringLiteral("u16.ass")), "v1")), VcsProblem::NoFile);
    EXPECT_EQ(problemOf(show(file(QStringLiteral("never.ass")), "HEAD")), VcsProblem::NoFile);
    const QString outside = m_dir.filePath(QStringLiteral("outside"));
    QDir().mkpath(outside);
    EXPECT_EQ(problemOf(show(outside + QStringLiteral("/a.ass"), "HEAD")), VcsProblem::NoRepository);
    EXPECT_EQ(problemOf(show(m_dir.filePath(QStringLiteral("gone/a.ass")), "HEAD")), VcsProblem::NoRepository);
    const auto listed = refs(outside);
    ASSERT_FALSE(listed);
    EXPECT_EQ(listed.error().problem, VcsProblem::NoRepository);
}

TEST_F(GitReaderTest, ARefThatLooksLikeAnOptionIsNeverPassedOn)
{
    const QString written = m_dir.filePath(QStringLiteral("written"));
    for (const std::string &ref : std::vector<std::string>{"--output=" + written.toStdString(), "-p", "HEAD:season1/a.ass", "a b"})
        EXPECT_EQ(problemOf(show(file(), ref)), VcsProblem::NoRef) << ref;
    EXPECT_FALSE(QFile::exists(written));
}

TEST_F(GitReaderTest, AFileOverTheCapAndASlowGitAreCutOff)
{
    reader.setLimits({10'000, 8});
    EXPECT_EQ(problemOf(show(file(), "HEAD")), VcsProblem::TooLarge);
    reader.setLimits({0, 16 << 20});
    EXPECT_EQ(problemOf(show(file(), "HEAD")), VcsProblem::TimedOut);
}

TEST_F(GitReaderTest, WithoutGitItSaysToInstallIt)
{
    reader.setGit(QString());
    const auto shown = show(file(), "HEAD");
    EXPECT_EQ(problemOf(shown), VcsProblem::NoGit);
    EXPECT_NE(shown.error().message.find("Git for Windows"), std::string::npos);
    reader.setGit(m_dir.filePath(QStringLiteral("no-such-git")));
    EXPECT_EQ(problemOf(show(file(), "HEAD")), VcsProblem::NoGit);
}

TEST_F(GitReaderTest, ARepositoryGitDistrustsAsksForSafeDirectory)
{
#ifdef Q_OS_WIN
    GTEST_SKIP() << "the stand-in git is a shell script";
#endif
    const QString fake = m_dir.filePath(QStringLiteral("git"));
    write(fake, "#!/bin/sh\necho \"fatal: detected dubious ownership in repository at '$PWD'\" >&2\nexit 128\n");
    QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    reader.setGit(fake);
    const auto shown = show(file(), "HEAD");
    EXPECT_EQ(problemOf(shown), VcsProblem::UnsafeRepository);
    EXPECT_NE(shown.error().message.find("safe.directory"), std::string::npos);
}

TEST_F(GitReaderTest, AHostileRepositoryConfigRunsNothing)
{
    giveHeadAPgpSignature();
    QFile config(repo() + QStringLiteral("/.git/config"));
    ASSERT_TRUE(config.open(QIODevice::Append));
    config.write(hostileConfig().toUtf8());
    config.close();
    armTheTraps();
    readEverything();
}

TEST_F(GitReaderTest, AHostileGlobalConfigRunsNothing)
{
    giveHeadAPgpSignature();
    const QByteArray hostile = hostileConfig().toUtf8();
    write(home() + QStringLiteral("/.gitconfig"), hostile);
    QDir().mkpath(home() + QStringLiteral("/.config/git"));
    write(home() + QStringLiteral("/.config/git/config"), hostile);
    armTheTraps();
    readEverything();
}

}
