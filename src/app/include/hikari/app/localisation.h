#pragma once

// O5: the interface language (docs/qt/localisation.md, ADR-0002). Legacy
// reads PROGRAM_LANGUAGE once at start (hikarisubApp.cpp:327-369: a wxLocale
// with the hikarisub catalog; "Language (program restart required)"); the
// rewrite switches live, as the card asks: the catalogs are replaced with a
// defined precedence, every QML engine retranslates, the layout direction
// follows the language and languageChanged() tells the owners of cached
// strings to rebuild them.
//
// Catalogs are the QM the build embeds under :/i18n (hikari_translations):
// hikarisub_<tag>.qm for the interface, hikarisub_gettext_<tag>.qm for
// aegisub.gettext, kept out of the translators the interface reads.
//
// Precedence, from the first searched: a pseudolocale (tests and
// developers), the HikariSub catalog, Qt's own catalog (qt_<language> from
// the Qt installation). English, the source language, has no catalog.

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <string>

class QTranslator;
class QQuickWindow;

namespace hikari::app {

class Localisation : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)
public:
    // The context of the aegisub.gettext entries (i18n/hikarisub_gettext_*.ts).
    static constexpr char kGettextContext[] = "HikariSub.Automation.Gettext";

    // Pseudolocales for layout checks: accented and longer text, or the
    // same mirrored right to left. They translate every string, also ones
    // no catalog holds, so an untranslated string stands out.
    enum class Pseudo { None, Accents, RightToLeft };

    // `catalogDir` holds the QM files (the embedded :/i18n by default).
    explicit Localisation(QString catalogDir = QStringLiteral(":/i18n"), QObject *parent = nullptr);
    ~Localisation() override; // its translators go; the layout is left to right again

    // The languages with an interface catalog ("ko_KR", "pl", "ta", "th_TH"),
    // sorted as legacy sorts wxTranslations::GetAvailableTranslations.
    QStringList catalogLanguages() const;

    // The catalog a PROGRAM_LANGUAGE value selects among `available`: none
    // (English) for "", "en" and legacy's "0" and "1"; the tag itself; else
    // a catalog of the same language ("ko" or "ko_KP" -> "ko_KR", "pl_PL" ->
    // "pl"), which is where wxLocale's canonical names led for the shipped
    // catalogs. A tag with no catalog of its language gives `found` false.
    static QString resolve(const QString &tag, const QStringList &available, bool *found = nullptr);

    // Legacy's first start (hikarisubApp.cpp:319-325): with no settings yet
    // and a Polish system interface language, PROGRAM_LANGUAGE and
    // DICTIONARY_LANGUAGE become "pl". `uiLanguages` is the system's
    // (QLocale::system().uiLanguages()), first preferred.
    static QString firstStartLanguage(const QStringList &uiLanguages);

    // Installs the catalogs `tag` selects (resolve()) in place of the
    // current ones and refreshes the interface. False when the tag names a
    // language without a catalog (English is used).
    bool setLanguage(const QString &tag);
    // The catalog language in use; empty for English.
    QString language() const { return m_language; }

    void setPseudo(Pseudo pseudo);
    Pseudo pseudo() const { return m_pseudo; }
    // "accents" or "rtl" (HIKARI_PSEUDOLOCALE), anything else none.
    static Pseudo pseudoNamed(const QString &name);

    // Whether the interface lays out right to left (the catalogs'
    // QT_LAYOUT_DIRECTION, as QGuiApplication reads it).
    bool rightToLeft() const { return m_rightToLeft; }

    // Legacy get_translation: wxGetTranslation(wxString(str, wxConvUTF8, len))
    // over the hikarisub catalog. Bytes that are not strict UTF-8 convert to
    // the empty string, which is never translated; the source comes back
    // when the catalog has no finished translation; a source with a NUL
    // never matches an entry. UTF-8 out.
    std::string gettext(const std::string &source) const;

    // QQmlEngine::retranslate for every engine with a window, and the
    // layout direction on every window.
    void refreshWindows();

signals:
    void languageChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void install();
    void mirror(QQuickWindow *window) const;

    QString m_dir;
    QString m_language;
    Pseudo m_pseudo = Pseudo::None;
    bool m_rightToLeft = false;
    std::unique_ptr<QTranslator> m_qt, m_app, m_pseudoTranslator, m_gettext;
};

} // namespace hikari::app
