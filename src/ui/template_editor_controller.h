#pragma once

#include "hikari/application/edit_session.h"
#include "hikari/application/template_preview.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <string>
#include <vector>

namespace hikari::ui {

class TemplateEditorController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(QVariantList templates READ templates NOTIFY changed)
    Q_PROPERTY(int templateCount READ templateCount NOTIFY changed)
    Q_PROPERTY(int codeCount READ codeCount NOTIFY changed)
    Q_PROPERTY(int karaokeCount READ karaokeCount NOTIFY changed)
    Q_PROPERTY(qulonglong current READ current WRITE setCurrent NOTIFY changed)
    Q_PROPERTY(int currentNumber READ currentNumber NOTIFY changed)
    Q_PROPERTY(bool currentIsCode READ currentIsCode NOTIFY changed)
    Q_PROPERTY(QString effect READ effect NOTIFY changed)
    Q_PROPERTY(QString style READ style NOTIFY changed)
    Q_PROPERTY(int layer READ layer NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QStringList styles READ styles NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY previewChanged)
    Q_PROPERTY(QVariantList problemRange READ problemRange NOTIFY previewChanged)
    Q_PROPERTY(bool hasProblems READ hasProblems NOTIFY previewChanged)
    Q_PROPERTY(qulonglong source READ source NOTIFY previewChanged)
    Q_PROPERTY(bool sourceUnknown READ sourceUnknown NOTIFY previewChanged)
    Q_PROPERTY(int sourceNumber READ sourceNumber NOTIFY previewChanged)
    Q_PROPERTY(QString sourceStyle READ sourceStyle NOTIFY previewChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY previewChanged)
    Q_PROPERTY(int sourceStartMs READ sourceStartMs NOTIFY previewChanged)
    Q_PROPERTY(int sourceEndMs READ sourceEndMs NOTIFY previewChanged)
    Q_PROPERTY(QVariantList previewLines READ previewLines NOTIFY previewChanged)
    Q_PROPERTY(double previewMs READ previewMs NOTIFY previewChanged)
    Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY previewChanged)
    Q_PROPERTY(bool previewPending READ previewPending NOTIFY changed)
    Q_PROPERTY(bool previewKept READ previewKept NOTIFY previewChanged)
    Q_PROPERTY(bool previewOnVideo READ previewOnVideo WRITE setPreviewOnVideo NOTIFY changed)
    Q_PROPERTY(bool videoPreview READ videoPreview NOTIFY previewChanged)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(bool canApplySelected READ canApplySelected NOTIFY changed)
    Q_PROPERTY(bool trusted READ trusted NOTIFY changed)
    Q_PROPERTY(int changedNumber READ changedNumber NOTIFY changed)
    Q_PROPERTY(int changedCount READ changedCount NOTIFY changed)
    Q_PROPERTY(QVariantMap lastApply READ lastApply NOTIFY changed)
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)

public:
    using SessionSource = std::function<application::EditSession *()>;
    // MUST KILL: DocumentKey is an anonymous pair (DocumentId, load generation)
    using DocumentKey = std::pair<std::uint64_t, std::uint64_t>;
    using DocumentKeySource = std::function<DocumentKey()>;
    using SessionOf = std::function<application::EditSession *(DocumentKey)>;
    using HostState = std::function<std::string()>;

    explicit TemplateEditorController(QObject *parent = nullptr);
    ~TemplateEditorController() override;

    void setSessionSource(SessionSource source);
    void setDocumentKey(DocumentKeySource key);
    void setSessionOf(SessionOf sessionOf);
    void setHostState(HostState state);
    void setTemplater(application::KaraokeTemplaterPort *templater);
    void setEditable(bool editable);
    void refresh();
    void hostChanged();
    void applied(const std::string &report, std::optional<DocumentKey> ranOn = std::nullopt);
    void setDelays(int previewMs, int commitMs);

    std::optional<core::Document> videoDocument(const core::Document &doc, std::int64_t timeMs, bool playing) const;
    application::TemplatePreviewWindow videoWindow(std::int64_t timeMs, bool playing) const;

    bool shown() const { return m_shown; }
    void setShown(bool shown);
    bool available() const;
    bool editable() const { return m_editable; }
    QVariantList templates() const;
    int templateCount() const;
    int codeCount() const;
    int karaokeCount() const;
    qulonglong current() const { return m_current ? m_current->value : 0; }
    void setCurrent(qulonglong id);
    int currentNumber() const;
    bool currentIsCode() const;
    QString effect() const;
    QString style() const;
    int layer() const;
    // MUST KILL: a non-empty translation silently stands in for the text the templater reads (TLMode)
    QString text() const;
    QStringList styles() const;
    static QVariantList presets();
    QString problem() const;
    QVariantList problemRange() const;
    bool hasProblems() const;
    qulonglong source() const { return m_source ? m_source->value : 0; }
    bool sourceUnknown() const { return m_sourceUnknown; }
    int sourceNumber() const;
    QString sourceStyle() const;
    QString sourceText() const;
    int sourceStartMs() const;
    int sourceEndMs() const;
    QVariantList previewLines() const;
    double previewMs() const { return m_previewSeconds * 1000; }
    bool hasPreview() const { return m_preview.has_value(); }
    bool previewKept() const { return m_previewKept; }
    bool previewOnVideo() const { return m_previewOnVideo; }
    void setPreviewOnVideo(bool on);
    bool videoPreview() const;
    bool canApply() const;
    bool canApplySelected() const;
    bool trusted() const;
    Q_INVOKABLE void trustDocument();
    bool previewPending() const;
    int changedNumber() const;
    int changedCount() const;
    QVariantMap lastApply() const { return m_lastApply; }
    QVariantMap values() const { return m_values; }

    Q_INVOKABLE void setText(const QString &text);
    Q_INVOKABLE void setEffect(const QString &effect);
    Q_INVOKABLE void setStyle(const QString &style);
    Q_INVOKABLE void setLayer(int layer);
    Q_INVOKABLE void commit();
    Q_INVOKABLE bool apply(bool selectedOnly);
    Q_INVOKABLE bool insertTemplate(const QString &effect);
    Q_INVOKABLE bool insertPreset(int index);
    Q_INVOKABLE void requestValues();
    Q_INVOKABLE QVariantList completions(const QString &text, int cursor) const;
    Q_INVOKABLE QVariantList colourRuns(const QString &text) const;
    Q_INVOKABLE QVariantMap effectParts(const QString &effect) const;
    Q_INVOKABLE QString effectWithClass(const QString &effect, const QString &className) const;
    Q_INVOKABLE QString effectWithModifier(const QString &effect, const QString &word, bool on) const;
    Q_INVOKABLE bool hasModifier(const QString &effect, const QString &word) const;

signals:
    void changed();
    void previewChanged();
    void valuesChanged();
    void documentEdited();
    void otherDocumentEdited(std::pair<std::uint64_t, std::uint64_t> document);
    void videoChanged();

private:
    struct Pending {
        core::LineId line;
        std::optional<std::u8string> text, effect, style;
        std::optional<std::int64_t> layer;
        DocumentKey document{};
    };
    struct Stats {
        int generated = 0;
        double seconds = 0;
    };
    struct Issue {
        core::LineId line;
        QString message;
        // MUST KILL: Issue start and length are 1-based byte offsets into the text the run saw
        int start = 0, length = 0;
    };

    bool insertLines(const char *commandName, std::vector<application::MacroDialogueLine> made);
    application::EditSession *session() const;
    const core::Document *document() const;
    const core::LineRecord *line(core::LineId id) const;
    std::optional<core::LineRecord> currentRecord() const;
    static core::LineRecord edited(core::LineRecord line, const Pending &pending);
    application::EditSession *pendingSession() const;
    void clearPreview();
    void edit(const Pending &change);
    std::map<core::LineId, std::u8string> pendingTexts() const;
    void schedulePreview();
    void runPreview();
    void runChain(std::uint64_t generation);
    void previewed(std::uint64_t generation, application::KaraokeTemplaterPort::Preview preview,
                   const application::MacroSnapshot &snapshot);
    void markOwn(std::uint64_t generation, application::KaraokeTemplaterPort::Preview without);
    void finishChain(std::uint64_t generation);
    void runValues(std::uint64_t generation);
    void takeBaseline();
    std::string keyOf(const core::LineRecord &line) const;
    void updateSource();
    void stopRuns();
    bool askOwners();

    DocumentKey documentKey() const;
    // MUST KILL: runDocument folds in the Line editor's draft
    core::Document runDocument() const;
    core::Document pendingDocument() const;
    // MUST KILL: sourceRecord folds in the Line editor's draft
    std::optional<core::LineRecord> sourceRecord() const;
    static std::string formOf(const core::LineRecord &line);
    static std::string fieldOf(const core::LineRecord &line, bool whole = false);
    static std::string headOf(const core::Document &doc);
    static std::string generatedKey(const core::Document &doc);
    struct TrustState {
        std::string templates;
        std::set<std::string> fields;
        std::string head;
    };
    static TrustState stateOf(const application::EditSession &s, bool withDraft = true);
    bool trustedIn(const application::EditSession &s, const DocumentKey &document, bool withDraft = true) const;
    void trust();
    void trust(const application::EditSession &s, const DocumentKey &document);

    SessionSource m_sessionSource;
    DocumentKeySource m_documentKey;
    SessionOf m_sessionOf;
    HostState m_hostState;
    std::string m_hostSeen;
    std::map<DocumentKey, std::vector<TrustState>> m_trusted;
    application::KaraokeTemplaterPort *m_templater = nullptr;
    bool m_editable = false;
    bool m_shown = false;
    bool m_wasAvailable = false;
    bool m_previewOnVideo = true;
    const application::EditSession *m_seenSession = nullptr;
    std::uint64_t m_seenRevision = 0;
    std::string m_seenDraft;
    std::uint64_t m_applyRevision = 0;
    std::optional<core::LineId> m_current;
    std::optional<Pending> m_pending;
    QTimer m_previewTimer, m_commitTimer;
    std::optional<core::LineId> m_source, m_lastSource;
    std::map<core::LineId, core::LineId> m_owners;
    std::optional<std::uint64_t> m_ownersRevision, m_ownersAsked;
    std::optional<std::vector<core::LineRecord>> m_preview;
    std::vector<application::MacroStyleLine> m_previewStyles;
    std::vector<bool> m_own;
    bool m_previewKept = false;
    double m_previewSeconds = 0;
    std::vector<Issue> m_issues;
    QString m_runError;
    std::map<core::LineId, Stats> m_stats;
    std::map<core::LineId, std::set<core::LineId>> m_replaced;
    std::map<core::LineId, std::string> m_matched;
    std::uint64_t m_generation = 0;
    bool m_running = false;
    bool m_previewing = false;
    bool m_wantValues = false;
    std::optional<application::MacroSnapshot> m_previewSnapshot;
    std::optional<core::Document> m_previewSeen;
    std::string m_previewSnapshotKey;
    std::map<DocumentKey, std::map<core::LineId, std::string>> m_baselines;
    bool m_sourceUnknown = false;
    std::map<DocumentKey, std::string> m_appliedElsewhere;
    QVariantMap m_lastApply;
    QVariantMap m_values;
};

}
