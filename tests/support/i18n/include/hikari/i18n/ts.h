#pragma once
// TS catalogs in the layout lupdate and lconvert write (Qt 6.12.0 translator/ts.cpp).

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
    // <location> file and line; read only, written catalogs have none (-locations none).
    QList<std::pair<QString, int>> locations;
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

// Numerus forms lupdate 6.12.0 creates for a catalog language; -1 for others.
int qtNumerusForms(const QString &language);

}
