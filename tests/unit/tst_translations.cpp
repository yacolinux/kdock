// La capa de traducciones: sembrado, merge por clave y respeto por lo editado.
//
// El caso que motiva el archivo es un bug real reportado por el usuario
// (2026-08-07): seedFiles() copia del qrc "solo lo que falta" a nivel de archivo
// completo, así que un english.md sembrado antes de que existieran los paneles
// de kdock-previews/kdock-tilemenu se quedaba sin esas claves **para siempre** —
// el catálogo del repo las tenía y los paneles igual salían sin traducir. Se
// arregló con un merge por clave; esto lo fija.

#include "translations.h"
#include "sandbox.h"

#include <QDir>
#include <QFile>
#include <QTest>

namespace {
QStringList readLines(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
}

// El valor de una clave en un .md, o vacío.
QString entryValue(const QString &path, const QString &key)
{
    for (const QString &line : readLines(path)) {
        if (line.startsWith(key + QStringLiteral(" = ")))
            return line.mid(key.size() + 3);
    }
    return {};
}
} // namespace

class TestTranslations : public QObject
{
    Q_OBJECT

private slots:
    void seedsTheBundledFilesOnFirstRun()
    {
        // Sin esto el usuario no tiene con qué traducir: los .md salen del qrc.
        // De paso comprueba que el recurso de kdock_core se inicializa cuando la
        // object library se linkea en otro ejecutable (el test).
        Translations layer;
        const QStringList available = layer.available();
        QVERIFY2(available.contains(QStringLiteral("capabase")), "falta capabase");
        QVERIFY(available.contains(QStringLiteral("spanish")));
        QVERIFY(available.contains(QStringLiteral("english")));
        QVERIFY(QFile::exists(Translations::filePathFor(QStringLiteral("spanish"))));
    }

    void topsUpAStaleUserFileWithoutTouchingItsEdits()
    {
        // Un archivo de usuario viejo: tiene la sección pero le faltan claves, y
        // trae una traducción propia que no se puede perder.
        const QString path = Translations::filePathFor(QStringLiteral("english"));
        const QString mine = QStringLiteral("Volume:");
        const QString myText = QStringLiteral("MI TRADUCCION PROPIA");

        QFile::remove(path);
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write(QStringLiteral("## Configuracion\n%1 = %2\n\n## UIdock\n\n## Widgets\n")
                        .arg(mine, myText)
                        .toUtf8());
        }

        Translations layer; // el merge corre al construir

        QCOMPARE(entryValue(path, mine), myText); // lo editado queda intacto
        const int lines = readLines(path).size();
        QVERIFY2(lines > 10, "el archivo viejo tendría que haberse completado con las claves nuevas");
        // Y una clave que el binario conoce y el archivo no tenía:
        QVERIFY2(!entryValue(path, QStringLiteral("Dock settings…")).isEmpty()
                     || !entryValue(path, QStringLiteral("Hide mode:")).isEmpty(),
                 "no entró ninguna clave nueva al archivo viejo");
    }

    void neverOverwritesAFileWholesale()
    {
        // El .md de disco es del usuario: sembrar de nuevo no puede pisarlo.
        const QString path = Translations::filePathFor(QStringLiteral("spanish"));
        { Translations first; }
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::Append | QIODevice::Text));
            f.write("\n## Apps\nmi.app.propia = Mi App\n");
        }
        { Translations second; }
        QCOMPARE(entryValue(path, QStringLiteral("mi.app.propia")), QStringLiteral("Mi App"));
    }

    void noCatalogueKeyContainsTheSeparator()
    {
        // El formato es `clave = valor` y se parte por el PRIMER " = ", así que
        // una clave con " = " adentro no se puede traducir nunca. El chequeo
        // estático lo mira en el código; esto lo mira en el catálogo servido.
        { Translations seed; }
        const QString path = Translations::filePathFor(QStringLiteral("capabase"));
        QStringList bad;
        for (const QString &line : readLines(path)) {
            if (line.startsWith(QLatin1String("#")) || !line.contains(QLatin1String(" = ")))
                continue;
            const QString key = line.section(QStringLiteral(" = "), 0, 0);
            if (key.contains(QLatin1String(" = ")))
                bad << key;
        }
        QVERIFY2(bad.isEmpty(), qPrintable(bad.join(QStringLiteral(", "))));
    }

    void altLayersFallBackToTheirBaseForTheAccessories()
    {
        // Los accesorios siguen el idioma, no el chiste: english-ALT-hacker les
        // tiene que dar english.
        QCOMPARE(Translations::baseLanguage(QStringLiteral("english-ALT-hacker")),
                 QStringLiteral("english"));
        QCOMPARE(Translations::baseLanguage(QStringLiteral("spanish")),
                 QStringLiteral("spanish"));
    }
};

KDOCK_TEST_MAIN(TestTranslations)
#include "tst_translations.moc"
