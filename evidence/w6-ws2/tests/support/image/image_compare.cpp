#include "image_compare.h"

#include <QDir>
#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cstdlib>

namespace hikari::testing {

ImageComparison compareImages(const QImage &actualIn, const QImage &expectedIn, const ImageTolerance &tolerance)
{
    ImageComparison result;
    const QImage actual = actualIn.convertToFormat(QImage::Format_ARGB32);
    const QImage expected = expectedIn.convertToFormat(QImage::Format_ARGB32);
    result.sizeMatches = actual.size() == expected.size();
    if (!result.sizeMatches)
        return result;
    result.diff = QImage(actual.size(), QImage::Format_ARGB32);
    result.diff.fill(Qt::transparent);
    for (int y = 0; y < actual.height(); ++y) {
        const auto *a = reinterpret_cast<const QRgb *>(actual.constScanLine(y));
        const auto *e = reinterpret_cast<const QRgb *>(expected.constScanLine(y));
        auto *d = reinterpret_cast<QRgb *>(result.diff.scanLine(y));
        for (int x = 0; x < actual.width(); ++x) {
            const int delta = std::max({std::abs(qRed(a[x]) - qRed(e[x])), std::abs(qGreen(a[x]) - qGreen(e[x])),
                                        std::abs(qBlue(a[x]) - qBlue(e[x])), std::abs(qAlpha(a[x]) - qAlpha(e[x]))});
            result.maxChannelDelta = std::max(result.maxChannelDelta, delta);
            if (delta > tolerance.channel) {
                ++result.differingPixels;
                d[x] = qRgba(255, 0, 0, 255);
            }
        }
    }
    const double total = double(actual.width()) * actual.height();
    result.withinTolerance = total > 0 && result.differingPixels / total <= tolerance.maxDifferentFraction;
    return result;
}

bool matchesReference(const QImage &actual, const QString &name, const ImageTolerance &tolerance,
                      const QString &referenceDir, const QString &artifactDir, QString *message)
{
    const QString referencePath = referenceDir + QLatin1Char('/') + name + QStringLiteral(".png");
    if (qEnvironmentVariableIntValue("HIKARI_UPDATE_REFERENCES") == 1) {
        QDir().mkpath(QFileInfo(referencePath).absolutePath());
        if (!actual.save(referencePath)) {
            *message = QStringLiteral("could not write reference ") + referencePath;
            return false;
        }
        return true;
    }
    const QImage expected(referencePath);
    if (expected.isNull()) {
        *message = QStringLiteral("missing reference ") + referencePath;
        return false;
    }
    const ImageComparison cmp = compareImages(actual, expected, tolerance);
    if (cmp.sizeMatches && cmp.withinTolerance)
        return true;
    QDir().mkpath(artifactDir);
    actual.save(artifactDir + QLatin1Char('/') + name + QStringLiteral("-actual.png"));
    if (!cmp.diff.isNull())
        cmp.diff.save(artifactDir + QLatin1Char('/') + name + QStringLiteral("-diff.png"));
    *message = cmp.sizeMatches
                   ? QStringLiteral("%1: %2 pixels differ (max channel delta %3)")
                         .arg(name)
                         .arg(cmp.differingPixels)
                         .arg(cmp.maxChannelDelta)
                   : QStringLiteral("%1: size %2x%3, reference %4x%5")
                         .arg(name)
                         .arg(actual.width())
                         .arg(actual.height())
                         .arg(expected.width())
                         .arg(expected.height());
    return false;
}

} // namespace hikari::testing
