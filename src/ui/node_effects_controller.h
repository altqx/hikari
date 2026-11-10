#pragma once

#include "automation_services_qt.h"
#include "hikari/application/edit_session.h"
#include "hikari/application/node_effects.h"
#include "hikari/application/node_graph.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hikari::ui {

class TemplateEditorController;

// The node-effects mode of one shell window: the current Document's graph Lines, the canvas's
// graph, its preview on one karaoke Line, and Apply.
class NodeEffectsController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(QVariantList graphs READ graphs NOTIFY changed)
    Q_PROPERTY(qulonglong current READ current WRITE setCurrent NOTIFY changed)
    Q_PROPERTY(bool hasGraph READ hasGraph NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString style READ style NOTIFY changed)
    Q_PROPERTY(bool allStyles READ allStyles NOTIFY changed)
    Q_PROPERTY(qint64 seed READ seed NOTIFY graphChanged)
    Q_PROPERTY(QStringList styles READ styles NOTIFY changed)
    Q_PROPERTY(QString unreadable READ unreadable NOTIFY changed)
    Q_PROPERTY(bool newerFormat READ newerFormat NOTIFY changed)
    Q_PROPERTY(QStringList path READ path NOTIFY graphChanged)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY graphChanged)
    Q_PROPERTY(QVariantList wires READ wires NOTIFY graphChanged)
    Q_PROPERTY(QVariantList problems READ problems NOTIFY graphChanged)
    Q_PROPERTY(QStringList selection READ selection WRITE setSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap inspector READ inspector NOTIFY inspectorChanged)
    Q_PROPERTY(QVariantList catalogue READ catalogue CONSTANT)
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY textsChanged)
    Q_PROPERTY(QStringList library READ library NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(qulonglong source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(QVariantList sourceSelection READ sourceSelection WRITE setSourceSelection NOTIFY changed)
    Q_PROPERTY(int sourceNumber READ sourceNumber NOTIFY changed)
    Q_PROPERTY(int sourceStartMs READ sourceStartMs NOTIFY changed)
    Q_PROPERTY(int sourceEndMs READ sourceEndMs NOTIFY changed)
    Q_PROPERTY(QVariantList previewLines READ previewLines NOTIFY previewChanged)
    Q_PROPERTY(bool previewKept READ previewKept NOTIFY previewChanged)
    Q_PROPERTY(double previewMs READ previewMs NOTIFY previewChanged)
    Q_PROPERTY(bool previewOnVideo READ previewOnVideo WRITE setPreviewOnVideo NOTIFY changed)
    Q_PROPERTY(bool videoPreview READ videoPreview NOTIFY previewChanged)
    Q_PROPERTY(bool hasLua READ hasLua NOTIFY graphChanged)
    Q_PROPERTY(bool trusted READ trusted NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(bool canApplySelected READ canApplySelected NOTIFY changed)
    Q_PROPERTY(QVariantMap lastApply READ lastApply NOTIFY changed)

public:
    using SessionSource = std::function<application::EditSession *()>;
    using LuaNodesSource = std::function<application::LuaNodePort *()>;

    explicit NodeEffectsController(QObject *parent = nullptr);
    ~NodeEffectsController() override;

    void setSessionSource(SessionSource source);
    // The Template editor holds the one Lua trust choice for the active Document.
    void shareTrustWith(TemplateEditorController *editor);
    // Called only when a graph with a Lua node runs, so the helper starts then.
    void setLuaNodes(LuaNodesSource source);
    void setLibraryFolder(std::filesystem::path folder);
    void setEditable(bool editable);
    void setMeasure(application::TextMeasurePort *measure) { m_measure = measure; }
    void setDelays(int previewMs, int commitMs);
    void refresh();
    void retranslate();

    // The Document as Apply would leave it for the current source Line, or nothing.
    std::optional<core::Document> videoDocument(const core::Document &doc) const;

    bool shown() const { return m_shown; }
    void setShown(bool shown);
    bool available() const { return session() != nullptr; }
    bool editable() const { return m_editable; }
    QVariantList graphs() const;
    qulonglong current() const { return m_current ? m_current->value : 0; }
    void setCurrent(qulonglong id);
    bool hasGraph() const { return m_graph.has_value(); }
    QString name() const;
    QString style() const;
    bool allStyles() const { return m_graph && m_graph->allStyles; }
    qint64 seed() const { return m_graph ? m_graph->seed : 0; }
    QStringList styles() const;
    QString unreadable() const { return m_unreadable; }
    bool newerFormat() const { return m_newerFormat; }
    QStringList path() const;
    QVariantList nodes() const;
    QVariantList wires() const;
    QVariantList problems() const;
    QStringList selection() const { return m_selection; }
    void setSelection(const QStringList &nodes);
    QString selected() const { return m_selection.isEmpty() ? QString() : m_selection.front(); }
    QVariantMap inspector() const;
    static QVariantList catalogue();
    QVariantList recent() const;
    static QVariantList presets();
    QStringList library() const;
    QVariantList sources() const;
    qulonglong source() const { return m_source ? m_source->value : 0; }
    void setSource(qulonglong id);
    QVariantList sourceSelection() const;
    void setSourceSelection(const QVariantList &ids);
    int sourceNumber() const;
    int sourceStartMs() const;
    int sourceEndMs() const;
    QVariantList previewLines() const;
    bool previewKept() const { return m_previewKept; }
    double previewMs() const { return m_previewSeconds * 1000; }
    bool previewOnVideo() const { return m_previewOnVideo; }
    void setPreviewOnVideo(bool on);
    bool videoPreview() const;
    bool hasLua() const;
    bool trusted() const;
    bool canApply() const;
    bool canApplySelected() const;
    QVariantMap lastApply() const { return m_lastApply; }

    // Canvas edits, each merged into one "Edit graph" step with those that follow within the commit delay.
    Q_INVOKABLE QString addNode(const QString &type, double x, double y, const QString &fromNode = {},
                                const QString &fromPort = {});
    Q_INVOKABLE void moveNode(const QString &node, double x, double y);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void removeNode(const QString &node);
    // Why the wire can't be drawn, or empty when it can; drawing it replaces the input's wire.
    Q_INVOKABLE QString wireProblem(const QString &fromNode, const QString &fromPort, const QString &toNode,
                                    const QString &toPort) const;
    Q_INVOKABLE QString connectPorts(const QString &fromNode, const QString &fromPort, const QString &toNode,
                                     const QString &toPort);
    Q_INVOKABLE void disconnectInput(const QString &node, const QString &port);
    // The catalogue's node types with an input a `portType` output can feed.
    Q_INVOKABLE QStringList nodeTypesTaking(const QString &portType) const;
    Q_INVOKABLE void setField(const QString &node, const QString &field, const QVariant &value);
    Q_INVOKABLE bool renameNode(const QString &from, const QString &to);
    Q_INVOKABLE bool groupSelection();
    Q_INVOKABLE bool enterGroup(const QString &node);
    Q_INVOKABLE bool leaveGroup();
    Q_INVOKABLE void setSeed(qint64 seed);
    Q_INVOKABLE void newSeed();
    Q_INVOKABLE void setStyle(const QString &style);
    Q_INVOKABLE void setAllStyles(bool all);
    Q_INVOKABLE void commit();

    Q_INVOKABLE bool newGraph();
    Q_INVOKABLE bool addPreset(int index);
    Q_INVOKABLE bool addFromLibrary(const QString &name);
    Q_INVOKABLE bool saveToLibrary();
    Q_INVOKABLE QString graphNameProblem(const QString &name) const;
    Q_INVOKABLE bool renameGraph(const QString &name);
    Q_INVOKABLE bool duplicateGraph();
    Q_INVOKABLE bool deleteGraph();
    Q_INVOKABLE bool apply(bool selectedOnly);
    Q_INVOKABLE void trustDocument();

signals:
    void changed();
    void graphChanged();
    void selectionChanged();
    void inspectorChanged();
    void previewChanged();
    void recentChanged();
    void libraryChanged();
    void documentEdited();
    void videoChanged();
    void textsChanged();

private:
    application::EditSession *session() const;
    const core::Document *document() const;
    const application::DocumentGraph *currentGraph() const;
    std::optional<application::NodeGraph> view() const;
    void edit(const application::NodeGraph &root);
    void editView(const application::NodeGraph &view);
    bool runGraphCommand(const std::function<bool(application::EditSession &)> &command);
    void reload();
    void updateSources();
    void schedulePreview();
    void runPreview();
    application::NodeEffectOptions options() const;
    bool editKeepingTrust(const std::function<bool()> &edit);
    void addRecent(const QString &type);
    QString luaName() const;

    SessionSource m_sessionSource;
    QPointer<TemplateEditorController> m_trustOwner;
    LuaNodesSource m_luaNodes;
    QtTextMeasurePort m_qtMeasure;
    application::TextMeasurePort *m_measure = &m_qtMeasure;
    std::filesystem::path m_libraryFolder;
    bool m_editable = false;
    bool m_shown = false;
    bool m_previewOnVideo = true;
    const application::EditSession *m_seenSession = nullptr;
    std::uint64_t m_seenRevision = 0;

    std::vector<application::DocumentGraph> m_graphs;
    std::optional<core::LineId> m_current;
    std::optional<application::NodeGraph> m_graph;
    bool m_pending = false;
    std::vector<std::string> m_path;
    QString m_unreadable;
    bool m_newerFormat = false;
    QStringList m_selection;
    QStringList m_recent;

    std::vector<core::LineId> m_sources;
    std::optional<core::LineId> m_source;
    std::set<core::LineId> m_sourceSelection;

    QTimer m_previewTimer, m_commitTimer;
    std::optional<application::NodeEffectRun> m_preview;
    application::NodeValueTrace m_trace;
    std::vector<application::NodeEffectProblem> m_runProblems;
    bool m_previewKept = false;
    double m_previewSeconds = 0;
    application::NodeEffectCache m_cache;
    QVariantMap m_lastApply;
};

}
