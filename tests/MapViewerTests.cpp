#include "MapCanvas.h"
#include "MapPackage.h"
#include "MainWindow.h"
#include "SaveDataReader.h"
#include "SourceDirectory.h"
#include "WorldMapGenerator.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalSpy>
#include <QShortcut>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

QString baselineSourcePath()
{
    return QString::fromUtf8(WATER1_TEST_SOURCE_DIR);
}

QString baselinePackagePath()
{
    return QString::fromUtf8(WATER1_TEST_PACKAGE_PATH);
}

bool copySourceSet(const QString &sourcePath, const QString &destinationPath)
{
    const QStringList files = {QStringLiteral("NEWGAME.DAT"), QStringLiteral("MAP.PUT"),
                               QStringLiteral("SHINARIO.CIM"), QStringLiteral("COLOR.CIM"),
                               QStringLiteral("MAIN.EXE")};
    for (const QString &file : files) {
        if (!QFile::copy(sourcePath + QLatin1Char('/') + file,
                         destinationPath + QLatin1Char('/') + file)) return false;
    }
    return true;
}

bool writeJson(const QString &path, const QJsonObject &object)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) >= 0 && file.commit();
}

} // namespace

class MapViewerTests final : public QObject {
    Q_OBJECT

private slots:
    void sourceProfileAcceptsBaseline();
    void sourceProfileRejectsMissingAndCaseCollision();
    void sourceProfileRejectsChangedHeader();
    void saveReaderHandlesInactiveAndActiveSlots();
    void packageReaderRejectsUnsafeAndTamperedMetadata();
    void mapCanvasSupportsCtrlWheelAndClick();
    void layoutStateAndAutomaticLoadUseIsolatedUserSettings();
    void cancelledGenerationPublishesNoPackage();
};

void MapViewerTests::sourceProfileAcceptsBaseline()
{
    if (!QFileInfo::exists(baselineSourcePath())) QSKIP("Baseline original files are not available.");
    const SourceDirectoryReport report = SourceDirectory::validate(baselineSourcePath());
    QVERIFY2(report.valid, qPrintable(report.toText()));
    QCOMPARE(report.profileId, QStringLiteral("uw1-eng-dos-baseline"));
    QCOMPARE(report.files.size(), 5);
}

void MapViewerTests::sourceProfileRejectsMissingAndCaseCollision()
{
    if (!QFileInfo::exists(baselineSourcePath())) QSKIP("Baseline original files are not available.");
    QTemporaryDir missingDirectory;
    QVERIFY(missingDirectory.isValid());
    QVERIFY(!SourceDirectory::validate(missingDirectory.path()).valid);

    QTemporaryDir duplicateDirectory;
    QVERIFY(duplicateDirectory.isValid());
    QVERIFY(copySourceSet(baselineSourcePath(), duplicateDirectory.path()));
    QVERIFY(QFile::copy(baselineSourcePath() + QStringLiteral("/MAP.PUT"),
                        duplicateDirectory.path() + QStringLiteral("/map.put")));
    const SourceDirectoryReport report = SourceDirectory::validate(duplicateDirectory.path());
    QVERIFY(!report.valid);
    QVERIFY(report.toText().contains(QStringLiteral("ambiguous"), Qt::CaseInsensitive));
}

void MapViewerTests::sourceProfileRejectsChangedHeader()
{
    if (!QFileInfo::exists(baselineSourcePath())) QSKIP("Baseline original files are not available.");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(copySourceSet(baselineSourcePath(), directory.path()));
    const QString newgamePath = directory.path() + QStringLiteral("/NEWGAME.DAT");
    QFile input(newgamePath);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QByteArray bytes = input.readAll();
    input.close();
    QVERIFY(!bytes.isEmpty());
    bytes[0] = static_cast<char>(0);
    QSaveFile output(newgamePath);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QCOMPARE(output.write(bytes), static_cast<qint64>(bytes.size()));
    QVERIFY(output.commit());
    QVERIFY(!SourceDirectory::validate(directory.path()).valid);
}

void MapViewerTests::saveReaderHandlesInactiveAndActiveSlots()
{
    const QString savePath = baselineSourcePath() + QStringLiteral("/SAVE.DAT");
    if (!QFileInfo::exists(savePath)) QSKIP("Baseline SAVE.DAT is not available.");
    QList<ActiveTreasure> treasures;
    QString error;
    QVERIFY2(SaveDataReader::readActiveTreasures(savePath, &treasures, &error), qPrintable(error));

    QFile input(savePath);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QByteArray bytes = input.readAll();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const int slotBase = 0x0100;
    bytes[0] = static_cast<char>(1);
    bytes[1] = static_cast<char>(0);
    bytes[2] = static_cast<char>(0);
    bytes[3] = static_cast<char>(0);
    bytes[4] = static_cast<char>(0);
    QByteArray slotName(11, '\0');
    slotName.replace(0, 9, QByteArrayLiteral("TEST SLOT"));
    bytes.replace(5, 11, slotName);
    bytes[slotBase + 0x1DA9] = static_cast<char>(0x04);
    bytes[slotBase + 0x1DA5] = static_cast<char>(32);
    bytes[slotBase + 0x1DA6] = static_cast<char>(8);
    bytes[slotBase + 0x1DA7] = static_cast<char>(3);
    bytes[slotBase + 0x1DA8] = static_cast<char>(12);
    bytes.replace(slotBase + 0x00A8 + 0x1F * 16, 14, QByteArray("Test treasure\0", 14));
    bytes[slotBase + 0x00A8 + 0x1F * 16 + 0x0E] = static_cast<char>(0x34);
    bytes[slotBase + 0x00A8 + 0x1F * 16 + 0x0F] = static_cast<char>(0x12);
    // Controlled market fixture for port 0.  The expected values below are
    // the original integer formulas: (12 + 50) * factor / 100.
    bytes[slotBase + 0x0DBA] = static_cast<char>(0xE8);       // population 1000, LE
    bytes[slotBase + 0x0DBA + 1] = static_cast<char>(0x03);
    bytes[slotBase + 0x0DBA + 0x0B] = static_cast<char>(12);  // prosperity
    bytes[slotBase + 0x0DBA + 0x0C] = static_cast<char>(70);  // market value, LE
    bytes[slotBase + 0x0DBA + 0x0D] = static_cast<char>(0);
    bytes[slotBase + 0x113E] = static_cast<char>(0);          // Mediterranean region
    bytes[slotBase + 0x113E + 1] = static_cast<char>(5);      // Olive oil specialty
    bytes[slotBase + 0x113E + 2] = static_cast<char>(0);      // mature at any population
    // Controlled cargo fixture: original cargo-use arithmetic is unsigned
    // 16-bit accumulation of goods + food/10 + water/10 + lumber.
    const int cargoBase = slotBase + 0x126E;
    bytes[cargoBase + 0x01] = static_cast<char>(7);            // Sugar
    bytes[cargoBase + 0x02] = static_cast<char>(1);            // Cinnamon
    bytes[cargoBase + 0x06] = static_cast<char>(0xFA);         // 65530, LE
    bytes[cargoBase + 0x07] = static_cast<char>(0xFF);
    bytes[cargoBase + 0x08] = static_cast<char>(10);
    bytes[cargoBase + 0x09] = static_cast<char>(0);
    bytes[cargoBase + 0x10] = static_cast<char>(230);          // Food = 23
    bytes[cargoBase + 0x11] = static_cast<char>(0);
    bytes[cargoBase + 0x12] = static_cast<char>(170);          // Water = 17
    bytes[cargoBase + 0x13] = static_cast<char>(0);
    bytes[cargoBase + 0x14] = static_cast<char>(20);           // Lumber
    bytes[cargoBase + 0x15] = static_cast<char>(0);
    const QString fixturePath = directory.path() + QStringLiteral("/SAVE.DAT");
    QSaveFile output(fixturePath);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QCOMPARE(output.write(bytes), static_cast<qint64>(bytes.size()));
    QVERIFY(output.commit());
    QVERIFY2(SaveDataReader::readActiveTreasures(fixturePath, &treasures, &error), qPrintable(error));
    QCOMPARE(treasures.size(), 1);
    QCOMPARE(treasures.first().sectorX, 32);
    QCOMPARE(treasures.first().sectorY, 8);
    QCOMPARE(treasures.first().subX, 3);
    QCOMPARE(treasures.first().subY, 12);
    QCOMPARE(treasures.first().itemValue, 0x1234);
    QList<SaveSlot> saveSlots;
    QVERIFY2(SaveDataReader::readSaveSlots(fixturePath, &saveSlots, &error), qPrintable(error));
    QCOMPARE(saveSlots.size(), 1);
    QCOMPARE(saveSlots.first().slot, 0);
    QVERIFY(saveSlots.first().hasActiveTreasure);
    QCOMPARE(saveSlots.first().portMarkets.size(), 50);
    const PortMarket &market = saveSlots.first().portMarkets.first();
    QCOMPARE(market.portId, 0);
    QCOMPARE(market.population, 1000);
    QCOMPARE(market.prices.size(), 26);
    QCOMPARE(market.prices.at(0).goodsName, QStringLiteral("Pepper"));
    QCOMPARE(market.prices.at(0).purchasePrice, -1);
    QCOMPARE(market.prices.at(0).salePrice, 49);
    QCOMPARE(market.prices.at(5).goodsName, QStringLiteral("Olive oil"));
    QCOMPARE(market.prices.at(5).purchasePrice, 26);
    QCOMPARE(market.prices.at(5).salePrice, 22);
    QCOMPARE(saveSlots.first().playerFleet.heroName, QStringLiteral("Steve"));
    QCOMPARE(saveSlots.first().playerFleet.ships.size(), 3);
    const PlayerShipStatus &firstShip = saveSlots.first().playerFleet.ships.first();
    QCOMPARE(firstShip.slot, 0);
    QCOMPARE(firstShip.shipName, QStringLiteral("Defiant"));
    QCOMPARE(firstShip.crew, 10);
    QCOMPARE(firstShip.maxCrew, 90);
    QCOMPARE(firstShip.cargoUsed, 64); // (65530 + 10 + 23 + 17 + 20) modulo 65536
    QCOMPARE(firstShip.cargoCapacity, 420);
    QCOMPARE(firstShip.stock.size(), 5);
    QCOMPARE(firstShip.stock.at(0).itemName, QStringLiteral("Food"));
    QCOMPARE(firstShip.stock.at(0).amount, 23);
    QCOMPARE(firstShip.stock.at(3).itemName, QStringLiteral("Sugar"));
    QCOMPARE(firstShip.stock.at(3).amount, 65530);
    QCOMPARE(firstShip.stock.at(4).itemName, QStringLiteral("Cinnamon"));
    QCOMPARE(firstShip.stock.at(4).amount, 10);
}

void MapViewerTests::packageReaderRejectsUnsafeAndTamperedMetadata()
{
    const QString packagePath = baselinePackagePath();
    if (!QFileInfo(packagePath).isFile()) QSKIP("Generated baseline package is not available.");
    QFile source(packagePath);
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QJsonObject baseline = QJsonDocument::fromJson(source.readAll()).object();
    QVERIFY(!baseline.isEmpty());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QJsonObject unsafe = baseline;
    QJsonObject unsafeImage = unsafe.value(QStringLiteral("image")).toObject();
    unsafeImage.insert(QStringLiteral("path"), QStringLiteral("../outside.png"));
    unsafe.insert(QStringLiteral("image"), unsafeImage);
    const QString unsafePath = directory.path() + QStringLiteral("/unsafe.json");
    QVERIFY(writeJson(unsafePath, unsafe));
    LoadedMapPackage loaded;
    QString error;
    QVERIFY(!MapPackage::load(unsafePath, &loaded, &error));
    QVERIFY(error.contains(QStringLiteral("relative"), Qt::CaseInsensitive));

    const QString originalImage = QFileInfo(packagePath).dir().filePath(QStringLiteral("world_map.png"));
    QVERIFY(QFile::link(originalImage, directory.path() + QStringLiteral("/world_map.png")));
    QJsonObject tampered = baseline;
    QJsonObject tamperedImage = tampered.value(QStringLiteral("image")).toObject();
    tamperedImage.insert(QStringLiteral("sha256"), QString(64, QLatin1Char('0')));
    tampered.insert(QStringLiteral("image"), tamperedImage);
    const QString tamperedPath = directory.path() + QStringLiteral("/tampered.json");
    QVERIFY(writeJson(tamperedPath, tampered));
    QVERIFY(!MapPackage::load(tamperedPath, &loaded, &error));
    QVERIFY(error.contains(QStringLiteral("SHA-256"), Qt::CaseInsensitive));
}

void MapViewerTests::mapCanvasSupportsCtrlWheelAndClick()
{
    QGraphicsScene scene;
    scene.setSceneRect(0, 0, 3000, 1800);
    MapCanvas canvas(&scene);
    canvas.resize(500, 300);
    canvas.show();
    QTest::qWait(20);
    canvas.resetToNativePixels();
    QTest::qWait(10);
    const QPixmap rulerImage = canvas.viewport()->grab();
    QCOMPARE(rulerImage.toImage().pixelColor(4, 4), QColor(20, 29, 40));
    const QPoint point(250, 150);
    QTest::mouseMove(canvas.viewport(), point);
    QTest::qWait(10);
    const QPointF before = canvas.mapToScene(point);
    const qreal beforeScale = canvas.transform().m22();
    QWheelEvent wheel(QPointF(point), QPointF(canvas.mapToGlobal(point)), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas.viewport(), &wheel);
    QVERIFY(canvas.transform().m22() > beforeScale);
    const QPointF after = canvas.mapToScene(point);
    QVERIFY(qAbs(after.x() - before.x()) < 1.0);
    QVERIFY(qAbs(after.y() - before.y()) < 1.0);

    QSignalSpy clicked(&canvas, &MapCanvas::mapCellClicked);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(clicked.count(), 1);
    const QPointF clickedPosition = clicked.takeFirst().at(0).toPointF();
    QVERIFY(qAbs(clickedPosition.x() - canvas.mapToScene(point).x()) < 1.0);
    QVERIFY(qAbs(clickedPosition.y() - canvas.mapToScene(point).y()) < 1.0);
}

void MapViewerTests::layoutStateAndAutomaticLoadUseIsolatedUserSettings()
{
    if (!QFileInfo::exists(baselineSourcePath()) || !QFileInfo(baselinePackagePath()).isFile()) {
        QSKIP("Baseline source or generated package is not available.");
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsPath = directory.path() + QStringLiteral("/viewer.user.ini");
    qputenv("WATER1_MAPVIEWER_USER_SETTINGS_PATH", settingsPath.toUtf8());
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Water1_MapViewer"));
        settings.setValue(QStringLiteral("originalDataDirectory"), baselineSourcePath());
        settings.setValue(QStringLiteral("outputDirectory"), QFileInfo(baselinePackagePath()).dir().absolutePath());
        settings.setValue(QStringLiteral("autoOpenGeneratedPackage"), false);
        settings.setValue(QStringLiteral("autoLoadSaveData"), false);
        settings.endGroup();
    }
    {
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        QTest::qWait(30);
        auto *upper = window.findChild<QSplitter *>(QStringLiteral("upperSplitter"));
        auto *vertical = window.findChild<QSplitter *>(QStringLiteral("verticalSplitter"));
        QVERIFY(upper && vertical);
        upper->setSizes({900, 300});
        vertical->setSizes({600, 200});
        window.close();
    }
    {
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Water1_MapViewer"));
        QCOMPARE(settings.value(QStringLiteral("upperSplitterSizes")).toStringList().size(), 2);
        QCOMPARE(settings.value(QStringLiteral("verticalSplitterSizes")).toStringList().size(), 2);
        settings.setValue(QStringLiteral("lastPackagePath"), baselinePackagePath());
        settings.setValue(QStringLiteral("optionalSaveDataPath"), baselineSourcePath() + QStringLiteral("/SAVE.DAT"));
        settings.setValue(QStringLiteral("autoOpenGeneratedPackage"), true);
        settings.setValue(QStringLiteral("autoLoadSaveData"), true);
        settings.endGroup();
    }
    {
        MainWindow window;
        window.resize(1300, 800);
        window.show();
        auto *upper = window.findChild<QSplitter *>(QStringLiteral("upperSplitter"));
        auto *vertical = window.findChild<QSplitter *>(QStringLiteral("verticalSplitter"));
        auto *tabs = window.findChild<QTabWidget *>();
        auto *findPortAction = window.findChild<QAction *>(QStringLiteral("findPortAction"));
        auto *findPortShortcut = window.findChild<QShortcut *>(QStringLiteral("findPortShortcut"));
        auto *findNextPortAction = window.findChild<QAction *>(QStringLiteral("findNextPortAction"));
        auto *findNextPortShortcut = window.findChild<QShortcut *>(QStringLiteral("findNextPortShortcut"));
        auto *selectSaveSlotAction = window.findChild<QAction *>(QStringLiteral("selectSaveSlotAction"));
        auto *selectSaveSlotShortcut = window.findChild<QShortcut *>(QStringLiteral("selectSaveSlotShortcut"));
        auto *fleetStatusAction = window.findChild<QAction *>(QStringLiteral("fleetStatusAction"));
        auto *fleetStatusShortcut = window.findChild<QShortcut *>(QStringLiteral("fleetStatusShortcut"));
        auto *portsTable = window.findChild<QTableWidget *>(QStringLiteral("portsTable"));
        auto *portDetails = window.findChild<QLabel *>(QStringLiteral("portDetailsLabel"));
        auto *marketPricesButton = window.findChild<QPushButton *>(QStringLiteral("marketPricesButton"));
        QVERIFY(upper && vertical && tabs && findPortAction && findPortShortcut && findNextPortAction
                && findNextPortShortcut && selectSaveSlotAction && selectSaveSlotShortcut && fleetStatusAction
                && fleetStatusShortcut && portsTable && portDetails && marketPricesButton);
        QCOMPARE(portsTable->selectionBehavior(), QAbstractItemView::SelectRows);
        QCOMPARE(portsTable->selectionMode(), QAbstractItemView::SingleSelection);
        QVERIFY(portsTable->focusPolicy() != Qt::NoFocus);
        QTRY_VERIFY(tabs->tabText(0).startsWith(QStringLiteral("Ports (70)")));
        QVERIFY(upper->sizes().at(0) > upper->sizes().at(1));
        QVERIFY(vertical->sizes().at(0) > vertical->sizes().at(1));
        QCOMPARE(findPortShortcut->key(), QKeySequence(Qt::CTRL | Qt::Key_F));
        QCOMPARE(findNextPortShortcut->key(), QKeySequence(Qt::Key_F3));
        QCOMPARE(fleetStatusShortcut->key(), QKeySequence(Qt::Key_F4));
        // The graphics-view viewport is the normal initial keyboard target.
        auto *viewport = window.findChild<QGraphicsView *>()->viewport();
        viewport->setFocus();
        bool searchCompleted = false;
        QTimer::singleShot(0, &window, [&window, &searchCompleted] {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("portNameSearchDialog"));
            auto *search = window.findChild<QLineEdit *>(QStringLiteral("portNameSearchDialogInput"));
            if (!dialog || !search || !dialog->isModal()) return;
            search->setText(QStringLiteral("Lisbon"));
            QTest::keyClick(search, Qt::Key_Return);
            searchCompleted = true;
        });
        QTest::keyClick(viewport, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(searchCompleted);
        QTRY_VERIFY(portDetails->text().contains(QStringLiteral("Lisbon")));
        QVERIFY(portDetails->text().contains(QStringLiteral("Portugal")));
        bool fleetStatusDialogChecked = false;
        QTimer::singleShot(0, &window, [&window, &fleetStatusDialogChecked] {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("fleetStatusDialog"));
            auto *summary = window.findChild<QLabel *>(QStringLiteral("fleetStatusSummary"));
            auto *fleetTable = window.findChild<QTableWidget *>(QStringLiteral("fleetSummaryTable"));
            if (!dialog) return;
            fleetStatusDialogChecked = dialog->isModal() && summary && fleetTable && fleetTable->rowCount() == 3
                && fleetTable->selectionMode() == QAbstractItemView::NoSelection
                && summary->text().contains(QStringLiteral("Hero: Steve"))
                && fleetTable->item(0, 1)->text() == QStringLiteral("Defiant")
                && fleetTable->item(0, 3)->text() == QStringLiteral("10 / 90");
            QTest::keyClick(dialog, Qt::Key_Escape);
        });
        QTest::keyClick(viewport, Qt::Key_F4);
        QVERIFY(fleetStatusDialogChecked);
        bool marketDialogChecked = false;
        QTimer::singleShot(0, &window, [&window, &marketDialogChecked] {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("marketPricesDialog"));
            auto *table = window.findChild<QTableWidget *>(QStringLiteral("marketPricesTable"));
            if (!dialog) return;
            const bool validTable = dialog->isModal() && table && table->rowCount() == 9 && table->columnCount() == 9
                && table->selectionMode() == QAbstractItemView::NoSelection;
            const bool validHeaders = validTable
                && table->horizontalHeaderItem(0)->text() == QStringLiteral("Item")
                && table->horizontalHeaderItem(1)->text() == QStringLiteral("Buy")
                && table->horizontalHeaderItem(2)->text() == QStringLiteral("Sell")
                && table->horizontalHeaderItem(3)->text() == QStringLiteral("Item")
                && table->horizontalHeaderItem(6)->text() == QStringLiteral("Item")
                && table->item(0, 0)->text() == QStringLiteral("Pepper")
                && table->item(0, 3)->text() == QStringLiteral("Cinnamon")
                && table->item(0, 6)->text() == QStringLiteral("Nutmeg");
            const auto *availableItem = validHeaders ? table->item(1, 6) : nullptr;
            marketDialogChecked = validHeaders && availableItem
                && availableItem->foreground().color() == QColor(0, 66, 170)
                && availableItem->font().bold();
            QTest::keyClick(dialog, Qt::Key_Escape);
        });
        marketPricesButton->click();
        QVERIFY(marketDialogChecked);
        bool marketCloseButtonWorked = false;
        QTimer::singleShot(0, &window, [&window, &marketCloseButtonWorked] {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("marketPricesDialog"));
            auto *buttons = window.findChild<QDialogButtonBox *>(QStringLiteral("marketPricesCloseButtons"));
            if (!dialog) return;
            auto *closeButton = buttons ? buttons->button(QDialogButtonBox::Close) : nullptr;
            marketCloseButtonWorked = dialog->isModal() && closeButton;
            if (closeButton) closeButton->click();
            else QTest::keyClick(dialog, Qt::Key_Escape);
        });
        marketPricesButton->click();
        QVERIFY(marketCloseButtonWorked);
        auto *scene = window.findChild<QGraphicsScene *>();
        QGraphicsRectItem *selection = nullptr;
        for (QGraphicsItem *item : scene->items()) {
            if (item->data(1).toString() == QStringLiteral("mapCellSelection")) {
                selection = dynamic_cast<QGraphicsRectItem *>(item);
                break;
            }
        }
        QVERIFY(selection);
        const QColor firstHighlightColor = selection->pen().color();
        QVERIFY(firstHighlightColor != QColor(Qt::white));
        QTest::qWait(250);
        QVERIFY(selection->pen().color() != firstHighlightColor);

        bool multipleSearchCompleted = false;
        QTimer::singleShot(0, &window, [&window, &multipleSearchCompleted] {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("portNameSearchDialog"));
            auto *search = window.findChild<QLineEdit *>(QStringLiteral("portNameSearchDialogInput"));
            if (!dialog || !search || !dialog->isModal()) return;
            search->setText(QStringLiteral("a"));
            QTest::keyClick(search, Qt::Key_Return);
            multipleSearchCompleted = true;
        });
        findPortAction->trigger();
        QVERIFY(multipleSearchCompleted);
        QTRY_VERIFY(portDetails->text().contains(QStringLiteral("Istanbul")));
        viewport->setFocus();
        QTest::keyClick(viewport, Qt::Key_F3);
        QTRY_VERIFY(portDetails->text().contains(QStringLiteral("Genoa")));

        portsTable->setFocus();
        portsTable->setCurrentCell(0, 0);
        QTRY_VERIFY(portDetails->text().contains(QStringLiteral("Lisbon")));
        QTest::keyClick(portsTable, Qt::Key_Down);
        QTRY_VERIFY(portDetails->text().contains(QStringLiteral("Seville")));

        bool failedSearchMessageShown = false;
        bool failedSearchDialogClosed = false;
        QTimer::singleShot(0, &window, [&window, &failedSearchMessageShown, &failedSearchDialogClosed] {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("portNameSearchDialog"));
            auto *search = window.findChild<QLineEdit *>(QStringLiteral("portNameSearchDialogInput"));
            if (!dialog || !search) return;
            search->setText(QStringLiteral("aaaaa"));
            QTest::keyClick(search, Qt::Key_Return);
            QTimer::singleShot(150, &window, [&window, &failedSearchMessageShown, &failedSearchDialogClosed] {
                auto *dialog = window.findChild<QDialog *>(QStringLiteral("portNameSearchDialog"));
                auto *feedback = window.findChild<QLabel *>(QStringLiteral("portNameSearchFeedback"));
                if (!dialog || !feedback || !feedback->isVisible()
                    || !feedback->text().contains(QStringLiteral("aaaaa"))) return;
                failedSearchMessageShown = true;
                QTest::keyClick(dialog, Qt::Key_Escape);
                failedSearchDialogClosed = true;
            });
        });
        findPortAction->trigger();
        QVERIFY(failedSearchMessageShown);
        QVERIFY(failedSearchDialogClosed);

        QCOMPARE(selectSaveSlotShortcut->key(), QKeySequence(Qt::Key_F2));
        bool saveSlotSwitched = false;
        QTimer::singleShot(0, &window, [&window, &saveSlotSwitched] {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("saveSlotDialog"));
            auto *slotList = window.findChild<QListWidget *>(QStringLiteral("saveSlotList"));
            if (!dialog || !dialog->isModal() || !slotList || slotList->count() != 3) return;
            const QRect secondSlot = slotList->visualItemRect(slotList->item(1));
            QTest::mouseClick(slotList->viewport(), Qt::LeftButton, Qt::NoModifier, secondSlot.center());
            saveSlotSwitched = true;
        });
        // The shortcut key is asserted above; invoke the menu entry here so
        // this nested modal test does not retain the preceding F3 key event.
        selectSaveSlotAction->trigger();
        QVERIFY(saveSlotSwitched);
        QVERIFY(tabs->tabText(1).startsWith(QStringLiteral("Treasures (Slot 2:")));

        bool saveSlotEscapeClosed = false;
        QTimer::singleShot(0, &window, [&window, &saveSlotEscapeClosed] {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("saveSlotDialog"));
            if (!dialog || !dialog->isModal()) return;
            QTest::keyClick(dialog, Qt::Key_Escape);
            saveSlotEscapeClosed = true;
        });
        selectSaveSlotAction->trigger();
        QVERIFY(saveSlotEscapeClosed);

        bool escapeClosed = false;
        QTimer::singleShot(0, &window, [&window, &escapeClosed] {
            auto *dialog = window.findChild<QDialog *>(QStringLiteral("portNameSearchDialog"));
            if (!dialog || !dialog->isModal()) return;
            QTest::keyClick(dialog, Qt::Key_Escape);
            escapeClosed = true;
        });
        // Exercise the menu entry separately; the earlier viewport key press
        // verifies the F3 shortcut path.
        findPortAction->trigger();
        QVERIFY(escapeClosed);
        window.close();
    }
    qunsetenv("WATER1_MAPVIEWER_USER_SETTINGS_PATH");
}

void MapViewerTests::cancelledGenerationPublishesNoPackage()
{
    if (!QFileInfo::exists(baselineSourcePath())) QSKIP("Baseline original files are not available.");
    const SourceDirectoryReport report = SourceDirectory::validate(baselineSourcePath());
    QVERIFY2(report.valid, qPrintable(report.toText()));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WorldMapGenerator generator(GenerationRequest{report, directory.path()});
    QSignalSpy failed(&generator, &WorldMapGenerator::generationFailed);
    QObject::connect(&generator, &QThread::started, &generator, &QThread::requestInterruption,
                     Qt::DirectConnection);
    generator.start();
    QVERIFY(generator.wait(30000));
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(failed.first().at(0).toString().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    QVERIFY(!QFileInfo::exists(directory.path() + QStringLiteral("/uw1-eng-dos-uw1-eng-dos-baseline")));
}

QTEST_MAIN(MapViewerTests)
#include "MapViewerTests.moc"
