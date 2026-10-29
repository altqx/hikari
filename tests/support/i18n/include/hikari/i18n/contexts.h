#pragma once

#include "hikari/i18n/ts.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <expected>

namespace hikari::i18n {

// The registry: context name -> the surface it covers.
using ContextRegistry = QMap<QString, QString>;
std::expected<ContextRegistry, QString> readContextRegistry(const QByteArray &bytes);

struct SourceFile {
    QString path;
    QString text;
};

// One line per problem: an unregistered context, a computed lookup with no mark in its file, or C++ tr().
QStringList lintContexts(const ContextRegistry &registry, const TsCatalog &extracted, const QList<SourceFile> &sources);

}
