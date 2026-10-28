#include "all_tags_editor.h"

namespace hikari::ui {

using application::visual::AllTagsEdition;
using application::visual::AllTagsSetting;
using application::visual::NumberField;

namespace {

QString qs(std::u16string_view text)
{
    return QString::fromUtf16(text.data(), static_cast<qsizetype>(text.size()));
}

QVariantMap message(const std::optional<AllTagsEdition::Message> &m)
{
    if (!m)
        return {};
    return {{QStringLiteral("text"), qs(m->text)}, {QStringLiteral("title"), qs(m->title)}};
}

template <std::size_t N>
QStringList listOf(const std::array<std::u16string_view, N> &items)
{
    QStringList out;
    for (const auto item : items)
        out.append(qs(item));
    return out;
}

} // namespace

AllTagsEditor::AllTagsEditor(std::vector<AllTagsSetting> tags, int curTag, Hooks hooks, QObject *parent)
    : QObject(parent), m_edition(std::move(tags), curTag), m_hooks(std::move(hooks))
{
}

QStringList AllTagsEditor::list() const
{
    QStringList out;
    for (const auto &n : m_edition.list())
        out.append(qs(n));
    return out;
}

QString AllTagsEditor::name() const
{
    return qs(m_edition.name);
}

QString AllTagsEditor::tag() const
{
    return qs(m_edition.tag);
}

QVariantMap AllTagsEditor::numbers() const
{
    return {{QStringLiteral("min"), qs(m_edition.minValue.text())},
            {QStringLiteral("max"), qs(m_edition.maxValue.text())},
            {QStringLiteral("value"), qs(m_edition.values[0].text())},
            {QStringLiteral("step"), qs(m_edition.step.text())},
            {QStringLiteral("digits"), qs(m_edition.digitsAfterDot.text())},
            {QStringLiteral("value2"), qs(m_edition.values[1].text())},
            {QStringLiteral("value3"), qs(m_edition.values[2].text())},
            {QStringLiteral("value4"), qs(m_edition.values[3].text())}};
}

QStringList AllTagsEditor::placings() const
{
    return listOf(AllTagsEdition::placings());
}

QStringList AllTagsEditor::valueCounts() const
{
    return listOf(AllTagsEdition::valueCounts());
}

QStringList AllTagsEditor::changeOptions() const
{
    return listOf(application::visual::allTagsChangeOptions());
}

void AllTagsEditor::setName(const QString &name)
{
    if (name == this->name())
        return;
    m_edition.name = name.toStdU16String();
    emit changed();
}

void AllTagsEditor::setTag(const QString &tag)
{
    if (tag == this->tag())
        return;
    m_edition.tag = tag.toStdU16String();
    emit changed();
}

void AllTagsEditor::setPlacing(int placing)
{
    if (placing == m_edition.placing)
        return;
    m_edition.placing = placing;
    emit changed();
}

void AllTagsEditor::setAdditionalValues(int count)
{
    // The count's choice enables the values it takes (VisualAllTagsEdition.
    // cpp:236-241).
    if (count == m_edition.additionalValues)
        return;
    m_edition.additionalValues = count;
    emit changed();
}

void AllTagsEditor::setChangeOption(int option)
{
    if (option == m_edition.changeOption)
        return;
    m_edition.changeOption = option;
    emit changed();
}

NumberField *AllTagsEditor::field(const QString &name)
{
    if (name == QLatin1String("min"))
        return &m_edition.minValue;
    if (name == QLatin1String("max"))
        return &m_edition.maxValue;
    if (name == QLatin1String("value"))
        return &m_edition.values[0];
    if (name == QLatin1String("step"))
        return &m_edition.step;
    if (name == QLatin1String("digits"))
        return &m_edition.digitsAfterDot;
    if (name == QLatin1String("value2"))
        return &m_edition.values[1];
    if (name == QLatin1String("value3"))
        return &m_edition.values[2];
    if (name == QLatin1String("value4"))
        return &m_edition.values[3];
    return nullptr;
}

void AllTagsEditor::setNumber(const QString &name, const QString &text)
{
    if (auto *f = field(name)) {
        f->setText(text.toStdU16String());
        emit changed();
    }
}

QVariantMap AllTagsEditor::addTag(const QString &newName)
{
    m_edition.newTagName = newName.toStdU16String();
    const auto m = m_edition.addTag();
    emit changed();
    return message(m);
}

QVariantMap AllTagsEditor::removeTag()
{
    const auto m = m_edition.removeTag();
    emit changed();
    return message(m);
}

QVariantMap AllTagsEditor::listChangeQuestion() const
{
    if (!m_edition.modified())
        return {};
    return message(m_edition.saveChangesQuestion());
}

void AllTagsEditor::select(int index)
{
    m_edition.select(index);
    emit changed();
}

QVariantMap AllTagsEditor::save(bool ok)
{
    const auto m = m_edition.save();
    emit changed();
    if (m)
        return message(m);
    if (ok)
        finish(true); // SaveSettings and EndModal(wxID_OK)
    return {};
}

QVariantMap AllTagsEditor::restoreQuestion() const
{
    return message(AllTagsEdition::restoreQuestion());
}

void AllTagsEditor::restoreDefaults()
{
    // OnResetDefault after Yes: the file goes at once (Cancel does not bring
    // it back), then LoadSettings gives legacy's defaults.
    if (m_hooks.removeFile)
        m_hooks.removeFile();
    m_edition.restoreDefaults();
    emit changed();
}

void AllTagsEditor::cancel()
{
    finish(false);
}

void AllTagsEditor::finish(bool ok)
{
    if (m_done)
        return;
    m_done = true;
    if (m_hooks.finished)
        m_hooks.finished(ok ? std::optional(m_edition.tags()) : std::nullopt);
    emit closed();
}

} // namespace hikari::ui
