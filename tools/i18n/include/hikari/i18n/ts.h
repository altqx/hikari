#pragma once
// O4: Qt Linguist TS catalogs, read and written in the layout lupdate and
// lconvert write (Qt 6.11.2 translator/ts.cpp), so a migrated catalog is
// already what the developer-invoked lupdate would leave: running it over the
// same sources changes nothing.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <utility>

namespace hikari::i18n {

enum class TsState { Finished, Unfinished, Vanished, Obsolete };

struct TsMessage {
    QString source;
    QString comment; // disambiguation
    QString extraComment;
    QString translatorComment;
    bool numerus = false;
    TsState state = TsState::Unfinished;
    QStringList translations; // one entry, or the numerus forms
    QList<std::pair<QString, QString>> extras; // <extra-NAME>, NAME without the prefix
};

struct TsContext {
    QString name;
    QList<TsMessage> messages;
};

struct TsCatalog {
    QString language;
    QString sourceLanguage;
    QList<std::pair<QString, QString>> extras;
    QList<TsContext> contexts;

    const TsMessage *find(const QString &context, const QString &source, const QString &comment) const;
};

std::expected<TsCatalog, QString> readTs(const QByteArray &bytes);
QByteArray writeTs(const TsCatalog &catalog);

// Qt's numerus form count for the languages the migration handles, as lupdate
// 6.11.2 creates them (pl 3, ko_KR 1, th_TH 1, ta 2, en 2); -1 otherwise.
int qtNumerusForms(const QString &language);

} // namespace hikari::i18n
