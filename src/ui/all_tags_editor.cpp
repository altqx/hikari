#include "all_tags_editor.h"

#include "hikari/application/visual_all_tags.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace hikari::ui {

using application::visual::AllTagsEdition;
using application::visual::AllTagsSetting;

namespace {

QString qs(std::u16string_view text)
{
    return QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
}

QString number(double value)
{
    return qs(application::visual::numCtrlText(value));
}

const QSet<QString> &defaultNames()
{
    static const QSet<QString> names = [] {
        QSet<QString> out;
        for (const AllTagsSetting &t : application::visual::defaultAllTags())
            out.insert(qs(t.name));
        return out;
    }();
    return names;
}

}

AllTagsEditor::AllTagsEditor(std::vector<AllTagsSetting> tags, int curTag, Hooks hooks, QObject *parent)
    : QObject(parent), m_edition(std::move(tags), curTag), m_hooks(std::move(hooks))
{
    connect(this, &AllTagsEditor::changed, this, &AllTagsEditor::previewChanged);
}

QVariantList AllTagsEditor::items() const
{
    QVariantList out;
    const auto sections = application::visual::allTagsSections(m_edition.tags());
    for (std::size_t i = 0; i < m_edition.tags().size(); ++i) {
        const AllTagsSetting &t = m_edition.tags()[i];
        out.append(QVariantMap{{QStringLiteral("name"), qs(t.name)},
                               {QStringLiteral("tag"), qs(t.tag)},
                               {QStringLiteral("section"), sections[i]}});
    }
    return out;
}

void AllTagsEditor::select(int index)
{
    if (index == selection())
        return;
    m_edition.select(index);
    emit changed();
}

QString AllTagsEditor::name() const
{
    return qs(m_edition.current().name);
}

QString AllTagsEditor::tag() const
{
    return qs(m_edition.current().tag);
}

QVariantMap AllTagsEditor::numbers() const
{
    const AllTagsSetting &t = m_edition.current();
    return {{QStringLiteral("min"), number(t.rangeMin)},
            {QStringLiteral("max"), number(t.rangeMax)},
            {QStringLiteral("step"), number(t.step)},
            {QStringLiteral("digits"), QString::number(t.digitsAfterDot)},
            {QStringLiteral("value0"), number(t.values[0])},
            {QStringLiteral("value1"), number(t.values[1])},
            {QStringLiteral("value2"), number(t.values[2])},
            {QStringLiteral("value3"), number(t.values[3])}};
}

QStringList AllTagsEditor::valueNames() const
{
    QStringList out;
    for (const auto &n : application::visual::allTagsValueNames(m_edition.current()))
        out.append(qs(n));
    return out;
}

QVariantMap AllTagsEditor::preview() const
{
    const AllTagsSetting &t = m_edition.current();
    const std::u16string line = m_hooks.activeLineText ? m_hooks.activeLineText() : std::u16string();
    if (t.tag.empty())
        return {{QStringLiteral("before"), QStringLiteral("{")},
                {QStringLiteral("tag"), QString()},
                {QStringLiteral("after"), QStringLiteral("}") + qs(line)}};
    const auto p = application::visual::allTagsSetPreview(t, line);
    return {{QStringLiteral("before"), qs(p.before)},
            {QStringLiteral("tag"), qs(p.tag)},
            {QStringLiteral("after"), qs(p.after)}};
}

int AllTagsEditor::ownCount() const
{
    return static_cast<int>(std::ranges::count_if(m_edition.tags(), [](const AllTagsSetting &t) {
        return !defaultNames().contains(qs(t.name));
    }));
}

void AllTagsEditor::kept()
{
    emit changed();
    if (m_edition.revision() == m_published)
        return;
    m_published = m_edition.revision();
    if (m_hooks.changed)
        m_hooks.changed(m_edition.usable(), m_edition.renamed());
}

int AllTagsEditor::checked(AllTagsEdition::Check check)
{
    if (check == AllTagsEdition::Check::Ok)
        kept();
    return static_cast<int>(check);
}

void AllTagsEditor::setCount(int count)
{
    if (count == this->count())
        return;
    m_edition.setCount(count);
    kept();
}

void AllTagsEditor::setChangeOption(int option)
{
    if (option == changeOption())
        return;
    m_edition.setChangeOption(option);
    kept();
}

void AllTagsEditor::setPlacing(int placing)
{
    if (placing == this->placing())
        return;
    m_edition.setPlacing(placing);
    kept();
}

int AllTagsEditor::setName(const QString &name)
{
    if (name == this->name())
        return CheckOk;
    std::u16string taken;
    const auto check = m_edition.setName(name.toStdU16String(), &taken);
    m_taken = qs(taken);
    return checked(check);
}

int AllTagsEditor::setTag(const QString &tag)
{
    QString bare = tag;
    while (bare.startsWith(QLatin1Char('\\')))
        bare.remove(0, 1);
    if (bare == this->tag())
        return CheckOk;
    return checked(m_edition.setTag(tag.toStdU16String()));
}

int AllTagsEditor::setNumber(const QString &field, const QString &text)
{
    using Number = AllTagsEdition::Number;
    static const QHash<QString, Number> fields{
        {QStringLiteral("min"), Number::Min},       {QStringLiteral("max"), Number::Max},
        {QStringLiteral("step"), Number::Step},     {QStringLiteral("digits"), Number::Digits},
        {QStringLiteral("value0"), Number::Value0}, {QStringLiteral("value1"), Number::Value1},
        {QStringLiteral("value2"), Number::Value2}, {QStringLiteral("value3"), Number::Value3}};
    const auto it = fields.constFind(field);
    if (it == fields.cend())
        return CheckNotANumber;
    if (text == numbers().value(field).toString())
        return CheckOk;
    float limit = 0;
    const auto check = m_edition.setNumber(*it, text.toStdU16String(), &limit);
    m_limit = number(limit);
    return checked(check);
}

void AllTagsEditor::addTag()
{
    m_edition.addTag();
    kept();
}

void AllTagsEditor::duplicate()
{
    m_edition.duplicate();
    kept();
}

bool AllTagsEditor::remove()
{
    if (!m_edition.remove())
        return false;
    kept();
    return true;
}

void AllTagsEditor::replaceNamed(const QString &name)
{
    m_edition.replaceNamed(name.toStdU16String());
    kept();
}

void AllTagsEditor::restoreDefaults()
{
    m_edition.restoreDefaults();
    kept();
}

bool AllTagsEditor::undoAll()
{
    if (!m_edition.undoAll())
        return false;
    kept();
    return true;
}

void AllTagsEditor::undo()
{
    if (m_edition.undo())
        kept();
}

void AllTagsEditor::redo()
{
    if (m_edition.redo())
        kept();
}

void AllTagsEditor::setPlacingOf(const QString &name, int placing)
{
    const auto &tags = m_edition.tags();
    const auto it = std::ranges::find_if(tags, [&](const AllTagsSetting &t) { return qs(t.name) == name; });
    if (it == tags.end() || it->mode == placing)
        return;
    const int was = selection();
    m_edition.select(static_cast<int>(it - tags.begin()));
    m_edition.setPlacing(placing);
    m_edition.select(was);
    kept();
}

void AllTagsEditor::close()
{
    if (m_done)
        return;
    m_done = true;
    if (m_hooks.finished)
        m_hooks.finished();
    emit closed();
}

}
