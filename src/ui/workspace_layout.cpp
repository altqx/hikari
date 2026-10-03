#include "workspace_layout.h"

#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/qtquick/DockWidgetInstantiator.h>
#include <kddockwidgets/kddockwidgets_version.h>

#include <QFile>
#include <QGuiApplication>
#include <QWindow>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace hikari::ui {

namespace {

QString engineVersion()
{
    return QStringLiteral("KDDockWidgets %1").arg(QStringLiteral(KDDOCKWIDGETS_VERSION_STRING));
}

std::optional<QByteArray> readFile(const QString &path)
{
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return f.read(WorkspaceLayoutController::kMaximumBytes + 1);
}

bool writeAtomically(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(bytes) != bytes.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

} // namespace

const QStringList &WorkspaceLayoutController::panelIds()
{
    static const QStringList ids{QStringLiteral("Video"), QStringLiteral("Audio"), QStringLiteral("Editor"),
                                 QStringLiteral("Grid"), QStringLiteral("Reference")};
    return ids;
}

WorkspaceLayoutController::WorkspaceLayoutController(QString layoutFile, QObject *parent)
    : QObject(parent), m_file(std::move(layoutFile))
{
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        connect(app, &QGuiApplication::focusWindowChanged, this, &WorkspaceLayoutController::focusWindowChanged);
}

QObject *WorkspaceLayoutController::focusWindow() const
{
    return QGuiApplication::focusWindow();
}

QByteArray WorkspaceLayoutController::envelope(const QByteArray &payload, const QString &preset)
{
    QJsonObject o;
    o.insert(QStringLiteral("schema"), kSchema);
    o.insert(QStringLiteral("panelRegistry"), kPanelRegistry);
    o.insert(QStringLiteral("engine"), engineVersion());
    o.insert(QStringLiteral("preset"), preset);
    o.insert(QStringLiteral("panels"), QJsonArray::fromStringList(panelIds()));
    o.insert(QStringLiteral("payload"), QJsonDocument::fromJson(payload).object());
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

const QStringList &WorkspaceLayoutController::presets()
{
    static const QStringList names{QStringLiteral("Editing"), QStringLiteral("Timing"), QStringLiteral("Translation"),
                                   QStringLiteral("Typesetting")};
    return names;
}

void WorkspaceLayoutController::setPreset(const QString &preset)
{
    if (!presets().contains(preset) || preset == m_preset)
        return;
    m_preset = preset;
    m_lastSaved.clear(); // the envelope changes even when the arrangement does not
    emit changed();
}

std::optional<QByteArray> WorkspaceLayoutController::payloadOf(const QByteArray &file, QString *problem, QString *preset)
{
    auto fail = [&](const QString &why) {
        if (problem)
            *problem = why;
        return std::nullopt;
    };
    if (file.size() > kMaximumBytes)
        return fail(tr("the file is larger than %1 bytes").arg(kMaximumBytes));
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return fail(tr("it is not a layout file"));
    const QJsonObject o = doc.object();
    const int schema = o.value(QStringLiteral("schema")).toInt(-1);
    if (schema != kSchema)
        return fail(schema > kSchema ? tr("it was written by a newer version") : tr("its format is not known"));
    if (o.value(QStringLiteral("panelRegistry")).toInt(-1) > kPanelRegistry)
        return fail(tr("it names panels from a newer version"));
    if (o.value(QStringLiteral("engine")).toString() != engineVersion())
        return fail(tr("it was made with %1").arg(o.value(QStringLiteral("engine")).toString()));
    for (const auto &panel : o.value(QStringLiteral("panels")).toArray())
        if (!panelIds().contains(panel.toString()))
            return fail(tr("it names an unknown panel (%1)").arg(panel.toString()));
    const QJsonObject payload = o.value(QStringLiteral("payload")).toObject();
    if (payload.isEmpty())
        return fail(tr("it holds no layout"));
    // IDs before the engine sees them: the Classic main window, known panels only.
    bool classic = false;
    for (const auto &w : payload.value(QStringLiteral("mainWindows")).toArray())
        classic = classic || w.toObject().value(QStringLiteral("uniqueName")).toString() == QStringLiteral("Classic");
    if (!classic)
        return fail(tr("it holds no Classic arrangement"));
    for (const auto &d : payload.value(QStringLiteral("allDockWidgets")).toArray()) {
        const QString name = d.toObject().value(QStringLiteral("uniqueName")).toString();
        if (!panelIds().contains(name))
            return fail(tr("it names an unknown panel (%1)").arg(name));
    }
    if (preset) {
        const QString named = o.value(QStringLiteral("preset")).toString();
        *preset = presets().contains(named) ? named : QStringLiteral("Editing");
    }
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

void WorkspaceLayoutController::captureDefault()
{
    m_default = KDDockWidgets::LayoutSaver().serializeLayout();
}

bool WorkspaceLayoutController::restorePayload(const QByteArray &payload)
{
    m_restoring = true; // no autosave while restoring
    const bool restored = KDDockWidgets::LayoutSaver().restoreLayout(payload);
    if (!restored && !m_default.isEmpty())
        KDDockWidgets::LayoutSaver().restoreLayout(m_default); // rebuild a known arrangement
    m_restoring = false;
    return restored;
}

bool WorkspaceLayoutController::restoreSaved()
{
    if (m_file.isEmpty())
        return false;
    const auto bytes = readFile(m_file);
    if (!bytes)
        return false;
    QString problem, preset;
    const auto payload = payloadOf(*bytes, &problem, &preset);
    if (!payload || !restorePayload(*payload)) {
        if (problem.isEmpty())
            problem = tr("the docking engine could not restore it");
        // A copy is kept for diagnosis before anything can overwrite the
        // file; saving resumes once the user changes or resets the layout.
        const QString kept = m_file + QStringLiteral(".unrestored");
        writeAtomically(kept, *bytes);
        m_lastSaved = KDDockWidgets::LayoutSaver().serializeLayout();
        m_notice = tr("The saved panel layout was not restored because %1. The Editing layout is shown; "
                      "the saved file is kept at %2.")
                       .arg(problem, QDir::toNativeSeparators(kept));
        emit changed();
        return false;
    }
    m_lastSaved = KDDockWidgets::LayoutSaver().serializeLayout();
    if (preset != m_preset) {
        m_preset = preset;
        emit changed();
    }
    return true;
}

bool WorkspaceLayoutController::save()
{
    if (m_file.isEmpty() || m_restoring)
        return false;
    const QByteArray current = KDDockWidgets::LayoutSaver().serializeLayout();
    if (current == m_lastSaved)
        return false;
    // The previous valid file becomes the backup.
    if (const auto old = readFile(m_file); old && payloadOf(*old, nullptr))
        writeAtomically(backupFile(), *old);
    if (!writeAtomically(m_file, envelope(current, m_preset)))
        return false;
    m_lastSaved = current;
    emit changed();
    return true;
}

bool WorkspaceLayoutController::resetLayout()
{
    if (m_default.isEmpty())
        return false;
    const bool done = restorePayload(m_default);
    dismissNotice();
    save();
    return done;
}

bool WorkspaceLayoutController::hasBackup() const
{
    return !m_file.isEmpty() && QFileInfo::exists(backupFile());
}

bool WorkspaceLayoutController::restoreBackup()
{
    const auto bytes = readFile(backupFile());
    const auto payload = bytes ? payloadOf(*bytes, nullptr) : std::nullopt;
    if (!payload || !restorePayload(*payload))
        return false;
    dismissNotice();
    save();
    return true;
}

void WorkspaceLayoutController::dismissNotice()
{
    if (m_notice.isEmpty())
        return;
    m_notice.clear();
    emit changed();
}

namespace {

KDDockWidgets::Core::DockWidget *controllerOf(QObject *dock)
{
    auto *instantiator = qobject_cast<KDDockWidgets::DockWidgetInstantiator *>(dock);
    return instantiator ? instantiator->controller() : nullptr;
}

} // namespace

QSize WorkspaceLayoutController::panelSize(QObject *dock) const
{
    auto *controller = controllerOf(dock);
    if (!controller || !controller->isOpen() || controller->isFloating())
        return {};
    const auto size = controller->sizeInLayout();
    return {size.width(), size.height()};
}

bool WorkspaceLayoutController::resizePanel(QObject *dock, int width, int height)
{
    auto *controller = controllerOf(dock);
    if (!controller || !controller->isOpen() || controller->isFloating() || width <= 0 || height <= 0)
        return false;
    const auto size = controller->sizeInLayout();
    controller->resizeInLayout(0, 0, width - size.width(), height - size.height());
    return true;
}

} // namespace hikari::ui
