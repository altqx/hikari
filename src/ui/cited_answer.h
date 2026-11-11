#pragma once

#include <QColor>
#include <QString>
#include <QVariantList>

namespace hikari::ui {

/// An answer as rich text with a numbered chip after each span its citations place, linked as `cite:<index>`.
/// `citations` are the thread's {index, url, title, start?, end?}; spans are UTF-16 offsets into `text`.
QString citedAnswerHtml(const QString &text, const QVariantList &citations, const QColor &chip, const QColor &ink);

/// The site a citation names: the host without "www.".
QString siteOf(const QString &url);

}
