#pragma once

// Y1/Y2: the Style manager window (legacy StyleStore with StyleChange and
// StylePreview at 20d647c4): the catalog's Styles ("Styles stored:") and the
// editing target's ("Styles in ASS file:"), the transfers between them, and
// the Style editor with its libass preview.

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
    // SV1: the device pixel ratio the preview was drawn at.
    Q_PROPERTY(qreal previewScale READ previewScale NOTIFY previewChanged)
    // SV1: whether the preview takes the screen's pixels; open windows draw again when it changes.
    Q_PROPERTY(bool previewAtScreenSize READ previewAtScreenSize NOTIFY previewAtScreenSizeChanged)
public:
    struct Hooks {
        std::function<application::EditSession *()> target;                  // the editing target
        // SY1: the editing target's Document, and an open Document's session
        // (null when closed, or the reference): an edit is pinned by the
        // Document, whose id is never reused.
        std::function<std::optional<application::DocumentId>()> targetDocument;
        std::function<application::EditSession *(application::DocumentId)> session;
        std::function<std::vector<application::EditSession *>()> documents;  // every open Document
        std::function<void()> refresh;                                       // after a Document changed
        std::function<std::vector<application::FontLease>()> fonts;          // Y6: the external fonts
        // SY1: the catalog chosen last (styles.lastCatalog), read once at
        // the start and written on each choice.
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

    // The window opened or the editing target changed (LoadAssStyles): the
    // catalog named by "Last Style Storage" becomes current when it exists.
    Q_INVOKABLE void refreshLists();
    // TP1 (#274): the Styles panel's rows, the editing target's Styles in
    // order: {name, font, size, bold, italic, primary, outline (QColor, the
    // ASS alpha as opacity), lines (how many Lines use it)}.
    Q_INVOKABLE QVariantList panelRows() const;
    // The editing target or one of its Lines changed (Application::refreshViews);
    // `available` is notified when the target now has ASS Styles or no longer has.
    void documentEdited();
    // SY1: the catalog refreshLists would make current for the target now.
    Q_INVOKABLE QString followedCatalog() const;
    // The Style manager's choice, kept as styles.lastCatalog.
    Q_INVOKABLE bool chooseCatalog(const QString &name);
    // Another user's load (Settings' conversion catalog): not kept.
    bool loadCatalog(const QString &name);
    Q_INVOKABLE bool createCatalog(const QString &name); // NewCatalog
    Q_INVOKABLE bool deleteCatalog(const QString &name);
    Q_INVOKABLE void saveCatalog();                       // OnClose saves the store
    // SY1: after the views refreshed (an edit, an undo, another tab):
    // targetEdited, `switched` when the target is another Document.
    void targetRefreshed();

    // SY1: a list's rows as the Style manager shows them: [{name, primary,
    // outline (#rrggbb), lines (the Lines using it; -1 in the catalog)}].
    Q_INVOKABLE QVariantList styleRows(bool store) const;
    // SY1: the editor's value keys that differ between the Styles at `rows`
    // ("Multiple values"); the name is left out.
    Q_INVOKABLE QStringList mixedFields(bool store, const QVariantList &rows) const;

    // The editor. begin* return the values it opens with ({} when refused).
    Q_INVOKABLE QVariantMap beginEdit(bool store, int row);
    Q_INVOKABLE QVariantMap beginNew(bool store);
    // LE6: a new Document Style named `name`, prefilled from the Document's
    // "Default" Style (the default Style when it has none); committed, it is
    // added to the Document.
    Q_INVOKABLE QVariantMap beginNamed(const QString &name);
    Q_INVOKABLE QVariantMap beginCopy(bool store, int row);
    // What a commit would ask: {fields (bits changed since the editor
    // opened), rename (an existing Document Style renamed)}.
    Q_INVOKABLE QVariantMap commitQuestions(const QVariantMap &values) const;
    // ChangeStyle: "" when committed, else the legacy message. `selected`
    // are the list's selected rows, used with `applyToSelected`. SY1:
    // `resolved` are the value keys a multiple selection set by hand, written
    // to each Style even where the value equals the first one's.
    Q_INVOKABLE QString commitEdit(const QVariantMap &values, const QVariantList &selected, bool applyToSelected,
                                   bool renameLines, const QStringList &resolved = {});
    Q_INVOKABLE void endEdit();
    // SY1: the edit can still be applied (its file is open; a catalog edit
    // always can).
    Q_INVOKABLE bool editOpen() const;
    // SY1: the catalog a catalog Style's edit goes to, which Settings may
    // have replaced in the lists since ("" without one).
    Q_INVOKABLE QString editCatalog() const;
    // SY1: what the lists are of: the target Document, the catalog and the
    // names in both lists. A question or a review opened over them acts on
    // its answer only while the key is the same (the main window may
    // switch the tab or change the Styles meanwhile).
    Q_INVOKABLE QString listsKey() const;

    // PK1's clash review: the clashes a transfer has, in order
    // ([{name, fields (the style_field flags that differ), same, suggestion}]).
    Q_INVOKABLE QVariantList transferClashes(bool toStore, const QVariantList &rows) const;
    // choices: the review's [{name, action ("replace", "rename", "skip"),
    // newName}]; a clash without one is skipped. Returns the rows to select
    // in the receiving list.
    Q_INVOKABLE QVariantList addToStore(const QVariantList &rows, const QVariantList &choices);
    Q_INVOKABLE QVariantList addToDocument(const QVariantList &rows, const QVariantList &choices);
    Q_INVOKABLE void addToAllDocuments(const QVariantList &rows, const QVariantList &choices);
    // LoadStylesS: the Styles of an ASS file, then the chosen ones transferred.
    Q_INVOKABLE QStringList fileStyles(const QUrl &file);
    Q_INVOKABLE QVariantList fileClashes(bool toStore, const QUrl &file, const QStringList &names) const;
    Q_INVOKABLE void loadFromFile(bool toStore, const QUrl &file, const QStringList &names, const QVariantList &choices);

    Q_INVOKABLE QVariantList removeStyles(bool store, const QVariantList &rows);
    Q_INVOKABLE void sortStyles(bool store);
    // kind: 0 to start, 1 up, 2 down, 3 to end. Returns the new rows.
    Q_INVOKABLE QVariantList moveStyles(bool store, const QVariantList &rows, int kind);
    // OnCleanStyles, for SY1's toast (NT1's notice): {used, deleted (names),
    // report (legacy's "Styles used:\n...\nStyles deleted:\n...", the
    // toast's Details)}; {} without an ASS Document.
    Q_INVOKABLE QVariantMap deleteUnusedStyles();

    // StylePreview: the editor's values over the checkered background;
    // `scale`, the window's device pixel ratio (SV1: used with Screen size).
    Q_INVOKABLE void renderPreview(const QVariantMap &values, int width, int height, const QString &text,
                                   qreal scale = 1);
    // SV1-screen-size-subtitles: previews drawn at the screen's pixels.
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
    // TP1: the Styles panel reads panelRows() again; `retargeted` when the
    // editing target is another Document than last time.
    void documentChanged(bool retargeted);

private:
    struct Edit {
        bool store = false;
        std::optional<std::size_t> row;
        core::StyleValues opened;
        // SY1: the Document the editor opened on, so Apply after a tab
        // switch still goes to it (none once it closed).
        std::optional<application::DocumentId> document;
        // SY1: a catalog Style's catalog, so Apply goes to it when Settings
        // loaded another since.
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
    bool m_available = false; // as last notified by documentEdited
    std::optional<application::DocumentId> m_notifiedTarget; // ditto
};

// StylePreview: `style` at alignment 5 over the checkered background, the
// size of the preview, rendered by libass with `fonts` in memory. SV1: a
// `scale` above 1 draws it with that many pixels per unit of the preview's
// size (the image's device pixel ratio), laid out as at scale 1. Without
// `wrap`, WrapStyle 2: the text on one line (#403, the Style editor's).
QImage renderStylePreview(backends::LibassRenderer &renderer, core::StyleValues style, int width, int height,
                          const QString &text, std::vector<application::FontLease> fonts = {}, qreal scale = 1,
                          bool wrap = true);

// "image://stylepreview/<key>": the controller's latest preview.
void attachStylePreview(QQmlEngine &engine, StyleManagerController &controller);

} // namespace hikari::app
