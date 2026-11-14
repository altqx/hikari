#include "workspace_layout.h"

#include "docking.h"
#include "tiered_presets.h"

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
#include <QHash>
#include <QSet>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <functional>
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

bool sameLayoutName(const QString &a, const QString &b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
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
                                 QStringLiteral("Log"), QStringLiteral("Styles"), QStringLiteral("Template"),
                                 QStringLiteral("Spellchecker"),
                                 QStringLiteral("Postprocessor"), QStringLiteral("QcNotes"), QStringLiteral("Review"),
                                 QStringLiteral("Agent"), QStringLiteral("NodeGraph"), QStringLiteral("NodeInspector"),
                                 QStringLiteral("NodeSources"), QStringLiteral("Explorer")};
    return ids;
}

QString WorkspaceLayoutController::pluginPanelId(const QString &plugin, const QString &panel)
{
    return QStringLiteral("plugin:") + plugin + u'/' + panel;
}

bool WorkspaceLayoutController::isPluginPanel(const QString &name)
{
    static const QString prefix = QStringLiteral("plugin:");
    if (!name.startsWith(prefix) || name.size() > 200)
        return false;
    const QStringView rest = QStringView(name).mid(prefix.size());
    const qsizetype cut = rest.indexOf(u'/');
    auto fits = [](QStringView part, bool dots) {
        return !part.isEmpty() && std::all_of(part.begin(), part.end(), [dots](QChar c) {
            return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-' || (dots && c == u'.');
        });
    };
    return cut > 0 && fits(rest.left(cut), true) && fits(rest.mid(cut + 1), false);
}

namespace {

QHash<QString, QVariantMap> &pluginPanels()
{
    static QHash<QString, QVariantMap> known;
    return known;
}

}

QVariantMap WorkspaceLayoutController::pluginPanelInfo(const QString &dock)
{
    return pluginPanels().value(dock);
}

void WorkspaceLayoutController::rememberPluginPanel(const QString &dock, const QString &title, const QString &pluginName)
{
    if (isPluginPanel(dock))
        pluginPanels().insert(dock, {{QStringLiteral("title"), title}, {QStringLiteral("pluginName"), pluginName}});
}

namespace {

QJsonValue renamed(const QJsonValue &value, const std::function<QString(const QString &)> &rename)
{
    static const QSet<QString> nameKeys{QStringLiteral("uniqueName"), QStringLiteral("objectName"),
                                        QStringLiteral("mainWindowUniqueName"), QStringLiteral("parentMainWindowName")};
    static const QSet<QString> nameLists{QStringLiteral("dockWidgets"), QStringLiteral("closedDockWidgets")};
    if (value.isArray()) {
        QJsonArray out;
        for (const QJsonValue &v : value.toArray())
            out.append(renamed(v, rename));
        return out;
    }
    if (!value.isObject())
        return value;
    QJsonObject out = value.toObject();
    for (auto it = out.begin(); it != out.end(); ++it) {
        if (nameKeys.contains(it.key()) && it->isString() && !it->toString().isEmpty()) {
            *it = rename(it->toString());
        } else if (nameLists.contains(it.key()) && it->isArray()) {
            QJsonArray names;
            for (const QJsonValue &n : it->toArray())
                names.append(n.isString() ? QJsonValue(rename(n.toString())) : n);
            *it = names;
        } else {
            *it = renamed(*it, rename);
        }
    }
    return out;
}

QByteArray rewritten(const QByteArray &payload, const std::function<QString(const QString &)> &rename,
                     const QString &affinity)
{
    QJsonObject root = QJsonDocument::fromJson(payload).object();
    if (root.isEmpty())
        return payload;
    root = renamed(root, rename).toObject();
    for (const QString list : {QStringLiteral("mainWindows"), QStringLiteral("floatingWindows"), QStringLiteral("allDockWidgets")}) {
        QJsonArray items = root.value(list).toArray();
        for (QJsonValueRef item : items) {
            QJsonObject o = item.toObject();
            if (!affinity.isEmpty())
                o.insert(QStringLiteral("affinities"), QJsonArray{affinity});
            else if (list == QLatin1String("mainWindows"))
                o.insert(QStringLiteral("affinities"), QJsonArray{});
            else
                o.remove(QStringLiteral("affinities"));
            item = o;
        }
        if (root.contains(list))
            root.insert(list, items);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

}

QString ShellWindowNames::affinity() const
{
    return QStringLiteral("w%1").arg(window);
}

QString ShellWindowNames::engineName(const QString &name) const
{
    return window == 0 ? name : name + QLatin1Char('~') + QString::number(window);
}

QByteArray ShellWindowNames::toEngine(const QByteArray &payload) const
{
    return rewritten(payload, [this](const QString &name) { return engineName(name); }, affinity());
}

QByteArray ShellWindowNames::toStored(const QByteArray &payload) const
{
    const QString suffix = engineName({});
    return rewritten(payload, [&suffix](const QString &name) {
        return !suffix.isEmpty() && name.endsWith(suffix) ? name.chopped(suffix.size()) : name;
    }, {});
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

QStringList pluginPanelsIn(const QJsonObject &payload)
{
    QStringList found;
    auto take = [&found](const QJsonValue &dock) {
        const QString name = dock.isObject() ? dock.toObject().value(QStringLiteral("uniqueName")).toString() : dock.toString();
        if (WorkspaceLayoutController::isPluginPanel(name) && !found.contains(name))
            found << name;
    };
    for (const char *list : {"allDockWidgets", "closedDockWidgets"})
        for (const auto &dock : payload.value(QLatin1String(list)).toArray())
            take(dock);
    for (const char *list : {"mainWindows", "floatingWindows"})
        for (const auto &window : payload.value(QLatin1String(list)).toArray()) {
            const QJsonObject frames = window.toObject()
                                           .value(QStringLiteral("multiSplitterLayout")).toObject()
                                           .value(QStringLiteral("frames")).toObject();
            for (const auto &frame : frames)
                for (const auto &dock : frame.toObject().value(QStringLiteral("dockWidgets")).toArray())
                    take(dock);
        }
    return found;
}

QJsonObject envelopeObject(const QByteArray &payload, const QString &preset, const QString &layout, bool locked)
{
    QJsonObject o;
    o.insert(QStringLiteral("schema"), WorkspaceLayoutController::kSchema);
    o.insert(QStringLiteral("panelRegistry"), WorkspaceLayoutController::kPanelRegistry);
    o.insert(QStringLiteral("engine"), engineVersion());
    o.insert(QStringLiteral("preset"), preset);
    if (!layout.isEmpty())
        o.insert(QStringLiteral("layout"), layout);
    if (locked)
        o.insert(QStringLiteral("locked"), true);
    o.insert(QStringLiteral("panels"), QJsonArray::fromStringList(WorkspaceLayoutController::panelIds()));
    if (payload.isEmpty())
        return o;
    const QJsonObject docks = QJsonDocument::fromJson(payload).object();
    o.insert(QStringLiteral("payload"), docks);
    QJsonObject plugins;
    for (const QString &dock : pluginPanelsIn(docks))
        plugins.insert(dock, QJsonObject::fromVariantMap(WorkspaceLayoutController::pluginPanelInfo(dock)));
    if (!plugins.isEmpty())
        o.insert(QStringLiteral("pluginPanels"), plugins);
    return o;
}

}

QByteArray WorkspaceLayoutController::envelope(const QByteArray &payload, const QString &preset, const QString &layout,
                                              bool locked)
{
    return QJsonDocument(envelopeObject(payload, preset, layout, locked)).toJson(QJsonDocument::Indented);
}

QByteArray WorkspaceArrangement::serialise() const
{
    return QJsonDocument(envelopeObject(payload, preset, layout, locked)).toJson(QJsonDocument::Compact);
}

std::optional<WorkspaceArrangement> WorkspaceArrangement::parse(const QByteArray &bytes, QString *problem)
{
    const QJsonObject o = bytes.size() > WorkspaceLayoutController::kMaximumBytes ? QJsonObject()
                                                                                  : QJsonDocument::fromJson(bytes).object();
    if (!o.isEmpty() && !o.contains(QStringLiteral("payload"))) {
        if (o.value(QStringLiteral("schema")).toInt(-1) == WorkspaceLayoutController::kSchema &&
            WorkspaceLayoutController::presets().contains(o.value(QStringLiteral("preset")).toString()))
            return WorkspaceArrangement{o.value(QStringLiteral("preset")).toString(), {},
                                        o.value(QStringLiteral("layout")).toString(),
                                        o.value(QStringLiteral("locked")).toBool()};
    }
    WorkspaceArrangement arrangement;
    const auto payload =
        WorkspaceLayoutController::payloadOf(bytes, problem, &arrangement.preset, &arrangement.layout, &arrangement.locked);
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

QByteArray onMainWindowOf(const QByteArray &payload, const QByteArray &own)
{
    const QJsonObject ownObject = QJsonDocument::fromJson(own).object();
    QJsonObject ownMain;
    for (const auto &w : ownObject.value(QStringLiteral("mainWindows")).toArray())
        if (w.toObject().value(QStringLiteral("uniqueName")).toString() == QStringLiteral("Classic"))
            ownMain = w.toObject();
    if (ownMain.isEmpty())
        return payload;
    const QPoint by = mainWindowPosition(ownObject) - mainWindowPosition(QJsonDocument::fromJson(payload).object());
    QJsonObject o = QJsonDocument::fromJson(moveWindows(payload, by, false)).object();
    QJsonArray mains = o.value(QStringLiteral("mainWindows")).toArray();
    for (qsizetype i = 0; i < mains.size(); ++i) {
        QJsonObject window = mains.at(i).toObject();
        if (window.value(QStringLiteral("uniqueName")).toString() != QStringLiteral("Classic"))
            continue;
        for (const QString key : {QStringLiteral("geometry"), QStringLiteral("normalGeometry"),
                                  QStringLiteral("windowState"), QStringLiteral("screenIndex"), QStringLiteral("screenSize")})
            if (ownMain.contains(key))
                window.insert(key, ownMain.value(key));
        mains.replace(i, window);
    }
    o.insert(QStringLiteral("mainWindows"), mains);
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

const QStringList kRemovedPanels{QStringLiteral("FixMinorErrors")};

QString dockName(const QJsonValue &dock)
{
    return dock.isObject() ? dock.toObject().value(QStringLiteral("uniqueName")).toString() : dock.toString();
}

QJsonArray withoutRemovedPanels(const QJsonArray &docks)
{
    QJsonArray kept;
    for (const auto &dock : docks)
        if (!kRemovedPanels.contains(dockName(dock)))
            kept.append(dock);
    return kept;
}

struct Axis {
    QString position, length;
};

Axis axisOf(const QJsonObject &container)
{
    if (container.value(QStringLiteral("orientation")).toInt() == Qt::Horizontal)
        return {QStringLiteral("x"), QStringLiteral("width")};
    return {QStringLiteral("y"), QStringLiteral("height")};
}

bool isContainer(const QJsonObject &item)
{
    return item.value(QStringLiteral("isContainer")).toBool();
}

QJsonObject sizingOf(const QJsonObject &item)
{
    return item.value(QStringLiteral("sizingInfo")).toObject();
}

QJsonObject geometryOf(const QJsonObject &item)
{
    return sizingOf(item).value(QStringLiteral("geometry")).toObject();
}

QJsonObject withSizing(QJsonObject item, const QString &key, const QJsonValue &value)
{
    QJsonObject sizing = sizingOf(item);
    sizing.insert(key, value);
    item.insert(QStringLiteral("sizingInfo"), sizing);
    return item;
}

bool showsAGroup(const QJsonObject &item)
{
    if (!isContainer(item))
        return item.value(QStringLiteral("isVisible")).toBool();
    const QJsonArray children = item.value(QStringLiteral("children")).toArray();
    return std::any_of(children.begin(), children.end(), [](const QJsonValue &c) { return showsAGroup(c.toObject()); });
}

QJsonObject withPercentages(QJsonObject container)
{
    const QString length = axisOf(container).length;
    QJsonArray children = container.value(QStringLiteral("children")).toArray();
    double shown = 0;
    for (const auto &child : children)
        if (showsAGroup(child.toObject()))
            shown += geometryOf(child.toObject()).value(length).toDouble();
    for (qsizetype i = 0; i < children.size(); ++i) {
        const QJsonObject child = children.at(i).toObject();
        const double share = showsAGroup(child) && shown > 0 ? geometryOf(child).value(length).toDouble() / shown : 0;
        children.replace(i, withSizing(child, QStringLiteral("percentageWithinParent"), share));
    }
    container.insert(QStringLiteral("children"), children);
    return container;
}

QJsonObject spanning(QJsonObject item, const Axis &axis, int position, int length)
{
    QJsonObject geometry = geometryOf(item);
    const int grown = length - geometry.value(axis.length).toInt();
    geometry.insert(axis.position, position);
    geometry.insert(axis.length, length);
    item = withSizing(item, QStringLiteral("geometry"), geometry);
    if (!isContainer(item) || grown == 0)
        return item;
    QJsonArray children = item.value(QStringLiteral("children")).toArray();
    const bool along = axisOf(item).length == axis.length;
    qsizetype last = -1;
    for (qsizetype i = 0; i < children.size(); ++i) {
        const QJsonObject child = children.at(i).toObject();
        if (!showsAGroup(child))
            continue;
        last = i;
        if (!along)
            children.replace(i, spanning(child, axis, geometryOf(child).value(axis.position).toInt(), length));
    }
    if (along && last >= 0) {
        const QJsonObject child = children.at(last).toObject();
        const QJsonObject g = geometryOf(child);
        children.replace(last, spanning(child, axis, g.value(axis.position).toInt(), g.value(axis.length).toInt() + grown));
    }
    item.insert(QStringLiteral("children"), children);
    return along ? withPercentages(item) : item;
}

QJsonObject hidingGroups(QJsonObject item, const QSet<QString> &groups)
{
    if (!isContainer(item)) {
        if (groups.contains(item.value(QStringLiteral("guestId")).toString())) {
            item.remove(QStringLiteral("guestId"));
            item.insert(QStringLiteral("isVisible"), false);
        }
        return item;
    }
    QJsonArray children = item.value(QStringLiteral("children")).toArray();
    QList<qsizetype> emptied;
    for (qsizetype i = 0; i < children.size(); ++i) {
        const QJsonObject child = children.at(i).toObject();
        const QJsonObject hidden = hidingGroups(child, groups);
        if (showsAGroup(child) && !showsAGroup(hidden))
            emptied << i;
        children.replace(i, hidden);
    }
    if (emptied.isEmpty()) {
        item.insert(QStringLiteral("children"), children);
        return item;
    }
    const Axis axis = axisOf(item);
    for (const qsizetype i : emptied) {
        qsizetype neighbour = -1;
        for (qsizetype j = i - 1; j >= 0 && neighbour < 0; --j)
            if (showsAGroup(children.at(j).toObject()))
                neighbour = j;
        for (qsizetype j = i + 1; j < children.size() && neighbour < 0; ++j)
            if (showsAGroup(children.at(j).toObject()))
                neighbour = j;
        if (neighbour < 0)
            continue;
        const QJsonObject freed = geometryOf(children.at(i).toObject());
        const QJsonObject kept = geometryOf(children.at(neighbour).toObject());
        const int start = std::min(freed.value(axis.position).toInt(), kept.value(axis.position).toInt());
        const int end = std::max(freed.value(axis.position).toInt() + freed.value(axis.length).toInt(),
                                 kept.value(axis.position).toInt() + kept.value(axis.length).toInt());
        children.replace(neighbour, spanning(children.at(neighbour).toObject(), axis, start, end - start));
    }
    item.insert(QStringLiteral("children"), children);
    return withPercentages(item);
}

bool emptiedByRemovedPanels(QJsonObject &window)
{
    QJsonObject split = window.value(QStringLiteral("multiSplitterLayout")).toObject();
    QJsonObject groups = split.value(QStringLiteral("frames")).toObject();
    QSet<QString> emptied;
    bool trimmed = false;
    for (const QString &id : groups.keys()) {
        QJsonObject group = groups.value(id).toObject();
        const QJsonArray docks = group.value(QStringLiteral("dockWidgets")).toArray();
        const QJsonArray kept = withoutRemovedPanels(docks);
        if (kept.size() == docks.size())
            continue;
        trimmed = true;
        if (kept.isEmpty()) {
            groups.remove(id);
            emptied.insert(id);
            continue;
        }
        const QString current = dockName(docks.at(group.value(QStringLiteral("currentTabIndex")).toInt()));
        int index = 0;
        for (qsizetype i = 0; i < kept.size(); ++i)
            if (dockName(kept.at(i)) == current)
                index = int(i);
        group.insert(QStringLiteral("dockWidgets"), kept);
        group.insert(QStringLiteral("currentTabIndex"), index);
        groups.insert(id, group);
    }
    if (!trimmed)
        return false;
    split.insert(QStringLiteral("frames"), groups);
    if (!emptied.isEmpty() && split.value(QStringLiteral("layout")).isObject())
        split.insert(QStringLiteral("layout"), hidingGroups(split.value(QStringLiteral("layout")).toObject(), emptied));
    window.insert(QStringLiteral("multiSplitterLayout"), split);
    return !emptied.isEmpty() && groups.isEmpty();
}

QJsonObject envelopeWithoutRemovedPanels(QJsonObject envelope)
{
    if (envelope.value(QStringLiteral("panels")).isArray())
        envelope.insert(QStringLiteral("panels"), withoutRemovedPanels(envelope.value(QStringLiteral("panels")).toArray()));
    QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
    if (payload.isEmpty())
        return envelope;
    QJsonArray mainWindows;
    for (const auto &w : payload.value(QStringLiteral("mainWindows")).toArray()) {
        QJsonObject window = w.toObject();
        emptiedByRemovedPanels(window);
        mainWindows.append(window);
    }
    QJsonArray floatingWindows;
    QList<int> floatingIndex;
    for (const auto &w : payload.value(QStringLiteral("floatingWindows")).toArray()) {
        QJsonObject window = w.toObject();
        if (emptiedByRemovedPanels(window)) {
            floatingIndex << -1;
        } else {
            floatingIndex << int(floatingWindows.size());
            floatingWindows.append(window);
        }
    }
    QJsonArray docks;
    for (const auto &d : withoutRemovedPanels(payload.value(QStringLiteral("allDockWidgets")).toArray())) {
        QJsonObject dock = d.toObject();
        QJsonObject position = dock.value(QStringLiteral("lastPosition")).toObject();
        QJsonArray places;
        for (const auto &place : position.value(QStringLiteral("placeholders")).toArray()) {
            QJsonObject p = place.toObject();
            const int index = p.value(QStringLiteral("indexOfFloatingWindow")).toInt(-1);
            if (p.value(QStringLiteral("isFloatingWindow")).toBool() && index >= 0 && index < floatingIndex.size()) {
                if (floatingIndex.at(index) < 0)
                    continue;
                p.insert(QStringLiteral("indexOfFloatingWindow"), floatingIndex.at(index));
            }
            places.append(place.isObject() ? QJsonValue(p) : place);
        }
        if (position.contains(QStringLiteral("placeholders"))) {
            position.insert(QStringLiteral("placeholders"), places);
            dock.insert(QStringLiteral("lastPosition"), position);
        }
        docks.append(d.isObject() ? QJsonValue(dock) : d);
    }
    if (payload.contains(QStringLiteral("mainWindows")))
        payload.insert(QStringLiteral("mainWindows"), mainWindows);
    if (payload.contains(QStringLiteral("floatingWindows")))
        payload.insert(QStringLiteral("floatingWindows"), floatingWindows);
    if (payload.contains(QStringLiteral("allDockWidgets")))
        payload.insert(QStringLiteral("allDockWidgets"), docks);
    if (payload.contains(QStringLiteral("closedDockWidgets")))
        payload.insert(QStringLiteral("closedDockWidgets"),
                       withoutRemovedPanels(payload.value(QStringLiteral("closedDockWidgets")).toArray()));
    envelope.insert(QStringLiteral("payload"), payload);
    return envelope;
}

}

std::optional<QByteArray> WorkspaceLayoutController::payloadOf(const QByteArray &file, QString *problem, QString *preset,
                                                               QString *layout, bool *locked)
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
    const QJsonObject o = envelopeWithoutRemovedPanels(doc.object());
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
    const QStringList pluginDocks = pluginPanelsIn(payload);
    auto known = [](const QString &name) { return panelIds().contains(name) || isPluginPanel(name); };
    const QJsonArray mainWindows = payload.value(QStringLiteral("mainWindows")).toArray();
    bool classic = false;
    for (const auto &w : mainWindows)
        classic = classic || w.toObject().value(QStringLiteral("uniqueName")).toString() == QStringLiteral("Classic");
    if (!classic)
        return fail(QCoreApplication::translate("HikariSub.Workspace", "it holds no “Classic” arrangement"));
    // KDDockWidgets makes a window, group or layout item per entry: cap them at what our panels can fill.
    const QJsonArray floatingWindows = payload.value(QStringLiteral("floatingWindows")).toArray();
    const qsizetype panels = panelIds().size() + pluginDocks.size();
    const QString tooMany =
        QCoreApplication::translate("HikariSub.Workspace", "it holds more windows or panels than HikariSub has");
    if (mainWindows.size() > 1 || floatingWindows.size() > panels || pluginDocks.size() > kMaxPluginPanels)
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
                if (!known(name))
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
        if (!known(name))
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
        if (!known(name))
            return fail(QCoreApplication::translate("HikariSub.Workspace", "it names an unknown panel (%1)").arg(name));
        if (closed.contains(name))
            return fail(tooMany);
        closed.insert(name);
    }
    const QJsonObject kept = o.value(QStringLiteral("pluginPanels")).toObject();
    for (const QString &dock : pluginDocks) {
        const QJsonObject info = kept.value(dock).toObject();
        const QString title = info.value(QStringLiteral("title")).toString().left(100);
        if (!title.isEmpty() && pluginPanelInfo(dock).isEmpty())
            rememberPluginPanel(dock, title, info.value(QStringLiteral("pluginName")).toString().left(80));
    }
    if (preset) {
        const QString named = o.value(QStringLiteral("preset")).toString();
        *preset = presets().contains(named) ? named : QStringLiteral("Editing");
    }
    if (layout)
        *layout = o.value(QStringLiteral("layout")).toString();
    if (locked)
        *locked = o.value(QStringLiteral("locked")).toBool();
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

QByteArray WorkspaceLayoutController::serialiseWindow() const
{
    KDDockWidgets::LayoutSaver saver;
    saver.setAffinityNames({m_names.affinity()});
    return m_names.toStored(saver.serializeLayout());
}

bool WorkspaceLayoutController::restoreWindow(const QByteArray &payload, int options) const
{
    KDDockWidgets::LayoutSaver saver{KDDockWidgets::RestoreOptions(options)};
    saver.setAffinityNames({m_names.affinity()});
    return saver.restoreLayout(m_names.toEngine(payload));
}

void WorkspaceLayoutController::captureDefault()
{
    m_default = serialiseWindow();
}

void WorkspaceLayoutController::buildsArrangement(const QString &name)
{
    if (presets().contains(name) && !m_builtPresets.contains(name))
        m_builtPresets << name;
}

bool WorkspaceLayoutController::restorePayload(const QByteArray &payload, bool inPlace)
{
    if (const QStringList docks = pluginPanelsIn(QJsonDocument::fromJson(payload).object()); !docks.isEmpty())
        emit pluginDocksNeeded(docks);
    m_restoring = true;
    const KDDockWidgets::RestoreOptions options =
        inPlace ? KDDockWidgets::RestoreOptions(KDDockWidgets::RestoreOption_RelativeToMainWindow |
                                                KDDockWidgets::RestoreOption_AbsoluteFloatingDockWindows)
                : KDDockWidgets::RestoreOptions(KDDockWidgets::RestoreOption_None);
    bool restored = false;
    {
        const hikari::ui::DockedMinimums docked;
        restored = restoreWindow(payload, options.toInt());
        if (!restored && !m_default.isEmpty())
            restoreWindow(m_default, options.toInt());
    }
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
    const QPoint origin = mainWindowPosition(QJsonDocument::fromJson(serialiseWindow()).object());
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
    bool locked = false;
    const auto payload = bytes ? payloadOf(*bytes, &problem, &preset, &layout, &locked) : std::nullopt;
    if (!payload || !restorePayload(*payload)) {
        if (problem.isEmpty())
            problem = QCoreApplication::translate("HikariSub.Workspace", "the docking engine could not restore it");
        const QString kept = m_file + QStringLiteral(".unrestored");
        m_lastSaved = serialiseWindow();
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
    m_lastSaved = serialiseWindow();
    layout = knownLayout(layout);
    if (preset != m_preset || layout != m_layout) {
        m_preset = preset;
        m_layout = layout;
        emit changed();
    }
    takeLock(locked);
    return true;
}

bool WorkspaceLayoutController::save()
{
    return write({m_preset, tabPayload(), m_layout, m_locked});
}

bool WorkspaceLayoutController::write(const WorkspaceArrangement &arrangement)
{
    if (m_file.isEmpty() || m_fileLocked || m_restoring || !m_loaded || m_quitting)
        return false;
    if (arrangement.payload == m_lastSaved)
        return false;
    if (const auto old = readFile(m_file); old && payloadOf(*old, nullptr))
        writeAtomically(backupFile(), *old);
    const QString failure = QCoreApplication::translate("HikariSub.Workspace", "The panel layout could not be written to %1.")
                                .arg(QDir::toNativeSeparators(m_file));
    if (!writeAtomically(m_file, envelope(arrangement.payload, arrangement.preset, arrangement.layout, arrangement.locked))) {
        addPanelNotice(failure);
        emit changed();
        return false;
    }
    m_panelNotices.removeAll(failure);
    m_lastSaved = arrangement.payload;
    emit changed();
    return true;
}

bool WorkspaceLayoutController::saveForQuit()
{
    return saveForQuit({m_preset, tabPayload(), m_layout, m_locked});
}

bool WorkspaceLayoutController::saveForQuit(const WorkspaceArrangement &active)
{
    WorkspaceArrangement placed = active;
    if (m_loaded && !placed.payload.isEmpty())
        placed.payload = onMainWindowOf(placed.payload, serialiseWindow());
    const bool saved = write(placed);
    if (m_beforeNodeEffects && !nodeEffectsFile().isEmpty())
        writeAtomically(nodeEffectsFile(), envelope(serialiseWindow()));
    if (m_loaded)
        m_quitArrangement = shownArrangement();
    m_quitting = true;
    return saved;
}

bool WorkspaceLayoutController::resetLayout()
{
    if (m_default.isEmpty())
        return false;
    if (m_beforeNodeEffects) {
        buildNodeEffects();
        emit arrangementSwapped();
        return true;
    }
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
        m_presetPayloads[name] = serialiseWindow();
        return true;
    }
    QString closed;
    if (name == QLatin1String("Timing"))
        closed = QStringLiteral("Video");
    else if (name == QLatin1String("Translation") || name == QLatin1String("Typesetting"))
        closed = QStringLiteral("Audio");
    if (!closed.isEmpty() && KDDockWidgets::Core::Platform::instance())
        if (auto *dock = KDDockWidgets::DockRegistry::self()->dockByName(m_names.engineName(closed)); dock && dock->isOpen())
            dock->close();
    return done;
}

bool WorkspaceLayoutController::applyPreset(const QString &name)
{
    if (!applyPresetArrangement(name))
        return false;
    choseMode();
    emit presetApplied(name);
    return true;
}

bool WorkspaceLayoutController::applyPresetArrangement(const QString &name)
{
    if (!presets().contains(name) || m_default.isEmpty())
        return false;
    dropNodeEffects();
    const bool done = buildPreset(name, true);
    dismissPanelNotice();
    takeLock(false);
    setPreset(name);
    emit arrangementSwapped();
    save();
    return done;
}

QString WorkspaceLayoutController::nodeEffectsFile() const
{
    if (m_file.isEmpty())
        return {};
    const QFileInfo file(m_file);
    return file.dir().filePath(file.completeBaseName() + QStringLiteral("-node-effects.json"));
}

QByteArray WorkspaceLayoutController::tabPayload() const
{
    return m_beforeNodeEffects ? *m_beforeNodeEffects : serialiseWindow();
}

void WorkspaceLayoutController::buildNodeEffects()
{
    restorePayload(m_default, true);
    emit buildArrangement(QStringLiteral("NodeEffects"));
}

bool WorkspaceLayoutController::enterNodeEffects()
{
    if (m_beforeNodeEffects || !m_loaded || m_default.isEmpty())
        return false;
    m_beforeNodeEffects = serialiseWindow();
    const QString file = nodeEffectsFile();
    const auto bytes = file.isEmpty() ? std::nullopt : readFile(file);
    const auto payload = bytes ? payloadOf(*bytes, nullptr) : std::nullopt;
    if (!payload || !restorePayload(*payload, true))
        buildNodeEffects();
    emit changed();
    emit arrangementSwapped();
    return true;
}

void WorkspaceLayoutController::dropNodeEffects()
{
    if (!m_beforeNodeEffects)
        return;
    if (const QString file = nodeEffectsFile(); !file.isEmpty())
        writeAtomically(file, envelope(serialiseWindow()));
    m_beforeNodeEffects.reset();
    emit changed();
}

bool WorkspaceLayoutController::leaveNodeEffects()
{
    if (!m_beforeNodeEffects)
        return false;
    const QByteArray before = *m_beforeNodeEffects;
    dropNodeEffects();
    restorePayload(before, true);
    emit arrangementSwapped();
    return true;
}

bool WorkspaceLayoutController::hasBackup() const
{
    return !m_file.isEmpty() && QFileInfo::exists(backupFile());
}

bool WorkspaceLayoutController::restoreBackup()
{
    const auto bytes = readFile(backupFile());
    QString preset, layout;
    bool locked = false;
    const auto payload = bytes ? payloadOf(*bytes, nullptr, &preset, &layout, &locked) : std::nullopt;
    if (!payload)
        return false;
    dropNodeEffects();
    if (!restorePayload(*payload))
        return false;
    takeLock(locked);
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
    return {m_preset, tabPayload(), m_layout, m_locked};
}

bool WorkspaceLayoutController::showArrangement(const WorkspaceArrangement &arrangement)
{
    dropNodeEffects();
    bool shown = true;
    if (arrangement.payload.isEmpty()) {
        shown = buildPreset(arrangement.preset, true);
    } else if (arrangement.payload != serialiseWindow()) {
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
    takeLock(shown && arrangement.locked);
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
        origin = mainWindowPosition(QJsonDocument::fromJson(serialiseWindow()).object());
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
    reloadSavedLayouts();
}

void WorkspaceLayoutController::reloadSavedLayouts()
{
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
    m_projectLocked = false;
    auto project = readLayoutsFile(m_projectSavedFile, m_projectLocked);
    auto mine = readLayoutsFile(m_savedFile, m_savedLocked);
    m_saved = application::combineTiers(std::move(project), std::move(mine), &SavedLayout::name, sameLayoutName);
}

std::vector<WorkspaceLayoutController::SavedLayout> WorkspaceLayoutController::readLayoutsFile(const QString &path,
                                                                                                bool &locked)
{
    std::vector<SavedLayout> out;
    if (path.isEmpty())
        return out;
    bool unreadable = false;
    const auto bytes = readFile(path, &unreadable);
    if (unreadable) {
        lockSavedLayouts(path, locked);
        return out;
    }
    if (!bytes)
        return out;
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
        const QString kept = path + QStringLiteral(".unrestored");
        if (!keepAside(path, kept)) {
            lockSavedLayouts(path, locked);
            return out;
        }
        addSavedNotice(QCoreApplication::translate("HikariSub.Workspace",
                                                   "The saved layouts were not read because %1. The file is kept at %2.")
                           .arg(problem, QDir::toNativeSeparators(kept)));
        return out;
    }
    bool skipped = false;
    for (const auto &value : o.value(QStringLiteral("layouts")).toArray()) {
        const QJsonObject record = value.toObject();
        SavedLayout saved;
        saved.name = record.value(QStringLiteral("name")).toString().trimmed();
        saved.record = record;
        const bool listed = std::ranges::any_of(out, [&](const SavedLayout &s) { return sameLayoutName(s.name, saved.name); });
        if (!value.isObject() || !layoutNameProblem(saved.name).isEmpty() || listed) {
            skipped = true;
            continue;
        }
        const QByteArray arrangement =
            QJsonDocument(record.value(QStringLiteral("arrangement")).toObject()).toJson(QJsonDocument::Compact);
        saved.arrangement = WorkspaceArrangement::parse(arrangement, &saved.problem);
        if (saved.arrangement)
            saved.arrangement->layout = saved.name;
        out.push_back(std::move(saved));
    }
    if (skipped) {
        const QString kept = path + QStringLiteral(".unrestored");
        if (!keepAside(path, kept)) {
            lockSavedLayouts(path, locked);
            return {};
        }
        addSavedNotice(QCoreApplication::translate("HikariSub.Workspace",
                                                   "Some saved layouts were not read: their names cannot name a layout, or were listed before. The file as it was is kept at %1.")
                           .arg(QDir::toNativeSeparators(kept)));
    }
    return out;
}

void WorkspaceLayoutController::lockSavedLayouts(const QString &path, bool &locked)
{
    locked = true;
    addSavedNotice(QCoreApplication::translate("HikariSub.Workspace",
                                               "The saved layouts at %1 could not be read or copied: they are not used and not changed.")
                       .arg(QDir::toNativeSeparators(path)));
}

bool WorkspaceLayoutController::writeSavedLayouts()
{
    markHiddenLayouts();
    const bool any = std::ranges::any_of(m_saved, [](const SavedLayout &s) { return s.mark.tier == application::Tier::Project; });
    const bool project = m_projectSavedFile.isEmpty() || (!any && !QFileInfo::exists(m_projectSavedFile)) ||
                         writeLayoutsFile(m_projectSavedFile, application::Tier::Project, m_projectLocked);
    return writeLayoutsFile(m_savedFile, application::Tier::Mine, m_savedLocked) && project;
}

bool WorkspaceLayoutController::writeLayoutsFile(const QString &path, application::Tier tier, bool &locked)
{
    if (path.isEmpty())
        return true;
    if (locked) {
        lockSavedLayouts(path, locked);
        emit changed();
        return false;
    }
    QJsonArray layouts;
    for (const SavedLayout &saved : m_saved) {
        if (saved.mark.tier != tier)
            continue;
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
    if (bytes.size() <= kMaximumBytes && writeAtomically(path, bytes))
        return true;
    m_writeFailure = bytes.size() > kMaximumBytes
                         ? QCoreApplication::translate("HikariSub.Workspace",
                                                       "The saved layouts were not changed: together they would be larger than %1 bytes. Delete layouts you no longer use first.")
                               .arg(kMaximumBytes)
                         : QCoreApplication::translate("HikariSub.Workspace", "The saved layouts could not be written to %1.")
                               .arg(QDir::toNativeSeparators(path));
    addSavedNotice(m_writeFailure);
    emit changed();
    return false;
}

int WorkspaceLayoutController::savedLayoutIndex(const QString &name) const
{
    const QString wanted = name.trimmed();
    for (std::size_t i = 0; i < m_saved.size(); ++i)
        if (!m_saved[i].mark.hidden && sameLayoutName(m_saved[i].name, wanted))
            return static_cast<int>(i);
    return -1;
}

int WorkspaceLayoutController::tierLayoutIndex(const QString &name, application::Tier tier) const
{
    const QString wanted = name.trimmed();
    for (std::size_t i = 0; i < m_saved.size(); ++i)
        if (m_saved[i].mark.tier == tier && sameLayoutName(m_saved[i].name, wanted))
            return static_cast<int>(i);
    return -1;
}

void WorkspaceLayoutController::markHiddenLayouts()
{
    application::markHidden(m_saved, &SavedLayout::name, sameLayoutName);
}

void WorkspaceLayoutController::setProjectLayoutsFile(const QString &file)
{
    if (file == m_projectSavedFile)
        return;
    m_projectSavedFile = file;
    reloadSavedLayouts();
}

QVariantList WorkspaceLayoutController::layoutClashes(const QString &name, const QString &from) const
{
    const auto source = hasProject() ? tierFromText(from).value_or(application::Tier::Mine) : application::Tier::Mine;
    const int at = tierLayoutIndex(name, application::otherTier(source));
    if (at < 0 || tierLayoutIndex(name, source) < 0)
        return {};
    const QString taken = m_saved[static_cast<std::size_t>(at)].name;
    QString suggestion;
    for (int n = 2; suggestion.isEmpty() || tierLayoutIndex(suggestion, application::otherTier(source)) >= 0; ++n)
        suggestion = QStringLiteral("%1 (%2)").arg(taken).arg(n);
    return {QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("suggestion"), suggestion}}};
}

QString WorkspaceLayoutController::copyLayout(const QString &name, const QString &from, const QVariantList &choices)
{
    return sendLayout(name, from, choices, false);
}

QString WorkspaceLayoutController::moveLayout(const QString &name, const QString &from, const QVariantList &choices)
{
    return sendLayout(name, from, choices, true);
}

QString WorkspaceLayoutController::sendLayout(const QString &name, const QString &from, const QVariantList &choices,
                                              bool move)
{
    if (!hasProject())
        return {};
    const auto source = tierFromText(from).value_or(application::Tier::Mine);
    const auto target = application::otherTier(source);
    const int at = tierLayoutIndex(name, source);
    if (at < 0)
        return {};
    SavedLayout sent = m_saved[static_cast<std::size_t>(at)];
    QString landing = sent.name;
    if (tierLayoutIndex(landing, target) >= 0) {
        const auto clash = clashChoiceFor(name, choices);
        if (clash.action == application::ClashAction::Skip)
            return {};
        if (clash.action == application::ClashAction::Rename) {
            const QString base = QString::fromStdU16String(newNameOf(clash));
            landing = base.isEmpty() ? sent.name : base;
            for (int n = 2; tierLayoutIndex(landing, target) >= 0; ++n)
                landing = QStringLiteral("%1 (%2)").arg(base.isEmpty() ? sent.name : base).arg(n);
            if (!layoutNameProblem(landing).isEmpty())
                return {};
        }
    }
    SavedLayoutsState before = savedLayoutsState();
    sent.name = landing;
    sent.mark = {target, false};
    if (sent.arrangement)
        sent.arrangement->layout = landing;
    else
        sent.record.insert(QStringLiteral("name"), landing);
    if (move)
        m_saved.erase(m_saved.begin() + at);
    if (const int replaced = tierLayoutIndex(landing, target); replaced >= 0)
        m_saved[static_cast<std::size_t>(replaced)] = std::move(sent);
    else
        m_saved.insert(m_saved.begin() + static_cast<std::ptrdiff_t>(application::tierEnd(m_saved, target)), std::move(sent));
    return commitSavedLayouts(std::move(before)) ? landing : QString();
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
                            {QStringLiteral("problem"), saved.problem},
                            {QStringLiteral("tier"), tierText(saved.mark.tier)},
                            {QStringLiteral("hidden"), saved.mark.hidden}};
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
    if (written)
        emit savedLayoutsWritten();
    else {
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

bool WorkspaceLayoutController::saveLayoutAs(const QString &name, const QString &tier)
{
    const QString trimmed = name.trimmed();
    if (!m_loaded || !layoutNameProblem(trimmed).isEmpty())
        return false;
    const auto into = hasProject() ? tierFromText(tier).value_or(application::Tier::Mine) : application::Tier::Mine;
    SavedLayoutsState before = savedLayoutsState();
    WorkspaceArrangement arrangement = shownArrangement();
    arrangement.layout = trimmed;
    if (const int index = tierLayoutIndex(trimmed, into); index >= 0) {
        SavedLayout &saved = m_saved[static_cast<std::size_t>(index)];
        renameLayoutOrigin(saved.name, trimmed);
        saved = SavedLayout{trimmed, arrangement, {}, {}, {into, false}};
    } else {
        m_saved.insert(m_saved.begin() + static_cast<std::ptrdiff_t>(application::tierEnd(m_saved, into)),
                       SavedLayout{trimmed, arrangement, {}, {}, {into, false}});
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
    if (m_beforeNodeEffects)
        return resetLayout();
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

namespace {

bool anyPanelFloats()
{
    if (!KDDockWidgets::Core::Platform::instance())
        return false;
    const auto docks = KDDockWidgets::DockRegistry::self()->dockwidgets();
    return std::any_of(docks.begin(), docks.end(), [](const auto *dock) { return dock->isOpen() && dock->isFloating(); });
}

}

void WorkspaceLayoutController::takeLock(bool locked)
{
    locked = locked && !anyPanelFloats();
    setLayoutLocked(locked);
    if (locked == m_locked)
        return;
    m_locked = locked;
    m_lastSaved.clear();
    emit changed();
}

bool WorkspaceLayoutController::setLocked(bool locked)
{
    if (locked && anyPanelFloats())
        return false;
    takeLock(locked);
    save();
    return true;
}

}
