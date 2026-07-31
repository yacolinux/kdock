#include "iconcolorprovider.h"

#include <QIcon>
#include <QImage>

QColor IconColorProvider::dominant(const QString &iconName, int revision)
{
    if (revision != m_revision) {
        m_cache.clear();
        m_revision = revision;
    }
    auto it = m_cache.constFind(iconName);
    if (it != m_cache.constEnd())
        return it.value();
    const QColor c = compute(iconName);
    m_cache.insert(iconName, c);
    return c;
}

QColor IconColorProvider::contrasting(const QString &iconName, int revision)
{
    const QColor d = dominant(iconName, revision);
    // Near-gray dominants invert to another near-gray (no contrast); snap to
    // black/white by luminance so the indicator stays visible over the fill.
    const int mx = qMax(d.red(), qMax(d.green(), d.blue()));
    const int mn = qMin(d.red(), qMin(d.green(), d.blue()));
    if (mx - mn < 40) {
        const double lum = 0.299 * d.red() + 0.587 * d.green() + 0.114 * d.blue();
        return lum < 128 ? QColor(255, 255, 255) : QColor(0, 0, 0);
    }
    // Otherwise: RGB inversion (stands out against a fill of the dominant color).
    return QColor(255 - d.red(), 255 - d.green(), 255 - d.blue());
}

QColor IconColorProvider::compute(const QString &iconName) const
{
    // Resolve the icon the same way IconProvider does.
    QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull() && iconName.contains(QLatin1Char('/')))
        icon = QIcon(iconName);
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));

    const QImage img = icon.pixmap(QSize(32, 32)).toImage().convertToFormat(QImage::Format_ARGB32);
    if (img.isNull())
        return QColor(127, 127, 127);

    // Two accumulators: "vivid" pixels (preferred) and all opaque pixels
    // (fallback for monochrome icons). Vivid pixels are bucketed by a coarse
    // RGB quantization; the most populated bucket wins.
    QHash<int, int> bucketCount;
    QHash<int, quint64> sumR, sumG, sumB;
    int bestBucket = -1, bestCount = 0;

    quint64 anyR = 0, anyG = 0, anyB = 0;
    int anyCount = 0;

    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            if (qAlpha(px) < 128)
                continue;
            const int r = qRed(px), g = qGreen(px), b = qBlue(px);

            ++anyCount;
            anyR += r; anyG += g; anyB += b;

            // Saturation/value gate (skip near-gray, near-black, near-white).
            const int mx = qMax(r, qMax(g, b));
            const int mn = qMin(r, qMin(g, b));
            const int delta = mx - mn;
            if (mx < 40 || mx > 242)
                continue;                       // too dark / too bright
            if (mx == 0 || (delta * 255 / mx) < 64)
                continue;                       // saturation < ~0.25

            const int key = ((r >> 5) << 6) | ((g >> 5) << 3) | (b >> 5); // 3 bits/channel
            const int c = ++bucketCount[key];
            sumR[key] += r; sumG[key] += g; sumB[key] += b;
            if (c > bestCount) {
                bestCount = c;
                bestBucket = key;
            }
        }
    }

    if (bestBucket >= 0 && bestCount > 0) {
        return QColor(int(sumR[bestBucket] / bestCount),
                      int(sumG[bestBucket] / bestCount),
                      int(sumB[bestBucket] / bestCount));
    }
    if (anyCount > 0)
        return QColor(int(anyR / anyCount), int(anyG / anyCount), int(anyB / anyCount));
    return QColor(127, 127, 127);
}
