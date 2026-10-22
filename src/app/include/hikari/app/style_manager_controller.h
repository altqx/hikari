#pragma once

// Y1/Y2: the Style manager window (legacy StyleStore with StyleChange and
// StylePreview at 20d647c4): the catalog's Styles ("Styles stored:") and the
// editing target's ("Styles in ASS file:"), the transfers between them, and
// the Style editor with its libass preview.

#include "hikari/application/style_manager.h"
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
public:
    struct Hooks {
        std::function<application::EditSession *()> target;                  // the editing target
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
    QImage preview() const { return m_preview; }
    const application::StyleCatalogs &catalogStore() const { return m_catalogs; }

    // The window opened or the editing target changed (LoadAssStyles): the
    // catalog named by "Last Style Storage" becomes current when it exists.
    Q_INVOKABLE void refreshLists();
    Q_INVOKABLE bool chooseCatalog(const QString &name);
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
    // are the list's selected rows, used with `applyToSelected`.
    Q_INVOKABLE QString commitEdit(const QVariantMap &values, const QVariantList &selected, bool applyToSelected,
                                   bool renameLines);
    Q_INVOKABLE void endEdit();

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
    // OnCleanStyles: the legacy "Styles used:\n...\nStyles deleted:\n..." text.
    Q_INVOKABLE QString cleanStyles();
    // SY1: the same, for the toast: {used, deleted (names), report (the
    // legacy text, for the Log)}; {} without an ASS Document.
    Q_INVOKABLE QVariantMap deleteUnusedStyles();

    // StylePreview: the editor's values over the checkered background.
    Q_INVOKABLE void renderPreview(const QVariantMap &values, int width, int height, const QString &text);

signals:
    void changed();
    void previewChanged();
    void targetEdited(bool switched);

private:
    struct Edit {
        bool store = false;
        std::optional<std::size_t> row;
        core::StyleValues opened;
        // SY1: the Document the editor opened on, so Apply after a tab
        // switch still goes to it.
        application::EditSession *session = nullptr;
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
    application::EditSession *m_lastTarget = nullptr;
    backends::LibassRenderer m_renderer;
    QImage m_preview;
    int m_previewKey = 0;
};

// StylePreview: `style` at alignment 5 over the checkered background, the
// size of the preview, rendered by libass with `fonts` in memory.
QImage renderStylePreview(backends::LibassRenderer &renderer, core::StyleValues style, int width, int height,
                          const QString &text, std::vector<application::FontLease> fonts = {});

// "image://stylepreview/<key>": the controller's latest preview.
void attachStylePreview(QQmlEngine &engine, StyleManagerController &controller);

} // namespace hikari::app
