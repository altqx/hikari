#pragma once

#include <QImage>
#include <QString>

namespace hikari::testing {

struct ImageTolerance {
    int channel = 0;                  // largest allowed per-channel difference (0-255)
    double maxDifferentFraction = 0;  // share of pixels allowed to exceed `channel`
};

struct ImageComparison {
    bool sizeMatches = false;
    qsizetype differingPixels = 0;   // pixels with any channel beyond tolerance
    int maxChannelDelta = 0;
    bool withinTolerance = false;
    QImage diff;                     // red where pixels differ beyond tolerance
};

ImageComparison compareImages(const QImage &actual, const QImage &expected, const ImageTolerance &tolerance);

// Compares with <referenceDir>/<name>.png; a mismatch sets `message` and writes <name>-actual/-diff.png to
// artifactDir. HIKARI_UPDATE_REFERENCES=1 rewrites the reference from `actual` instead.
bool matchesReference(const QImage &actual, const QString &name, const ImageTolerance &tolerance,
                      const QString &referenceDir, const QString &artifactDir, QString *message);

}
