#include "hikari/backends/git_reader.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTimer>

#include <memory>

namespace hikari::backends {

namespace {

using application::VcsFailure;
using application::VcsProblem;

constexpr int kMostRefs = 500;
constexpr qint64 kListBytes = 1 << 20;
constexpr qint64 kErrorBytes = 64 << 10;

std::string utf8(const QString &text)
{
    return text.toStdString();
}

QString qs(const std::string &text)
{
    return QString::fromStdString(text);
}

std::unexpected<VcsFailure> problem(VcsProblem kind, QString message)
{
    return std::unexpected(VcsFailure{kind, utf8(message)});
}

QProcessEnvironment cleanEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &key : env.keys())
        if (key.startsWith(QStringLiteral("GIT_"), Qt::CaseInsensitive))
            env.remove(key);
    env.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_ASKPASS"), QString());
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_NO_LAZY_FETCH"), QStringLiteral("1"));
    env.insert(QStringLiteral("GIT_NO_REPLACE_OBJECTS"), QStringLiteral("1"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANGUAGE"), QString());
    return env;
}

QStringList guarded(const QStringList &args)
{
    const QString null = QProcess::nullDevice();
    QStringList all{QStringLiteral("--no-pager")};
    for (const QString &setting :
         {QStringLiteral("core.fsmonitor=false"), QStringLiteral("core.hooksPath=") + null, QStringLiteral("core.pager=cat"),
          QStringLiteral("core.askPass="), QStringLiteral("credential.helper="), QStringLiteral("log.showSignature=false"),
          QStringLiteral("gpg.program=") + null, QStringLiteral("protocol.allow=never"),
          QStringLiteral("safe.bareRepository=explicit")})
        all << QStringLiteral("-c") << setting;
    return all + args;
}

std::optional<VcsFailure> failed(const GitReader::Output &o, const QString &folder)
{
    if (!o.started)
        return VcsFailure{VcsProblem::NoGit, "Git isn't installed or isn't on PATH. Install git (on Windows, Git for Windows)."};
    if (o.timedOut)
        return VcsFailure{VcsProblem::TimedOut, "Git didn't answer within the time limit."};
    if (o.tooLarge)
        return VcsFailure{VcsProblem::TooLarge, "Git's answer is too large."};
    if (o.exitCode == 0)
        return std::nullopt;
    const QString why = QString::fromUtf8(o.err).trimmed();
    if (why.contains(QStringLiteral("dubious ownership")))
        return VcsFailure{VcsProblem::UnsafeRepository,
                          utf8(QStringLiteral("Git refuses the repository holding %1 because another user owns it. Add it "
                                              "to safe.directory in your git config to read it.")
                                   .arg(QDir::toNativeSeparators(folder)))};
    if (why.contains(QStringLiteral("not a git repository")))
        return VcsFailure{VcsProblem::NoRepository,
                          utf8(QStringLiteral("%1 isn't in a git repository.").arg(QDir::toNativeSeparators(folder)))};
    return VcsFailure{VcsProblem::Failed, utf8(QStringLiteral("Git failed: %1").arg(why.left(500)))};
}

QList<QByteArray> lines(const QByteArray &out)
{
    QList<QByteArray> all = out.split('\n');
    if (!all.isEmpty() && all.last().isEmpty())
        all.removeLast();
    return all;
}

std::optional<std::string> textOf(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    QByteArrayView body = bytes;
    if (bytes.startsWith("\xFF\xFE"))
        decoder = QStringDecoder(QStringDecoder::Utf16LE);
    else if (bytes.startsWith("\xFE\xFF"))
        decoder = QStringDecoder(QStringDecoder::Utf16BE);
    else if (bytes.contains('\0'))
        return std::nullopt;
    const QString text = decoder.decode(body);
    if (decoder.hasError())
        return std::nullopt;
    return utf8(text.startsWith(QChar(0xFEFF)) ? text.mid(1) : text);
}

struct Place {
    QString root, prefix;
};

std::expected<Place, VcsFailure> placeOf(const GitReader::Output &o, const QString &folder)
{
    if (auto f = failed(o, folder))
        return std::unexpected(*f);
    const QList<QByteArray> found = lines(o.out);
    if (found.isEmpty())
        return problem(VcsProblem::NoRepository, QStringLiteral("%1 isn't in a git work tree.").arg(QDir::toNativeSeparators(folder)));
    return Place{QString::fromUtf8(found.at(0)), found.size() > 1 ? QString::fromUtf8(found.at(1)) : QString()};
}

std::vector<application::VcsRef> refsOf(const QByteArray &out, bool peel)
{
    std::vector<application::VcsRef> refs;
    for (const QByteArray &line : lines(out)) {
        const QList<QByteArray> f = line.split('\0');
        if (f.size() < 4)
            continue;
        const QByteArray &peeled = f.at(2);
        refs.push_back({utf8(QString::fromUtf8(f.at(0))), (peel && !peeled.isEmpty() ? peeled : f.at(1)).toStdString(),
                        f.at(3) == "*"});
    }
    return refs;
}

std::vector<application::VcsCommit> commitsOf(const QByteArray &out)
{
    std::vector<application::VcsCommit> commits;
    for (const QByteArray &line : lines(out)) {
        const QList<QByteArray> f = line.split('\0');
        if (f.size() < 4)
            continue;
        commits.push_back({f.at(0).toStdString(), utf8(QString::fromUtf8(f.at(3))), utf8(QString::fromUtf8(f.at(1))),
                           f.at(2).toLongLong()});
    }
    return commits;
}

QStringList listRefs(const char *prefix, const char *format)
{
    return {QStringLiteral("for-each-ref"), QStringLiteral("--sort=-creatordate"),
            QStringLiteral("--count=%1").arg(kMostRefs), QString::fromLatin1(format), QString::fromLatin1(prefix)};
}

}

GitReader::GitReader(QObject *parent) : QObject(parent) {}

QString GitReader::program() const
{
    return m_git ? *m_git : QStandardPaths::findExecutable(QStringLiteral("git"));
}

void GitReader::git(const QString &folder, const QStringList &args, qint64 cap, const QByteArray &input, Ran done)
{
    const QString git = program();
    if (git.isEmpty() || !QDir(folder).exists()) {
        Output none;
        none.started = !git.isEmpty();
        none.exitCode = 128;
        none.err = "fatal: not a git repository";
        QTimer::singleShot(0, this, [done, none] { done(none); });
        return;
    }
    auto *process = new QProcess(this);
    auto output = std::make_shared<Output>();
    process->setProgram(git);
    process->setArguments(guarded(args));
    process->setWorkingDirectory(folder);
    process->setProcessEnvironment(cleanEnvironment());
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, [process, output] {
        output->timedOut = true;
        process->kill();
    });
    connect(process, &QProcess::readyReadStandardOutput, process, [process, output, cap] {
        output->out += process->readAllStandardOutput();
        if (output->out.size() > cap && !output->tooLarge) {
            output->tooLarge = true;
            process->kill();
        }
    });
    connect(process, &QProcess::readyReadStandardError, process, [process, output] {
        const QByteArray more = process->readAllStandardError();
        output->err += more.left(qMax<qint64>(0, kErrorBytes - output->err.size()));
    });
    connect(process, &QProcess::finished, this, [process, output, done](int code, QProcess::ExitStatus status) {
        output->out += process->readAllStandardOutput();
        output->started = true;
        output->exitCode = status == QProcess::NormalExit ? code : -1;
        process->deleteLater();
        done(*output);
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        done(Output{});
    });
    timer->start(m_limits.timeoutMs);
    process->start(QIODevice::ReadWrite);
    process->write(input);
    process->closeWriteChannel();
}

void GitReader::refs(const std::string &at, int commits,
                     std::function<void(std::expected<application::VcsRefs, application::VcsFailure>)> done)
{
    const QString folder = qs(at);
    auto refs = std::make_shared<application::VcsRefs>();
    QPointer<GitReader> self(this);
    const auto fail = [done](const VcsFailure &f) { done(std::unexpected(f)); };
    git(folder, {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel"), QStringLiteral("--show-prefix")}, kListBytes, {},
        [=](Output o) {
            auto place = placeOf(o, folder);
            if (!place)
                return fail(place.error());
            refs->root = utf8(QDir::toNativeSeparators(place->root));
            refs->prefix = utf8(place->prefix);
            const QString root = place->root;
            self->git(root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"), QStringLiteral("HEAD^{commit}")},
                      kListBytes, {}, [=](Output head) {
                if (!head.started || head.timedOut)
                    return fail(*failed(head, folder));
                refs->headCommit = head.exitCode == 0 ? lines(head.out).value(0).toStdString() : std::string();
                self->git(root, listRefs("refs/heads", "--format=%(refname:short)%00%(objectname)%00%00%(HEAD)"), kListBytes, {},
                          [=](Output branches) {
                    if (auto f = failed(branches, folder))
                        return fail(*f);
                    refs->branches = refsOf(branches.out, false);
                    for (const application::VcsRef &b : refs->branches)
                        if (b.current)
                            refs->head = b.name;
                    self->git(root, listRefs("refs/tags", "--format=%(refname:short)%00%(objectname)%00%(*objectname)%00"),
                              kListBytes, {}, [=](Output tags) {
                        if (auto f = failed(tags, folder))
                            return fail(*f);
                        refs->tags = refsOf(tags.out, true);
                        if (refs->headCommit.empty())
                            return done(std::move(*refs));
                        self->git(root,
                                  {QStringLiteral("log"), QStringLiteral("--no-show-signature"), QStringLiteral("--no-notes"),
                                   QStringLiteral("--no-decorate"), QStringLiteral("--no-color"), QStringLiteral("--encoding=UTF-8"),
                                   QStringLiteral("-n"), QString::number(commits), QStringLiteral("--format=%H%x00%an%x00%ct%x00%s"),
                                   qs(refs->headCommit), QStringLiteral("--")},
                                  kListBytes, {}, [=](Output log) {
                            if (auto f = failed(log, folder))
                                return fail(*f);
                            refs->commits = commitsOf(log.out);
                            done(std::move(*refs));
                        });
                    });
                });
            });
        });
}

void GitReader::show(const std::string &file, const std::string &ref,
                     std::function<void(std::expected<application::VcsFile, application::VcsFailure>)> done)
{
    const QFileInfo info(qs(file));
    const QString folder = info.absolutePath();
    const QString name = info.fileName();
    const QString wanted = qs(ref);
    const qint64 most = m_limits.fileBytes;
    QPointer<GitReader> self(this);
    const auto fail = [done](const VcsFailure &f) { done(std::unexpected(f)); };
    if (wanted.isEmpty() || wanted.startsWith(u'-') || wanted.contains(u':') || wanted.contains(u' ') || wanted.contains(u'\n'))
        return QTimer::singleShot(0, this, [fail, wanted] {
            fail(VcsFailure{VcsProblem::NoRef, utf8(QStringLiteral("\"%1\" isn't a ref.").arg(wanted))});
        });
    git(folder, {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel"), QStringLiteral("--show-prefix")}, kListBytes, {},
        [=](Output o) {
            auto place = placeOf(o, folder);
            if (!place)
                return fail(place.error());
            const QString root = place->root;
            const QString path = place->prefix + name;
            if (path.contains(u'\n'))
                return fail(VcsFailure{VcsProblem::NoFile, "Git can't name a file whose name has a line break."});
            self->git(root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"),
                             QStringLiteral("--end-of-options"), wanted + QStringLiteral("^{commit}")},
                      kListBytes, {}, [=](Output resolved) {
                if (!resolved.started || resolved.timedOut)
                    return fail(*failed(resolved, folder));
                if (resolved.exitCode != 0)
                    return fail(VcsFailure{VcsProblem::NoRef, utf8(QStringLiteral("No commit is named \"%1\".").arg(wanted))});
                const QString commit = QString::fromLatin1(lines(resolved.out).value(0));
                self->git(root, {QStringLiteral("cat-file"), QStringLiteral("--batch-check=%(objectname) %(objecttype) %(objectsize)")},
                          kListBytes, (commit + u':' + path + u'\n').toUtf8(), [=](Output checked) {
                    if (auto f = failed(checked, folder))
                        return fail(*f);
                    const QList<QByteArray> f = lines(checked.out).value(0).split(' ');
                    if (f.size() != 3 || f.at(1) != "blob")
                        return fail(VcsFailure{VcsProblem::NoFile,
                                               utf8(QStringLiteral("%1 isn't in %2.").arg(path, wanted))});
                    if (f.at(2).toLongLong() > most)
                        return fail(VcsFailure{VcsProblem::TooLarge,
                                               utf8(QStringLiteral("%1 in %2 is larger than %3 MiB.").arg(path, wanted).arg(most >> 20))});
                    self->git(root, {QStringLiteral("cat-file"), QStringLiteral("blob"), QString::fromLatin1(f.at(0))}, most, {},
                              [=](Output blob) {
                        if (auto e = failed(blob, folder))
                            return fail(*e);
                        const std::optional<std::string> text = textOf(blob.out);
                        if (!text)
                            return fail(VcsFailure{VcsProblem::NotText,
                                                   utf8(QStringLiteral("%1 in %2 isn't UTF-8 or UTF-16 text.").arg(path, wanted))});
                        done(application::VcsFile{commit.toStdString(), utf8(path), *text});
                    });
                });
            });
        });
}

}
