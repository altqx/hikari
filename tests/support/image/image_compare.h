#pragma once

// Rendered-image comparison for tests (E2-img). References live under
// tests/references/<name>.png. On a mismatch the actual image and a diff are
// written to the test's artifact directory. HIKARI_UPDATE_REFERENCES=1 rewrites
// references from the actual images (a reviewed developer action).

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

// Compares against tests/references/<name>.png. Returns true on a match; on a
// mismatch sets `message` and writes <name>-actual.png and <name>-diff.png to
// artifactDir.
bool matchesReference(const QImage &actual, const QString &name, const ImageTolerance &tolerance,
                      const QString &referenceDir, const QString &artifactDir, QString *message);

} // namespace hikari::testing
