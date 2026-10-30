#include "workspace_layout.h"

#include "docking.h"

#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/core/Platform.h>
#include <kddockwidgets/qtquick/DockWidgetInstantiator.h>
#include <kddockwidgets/kddockwidgets_version.h>

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QQuickItem>
#include <QWindow>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSet>
#include <QScreen>
#include <QTimer>

#include <cmath>
#include <utility>

namespace hikari::ui {

namespace {

QString engineVersion()
{
    return QStringLiteral("KDDockWidgets %1").arg(QStringLiteral(KDDOCKWIDGETS_VERSION_STRING));
}

bool restorableEngine(const QString &engine)
{
    static const QStringList migrated{QStringLiteral("KDDockWidgets 2.4.1")};
    return engine == engineVersion() || migrated.contains(engine);
}

std::optional<QByteArray> readFile(const QString &path, bool *unreadable = nullptr)
{
    QFile f(path);
    if (!f.exists())
        return std::nullopt;
    if (!f.open(QIODevice::ReadOnly)) {
        if (unreadable)
            *unreadable = true;
        return std::nullopt;
    }
    return f.read(WorkspaceLayoutController::kMaximumBytes + 1);
}

bool keepAside(const QString &path, const QString &kept)
{
    QFile from(path);
    if (!from.open(QIODevice::ReadOnly))
        return false;
    QSaveFile to(kept);
    if (!to.open(QIODevice::WriteOnly))
        return false;
    while (!from.atEnd()) {
        const QByteArray chunk = from.read(1 << 16);
        if (chunk.isEmpty() || to.write(chunk) != chunk.size()) {
            to.cancelWriting();
            return false;
        }
    }
    return to.commit();
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

// QQuickMenu is a private type: matched by class name.
bool isInMenu(QObject *focus)
{
    for (auto *item = qobject_cast<QQuickItem *>(focus); item; item = item->parentItem())
        if (item->inherits("QQuickMenuItem") || (item->parent() && item->parent()->inherits("QQuickMenu")))
            return true;
    return false;
}

// QQuickOverlay is a private type (matched by class name), under the content item or beside it.
QQuickItem *overlayOf(QQuickWindow *window)
{
    for (QQuickItem *layer : {window->contentItem(), window->contentItem()->parentItem()})
        if (layer)
            for (QQuickItem *item : layer->childItems())
                if (item->inherits("QQuickOverlay"))
                    return item;
    return nullptr;
}

// A popup's item is in the overlay from its opening to the end of its closing, before it shows.
bool showsModal(QQuickItem *overlay)
{
    for (QQuickItem *item : overlay->childItems())
        if (item->inherits("QQuickPopupItem") && item->parent() &&
            item->parent()->property("modal").toBool())
            return true;
    return false;
}

}

const QStringList &WorkspaceLayoutController::panelIds()
{
    static const QStringList ids{QStringLiteral("Video"), QStringLiteral("Audio"), QStringLiteral("Editor"),
                                 QStringLiteral("Grid"), QStringLiteral("Reference"), QStringLiteral("Timing"),
                                 QStringLiteral("Search"), QStringLiteral("Automation"), QStringLiteral("Original"),
                                 QStringLiteral("Log"), QStringLiteral("Styles"),
                                 QStringLiteral("FixMinorErrors"), QStringLiteral("Template"),
                                 QStringLiteral("Spellchecker")};
    return ids;
}

WorkspaceLayoutController::WorkspaceLayoutController(QString layoutFile, QString savedLayoutsFile, QObject *parent)
    : QObject(parent), m_savedFile(std::move(savedLayoutsFile)), m_file(std::move(layoutFile))
{
    if (!m_file.isEmpty() && m_savedFile.isEmpty())
        m_savedFile = QFileInfo(m_file).absolutePath() + QStringLiteral("/layouts.json");
    readSavedLayouts();
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::focusWindowChanged, this, &WorkspaceLayoutController::focusWindowChanged);
        connect(app, &QGuiApplication::focusWindowChanged, this, &WorkspaceLayoutController::checkModal);
        connect(app, &QGuiApplication::focusObjectChanged, this, [this](QObject *focus) {
            if (const bool inMenu = isInMenu(focus); inMenu != m_menuHasFocus) {
                m_menuHasFocus = inMenu;
                emit menuHasFocusChanged();
            }
            checkModal();
        });
        m_screenCheck.setSingleShot(true);
        m_screenCheck.setInterval(250); // after the platform has moved its windows
        connect(&m_screenCheck, &QTimer::timeout, this, &WorkspaceLayoutController::keepFloatingPanelsOnScreen);
        auto watch = [this](QScreen *screen) {
            connect(screen, &QScreen::availableGeometryChanged, &m_screenCheck, qOverload<>(&QTimer::start));
        };
        for (QScreen *screen : QGuiApplication::screens())
            watch(screen);
        connect(app, &QGuiApplication::screenAdded, this, watch);
        connect(app, &QGuiApplication::screenRemoved, &m_screenCheck, qOverload<>(&QTimer::start));
    }
}

QObject *WorkspaceLayoutController::focusWindow() const
{
    return QGuiApplication::focusWindow();
}

void WorkspaceLayoutController::checkModal()
{
    bool open = false;
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        auto *quick = qobject_cast<QQuickWindow *>(window);
        if (!quick || !quick->isVisible())
            continue;
        if (quick->modality() != Qt::NonModal) {
            open = true;
            continue;
        }
        QQuickItem *overlay = overlayOf(quick);
        if (!overlay)
            continue;
        connect(overlay, &QQuickItem::childrenChanged, this, &WorkspaceLayoutController::checkModal,
                Qt::UniqueConnection);
        open = open || showsModal(overlay);
    }
    if (open != m_modalOpen) {
        m_modalOpen = open;
        emit modalOpenChanged();
    }
}

namespace {

QJsonObject envelopeObject(const QByteArray &payload, const QString &preset, const QString &layout)
{
    QJsonObject o;
    o.insert(QStringLiteral("schema"), WorkspaceLayoutController::kSchema);
    o.insert(QStringLiteral("panelRegistry"), WorkspaceLayoutController::kPanelRegistry);
    o.insert(QStringLiteral("engine"), engineVersion());
    o.insert(QStringLiteral("preset"), preset);
    if (!layout.isEmpty())
        o.insert(QStringLiteral("layout"), layout);
    o.insert(QStringLiteral("panels"), QJsonArray::fromStringList(WorkspaceLayoutController::panelIds()));
    if (!payload.isEmpty())
        o.insert(QStringLiteral("payload"), QJsonDocument::fromJson(payload).object());
    return o;
}

}

QByteArray WorkspaceLayoutController::envelope(const QByteArray &payload, const QString &preset, const QString &layout)
{
    return QJsonDocument(envelopeObject(payload, preset, layout)).toJson(QJsonDocument::Indented);
}

QByteArray WorkspaceArrangement::serialise() const
{
    return QJsonDocument(envelopeObject(payload, preset, layout)).toJson(QJsonDocument::Compact);
}

std::optional<WorkspaceArrangement> WorkspaceArrangement::parse(const QByteArray &bytes, QString *problem)
{
    const QJsonObject o = bytes.size() > WorkspaceLayoutController::kMaximumBytes ? QJsonObject()
                                                                                  : QJsonDocument::fromJson(bytes).object();
    if (!o.isEmpty() && !o.contains(QStringLiteral("payload"))) {
        if (o.value(QStringLiteral("schema")).toInt(-1) == WorkspaceLayoutController::kSchema &&
            WorkspaceLayoutController::presets().contains(o.value(QStringLiteral("preset")).toString()))
            return WorkspaceArrangement{o.value(QStringLiteral("preset")).toString(), {},
                                        o.value(QStringLiteral("layout")).toString()};
    }
    WorkspaceArrangement arrangement;
    const auto payload = WorkspaceLayoutController::payloadOf(bytes, problem, &arrangement.preset, &arrangement.layout);
    if (!payload)
        return std::nullopt;
    arrangement.payload = *payload;
    return arrangement;
}

namespace {

QPoint rectTopLeft(const QJsonObject &holder, const QString &key)
{
    const QJsonObject rect = holder.value(key).toObject();
    return {rect.value(QStringLiteral("x")).toInt(), rect.value(QStringLiteral("y")).toInt()};
}

void moveRect(QJsonObject &holder, const QString &key, QPoint by)
{
    QJsonObject rect = holder.value(key).toObject();
    if (rect.value(QStringLiteral("width")).toInt() <= 0 || rect.value(QStringLiteral("height")).toInt() <= 0)
        return;
    rect.insert(QStringLiteral("x"), rect.value(QStringLiteral("x")).toInt() + by.x());
    rect.insert(QStringLiteral("y"), rect.value(QStringLiteral("y")).toInt() + by.y());
    holder.insert(key, rect);
}

QPoint mainWindowPosition(const QJsonObject &payload)
{
    for (const auto &w : payload.value(QStringLiteral("mainWindows")).toArray()) {
        const QJsonObject window = w.toObject();
        if (window.value(QStringLiteral("uniqueName")).toString() == QStringLiteral("Classic")) {
            const QJsonObject geometry = window.value(QStringLiteral("geometry")).toObject();
            return {geometry.value(QStringLiteral("x")).toInt(), geometry.value(QStringLiteral("y")).toInt()};
        }
    }
    return {};
}

QByteArray moveWindows(const QByteArray &payload, QPoint by, bool forFile)
{
    QJsonObject o = QJsonDocument::fromJson(payload).object();
    const QString screen = QStringLiteral("screenIndex"), screenSize = QStringLiteral("screenSize");
    auto windows = [&](const QString &key, bool main) {
        QJsonArray list = o.value(key).toArray();
        for (qsizetype i = 0; i < list.size(); ++i) {
            QJsonObject window = list.at(i).toObject();
            moveRect(window, QStringLiteral("geometry"), by);
            const QString normal = QStringLiteral("normalGeometry");
            moveRect(window, normal, forFile && main ? -rectTopLeft(window, normal) : by);
            if (forFile) {
                window.remove(screen);
                window.remove(screenSize);
            }
            list.replace(i, window);
        }
        o.insert(key, list);
    };
    windows(QStringLiteral("mainWindows"), true);
    windows(QStringLiteral("floatingWindows"), false);
    QJsonArray docks = o.value(QStringLiteral("allDockWidgets")).toArray();
    for (qsizetype i = 0; i < docks.size(); ++i) {
        QJsonObject dock = docks.at(i).toObject();
        QJsonObject position = dock.value(QStringLiteral("lastPosition")).toObject();
        moveRect(position, QStringLiteral("lastFloatingGeometry"), by);
        dock.insert(QStringLiteral("lastPosition"), position);
        docks.replace(i, dock);
    }
    o.insert(QStringLiteral("allDockWidgets"), docks);
    if (forFile)
        o.remove(QStringLiteral("screenInfo"));
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

}

QByteArray WorkspaceArrangement::serialiseForFile() const
{
    WorkspaceArrangement placed = *this;
    if (!payload.isEmpty())
        placed.payload = moveWindows(payload, -mainWindowPosition(QJsonDocument::fromJson(payload).object()), true);
    return placed.serialise().toBase64();
}

std::optional<WorkspaceArrangement> WorkspaceArrangement::parseFromFile(QByteArrayView text, QPoint origin,
                                                                        QString *problem)
{
    text = text.trimmed();
    if (text.size() > (WorkspaceLayoutController::kMaximumBytes + 2) / 3 * 4) {
        if (problem)
            *problem = QCoreApplication::translate("HikariSub.Workspace", "the file is larger than %1 bytes")
                           .arg(WorkspaceLayoutController::kMaximumBytes);
        return std::nullopt;
    }
    const auto decoded = QByteArray::fromBase64Encoding(text.toByteArray(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty()) {
        if (problem)
            *problem = QCoreApplication::translate("HikariSub.Workspace", "it is not a layout file");
        return std::nullopt;
    }
    auto arrangement = parse(decoded.decoded, problem);
    if (arrangement && !arrangement->payload.isEmpty())
        arrangement->payload = moveWindows(arrangement->payload, origin, false);
    return arrangement;
}

const QStringList &WorkspaceLayoutController::presets()
{
    static const QStringList names{QStringLiteral("Editing"), QStringLiteral("Timing"), QStringLiteral("Translation"),
                                   QStringLiteral("Typesetting"), QStringLiteral("Karaoke")};
    return names;
}

QString WorkspaceLayoutController::presetLabel(const QString &name)
{
    if (name == QLatin1String("Editing"))
        return QCoreApplication::translate("HikariSub.Workspace", "Editing");
    if (name == QLatin1String("Timing"))
        return QCoreApplication::translate("HikariSub.Workspace", "Timing");
    if (name == QLatin1String("Translation"))
        return QCoreApplication::translate("HikariSub.Workspace", "Translation");
    if (name == QLatin1String("Typesetting"))
        return QCoreApplication::translate("HikariSub.Workspace", "Typesetting");
    if (name == QLatin1String("Karaoke"))
        return QCoreApplication::translate("HikariSub.Workspace", "Karaoke");
    return name;
}

QString WorkspaceLayoutController::presetMode(const QString &preset)
{
    static const std::map<QString, QString> modes{{QStringLiteral("Translation"), QStringLiteral("Translation")},
                                                  {QStringLiteral("Karaoke"), QStringLiteral("Karaoke")}};
    const auto mode = modes.find(preset);
    return mode == modes.end() ? QString() : mode->second;
}

void WorkspaceLayoutController::choseMode()
{
    const QString chosen = presetMode(m_preset);
    if (!m_chosenMode.isEmpty() && m_chosenMode != chosen)
        emit presetModeLeft(m_chosenMode);
    m_chosenMode = chosen;
    if (!chosen.isEmpty())
        emit presetModeChosen(chosen);
}

void WorkspaceLayoutController::resumeMode(const QString &mode)
{
    if (m_chosenMode.isEmpty())
        m_chosenMode = mode;
}

void WorkspaceLayoutController::setPreset(const QString &preset)
{
    if (!presets().contains(preset) || (preset == m_preset && m_layout.isEmpty()))
        return;
    m_preset = preset;
    m_layout.clear();
    m_lastSaved.clear();
    emit changed();
}

namespace {

// KDDockWidgets reads the layout tree outside its own catch: a mistyped value throws mid-restore. 16777215 is Qt's largest size.
bool readableNumber(const QJsonValue &v)
{
    return v.isUndefined() || (v.isDouble() && std::abs(v.toDouble()) <= 16777215);
}

bool readableRect(const QJsonValue &v)
{
    if (v.isUndefined())
        return true;
    const QJsonObject o = v.toObject();
    return v.isObject() && readableNumber(o.value(QStringLiteral("x"))) && readableNumber(o.value(QStringLiteral("y"))) &&
           readableNumber(o.value(QStringLiteral("width"))) && readableNumber(o.value(QStringLiteral("height")));
}

bool readableItem(const QJsonObject &item)
{
    auto is = [&](const char *key, bool (QJsonValue::*type)() const) {
        const QJsonValue v = item.value(QLatin1String(key));
        return v.isUndefined() || (v.*type)();
    };
    const QJsonValue orientation = item.value(QStringLiteral("orientation"));
    const QJsonValue sizing = item.value(QStringLiteral("sizingInfo"));
    const QJsonObject s = sizing.toObject();
    return is("children", &QJsonValue::isArray) && is("isContainer", &QJsonValue::isBool) &&
           is("isVisible", &QJsonValue::isBool) && is("guestId", &QJsonValue::isString) &&
           (orientation.isUndefined() || orientation == QJsonValue(Qt::Horizontal) ||
            orientation == QJsonValue(Qt::Vertical)) &&
           (sizing.isUndefined() ||
            (sizing.isObject() && readableRect(s.value(QStringLiteral("geometry"))) &&
             readableRect(s.value(QStringLiteral("minSize"))) && readableRect(s.value(QStringLiteral("maxSizeHint"))) &&
             (s.value(QStringLiteral("percentageWithinParent")).isUndefined() ||
              s.value(QStringLiteral("percentageWithinParent")).isDouble())));
}

bool readableIndex(const QJsonValue &v, double first, double last)
{
    if (v.isUndefined())
        return true;
    const double d = v.toDouble();
    return v.isDouble() && d == std::trunc(d) && d >= first && d <= last;
}

bool noAffinities(const QJsonObject &o)
{
    const QJsonValue affinities = o.value(QStringLiteral("affinities"));
    const QJsonValue name = o.value(QStringLiteral("affinityName"));
    return (affinities.isUndefined() || affinities == QJsonArray()) &&
           (name.isUndefined() || name == QJsonValue(QString()));
}

}

std::optional<QByteArray> WorkspaceLayoutController::payloadOf(const QByteArray &file, QString *problem, QString *preset,
                                                               QString *layout)
{
    auto fail = [&](const QString &why) {
        if (problem)
            *problem = why;
        return std::nullopt;
    };
    if (file.size() > kMaximumBytes)
        return fail(QCoreApplication::translate("HikariSub.Workspace", "the file is larger than %1 bytes").arg(kMaximumBytes));
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QCoreApplication::translate("HikariSub.Workspace", "it is not a layout file"));
    const QJsonObject o = doc.object();
    const int schema = o.value(QStringLiteral("schema")).toInt(-1);
    if (schema != kSchema)
        return fail(schema > kSchema ? QCoreApplication::translate("HikariSub.Workspace", "it was written by a newer version")
                                     : QCoreApplication::translate("HikariSub.Workspace", "its format is not known"));
    if (o.value(QStringLiteral("panelRegistry")).toInt(-1) > kPanelRegistry)
        return fail(QCoreApplication::translate("HikariSub.Workspace", "it names panels from a newer version"));
    if (!restorableEngine(o.value(QStringLiteral("engine")).toString()))
        return fail(QCoreApplication::translate("HikariSub.Workspace", "it was made with %1")
                        .arg(o.value(QStringLiteral("engine")).toString()));
    for (const auto &panel : o.value(QStringLiteral("panels")).toArray())
        if (!panelIds().contains(panel.toString()))
            return fail(QCoreApplication::translate("HikariSub.Workspace", "it names an unknown panel (%1)").arg(panel.toString()));
    const QJsonObject payload = o.value(QStringLiteral("payload")).toObject();
    if (payload.isEmpty())
        return fail(QCoreApplication::translate("HikariSub.Workspace", "it holds no layout"));
    const QJsonArray mainWindows = payload.value(QStringLiteral("mainWindows")).toArray();
    bool classic = false;
    for (const auto &w : mainWindows)
        classic = classic || w.toObject().value(QStringLiteral("uniqueName")).toString() == QStringLiteral("Classic");
    if (!classic)
        return fail(QCoreApplication::translate("HikariSub.Workspace", "it holds no Classic arrangement"));
    // KDDockWidgets makes a window, group or layout item per entry: cap them at what our panels can fill.
    const QJsonArray floatingWindows = payload.value(QStringLiteral("floatingWindows")).toArray();
    const qsizetype panels = panelIds().size();
    const QString tooMany =
        QCoreApplication::translate("HikariSub.Workspace", "it holds more windows or panels than HikariSub has");
    if (mainWindows.size() > 1 || floatingWindows.size() > panels)
        return fail(tooMany);
    qsizetype groups = 0;
    QSet<QString> grouped;
    QJsonArray windows = mainWindows;
    for (const auto &w : floatingWindows)
        windows.append(w);
    const QString unreadable = QCoreApplication::translate("HikariSub.Workspace", "it is not a layout file");
    for (const auto &w : windows) {
        if (!noAffinities(w.toObject()))
            return fail(unreadable);
        if (!readableRect(w.toObject().value(QStringLiteral("geometry"))) ||
            !readableRect(w.toObject().value(QStringLiteral("normalGeometry"))))
            return fail(unreadable);
        const QJsonObject split = w.toObject().value(QStringLiteral("multiSplitterLayout")).toObject();
        const QJsonObject frames = split.value(QStringLiteral("frames")).toObject();
        groups += frames.size();
        for (const auto &frame : frames)
            for (const auto &dock : frame.toObject().value(QStringLiteral("dockWidgets")).toArray()) {
                const QString name = dock.isObject() ? dock.toObject().value(QStringLiteral("uniqueName")).toString()
                                                     : dock.toString();
                if (!panelIds().contains(name))
                    return fail(QCoreApplication::translate("HikariSub.Workspace", "it names an unknown panel (%1)").arg(name));
                if (grouped.contains(name))
                    return fail(tooMany);
                grouped.insert(name);
            }
        // KDDockWidgets finds a group by its id; options bit 2 is the central group, which Classic lacks.
        for (auto frame = frames.begin(); frame != frames.end(); ++frame) {
            const QJsonObject f = frame.value().toObject();
            if (f.value(QStringLiteral("id")) != QJsonValue(frame.key()) || frame.key().isEmpty() ||
                f.value(QStringLiteral("isNull")) != QJsonValue(false) ||
                (f.value(QStringLiteral("options")).toInt() & 2) != 0)
                return fail(unreadable);
        }
        const QJsonValue root = split.value(QStringLiteral("layout"));
        if (!root.isUndefined() && !root.isObject())
            return fail(unreadable);
        qsizetype items = 0;
        QSet<QString> placed;
        QList<QJsonObject> pending{root.toObject()};
        while (!pending.isEmpty()) {
            if (++items > 4 * panels)
                return fail(tooMany);
            const QJsonObject item = pending.takeLast();
            if (!readableItem(item))
                return fail(unreadable);
            const QString guest = item.value(QStringLiteral("guestId")).toString();
            if (!guest.isEmpty() && (!frames.contains(guest) || placed.contains(guest)))
                return fail(unreadable);
            placed.insert(guest);
            for (const auto &child : item.value(QStringLiteral("children")).toArray()) {
                if (!child.isObject())
                    return fail(unreadable);
                pending.append(child.toObject());
            }
        }
        placed.remove(QString());
        if (placed.size() != frames.size())
            return fail(unreadable);
    }
    if (groups > panels)
        return fail(tooMany);
    for (const auto &f : payload.value(QStringLiteral("floatingWindows")).toArray()) {
        const QJsonValue parent = f.toObject().value(QStringLiteral("parentIndex"));
        const QJsonValue parentName = f.toObject().value(QStringLiteral("parentMainWindowName"));
        if ((!parent.isUndefined() && !(parent.isDouble() && (parent.toDouble() == -1 || parent.toDouble() == 0))) ||
            (!parentName.isUndefined() && parentName != QJsonValue(QString()) &&
             parentName != QJsonValue(QStringLiteral("Classic"))))
            return fail(QCoreApplication::translate("HikariSub.Workspace", "it puts a floating panel in a window that does not exist"));
    }
    QSet<QString> docks;
    for (const auto &d : payload.value(QStringLiteral("allDockWidgets")).toArray()) {
        const QString name = d.toObject().value(QStringLiteral("uniqueName")).toString();
        if (!panelIds().contains(name))
            return fail(QCoreApplication::translate("HikariSub.Workspace", "it names an unknown panel (%1)").arg(name));
        if (!noAffinities(d.toObject()))
            return fail(unreadable);
        const QJsonObject position = d.toObject().value(QStringLiteral("lastPosition")).toObject();
        const QJsonArray places = position.value(QStringLiteral("placeholders")).toArray();
        if (docks.contains(name) || places.size() > 1 + floatingWindows.size())
            return fail(tooMany);
        docks.insert(name);
        if (!readableRect(position.value(QStringLiteral("lastFloatingGeometry"))))
            return fail(unreadable);
        // KDDockWidgets inserts at tabIndex unchecked unless it is -1 (Layout::restorePlaceholder).
        if (!readableIndex(position.value(QStringLiteral("tabIndex")), -1, 16777215))
            return fail(unreadable);
        // KDDockWidgets resolves a place's window unchecked (Positions::deserialize); it writes -1 for a window being deleted.
        const QString nowhere =
            QCoreApplication::translate("HikariSub.Workspace", "it puts a panel in a window that does not exist");
        for (const auto &place : places) {
            const QJsonObject p = place.toObject();
            if (!place.isObject())
                return fail(nowhere);
            if (!readableIndex(p.value(QStringLiteral("itemIndex")), -1, 16777215))
                return fail(unreadable);
            const QJsonValue index = p.value(QStringLiteral("indexOfFloatingWindow"));
            if (p.value(QStringLiteral("isFloatingWindow")) == QJsonValue(true)
                    ? index.isUndefined() || !readableIndex(index, -1, double(floatingWindows.size() - 1))
                    : p.value(QStringLiteral("mainWindowUniqueName")).toString() != QStringLiteral("Classic"))
                return fail(nowhere);
        }
    }
    QSet<QString> closed;
    for (const auto &d : payload.value(QStringLiteral("closedDockWidgets")).toArray()) {
        const QString name = d.isObject() ? d.toObject().value(QStringLiteral("uniqueName")).toString() : d.toString();
        if (!panelIds().contains(name))
            return fail(QCoreApplication::translate("HikariSub.Workspace", "it names an unknown panel (%1)").arg(name));
        if (closed.contains(name))
            return fail(tooMany);
        closed.insert(name);
    }
    if (preset) {
        const QString named = o.value(QStringLiteral("preset")).toString();
        *preset = presets().contains(named) ? named : QStringLiteral("Editing");
    }
    if (layout)
        *layout = o.value(QStringLiteral("layout")).toString();
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

void WorkspaceLayoutController::captureDefault()
{
    m_default = KDDockWidgets::LayoutSaver().serializeLayout();
}

void WorkspaceLayoutController::buildsArrangement(const QString &name)
{
    if (presets().contains(name) && !m_builtPresets.contains(name))
        m_builtPresets << name;
}

bool WorkspaceLayoutController::restorePayload(const QByteArray &payload, bool inPlace)
{
    m_restoring = true;
    const KDDockWidgets::RestoreOptions options =
        inPlace ? KDDockWidgets::RestoreOptions(KDDockWidgets::RestoreOption_RelativeToMainWindow |
                                                KDDockWidgets::RestoreOption_AbsoluteFloatingDockWindows)
                : KDDockWidgets::RestoreOptions(KDDockWidgets::RestoreOption_None);
    const bool restored = KDDockWidgets::LayoutSaver(options).restoreLayout(payload);
    if (!restored && !m_default.isEmpty())
        KDDockWidgets::LayoutSaver(options).restoreLayout(m_default);
    m_restoring = false;
    // Floating windows come back short of their layouts on Wayland.
    hikari::ui::fitFloatingLayouts();
    hikari::ui::refreshDockConstraints();
    hikari::ui::keepFloatingPanelsOnScreen();
    return restored;
}

bool WorkspaceLayoutController::restoreSaved()
{
    m_loaded = true;
    const bool restored = restoreSavedFile();
    const QPoint origin = mainWindowPosition(QJsonDocument::fromJson(KDDockWidgets::LayoutSaver().serializeLayout()).object());
    for (auto &[id, t] : m_tabs)
        if (std::exchange(t.unplaced, false) && t.arrangement && !t.arrangement->payload.isEmpty())
            t.arrangement->payload = moveWindows(t.arrangement->payload, origin, false);
    if (m_tab) {
        Tab &shown = m_tabs[*m_tab];
        if (shown.arrangement) {
            showArrangement(*shown.arrangement);
        } else {
            shown.arrangement = shownArrangement();
        }
    }
    for (auto &[id, t] : m_tabs)
        if (!t.arrangement && m_tab != id)
            t.arrangement = shownArrangement();
    return restored;
}

bool WorkspaceLayoutController::restoreSavedFile()
{
    if (m_file.isEmpty())
        return false;
    bool unreadable = false;
    const auto bytes = readFile(m_file, &unreadable);
    if (!bytes && !unreadable)
        return false;
    QString problem, preset, layout;
    const auto payload = bytes ? payloadOf(*bytes, &problem, &preset, &layout) : std::nullopt;
    if (!payload || !restorePayload(*payload)) {
        if (problem.isEmpty())
            problem = QCoreApplication::translate("HikariSub.Workspace", "the docking engine could not restore it");
        const QString kept = m_file + QStringLiteral(".unrestored");
        m_lastSaved = KDDockWidgets::LayoutSaver().serializeLayout();
        if (unreadable || !keepAside(m_file, kept)) {
            m_fileLocked = true;
            addPanelNotice(QCoreApplication::translate("HikariSub.Workspace",
                                                       "The saved panel layout at %1 could not be read or copied: it is not restored and not changed. The Editing layout is shown.")
                               .arg(QDir::toNativeSeparators(m_file)));
        } else {
            addPanelNotice(QCoreApplication::translate("HikariSub.Workspace",
                                                       "The saved panel layout was not restored because %1. The Editing layout is shown; the saved file is kept at %2.")
                               .arg(problem, QDir::toNativeSeparators(kept)));
        }
        emit changed();
        return false;
    }
    m_lastSaved = KDDockWidgets::LayoutSaver().serializeLayout();
    layout = knownLayout(layout);
    if (preset != m_preset || layout != m_layout) {
        m_preset = preset;
        m_layout = layout;
        emit changed();
    }
    return true;
}

bool WorkspaceLayoutController::save()
{
    if (m_file.isEmpty() || m_fileLocked || m_restoring || !m_loaded || m_quitting)
        return false;
    const QByteArray current = KDDockWidgets::LayoutSaver().serializeLayout();
    if (current == m_lastSaved)
        return false;
    if (const auto old = readFile(m_file); old && payloadOf(*old, nullptr))
        writeAtomically(backupFile(), *old);
    const QString failure = QCoreApplication::translate("HikariSub.Workspace", "The panel layout could not be written to %1.")
                                .arg(QDir::toNativeSeparators(m_file));
    if (!writeAtomically(m_file, envelope(current, m_preset, m_layout))) {
        addPanelNotice(failure);
        emit changed();
        return false;
    }
    m_panelNotices.removeAll(failure);
    m_lastSaved = current;
    emit changed();
    return true;
}

bool WorkspaceLayoutController::saveForQuit()
{
    const bool saved = save();
    if (m_loaded)
        m_quitArrangement = shownArrangement();
    m_quitting = true;
    return saved;
}

bool WorkspaceLayoutController::resetLayout()
{
    if (m_default.isEmpty())
        return false;
    const bool done = restorePayload(m_default);
    emit arrangementSwapped();
    if (!m_layout.isEmpty()) {
        m_layout.clear();
        m_lastSaved.clear();
        emit changed();
    }
    dismissPanelNotice();
    save();
    return done;
}

bool WorkspaceLayoutController::buildPreset(const QString &name, bool inPlace)
{
    if (m_default.isEmpty())
        return false;
    if (const auto own = m_presetPayloads.find(name); own != m_presetPayloads.end())
        return restorePayload(own->second, inPlace);
    const bool done = restorePayload(m_default, inPlace);
    if (done && m_builtPresets.contains(name)) {
        emit buildArrangement(name);
        m_presetPayloads[name] = KDDockWidgets::LayoutSaver().serializeLayout();
        return true;
    }
    QString closed;
    if (name == QLatin1String("Timing"))
        closed = QStringLiteral("Video");
    else if (name == QLatin1String("Translation") || name == QLatin1String("Typesetting"))
        closed = QStringLiteral("Audio");
    if (!closed.isEmpty() && KDDockWidgets::Core::Platform::instance())
        if (auto *dock = KDDockWidgets::DockRegistry::self()->dockByName(closed); dock && dock->isOpen())
            dock->close();
    return done;
}

bool WorkspaceLayoutController::applyPreset(const QString &name)
{
    if (!applyPresetArrangement(name))
        return false;
    choseMode();
    return true;
}

bool WorkspaceLayoutController::applyPresetArrangement(const QString &name)
{
    if (!presets().contains(name) || m_default.isEmpty())
        return false;
    const bool done = buildPreset(name, true);
    dismissPanelNotice();
    setPreset(name);
    emit arrangementSwapped();
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
    QString preset, layout;
    const auto payload = bytes ? payloadOf(*bytes, nullptr, &preset, &layout) : std::nullopt;
    if (!payload)
        return false;
    if (!restorePayload(*payload))
        return false;
    emit arrangementSwapped();
    layout = knownLayout(layout);
    if (preset != m_preset || layout != m_layout) {
        m_preset = preset;
        m_layout = layout;
        m_lastSaved.clear();
        emit changed();
    }
    dismissPanelNotice();
    save();
    return true;
}

void WorkspaceLayoutController::dismissNotice()
{
    if (m_panelNotices.isEmpty() && m_savedNotices.isEmpty())
        return;
    m_panelNotices.clear();
    m_savedNotices.clear();
    emit changed();
}

void WorkspaceLayoutController::dismissPanelNotice()
{
    if (m_panelNotices.isEmpty())
        return;
    m_panelNotices.clear();
    emit changed();
}

void WorkspaceLayoutController::addPanelNotice(const QString &text)
{
    if (!m_panelNotices.contains(text))
        m_panelNotices << text;
}

void WorkspaceLayoutController::addSavedNotice(const QString &text)
{
    if (!m_savedNotices.contains(text))
        m_savedNotices << text;
}

namespace {

KDDockWidgets::Core::DockWidget *controllerOf(QObject *dock)
{
    auto *instantiator = qobject_cast<KDDockWidgets::DockWidgetInstantiator *>(dock);
    return instantiator ? instantiator->controller() : nullptr;
}

}

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

WorkspaceArrangement WorkspaceLayoutController::shownArrangement() const
{
    if (m_quitArrangement)
        return *m_quitArrangement;
    return {m_preset, KDDockWidgets::LayoutSaver().serializeLayout(), m_layout};
}

bool WorkspaceLayoutController::showArrangement(const WorkspaceArrangement &arrangement)
{
    bool shown = true;
    if (arrangement.payload.isEmpty()) {
        shown = buildPreset(arrangement.preset, true);
    } else if (arrangement.payload != KDDockWidgets::LayoutSaver().serializeLayout()) {
        shown = restorePayload(arrangement.payload, true);
    }
    if (!shown)
        refuseArrangement(arrangement);
    const QString preset = shown ? arrangement.preset : QStringLiteral("Editing");
    const QString layout = shown ? knownLayout(arrangement.layout) : QString();
    if (preset != m_preset || layout != m_layout) {
        m_preset = preset;
        m_layout = layout;
        m_lastSaved.clear();
    }
    emit changed();
    emit arrangementSwapped();
    return shown;
}

void WorkspaceLayoutController::refuseArrangement(const WorkspaceArrangement &arrangement)
{
    const QByteArray rejected = arrangement.serialise();
    if (m_tab)
        m_tabs[*m_tab].arrangement = WorkspaceArrangement{};
    keepUnrestored(rejected, QCoreApplication::translate("HikariSub.Workspace", "the docking engine could not restore it"), {});
}

void WorkspaceLayoutController::keepUnrestored(const QByteArray &bytes, const QString &problem, const QString &title)
{
    QString notice = title.isEmpty()
                   ? QCoreApplication::translate("HikariSub.Workspace",
                                                 "The panel layout of this tab was not restored because %1. The tab shows the Editing layout.")
                         .arg(problem)
                   : QCoreApplication::translate("HikariSub.Workspace",
                                                 "The panel layout of %1 was not restored because %2. The tab shows the Editing layout.")
                         .arg(title, problem);
    if (!m_file.isEmpty()) {
        const QString kept = m_file + QStringLiteral(".tab.unrestored");
        if (writeAtomically(kept, bytes))
            notice += QLatin1Char(' ') + QCoreApplication::translate("HikariSub.Workspace", "The layout is kept at %1.")
                                             .arg(QDir::toNativeSeparators(kept));
    }
    addPanelNotice(notice);
    emit changed();
}

void WorkspaceLayoutController::showTab(std::uint64_t tab, TabStart start)
{
    if (m_tab == tab)
        return;
    if (m_tab && m_loaded)
        m_tabs[*m_tab].arrangement = shownArrangement();
    m_tab = tab;
    Tab &shown = m_tabs[tab];
    if (!shown.arrangement) {
        if (start == TabStart::Translation) {
            shown.arrangement = WorkspaceArrangement{QStringLiteral("Translation"), {}};
        } else {
            if (m_loaded)
                shown.arrangement = shownArrangement();
            return;
        }
    }
    if (!m_loaded)
        return;
    showArrangement(*shown.arrangement);
}

void WorkspaceLayoutController::addTab(std::uint64_t tab, TabStart start)
{
    if (m_tab == tab)
        return;
    Tab &added = m_tabs[tab];
    if (added.arrangement)
        return;
    if (start == TabStart::Translation)
        added.arrangement = WorkspaceArrangement{QStringLiteral("Translation"), {}};
    else if (m_loaded)
        added.arrangement = shownArrangement();
}

void WorkspaceLayoutController::replaceTab(std::uint64_t tab, std::uint64_t replacement, TabStart start)
{
    Tab moved;
    if (auto node = m_tabs.extract(tab))
        moved = std::move(node.mapped());
    if (m_tab == tab)
        m_tab = replacement;
    if (start == TabStart::Translation)
        moved.arrangement = WorkspaceArrangement{QStringLiteral("Translation"), {}};
    m_tabs[replacement] = moved;
    if (start == TabStart::Translation && m_tab == replacement && m_loaded)
        showArrangement(*moved.arrangement);
}

void WorkspaceLayoutController::forgetTab(std::uint64_t tab)
{
    m_tabs.erase(tab);
    if (m_tab == tab)
        m_tab.reset();
}

void WorkspaceLayoutController::forgetTabs()
{
    m_tabs.clear();
    m_tab.reset();
}

std::optional<WorkspaceArrangement> WorkspaceLayoutController::tabArrangement(std::uint64_t tab) const
{
    if (m_tab == tab && m_loaded)
        return shownArrangement();
    const auto it = m_tabs.find(tab);
    return it == m_tabs.end() ? std::nullopt : it->second.arrangement;
}

void WorkspaceLayoutController::setTabArrangement(std::uint64_t tab, const QByteArray &serialised, const QString &title)
{
    QString problem;
    auto arrangement = WorkspaceArrangement::parse(serialised, &problem);
    if (!arrangement) {
        keepUnrestored(serialised, problem, title);
        arrangement = WorkspaceArrangement{};
    }
    giveTab(tab, *arrangement);
}

void WorkspaceLayoutController::giveTab(std::uint64_t tab, WorkspaceArrangement arrangement)
{
    arrangement.layout = knownLayout(arrangement.layout);
    Tab &t = m_tabs[tab];
    t.arrangement = arrangement;
    t.unplaced = false;
    if (m_tab == tab && m_loaded)
        showArrangement(arrangement);
}

QByteArray WorkspaceLayoutController::fileArrangement(std::uint64_t tab) const
{
    const auto arrangement = tabArrangement(tab);
    return arrangement ? arrangement->serialiseForFile() : QByteArray();
}

bool WorkspaceLayoutController::setFileArrangement(std::uint64_t tab, const QByteArray &text, QString *problem)
{
    QPoint origin;
    if (m_loaded)
        origin = mainWindowPosition(QJsonDocument::fromJson(KDDockWidgets::LayoutSaver().serializeLayout()).object());
    const auto arrangement = WorkspaceArrangement::parseFromFile(text, origin, problem);
    if (!arrangement)
        return false;
    giveTab(tab, *arrangement);
    m_tabs[tab].unplaced = !m_loaded;
    return true;
}

QString WorkspaceLayoutController::knownLayout(const QString &name) const
{
    return savedLayoutIndex(name) >= 0 ? savedLayoutNamed(name) : QString();
}

void WorkspaceLayoutController::reservePresetNames(const QStringList &names)
{
    if (names == m_reservedNames)
        return;
    m_reservedNames = names;
    readSavedLayouts();
    m_layout = knownLayout(m_layout);
    for (auto &[id, tab] : m_tabs)
        if (tab.arrangement)
            tab.arrangement->layout = knownLayout(tab.arrangement->layout);
    emit savedLayoutsChanged();
    emit changed();
}

void WorkspaceLayoutController::readSavedLayouts()
{
    m_saved.clear();
    if (m_savedFile.isEmpty())
        return;
    bool unreadable = false;
    const auto bytes = readFile(m_savedFile, &unreadable);
    if (unreadable) {
        lockSavedLayouts();
        return;
    }
    if (!bytes)
        return;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(*bytes, &error);
    const QJsonObject o = doc.object();
    QString problem;
    if (bytes->size() > kMaximumBytes)
        problem = QCoreApplication::translate("HikariSub.Workspace", "the file is larger than %1 bytes").arg(kMaximumBytes);
    else if (error.error != QJsonParseError::NoError || !doc.isObject())
        problem = QCoreApplication::translate("HikariSub.Workspace", "it is not a layouts file");
    else if (const int schema = o.value(QStringLiteral("schema")).toInt(-1); schema != kSavedLayoutsSchema)
        problem = schema > kSavedLayoutsSchema
                      ? QCoreApplication::translate("HikariSub.Workspace", "it was written by a newer version")
                      : QCoreApplication::translate("HikariSub.Workspace", "its format is not known");
    else if (!o.value(QStringLiteral("layouts")).isArray())
        problem = QCoreApplication::translate("HikariSub.Workspace", "it is not a layouts file");
    if (!problem.isEmpty()) {
        const QString kept = m_savedFile + QStringLiteral(".unrestored");
        if (!keepAside(m_savedFile, kept)) {
            lockSavedLayouts();
            return;
        }
        addSavedNotice(QCoreApplication::translate("HikariSub.Workspace",
                                                   "The saved layouts were not read because %1. The file is kept at %2.")
                           .arg(problem, QDir::toNativeSeparators(kept)));
        return;
    }
    bool skipped = false;
    for (const auto &value : o.value(QStringLiteral("layouts")).toArray()) {
        const QJsonObject record = value.toObject();
        SavedLayout saved;
        saved.name = record.value(QStringLiteral("name")).toString().trimmed();
        saved.record = record;
        if (!value.isObject() || !layoutNameProblem(saved.name).isEmpty() || savedLayoutIndex(saved.name) >= 0) {
            skipped = true;
            continue;
        }
        const QByteArray arrangement =
            QJsonDocument(record.value(QStringLiteral("arrangement")).toObject()).toJson(QJsonDocument::Compact);
        saved.arrangement = WorkspaceArrangement::parse(arrangement, &saved.problem);
        if (saved.arrangement)
            saved.arrangement->layout = saved.name;
        m_saved.push_back(std::move(saved));
    }
    if (skipped) {
        const QString kept = m_savedFile + QStringLiteral(".unrestored");
        if (!keepAside(m_savedFile, kept)) {
            m_saved.clear();
            lockSavedLayouts();
            return;
        }
        addSavedNotice(QCoreApplication::translate("HikariSub.Workspace",
                                                   "Some saved layouts were not read: their names cannot name a layout, or were listed before. The file as it was is kept at %1.")
                           .arg(QDir::toNativeSeparators(kept)));
    }
}

void WorkspaceLayoutController::lockSavedLayouts()
{
    m_savedLocked = true;
    addSavedNotice(QCoreApplication::translate("HikariSub.Workspace",
                                               "The saved layouts at %1 could not be read or copied: they are not used and not changed.")
                       .arg(QDir::toNativeSeparators(m_savedFile)));
}

bool WorkspaceLayoutController::writeSavedLayouts()
{
    if (m_savedFile.isEmpty())
        return true;
    if (m_savedLocked) {
        lockSavedLayouts();
        emit changed();
        return false;
    }
    QJsonArray layouts;
    for (const SavedLayout &saved : m_saved) {
        if (!saved.arrangement) {
            layouts.append(saved.record);
            continue;
        }
        QJsonObject record;
        record.insert(QStringLiteral("name"), saved.name);
        record.insert(QStringLiteral("arrangement"), QJsonDocument::fromJson(saved.arrangement->serialise()).object());
        layouts.append(record);
    }
    QJsonObject o;
    o.insert(QStringLiteral("schema"), kSavedLayoutsSchema);
    o.insert(QStringLiteral("layouts"), layouts);
    const QByteArray bytes = QJsonDocument(o).toJson(QJsonDocument::Indented);
    m_savedNotices.removeAll(m_writeFailure);
    m_writeFailure.clear();
    if (bytes.size() <= kMaximumBytes && writeAtomically(m_savedFile, bytes))
        return true;
    m_writeFailure = bytes.size() > kMaximumBytes
                         ? QCoreApplication::translate("HikariSub.Workspace",
                                                       "The saved layouts were not changed: together they would be larger than %1 bytes. Delete layouts you no longer use first.")
                               .arg(kMaximumBytes)
                         : QCoreApplication::translate("HikariSub.Workspace", "The saved layouts could not be written to %1.")
                               .arg(QDir::toNativeSeparators(m_savedFile));
    addSavedNotice(m_writeFailure);
    emit changed();
    return false;
}

int WorkspaceLayoutController::savedLayoutIndex(const QString &name) const
{
    const QString wanted = name.trimmed();
    for (std::size_t i = 0; i < m_saved.size(); ++i)
        if (m_saved[i].name.compare(wanted, Qt::CaseInsensitive) == 0)
            return static_cast<int>(i);
    return -1;
}

QString WorkspaceLayoutController::savedLayoutNamed(const QString &name) const
{
    const int index = savedLayoutIndex(name);
    return index < 0 ? QString() : m_saved[static_cast<std::size_t>(index)].name;
}

QVariantList WorkspaceLayoutController::savedLayouts() const
{
    QVariantList rows;
    for (const SavedLayout &saved : m_saved)
        rows << QVariantMap{{QStringLiteral("name"), saved.name},
                            {QStringLiteral("usable"), saved.arrangement.has_value()},
                            {QStringLiteral("problem"), saved.problem}};
    return rows;
}

QString WorkspaceLayoutController::layout() const
{
    return savedLayoutIndex(m_layout) >= 0 ? savedLayoutNamed(m_layout) : QString();
}

QString WorkspaceLayoutController::layoutLabel() const
{
    const QString saved = layout();
    return saved.isEmpty() ? presetLabel(m_preset) : saved;
}

QString WorkspaceLayoutController::layoutNameProblem(const QString &name, const QString &renaming) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return QCoreApplication::translate("HikariSub.Workspace", "Enter a name for the layout.");
    if (trimmed.size() > kMaximumNameLength)
        return QCoreApplication::translate("HikariSub.Workspace", "The name is longer than %1 characters.")
            .arg(kMaximumNameLength);
    for (const QChar c : trimmed)
        if (c.category() == QChar::Other_Control || c == QChar::LineSeparator || c == QChar::ParagraphSeparator)
            return QCoreApplication::translate("HikariSub.Workspace", "The name cannot hold line breaks or tabs.");
    for (const QString &preset : presets())
        if (trimmed.compare(preset, Qt::CaseInsensitive) == 0 ||
            trimmed.compare(presetLabel(preset), Qt::CaseInsensitive) == 0)
            return QCoreApplication::translate("HikariSub.Workspace", "%1 is a built-in layout.").arg(presetLabel(preset));
    for (const QString &reserved : m_reservedNames)
        if (trimmed.compare(reserved, Qt::CaseInsensitive) == 0)
            return QCoreApplication::translate("HikariSub.Workspace", "%1 is a built-in layout.").arg(reserved);
    if (!renaming.isEmpty()) {
        const int index = savedLayoutIndex(trimmed);
        if (index >= 0 && index != savedLayoutIndex(renaming))
            return QCoreApplication::translate("HikariSub.Workspace", "A layout named %1 already exists.")
                .arg(m_saved[static_cast<std::size_t>(index)].name);
    }
    return {};
}

void WorkspaceLayoutController::renameLayoutOrigin(const QString &from, const QString &to)
{
    auto rename = [&](std::optional<WorkspaceArrangement> &arrangement) {
        if (arrangement && arrangement->layout.compare(from, Qt::CaseInsensitive) == 0)
            arrangement->layout = to;
    };
    for (auto &[id, tab] : m_tabs)
        rename(tab.arrangement);
    rename(m_quitArrangement);
    if (m_layout.compare(from, Qt::CaseInsensitive) == 0) {
        m_layout = to;
        m_lastSaved.clear();
    }
}

WorkspaceLayoutController::SavedLayoutsState WorkspaceLayoutController::savedLayoutsState() const
{
    return {m_saved, m_tabs, m_quitArrangement, m_layout, m_lastSaved};
}

bool WorkspaceLayoutController::commitSavedLayouts(SavedLayoutsState before)
{
    const bool written = writeSavedLayouts();
    if (!written) {
        m_saved = std::move(before.saved);
        m_tabs = std::move(before.tabs);
        m_quitArrangement = std::move(before.quitArrangement);
        m_layout = std::move(before.layout);
        m_lastSaved = std::move(before.lastSaved);
    }
    emit savedLayoutsChanged();
    emit changed();
    save();
    return written;
}

bool WorkspaceLayoutController::saveLayoutAs(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!m_loaded || !layoutNameProblem(trimmed).isEmpty())
        return false;
    SavedLayoutsState before = savedLayoutsState();
    WorkspaceArrangement arrangement = shownArrangement();
    arrangement.layout = trimmed;
    if (const int index = savedLayoutIndex(trimmed); index >= 0) {
        SavedLayout &saved = m_saved[static_cast<std::size_t>(index)];
        renameLayoutOrigin(saved.name, trimmed);
        saved = SavedLayout{trimmed, arrangement, {}, {}};
    } else {
        m_saved.push_back(SavedLayout{trimmed, arrangement, {}, {}});
    }
    m_layout = trimmed;
    m_lastSaved.clear();
    return commitSavedLayouts(std::move(before));
}

bool WorkspaceLayoutController::applySavedLayout(const QString &name)
{
    if (!applySavedArrangement(name))
        return false;
    choseMode();
    return true;
}

bool WorkspaceLayoutController::applySavedArrangement(const QString &name)
{
    const int index = savedLayoutIndex(name);
    if (index < 0 || !m_loaded || m_default.isEmpty())
        return false;
    const auto &arrangement = m_saved[static_cast<std::size_t>(index)].arrangement;
    if (!arrangement)
        return false;
    dismissPanelNotice();
    const bool shown = showArrangement(*arrangement);
    save();
    return shown;
}

bool WorkspaceLayoutController::resetCurrentLayout()
{
    if (const QString saved = layout(); !saved.isEmpty() && m_saved[static_cast<std::size_t>(savedLayoutIndex(saved))].arrangement)
        return applySavedArrangement(saved);
    return applyPresetArrangement(m_preset);
}

bool WorkspaceLayoutController::renameSavedLayout(const QString &from, const QString &to)
{
    const int index = savedLayoutIndex(from);
    const QString trimmed = to.trimmed();
    if (index < 0 || !layoutNameProblem(trimmed, from).isEmpty())
        return false;
    SavedLayoutsState before = savedLayoutsState();
    SavedLayout &saved = m_saved[static_cast<std::size_t>(index)];
    renameLayoutOrigin(saved.name, trimmed);
    saved.name = trimmed;
    if (saved.arrangement)
        saved.arrangement->layout = trimmed;
    else
        saved.record.insert(QStringLiteral("name"), trimmed);
    return commitSavedLayouts(std::move(before));
}

bool WorkspaceLayoutController::deleteSavedLayout(const QString &name)
{
    const int index = savedLayoutIndex(name);
    if (index < 0)
        return false;
    SavedLayoutsState before = savedLayoutsState();
    renameLayoutOrigin(m_saved[static_cast<std::size_t>(index)].name, {});
    m_saved.erase(m_saved.begin() + index);
    return commitSavedLayouts(std::move(before));
}

int WorkspaceLayoutController::keepFloatingPanelsOnScreen()
{
    return hikari::ui::keepFloatingPanelsOnScreen();
}

}
