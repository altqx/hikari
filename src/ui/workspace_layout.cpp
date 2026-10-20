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

// Whether `focus` is in a Qt Quick Controls menu: a menu's popup item, or an
// item under it. The menu's items and its popup item are QObject children of
// the menu (QQuickMenu, a private type: matched by class name).
bool isInMenu(QObject *focus)
{
    for (auto *item = qobject_cast<QQuickItem *>(focus); item; item = item->parentItem())
        if (item->inherits("QQuickMenuItem") || (item->parent() && item->parent()->inherits("QQuickMenu")))
            return true;
    return false;
}

} // namespace

const QStringList &WorkspaceLayoutController::panelIds()
{
    static const QStringList ids{QStringLiteral("Video"), QStringLiteral("Audio"), QStringLiteral("Editor"),
                                 QStringLiteral("Grid"), QStringLiteral("Reference"), QStringLiteral("Timing"),
                                 QStringLiteral("Search")};
    return ids;
}

WorkspaceLayoutController::WorkspaceLayoutController(QString layoutFile, QObject *parent)
    : QObject(parent), m_file(std::move(layoutFile))
{
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::focusWindowChanged, this, &WorkspaceLayoutController::focusWindowChanged);
        connect(app, &QGuiApplication::focusObjectChanged, this, [this](QObject *focus) {
            if (const bool inMenu = isInMenu(focus); inMenu != m_menuHasFocus) {
                m_menuHasFocus = inMenu;
                emit menuHasFocusChanged();
            }
        });
        // A screen that goes away or shrinks can leave a floating panel
        // where no screen shows it: bring it back (docs/qt/docking.md).
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

namespace {

QJsonObject envelopeObject(const QByteArray &payload, const QString &preset)
{
    QJsonObject o;
    o.insert(QStringLiteral("schema"), WorkspaceLayoutController::kSchema);
    o.insert(QStringLiteral("panelRegistry"), WorkspaceLayoutController::kPanelRegistry);
    o.insert(QStringLiteral("engine"), engineVersion());
    o.insert(QStringLiteral("preset"), preset);
    o.insert(QStringLiteral("panels"), QJsonArray::fromStringList(WorkspaceLayoutController::panelIds()));
    if (!payload.isEmpty())
        o.insert(QStringLiteral("payload"), QJsonDocument::fromJson(payload).object());
    return o;
}

} // namespace

QByteArray WorkspaceLayoutController::envelope(const QByteArray &payload, const QString &preset)
{
    return QJsonDocument(envelopeObject(payload, preset)).toJson(QJsonDocument::Indented);
}

QByteArray WorkspaceArrangement::serialise() const
{
    return QJsonDocument(envelopeObject(payload, preset)).toJson(QJsonDocument::Compact);
}

std::optional<WorkspaceArrangement> WorkspaceArrangement::parse(const QByteArray &bytes, QString *problem)
{
    // A preset alone has no payload; one with a payload is checked as the
    // saved file is, its size before it is read.
    const QJsonObject o = bytes.size() > WorkspaceLayoutController::kMaximumBytes ? QJsonObject()
                                                                                  : QJsonDocument::fromJson(bytes).object();
    if (!o.isEmpty() && !o.contains(QStringLiteral("payload"))) {
        if (o.value(QStringLiteral("schema")).toInt(-1) == WorkspaceLayoutController::kSchema &&
            WorkspaceLayoutController::presets().contains(o.value(QStringLiteral("preset")).toString()))
            return WorkspaceArrangement{o.value(QStringLiteral("preset")).toString(), {}};
    }
    WorkspaceArrangement arrangement;
    const auto payload = WorkspaceLayoutController::payloadOf(bytes, problem, &arrangement.preset);
    if (!payload)
        return std::nullopt;
    arrangement.payload = *payload;
    return arrangement;
}

namespace {

// WS2: a saved rectangle ({"x", "y", "width", "height"}) under `key`, moved
// by `by`; an empty one (a window that never had a place) stays.
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

// The Classic main window's top-left in an engine payload.
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

// The payload's windows moved by `by`: the main windows, the floating
// windows and the places closed panels float again at. `forFile` also
// leaves out the screens and each window's screen.
QByteArray moveWindows(const QByteArray &payload, QPoint by, bool forFile)
{
    QJsonObject o = QJsonDocument::fromJson(payload).object();
    const QString screen = QStringLiteral("screenIndex"), screenSize = QStringLiteral("screenSize");
    auto windows = [&](const QString &key, bool main) {
        QJsonArray list = o.value(key).toArray();
        for (qsizetype i = 0; i < list.size(); ++i) {
            QJsonObject window = list.at(i).toObject();
            moveRect(window, QStringLiteral("geometry"), by);
            // A maximized main window's normal place is a screen position of
            // its own: the file keeps it at the main window's top-left too.
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

} // namespace

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

namespace {

// WS2: what the engine reads from a window's layout tree (Item and
// ItemBoxContainer::fillFromJson, SizingInfo, Rect, Size) it reads outside
// its own catch, so a value of the wrong type, or null, throws out of
// restoreLayout halfway through the restore. Each value is absent or of the
// type the engine reads; a size is a number a window can have (up to Qt's
// largest, which an unbounded panel's size hint is).
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

// An index the engine reads into an int: absent, or a whole number from
// first to last.
bool readableIndex(const QJsonValue &v, double first, double last)
{
    if (v.isUndefined())
        return true;
    const double d = v.toDouble();
    return v.isDouble() && d == std::trunc(d) && d >= first && d <= last;
}

// Affinities the engine would copy onto Hikari's window and panels, which
// have none.
bool noAffinities(const QJsonObject &o)
{
    const QJsonValue affinities = o.value(QStringLiteral("affinities"));
    const QJsonValue name = o.value(QStringLiteral("affinityName"));
    return (affinities.isUndefined() || affinities == QJsonArray()) &&
           (name.isUndefined() || name == QJsonValue(QString()));
}

} // namespace

std::optional<QByteArray> WorkspaceLayoutController::payloadOf(const QByteArray &file, QString *problem, QString *preset)
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
    if (o.value(QStringLiteral("engine")).toString() != engineVersion())
        return fail(QCoreApplication::translate("HikariSub.Workspace", "it was made with %1")
                        .arg(o.value(QStringLiteral("engine")).toString()));
    for (const auto &panel : o.value(QStringLiteral("panels")).toArray())
        if (!panelIds().contains(panel.toString()))
            return fail(QCoreApplication::translate("HikariSub.Workspace", "it names an unknown panel (%1)").arg(panel.toString()));
    const QJsonObject payload = o.value(QStringLiteral("payload")).toObject();
    if (payload.isEmpty())
        return fail(QCoreApplication::translate("HikariSub.Workspace", "it holds no layout"));
    // IDs before the engine sees them: the Classic main window, known panels only.
    const QJsonArray mainWindows = payload.value(QStringLiteral("mainWindows")).toArray();
    bool classic = false;
    for (const auto &w : mainWindows)
        classic = classic || w.toObject().value(QStringLiteral("uniqueName")).toString() == QStringLiteral("Classic");
    if (!classic)
        return fail(QCoreApplication::translate("HikariSub.Workspace", "it holds no Classic arrangement"));
    // The engine makes a window, a group or a layout item for every entry it
    // is given, and a file can come from anyone: no more than Hikari's panels
    // can fill. One main window; a floating window, a group and each panel
    // in a group at most once per panel; in each window's layout, a place and
    // a placeholder per panel and the containers between them.
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
        const QJsonObject layout = w.toObject().value(QStringLiteral("multiSplitterLayout")).toObject();
        const QJsonObject frames = layout.value(QStringLiteral("frames")).toObject();
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
        // A group is found by its id from its layout item: each one a real
        // group under its own id, not the central one (Classic has none),
        // in one layout item of its window.
        for (auto frame = frames.begin(); frame != frames.end(); ++frame) {
            const QJsonObject f = frame.value().toObject();
            if (f.value(QStringLiteral("id")) != QJsonValue(frame.key()) || frame.key().isEmpty() ||
                f.value(QStringLiteral("isNull")) != QJsonValue(false) ||
                (f.value(QStringLiteral("options")).toInt() & 2) != 0)
                return fail(unreadable);
        }
        const QJsonValue root = layout.value(QStringLiteral("layout"));
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
    // The engine takes a floating window's parent from its own main windows
    // by this index without a check (LayoutSaver::restoreLayout), and a file
    // can come from anyone: Hikari's one main window, or none.
    for (const auto &f : payload.value(QStringLiteral("floatingWindows")).toArray()) {
        const QJsonValue parent = f.toObject().value(QStringLiteral("parentIndex"));
        if (!parent.isUndefined() && !(parent.isDouble() && (parent.toDouble() == -1 || parent.toDouble() == 0)))
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
        // The tab a panel goes back to: the engine inserts it at this index
        // when it is not -1 and the group has no fewer tabs, so one below -1
        // reaches its tab list unchecked (Layout::restorePlaceholder).
        if (!readableIndex(position.value(QStringLiteral("tabIndex")), -1, 16777215))
            return fail(unreadable);
        // A panel's place: the engine finds a docked one's main window by
        // name and uses it without a check (Positions::deserialize), and a
        // floating one's window by index. What it cannot read is a docked
        // place in no window, or a floating place in a window the file does
        // not have (-1, as the engine writes for a window being deleted,
        // is passed over).
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
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

void WorkspaceLayoutController::captureDefault()
{
    m_default = KDDockWidgets::LayoutSaver().serializeLayout();
}

bool WorkspaceLayoutController::restorePayload(const QByteArray &payload, bool inPlace)
{
    m_restoring = true; // no autosave while restoring
    // D2: an arrangement taken back within the session keeps the main
    // window as it is now and the floating panels at their own sizes.
    const KDDockWidgets::RestoreOptions options =
        inPlace ? KDDockWidgets::RestoreOptions(KDDockWidgets::RestoreOption_RelativeToMainWindow |
                                                KDDockWidgets::RestoreOption_AbsoluteFloatingDockWindows)
                : KDDockWidgets::RestoreOptions(KDDockWidgets::RestoreOption_None);
    const bool restored = KDDockWidgets::LayoutSaver(options).restoreLayout(payload);
    if (!restored && !m_default.isEmpty())
        KDDockWidgets::LayoutSaver(options).restoreLayout(m_default); // rebuild a known arrangement
    m_restoring = false;
    // D1: the floating windows' layouts at their windows' size (native
    // gate: the floating Audio panel came back 35 pixels short on Wayland).
    hikari::ui::fitFloatingLayouts();
    // D3: the panels' minimum sizes now, not the ones it was saved with.
    hikari::ui::refreshDockConstraints();
    // A layout saved with another set of screens may place a floating panel
    // where none shows it.
    hikari::ui::keepFloatingPanelsOnScreen();
    return restored;
}

bool WorkspaceLayoutController::restoreSaved()
{
    m_loaded = true;
    const bool restored = restoreSavedFile();
    // WS2: files opened before this place their windows from the main
    // window's top-left now.
    const QPoint origin = mainWindowPosition(QJsonDocument::fromJson(KDDockWidgets::LayoutSaver().serializeLayout()).object());
    for (auto &[id, t] : m_tabs)
        if (std::exchange(t.unplaced, false) && t.arrangement && !t.arrangement->payload.isEmpty())
            t.arrangement->payload = moveWindows(t.arrangement->payload, origin, false);
    // WS1: the current tab's own arrangement (from the Session, or the
    // Translation preset of a TLMode file opened before the first frame);
    // without one the tab takes the arrangement just restored.
    if (m_tab) {
        Tab &shown = m_tabs[*m_tab];
        if (shown.arrangement) {
            m_full = shown.full;
            m_timingHidden = shown.timingHidden;
            showArrangement(*shown.arrangement);
        } else {
            shown.arrangement = shownArrangement();
        }
    }
    // Tabs opened in the background before it copy it too.
    for (auto &[id, t] : m_tabs)
        if (!t.arrangement && m_tab != id) {
            t.arrangement = shownArrangement();
            t.full = m_full;
            t.timingHidden = m_timingHidden;
        }
    return restored;
}

bool WorkspaceLayoutController::restoreSavedFile()
{
    if (m_file.isEmpty())
        return false;
    const auto bytes = readFile(m_file);
    if (!bytes)
        return false;
    QString problem, preset;
    const auto payload = payloadOf(*bytes, &problem, &preset);
    m_full.clear();
    if (!payload || !restorePayload(*payload)) {
        if (problem.isEmpty())
            problem = QCoreApplication::translate("HikariSub.Workspace", "the docking engine could not restore it");
        // A copy is kept for diagnosis before anything can overwrite the
        // file; saving resumes once the user changes or resets the layout.
        const QString kept = m_file + QStringLiteral(".unrestored");
        writeAtomically(kept, *bytes);
        m_lastSaved = KDDockWidgets::LayoutSaver().serializeLayout();
        m_notice = QCoreApplication::translate("HikariSub.Workspace",
                                               "The saved panel layout was not restored because %1. The Editing layout is shown; the saved file is kept at %2.")
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
    if (m_file.isEmpty() || m_restoring || !m_loaded || m_quitting || holding())
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

bool WorkspaceLayoutController::saveForQuit()
{
    const bool saved = save();
    // The Session written after this (endSession) names this arrangement
    // for the current tab, not the closed panels of the window going down.
    if (m_loaded)
        m_quitArrangement = shownArrangement();
    m_quitting = true;
    return saved;
}

bool WorkspaceLayoutController::resetLayout()
{
    if (m_default.isEmpty())
        return false;
    m_full.clear();
    const bool done = restorePayload(m_default);
    dismissNotice();
    save();
    return done;
}

bool WorkspaceLayoutController::buildPreset(const QString &name, bool inPlace)
{
    if (m_default.isEmpty())
        return false;
    const bool done = restorePayload(m_default, inPlace);
    // The Editing arrangement with the panels the preset leaves closed
    // (docs/qt/ux/workspaces.md): Timing without Video, Translation and
    // Typesetting without Audio.
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
    if (!presets().contains(name) || m_default.isEmpty())
        return false;
    m_full.clear();
    const bool done = buildPreset(name, false);
    dismissNotice();
    setPreset(name);
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
    if (!payload)
        return false;
    m_full.clear();
    if (!restorePayload(*payload))
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

const QStringList &WorkspaceLayoutController::arrangements()
{
    // Legacy ViewMenu's order (HikariSubFrame.cpp:308-312).
    static const QStringList names{QStringLiteral("GLOBAL_VIEW_ALL"), QStringLiteral("GLOBAL_VIEW_VIDEO"),
                                   QStringLiteral("GLOBAL_VIEW_AUDIO"), QStringLiteral("GLOBAL_VIEW_ONLY_VIDEO"),
                                   QStringLiteral("GLOBAL_VIEW_SUBS")};
    return names;
}

QStringList WorkspaceLayoutController::arrangementPanels(const QString &arrangement)
{
    // HikariSubFrame::OnMenuSelected: the video for ALL, VIDEO and
    // ONLY_VIDEO; the audio box for ALL and AUDIO; the Line editor and the
    // Grid for all but ONLY_VIDEO.
    const QString video = QStringLiteral("Video"), audio = QStringLiteral("Audio"), editor = QStringLiteral("Editor"),
                  grid = QStringLiteral("Grid");
    if (arrangement == QLatin1String("GLOBAL_VIEW_ALL"))
        return {video, audio, editor, grid};
    if (arrangement == QLatin1String("GLOBAL_VIEW_VIDEO"))
        return {video, editor, grid};
    if (arrangement == QLatin1String("GLOBAL_VIEW_AUDIO"))
        return {audio, editor, grid};
    if (arrangement == QLatin1String("GLOBAL_VIEW_ONLY_VIDEO"))
        return {video};
    if (arrangement == QLatin1String("GLOBAL_VIEW_SUBS"))
        return {editor, grid};
    return {};
}

void WorkspaceLayoutController::holdArrangement()
{
    if (holding())
        return;
    m_held = WorkspaceArrangement{m_preset, KDDockWidgets::LayoutSaver().serializeLayout()};
    emit changed();
}

bool WorkspaceLayoutController::releaseArrangement()
{
    if (!holding())
        return false;
    // WS1: the tab shown now may not be the one the hold began in, and its
    // arrangement (from a Session loaded meanwhile) is only restored now.
    const WorkspaceArrangement held = *std::exchange(m_held, std::nullopt);
    const bool restored = held.payload.isEmpty() ? buildPreset(held.preset, true) : restorePayload(held.payload, true);
    if (!restored)
        refuseArrangement(held);
    const QString preset = restored ? held.preset : QStringLiteral("Editing");
    if (preset != m_preset) {
        m_preset = preset;
        m_lastSaved.clear();
    }
    emit changed();
    return true; // the editing arrangement is back, or the Editing layout in its place
}

// --- WS1: each Document tab's arrangement -----------------------------------

WorkspaceArrangement WorkspaceLayoutController::shownArrangement() const
{
    if (m_quitArrangement)
        return *m_quitArrangement;
    if (m_held)
        return *m_held; // the editing arrangement the player layout holds
    return {m_preset, KDDockWidgets::LayoutSaver().serializeLayout()};
}

bool WorkspaceLayoutController::showArrangement(const WorkspaceArrangement &arrangement)
{
    bool shown = true;
    if (holding()) {
        m_held = arrangement; // shown when the editor comes back
    } else if (arrangement.payload.isEmpty()) {
        shown = buildPreset(arrangement.preset, true);
    } else if (arrangement.payload != KDDockWidgets::LayoutSaver().serializeLayout()) {
        // In place: the main window keeps its geometry, the floating panels
        // take theirs; nothing is drawn between taking the panels out and
        // putting them back.
        shown = restorePayload(arrangement.payload, true);
    }
    if (!shown)
        refuseArrangement(arrangement);
    const QString preset = shown ? arrangement.preset : QStringLiteral("Editing");
    if (preset != m_preset) {
        m_preset = preset;
        m_lastSaved.clear();
    }
    emit changed();
    emit arrangementSwapped();
    return shown;
}

void WorkspaceLayoutController::refuseArrangement(const WorkspaceArrangement &arrangement)
{
    // restorePayload rebuilt the default arrangement (a partial restore): the
    // tab shows the Editing layout from now on. (Serialised first: it may be
    // the tab's own.)
    const QByteArray rejected = arrangement.serialise();
    if (m_tab)
        m_tabs[*m_tab].arrangement = WorkspaceArrangement{};
    m_full.clear();
    m_timingHidden = false;
    keepUnrestored(rejected, QCoreApplication::translate("HikariSub.Workspace", "the docking engine could not restore it"), {});
}

void WorkspaceLayoutController::keepUnrestored(const QByteArray &bytes, const QString &problem, const QString &title)
{
    m_notice = title.isEmpty()
                   ? QCoreApplication::translate("HikariSub.Workspace",
                                                 "The panel layout of this tab was not restored because %1. The tab shows the Editing layout.")
                         .arg(problem)
                   : QCoreApplication::translate("HikariSub.Workspace",
                                                 "The panel layout of %1 was not restored because %2. The tab shows the Editing layout.")
                         .arg(title, problem);
    // A copy is kept for diagnosis; the Session is written with the tab's
    // Editing layout from now on.
    if (!m_file.isEmpty()) {
        const QString kept = m_file + QStringLiteral(".tab.unrestored");
        if (writeAtomically(kept, bytes))
            m_notice += QLatin1Char(' ') + QCoreApplication::translate("HikariSub.Workspace", "The layout is kept at %1.")
                                               .arg(QDir::toNativeSeparators(kept));
    }
    emit changed();
}

void WorkspaceLayoutController::showTab(std::uint64_t tab, TabStart start)
{
    if (m_tab == tab)
        return;
    if (m_tab) {
        Tab &left = m_tabs[*m_tab];
        if (m_loaded)
            left.arrangement = shownArrangement();
        left.full = m_full;
        left.timingHidden = m_timingHidden;
    }
    m_tab = tab;
    Tab &shown = m_tabs[tab];
    if (!shown.arrangement) {
        if (start == TabStart::Translation) {
            shown.arrangement = WorkspaceArrangement{QStringLiteral("Translation"), {}};
            shown.full.clear();
            shown.timingHidden = false;
        } else {
            // A copy of the arrangement shown: nothing moves. Before the
            // first arrangement the tab takes the one restoreSaved shows.
            if (m_loaded)
                shown.arrangement = shownArrangement();
            shown.full = m_full;
            shown.timingHidden = m_timingHidden;
            return;
        }
    }
    if (!m_loaded)
        return;
    m_full = shown.full;
    m_timingHidden = shown.timingHidden;
    showArrangement(*shown.arrangement);
}

void WorkspaceLayoutController::addTab(std::uint64_t tab, TabStart start)
{
    if (m_tab == tab)
        return; // showTab's
    Tab &added = m_tabs[tab];
    if (added.arrangement)
        return;
    if (start == TabStart::Translation) {
        added.arrangement = WorkspaceArrangement{QStringLiteral("Translation"), {}};
    } else if (m_loaded) {
        added.arrangement = shownArrangement();
        added.full = m_full;
        added.timingHidden = m_timingHidden;
    } // else restoreSaved gives it the arrangement it shows
}

void WorkspaceLayoutController::replaceTab(std::uint64_t tab, std::uint64_t replacement, TabStart start)
{
    Tab moved;
    if (auto node = m_tabs.extract(tab))
        moved = std::move(node.mapped());
    if (m_tab == tab)
        m_tab = replacement;
    if (start == TabStart::Translation) {
        moved.arrangement = WorkspaceArrangement{QStringLiteral("Translation"), {}};
        moved.full.clear();
        moved.timingHidden = false;
    }
    m_tabs[replacement] = moved;
    if (start == TabStart::Translation && m_tab == replacement && m_loaded) {
        m_full.clear();
        m_timingHidden = false;
        showArrangement(*moved.arrangement);
    }
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

void WorkspaceLayoutController::giveTab(std::uint64_t tab, const WorkspaceArrangement &arrangement)
{
    Tab &t = m_tabs[tab];
    t.arrangement = arrangement;
    t.full.clear();
    t.timingHidden = false;
    t.unplaced = false;
    if (m_tab == tab && m_loaded) {
        m_full.clear();
        m_timingHidden = false;
        showArrangement(arrangement);
    }
}

QByteArray WorkspaceLayoutController::fileArrangement(std::uint64_t tab) const
{
    const auto arrangement = tabArrangement(tab);
    return arrangement ? arrangement->serialiseForFile() : QByteArray();
}

bool WorkspaceLayoutController::setFileArrangement(std::uint64_t tab, const QByteArray &text, QString *problem)
{
    // Where the main window is now, as the engine would save it. A file
    // opened before its first arrangement (from the command line, before
    // the window exists) is placed when restoreSaved has put it somewhere.
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

void WorkspaceLayoutController::rememberFullArrangement()
{
    if (!m_restoring && !holding())
        m_full = KDDockWidgets::LayoutSaver().serializeLayout();
}

bool WorkspaceLayoutController::restoreFullArrangement()
{
    return !m_full.isEmpty() && restorePayload(m_full, true);
}

int WorkspaceLayoutController::keepFloatingPanelsOnScreen()
{
    return hikari::ui::keepFloatingPanelsOnScreen();
}

} // namespace hikari::ui
