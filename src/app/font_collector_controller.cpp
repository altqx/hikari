#include "hikari/app/font_collector_controller.h"

#include "hikari/backends/font_collector_output.h"
#include "hikari/backends/libass_font_service.h"
#include "hikari/core/ass_save.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
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

// HikariNormalizePath (config.h:38-45): outside Windows, '\' is '/'.
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

// HikariPathDir (wxFileName::GetPath with the volume): the directory part
// in native form, without a trailing separator; with `withSeparator`,
// followed by one.
QString pathDir(const QString &path, bool withSeparator = false)
{
    const QString p = QDir::toNativeSeparators(normalizePath(path));
    const qsizetype slash = std::max(p.lastIndexOf(QLatin1Char('/')), p.lastIndexOf(separator()));
    QString dir = slash < 0 ? QString() : p.left(slash);
    if (withSeparator && !dir.isEmpty())
        dir += separator();
    return dir;
}

// HikariPathName: the file name, or the path when it has none.
QString pathName(const QString &path)
{
    const QString p = normalizePath(path);
    const qsizetype slash = std::max(p.lastIndexOf(QLatin1Char('/')), p.lastIndexOf(separator()));
    const QString name = p.mid(slash + 1);
    return name.isEmpty() ? p : name;
}

// HikariPathJoin (wxFileName::GetFullPath: native form).
QString pathJoin(const QString &dir, const QString &name)
{
    QString d = QDir::toNativeSeparators(normalizePath(dir));
    if (!d.isEmpty() && !d.endsWith(separator()) && !d.endsWith(QLatin1Char('/')))
        d += separator();
    return d + name;
}

// wxGETTEXT_IN_CONTEXT_PLURAL("found or copied", "%d font", "%d fonts", n).
QString fonts(int n)
{
    return n == 1 ? FontCollectorController::tr("%1 font").arg(n) : FontCollectorController::tr("%1 fonts").arg(n);
}

// SubsTime::GetFormatted(SRT): "%02i:%02i:%02i,%03i".
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
    case K::FoundFile: // AppendInfo (FontCollector.cpp:1189)
        return FontCollectorController::tr("Found \"%1\" font file.").arg(a);
    case K::RendererFile:
        return FontCollectorController::tr("The renderer also used \"%1\" (%2); it is not collected.").arg(a, b);
    case K::Copied: // SaveFont (902)
        return FontCollectorController::tr("Copied font \"%1\".").arg(a);
    case K::AddedToArchive: // SaveFont (887)
        return FontCollectorController::tr("Added font \"%1\" to the archive.").arg(a);
    case K::UnusedStyle: // CheckPathAndGlyphs (1096), CheckOrCopyFonts (835)
        return FontCollectorController::tr("Font \"%1\" belongs to a style\nthat is not used.").arg(fn) +
               (n.flag ? FontCollectorController::tr("\nWill not be copied.") : QString());
    case K::MissingNormal:
        return FontCollectorController::tr("Font \"%1\" is missing normal style.").arg(fn);
    case K::MissingBoldItalic:
        return FontCollectorController::tr("Font \"%1\" is missing bold italics.").arg(fn);
    case K::MissingBold:
        return FontCollectorController::tr("Font \"%1\" is missing bold.").arg(fn);
    case K::MissingItalic:
        return FontCollectorController::tr("Font \"%1\" is missing italics.").arg(fn);
    case K::CannotCheckCharacters:
        return FontCollectorController::tr("Cannot check the characters in font \"%1\".").arg(fn);
    case K::MissingCharacters:
        return FontCollectorController::tr("Font \"%1\" does not contain characters: \"%2\".").arg(fn, a);
    case K::FallbackCharacters:
        return FontCollectorController::tr("Characters \"%1\" were drawn by the fallback font \"%2\".").arg(a, b);
    case K::CannotGetContents:
        return FontCollectorController::tr("Cannot get the contents of font \"%1\".").arg(fn);
    case K::CannotOpenFile:
        return FontCollectorController::tr("Cannot open file \"%1\".").arg(a);
    case K::CannotFindInFolder:
        return FontCollectorController::tr("Cannot find \"%1\" font in Fonts folder.").arg(fn);
    case K::CannotCopy:
        return FontCollectorController::tr("Cannot copy font \"%1\".").arg(a);
    case K::CannotZip:
        return FontCollectorController::tr("Cannot zip font \"%1\".").arg(a);
    }
    return {};
}

// The other half of a Type 1 pair, read from the disk (FC-type1-pair).
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
    // SendMessageD: AppendTextWithStyle in its colour.
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
    // OnChangeOpt (FontCollector.cpp:543-553).
    m_action = std::clamp(action, 0, 2);
    m_useSubsDirectory = useSubsDirectory;
    m_settings.set("fontCollector.action", m_action);
    m_settings.set("fontCollector.useSubsDirectory", m_useSubsDirectory);
    m_settings.sync();
    emit settingsChanged();
}

QVariantMap FontCollectorController::chooserStart(const QString &path) const
{
    // OnButtonPath (FontCollector.cpp:418-429): the folder chooser starts in
    // the path; the archive chooser in its folder with its name when it
    // ends with "zip".
    if (m_action == 1)
        return {{QStringLiteral("folder"), path}, {QStringLiteral("name"), QString()}};
    const bool zip = path.endsWith(QLatin1String("zip"));
    return {{QStringLiteral("folder"), zip ? pathDir(path) : path}, {QStringLiteral("name"), zip ? pathName(path) : QString()}};
}

void FontCollectorController::chooseDirectory(const QString &path)
{
    // Options.SetString(FONT_COLLECTOR_DIRECTORY, destdir) and SaveOptions.
    // Legacy also stored a cancelled chooser's empty answer
    // (FontCollector.cpp:418-433); the previous path stays (FC-chooser-cancel).
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
    // OnButtonStart (FontCollector.cpp:435-541).
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
    if (m_action == 0) {
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
        return refuse(tr("Select the folder where you want to copy fonts"));
    if (m_useSubsDirectory && subsPath.isEmpty())
        return refuse(tr("No subtitles loaded. Load subtitles or deselect this option."));
    const QString pathValue = normalizePath(path);
    const bool zip = m_action == 2;
    if (zip && QFileInfo(pathValue).isDir() && !m_useSubsDirectory)
        return refuse(tr("Choose a name for the archive"));
    QString copypath;
    if (m_useSubsDirectory) {
        const QString rest = pathName(subsPath);
        // Legacy always wrote a Polish "Czcionki" folder (FontCollector.cpp:
        // 482); the folder is named in the interface language
        // (FC-czcionki). An existing Czcionki folder is left as it is.
        const QString fontDir =
            pathJoin(pathDir(subsPath), tr("Fonts", "the folder the font collector writes beside the subtitles"));
        const qsizetype dot = rest.lastIndexOf(QLatin1Char('.'));
        copypath = zip ? pathJoin(fontDir, (dot < 0 ? rest : rest.left(dot)) + QStringLiteral(".zip")) : fontDir + separator();
    } else {
        copypath = pathValue;
#ifdef _WIN32
        // wxFileName::GetVolume: a drive letter.
        const bool valid = QRegularExpression(QStringLiteral("^[A-Za-z]:")).match(copypath).hasMatch();
#else
        const bool valid = !copypath.isEmpty();
#endif
        if (!valid)
            return refuse(tr("The save path is not valid."));
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
        m_pendingArchive = copypath;
        return {{QStringLiteral("question"), tr("The zip file already exists, delete it?")},
                {QStringLiteral("title"), tr("Confirmation")},
                {QStringLiteral("archive"), copypath}};
    }
    m_copyPath = copypath;
    runPrepare();
    return {};
}

bool FontCollectorController::confirmReplace(bool remove)
{
    if (m_pendingArchive.isEmpty())
        return false;
    const QString archive = std::exchange(m_pendingArchive, QString());
    // Legacy removes the archive on Yes (wxRemoveFile, FontCollector.cpp:514-518)
    // and the run that writes the new one follows at once. Here a review
    // stands between them and nothing is written until Apply (routing #60,
    // L58-staged-replacement), so Yes is remembered and the archive removed
    // when Apply starts writing; a review closed or refused keeps it. No
    // keeps it, and the new archive replaces it when written.
    if (remove)
        m_removeArchive = archive;
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
    // FontLogContent::DoLog (FontCollector.cpp:95-132).
    QString info;
    switch (block.header) {
    case Block::Header::Found:
        info = tr("Found font \"%1\"\n").arg(qs(block.family));
        break;
    case Block::Header::FoundDot:
        info = tr("Found font \"%1\".").arg(qs(block.family));
        break;
    case Block::Header::NotFound:
        info = tr("Font not found \"") + qs(block.family) + QStringLiteral("\".\n");
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
        messageText += tr("In styles:\n");
    area.stylesFrom = position + int(messageText.size());
    for (const auto &[style, tabs] : block.styles) {
        messageText += QStringLiteral(" - ") + qs(style);
        messageText += tr(" tabs: ");
        QStringList numbers;
        for (const int t : tabs)
            numbers << QString::number(t + 1);
        messageText += numbers.join(QStringLiteral(", ")) + QLatin1Char('\n');
    }
    area.stylesTo = position + int(messageText.size());
    if (!block.lines.empty())
        messageText += tr("In lines: ");
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
    // The renderer's verification (fonts.md): what it did not find, what a
    // font of another name or a fallback drew, and whether the collected
    // fonts alone reproduce every frame.
    QString text;
    if (!review.provider.empty())
        text += tr("Checked by the subtitle renderer (%1).\n").arg(qs(review.provider));
    QStringList missing, substituted;
    for (const auto &f : review.missingFamilies)
        missing << qs(f);
    for (const auto &f : review.substitutedFamilies)
        substituted << qs(f);
    QString problems;
    for (const int t : review.unrendered)
        problems += tr("Tab %1: the renderer could not read this Document; it was not checked.\n").arg(t + 1);
    if (!missing.isEmpty())
        problems += tr("Families the renderer did not find: %1.\n").arg(missing.join(QStringLiteral(", ")));
    if (!substituted.isEmpty())
        problems += tr("Families answered by a font of another name: %1.\n").arg(substituted.join(QStringLiteral(", ")));
    if (!review.missingGlyphs.empty())
        problems += tr("Characters no font has: %1.\n").arg(codes(review.missingGlyphs));
    if (!review.fallbackGlyphs.empty())
        problems += tr("Characters drawn by a fallback font of this computer: %1.\n").arg(codes(review.fallbackGlyphs));
    for (const auto &r : review.reimports)
        if (!r.identical)
            problems += tr("Tab %1: with the collected fonts alone, %2 of %3 frames differ.\n")
                            .arg(r.tab + 1)
                            .arg(r.differingFrames.size())
                            .arg(r.frames);
    QStringList clashes;
    for (const auto &n : review.nameClashes)
        clashes << qs(n);
    if (!clashes.isEmpty())
        problems += tr("Different fonts with the same file name: %1. Only one of them is kept in a folder.\n")
                        .arg(clashes.join(QStringLiteral(", ")));
    bool reproduced = !review.reimports.empty() && review.unrendered.empty();
    for (const auto &r : review.reimports)
        reproduced = reproduced && r.identical;
    send(text, kNormal);
    send(problems, kWarning);
    if (reproduced)
        send(tr("The collected fonts alone reproduce every frame.\n"), kNormal);
}

void FontCollectorController::logSummary(const CollectorReview &review, const CollectorResult &result)
{
    // CheckOrCopyFonts (FontCollector.cpp:844-873). Output the renderer does
    // not agree with, or that was not all written, is never reported as
    // success (fonts.md; partial-output rule).
    const bool checkFonts = review.action == CollectorAction::Check;
    const QString verb = checkFonts ? tr("found") : tr("copied");
    const bool success = result.complete;
    if (!success) {
        QString message = QStringLiteral("\n") + tr("Finished, %1 %2.\n").arg(verb, fonts(result.foundCount));
        if (result.notFoundCount)
            message += tr("Not found %1.\n").arg(fonts(result.notFoundCount));
        if (result.notCopiedCount)
            message += tr("Cannot copy %1.\n").arg(fonts(result.notCopiedCount));
        if (!result.allGlyphs)
            message += tr("Some fonts do not contain all glyphs used in the text.\n");
        if (!review.rendererComplete())
            message += tr("The renderer cannot reproduce the subtitles from these fonts alone: the collection is "
                          "incomplete.\n");
        else if (!review.nameClashes.empty())
            message += tr("The output does not hold every collected font under its own name: the collection is "
                          "incomplete.\n");
        if (result.cancelled)
            message += tr("Writing was cancelled.\n");
        if (result.labelled)
            message += tr("The output is labelled incomplete (\"%1\").\n").arg(QString::fromUtf8(backends::kIncompleteLabel));
        send(message, kWarning);
    } else {
        send(QStringLiteral("\n") + tr("Completed Successfully, %1 %2.\n").arg(verb, fonts(result.foundCount)), kSuccess);
    }
}

void FontCollectorController::logFinished()
{
    // FontCollectorThread::Entry (FontCollector.cpp:1335-1336).
    send(QStringLiteral("\n") + tr("Finished in %1ms").arg(srtTime(m_elapsed)), kNormal);
}

void FontCollectorController::prepared(std::expected<CollectorReview, application::FontError> review)
{
    join();
    m_elapsed += m_clock.elapsed();
    clearLog();
    const bool copy = m_runAction != CollectorAction::Check;
    if (!review) {
        send(review.error() == application::FontError::Cancelled
                 ? tr("Cancelled; nothing was written.\n")
                 : tr("The subtitle renderer is not available; the fonts cannot be checked.\n"),
             kWarning);
        logFinished();
        emit logChanged();
        setStage(Done);
        return;
    }
    m_review = std::move(*review);
    if (m_review->retrieveFailed) {
        send(tr("Cannot retrieve the font file sizes and names;\ncopying will be canceled.\n"), kWarning);
        logFinished();
        m_canSaveFolder = true; // EVT_ENABLE_OPEN_FOLDER after any copy run
        m_review.reset();
        emit logChanged();
        setStage(Done);
        return;
    }
    if (m_review->retrievedFonts)
        send(tr("Retrieved sizes and names of %1 fonts, elapsed time %2ms.\n\n")
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
    // The staged review: what Apply would write, and where.
    const int files = int(m_review->files.size());
    QString ready = QStringLiteral("\n") +
                    (m_runAction == CollectorAction::Zip
                         ? tr("Ready to add %1 to the archive \"%2\".\n").arg(fonts(files), m_copyPath)
                         : tr("Ready to copy %1 to \"%2\".\n").arg(fonts(files), m_copyPath));
    send(ready, kNormal);
    if (!m_review->complete())
        send(tr("This collection is incomplete. It is written only when you acknowledge that, and the output is then "
                "labelled incomplete.\n"),
             kWarning);
    emit logChanged();
    setStage(Review);
}

void FontCollectorController::apply(bool acknowledged)
{
    if (m_stage != Review || !m_review)
        return;
    if (!m_review->complete() && !acknowledged)
        return;
    // The Yes to "The zip file already exists, delete it?": legacy's
    // wxRemoveFile, and when it fails nothing runs (FontCollector.cpp:515-518).
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
    if (!m_review) { // not expected: close() keeps the review while a job runs
        setStage(Done);
        return;
    }
    const CollectorReview review = *m_review;
    m_review.reset();
    clearLog();
    if (review.retrievedFonts)
        send(tr("Retrieved sizes and names of %1 fonts, elapsed time %2ms.\n\n")
                 .arg(*review.retrievedFonts)
                 .arg(srtTime(review.retrieveMs)),
             kNormal);
    if (result.pathNotAvailable) {
        // MakeDirectory and CheckOrCopyFonts (FontCollector.cpp:1005, 822).
        if (result.cannotCreateFolder)
            send(tr("Cannot create folder."), kWarning);
        send(tr("Path is not available"), kWarning);
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

void FontCollectorController::cancel()
{
    m_cancel = true;
}

void FontCollectorController::close()
{
    m_cancel = true;
    // A job that outlasts the wait ends cancelled later; its result (applied)
    // still reads the review and drops it then.
    if (m_stage == Working && !waitIdle(m_closeWaitMs))
        return;
    m_review.reset();
    m_removeArchive.clear(); // the review's Yes goes with it: the archive stays
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
    // SHOpenFolderAndSelectItems on the path.
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,") + QDir::toNativeSeparators(m_copyPath)});
#else
    // SelectInFolder outside Windows opens the path's folder.
    QString target = pathDir(m_copyPath);
    if (target.isEmpty())
        target = m_copyPath;
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
#endif
}

void FontCollectorController::logDoubleClicked(int position)
{
    // OnConsoleDoubleClick and ParseDoubleClickResults (FontCollector.cpp:269-389).
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
        const qsizetype result = name.indexOf(tr(" tabs: "));
        if (result >= 0)
            name = name.left(result);
        return name;
    };
    const Block &block = hit->block;
    if (number.match(word).hasMatch()) {
        if (isStyle) {
            const QString line = text.left(end).section(QLatin1Char('\n'), -1);
            if (!line.startsWith(QLatin1String(" - ")) || line.mid(3).indexOf(tr(" tabs: ")) < 0)
                return;
            const QString style = line.mid(3).left(line.mid(3).indexOf(tr(" tabs: ")));
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
