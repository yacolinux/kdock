// Turns a wallpaper into a valid KDE 6 color scheme. Pure: no D-Bus, no config,
// no UI, no KDE Frameworks — everything here is a function of its arguments, so
// the whole engine is exercised by tests/unit/tst_wallpapercolors.cpp without a
// session, without KWin and without Plasma. The part that talks to the desktop
// is AutoColorScheme.
//
// The sampler is deliberately the same algorithm the dock already uses to tint
// an app icon (IconColorProvider::compute): coarse RGB buckets, three bits per
// channel, most populated bucket wins, with a saturation/value gate that throws
// away gray, near-black and near-white. It is fast and its output is already
// known to look right on this desktop. Two things differ:
//
//   - The image comes off disk through QImageReader::setScaledSize() rather
//     than QImage::load(). For a JPEG that goes through libjpeg's scale_denom,
//     so a 4K wallpaper is never materialised at full size — this is where the
//     speed of the whole feature lives, not in the pixel loop.
//   - The same pass accumulates the mean luminance, which costs nothing and is
//     what decides whether the generated scheme comes out light or dark.
//
// Derivation is contrast-first: every foreground, and the button background,
// are pushed until they meet a WCAG contrast ratio against what they sit on
// (see the k*Contrast constants). That is what keeps a scheme built from an
// arbitrary photo readable instead of merely colorful.

#pragma once

#include <QColor>
#include <QString>

struct WallpaperPalette
{
    // Predominant color of the image, the seed every background derives from.
    QColor seed;
    // Mean perceptual luminance of the image, 0..1. Decides light vs dark.
    qreal meanLuma = 0.0;
    bool valid = false;
};

// One resolved scheme. The per-group extras a .colors file needs (alternate
// backgrounds, inactive foregrounds, the semantic red/green/orange) are derived
// from these when the file is written, so nothing here has to carry 12 keys ×
// 7 groups.
struct SchemeColors
{
    bool dark = false;
    // DecorationFocus / DecorationHover / ForegroundActive: the accent.
    QColor decoration;
    QColor selectionBg, selectionFg;
    QColor windowBg, windowFg;
    QColor viewBg, viewFg;
    QColor buttonBg, buttonFg;
    QColor tooltipBg, tooltipFg;
    QColor headerBg, headerFg;
    QColor complementaryBg, complementaryFg;
    QColor wmActiveBg, wmActiveFg, wmInactiveBg, wmInactiveFg;
};

class WallpaperColors
{
public:
    struct Options
    {
        // Whether the scheme follows the wallpaper's own brightness or is
        // pinned. Auto is the default; the other two exist because a photo
        // sitting right on the threshold otherwise flips between two schemes on
        // every slideshow step.
        enum Lightness { Auto = 0, ForceLight = 1, ForceDark = 2 };
        // Where the selection color comes from. DefaultGrays is the product
        // decision: a light scheme selects with a dark gray (white text), a dark
        // scheme with a light gray (black text) — the text side falls out of the
        // contrast rule, it is not a special case.
        enum SelectionMode { DefaultGrays = 0, Custom = 1, FromWallpaper = 2 };

        int lightness = Auto;
        int selectionMode = DefaultGrays;
        // Used in Custom mode only, one per resulting scheme kind.
        QColor selectionLight = QColor(0x3A, 0x3A, 0x3A); // for a LIGHT scheme
        QColor selectionDark = QColor(0xD6, 0xD6, 0xD6);  // for a DARK scheme
    };

    // Longest edge the image is decoded at. 128 px is ~16k pixels: the bucket
    // loop is noise next to the decode, and going smaller starts to lose small
    // but saturated regions that are exactly what reads as "the color of this
    // wallpaper".
    static constexpr int kSampleSize = 128;

    // Contrast targets, all WCAG ratios against the background the thing sits
    // on. 7.0 is the AAA level for body text; 1.2 is just enough for a button
    // to read as a raised surface; 3.0 keeps the selection from melting into
    // the window; 4.5 (AA) is for the semantic colors, which have to stay
    // recognisable as red/green/orange and so cannot be pushed as far.
    static constexpr qreal kTextContrast = 7.0;
    static constexpr qreal kButtonContrast = 1.2;
    static constexpr qreal kSelectionContrast = 3.0;
    static constexpr qreal kSemanticContrast = 4.5;

    // Sample an image file. Cached by path + mtime + size, so the same
    // wallpaper is never decoded twice; an invalid/unreadable path comes back
    // with valid == false and callers leave the desktop alone.
    static WallpaperPalette sample(const QString &imagePath);

    static SchemeColors buildScheme(const WallpaperPalette &palette, const Options &options);

    // Write a KDE 6 .colors file. `id` is the base name the scheme is applied
    // by (and the [General] ColorScheme value), `name` its display name.
    // Creates the parent directory. Written by hand and not with QSettings on
    // purpose: QSettings quotes any value containing a comma, and every color
    // in this format is "r,g,b".
    static bool writeSchemeFile(const SchemeColors &scheme, const QString &filePath,
                                const QString &id, const QString &name);

    // WCAG relative luminance and contrast ratio.
    static qreal relativeLuminance(const QColor &c);
    static qreal contrastRatio(const QColor &a, const QColor &b);
    // Push `fg` away from `bg` until the ratio reaches `target`, by value first
    // and saturation second. Returns the best it could reach when the target is
    // out of range rather than pretending it succeeded.
    static QColor ensureContrast(const QColor &fg, const QColor &bg, qreal target);
};
