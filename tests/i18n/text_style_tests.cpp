#include "hikari/i18n/ts.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

using namespace hikari::i18n;

namespace {

QByteArray readSource(const QString &relative)
{
    QFile file(QStringLiteral(HIKARI_SOURCE_DIR "/") + relative);
    if (!file.open(QIODevice::ReadOnly))
        ADD_FAILURE() << "cannot read " << relative.toStdString();
    return file.readAll();
}

QString withoutMnemonics(const QString &source)
{
    QString out;
    for (qsizetype i = 0; i < source.size(); ++i) {
        if (source[i] == u'&' && i + 1 < source.size()) {
            ++i;
            out += source[i];
        } else {
            out += source[i];
        }
    }
    return out;
}

const QSet<QString> &properNames()
{
    static const QSet<QString> names = [] {
        QSet<QString> set;
        for (const QString &w : QStringLiteral(
                 "HikariSub Aegisub Kainote ASS SSA SRT VTT MicroDVD MPL2 TMPlayer TTML Lua MoonScript Windows Linux macOS "
                 "Matroska MKV FFmpeg DirectShow Harper Hunspell Discord PostHog Unicode VSFilter VSFilterMod libass Qt GitHub "
                 "Automattic AUR AppImage Ctrl Shift Alt Enter Esc Tab Del Backspace OK CPS FPS ID URL JSON TOML XML GUI UI RGB "
                 "TV PC AI DPI DependencyControl English Japanese Korean Polish Tamil Thai Romaji Kanji MeCab Wayland X11 GNU "
                 "GPL Apache Script Info Events Fonts Graphics Grid Line Log Home Automation Settings Reference Spellchecker "
                 "Style Timing Translation Template Original Translated Unconfirmed Audio Video")
                 .split(u' '))
            set.insert(w);
        return set;
    }();
    return names;
}

const QList<QRegularExpression> &americanSpellings()
{
    static const QList<QRegularExpression> list = [] {
        const char *const patterns[] = {
            "\\bcolor(s|ed|ing)?\\b", "\\bbehavior", "\\bcenter(ed|s)?\\b", "\\bgray\\b", "\\bcancel(ed|ing)\\b",
            "\\blabel(ed|ing)\\b", "\\bfavorite", "\\bcatalogs?\\b", "\\banalyz", "\\btoward\\b", "\\bafterward\\b",
            "\\banalog\\b", "\\bartifact",
            "\\b(initial|organ|capital|normal|synchron|recogn|optim|custom|minim|maxim|priorit|serial|visual|author|util|"
            "special|summar)iz"};
        QList<QRegularExpression> out;
        for (const char *pattern : patterns)
            out.append(QRegularExpression(QString::fromLatin1(pattern), QRegularExpression::CaseInsensitiveOption));
        return out;
    }();
    return list;
}

bool isLabel(const QString &text)
{
    static const QRegularExpression sentenceEnd(QStringLiteral("[.?!]\\s*$"));
    return !text.contains(sentenceEnd) && text.split(u' ', Qt::SkipEmptyParts).size() <= 8;
}

QStringList textStyleProblems(const QString &source)
{
    const QString text = withoutMnemonics(source);
    QStringList rules;
    if (text.contains(QLatin1String("...")))
        rules << QStringLiteral("ellipsis");
    static const QRegularExpression apostrophe(QStringLiteral("\\w'|'\\w"));
    if (text.contains(u'"') || text.contains(apostrophe))
        rules << QStringLiteral("quotes");
    static const QRegularExpression ownName(QStringLiteral("General Public License|Apache License"));
    const QString checked = QString(text).remove(ownName);
    for (const QRegularExpression &american : americanSpellings())
        if (checked.contains(american)) {
            rules << QStringLiteral("spelling");
            break;
        }
    if ((text.startsWith(QLatin1String("The ")) || text.startsWith(QLatin1String("the "))) && isLabel(text))
        rules << QStringLiteral("leading-the");
    if (text.size() < 60 && text.trimmed().endsWith(u':'))
        rules << QStringLiteral("colon");
    if (text != text.trimmed() || text.contains(QLatin1String("  ")))
        rules << QStringLiteral("whitespace");
    if (text.contains(u'\t'))
        rules << QStringLiteral("hard-coded-key");
    static const QRegularExpression please(QStringLiteral("\\bplease\\b"), QRegularExpression::CaseInsensitiveOption);
    if (text.contains(please))
        rules << QStringLiteral("please");
    static const QRegularExpression word(QStringLiteral("[A-Za-z][\\w’'+.-]*"));
    static const QRegularExpression startsAnew(QStringLiteral("([:.?!“\"(›>/-]|·)\\s*$"));
    QList<QRegularExpressionMatch> words;
    for (auto it = word.globalMatch(text); it.hasNext();)
        words.append(it.next());
    if (text.size() < 60 && words.size() >= 2 && words.size() <= 8) {
        for (qsizetype i = 1; i < words.size(); ++i) {
            const QString w = words[i].captured();
            QString bare = w;
            while (bare.endsWith(u'.'))
                bare.chop(1);
            if (w[0].isUpper() && w != w.toUpper() && !properNames().contains(bare) &&
                !text.left(words[i].capturedStart()).contains(startsAnew)) {
                rules << QStringLiteral("title-case");
                break;
            }
        }
    }
    return rules;
}

QString escaped(QString s)
{
    return s.replace(u'\\', QLatin1String("\\\\")).replace(u'\n', QLatin1String("\\n")).replace(u'\t', QLatin1String("\\t"));
}

struct Allowed {
    QString context, source, rule;
};

QList<Allowed> readAllowList(const QByteArray &bytes, QString *error)
{
    QList<Allowed> rows;
    const QStringList lines = QString::fromUtf8(bytes).split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.isEmpty() || line.startsWith(u'#') || line == QLatin1String("context\tsource\trule\treason"))
            continue;
        const QStringList cells = line.split(u'\t');
        if (cells.size() != 4 || cells[3].trimmed().isEmpty()) {
            *error = QStringLiteral("line %1 needs context, source, rule and a reason").arg(i + 1);
            return {};
        }
        rows.append({cells[0], cells[1], cells[2]});
    }
    return rows;
}

}

TEST(TextStyle, FindsEachRule)
{
    auto has = [](const char *source, const char *rule) {
        return textStyleProblems(QString::fromUtf8(source)).contains(QString::fromLatin1(rule));
    };
    EXPECT_TRUE(has("Shift times...", "ellipsis"));
    EXPECT_FALSE(has("Shift times…", "ellipsis"));
    EXPECT_TRUE(has("Don't send", "quotes"));
    EXPECT_TRUE(has("the conversion style \"%1\".", "quotes"));
    EXPECT_FALSE(has("Don’t send “%1”", "quotes"));
    EXPECT_TRUE(has("Border color", "spelling"));
    EXPECT_TRUE(has("Top-center", "spelling"));
    EXPECT_TRUE(has("Failed to initialize the renderer.", "spelling"));
    EXPECT_TRUE(has("Add to catalog", "spelling"));
    EXPECT_FALSE(has("Border colour", "spelling"));
    EXPECT_FALSE(has("Resize the window", "spelling"));
    EXPECT_FALSE(has("HikariSub is free software under the GNU General Public License, version 3.", "spelling"));
    EXPECT_TRUE(has("The starting time", "leading-the"));
    EXPECT_FALSE(has("The list keeps at least one tag.", "leading-the"));
    EXPECT_TRUE(has("Filtering mode:", "colon"));
    EXPECT_FALSE(has("Line %1: no end time", "colon"));
    EXPECT_TRUE(has("Video: ", "whitespace"));
    EXPECT_TRUE(has("Copy\tCtrl+C", "hard-coded-key"));
    EXPECT_TRUE(has("Please choose a file", "please"));
    EXPECT_TRUE(has("Apply to All", "title-case"));
    EXPECT_TRUE(has("Insert &Tag at text beginning", "title-case"));
    EXPECT_FALSE(has("Open in Style manager", "title-case"));
    EXPECT_FALSE(has("Saves %1 · Discards %2", "title-case"));
    EXPECT_FALSE(has("Show VSFilterMod tags", "title-case"));
    EXPECT_FALSE(has("Shift &times…", "title-case"));
}

TEST(TextStyle, AllowListRowsNeedAReason)
{
    QString error;
    EXPECT_EQ(readAllowList("context\tsource\trule\treason\nHikariSub.Grid\tStyles\tspelling\tpending\n", &error).size(), 1);
    EXPECT_TRUE(readAllowList("context\tsource\trule\treason\nHikariSub.Grid\tStyles\tspelling\t\n", &error).isEmpty());
    EXPECT_FALSE(error.isEmpty());
}

// docs/ux/text-style.md, "The guard": every catalog source follows the guide or is allow-listed with a reason.
TEST(CheckedIn, InterfaceTextFollowsTheStyleGuide)
{
    const auto catalog = readTs(readSource(QStringLiteral("i18n/hikarisub_pl.ts")));
    ASSERT_TRUE(catalog.has_value()) << catalog.error().toStdString();
    QString error;
    const QList<Allowed> allowed = readAllowList(readSource(QStringLiteral("i18n/text-style-allow.tsv")), &error);
    ASSERT_TRUE(error.isEmpty()) << error.toStdString();
    QStringList unlisted;
    for (const TsContext &context : catalog->contexts) {
        for (const TsMessage &message : context.messages) {
            if (message.state == TsState::Vanished || message.state == TsState::Obsolete)
                continue;
            for (const QString &rule : textStyleProblems(message.source)) {
                const QString source = escaped(message.source);
                const bool listed = std::ranges::any_of(allowed, [&](const Allowed &a) {
                    return a.context == context.name && a.source == source && a.rule == rule;
                });
                if (!listed)
                    unlisted << context.name + u'\t' + source + u'\t' + rule + QStringLiteral("\tpending #596");
            }
        }
    }
    EXPECT_TRUE(unlisted.isEmpty()) << "Fix these strings (docs/ux/text-style.md) or add them to i18n/text-style-allow.tsv "
                                       "with a reason:\n"
                                    << unlisted.join(u'\n').toStdString();
}
