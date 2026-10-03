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

    // The editor. begin* return the values it opens with ({} when refused).
    Q_INVOKABLE QVariantMap beginEdit(bool store, int row);
    Q_INVOKABLE QVariantMap beginNew(bool store);
    Q_INVOKABLE QVariantMap beginCopy(bool store, int row);
    // What a commit would ask: {fields (bits changed since the editor
    // opened), rename (an existing Document Style renamed)}.
    Q_INVOKABLE QVariantMap commitQuestions(const QVariantMap &values) const;
    // ChangeStyle: "" when committed, else the legacy message. `selected`
    // are the list's selected rows, used with `applyToSelected`.
    Q_INVOKABLE QString commitEdit(const QVariantMap &values, const QVariantList &selected, bool applyToSelected,
                                   bool renameLines);
    Q_INVOKABLE void endEdit();

    // Conflicts a transfer would ask about, in order (names already there).
    Q_INVOKABLE QStringList transferConflicts(bool toStore, const QVariantList &rows) const;
    // answers: one of "yes", "yesToAll", "no", "cancel" per question asked.
    // Returns the rows to select in the receiving list.
    Q_INVOKABLE QVariantList addToStore(const QVariantList &rows, const QStringList &answers);
    Q_INVOKABLE QVariantList addToDocument(const QVariantList &rows, const QStringList &answers);
    Q_INVOKABLE void addToAllDocuments(const QVariantList &rows, const QStringList &answers);
    // LoadStylesS: the Styles of an ASS file, then the chosen ones transferred.
    Q_INVOKABLE QStringList fileStyles(const QUrl &file);
    Q_INVOKABLE QStringList fileConflicts(bool toStore, const QUrl &file, const QStringList &names) const;
    Q_INVOKABLE void loadFromFile(bool toStore, const QUrl &file, const QStringList &names, const QStringList &answers);

    Q_INVOKABLE QVariantList removeStyles(bool store, const QVariantList &rows);
    Q_INVOKABLE void sortStyles(bool store);
    // kind: 0 to start, 1 up, 2 down, 3 to end. Returns the new rows.
    Q_INVOKABLE QVariantList moveStyles(bool store, const QVariantList &rows, int kind);
    // OnCleanStyles: the legacy "Styles used:\n...\nStyles deleted:\n..." text.
    Q_INVOKABLE QString cleanStyles();

    // StylePreview: the editor's values over the checkered background.
    Q_INVOKABLE void renderPreview(const QVariantMap &values, int width, int height, const QString &text);

signals:
    void changed();
    void previewChanged();

private:
    struct Edit {
        bool store = false;
        std::optional<std::size_t> row;
        core::StyleValues opened;
    };
    application::StyleList *list(bool store, std::optional<application::StyleList> &document) const;
    bool applyDocument(application::EditSession &session, const application::StyleList &styles,
                       const std::vector<std::optional<std::size_t>> &origin,
                       std::optional<std::pair<std::u8string, std::u8string>> rename = std::nullopt);

    application::StyleCatalogs m_catalogs;
    Hooks m_hooks;
    std::optional<Edit> m_edit;
    backends::LibassRenderer m_renderer;
    QImage m_preview;
    int m_previewKey = 0;
};

// "image://stylepreview/<key>": the controller's latest preview.
void attachStylePreview(QQmlEngine &engine, StyleManagerController &controller);

} // namespace hikari::app
