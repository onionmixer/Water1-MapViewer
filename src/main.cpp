#include "MainWindow.h"
#include "MapPackage.h"
#include "SaveDataReader.h"
#include "SourceDirectory.h"
#include "WorldMapGenerator.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Water1"));
    QCoreApplication::setApplicationName(QStringLiteral("Water1_MapViewer"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Water1 DOS world-map package generator and Qt5 viewer"));
    parser.addHelpOption();
    const QCommandLineOption validateOption(
        QStringList{QStringLiteral("validate-source")},
        QStringLiteral("Validate an original data directory and exit."), QStringLiteral("directory"));
    const QCommandLineOption generateOption(
        QStringList{QStringLiteral("generate")},
        QStringLiteral("Generate a map package without opening the main window."), QStringLiteral("source-directory"));
    const QCommandLineOption outputOption(
        QStringList{QStringLiteral("output")},
        QStringLiteral("Output directory required with --generate."), QStringLiteral("directory"));
    parser.addOption(validateOption);
    parser.addOption(generateOption);
    parser.addOption(outputOption);
    const QCommandLineOption verifyPackageOption(
        QStringList{QStringLiteral("verify-package")},
        QStringLiteral("Verify a generated world_map.json package and exit."), QStringLiteral("metadata-path"));
    parser.addOption(verifyPackageOption);
    const QCommandLineOption openPackageOption(
        QStringList{QStringLiteral("open-package")},
        QStringLiteral("Open a generated world_map.json package in the Qt viewer."), QStringLiteral("metadata-path"));
    parser.addOption(openPackageOption);
    const QCommandLineOption inspectSaveOption(
        QStringList{QStringLiteral("inspect-save")},
        QStringLiteral("Read active treasure locations from an original SAVE.DAT and exit."), QStringLiteral("save-path"));
    parser.addOption(inspectSaveOption);
    parser.process(app);

    if (parser.isSet(validateOption)) {
        const SourceDirectoryReport report = SourceDirectory::validate(parser.value(validateOption));
        QTextStream(stdout) << report.toText();
        return report.valid ? 0 : 1;
    }

    if (parser.isSet(verifyPackageOption)) {
        LoadedMapPackage package;
        QString error;
        if (!MapPackage::load(parser.value(verifyPackageOption), &package, &error)) {
            QTextStream(stderr) << "Package verification failed: " << error << "\n";
            return 1;
        }
        const int candidates = package.metadata.value(QStringLiteral("treasure_model"))
                                   .toObject().value(QStringLiteral("candidate_count")).toInt();
        QTextStream(stdout) << "Package valid\n"
                            << "Image: " << package.imageSize.width() << "x" << package.imageSize.height() << "\n"
                            << "Ports: " << package.ports.size() << "\n"
                            << "Treasure candidates: " << candidates << "\n";
        return 0;
    }

    if (parser.isSet(openPackageOption)) {
        MainWindow window;
        QString error;
        if (!window.openMapPackage(parser.value(openPackageOption), &error)) {
            QTextStream(stderr) << "Cannot open package: " << error << "\n";
            return 1;
        }
        window.show();
        return app.exec();
    }

    if (parser.isSet(inspectSaveOption)) {
        QList<ActiveTreasure> treasures;
        QString error;
        if (!SaveDataReader::readActiveTreasures(parser.value(inspectSaveOption), &treasures, &error)) {
            QTextStream(stderr) << "SAVE.DAT inspection failed: " << error << "\n";
            return 1;
        }
        QTextStream(stdout) << "SAVE.DAT valid; active treasures: " << treasures.size() << "\n";
        for (const ActiveTreasure &treasure : treasures) {
            QTextStream(stdout) << "Slot " << treasure.slot + 1 << " (" << treasure.slotName << "): "
                                << treasure.itemName << " value=" << treasure.itemValue
                                << " sector=(" << treasure.sectorX << "," << treasure.sectorY << ")"
                                << " sub=(" << treasure.subX << "," << treasure.subY << ")\n";
        }
        return 0;
    }

    if (parser.isSet(generateOption)) {
        if (!parser.isSet(outputOption)) {
            QTextStream(stderr) << "--generate requires --output <directory>\n";
            return 2;
        }
        const SourceDirectoryReport report = SourceDirectory::validate(parser.value(generateOption));
        if (!report.valid) {
            QTextStream(stderr) << report.toText();
            return 1;
        }
        auto *generator = new WorldMapGenerator(GenerationRequest{report, parser.value(outputOption)}, &app);
        int generationExitCode = 1;
        QObject::connect(generator, &WorldMapGenerator::generationProgress, &app,
                         [](int complete, int total) {
                             if (complete == total || complete % 72 == 0) {
                                 QTextStream(stdout) << "Rendered " << complete << "/" << total << " sectors\n";
                             }
                         });
        QObject::connect(generator, &WorldMapGenerator::generationSucceeded, &app,
                         [&generationExitCode](const QString &packagePath) {
                             QTextStream(stdout) << "Generated package: " << packagePath << "\n";
                             generationExitCode = 0;
                         });
        QObject::connect(generator, &WorldMapGenerator::generationFailed, &app,
                         [&generationExitCode](const QString &error) {
                             QTextStream(stderr) << "Generation failed: " << error << "\n";
                             generationExitCode = 1;
                         });
        QObject::connect(generator, &QThread::finished, &app,
                         [&app, &generationExitCode] { app.exit(generationExitCode); });
        generator->start();
        return app.exec();
    }

    MainWindow window;
    window.show();
    return app.exec();
}
