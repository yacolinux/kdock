// WallpaperColors — el motor de ColorAuto, sin sesión, sin KWin y sin Plasma.
//
// Es la mitad de la feature que se puede probar en CI: dada una imagen, un color
// dominante, una decisión claro/oscuro y un esquema de KDE 6 completo. Lo que
// queda afuera (leer el wallpaper de plasmashell, aplicarlo, el ping-pong de los
// dos .colors) vive en AutoColorScheme y solo se prueba en la sesión real.
//
// Las imágenes se **generan acá** en vez de venir como PNG de fixture: el test
// afirma sobre el color que sale, así que el valor esperado depende del píxel
// exacto, y un blob binario en el repo esconde justamente eso.

#include "sandbox.h"
#include "wallpapercolors.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

namespace {

// Un PNG liso del color pedido.
QString writeFlat(const QString &dir, const QString &name, const QColor &c,
                  int w = 320, int h = 200)
{
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(c);
    const QString path = dir + QLatin1Char('/') + name;
    return img.save(path, "PNG") ? path : QString();
}

// Dos bandas: `bulk` ocupa la mayor parte y `accent` una franja. Sirve para
// comprobar que el portón de saturación descarta el fondo apagado y se queda
// con el color que de verdad se ve.
QString writeBanded(const QString &dir, const QString &name, const QColor &bulk,
                    const QColor &accent, int accentRows)
{
    QImage img(320, 200, QImage::Format_ARGB32);
    img.fill(bulk);
    for (int y = 0; y < accentRows && y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x)
            img.setPixelColor(x, y, accent);
    }
    const QString path = dir + QLatin1Char('/') + name;
    return img.save(path, "PNG") ? path : QString();
}

// Una candidata es el **promedio** de su cubeta, no un píxel: en una imagen con
// varias zonas, los píxeles que el remuestreo mezcla en los bordes caen en la
// cubeta de al lado y corren esa media uno o dos puntos. Comparar exacto solo
// vale para una imagen lisa.
bool nearly(const QColor &a, const QColor &b, int tol = 4)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol
           && qAbs(a.blue() - b.blue()) <= tol;
}

// Los grupos que un .colors de KDE 6 tiene que traer. Verificados contra
// /opt/kde/share/color-schemes/BreezeDark.colors: si falta uno, las apps caen a
// sus propios defaults para esa parte y se ve como "el esquema se aplicó a
// medias".
const QStringList kRequiredGroups = {
    QStringLiteral("[ColorEffects:Disabled]"), QStringLiteral("[ColorEffects:Inactive]"),
    QStringLiteral("[Colors:Button]"),         QStringLiteral("[Colors:Complementary]"),
    QStringLiteral("[Colors:Header]"),         QStringLiteral("[Colors:Header][Inactive]"),
    QStringLiteral("[Colors:Selection]"),      QStringLiteral("[Colors:Tooltip]"),
    QStringLiteral("[Colors:View]"),           QStringLiteral("[Colors:Window]"),
    QStringLiteral("[General]"),               QStringLiteral("[KDE]"),
    QStringLiteral("[WM]")};

} // namespace

class TestWallpaperColors : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_blue = writeFlat(m_dir.path(), QStringLiteral("blue.png"), QColor(30, 90, 200));
        QVERIFY(!m_blue.isEmpty());
        m_bright = writeFlat(m_dir.path(), QStringLiteral("bright.png"), QColor(240, 225, 120));
        QVERIFY(!m_bright.isEmpty());
        m_gray = writeFlat(m_dir.path(), QStringLiteral("gray.png"), QColor(128, 128, 128));
        QVERIFY(!m_gray.isEmpty());
        m_darkPhoto = writeFlat(m_dir.path(), QStringLiteral("darkphoto.png"), QColor(20, 30, 45));
        QVERIFY(!m_darkPhoto.isEmpty());
    }

    // ---- Muestreo ---------------------------------------------------------

    void sampleFlatColor()
    {
        const WallpaperPalette p = WallpaperColors::sample(m_blue);
        QVERIFY(p.valid);
        // La cubeta es de 3 bits por canal, así que el promedio del cubo ganador
        // es el color exacto de una imagen lisa.
        QCOMPARE(p.seed, QColor(30, 90, 200));
        // 0.299*30 + 0.587*90 + 0.114*200 = 84.6 -> 0.33
        QVERIFY(p.meanLuma > 0.30 && p.meanLuma < 0.36);
    }

    void sampleMissingFile()
    {
        // Un wallpaper que no se puede leer NO puede devolver un color: el
        // llamador tiene que poder distinguirlo y dejar el escritorio en paz.
        QVERIFY(!WallpaperColors::sample(m_dir.path() + QStringLiteral("/nope.png")).valid);
        QVERIFY(!WallpaperColors::sample(QString()).valid);
        QVERIFY(!WallpaperColors::sample(m_dir.path()).valid); // un directorio
    }

    void sampleGrayFallsBackToMean()
    {
        // Sin un solo píxel vivo (el portón descarta todo lo que tenga
        // saturación < ~0.25) el acumulador de respaldo es el que contesta, y
        // tiene que contestar algo válido en vez de dejar la feature muerta.
        const WallpaperPalette p = WallpaperColors::sample(m_gray);
        QVERIFY(p.valid);
        QVERIFY(p.seed.isValid());
        QCOMPARE(p.seed, QColor(128, 128, 128));
    }

    void sampleIgnoresDullBulkForVividAccent()
    {
        // El fondo casi gris ocupa el 90% de la imagen y aun así no gana: el
        // color que una persona diría que "tiene" este wallpaper es la franja
        // saturada. Es exactamente el criterio de IconColorProvider.
        const QString path = writeBanded(m_dir.path(), QStringLiteral("banded.png"),
                                         QColor(70, 72, 74), QColor(200, 40, 60), 20);
        QVERIFY(!path.isEmpty());
        const WallpaperPalette p = WallpaperColors::sample(path);
        QVERIFY(p.valid);
        QCOMPARE(p.seed, QColor(200, 40, 60));
    }

    void sampleCacheFollowsFileChanges()
    {
        // La caché tiene mtime y tamaño en la clave. Un slideshow que reescribe
        // la misma ruta tiene que volver a muestrear, o el esquema se congela en
        // el primer fondo del día.
        const QString path = writeFlat(m_dir.path(), QStringLiteral("mut.png"),
                                       QColor(200, 40, 60));
        QVERIFY(!path.isEmpty());
        QCOMPARE(WallpaperColors::sample(path).seed, QColor(200, 40, 60));

        // Otro color *y* otro tamaño: así la clave cambia aunque el sistema de
        // archivos tenga una resolución de mtime gruesa.
        QVERIFY(!writeFlat(m_dir.path(), QStringLiteral("mut.png"), QColor(40, 180, 90),
                           400, 260)
                     .isEmpty());
        QCOMPARE(WallpaperColors::sample(path).seed, QColor(40, 180, 90));
    }

    // ---- Colores alternativos ---------------------------------------------

    void candidatesAreRankedAndDistinct()
    {
        // Tres franjas de colores muy distintos, de más a menos superficie: el
        // orden de las candidatas tiene que seguir esa población, porque es lo
        // que hace que la variante 0 sea "el color del fondo" y las siguientes
        // sean alternativas razonables y no ruido.
        QImage img(300, 300, QImage::Format_ARGB32);
        img.fill(QColor(200, 40, 60));                                  // más
        for (int y = 0; y < 100; ++y)
            for (int x = 0; x < 300; ++x)
                img.setPixelColor(x, y, QColor(40, 80, 200));           // medio
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 300; ++x)
                img.setPixelColor(x, y, QColor(40, 200, 90));           // menos
        const QString path = m_dir.path() + QStringLiteral("/ranked.png");
        QVERIFY(img.save(path, "PNG"));

        const WallpaperPalette p = WallpaperColors::sample(path);
        QVERIFY(p.valid);
        // Tres o más, no exactamente tres: la imagen se decodifica a 128x128 y
        // el remuestreo mezcla los píxeles del borde entre franjas, lo que deja
        // cubetas intermedias de poca población. Eso es correcto —una foto real
        // tiene degradés por todos lados— y por eso lo que se afirma es el
        // *orden* de las tres que importan, no el conteo.
        QVERIFY(p.candidates.size() >= 3);
        QVERIFY(nearly(p.candidates.at(0), QColor(200, 40, 60)));
        QVERIFY(nearly(p.candidates.at(1), QColor(40, 80, 200)));
        QVERIFY(nearly(p.candidates.at(2), QColor(40, 200, 90)));
        // seed es la primera por contrato: todo el camino automático lo asume.
        QCOMPARE(p.seed, p.candidates.first());
        // Y no se repiten, o ciclar daría el mismo esquema dos veces seguidas.
        QCOMPARE(QSet<QRgb>({p.candidates.at(0).rgb(), p.candidates.at(1).rgb(),
                             p.candidates.at(2).rgb()})
                     .size(),
                 3);
    }

    void candidatesAreCapped()
    {
        // Un degradé llena muchísimas cubetas; la paleta no puede crecer sin
        // límite porque vive en la caché de muestreo toda la sesión.
        QImage img(256, 256, QImage::Format_ARGB32);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
                img.setPixelColor(x, y, QColor::fromHsv(x, 200, 128 + (y >> 1)));
        const QString path = m_dir.path() + QStringLiteral("/grad.png");
        QVERIFY(img.save(path, "PNG"));

        const WallpaperPalette p = WallpaperColors::sample(path);
        QVERIFY(p.valid);
        QVERIFY(p.candidates.size() <= WallpaperColors::kMaxCandidates);
        QVERIFY(!p.candidates.isEmpty());
    }

    void grayWallpaperStillHasOneCandidate()
    {
        // Sin un solo píxel vivo, ciclar variantes tiene que ser inocuo (el
        // mismo color) y nunca dividir por cero.
        const WallpaperPalette p = WallpaperColors::sample(m_gray);
        QVERIFY(p.valid);
        QCOMPARE(p.candidates.size(), 1);
        WallpaperColors::Options opt;
        opt.variant = 5;
        const SchemeColors s = WallpaperColors::buildScheme(p, opt);
        QVERIFY(s.windowBg.isValid());
    }

    void candidatesAreSeparatedByHue()
    {
        // Cuatro azules apenas distintos: sin separación por tono las cubetas
        // más votadas serían las cuatro, y como el fondo se arma con el TONO a
        // saturación y valor fijos, ciclar entre ellas daría cuatro fondos
        // idénticos. Medido en vivo antes de arreglarlo: 33,37,40 contra
        // 33,38,40 — un punto de diferencia, o sea nada.
        QImage img(320, 320, QImage::Format_ARGB32);
        const QList<QColor> blues = {QColor(25, 39, 47), QColor(71, 116, 143),
                                     QColor(51, 87, 109), QColor(38, 66, 84)};
        for (int i = 0; i < blues.size(); ++i)
            for (int y = i * 80; y < (i + 1) * 80; ++y)
                for (int x = 0; x < 320; ++x)
                    img.setPixelColor(x, y, blues.at(i));
        const QString path = m_dir.path() + QStringLiteral("/blues.png");
        QVERIFY(img.save(path, "PNG"));

        const WallpaperPalette p = WallpaperColors::sample(path);
        QVERIFY(p.valid);
        // De los cuatro azules sobrevive uno solo —son el mismo tono— y ese es
        // el que manda.
        QVERIFY(nearly(p.candidates.first(), QColor(25, 39, 47), 12));

        // Pero la lista NO se queda en uno: un fondo monocromático igual tiene
        // que dar variantes, o el botón "Generar Color" deja de hacer nada a
        // partir del segundo clic (reportado 2026-08-12). Las de más son
        // armonías del dominante — su mismo tono, rotado.
        QVERIFY2(p.candidates.size() > 1,
                 "un fondo de un solo tono se quedó sin variantes");

        // Y con tonos de verdad distintos sí entran varias, separadas.
        for (int i = 0; i < p.candidates.size(); ++i) {
            for (int j = i + 1; j < p.candidates.size(); ++j) {
                int d = qAbs(p.candidates.at(i).hue() - p.candidates.at(j).hue());
                if (d > 180)
                    d = 360 - d;
                QVERIFY(d >= WallpaperColors::kMinHueDistance);
            }
        }
    }

    void variantPicksAnotherCandidate()
    {
        const QString path = writeBanded(m_dir.path(), QStringLiteral("twotone.png"),
                                         QColor(200, 40, 60), QColor(40, 80, 200), 60);
        QVERIFY(!path.isEmpty());
        const WallpaperPalette p = WallpaperColors::sample(path);
        QVERIFY(p.candidates.size() >= 2);

        WallpaperColors::Options a, b;
        a.variant = 0;
        b.variant = 1;
        // Distinto color de fondo: es lo único que hace que un segundo clic en
        // "Generar Color" se vea.
        QVERIFY(WallpaperColors::buildScheme(p, a).windowBg
                != WallpaperColors::buildScheme(p, b).windowBg);
    }

    void everyVariantOfAMonochromeWallpaperDiffers()
    {
        // El caso del usuario, medido: una captura de pantalla con 56 cubetas
        // vívidas, todas en el tono 202°. Antes daba una sola candidata y ocho
        // clics en "Generar Color" daban ocho veces el mismo esquema — lo que
        // se ve exactamente como "el botón dejó de funcionar".
        const WallpaperPalette p = WallpaperColors::sample(m_blue); // liso, un tono
        QVERIFY(p.valid);
        QSet<QRgb> backgrounds;
        for (int v = 0; v < p.candidates.size(); ++v) {
            WallpaperColors::Options opt;
            opt.variant = v;
            backgrounds.insert(WallpaperColors::buildScheme(p, opt).windowBg.rgb());
        }
        // Un color de fondo distinto por variante: si dos coinciden, ese clic
        // no se ve.
        QCOMPARE(backgrounds.size(), p.candidates.size());
        QVERIFY(p.candidates.size() >= 4);
    }

    void variantWraps()
    {
        // Más clics que candidatas vuelve al principio en vez de caerse.
        const WallpaperPalette p = WallpaperColors::sample(m_blue);
        WallpaperColors::Options first, wrapped;
        first.variant = 0;
        wrapped.variant = p.candidates.size();
        QCOMPARE(WallpaperColors::buildScheme(p, wrapped).windowBg,
                 WallpaperColors::buildScheme(p, first).windowBg);
    }

    // ---- Claro / oscuro ---------------------------------------------------

    void lightnessFollowsWallpaper()
    {
        WallpaperColors::Options opt; // Auto
        QVERIFY(WallpaperColors::buildScheme(WallpaperColors::sample(m_darkPhoto), opt).dark);
        QVERIFY(!WallpaperColors::buildScheme(WallpaperColors::sample(m_bright), opt).dark);
    }

    void lightnessCanBeForced()
    {
        // El combo de la solapa existe porque una foto justo en el umbral hace
        // titilar el esquema entre los dos en cada paso del slideshow.
        const WallpaperPalette darkPal = WallpaperColors::sample(m_darkPhoto);
        const WallpaperPalette lightPal = WallpaperColors::sample(m_bright);

        WallpaperColors::Options forceLight;
        forceLight.lightness = WallpaperColors::Options::ForceLight;
        QVERIFY(!WallpaperColors::buildScheme(darkPal, forceLight).dark);

        WallpaperColors::Options forceDark;
        forceDark.lightness = WallpaperColors::Options::ForceDark;
        QVERIFY(WallpaperColors::buildScheme(lightPal, forceDark).dark);
    }

    // ---- Contraste --------------------------------------------------------

    void contrastRatioMatchesWcag()
    {
        // Los dos extremos conocidos de la fórmula, como control de que la
        // linearización sRGB está bien: blanco contra negro es 21:1 y cualquier
        // color contra sí mismo es 1:1.
        QVERIFY(qAbs(WallpaperColors::contrastRatio(Qt::white, Qt::black) - 21.0) < 0.01);
        QVERIFY(qAbs(WallpaperColors::contrastRatio(QColor(30, 90, 200), QColor(30, 90, 200))
                     - 1.0)
                < 0.001);
    }

    void ensureContrastReachesTarget()
    {
        // Un gris medio sobre otro gris medio no contrasta con nada; empujarlo
        // tiene que llegar al objetivo, no acercarse.
        const QColor bg(120, 120, 120);
        const QColor pushed = WallpaperColors::ensureContrast(QColor(125, 125, 125), bg, 4.5);
        QVERIFY(WallpaperColors::contrastRatio(pushed, bg) >= 4.5);
    }

    void ensureContrastPicksTheReachableEnd()
    {
        // El caso que motiva no deducir la dirección de la luminancia del fondo:
        // un gris medio está más cerca del blanco en luminancia pero contrasta
        // mucho mejor con el negro. Deducirlo mal camina para el lado que no es.
        const QColor bg(128, 128, 128);
        const QColor out = WallpaperColors::ensureContrast(QColor(130, 130, 130), bg, 7.0);
        QVERIFY(WallpaperColors::relativeLuminance(out)
                < WallpaperColors::relativeLuminance(bg));
    }

    void ensureContrastReturnsBestEffort()
    {
        // 21:1 contra un gris medio no existe. Devolver lo mejor alcanzado es
        // más honesto que fingir, y sigue siendo lo más legible que hay.
        const QColor bg(128, 128, 128);
        const QColor out = WallpaperColors::ensureContrast(QColor(120, 120, 120), bg, 21.0);
        QVERIFY(out.isValid());
        QVERIFY(WallpaperColors::contrastRatio(out, bg)
                > WallpaperColors::contrastRatio(QColor(120, 120, 120), bg));
    }

    // Lo que de verdad importa de todo el motor: salga la foto que salga, el
    // esquema generado se puede leer. Se corre sobre las cuatro imágenes y las
    // dos claridades forzadas.
    void generatedSchemesAreReadable()
    {
        const QStringList images = {m_blue, m_bright, m_gray, m_darkPhoto};
        const QList<int> modes = {WallpaperColors::Options::Auto,
                                  WallpaperColors::Options::ForceLight,
                                  WallpaperColors::Options::ForceDark};
        for (const QString &image : images) {
            for (int mode : modes) {
              const WallpaperPalette pal = WallpaperColors::sample(image);
              QVERIFY(pal.valid);
              // Todas las variantes, no solo la predominante: el botón "Generar
              // Color" cicla por ellas y cualquiera puede terminar siendo el
              // esquema del escritorio, así que la garantía de legibilidad vale
              // para todas o no vale para ninguna.
              for (int variant = 0; variant < pal.candidates.size(); ++variant) {
                WallpaperColors::Options opt;
                opt.lightness = mode;
                opt.variant = variant;
                const SchemeColors s = WallpaperColors::buildScheme(pal, opt);

                const QString ctx = QStringLiteral("%1 modo %2 variante %3")
                                        .arg(QFileInfo(image).fileName())
                                        .arg(mode)
                                        .arg(variant);

                // Texto: AAA de cuerpo de texto sobre cada superficie.
                for (const auto &pair : {qMakePair(s.windowFg, s.windowBg),
                                         qMakePair(s.viewFg, s.viewBg),
                                         qMakePair(s.buttonFg, s.buttonBg),
                                         qMakePair(s.tooltipFg, s.tooltipBg),
                                         qMakePair(s.headerFg, s.headerBg),
                                         qMakePair(s.complementaryFg, s.complementaryBg),
                                         qMakePair(s.selectionFg, s.selectionBg)}) {
                    QVERIFY2(WallpaperColors::contrastRatio(pair.first, pair.second)
                                 >= WallpaperColors::kTextContrast,
                             qPrintable(ctx));
                }
                // El botón tiene que leerse como una superficie aparte.
                QVERIFY2(WallpaperColors::contrastRatio(s.buttonBg, s.windowBg)
                             >= WallpaperColors::kButtonContrast,
                         qPrintable(ctx));
                // Y la selección no puede fundirse con el fondo.
                QVERIFY2(WallpaperColors::contrastRatio(s.selectionBg, s.windowBg)
                             >= WallpaperColors::kSelectionContrast,
                         qPrintable(ctx));
              }
            }
        }
    }

    // ---- Color de selección ------------------------------------------------

    void defaultSelectionIsTheSpecifiedGrays()
    {
        // El pedido, literal: esquema claro -> gris oscuro con fuente blanca;
        // esquema oscuro -> gris claro con fuente negra. La fuente no es un caso
        // especial, sale sola de la regla de contraste.
        WallpaperColors::Options opt;
        opt.lightness = WallpaperColors::Options::ForceLight;
        const SchemeColors light = WallpaperColors::buildScheme(
            WallpaperColors::sample(m_blue), opt);
        QVERIFY(WallpaperColors::relativeLuminance(light.selectionBg) < 0.15); // gris oscuro
        QVERIFY(WallpaperColors::relativeLuminance(light.selectionFg) > 0.7);  // fuente clara

        opt.lightness = WallpaperColors::Options::ForceDark;
        const SchemeColors dark = WallpaperColors::buildScheme(
            WallpaperColors::sample(m_blue), opt);
        QVERIFY(WallpaperColors::relativeLuminance(dark.selectionBg) > 0.6);  // gris claro
        QVERIFY(WallpaperColors::relativeLuminance(dark.selectionFg) < 0.05); // fuente oscura
    }

    void customSelectionIsHonoured()
    {
        WallpaperColors::Options opt;
        opt.lightness = WallpaperColors::Options::ForceLight;
        opt.selectionMode = WallpaperColors::Options::Custom;
        opt.selectionLight = QColor(120, 20, 20);
        const SchemeColors s =
            WallpaperColors::buildScheme(WallpaperColors::sample(m_blue), opt);
        // Se respeta el tono elegido (el rojo sigue siendo el canal dominante),
        // aunque el contraste lo haya podido mover de valor.
        QVERIFY(s.selectionBg.red() > s.selectionBg.green());
        QVERIFY(s.selectionBg.red() > s.selectionBg.blue());
    }

    void wallpaperSelectionIsComplementary()
    {
        WallpaperColors::Options opt;
        opt.selectionMode = WallpaperColors::Options::FromWallpaper;
        const WallpaperPalette pal = WallpaperColors::sample(m_blue); // azul
        const SchemeColors s = WallpaperColors::buildScheme(pal, opt);
        // El complementario de un azul es un naranja/amarillo: el canal rojo
        // pasa al frente. Y sigue teniendo que contrastar contra el fondo.
        QVERIFY(s.selectionBg.red() > s.selectionBg.blue());
        QVERIFY(WallpaperColors::contrastRatio(s.selectionBg, s.windowBg)
                >= WallpaperColors::kSelectionContrast);
    }

    // ---- El archivo -------------------------------------------------------

    void writtenSchemeHasEveryGroup()
    {
        const SchemeColors s = WallpaperColors::buildScheme(WallpaperColors::sample(m_blue),
                                                            WallpaperColors::Options());
        const QString path = m_dir.path() + QStringLiteral("/out/KdockTest.colors");
        QVERIFY(WallpaperColors::writeSchemeFile(s, path, QStringLiteral("KdockTest"),
                                                 QStringLiteral("kdock test")));
        QVERIFY(QFile::exists(path)); // y creó el directorio

        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(f.readAll());
        for (const QString &group : kRequiredGroups)
            QVERIFY2(text.contains(group + QLatin1Char('\n')), qPrintable(group));

        // Lo que aplica el esquema lo busca por este nombre.
        QVERIFY(text.contains(QStringLiteral("ColorScheme=KdockTest\n")));
        QVERIFY(text.contains(QStringLiteral("Name=kdock test\n")));
    }

    void writtenColorsAreUnquotedTriplets()
    {
        // La razón por la que el archivo se escribe a mano y no con QSettings:
        // QSettings entrecomilla cualquier valor con comas, y todos los colores
        // de este formato son "r,g,b". Un '"' acá rompe el parseo de KConfig.
        const SchemeColors s = WallpaperColors::buildScheme(WallpaperColors::sample(m_blue),
                                                            WallpaperColors::Options());
        const QString path = m_dir.path() + QStringLiteral("/out/Quoting.colors");
        QVERIFY(WallpaperColors::writeSchemeFile(s, path, QStringLiteral("Quoting"),
                                                 QStringLiteral("q")));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));

        int triplets = 0;
        for (const QString &line : lines) {
            if (!line.startsWith(QLatin1String("BackgroundNormal="))
                && !line.startsWith(QLatin1String("ForegroundNormal="))
                && !line.startsWith(QLatin1String("activeBackground=")))
                continue;
            const QString value = line.section(QLatin1Char('='), 1);
            QVERIFY2(!value.contains(QLatin1Char('"')), qPrintable(line));
            const QStringList parts = value.split(QLatin1Char(','));
            QCOMPARE(parts.size(), 3);
            for (const QString &part : parts) {
                bool ok = false;
                const int n = part.toInt(&ok);
                QVERIFY2(ok && n >= 0 && n <= 255, qPrintable(line));
            }
            ++triplets;
        }
        QVERIFY(triplets >= 8); // uno por grupo de color, más el WM
    }

    void writtenSchemeReadsBackThroughQSettings()
    {
        // AppearanceControl lista los esquemas leyéndolos con QSettings, así que
        // el generado tiene que aparecer en el picker de kdock como cualquier
        // otro — con su nombre y sus tres colores de vista previa.
        const SchemeColors s = WallpaperColors::buildScheme(WallpaperColors::sample(m_blue),
                                                            WallpaperColors::Options());
        const QString path = m_dir.path() + QStringLiteral("/out/Readback.colors");
        QVERIFY(WallpaperColors::writeSchemeFile(s, path, QStringLiteral("Readback"),
                                                 QStringLiteral("kdock readback")));

        QSettings ini(path, QSettings::IniFormat);
        // Ojo: "Name", no "General/Name" — QSettings mapea [General] a la raíz.
        QCOMPARE(ini.value(QStringLiteral("Name")).toString(),
                 QStringLiteral("kdock readback"));
        const QStringList bg =
            ini.value(QStringLiteral("Colors:Window/BackgroundNormal")).toStringList();
        QCOMPARE(bg.size(), 3);
        // Comparación directa a propósito: buildScheme() devuelve los colores en
        // spec RGB justamente para que esto valga. QColor::operator== compara el
        // spec, así que un color que quedara en Hsv fallaría acá aunque los tres
        // canales fueran idénticos.
        QCOMPARE(QColor(bg.at(0).toInt(), bg.at(1).toInt(), bg.at(2).toInt()), s.windowBg);
    }

    void schemeColorsAreRgbSpec()
    {
        // El invariante que hace que la comparación de arriba (y el early return
        // de DockConfig::setAutoColors) tengan sentido.
        const SchemeColors s = WallpaperColors::buildScheme(WallpaperColors::sample(m_blue),
                                                            WallpaperColors::Options());
        for (const QColor &c : {s.windowBg, s.windowFg, s.selectionBg, s.buttonBg,
                                s.decoration, s.wmActiveBg})
            QCOMPARE(c.spec(), QColor::Rgb);
    }

private:
    QTemporaryDir m_dir;
    QString m_blue, m_bright, m_gray, m_darkPhoto;
};

KDOCK_TEST_MAIN(TestWallpaperColors)
#include "tst_wallpapercolors.moc"
