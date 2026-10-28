// F3: the Qt side of the spell checker: the Grid's marks role (legacy
// SubsGridWindow at 20d647c4) and config::FindLanguage's names. Word
// segmentation (boost::locale over ICU) is pinned in the backends' tests.

#include "line_table_model.h"
#include "spelling_text.h"
#include "spelling/fake_spelling.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/spelling.h"

#include <QtTest>

#include <cstring>
#include <set>

using namespace hikari;

namespace {

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
    // FindLanguage: shipped names by symbol or language, then the locale's
    // own name for its language, then the symbol itself.
    void dictionaryNames()
    {
        QCOMPARE(ui::dictionaryName(QStringLiteral("pl_PL")), QStringLiteral("Polski"));
        QCOMPARE(ui::dictionaryName(QStringLiteral("en_US")), QStringLiteral("English"));
        QCOMPARE(ui::dictionaryName(QStringLiteral("de_DE")), QStringLiteral("Deutsch"));
        QCOMPARE(ui::dictionaryName(QStringLiteral("qq")), QStringLiteral("qq"));
    }

    // The Grid's marks (TextData::Init): comments have none. Legacy draws
    // marks on the last column only, which in translation mode is the
    // translation column (not built yet), so the Text column has none there.
    void gridMarksRole()
    {
        ui::LineTableModel model;
        std::vector<bool> spellRequests;
        model.setSpelling([&](std::u16string_view text, core::SubtitleFormat format, bool spell) {
            spellRequests.push_back(spell);
            const core::legacy::WordCheck check = [](std::u16string_view w) { return w != u"wrold"; };
            return core::legacy::checkTextAndBrackets(text, format, fakes::asciiSpellingText().segment,
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
        QVERIFY(model.index(0, 0).data(ui::LineTableModel::SpellMarksRole).toList().isEmpty());
        QCOMPARE(spellRequests, std::vector<bool>{true});
    }
};

QTEST_MAIN(SpellingUiTests)
#include "spelling_ui_tests.moc"
