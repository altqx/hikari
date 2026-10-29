#include "hikari/app/font_collector_controller.h"

#include "hikari/backends/desktop_handoff.h"
#include "hikari/backends/font_collector_output.h"
#include "hikari/backends/libass_font_service.h"
#include "hikari/core/ass_save.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cstring>
#include <utility>

namespace hikari::app {

using application::CollectorAction;
using application::CollectorResult;
using application::CollectorReview;
using application::collector::Block;
using application::collector::Note;

namespace {

constexpr int kNormal = 0, kWarning = 1, kSuccess = 2;

QString qs(const std::u16string &s)
{
    return QString::fromStdU16String(s);
}

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

QString normalizePath(QString path)
{
#ifndef _WIN32
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
#endif
    return path;
}

QChar separator()
{
#ifdef _WIN32
    return QLatin1Char('\\');
#else
    return QLatin1Char('/');
#endif
}

QString pathDir(const QString &path, bool withSeparator = false)
{
    const QString p = QDir::toNativeSeparators(normalizePath(path));
    const qsizetype slash = std::max(p.lastIndexOf(QLatin1Char('/')), p.lastIndexOf(separator()));
    QString dir = slash < 0 ? QString() : p.left(slash);
    if (withSeparator && !dir.isEmpty())
        dir += separator();
    return dir;
}

QString pathName(const QString &path)
{
    const QString p = normalizePath(path);
    const qsizetype slash = std::max(p.lastIndexOf(QLatin1Char('/')), p.lastIndexOf(separator()));
    const QString name = p.mid(slash + 1);
    return name.isEmpty() ? p : name;
}

QString pathJoin(const QString &dir, const QString &name)
{
    QString d = QDir::toNativeSeparators(normalizePath(dir));
    if (!d.isEmpty() && !d.endsWith(separator()) && !d.endsWith(QLatin1Char('/')))
        d += separator();
    return d + name;
}

QString fonts(int n)
{
    return n == 1 ? QCoreApplication::translate("HikariSub.Fonts", "%1 font").arg(n)
                  : QCoreApplication::translate("HikariSub.Fonts", "%1 fonts").arg(n);
}

QString srtTime(qint64 ms)
{
    return QString::asprintf("%02d:%02d:%02d,%03d", int(ms / 3600000), int(ms / 60000 % 60), int(ms / 1000 % 60),
                             int(ms % 1000));
}

QString codes(const std::vector<std::uint32_t> &list)
{
    QStringList out;
    for (const auto c : list)
        out << QStringLiteral("U+") + QString::number(c, 16).toUpper().rightJustified(4, QLatin1Char('0'));
    return out.join(QStringLiteral(", "));
}

QString noteText(const Note &n)
{
    using K = Note::Kind;
    const QString fn = qs(n.family), a = qs(n.a), b = qs(n.b);
    switch (n.kind) {
    case K::FoundFile:
        return QCoreApplication::translate("HikariSub.Fonts", "Found \"%1\" font file.").arg(a);
    case K::RendererFile:
        return QCoreApplication::translate("HikariSub.Fonts", "The renderer also used \"%1\" (%2); it is not collected.").arg(a, b);
    case K::Copied:
        return QCoreApplication::translate("HikariSub.Fonts", "Copied font \"%1\".").arg(a);
    case K::AddedToArchive:
        return QCoreApplication::translate("HikariSub.Fonts", "Added font \"%1\" to the archive.").arg(a);
    case K::UnusedStyle:
        return QCoreApplication::translate("HikariSub.Fonts", "Font \"%1\" belongs to a style\nthat is not used.").arg(fn) +
               (n.flag ? QCoreApplication::translate("HikariSub.Fonts", "\nWill not be copied.") : QString());
    case K::MissingNormal:
        return QCoreApplication::translate("HikariSub.Fonts", "Font \"%1\" is missing normal style.").arg(fn);
    case K::MissingBoldItalic:
        return QCoreApplication::translate("HikariSub.Fonts", "Font \"%1\" is missing bold italics.").arg(fn);
    case K::MissingBold:
        return QCoreApplication::translate("HikariSub.Fonts", "Font \"%1\" is missing bold.").arg(fn);
    case K::MissingItalic:
        return QCoreApplication::translate("HikariSub.Fonts", "Font \"%1\" is missing italics.").arg(fn);
    case K::CannotCheckCharacters:
        return QCoreApplication::translate("HikariSub.Fonts", "Cannot check the characters in font \"%1\".").arg(fn);
    case K::MissingCharacters:
        return QCoreApplication::translate("HikariSub.Fonts", "Font \"%1\" does not contain characters: \"%2\".").arg(fn, a);
    case K::FallbackCharacters:
        return QCoreApplication::translate("HikariSub.Fonts", "Characters \"%1\" were drawn by the fallback font \"%2\".").arg(a, b);
    case K::CannotGetContents:
        return QCoreApplication::translate("HikariSub.Fonts", "Cannot get the contents of font \"%1\".").arg(fn);
    case K::CannotOpenFile:
        return QCoreApplication::translate("HikariSub.Fonts", "Cannot open file \"%1\".").arg(a);
    case K::CannotFindInFolder:
        return QCoreApplication::translate("HikariSub.Fonts", "Cannot find \"%1\" font in Fonts folder.").arg(fn);
    case K::CannotCopy:
        return QCoreApplication::translate("HikariSub.Fonts", "Cannot copy font \"%1\".").arg(a);
    case K::CannotZip:
        return QCoreApplication::translate("HikariSub.Fonts", "Cannot zip font \"%1\".").arg(a);
    }
    return {};
}

std::shared_ptr<const std::vector<std::byte>> readFontFile(const std::u16string &path)
{
    QFile f(QString::fromUtf16(path.data(), qsizetype(path.size())));
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;
    const QByteArray data = f.readAll();
    auto out = std::make_shared<std::vector<std::byte>>(std::size_t(data.size()));
    std::memcpy(out->data(), data.constData(), std::size_t(data.size()));
    return out;
}

} // namespace

FontCollectorController::FontCollectorController(ui::SettingsStore &settings, Hooks hooks, QObject *parent)
    : QObject(parent), m_settings(settings), m_hooks(std::move(hooks)),
      m_service(std::make_unique<backends::LibassFontService>()),
      m_collector(std::make_unique<application::FontCollector>(*m_service, readFontFile))
{
    open();
}

FontCollectorController::~FontCollectorController()
{
    m_cancel = true;
    join();
}

void FontCollectorController::join()
{
    if (m_worker.joinable())
        m_worker.join();
}

void FontCollectorController::setFontService(std::unique_ptr<application::FontServicePort> service)
{
    m_cancel = true;
    join();
    m_service = std::move(service);
    m_collector = std::make_unique<application::FontCollector>(*m_service, readFontFile);
}

bool FontCollectorController::waitIdle(int ms)
{
    QElapsedTimer t;
    t.start();
    while (m_stage == Working) {
        if (t.elapsed() > ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

QVariantList FontCollectorController::log() const
{
    QVariantList out;
    for (const auto &s : m_log)
        out << QVariantMap{{QStringLiteral("text"), s.text}, {QStringLiteral("kind"), s.kind}};
    return out;
}

QString FontCollectorController::logText() const
{
    QString out;
    for (const auto &s : m_log)
        out += s.text;
    return out;
}

void FontCollectorController::setStage(Stage stage)
{
    m_stage = stage;
    emit stageChanged();
}

void FontCollectorController::send(const QString &text, int kind)
{
    if (text.isEmpty())
        return;
    m_log.push_back({text, kind});
}

void FontCollectorController::clearLog()
{
    m_log.clear();
    m_areas.clear();
}

void FontCollectorController::open()
{
    if (m_stage == Working)
        return;
    m_action = std::clamp(m_settings.integer("fontCollector.action"), 0, 2);
    m_directory = m_settings.text("fontCollector.directory");
    m_useSubsDirectory = m_settings.boolean("fontCollector.useSubsDirectory");
    m_fromMkv = m_settings.boolean("fontCollector.fromMkv");
    m_fromMkvEnabled = application::fontsFromMkvEnabled(currentVideo().toStdU16String());
    clearLog();
    m_review.reset();
    m_canSaveFolder = false;
    m_copyPath.clear();
    m_stage = Options;
    emit settingsChanged();
    emit logChanged();
    emit stageChanged();
}

void FontCollectorController::changeOptions(int action, bool useSubsDirectory)
{
    m_action = std::clamp(action, 0, 2);
    m_useSubsDirectory = useSubsDirectory;
    m_settings.set("fontCollector.action", m_action);
    m_settings.set("fontCollector.useSubsDirectory", m_useSubsDirectory);
    m_settings.sync();
    m_fromMkvEnabled = m_action != 0 && application::fontsFromMkvEnabled(currentVideo().toStdU16String());
    emit settingsChanged();
}

void FontCollectorController::setFromMkv(bool on)
{
    if (m_fromMkv == on)
        return;
    m_fromMkv = on;
    m_settings.set("fontCollector.fromMkv", on);
    m_settings.sync();
    emit settingsChanged();
}

QString FontCollectorController::currentVideo() const
{
    const auto tabs = m_hooks.tabs ? m_hooks.tabs() : std::vector<Tab>{};
    const int current = m_hooks.currentTab ? m_hooks.currentTab() : -1;
    return current >= 0 && current < int(tabs.size()) ? tabs[std::size_t(current)].video : QString();
}

QVariantMap FontCollectorController::chooserStart(const QString &path) const
{
    if (m_action == 1)
        return {{QStringLiteral("folder"), path}, {QStringLiteral("name"), QString()}};
    const bool zip = path.endsWith(QLatin1String("zip"));
    return {{QStringLiteral("folder"), zip ? pathDir(path) : path}, {QStringLiteral("name"), zip ? pathName(path) : QString()}};
}

void FontCollectorController::chooseDirectory(const QString &path)
{
    if (path.isEmpty())
        return;
    m_directory = path;
    m_settings.set("fontCollector.directory", path);
    m_settings.sync();
    emit settingsChanged();
}

QVariantMap FontCollectorController::start(const QString &path, bool allTabs)
{
    if (m_stage == Working)
        return {};
    clearLog();
    emit logChanged();
    m_review.reset();
    const auto tabs = m_hooks.tabs ? m_hooks.tabs() : std::vector<Tab>{};
    const int current = m_hooks.currentTab ? m_hooks.currentTab() : -1;
    m_documents.clear();
    for (int i = 0; i < int(tabs.size()); ++i) {
        if (!allTabs && i != current)
            continue;
        application::CollectorDocument d;
        d.tab = i;
        d.document = tabs[std::size_t(i)].document;
        d.ass = d.document && d.document->format() == core::SubtitleFormat::Ass;
        if (d.ass)
            d.script = core::encodeAss(*d.document);
        m_documents.push_back(std::move(d));
    }
    m_runAction = CollectorAction(m_action);
    m_pendingArchive.clear();
    m_removeArchive.clear();
    m_runMkv = m_fromMkv && m_fromMkvEnabled;
    m_runAllTabs = allTabs;
    m_runZip = false;
    m_mkvVideos.clear();
    for (int i = 0; i < int(tabs.size()); ++i)
        if (allTabs || i == current)
            m_mkvVideos.push_back(tabs[std::size_t(i)].video);
    if (m_action == 0) {
        if (m_runMkv)
            runMkvPrepare();
        else
            runPrepare();
        return {};
    }
    const auto refuse = [](const QString &message) { return QVariantMap{{QStringLiteral("message"), message}}; };
    if (m_canSaveFolder) {
        m_canSaveFolder = false;
        emit stageChanged();
    }
    m_directory = path;
    m_settings.set("fontCollector.directory", path);
    emit settingsChanged();
    const QString subsPath = current >= 0 && current < int(tabs.size()) ? tabs[std::size_t(current)].path : QString();
    if (path.isEmpty() && !m_useSubsDirectory)
        return refuse(QCoreApplication::translate("HikariSub.Fonts", "Select the folder where you want to copy fonts"));
    if (!m_runMkv && m_useSubsDirectory && subsPath.isEmpty())
        return refuse(QCoreApplication::translate("HikariSub.Fonts", "No subtitles loaded. Load subtitles or deselect this option."));
    const QString pathValue = normalizePath(path);
    const bool zip = m_action == 2;
    if (zip && QFileInfo(pathValue).isDir() && !m_useSubsDirectory)
        return refuse(QCoreApplication::translate("HikariSub.Fonts", "Choose a name for the archive"));
    QString copypath;
    if (m_useSubsDirectory) {
        const QString sourcePath = m_runMkv ? currentVideo() : subsPath;
        const QString rest = pathName(sourcePath);
        const QString fontDir =
            pathJoin(pathDir(sourcePath), QCoreApplication::translate("HikariSub.Fonts",
                                                                      "Fonts", "the folder the font collector writes beside the subtitles"));
        const qsizetype dot = rest.lastIndexOf(QLatin1Char('.'));
        copypath = zip ? pathJoin(fontDir, (dot < 0 ? rest : rest.left(dot)) + QStringLiteral(".zip")) : fontDir + separator();
    } else {
        copypath = pathValue;
#ifdef _WIN32
        const bool valid = QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(copypath).hasMatch();
#else
        const bool valid = !copypath.isEmpty();
#endif
        if (!valid)
            return refuse(QCoreApplication::translate("HikariSub.Fonts", "The save path is not valid."));
        if (!zip && !copypath.endsWith(separator()))
            copypath += separator();
        else if (zip && !copypath.endsWith(QLatin1String(".zip")))
            copypath += QStringLiteral(".zip");
    }
    if (!zip) {
        if (copypath.right(4).toLower() == QLatin1String(".zip"))
            copypath = pathDir(copypath, true);
    } else if (QFileInfo::exists(copypath)) {
        m_copyPath = copypath;
        m_runZip = zip;
        m_pendingArchive = copypath;
        return {{QStringLiteral("question"), QCoreApplication::translate("HikariSub.Fonts", "The zip file already exists, delete it?")},
                {QStringLiteral("title"), QCoreApplication::translate("HikariSub.Fonts", "Confirmation")},
                {QStringLiteral("archive"), copypath}};
    }
    m_copyPath = copypath;
    m_runZip = zip;
    if (m_runMkv)
        runMkvPrepare();
    else
        runPrepare();
    return {};
}

bool FontCollectorController::confirmReplace(bool remove)
{
    if (m_pendingArchive.isEmpty())
        return false;
    const QString archive = std::exchange(m_pendingArchive, QString());
    if (remove)
        m_removeArchive = archive;
    if (m_runMkv)
        runMkvPrepare();
    else
        runPrepare();
    return true;
}

void FontCollectorController::runPrepare()
{
    join();
    m_cancel = false;
    m_elapsed = 0;
    m_clock.start();
    setStage(Working);
    auto documents = m_documents;
    const CollectorAction action = m_runAction;
    m_worker = std::thread([this, documents = std::move(documents), action] {
        auto review = m_collector->prepare(documents, action, &m_cancel);
        QMetaObject::invokeMethod(this, [this, review = std::move(review)]() mutable { prepared(std::move(review)); },
                                  Qt::QueuedConnection);
    });
}

void FontCollectorController::doLog(const Block &block)
{
    QString info;
    switch (block.header) {
    case Block::Header::Found:
        info = QCoreApplication::translate("HikariSub.Fonts", "Found font \"%1\"\n").arg(qs(block.family));
        break;
    case Block::Header::FoundDot:
        info = QCoreApplication::translate("HikariSub.Fonts", "Found font \"%1\".").arg(qs(block.family));
        break;
    case Block::Header::NotFound:
        info = QCoreApplication::translate("HikariSub.Fonts", "Font not found \"") + qs(block.family) + QStringLiteral("\".\n");
        break;
    }
    for (const auto &n : block.infos)
        info += noteText(n) + QLatin1Char('\n');
    QString warnings;
    for (const auto &n : block.warnings)
        warnings += noteText(n) + QLatin1Char('\n');
    const int colour = block.notFound ? kWarning : kNormal;
    send(info, colour);
    int position = int(logText().size());
    Area area;
    area.block = block;
    QString messageText;
    if (!block.styles.empty())
        messageText += QCoreApplication::translate("HikariSub.Fonts", "In styles:\n");
    area.stylesFrom = position + int(messageText.size());
    for (const auto &[style, tabs] : block.styles) {
        messageText += QStringLiteral(" - ") + qs(style);
        messageText += QCoreApplication::translate("HikariSub.Fonts", " tabs: ");
        QStringList numbers;
        for (const int t : tabs)
            numbers << QString::number(t + 1);
        messageText += numbers.join(QStringLiteral(", ")) + QLatin1Char('\n');
    }
    area.stylesTo = position + int(messageText.size());
    if (!block.lines.empty())
        messageText += QCoreApplication::translate("HikariSub.Fonts", "In lines: ");
    area.linesFrom = position + int(messageText.size());
    QStringList lines;
    for (const auto &[line, tabs] : block.lines)
        lines << QString::number(line + 1);
    if (!block.lines.empty())
        messageText += lines.join(QStringLiteral(", ")) + QLatin1Char('\n');
    area.linesTo = position + int(messageText.size());
    if (warnings.isEmpty())
        messageText += QLatin1Char('\n');
    else
        warnings += QLatin1Char('\n');
    send(messageText, colour);
    send(warnings, kWarning);
    m_areas.push_back(std::move(area));
}

void FontCollectorController::logBlocks(const std::map<std::u16string, Block> &found,
                                        const std::map<std::u16string, Block> &notFound)
{
    for (const auto &[family, block] : found)
        doLog(block);
    for (const auto &[family, block] : notFound)
        doLog(block);
}

void FontCollectorController::logRenderer(const CollectorReview &review)
{
    QString text;
    if (!review.provider.empty())
        text += QCoreApplication::translate("HikariSub.Fonts", "Checked by the subtitle renderer (%1).\n").arg(qs(review.provider));
    QStringList missing, substituted;
    for (const auto &f : review.missingFamilies)
        missing << qs(f);
    for (const auto &f : review.substitutedFamilies)
        substituted << qs(f);
    QString problems;
    for (const int t : review.unrendered)
        problems += QCoreApplication::translate("HikariSub.Fonts",
                                                "Tab %1: the renderer could not read this document; it was not checked.\n").arg(t + 1);
    if (!missing.isEmpty())
        problems += QCoreApplication::translate("HikariSub.Fonts", "Families the renderer did not find: %1.\n")
                        .arg(missing.join(QStringLiteral(", ")));
    if (!substituted.isEmpty())
        problems += QCoreApplication::translate("HikariSub.Fonts", "Families answered by a font of another name: %1.\n")
                        .arg(substituted.join(QStringLiteral(", ")));
    if (!review.missingGlyphs.empty())
        problems += QCoreApplication::translate("HikariSub.Fonts", "Characters no font has: %1.\n").arg(codes(review.missingGlyphs));
    if (!review.fallbackGlyphs.empty())
        problems += QCoreApplication::translate("HikariSub.Fonts", "Characters drawn by a fallback font of this computer: %1.\n")
                        .arg(codes(review.fallbackGlyphs));
    for (const auto &r : review.reimports)
        if (!r.identical)
            problems += QCoreApplication::translate("HikariSub.Fonts", "Tab %1: with the collected fonts alone, %2 of %3 frames differ.\n")
                            .arg(r.tab + 1)
                            .arg(r.differingFrames.size())
                            .arg(r.frames);
    QStringList clashes;
    for (const auto &n : review.nameClashes)
        clashes << qs(n);
    if (!clashes.isEmpty())
        problems += QCoreApplication::translate("HikariSub.Fonts",
                                                "Different fonts with the same file name: %1. Only one of them is kept in a folder.\n")
                        .arg(clashes.join(QStringLiteral(", ")));
    bool reproduced = !review.reimports.empty() && review.unrendered.empty();
    for (const auto &r : review.reimports)
        reproduced = reproduced && r.identical;
    send(text, kNormal);
    if (!m_displayRenderer.isEmpty() && m_displayRenderer != QLatin1String("libass"))
        send(QCoreApplication::translate("HikariSub.Fonts",
                                         "The video shows the subtitles through %1, whose output this check does not verify.\n")
                 .arg(m_displayRenderer),
             kWarning);
    send(problems, kWarning);
    if (reproduced)
        send(QCoreApplication::translate("HikariSub.Fonts", "The collected fonts alone reproduce every frame.\n"), kNormal);
}

void FontCollectorController::logSummary(const CollectorReview &review, const CollectorResult &result)
{
    const bool checkFonts = review.action == CollectorAction::Check;
    const QString verb = checkFonts ? QCoreApplication::translate("HikariSub.Fonts", "found")
                                    : QCoreApplication::translate("HikariSub.Fonts", "copied");
    const bool success = result.complete;
    if (!success) {
        QString message = QStringLiteral("\n") +
                          QCoreApplication::translate("HikariSub.Fonts", "Finished, %1 %2.\n").arg(verb, fonts(result.foundCount));
        if (result.notFoundCount)
            message += QCoreApplication::translate("HikariSub.Fonts", "Not found %1.\n").arg(fonts(result.notFoundCount));
        if (result.notCopiedCount)
            message += QCoreApplication::translate("HikariSub.Fonts", "Cannot copy %1.\n").arg(fonts(result.notCopiedCount));
        if (!result.allGlyphs)
            message += QCoreApplication::translate("HikariSub.Fonts", "Some fonts do not contain all glyphs used in the text.\n");
        if (!review.rendererComplete())
            message += QCoreApplication::translate("HikariSub.Fonts",
                                                   "The renderer cannot reproduce the subtitles from these fonts alone: the collection is incomplete.\n");
        else if (!review.nameClashes.empty())
            message += QCoreApplication::translate("HikariSub.Fonts",
                                                   "The output does not hold every collected font under its own name: the collection is incomplete.\n");
        if (result.cancelled)
            message += QCoreApplication::translate("HikariSub.Fonts", "Writing was cancelled.\n");
        if (result.labelled)
            message += QCoreApplication::translate("HikariSub.Fonts", "The output is labelled incomplete (\"%1\").\n")
                           .arg(QString::fromUtf8(backends::kIncompleteLabel));
        send(message, kWarning);
    } else {
        send(QStringLiteral("\n") + QCoreApplication::translate("HikariSub.Fonts", "Completed Successfully, %1 %2.\n")
                                        .arg(verb, fonts(result.foundCount)), kSuccess);
    }
}

void FontCollectorController::logFinished()
{
    send(QStringLiteral("\n") + QCoreApplication::translate("HikariSub.Fonts", "Finished in %1ms").arg(srtTime(m_elapsed)), kNormal);
}

void FontCollectorController::prepared(std::expected<CollectorReview, application::FontError> review)
{
    join();
    m_elapsed += m_clock.elapsed();
    clearLog();
    const bool copy = m_runAction != CollectorAction::Check;
    if (!review) {
        send(review.error() == application::FontError::Cancelled
                 ? QCoreApplication::translate("HikariSub.Fonts", "Cancelled; nothing was written.\n")
                 : QCoreApplication::translate("HikariSub.Fonts", "The subtitle renderer is not available; the fonts cannot be checked.\n"),
             kWarning);
        logFinished();
        emit logChanged();
        setStage(Done);
        return;
    }
    m_review = std::move(*review);
    if (m_review->retrieveFailed) {
        send(QCoreApplication::translate("HikariSub.Fonts",
                                         "Cannot retrieve the font file sizes and names;\n"
                                         "copying will be canceled.\n"), kWarning);
        logFinished();
        m_canSaveFolder = true;
        m_review.reset();
        emit logChanged();
        setStage(Done);
        return;
    }
    if (m_review->retrievedFonts)
        send(QCoreApplication::translate("HikariSub.Fonts", "Retrieved sizes and names of %1 fonts, elapsed time %2ms.\n\n")
                 .arg(*m_review->retrievedFonts)
                 .arg(srtTime(m_review->retrieveMs)),
             kNormal);
    logBlocks(m_review->found, m_review->notFound);
    logRenderer(*m_review);
    if (!copy) {
        logSummary(*m_review, m_collector->result(*m_review));
        logFinished();
        m_review.reset();
        emit logChanged();
        setStage(Done);
        return;
    }
    const int files = int(m_review->files.size());
    QString ready = QStringLiteral("\n") +
                    (m_runAction == CollectorAction::Zip
                         ? QCoreApplication::translate("HikariSub.Fonts", "Ready to add %1 to the archive \"%2\".\n")
                               .arg(fonts(files), m_copyPath)
                         : QCoreApplication::translate("HikariSub.Fonts", "Ready to copy %1 to \"%2\".\n").arg(fonts(files), m_copyPath));
    send(ready, kNormal);
    if (!m_review->complete())
        send(QCoreApplication::translate("HikariSub.Fonts",
                                         "This collection is incomplete. It is written only when you acknowledge that, and the output is then labelled incomplete.\n"),
             kWarning);
    emit logChanged();
    setStage(Review);
}

void FontCollectorController::apply(bool acknowledged)
{
    if (m_stage == Review && m_runMkv) {
        mkvApply();
        return;
    }
    if (m_stage != Review || !m_review)
        return;
    if (!m_review->complete() && !acknowledged)
        return;
    if (!m_removeArchive.isEmpty() && QFileInfo::exists(m_removeArchive) && !QFile::remove(m_removeArchive))
        return;
    m_removeArchive.clear();
    join();
    m_cancel = false;
    m_clock.start();
    setStage(Working);
    const CollectorReview review = *m_review;
    const QString path = m_copyPath;
    m_worker = std::thread([this, review, path, acknowledged] {
        std::unique_ptr<application::CollectorOutput> output;
        if (review.action == CollectorAction::Zip)
            output = std::make_unique<backends::ZipCollectorOutput>(path);
        else
            output = std::make_unique<backends::FolderCollectorOutput>(path);
        auto result = m_collector->apply(review, *output, acknowledged, &m_cancel);
        output.reset();
        QMetaObject::invokeMethod(this, [this, result = std::move(result)]() mutable { applied(std::move(result)); },
                                  Qt::QueuedConnection);
    });
}

void FontCollectorController::applied(CollectorResult result)
{
    join();
    m_elapsed += m_clock.elapsed();
    if (!m_review) {
        setStage(Done);
        return;
    }
    const CollectorReview review = *m_review;
    m_review.reset();
    clearLog();
    if (review.retrievedFonts)
        send(QCoreApplication::translate("HikariSub.Fonts", "Retrieved sizes and names of %1 fonts, elapsed time %2ms.\n\n")
                 .arg(*review.retrievedFonts)
                 .arg(srtTime(review.retrieveMs)),
             kNormal);
    if (result.pathNotAvailable) {
        if (result.cannotCreateFolder)
            send(QCoreApplication::translate("HikariSub.Fonts", "Cannot create folder."), kWarning);
        send(QCoreApplication::translate("HikariSub.Fonts", "Path is not available"), kWarning);
    } else {
        logBlocks(result.found, result.notFound);
        logRenderer(review);
        logSummary(review, result);
    }
    logFinished();
    m_canSaveFolder = true;
    emit logChanged();
    setStage(Done);
}

namespace {

class NoFolderOutput final : public application::CollectorOutput {
public:
    bool open() override { return false; }
    bool folderFailed() const override { return true; }
    bool put(const std::u16string &, const std::vector<std::byte> &) override { return false; }
    bool label(const std::string &) override { return false; }
    bool unlabel() override { return true; }
    bool commit() override { return true; }
    void discard() override {}
};

} // namespace

void FontCollectorController::runMkvPrepare()
{
    join();
    m_cancel = false;
    m_elapsed = 0;
    m_clock.start();
    m_mkvTabs.clear();
    setStage(Working);
    mkvNext(++m_mkvRun);
}

void FontCollectorController::mkvNext(std::uint64_t run)
{
    if (run != m_mkvRun)
        return;
    if (m_cancel)
        return mkvPrepared(true);
    if (m_mkvTabs.size() == m_mkvVideos.size())
        return mkvPrepared(false);
    const QString video = m_mkvVideos[m_mkvTabs.size()];
    application::MatroskaFontTab tab;
    tab.video = video.toStdU16String();
    if (!application::isMkvExtension(tab.video) || !m_matroska) {
        tab.status = application::MatroskaFontTab::Status::NotMkv;
        m_mkvTabs.push_back(std::move(tab));
        return mkvNext(run);
    }
    m_matroska->attachments(video.toStdString(), [this, run, tab = std::move(tab)](
                                                     std::expected<std::vector<application::MatroskaAttachment>,
                                                                   application::MatroskaError> read) mutable {
        if (run != m_mkvRun)
            return;
        using Status = application::MatroskaFontTab::Status;
        if (!read && read.error().failure == application::MatroskaFailure::Cancelled) {
            m_cancel = true;
        } else if (!read) {
            tab.status = Status::CannotOpen;
        } else {
            tab.fonts = application::matroskaFonts(*read);
            tab.status = tab.fonts.empty() ? Status::NoFonts : Status::Fonts;
        }
        if (!m_cancel)
            m_mkvTabs.push_back(std::move(tab));
        QMetaObject::invokeMethod(this, [this, run] { mkvNext(run); }, Qt::QueuedConnection);
    });
}

void FontCollectorController::logMkvTab(std::size_t index)
{
    using Status = application::MatroskaFontTab::Status;
    const auto &tab = m_mkvTabs[index];
    if (m_runAllTabs)
        send(QCoreApplication::translate("HikariSub.Fonts", "Video: %1\n\n").arg(qs(tab.video)), kNormal);
    switch (tab.status) {
    case Status::NotMkv:
        send(QCoreApplication::translate("HikariSub.Fonts", "This video is not an MKV file."), kWarning);
        break;
    case Status::CannotOpen:
        send(QCoreApplication::translate("HikariSub.Fonts", "Cannot open MKV file"), kWarning);
        break;
    case Status::NoFonts:
        send(QCoreApplication::translate("HikariSub.Fonts", "Loaded MKV file does not have any fonts."), kWarning);
        break;
    case Status::Fonts:
        break;
    }
}

void FontCollectorController::mkvPrepared(bool cancelled)
{
    m_elapsed += m_clock.elapsed();
    clearLog();
    if (cancelled) {
        m_mkvTabs.clear();
        send(QCoreApplication::translate("HikariSub.Fonts", "Cancelled; nothing was written.\n"), kWarning);
        logFinished();
        emit logChanged();
        setStage(Done);
        return;
    }
    int count = 0;
    for (std::size_t i = 0; i < m_mkvTabs.size(); ++i) {
        logMkvTab(i);
        for (const auto &font : m_mkvTabs[i].fonts) {
            send(QCoreApplication::translate("HikariSub.Fonts", "Font named \"%1\".\n").arg(qs(font.name)), kNormal);
            ++count;
        }
    }
    if (m_runAction == CollectorAction::Check) {
        m_mkvTabs.clear();
        logFinished();
        emit logChanged();
        setStage(Done);
        return;
    }
    if (count == 0) {
        m_mkvTabs.clear();
        logFinished();
        m_canSaveFolder = true;
        emit logChanged();
        setStage(Done);
        return;
    }
    const QString target = m_copyPath.isEmpty() ? QCoreApplication::translate("HikariSub.Fonts", "(no folder)") : m_copyPath;
    send(QStringLiteral("\n") + (m_runZip ? QCoreApplication::translate("HikariSub.Fonts", "Ready to add %1 to the archive \"%2\".\n")
                                                .arg(fonts(count), target)
                                           : QCoreApplication::translate("HikariSub.Fonts", "Ready to copy %1 to \"%2\".\n")
                                                 .arg(fonts(count), target)),
         kNormal);
    emit logChanged();
    setStage(Review);
}

void FontCollectorController::mkvApply()
{
    if (!m_removeArchive.isEmpty() && QFileInfo::exists(m_removeArchive) && !QFile::remove(m_removeArchive))
        return;
    m_removeArchive.clear();
    join();
    m_cancel = false;
    m_clock.start();
    setStage(Working);
    const auto tabs = m_mkvTabs;
    const QString path = m_copyPath;
    const bool zip = m_runZip;
    m_worker = std::thread([this, tabs, path, zip] {
        std::unique_ptr<application::CollectorOutput> output;
        if (zip)
            output = std::make_unique<backends::ZipCollectorOutput>(path);
        else if (path.isEmpty())
            output = std::make_unique<NoFolderOutput>();
        else
            output = std::make_unique<backends::FolderCollectorOutput>(pathDir(path, true));
        auto saved = application::saveMatroskaFonts(tabs, *output, &m_cancel);
        output.reset();
        const bool cancelled = m_cancel;
        QMetaObject::invokeMethod(this, [this, saved = std::move(saved), cancelled]() mutable {
            mkvApplied(std::move(saved), cancelled);
        }, Qt::QueuedConnection);
    });
}

void FontCollectorController::mkvApplied(std::vector<application::MatroskaFontsSaved> saved, bool cancelled)
{
    join();
    m_elapsed += m_clock.elapsed();
    clearLog();
    for (std::size_t i = 0; i < m_mkvTabs.size() && i < saved.size(); ++i) {
        logMkvTab(i);
        const auto &tab = m_mkvTabs[i];
        const auto &result = saved[i];
        if (tab.status != application::MatroskaFontTab::Status::Fonts)
            continue;
        if (result.outputFailed) {
            if (result.cannotCreateFolder)
                send(QCoreApplication::translate("HikariSub.Fonts", "Cannot create folder."), kWarning);
            continue;
        }
        int copied = 0;
        for (std::size_t k = 0; k < result.saved.size(); ++k) {
            const QString name = qs(tab.fonts[k].name);
            if (result.saved[k]) {
                send(QCoreApplication::translate("HikariSub.Fonts", "Saved a font named \"") + name + QStringLiteral("\".\n \n"), kNormal);
                ++copied;
            } else {
                send(QCoreApplication::translate("HikariSub.Fonts", "Cannot save font named \"") + name + QStringLiteral("\".\n \n"),
                     kWarning);
            }
        }
        if (result.cancelled) {
            send(QCoreApplication::translate("HikariSub.Fonts", "Writing was cancelled.\n"), kWarning);
            break;
        }
        const int total = int(tab.fonts.size());
        if (copied < total)
            send(QCoreApplication::translate("HikariSub.Fonts", "Completed, copied %1 fonts.\nFailed to copy %2 fonts.")
                     .arg(copied).arg(total - copied), kWarning);
        else
            send(QCoreApplication::translate("HikariSub.Fonts", "Completed successfully and copied %1 fonts.").arg(copied), kSuccess);
    }
    (void)cancelled;
    m_mkvTabs.clear();
    logFinished();
    m_canSaveFolder = true;
    emit logChanged();
    setStage(Done);
}

void FontCollectorController::cancel()
{
    m_cancel = true;
    if (m_runMkv && m_stage == Working && m_matroska)
        m_matroska->cancel();
}

void FontCollectorController::close()
{
    cancel();
    if (m_stage == Working && !waitIdle(m_closeWaitMs))
        return;
    m_review.reset();
    m_mkvTabs.clear();
    m_removeArchive.clear();
    if (m_stage == Review)
        setStage(Options);
}

void FontCollectorController::saveFolder()
{
    if (!m_canSaveFolder)
        return;
    if (m_hooks.reveal) {
        m_hooks.reveal(m_copyPath);
        return;
    }
#ifdef _WIN32
    backends::desktop::startDetached(QStringLiteral("explorer.exe"),
                                     {QStringLiteral("/select,") + QDir::toNativeSeparators(m_copyPath)});
#else
    QString target = pathDir(m_copyPath);
    if (target.isEmpty())
        target = m_copyPath;
    backends::desktop::openUrl(QUrl::fromLocalFile(target));
#endif
}

void FontCollectorController::logDoubleClicked(int position)
{
    const Area *hit = nullptr;
    bool isStyle = false;
    for (const auto &area : m_areas) {
        if (position >= area.stylesFrom && position <= area.stylesTo) {
            hit = &area;
            isStyle = true;
            break;
        }
        if (position >= area.linesFrom && position <= area.linesTo) {
            hit = &area;
            break;
        }
    }
    if (!hit)
        return;
    const QString text = logText();
    const QString delim = QStringLiteral(" \n,");
    qsizetype start = 0, end = 0;
    for (qsizetype i = position; i > 0; --i)
        if (i < text.size() && delim.contains(text[i])) {
            start = i + 1;
            break;
        }
    for (qsizetype i = position; i < text.size(); ++i)
        if (delim.contains(text[i])) {
            end = i - 1;
            if (end < start)
                end = start - 1;
            break;
        }
    const qsizetype count = end - start + 1;
    const QString word = count < 0 ? text.mid(start) : text.mid(start, count);
    static const QRegularExpression number(QStringLiteral("^[+-]?[0-9]+$"));
    const auto styleOf = [&](const QString &line) -> std::optional<QString> {
        if (!line.startsWith(QLatin1String(" - ")))
            return std::nullopt;
        QString name = line.mid(3);
        const qsizetype result = name.indexOf(QCoreApplication::translate("HikariSub.Fonts", " tabs: "));
        if (result >= 0)
            name = name.left(result);
        return name;
    };
    const Block &block = hit->block;
    if (number.match(word).hasMatch()) {
        if (isStyle) {
            const QString line = text.left(end).section(QLatin1Char('\n'), -1);
            const QString tabs = QCoreApplication::translate("HikariSub.Fonts", " tabs: ");
            if (!line.startsWith(QLatin1String(" - ")) || line.mid(3).indexOf(tabs) < 0)
                return;
            const QString style = line.mid(3).left(line.mid(3).indexOf(tabs));
            const auto it = block.styles.find(style.toStdU16String());
            if (it != block.styles.end() && !it->second.empty() && m_hooks.goToStyle) {
                m_hooks.goToStyle(word.toInt() - 1, style);
                emit styleRequested(style);
            }
        } else {
            const int line = word.toInt() - 1;
            const auto it = block.lines.find(line);
            if (it != block.lines.end() && !it->second.empty() && m_hooks.goToLine)
                m_hooks.goToLine(it->second[0], line);
        }
    } else if (!word.isEmpty()) {
        const QString line = text.left(end).section(QLatin1Char('\n'), -1) + text.mid(end).section(QLatin1Char('\n'), 0, 0);
        if (const auto style = styleOf(line)) {
            const auto it = block.styles.find(style->toStdU16String());
            if (it != block.styles.end() && !it->second.empty() && m_hooks.goToStyle) {
                m_hooks.goToStyle(it->second[0], *style);
                emit styleRequested(*style);
            }
        }
    }
}

} // namespace hikari::app
