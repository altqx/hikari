#pragma once

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
    static constexpr char kGettextContext[] = "HikariSub.Automation.Gettext";

    // Pseudolocales for layout checks; they translate every string, also ones no catalog holds.
    enum class Pseudo { None, Accents, RightToLeft };

    explicit Localisation(QString catalogDir = QStringLiteral(":/i18n"), QObject *parent = nullptr);
    ~Localisation() override; // the layout goes back to left to right

    // The languages with an interface catalog ("ko_KR", "pl"), sorted by code unit.
    QStringList catalogLanguages() const;
    // What each catalog makes of `sources` in `context`: every translation
    // that differs from its source, once.
    QStringList translationsOf(const char *context, const QStringList &sources) const;

    // The catalog a PROGRAM_LANGUAGE value selects among available: "" (English) for "", "en",
    // "0" and "1"; the tag itself; else one of the same language ("ko" -> "ko_KR"). found is
    // false when no catalog has the tag's language.
    static QString resolve(const QString &tag, const QStringList &available, bool *found = nullptr);

    // "pl" when the first of the system's uiLanguages is Polish, else "".
    static QString firstStartLanguage(const QStringList &uiLanguages);

    // False when tag names a language without a catalog (English is used).
    bool setLanguage(const QString &tag);
    // The catalog language in use; empty for English.
    QString language() const { return m_language; }

    // Where qt_<language>.qm is looked for; read at the next switch.
    void setQtCatalogDir(const QString &dir) { m_qtDir = dir; }

    void setPseudo(Pseudo pseudo);
    Pseudo pseudo() const { return m_pseudo; }
    // "accents" or "rtl" (HIKARI_PSEUDOLOCALE), anything else none.
    static Pseudo pseudoNamed(const QString &name);

    // From the catalogs' QT_LAYOUT_DIRECTION, as QGuiApplication reads it.
    bool rightToLeft() const { return m_rightToLeft; }

    // Invalid UTF-8 gives ""; a source with a NUL or no finished translation comes back as is.
    // UTF-8 in and out.
    std::string gettext(const std::string &source) const;

    void refreshWindows();

signals:
    void languageChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void install();
    void mirror(QQuickWindow *window) const;

    QString m_dir;
    QString m_qtDir;
    QString m_language;
    Pseudo m_pseudo = Pseudo::None;
    bool m_rightToLeft = false;
    std::unique_ptr<QTranslator> m_qt, m_app, m_pseudoTranslator, m_gettext, m_english;
};

}
