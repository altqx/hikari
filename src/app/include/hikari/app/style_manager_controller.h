#pragma once

#include "hikari/application/style_manager.h"
#include "hikari/application/write_coordinator.h"
#include "hikari/backends/libass_renderer.h"

#include <QImage>
#include <QQmlEngine>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace hikari::app {

class StyleManagerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList catalogs READ catalogs NOTIFY changed)
    Q_PROPERTY(QString catalog READ catalog NOTIFY changed)
    Q_PROPERTY(QStringList storeStyles READ storeStyles NOTIFY changed)
    Q_PROPERTY(QStringList documentStyles READ documentStyles NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed) // the editing target has ASS Styles
    Q_PROPERTY(int previewKey READ previewKey NOTIFY previewChanged)
    // The device pixel ratio the preview was drawn at.
    Q_PROPERTY(qreal previewScale READ previewScale NOTIFY previewChanged)
    Q_PROPERTY(bool previewAtScreenSize READ previewAtScreenSize NOTIFY previewAtScreenSizeChanged)
public:
    struct Hooks {
        std::function<application::EditSession *()> target;
        // `session` is null for a closed Document or the reference; ids are never reused.
        std::function<std::optional<application::DocumentId>()> targetDocument;
        std::function<application::EditSession *(application::DocumentId)> session;
        std::function<std::vector<application::EditSession *>()> documents;
        std::function<void()> refresh;                                       // after a Document changed
        std::function<std::vector<application::FontLease>()> fonts;          // the external fonts
        // styles.lastCatalog: read once at the start, written on each choice.
        std::function<QString()> lastCatalog;
        std::function<void(const QString &)> catalogChosen;
    };
    StyleManagerController(std::filesystem::path catalogDir, Hooks hooks, QObject *parent = nullptr);

    QStringList catalogs() const;
    QString catalog() const;
    QStringList storeStyles() const;
    QStringList documentStyles() const;
    bool available() const;
    int previewKey() const { return m_previewKey; }
    qreal previewScale() const { return m_preview.devicePixelRatio(); }
    QImage preview() const { return m_preview; }
    const application::StyleCatalogs &catalogStore() const { return m_catalogs; }

    Q_INVOKABLE void refreshLists();
    // The editing target's Styles in order: {name, font, size, bold, italic, primary, outline
    // (QColor, the ASS alpha as opacity), lines (how many Lines use it)}.
    Q_INVOKABLE QVariantList panelRows() const;
    // Notifies `available` when the target gains or loses ASS Styles.
    void documentEdited();
    // The catalog refreshLists would make current for the target now.
    Q_INVOKABLE QString followedCatalog() const;
    // The Style manager's choice, kept as styles.lastCatalog.
    Q_INVOKABLE bool chooseCatalog(const QString &name);
    // Not kept as styles.lastCatalog.
    bool loadCatalog(const QString &name);
    Q_INVOKABLE bool createCatalog(const QString &name);
    Q_INVOKABLE bool deleteCatalog(const QString &name);
    Q_INVOKABLE void saveCatalog();
    // Emits targetEdited; `switched` when the target is another Document.
    void targetRefreshed();

    // [{name, primary, outline (#rrggbb), lines (the Lines using it; -1 in the catalog)}].
    Q_INVOKABLE QVariantList styleRows(bool store) const;
    // The editor's value keys that differ between the Styles at `rows`, the name left out.
    Q_INVOKABLE QStringList mixedFields(bool store, const QVariantList &rows) const;

    // begin* return the editor's opening values, {} when refused.
    Q_INVOKABLE QVariantMap beginEdit(bool store, int row);
    Q_INVOKABLE QVariantMap beginNew(bool store);
    // Prefilled from the Document's "Default" Style, else the default Style.
    Q_INVOKABLE QVariantMap beginNamed(const QString &name);
    Q_INVOKABLE QVariantMap beginCopy(bool store, int row);
    // {fields (bits changed since the editor opened), rename (an existing Document Style
    // renamed)}.
    Q_INVOKABLE QVariantMap commitQuestions(const QVariantMap &values) const;
    // "" when committed, else the message. `selected` is used with `applyToSelected`; the
    // `resolved` keys are written to each Style even where they equal the first one's.
    Q_INVOKABLE QString commitEdit(const QVariantMap &values, const QVariantList &selected, bool applyToSelected,
                                   bool renameLines, const QStringList &resolved = {});
    Q_INVOKABLE void endEdit();
    // Whether the edit can still be applied; a catalog edit always can.
    Q_INVOKABLE bool editOpen() const;
    // The catalog a catalog Style's edit goes to; "" without one.
    Q_INVOKABLE QString editCatalog() const;
    // Keys the target Document, the catalog and both lists' names; a question opened over the
    // lists acts on its answer only while the key is unchanged.
    Q_INVOKABLE QString listsKey() const;

    // [{name, occurrence, fields (the style_field flags that differ), same, suggestion}].
    Q_INVOKABLE QVariantList transferClashes(bool toStore, const QVariantList &rows) const;
    // choices: [{name, occurrence, action ("replace", "rename", "skip"), newName}]; a clash
    // without one is skipped. Returns the rows to select in the receiving list.
    Q_INVOKABLE QVariantList addToStore(const QVariantList &rows, const QVariantList &choices);
    Q_INVOKABLE QVariantList addToDocument(const QVariantList &rows, const QVariantList &choices);
    Q_INVOKABLE void addToAllDocuments(const QVariantList &rows, const QVariantList &choices);
    Q_INVOKABLE QStringList fileStyles(const QUrl &file);
    Q_INVOKABLE QVariantList fileClashes(bool toStore, const QUrl &file, const QStringList &names) const;
    Q_INVOKABLE void loadFromFile(bool toStore, const QUrl &file, const QStringList &names, const QVariantList &choices);

    Q_INVOKABLE QVariantList removeStyles(bool store, const QVariantList &rows);
    Q_INVOKABLE void sortStyles(bool store);
    // kind: 0 to start, 1 up, 2 down, 3 to end. Returns the new rows.
    Q_INVOKABLE QVariantList moveStyles(bool store, const QVariantList &rows, int kind);
    // {used, deleted (names), report (the toast's Details)}; {} without an ASS Document.
    Q_INVOKABLE QVariantMap deleteUnusedStyles();

    // `scale` is the window's device pixel ratio.
    Q_INVOKABLE void renderPreview(const QVariantMap &values, int width, int height, const QString &text,
                                   qreal scale = 1);
    void setPreviewAtScreenSize(bool on)
    {
        if (on == m_atScreenSize)
            return;
        m_atScreenSize = on;
        emit previewAtScreenSizeChanged();
    }
    bool previewAtScreenSize() const { return m_atScreenSize; }

signals:
    void changed();
    void previewChanged();
    void previewAtScreenSizeChanged();
    void targetEdited(bool switched);
    // `retargeted` when the editing target is another Document than last time.
    void documentChanged(bool retargeted);

private:
    struct Edit {
        bool store = false;
        std::optional<std::size_t> row;
        core::StyleValues opened;
        std::optional<application::DocumentId> document;
        std::u8string catalog;
    };
    application::StyleList *list(bool store, std::optional<application::StyleList> &document) const;
    void catalogChosen();
    void pin(Edit edit);
    application::EditSession *editSession() const;
    bool applyDocument(application::EditSession &session, const application::StyleList &styles,
                       const std::vector<std::optional<std::size_t>> &origin,
                       std::optional<std::pair<std::u8string, std::u8string>> rename = std::nullopt);

    application::StyleCatalogs m_catalogs;
    Hooks m_hooks;
    std::optional<Edit> m_edit;
    std::optional<application::DocumentId> m_lastTarget;
    backends::LibassRenderer m_renderer;
    QImage m_preview;
    bool m_atScreenSize = false;
    int m_previewKey = 0;
    bool m_available = false;
    std::optional<application::DocumentId> m_notifiedTarget;
};

// `style` at alignment 5 over the checkered background. A `scale` above 1 draws that many
// pixels per unit, laid out as at 1. Without `wrap`, WrapStyle 2 keeps the text on one line.
QImage renderStylePreview(backends::LibassRenderer &renderer, core::StyleValues style, int width, int height,
                          const QString &text, std::vector<application::FontLease> fonts = {}, qreal scale = 1,
                          bool wrap = true);

// "image://stylepreview/<key>": the controller's latest preview.
void attachStylePreview(QQmlEngine &engine, StyleManagerController &controller);

} // namespace hikari::app
