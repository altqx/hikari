#include "hikari/app/application.h"
#include "hikari/application/layer_builders.h"

#include <QCoreApplication>
#include <QJsonDocument>

#include <algorithm>

namespace hikari::app {

namespace {

std::u8string utf8(const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    return {reinterpret_cast<const char8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size())};
}

std::optional<core::Colour> colourOf(const QVariant &value)
{
    const QString text = value.toString().trimmed();
    if (text.isEmpty())
        return std::nullopt;
    auto c = core::legacy::colour(utf8(text));
    c.a = 0;
    return c;
}

int alphaOf(const QVariant &value, int fallback)
{
    bool ok = false;
    const int a = value.toString().trimmed().toInt(&ok, 16);
    return ok ? std::clamp(a, 0, 255) : fallback;
}

std::optional<double> numberOf(const QVariant &value)
{
    bool ok = false;
    const double v = value.toString().trimmed().toDouble(&ok);
    return ok ? std::optional(v) : std::nullopt;
}

application::LayerOptions layerOptions(const QVariantMap &o)
{
    application::LayerOptions out;
    const auto flag = [&](const char *name, bool fallback) {
        const QVariant v = o.value(QLatin1String(name));
        return v.isValid() ? v.toBool() : fallback;
    };
    const auto real = [&](const char *name, double fallback) { return numberOf(o.value(QLatin1String(name))).value_or(fallback); };
    out.glow = flag("glow", out.glow);
    if (o.contains(QStringLiteral("glows"))) {
        out.glows.clear();
        for (const QVariant &row : o.value(QStringLiteral("glows")).toList()) {
            const QVariantMap g = row.toMap();
            application::GlowLayer layer;
            layer.blur = numberOf(g.value(QStringLiteral("blur"))).value_or(layer.blur);
            layer.border = numberOf(g.value(QStringLiteral("border"))).value_or(layer.border);
            layer.colour = colourOf(g.value(QStringLiteral("colour")));
            layer.alpha = alphaOf(g.value(QStringLiteral("alpha")), layer.alpha);
            out.glows.push_back(layer);
        }
    }
    out.border = flag("border", out.border);
    out.under = o.value(QStringLiteral("under")).toString() == QLatin1String("transparent")
                    ? application::UnderBorder::Transparent
                    : application::UnderBorder::BorderColour;
    out.borderBlur = numberOf(o.value(QStringLiteral("borderBlur")));
    out.fadeFix = flag("fadeFix", out.fadeFix);
    out.fadeMs = static_cast<int>(real("fadeMs", out.fadeMs));
    out.secondBorder = flag("secondBorder", out.secondBorder);
    out.secondWidth = real("secondWidth", out.secondWidth);
    out.secondColour = colourOf(o.value(QStringLiteral("secondColour")));
    out.shadow = flag("shadow", out.shadow);
    if (const auto steps = numberOf(o.value(QStringLiteral("steps"))))
        out.steps = static_cast<int>(*steps);
    out.nearColour = colourOf(o.value(QStringLiteral("nearColour")));
    out.farColour = colourOf(o.value(QStringLiteral("farColour")));
    out.nearAlpha = alphaOf(o.value(QStringLiteral("nearAlpha")), out.nearAlpha);
    out.farAlpha = alphaOf(o.value(QStringLiteral("farAlpha")), out.farAlpha);
    out.defaultBlur = real("defaultBlur", out.defaultBlur);
    return out;
}

QString problemText(application::LayerProblem problem)
{
    using enum application::LayerProblem;
    switch (problem) {
    case NothingOn: return QCoreApplication::translate("HikariSub.Layers", "Turn on glow, border or shadow.");
    case NoLines: return QCoreApplication::translate("HikariSub.Layers", "Select the lines to build layers under.");
    case TooManyGlows: return QCoreApplication::translate("HikariSub.Layers", "At most %n glow layer(s) can be built at once.", nullptr, application::kMaxGlows);
    case TooManySteps:
        return QCoreApplication::translate("HikariSub.Layers", "A shadow can have at most %n step(s). Set fewer steps.", nullptr, application::kMaxShadowSteps);
    case TooManyLines:
        return QCoreApplication::translate("HikariSub.Layers", "That builds more than %n line(s). Select fewer lines.", nullptr, application::kMaxBuiltLines);
    }
    return {};
}

QVariantMap status(const char *state, const QString &text, bool canApply = false)
{
    return {{QStringLiteral("state"), QString::fromLatin1(state)},
            {QStringLiteral("text"), text},
            {QStringLiteral("canApply"), canApply}};
}

QString planText(const application::LayerPlan &plan)
{
    QString text;
    if (plan.sources.empty()) {
        text = QCoreApplication::translate("HikariSub.Layers", "Nothing to build for the selected lines.");
    } else if (plan.sources.size() == 1) {
        const auto &s = plan.sources.front();
        text = QCoreApplication::translate("HikariSub.Layers", "Builds %n line(s) under line %1.", nullptr, plan.built).arg(s.row) + QLatin1Char(' ') +
               QCoreApplication::translate("HikariSub.Layers", "It moves up from layer %1 to layer %2.").arg(s.layer).arg(s.newLayer);
    } else {
        text = QCoreApplication::translate("HikariSub.Layers", "Builds %n line(s) under the selected lines.", nullptr, plan.built) + QLatin1Char(' ') +
               QCoreApplication::translate("HikariSub.Layers", "Each moves up by the lines built under it.");
    }
    if (plan.skippedBorder > 0)
        text += QLatin1Char(' ') + QCoreApplication::translate("HikariSub.Layers", "%n line(s) have no border and skip Border.", nullptr, plan.skippedBorder);
    if (plan.skippedShadow > 0)
        text += QLatin1Char(' ') + QCoreApplication::translate("HikariSub.Layers", "%n line(s) have a shadow under 1 px and skip Shadow.", nullptr, plan.skippedShadow);
    return text;
}

}

QVariantMap Application::checkLayerBuilders(const QVariantMap &options) const
{
    auto *session = targetSession();
    if (!session)
        return {};
    const auto plan = application::planLayers(session->document(), session->selection().selected,
                                              layerOptions(options), shownLines());
    if (!plan)
        return status("error", problemText(plan.error()));
    if (plan->sources.empty())
        return status("error", planText(*plan));
    if (!plan->covered.empty()) {
        const auto &c = plan->covered.front();
        return status("warning",
                      QCoreApplication::translate("HikariSub.Layers", "Line %1 (layer %2) is shown with line %3 and was above it. Once built, line %3’s stack covers it.")
                          .arg(c.row)
                          .arg(c.layer)
                          .arg(c.sourceRow),
                      true);
    }
    return status("ok", planText(*plan), true);
}

std::optional<core::Document> Application::layerPreviewDocument(const core::Document &doc,
                                                                const std::set<core::LineId> &selection) const
{
    const auto plan = application::planLayers(doc, selection, layerOptions(*m_layerPreview), shownLines());
    if (!plan || plan->sources.empty())
        return std::nullopt;
    core::Document preview = doc;
    if (!application::applyLayerPlan(preview, *plan))
        return std::nullopt;
    return preview;
}

void Application::setLayerPreview(const QVariantMap &options)
{
    m_layerPreview = options;
    showVideoSubtitles();
}

void Application::clearLayerPreview()
{
    if (!std::exchange(m_layerPreview, std::nullopt))
        return;
    showVideoSubtitles();
}

QString Application::applyLayerBuilders(const QVariantMap &options)
{
    const auto o = layerOptions(options);
    QString failure;
    const bool held = m_layerPreview.has_value();
    m_layerPreview.reset();
    const bool done = runFilter([&](application::EditSession &s) -> std::expected<void, application::CommandRefusal> {
        const auto plan = application::planLayers(s.document(), s.selection().selected, o, shownLines());
        if (!plan) {
            failure = problemText(plan.error());
            return std::unexpected(application::CommandRefusal::Invalid);
        }
        if (plan->sources.empty()) {
            failure = planText(*plan);
            return std::unexpected(application::CommandRefusal::Invalid);
        }
        return application::buildLayerStacks(s, o, shownLines());
    });
    if (!done && held)
        m_layerPreview = options;
    showVideoSubtitles();
    if (done)
        return {};
    return failure.isEmpty() ? QCoreApplication::translate("HikariSub.Layers", "The layers can’t be built here.") : failure;
}

QVariantMap Application::repeatLayerBuilders()
{
    const auto toast = [](bool ok, const QString &text) {
        return QVariantMap{{QStringLiteral("ok"), ok}, {QStringLiteral("text"), text}};
    };
    const auto saved = QJsonDocument::fromJson(QByteArray::fromStdString(m_settings->settings().text("typesetting.layerOptions")));
    if (!saved.isObject())
        return toast(false, QCoreApplication::translate("HikariSub.Layers", "Build layers once from Blur and glow layers… to choose the values."));
    auto *session = targetSession();
    const std::size_t before = session ? session->document().lines().size() : 0;
    const QString failure = applyLayerBuilders(saved.toVariant().toMap());
    if (!failure.isEmpty())
        return toast(false, failure);
    session = targetSession();
    const int built = session ? static_cast<int>(session->document().lines().size() - before) : 0;
    return toast(true, QCoreApplication::translate("HikariSub.Layers", "Built %n layer line(s)", nullptr, built));
}

}
