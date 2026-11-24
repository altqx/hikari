#include "hikari/app/application.h"

#include "hikari/backends/text_encoding.h"

#include <QCoreApplication>
#include <QVariantMap>

namespace hikari::app {

namespace {

QVariantMap encodingRow(std::string_view name)
{
    return {{QStringLiteral("name"), QString::fromUtf8(name.data(), qsizetype(name.size()))},
            {QStringLiteral("label"), QString::fromStdString(backends::encodingLabel(name))}};
}

}

QString Application::encodingSuggestion() const
{
    const auto target = m_workspace.editingTarget();
    if (!target)
        return {};
    auto [it, fresh] = m_encodingSuggestions.try_emplace(target->value);
    if (fresh) {
        const auto *session = m_files->session(*target);
        const auto destination = m_files->destination(*target);
        if (session && destination && !destination->value.empty())
            it->second = backends::suggestedEncoding(session->document().source().bytes);
    }
    return it->second ? QString::fromStdString(backends::encodingLabel(*it->second)) : QString();
}

bool Application::canReopenWithEncoding() const
{
    const auto target = m_workspace.editingTarget();
    const auto destination = target ? m_files->destination(*target) : std::nullopt;
    return destination && !destination->value.empty();
}

void Application::dismissEncodingSuggestion()
{
    if (const auto target = m_workspace.editingTarget()) {
        m_encodingSuggestions[target->value].reset();
        emit encodingSuggestionChanged();
    }
}

QVariantList Application::reopenEncodings() const
{
    QVariantList rows;
    std::string suggested;
    if (const auto target = m_workspace.editingTarget(); target && !encodingSuggestion().isEmpty())
        suggested = *m_encodingSuggestions.at(target->value);
    if (!suggested.empty())
        rows << encodingRow(suggested);
    for (const auto &encoding : backends::reopenEncodings())
        if (encoding.name != suggested)
            rows << encodingRow(encoding.name);
    return rows;
}

bool Application::reopenWithEncoding(const QString &name)
{
    const std::string encoding = name.toStdString();
    return reloadTarget([&](std::span<const std::byte> bytes) { return backends::utf8From(bytes, encoding); },
                        QCoreApplication::translate("HikariSub.Documents", "Reopen with encoding"));
}

}
