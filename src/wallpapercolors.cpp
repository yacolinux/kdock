#include "wallpapercolors.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {

// Breeze's semantic foregrounds. These do NOT follow the wallpaper: a negative
// that is not red stops meaning "negative". They are only nudged for contrast.
const QColor kNegative(218, 68, 83);
const QColor kNeutral(246, 116, 0);
const QColor kPositive(39, 174, 96);
const QColor kLink(29, 153, 243);
const QColor kVisited(155, 89, 182);

// The two text colors the dock already picks between (see qml/Dock.qml's
// dockContrastColor): not pure black/white, which reads as harsh over a tinted
// background. ensureContrast() takes them the rest of the way if they are not
// enough on their own.
const QColor kTextLight(0xF2, 0xF2, 0xF2);
const QColor kTextDark(0x14, 0x14, 0x14);

QString rgbTriplet(const QColor &c)
{
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

// Move a color along its own value axis. Positive lightens.
QColor shade(const QColor &c, int deltaPercent)
{
    int h, s, v, a;
    c.getHsv(&h, &s, &v, &a);
    v = qBound(0, v + deltaPercent * 255 / 100, 255);
    return QColor::fromHsv(h, s, v, a);
}

QColor blend(const QColor &a, const QColor &b, qreal f)
{
    return QColor(int(a.red() * (1 - f) + b.red() * f), int(a.green() * (1 - f) + b.green() * f),
                  int(a.blue() * (1 - f) + b.blue() * f));
}

WallpaperPalette computePalette(const QString &path)
{
    WallpaperPalette out;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize orig = reader.size();
    if (orig.isValid() && !orig.isEmpty()) {
        // Ask the *decoder* for a small image rather than scaling afterwards.
        // For JPEG this is libjpeg's scale_denom, so the full frame is never
        // built; this one line is what makes sampling a 4K wallpaper cheap.
        QSize target = orig.scaled(WallpaperColors::kSampleSize, WallpaperColors::kSampleSize,
                                   Qt::KeepAspectRatio);
        if (target.isEmpty())
            target = QSize(1, 1);
        reader.setScaledSize(target);
    }
    QImage img = reader.read();
    if (img.isNull())
        return out;
    img = img.convertToFormat(QImage::Format_ARGB32);
    if (img.isNull())
        return out;

    // Two accumulators, exactly as IconColorProvider::compute(): "vivid" pixels
    // bucketed by a coarse RGB quantization (preferred), and every opaque pixel
    // averaged (the fallback that keeps a grayscale wallpaper from coming back
    // with no color at all). The luminance sum rides along on the second one.
    QHash<int, int> bucketCount;
    QHash<int, quint64> sumR, sumG, sumB;

    quint64 anyR = 0, anyG = 0, anyB = 0;
    qreal lumaSum = 0.0;
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
            lumaSum += (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;

            const int mx = qMax(r, qMax(g, b));
            const int mn = qMin(r, qMin(g, b));
            const int delta = mx - mn;
            if (mx < 40 || mx > 242)
                continue;                       // too dark / too bright
            if (mx == 0 || (delta * 255 / mx) < 64)
                continue;                       // saturation < ~0.25

            const int key = ((r >> 5) << 6) | ((g >> 5) << 3) | (b >> 5); // 3 bits/channel
            ++bucketCount[key];
            sumR[key] += r; sumG[key] += g; sumB[key] += b;
        }
    }

    if (anyCount == 0)
        return out;

    out.valid = true;
    out.meanLuma = lumaSum / anyCount;

    // Rank the buckets instead of only tracking the winner: the extra ones are
    // what "Generate color" cycles through. Sorted by population, and by key on
    // a tie so the order does not depend on QHash's iteration order — otherwise
    // the same wallpaper could hand out its variants in a different order on
    // every run, and variant 3 would stop meaning anything.
    QList<int> keys = bucketCount.keys();
    std::sort(keys.begin(), keys.end(), [&bucketCount](int a, int b) {
        if (bucketCount[a] != bucketCount[b])
            return bucketCount[a] > bucketCount[b];
        return a < b;
    });
    // Accept a bucket only when its hue is far enough from every candidate
    // already taken: the winner of a hue band represents it, and the runners-up
    // of that same band would only produce the same background again.
    const auto hueFarEnough = [&out](const QColor &c) {
        const int h = c.hue();
        if (h < 0)
            return out.candidates.isEmpty(); // achromatic: only as the first one
        for (const QColor &other : std::as_const(out.candidates)) {
            const int oh = other.hue();
            if (oh < 0)
                continue;
            int d = qAbs(h - oh);
            if (d > 180)
                d = 360 - d; // the hue wheel wraps: 350° and 10° are 20° apart
            if (d < WallpaperColors::kMinHueDistance)
                return false;
        }
        return true;
    };

    // Every bucket, not just the first kMaxCandidates: the cap is on how many
    // candidates come *out*, not on how far we look. Capping the scan was a bug
    // — a wallpaper whose eight most populated buckets are all one hue (a
    // screenshot, measured: 56 vivid buckets, every one of them at hue 202°)
    // ended up with a single candidate, so "Generate color" kept producing the
    // same scheme and read as having stopped working.
    for (int key : keys) {
        if (out.candidates.size() >= WallpaperColors::kMaxCandidates)
            break;
        const int n = bucketCount[key];
        const QColor c(int(sumR[key] / n), int(sumG[key] / n), int(sumB[key] / n));
        if (hueFarEnough(c))
            out.candidates.append(c);
    }

    // A genuinely monochromatic wallpaper has one hue, and no amount of looking
    // finds a second — but the button still has to give something new on the
    // next press. Top the list up with *harmonies* of the dominant color: the
    // complementary, the two triadic thirds, and so on. They are still derived
    // from the wallpaper (they are its own hue, rotated by the classic
    // intervals), which is what keeps the result feeling related to the image
    // instead of random. The image's own colors always come first.
    if (!out.candidates.isEmpty()) {
        static const int kHarmonies[] = {180, 120, 240, 30, 330, 150, 210, 60};
        const QColor base = out.candidates.first();
        int h, s, v, a;
        base.getHsv(&h, &s, &v, &a);
        if (h >= 0) {
            for (int rot : kHarmonies) {
                if (out.candidates.size() >= WallpaperColors::kMaxCandidates)
                    break;
                // Keep the dominant's own saturation and value: only the hue
                // moves, so the harmony reads as "the same photo, another
                // note". A floor on saturation stops a washed-out dominant from
                // producing eight identical grays.
                const QColor c = QColor::fromHsv((h + rot) % 360, qMax(s, 120), qMax(v, 90));
                if (hueFarEnough(c))
                    out.candidates.append(c);
            }
        }
    }

    if (out.candidates.isEmpty()) {
        // Nothing vivid anywhere: a grayscale or washed-out wallpaper. The mean
        // is the honest answer, and the tiny saturations buildScheme() applies
        // keep the result neutral instead of inventing a hue. One candidate, so
        // cycling variants on such an image is a no-op rather than a crash.
        out.candidates.append(
            QColor(int(anyR / anyCount), int(anyG / anyCount), int(anyB / anyCount)));
    }
    out.seed = out.candidates.first();
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Contrast
// ---------------------------------------------------------------------------

qreal WallpaperColors::relativeLuminance(const QColor &c)
{
    const auto lin = [](qreal v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

qreal WallpaperColors::contrastRatio(const QColor &a, const QColor &b)
{
    const qreal la = relativeLuminance(a);
    const qreal lb = relativeLuminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

QColor WallpaperColors::ensureContrast(const QColor &fg, const QColor &bg, qreal target)
{
    if (!fg.isValid() || !bg.isValid())
        return fg;

    // Everything here is measured on the 8-bit form, because 8 bits is what
    // ends up in the .colors file and therefore what the user actually sees.
    // QColor keeps more precision internally, so a value that clears the target
    // at full precision can fall under it once rounded — measured: a button
    // that came out at exactly 1.200 landed at 1.193 after rounding, and the
    // test that asserts the contract failed on a colour this function had
    // already declared good.
    const auto round8 = [](const QColor &c) {
        const QColor r = c.toRgb();
        return QColor(r.red(), r.green(), r.blue());
    };
    const QColor bg8 = round8(bg);
    if (contrastRatio(round8(fg), bg8) >= target)
        return round8(fg);

    // Which end of the scale can actually get there, asked directly instead of
    // inferred from the background's luminance: for a mid gray the luminance
    // test picks the wrong end (it is nearer white in luma but contrasts better
    // with black), and the loop below would then walk away from the target.
    const bool lighten =
        contrastRatio(QColor(Qt::white), bg8) >= contrastRatio(QColor(Qt::black), bg8);

    int h, s, v, a;
    fg.getHsv(&h, &s, &v, &a);
    QColor best = round8(fg);
    qreal bestRatio = contrastRatio(best, bg8);

    // Value first, saturation second: dropping saturation is what lets a vivid
    // color reach white at all, but it is the more destructive of the two, so
    // it only starts once the value is already pinned at the end of its range.
    for (int step = 0; step < 128; ++step) {
        if (lighten) {
            if (v < 255)
                v = qMin(255, v + 6);
            else if (s > 0)
                s = qMax(0, s - 6);
            else
                break;
        } else {
            if (v > 0)
                v = qMax(0, v - 6);
            else if (s > 0)
                s = qMax(0, s - 6);
            else
                break;
        }
        const QColor c = round8(QColor::fromHsv(h, s, v, a));
        const qreal r = contrastRatio(c, bg8);
        if (r > bestRatio) {
            bestRatio = r;
            best = c;
        }
        if (r >= target)
            return c;
    }
    // Target out of reach (a mid gray can never be 7:1 against anything): the
    // best available beats pretending, and it is still the most readable choice.
    return best;
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

WallpaperPalette WallpaperColors::sample(const QString &imagePath)
{
    if (imagePath.isEmpty())
        return {};
    const QFileInfo fi(imagePath);
    if (!fi.exists() || !fi.isFile())
        return {};

    // mtime and size in the key: a slideshow that rewrites the same path still
    // gets resampled, and an unchanged wallpaper is never decoded twice.
    const QString key = QStringLiteral("%1|%2|%3")
                            .arg(fi.absoluteFilePath())
                            .arg(fi.lastModified().toMSecsSinceEpoch())
                            .arg(fi.size());

    static QHash<QString, WallpaperPalette> cache;
    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    const WallpaperPalette pal = computePalette(imagePath);
    // Bounded: a slideshow folder can hold thousands of images and this class
    // lives for the whole session. Dropping the lot is fine — the entries are
    // cheap to rebuild and only the current wallpaper is ever asked for twice.
    if (cache.size() > 64)
        cache.clear();
    cache.insert(key, pal);
    return pal;
}

// ---------------------------------------------------------------------------
// Derivation
// ---------------------------------------------------------------------------

SchemeColors WallpaperColors::buildScheme(const WallpaperPalette &palette, const Options &options)
{
    SchemeColors s;
    s.dark = options.lightness == Options::ForceDark
             || (options.lightness == Options::Auto && palette.meanLuma < 0.5);

    // The variant wraps, so a wallpaper with fewer candidates than the click
    // count still answers instead of falling off the end.
    QColor seed(127, 127, 127);
    if (palette.valid && !palette.candidates.isEmpty()) {
        const int n = palette.candidates.size();
        const int idx = ((options.variant % n) + n) % n; // also right for a negative
        seed = palette.candidates.at(idx);
    } else if (palette.valid && palette.seed.isValid()) {
        seed = palette.seed;
    }
    int h, sat, v, a;
    seed.getHsv(&h, &sat, &v, &a);
    if (h < 0)
        h = 0; // achromatic seed: any hue does, the saturations below are tiny

    // The backgrounds are the seed's *hue* at the scheme's own saturation and
    // value band. Keeping the hue and throwing away the seed's own lightness is
    // what makes a scheme that reads as "this wallpaper" without being a wall
    // of saturated color.
    const auto tone = [h](int satPercent, int valPercent) {
        return QColor::fromHsv(h, qBound(0, satPercent * 255 / 100, 255),
                               qBound(0, valPercent * 255 / 100, 255));
    };

    if (s.dark) {
        s.windowBg = tone(18, 16);
        s.viewBg = tone(20, 10);
        s.buttonBg = tone(18, 22);
        s.tooltipBg = tone(18, 20);
        s.headerBg = tone(18, 20);
        s.complementaryBg = tone(20, 13);
    } else {
        s.windowBg = tone(10, 95);
        s.viewBg = tone(5, 100);
        s.buttonBg = tone(12, 89);
        s.tooltipBg = tone(10, 92);
        s.headerBg = tone(12, 92);
        s.complementaryBg = tone(14, 90);
    }
    // A button has to read as a separate surface whatever the hue did to it.
    s.buttonBg = ensureContrast(s.buttonBg, s.windowBg, kButtonContrast);

    const auto textOn = [](const QColor &bg) {
        const QColor base = contrastRatio(kTextLight, bg) >= contrastRatio(kTextDark, bg)
                                ? kTextLight
                                : kTextDark;
        return ensureContrast(base, bg, kTextContrast);
    };

    switch (options.selectionMode) {
    case Options::Custom:
        s.selectionBg = s.dark ? options.selectionDark : options.selectionLight;
        break;
    case Options::FromWallpaper:
        // Complementary hue, forced saturated: the seed's own saturation can be
        // near zero and a gray selection is not a selection.
        s.selectionBg = QColor::fromHsv((h + 180) % 360, qMax(sat, 160), s.dark ? 200 : 130);
        break;
    default:
        // The product decision: light scheme -> dark gray, dark scheme -> light
        // gray. The white/black text that goes with each falls out of textOn().
        s.selectionBg = s.dark ? QColor(0xD6, 0xD6, 0xD6) : QColor(0x3A, 0x3A, 0x3A);
        break;
    }
    if (!s.selectionBg.isValid())
        s.selectionBg = s.dark ? QColor(0xD6, 0xD6, 0xD6) : QColor(0x3A, 0x3A, 0x3A);
    s.selectionBg = ensureContrast(s.selectionBg, s.windowBg, kSelectionContrast);
    s.selectionFg = textOn(s.selectionBg);

    s.windowFg = textOn(s.windowBg);
    s.viewFg = textOn(s.viewBg);
    s.buttonFg = textOn(s.buttonBg);
    s.tooltipFg = textOn(s.tooltipBg);
    s.headerFg = textOn(s.headerBg);
    s.complementaryFg = textOn(s.complementaryBg);

    // The accent every group's DecorationFocus/Hover and ForegroundActive uses.
    // The selection background is what KDE's own schemes put there.
    s.decoration = s.selectionBg;

    // Titlebars: active gets the header shade, inactive the plain window, both
    // with their own readable text.
    s.wmActiveBg = shade(s.windowBg, s.dark ? 3 : -3);
    s.wmActiveFg = textOn(s.wmActiveBg);
    s.wmInactiveBg = s.windowBg;
    s.wmInactiveFg = ensureContrast(blend(s.windowFg, s.windowBg, 0.45), s.windowBg,
                                    kSemanticContrast);

    // Hand every color out rebuilt from its own 8-bit channels, which is a good
    // deal less obvious than it looks. QColor keeps more than 8 bits per channel
    // internally, and operator== compares *that*, not what red()/green()/blue()
    // return: a color out of QColor::fromHsv() and the same color written as
    // "33,36,40" and read back compare **unequal** while printing identically
    // (measured: 8627 vs 8738 in the red channel, both rounding to 0x21).
    // Rounding here makes the in-memory value exactly the one that round-trips
    // through the r,g,b file — without it, DockConfig::setAutoColors() never
    // hits its early return and re-emits on every single refresh, and a test
    // comparing against the written file fails for a reason nothing shows.
    for (QColor *c : {&s.decoration, &s.selectionBg, &s.selectionFg, &s.windowBg, &s.windowFg,
                      &s.viewBg, &s.viewFg, &s.buttonBg, &s.buttonFg, &s.tooltipBg,
                      &s.tooltipFg, &s.headerBg, &s.headerFg, &s.complementaryBg,
                      &s.complementaryFg, &s.wmActiveBg, &s.wmActiveFg, &s.wmInactiveBg,
                      &s.wmInactiveFg})
        *c = QColor(c->red(), c->green(), c->blue());

    return s;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

bool WallpaperColors::writeSchemeFile(const SchemeColors &scheme, const QString &filePath,
                                      const QString &id, const QString &name)
{
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QTextStream out(&file);

    // One color group, in the shape KDE 6 expects. The keys and their order are
    // taken from a shipped scheme (BreezeDark.colors); a group missing keys is
    // not an error but leaves applications falling back to their own defaults
    // for whatever is absent, which reads as "the scheme half applied".
    const auto writeGroup = [&out, &scheme](const QString &header, const QColor &bg,
                                            const QColor &fg) {
        const QColor bgAlt = shade(bg, scheme.dark ? 6 : -4);
        const QColor fgInactive =
            ensureContrast(blend(fg, bg, 0.45), bg, kSemanticContrast);
        out << '[' << header << "]\n";
        out << "BackgroundAlternate=" << rgbTriplet(bgAlt) << '\n';
        out << "BackgroundNormal=" << rgbTriplet(bg) << '\n';
        out << "DecorationFocus=" << rgbTriplet(scheme.decoration) << '\n';
        out << "DecorationHover=" << rgbTriplet(scheme.decoration) << '\n';
        out << "ForegroundActive=" << rgbTriplet(ensureContrast(scheme.decoration, bg,
                                                                kSemanticContrast))
            << '\n';
        out << "ForegroundInactive=" << rgbTriplet(fgInactive) << '\n';
        out << "ForegroundLink=" << rgbTriplet(ensureContrast(kLink, bg, kSemanticContrast))
            << '\n';
        out << "ForegroundNegative="
            << rgbTriplet(ensureContrast(kNegative, bg, kSemanticContrast)) << '\n';
        out << "ForegroundNeutral="
            << rgbTriplet(ensureContrast(kNeutral, bg, kSemanticContrast)) << '\n';
        out << "ForegroundNormal=" << rgbTriplet(fg) << '\n';
        out << "ForegroundPositive="
            << rgbTriplet(ensureContrast(kPositive, bg, kSemanticContrast)) << '\n';
        out << "ForegroundVisited="
            << rgbTriplet(ensureContrast(kVisited, bg, kSemanticContrast)) << '\n';
        out << '\n';
    };

    out << "# Generated by kdock (ColorAuto) from the current wallpaper.\n";
    out << "# Temporary: it is rewritten on every wallpaper change and removed\n";
    out << "# when ColorAuto is turned off. Do not edit.\n\n";

    // Copied verbatim from Breeze: these describe *how* disabled and inactive
    // states are dimmed, not which colors are used, so they carry over.
    out << "[ColorEffects:Disabled]\n";
    out << "Color=56,56,56\n";
    out << "ColorAmount=0\n";
    out << "ColorEffect=0\n";
    out << "ContrastAmount=0.65\n";
    out << "ContrastEffect=1\n";
    out << "IntensityAmount=0.1\n";
    out << "IntensityEffect=2\n\n";

    out << "[ColorEffects:Inactive]\n";
    out << "ChangeSelectionColor=true\n";
    out << "Color=112,111,110\n";
    out << "ColorAmount=0.025\n";
    out << "ColorEffect=2\n";
    out << "ContrastAmount=0.1\n";
    out << "ContrastEffect=2\n";
    out << "Enable=false\n";
    out << "IntensityAmount=0\n";
    out << "IntensityEffect=0\n\n";

    writeGroup(QStringLiteral("Colors:Button"), scheme.buttonBg, scheme.buttonFg);
    writeGroup(QStringLiteral("Colors:Complementary"), scheme.complementaryBg,
               scheme.complementaryFg);
    writeGroup(QStringLiteral("Colors:Header"), scheme.headerBg, scheme.headerFg);
    // KConfig sub-group syntax, and the reason this file is written by hand:
    // "[Colors:Header][Inactive]" is not a section name QSettings can produce.
    writeGroup(QStringLiteral("Colors:Header][Inactive"), scheme.windowBg, scheme.windowFg);
    writeGroup(QStringLiteral("Colors:Selection"), scheme.selectionBg, scheme.selectionFg);
    writeGroup(QStringLiteral("Colors:Tooltip"), scheme.tooltipBg, scheme.tooltipFg);
    writeGroup(QStringLiteral("Colors:View"), scheme.viewBg, scheme.viewFg);
    writeGroup(QStringLiteral("Colors:Window"), scheme.windowBg, scheme.windowFg);

    out << "[General]\n";
    out << "ColorScheme=" << id << '\n';
    out << "Name=" << name << '\n';
    out << "shadeSortColumn=true\n\n";

    out << "[KDE]\n";
    out << "contrast=4\n\n";

    out << "[WM]\n";
    out << "activeBackground=" << rgbTriplet(scheme.wmActiveBg) << '\n';
    out << "activeBlend=" << rgbTriplet(scheme.wmActiveFg) << '\n';
    out << "activeForeground=" << rgbTriplet(scheme.wmActiveFg) << '\n';
    out << "inactiveBackground=" << rgbTriplet(scheme.wmInactiveBg) << '\n';
    out << "inactiveBlend=" << rgbTriplet(scheme.wmInactiveFg) << '\n';
    out << "inactiveForeground=" << rgbTriplet(scheme.wmInactiveFg) << '\n';

    out.flush();
    file.close();
    return file.error() == QFileDevice::NoError;
}
