// F3: the Qt side of the spell checker. Word segmentation stands in for
// legacy's boost::locale over ICU (SpellChecker::Check / CheckText at
// 20d647c4): words with letters are checked, numbers and punctuation are
// not, and tags, drawings and breaks are left out by the core walk. The
// Grid's marks role and config::FindLanguage's names are pinned too.

#include "line_table_model.h"
#include "spelling_text.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/spelling.h"

#include <QtTest>

#include <cstring>
#include <set>

using namespace hikari;

namespace {

struct Segment {
    QString text;
    bool letters;
    bool number;
};

std::vector<Segment> words(const QString &text)
{
    const auto spelling = ui::qtSpellingText();
    const std::u16string s = text.toStdU16String();
    std::vector<Segment> out;
    for (const auto &seg : spelling.segment(s))
        out.push_back({QString::fromStdU16String(s.substr(seg.start, seg.length)), seg.letters, seg.number});
    return out;
}

core::Document load(std::string_view text)
{
    std::vector<std::byte> b(text.size());
    std::memcpy(b.data(), text.data(), text.size());
    return core::loadAss(b).document;
}

} // namespace

class SpellingUiTests : public QObject {
    Q_OBJECT
private slots:
    // Segments cover the text; apostrophes and decimal points stay inside
    // words and numbers (ICU's MidLetter/MidNum rules), letters mixed with
    // digits are a letters word, and punctuation is neither.
    void segmentsLikeIcuWordRules()
    {
        const auto s = words(QStringLiteral("don't 3.14 abc123, 42!"));
        QStringList texts;
        for (const auto &w : s)
            texts << w.text;
        QCOMPARE(texts, (QStringList{"don't", " ", "3.14", " ", "abc123", ",", " ", "42", "!"}));
        QVERIFY(s[0].letters && !s[0].number);
        QVERIFY(!s[2].letters && s[2].number);
        QVERIFY(s[4].letters);
        QVERIFY(!s[5].letters && !s[5].number);
        // Polish letters are letters; an ideograph is a word of its own here
        // (ICU's dictionary segmentation would join "世界" into one word).
        const auto pl = words(QString::fromUtf8("Zażółć 世界"));
        QCOMPARE(pl[0].text, QString::fromUtf8("Zażółć"));
        QVERIFY(pl[0].letters);
        QCOMPARE(pl.size(), std::size_t(4));
        QCOMPARE(pl[2].text, QString::fromUtf8("世"));
        QVERIFY(pl[2].letters);
    }

    // The core walk with the Qt segmenter: tag-aware offsets in the raw text.
    void tagAwareMarks()
    {
        const auto spelling = ui::qtSpellingText();
        const std::set<std::u16string> dictionary{u"gęślą", u"don't"};
        const core::legacy::WordCheck check = [&](std::u16string_view w) {
            return dictionary.contains(std::u16string(w));
        };
        const std::u16string text = QString::fromUtf8("{\\i1}Zażółć{\\i0} gęślą don't\\Ndont {\\p1}m 0 0{\\p0}").toStdU16String();
        const auto marks = core::legacy::checkTextAndBrackets(text, core::SubtitleFormat::Ass, spelling.segment, check);
        QCOMPARE(marks.errors, (std::vector<int>{5, 10, 30, 33}));
        QCOMPARE(marks.misspells.size(), std::size_t(2));
        QCOMPARE(QString::fromStdU16String(marks.misspells[0].word), QString::fromUtf8("Zażółć"));
        // Per-unit case functions: GetRightCase keeps a capital.
        QCOMPARE(QString::fromStdU16String(core::legacy::rightCase(u"żółw", u"Zólw", spelling.cases)),
                 QString::fromUtf8("Żółw"));
        QVERIFY(core::legacy::isAllUpperCase(QString::fromUtf8("ŻÓŁW").toStdU16String(), spelling.cases));
    }

    // FindLanguage: shipped names by symbol or language, then the locale's
    // own name for its language, then the symbol itself.
    void dictionaryNames()
    {
        QCOMPARE(ui::dictionaryName(QStringLiteral("pl_PL")), QStringLiteral("Polski"));
        QCOMPARE(ui::dictionaryName(QStringLiteral("en_US")), QStringLiteral("English"));
        QCOMPARE(ui::dictionaryName(QStringLiteral("de_DE")), QStringLiteral("Deutsch"));
        QCOMPARE(ui::dictionaryName(QStringLiteral("qq")), QStringLiteral("qq"));
    }

    // The Grid's marks (TextData::Init): comments have none; the original in
    // translation mode is checked for brackets only.
    void gridMarksRole()
    {
        ui::LineTableModel model;
        std::vector<bool> spellRequests;
        model.setSpelling([&](std::u16string_view text, core::SubtitleFormat format, bool spell) {
            spellRequests.push_back(spell);
            const core::legacy::WordCheck check = [](std::u16string_view w) { return w != u"wrold"; };
            return core::legacy::checkTextAndBrackets(text, format, ui::qtSpellingText().segment,
                                                      spell ? check : core::legacy::WordCheck{});
        });
        model.setDocument(load("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello wrold}\n"
                               "Comment: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,wrold\n"));
        QCOMPARE(model.index(0, 0).data(ui::LineTableModel::SpellMarksRole).toList(),
                 (QVariantList{11, 11, 6, 10}));
        QVERIFY(model.index(1, 0).data(ui::LineTableModel::SpellMarksRole).toList().isEmpty());
        QCOMPARE(spellRequests, std::vector<bool>{true});
        model.setDocument(load("[Script Info]\nTLMode: Yes\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, "
                               "MarginR, MarginV, Effect, Text\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Hello wrold}\n"));
        QCOMPARE(model.index(0, 0).data(ui::LineTableModel::SpellMarksRole).toList(), (QVariantList{11, 11}));
    }
};

QTEST_MAIN(SpellingUiTests)
#include "spelling_ui_tests.moc"
