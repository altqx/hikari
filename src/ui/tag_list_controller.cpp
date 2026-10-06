#include "tag_list_controller.h"

#include <QCoreApplication>
#include <QFontMetricsF>

#include <algorithm>
#include <array>

namespace hikari::ui {

namespace {

// PopupTagList::InitList's descriptions (TextEditorTagList.cpp:340-416), in
// the entries' order.
constexpr std::array<const char *, core::taglist::kEntryCount> kDescriptions{{
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency of primary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency of secondary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency of border color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency of shadow color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Primary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Secondary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Border color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Shadow color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "PNG mask of primary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "PNG mask of secondary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "PNG mask of border color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "PNG mask of shadow color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency gradient of primary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency gradient of secondary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency gradient of border color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency gradient of shadow color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Gradient of primary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Gradient of secondary color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Gradient of border color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Gradient of shadow color"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text alignment (SSA)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Transparency"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text position"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Bold text"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Edge blur"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Blur of border, shadow or font"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Thickness of border"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Vector clip / rectangle clip"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font distortion"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Fading in / fading out of text"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Fading in / fading out of text (advanced version)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Skew on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Skew on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text encoding"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font name"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text rounding"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Rotation on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Rotation on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Rotation on Z axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font size"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Scale on X and Y axes"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Scale on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Scale on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font spacing"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Vertical font spacing"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Italic text"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Reverse vector clip or rectangle clip"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text jitter"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Karaoke timing"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Karaoke timing smooth transition"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Karaoke timing border"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Karaoke timing (not supported)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text movement"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text movement along a circle"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text movement along a curve (3 points)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text movement along a curve (4 points)"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Vector drawing movement"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Anchor for rotation"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Drawing and its scale"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Offset of Y vector points"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text position"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text wrap method"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Reset tags"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness seed"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Font point randomness on Z axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text strikethrough"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text shadow"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text animation"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Text underline"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Border on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Border on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Shadow on X axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Shadow on Y axis"),
    QT_TRANSLATE_NOOP("HikariSub.TagList", "Z coordinate for tags \\frx and \\fry"),
}};

std::u16string u16(const QString &s)
{
    return s.toStdU16String();
}

} // namespace

TagListController::TagListController(QObject *parent) : QObject(parent) {}

void TagListController::setOptionsStore(ReadOptions read, WriteOptions write)
{
    m_read = std::move(read);
    m_write = std::move(write);
}

QString TagListController::description(std::size_t entry)
{
    return entry < kDescriptions.size() ? QCoreApplication::translate("HikariSub.TagList", kDescriptions[entry])
                                        : QString();
}

QString TagListController::rowText(std::size_t entry) const
{
    // TagListItem::GetTagText (TextEditorTagList.h:48-52).
    QString text = QString::fromStdU16String(std::u16string(core::taglist::entries()[entry].tag));
    if (m_options & core::taglist::ShowDescription)
        text += QStringLiteral(" - ") + description(entry);
    return text;
}

QStringList TagListController::rows() const
{
    QStringList out;
    if (const auto *list = m_completion.list())
        for (const auto entry : list->shownEntries())
            out << rowText(entry);
    return out;
}

QString TagListController::selectedAnnouncement() const
{
    const auto *list = m_completion.list();
    if (!list || !list->popupShown())
        return {};
    const auto entry = list->entryAt(list->selection());
    if (!entry)
        return {};
    const QString tag = QStringLiteral("\\") + QString::fromStdU16String(std::u16string(core::taglist::entries()[*entry].tag));
    return tr("%1, %2, %3 of %4")
        .arg(tag, description(*entry))
        .arg(list->selection() + 1)
        .arg(list->count());
}

bool TagListController::typed(const QString &text, int caret, const QString &key)
{
    if (key.size() != 1)
        return false;
    const bool wasOpen = m_completion.open();
    // The PopupTagList constructor reads the options (TextEditorTagList.cpp:250).
    const int options = m_read ? m_read() : m_options;
    if (!wasOpen)
        m_options = options;
    const bool acted = m_completion.typed(u16(text), static_cast<std::size_t>(std::max(0, caret)), key.at(0).unicode(),
                                          m_options);
    if (!acted)
        return false;
    emit changed();
    return true;
}

bool TagListController::move(int delta)
{
    if (!m_completion.move(delta))
        return false;
    emit changed();
    return true;
}

QVariantMap TagListController::put(const QString &text, int caret)
{
    const auto put = m_completion.put(u16(text), static_cast<std::size_t>(std::max(0, caret)));
    if (!put)
        return {};
    emit changed();
    return {{QStringLiteral("text"), QString::fromStdU16String(put->text)},
            {QStringLiteral("caret"), static_cast<int>(put->caret)}};
}

void TagListController::close()
{
    if (!m_completion.open())
        return;
    m_completion.close();
    emit changed();
}

bool TagListController::pointerAt(int row)
{
    auto *list = m_completion.list();
    if (!list)
        return false;
    const int before = list->selection();
    const bool acts = list->pointerAt(row);
    if (list->selection() != before)
        emit changed();
    return acts;
}

void TagListController::scrollBy(int rows)
{
    auto *list = m_completion.list();
    if (!list)
        return;
    const int before = list->scrollPosition();
    list->scrollBy(rows);
    if (list->scrollPosition() != before)
        emit changed();
}

void TagListController::wheel(int notches)
{
    auto *list = m_completion.list();
    if (!list)
        return;
    const int before = list->scrollPosition();
    // step = 3 * rotation / delta; scrollPositionV -= step.
    list->wheel(-3 * notches);
    if (list->scrollPosition() != before)
        emit changed();
}

void TagListController::toggleOption(int bit)
{
    auto *list = m_completion.list();
    if (!list || (bit != 1 && bit != 2 && bit != 4))
        return;
    // PopupWindow::OnMouseEvent (TextEditorTagList.cpp:130-149): the stored
    // options flip, are saved, and filter the list again.
    const int options = (m_read ? m_read() : m_options) ^ bit;
    if (m_write)
        m_write(options);
    m_options = options;
    list->filterByOptions(options);
    emit changed();
}

double TagListController::popupWidth(const QFont &font) const
{
    const QFontMetricsF metrics(font);
    double width = 0;
    for (std::size_t entry = 0; entry < core::taglist::kEntryCount; ++entry)
        width = std::max(width, metrics.horizontalAdvance(rowText(entry)));
    width += 18;
    const auto *list = m_completion.list();
    if (list && list->count() > static_cast<std::size_t>(core::taglist::kMaxVisible))
        width += 20;
    width = std::min(width, 800.0);
    // The first Popup reads the control size before it is set (0); the
    // later ones the editor's 100 (DialogueTextEditor.cpp:472).
    if (list && list->popupCalls() > 1)
        width = std::max(width, 100.0);
    return width;
}

double TagListController::rowHeight(const QFont &font) const
{
    return QFontMetricsF(font).height() + 6;
}

} // namespace hikari::ui
