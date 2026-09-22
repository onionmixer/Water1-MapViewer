#include "MainWindow.h"
#include "MapCanvas.h"
#include "MapDataOverlayItem.h"
#include "MapPackage.h"
#include "TilePyramidItem.h"
#include "WorldMapGenerator.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QLabel>
#include <QKeySequence>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr auto kSettingsGroup = "Water1_MapViewer";
constexpr auto kOriginalDataDirectoryKey = "originalDataDirectory";
constexpr auto kOutputDirectoryKey = "outputDirectory";
constexpr auto kLastPackagePathKey = "lastPackagePath";
constexpr auto kOptionalSaveDataPathKey = "optionalSaveDataPath";
constexpr int kNumericSortRole = Qt::UserRole + 1;

class SortableTableItem final : public QTableWidgetItem {
public:
    explicit SortableTableItem(const QString &text, const QVariant &sortValue = {})
        : QTableWidgetItem(text)
    {
        if (sortValue.isValid()) setData(kNumericSortRole, sortValue);
    }

    bool operator<(const QTableWidgetItem &other) const override
    {
        const QVariant mine = data(kNumericSortRole);
        const QVariant theirs = other.data(kNumericSortRole);
        if (mine.isValid() && theirs.isValid()) return mine.toDouble() < theirs.toDouble();
        return QTableWidgetItem::operator<(other);
    }
};

QString defaultConfigurationFilePath()
{
    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        applicationDirectory.filePath(QStringLiteral("water1_mapviewer.ini")),
        applicationDirectory.filePath(QStringLiteral("../water1_mapviewer.ini")),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isFile()) return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

QVariant configuredValue(const QString &key)
{
    const QString configurationPath = defaultConfigurationFilePath();
    if (configurationPath.isEmpty()) return {};
    QSettings settings(configurationPath, QSettings::IniFormat);
    return settings.value(key);
}

QString configuredPath(const QString &key)
{
    const QString configurationPath = defaultConfigurationFilePath();
    const QString value = configuredValue(key).toString().trimmed();
    if (configurationPath.isEmpty() || value.isEmpty()) return {};
    return QFileInfo(QDir(QFileInfo(configurationPath).absolutePath()).absoluteFilePath(value)).absoluteFilePath();
}

QString userSettingsPath()
{
    const QString testOrPortableOverride = qEnvironmentVariable("WATER1_MAPVIEWER_USER_SETTINGS_PATH").trimmed();
    if (!testOrPortableOverride.isEmpty()) return QFileInfo(testOrPortableOverride).absoluteFilePath();
    const QString configurationPath = defaultConfigurationFilePath();
    if (!configurationPath.isEmpty()) {
        return QDir(QFileInfo(configurationPath).absolutePath()).filePath(QStringLiteral("water1_mapviewer.user.ini"));
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("water1_mapviewer.user.ini"));
}

QList<int> splitterSizesFromSettings(const QVariant &value)
{
    QList<int> sizes;
    for (const QString &entry : value.toStringList()) {
        bool valid = false;
        const int size = entry.toInt(&valid);
        if (valid && size >= 0) sizes.append(size);
    }
    return sizes;
}

QStringList splitterSizesForSettings(const QList<int> &sizes)
{
    QStringList entries;
    entries.reserve(sizes.size());
    for (const int size : sizes) entries.append(QString::number(size));
    return entries;
}

QTableWidget *createPlaceholderTable(const QStringList &headers, QWidget *parent)
{
    auto *table = new QTableWidget(parent);
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(true);
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

qreal displayPixelAspectX(const QJsonObject &metadata)
{
    const QJsonObject aspect = metadata.value(QStringLiteral("display_pixel_aspect")).toObject();
    const qreal horizontal = aspect.value(QStringLiteral("horizontal")).toDouble();
    const qreal vertical = aspect.value(QStringLiteral("vertical")).toDouble();
    return horizontal > 0.0 && vertical > 0.0 ? horizontal / vertical : 1.0;
}

/* MapCanvas applies the non-square original EGA pixel aspect to all scene items.  Host UI overlays,
 * unlike the original sprite marker, need their scene-space width compensated to remain circular. */
QGraphicsEllipseItem *addRoundOverlayMarker(QGraphicsScene *scene, qreal centerX, qreal centerY,
                                            const QPen &pen, const QBrush &brush, qreal pixelAspectX)
{
    constexpr qreal kScreenDiameter = 10.0;
    const qreal sceneWidth = kScreenDiameter / pixelAspectX;
    return scene->addEllipse(centerX - sceneWidth / 2.0, centerY - kScreenDiameter / 2.0,
                             sceneWidth, kScreenDiameter, pen, brush);
}

} // namespace

MainWindow::MainWindow()
{
    setObjectName(QStringLiteral("Water1MapViewerMainWindow"));
    setWindowTitle(QStringLiteral("Water1 Map Viewer"));
    resize(1440, 900);
    qApp->installEventFilter(this);

    createActions();
    createLayout();
    m_portSelectionAnimationTimer = new QTimer(this);
    m_portSelectionAnimationTimer->setInterval(100);
    connect(m_portSelectionAnimationTimer, &QTimer::timeout, this, [this] {
        if (!m_selectionItem || m_selectedPortIndex < 0) {
            m_portSelectionAnimationTimer->stop();
            return;
        }
        ++m_portSelectionColorIndex;
        if (m_portSelectionColorIndex == 7) m_portSelectionColorIndex = 0;
        updatePortSelectionHighlight();
    });

    m_userSettingsPath = userSettingsPath();
    QSettings settings(m_userSettingsPath, QSettings::IniFormat);
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    m_originalDataDirectory = settings.value(QString::fromLatin1(kOriginalDataDirectoryKey)).toString();
    m_outputDirectory = settings.value(QString::fromLatin1(kOutputDirectoryKey)).toString();
    m_lastPackagePath = settings.value(QString::fromLatin1(kLastPackagePathKey)).toString();
    m_saveDataPath = settings.value(QString::fromLatin1(kOptionalSaveDataPathKey)).toString();
    const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    const QList<int> verticalSizes = splitterSizesFromSettings(settings.value(QStringLiteral("verticalSplitterSizes")));
    const QList<int> upperSizes = splitterSizesFromSettings(settings.value(QStringLiteral("upperSplitterSizes")));
    m_autoOpenGeneratedPackage = settings.value(QStringLiteral("autoOpenGeneratedPackage"),
                                                 configuredValue(QStringLiteral("automation/open_generated_map_package"))).toBool();
    m_autoLoadSaveData = settings.value(QStringLiteral("autoLoadSaveData"),
                                        configuredValue(QStringLiteral("automation/auto_load_optional_save_data"))).toBool();
    settings.endGroup();
    if (m_originalDataDirectory.isEmpty()) {
        m_originalDataDirectory = configuredPath(QStringLiteral("paths/original_data_directory"));
    }
    if (m_outputDirectory.isEmpty()) {
        m_outputDirectory = configuredPath(QStringLiteral("paths/output_directory"));
    }
    if (m_saveDataPath.isEmpty()) {
        m_saveDataPath = configuredPath(QStringLiteral("automation/optional_save_data_path"));
    }
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    QTimer::singleShot(0, this, [this, verticalSizes, upperSizes] {
        if (!verticalSizes.isEmpty()) m_verticalSplitter->setSizes(verticalSizes);
        if (!upperSizes.isEmpty()) m_upperSplitter->setSizes(upperSizes);
    });

    if (!m_originalDataDirectory.isEmpty()) {
        applyReport(SourceDirectory::validate(m_originalDataDirectory), false);
    } else {
        updateWindowState();
    }
    QTimer::singleShot(0, this, [this] { tryAutomaticStartupLoad(); });
}

void MainWindow::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    m_setDataDirectoryAction = fileMenu->addAction(tr("Set Original Data &Directory…"));
    connect(m_setDataDirectoryAction, &QAction::triggered, this, [this] { setOriginalDataDirectory(); });

    m_validateDataDirectoryAction = fileMenu->addAction(tr("&Validate Original Data Directory"));
    connect(m_validateDataDirectoryAction, &QAction::triggered, this, [this] { validateOriginalDataDirectory(); });

    fileMenu->addSeparator();
    m_generateAction = fileMenu->addAction(tr("Generate Map Image and Metadata…"));
    connect(m_generateAction, &QAction::triggered, this, [this] { generateMapPackage(); });

    m_openPackageAction = fileMenu->addAction(tr("Open Generated Map Package…"));
    connect(m_openPackageAction, &QAction::triggered, this, [this] { openGeneratedMapPackage(); });

    m_setSaveAction = fileMenu->addAction(tr("Set Optional SAVE.DAT…"));
    m_setSaveAction->setToolTip(tr("Read active treasure locations from an original SAVE.DAT without modifying it."));
    connect(m_setSaveAction, &QAction::triggered, this, [this] { setOptionalSaveData(); });

    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::closeAllWindows);

    QMenu *navigateMenu = menuBar()->addMenu(tr("&Navigate"));
    m_findPortAction = navigateMenu->addAction(tr("Find Port &Name… (Ctrl+F)"));
    m_findPortAction->setObjectName(QStringLiteral("findPortAction"));
    m_findPortAction->setStatusTip(tr("Open the port-name search dialog (Ctrl+F)."));
    connect(m_findPortAction, &QAction::triggered, this, &MainWindow::openPortNameSearchDialog);

    // Keep Ctrl+F independent of the menu action.  On some Qt5 platform styles a
    // menu action's WindowShortcut is not dispatched while a graphics viewport
    // or table has keyboard focus; this shortcut is owned by the application.
    m_findPortShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    m_findPortShortcut->setObjectName(QStringLiteral("findPortShortcut"));
    m_findPortShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_findPortShortcut, &QShortcut::activated, this, &MainWindow::openPortNameSearchDialog);

    m_findNextPortAction = navigateMenu->addAction(tr("Find &Next Port (F3)"));
    m_findNextPortAction->setObjectName(QStringLiteral("findNextPortAction"));
    m_findNextPortAction->setStatusTip(tr("Select the next port matching the most recent search (F3)."));
    connect(m_findNextPortAction, &QAction::triggered, this, &MainWindow::findNextPort);
    m_findNextPortShortcut = new QShortcut(QKeySequence(Qt::Key_F3), this);
    m_findNextPortShortcut->setObjectName(QStringLiteral("findNextPortShortcut"));
    m_findNextPortShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_findNextPortShortcut, &QShortcut::activated, this, &MainWindow::findNextPort);

    m_selectSaveSlotAction = navigateMenu->addAction(tr("Select SAVE.DAT Slot… (F2)"));
    m_selectSaveSlotAction->setObjectName(QStringLiteral("selectSaveSlotAction"));
    m_selectSaveSlotAction->setStatusTip(tr("Open the loaded SAVE.DAT slot list (F2)."));
    connect(m_selectSaveSlotAction, &QAction::triggered, this, &MainWindow::openSaveSlotDialog);
    m_selectSaveSlotShortcut = new QShortcut(QKeySequence(Qt::Key_F2), this);
    m_selectSaveSlotShortcut->setObjectName(QStringLiteral("selectSaveSlotShortcut"));
    m_selectSaveSlotShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_selectSaveSlotShortcut, &QShortcut::activated, this, &MainWindow::openSaveSlotDialog);

    m_fleetStatusAction = navigateMenu->addAction(tr("Fleet Status (F4)"));
    m_fleetStatusAction->setObjectName(QStringLiteral("fleetStatusAction"));
    m_fleetStatusAction->setStatusTip(tr("Show the selected SAVE.DAT slot's player fleet, crew, and stock (F4)."));
    connect(m_fleetStatusAction, &QAction::triggered, this, &MainWindow::openFleetStatusDialog);
    m_fleetStatusShortcut = new QShortcut(QKeySequence(Qt::Key_F4), this);
    m_fleetStatusShortcut->setObjectName(QStringLiteral("fleetStatusShortcut"));
    m_fleetStatusShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_fleetStatusShortcut, &QShortcut::activated, this, &MainWindow::openFleetStatusDialog);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *resetAction = viewMenu->addAction(tr("Reset View"));
    connect(resetAction, &QAction::triggered, this, [this] { m_mapView->resetToFit(); });
    m_showPortsAction = viewMenu->addAction(tr("Show Ports"));
    m_showPortsAction->setCheckable(true);
    m_showPortsAction->setChecked(true);
    connect(m_showPortsAction, &QAction::toggled, this, [this](bool visible) {
        for (QGraphicsItem *item : m_portOverlayItems) item->setVisible(visible);
    });
    m_showTreasureCandidatesAction = viewMenu->addAction(tr("Show Eligible Treasure Cells"));
    m_showTreasureCandidatesAction->setCheckable(true);
    connect(m_showTreasureCandidatesAction, &QAction::toggled, this, [this](bool visible) {
        if (m_mapDataOverlayItem) m_mapDataOverlayItem->setTreasureCandidatesVisible(visible);
    });
    m_showGridAction = viewMenu->addAction(tr("Show Sector Grid"));
    m_showGridAction->setCheckable(true);
    connect(m_showGridAction, &QAction::toggled, this, [this](bool visible) {
        if (m_mapDataOverlayItem) m_mapDataOverlayItem->setGridVisible(visible);
    });

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAction = helpMenu->addAction(tr("About"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About Water1 Map Viewer"),
                           tr("Water1 DOS original world-map extraction and inspection tool.\n\n"
                              "Current milestone: original data directory validation."));
    });
}

void MainWindow::createLayout()
{
    auto *central = new QWidget(this);
    m_verticalSplitter = new QSplitter(Qt::Vertical, central);
    m_upperSplitter = new QSplitter(Qt::Horizontal, m_verticalSplitter);
    m_verticalSplitter->setObjectName(QStringLiteral("verticalSplitter"));
    m_upperSplitter->setObjectName(QStringLiteral("upperSplitter"));

    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(0, 0, 1000, 600);
    m_scene->setBackgroundBrush(QColor(25, 34, 45));
    auto *placeholder = m_scene->addText(tr("No generated map package is open.\n\n"
                                             "File → Set Original Data Directory…\n"
                                             "then validate the original data files."));
    placeholder->setDefaultTextColor(QColor(220, 230, 240));
    placeholder->setPos(80, 80);

    m_mapView = new MapCanvas(m_scene, m_upperSplitter);
    connect(m_mapView, &MapCanvas::mapCellClicked, this,
            [this](const QPointF &position) { selectMapPosition(position, false); });
    connect(m_mapView, &MapCanvas::visibleSceneRectChanged, this,
            [this](const QRectF &rect, qreal scale) { updateVisibleTiles(rect, scale); });

    auto *infoPanel = new QWidget(m_upperSplitter);
    auto *infoLayout = new QVBoxLayout(infoPanel);
    auto *title = new QLabel(tr("Selection / Information"), infoPanel);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    infoLayout->addWidget(title);

    auto *form = new QFormLayout;
    m_dataDirectoryLabel = new QLabel(infoPanel);
    m_dataDirectoryLabel->setWordWrap(true);
    m_profileLabel = new QLabel(infoPanel);
    m_profileLabel->setWordWrap(true);
    m_selectionLabel = new QLabel(tr("No map cell selected."), infoPanel);
    m_selectionLabel->setWordWrap(true);
    m_portDetailsLabel = new QLabel(tr("No port selected."), infoPanel);
    m_portDetailsLabel->setObjectName(QStringLiteral("portDetailsLabel"));
    m_portDetailsLabel->setWordWrap(true);
    m_treasureDetailsLabel = new QLabel(tr("No active treasure selected."), infoPanel);
    m_treasureDetailsLabel->setObjectName(QStringLiteral("treasureDetailsLabel"));
    m_treasureDetailsLabel->setWordWrap(true);
    form->addRow(tr("Original data:"), m_dataDirectoryLabel);
    form->addRow(tr("Profile:"), m_profileLabel);
    form->addRow(tr("Selection:"), m_selectionLabel);
    infoLayout->addLayout(form);
    auto *portDetailsTitle = new QLabel(tr("Port Details"), infoPanel);
    portDetailsTitle->setFont(titleFont);
    infoLayout->addWidget(portDetailsTitle);
    infoLayout->addWidget(m_portDetailsLabel);
    auto *treasureDetailsTitle = new QLabel(tr("Treasure Details"), infoPanel);
    treasureDetailsTitle->setFont(titleFont);
    infoLayout->addWidget(treasureDetailsTitle);
    infoLayout->addWidget(m_treasureDetailsLabel);
    m_marketPricesButton = new QPushButton(tr("Market Prices…"), infoPanel);
    m_marketPricesButton->setObjectName(QStringLiteral("marketPricesButton"));
    m_marketPricesButton->setToolTip(tr("Show market prices for the selected port from the selected SAVE.DAT slot."));
    infoLayout->addWidget(m_marketPricesButton);
    connect(m_marketPricesButton, &QPushButton::clicked, this, &MainWindow::openMarketPricesDialog);
    infoLayout->addStretch(1);

    m_dataTabs = new QTabWidget(m_verticalSplitter);
    auto *portsPage = new QWidget(m_dataTabs);
    auto *portsLayout = new QVBoxLayout(portsPage);
    m_portsTable = createPlaceholderTable({tr("ID"), tr("Name"), tr("Latitude"), tr("Longitude"),
                                           tr("Sector X"), tr("Sector Y"), tr("Sub X"), tr("Sub Y"),
                                           tr("Sovereignty"), tr("Attributes")}, portsPage);
    m_portsTable->setObjectName(QStringLiteral("portsTable"));
    portsLayout->addWidget(m_portsTable);

    auto *treasuresPage = new QWidget(m_dataTabs);
    auto *treasuresLayout = new QVBoxLayout(treasuresPage);
    m_treasuresFilter = new QLineEdit(treasuresPage);
    m_treasuresFilter->setPlaceholderText(tr("Filter treasure entries by any column…"));
    m_treasuresTable = createPlaceholderTable({tr("Kind"), tr("Name"), tr("Latitude"), tr("Longitude"),
                                               tr("Sector X"), tr("Sector Y"), tr("Sub X"), tr("Sub Y"),
                                               tr("Terrain"), tr("Source")}, treasuresPage);
    treasuresLayout->addWidget(m_treasuresFilter);
    treasuresLayout->addWidget(m_treasuresTable);
    m_dataTabs->addTab(portsPage, tr("Ports (0)"));
    m_dataTabs->addTab(treasuresPage, tr("Treasures"));
    connect(m_portsTable, &QTableWidget::cellClicked, this,
            [this](int row, int) { selectPortRow(row); });
    connect(m_portsTable, &QTableWidget::currentCellChanged, this,
            [this](int currentRow, int, int, int) { selectPortRow(currentRow); });
    connect(m_treasuresTable, &QTableWidget::cellClicked, this,
            [this](int row, int) { selectTreasureRow(row); });
    connect(m_treasuresFilter, &QLineEdit::textChanged, this,
            [this](const QString &text) { applyTableFilter(m_treasuresTable, text); });

    m_verticalSplitter->addWidget(m_upperSplitter);
    m_verticalSplitter->addWidget(m_dataTabs);
    m_verticalSplitter->setStretchFactor(0, 7);
    m_verticalSplitter->setStretchFactor(1, 3);
    m_upperSplitter->setStretchFactor(0, 7);
    m_upperSplitter->setStretchFactor(1, 3);
    connect(m_verticalSplitter, &QSplitter::splitterMoved, this, [this] { saveUserState(); });
    connect(m_upperSplitter, &QSplitter::splitterMoved, this, [this] { saveUserState(); });

    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_verticalSplitter);
    setCentralWidget(central);

    statusBar()->showMessage(tr("Choose an original data directory to begin."));
}

void MainWindow::openGeneratedMapPackage()
{
    const QString metadataPath = QFileDialog::getOpenFileName(
        this, tr("Open world_map.json"),
        m_lastPackagePath.isEmpty() ? (m_outputDirectory.isEmpty() ? QDir::homePath() : m_outputDirectory)
                                    : m_lastPackagePath,
        tr("Water1 map metadata (world_map.json)"));
    if (metadataPath.isEmpty()) return;

    QString error;
    if (!openMapPackage(metadataPath, &error)) {
        QMessageBox::warning(this, tr("Cannot Open Map Package"), error);
    }
}

bool MainWindow::openMapPackage(const QString &metadataPath, QString *error)
{
    LoadedMapPackage package;
    QString loadError;
    if (!MapPackage::load(metadataPath, &package, &loadError)) {
        if (error) *error = loadError;
        return false;
    }

    m_portSelectionAnimationTimer->stop();
    m_selectedPortIndex = -1;
    m_portSelectionColorIndex = 0;
    m_scene->clear();
    m_overlayItems.clear(); // clear() already destroyed the old scene-owned overlay items.
    m_portOverlayItems.clear();
    m_treasureOverlayItems.clear();
    m_tileMapItem = nullptr;
    m_mapDataOverlayItem = nullptr;
    m_mapImageSize = package.imageSize;
    m_portMarkerImage = package.portMarkerImage;
    m_tileMapItem = new TilePyramidItem(package.imageSize, package.tilePyramid, package.tileManifest,
                                         QFileInfo(package.metadataPath).absolutePath());
    m_scene->addItem(m_tileMapItem);
    m_scene->setSceneRect(m_tileMapItem->boundingRect());
    m_selectionItem = nullptr;
    m_packageMetadata = package.metadata;
    m_packagePorts = package.ports;
    m_lastPortSearchTerm.clear();
    m_lastPortSearchIndex = -1;
    const QByteArray treasureCandidates = QByteArray::fromBase64(
        m_packageMetadata.value(QStringLiteral("treasure_model")).toObject()
            .value(QStringLiteral("cell_bitset_base64")).toString().toLatin1());
    m_mapDataOverlayItem = new MapDataOverlayItem(package.imageSize, treasureCandidates);
    m_scene->addItem(m_mapDataOverlayItem);
    m_mapDataOverlayItem->setGridVisible(m_showGridAction->isChecked());
    m_mapDataOverlayItem->setTreasureCandidatesVisible(m_showTreasureCandidatesAction->isChecked());
    const QJsonObject pixelAspect = m_packageMetadata.value(QStringLiteral("display_pixel_aspect")).toObject();
    m_mapView->setDisplayPixelAspect(pixelAspect.value(QStringLiteral("horizontal")).toDouble()
                                    / pixelAspect.value(QStringLiteral("vertical")).toDouble());
    m_saveSlots.clear();
    m_activeTreasures.clear();
    m_selectedSaveSlot = -1;
    m_lastPackagePath = QFileInfo(package.metadataPath).absoluteFilePath();
    m_portDetailsLabel->setText(tr("No port selected."));
    m_treasureDetailsLabel->setText(tr("No active treasure selected."));
    populatePackageUi();
    updateWindowState();
    saveUserState();
    connect(m_tileMapItem, &TilePyramidItem::loadingProgress, this, [this](int ready, int total) {
        if (total > 0 && ready < total) {
            statusBar()->showMessage(tr("Loading map tiles: %1 / %2 (96 MiB cache limit)").arg(ready).arg(total));
        }
    });
    connect(m_tileMapItem, &TilePyramidItem::tileLoadWarning, this, [this](const QString &message) {
        statusBar()->showMessage(message, 8000);
    });
    QTimer::singleShot(0, this, [this] { m_mapView->resetToFit(); });
    statusBar()->showMessage(tr("Opened generated map package: %1").arg(package.metadataPath));
    return true;
}

void MainWindow::populatePackageUi()
{
    for (QGraphicsItem *item : m_overlayItems) delete item;
    m_overlayItems.clear();
    m_portOverlayItems.clear();
    m_treasureOverlayItems.clear();
    m_portsTable->setSortingEnabled(false);
    m_treasuresTable->setSortingEnabled(false);
    m_portsTable->setRowCount(m_packagePorts.size());
    for (int row = 0; row < m_packagePorts.size(); ++row) {
        const QJsonObject port = m_packagePorts.at(row).toObject();
        const QJsonObject position = port.value(QStringLiteral("position")).toObject();
        const QJsonObject coordinates = port.value(QStringLiteral("coordinates")).toObject();
        const QJsonObject latitude = coordinates.value(QStringLiteral("latitude")).toObject();
        const QJsonObject longitude = coordinates.value(QStringLiteral("longitude")).toObject();
        const QJsonObject attributes = port.value(QStringLiteral("attributes")).toObject();
        const QJsonObject politicalStatus = port.value(QStringLiteral("political_status")).toObject();
        const QStringList values = {
            QString::number(port.value(QStringLiteral("id")).toInt()),
            port.value(QStringLiteral("name")).toString(),
            QStringLiteral("%1%2").arg(latitude.value(QStringLiteral("degrees")).toInt()).arg(latitude.value(QStringLiteral("hemisphere")).toString()),
            QStringLiteral("%1%2").arg(longitude.value(QStringLiteral("degrees")).toInt()).arg(longitude.value(QStringLiteral("hemisphere")).toString()),
            QString::number(position.value(QStringLiteral("sector_x")).toInt()),
            QString::number(position.value(QStringLiteral("sector_y")).toInt()),
            QString::number(position.value(QStringLiteral("sub_x")).toInt()),
            QString::number(position.value(QStringLiteral("sub_y")).toInt()),
            politicalStatus.value(QStringLiteral("sovereignty")).toString(),
            QStringLiteral("0x%1").arg(attributes.value(QStringLiteral("raw")).toInt(), 2, 16, QLatin1Char('0')).toUpper(),
        };
        const QList<QVariant> sortValues = {
            port.value(QStringLiteral("id")).toInt(), port.value(QStringLiteral("name")).toString(),
            latitude.value(QStringLiteral("signed_degrees")).toInt(), longitude.value(QStringLiteral("east_degrees")).toInt(),
            position.value(QStringLiteral("sector_x")).toInt(), position.value(QStringLiteral("sector_y")).toInt(),
            position.value(QStringLiteral("sub_x")).toInt(), position.value(QStringLiteral("sub_y")).toInt(),
            politicalStatus.value(QStringLiteral("ownership_code")).toInt(), attributes.value(QStringLiteral("raw")).toInt(),
        };
        for (int column = 0; column < values.size(); ++column) {
            m_portsTable->setItem(row, column, new SortableTableItem(values.at(column), sortValues.at(column)));
        }
        m_portsTable->item(row, 0)->setData(Qt::UserRole, row);

        auto *marker = m_scene->addPixmap(QPixmap::fromImage(m_portMarkerImage, Qt::NoFormatConversion));
        marker->setPos(position.value(QStringLiteral("pixel_x")).toInt(),
                       position.value(QStringLiteral("pixel_y")).toInt());
        marker->setData(0, port.value(QStringLiteral("id")).toInt());
        marker->setToolTip(port.value(QStringLiteral("name")).toString());
        marker->setTransformationMode(Qt::FastTransformation);
        marker->setZValue(20);
        m_overlayItems.append(marker);
        m_portOverlayItems.append(marker);
        marker->setVisible(m_showPortsAction->isChecked());
    }

    const QJsonObject treasureModel = m_packageMetadata.value(QStringLiteral("treasure_model")).toObject();
    m_treasuresTable->setRowCount(1 + m_activeTreasures.size());
    const QStringList treasureValues = {
        tr("Possible cells"), tr("Quest-generated candidates"), tr("—"), tr("—"), tr("—"), tr("—"),
        tr("—"), tr("—"), tr("—"),
        tr("%1 eligible cells (static model)").arg(treasureModel.value(QStringLiteral("candidate_count")).toInt()),
    };
    for (int column = 0; column < treasureValues.size(); ++column) {
        m_treasuresTable->setItem(0, column, new SortableTableItem(treasureValues.at(column)));
    }
    m_treasuresTable->item(0, 0)->setData(Qt::UserRole, -1);
    for (int index = 0; index < m_activeTreasures.size(); ++index) {
        const ActiveTreasure &treasure = m_activeTreasures.at(index);
        const int row = index + 1;
        const int latitudeSigned = treasure.sectorY < 18 ? 90 - 5 * treasure.sectorY : -(5 * treasure.sectorY - 90);
        const int longitudeRemainder = (treasure.sectorX + 38) % 72;
        const QString latitude = QStringLiteral("%1%2").arg(qAbs(latitudeSigned)).arg(latitudeSigned >= 0 ? "N" : "S");
        const QString longitude = QStringLiteral("%1%2")
                                      .arg(longitudeRemainder < 36 ? longitudeRemainder * 5 : (72 - longitudeRemainder) * 5)
                                      .arg(longitudeRemainder < 36 ? "E" : "W");
        const QStringList values = {
            tr("Active save"),
            tr("Slot %1: %2").arg(treasure.slot + 1).arg(treasure.itemName.isEmpty() ? tr("unnamed treasure") : treasure.itemName),
            latitude, longitude, QString::number(treasure.sectorX), QString::number(treasure.sectorY),
            QString::number(treasure.subX), QString::number(treasure.subY), tr("Value %1").arg(treasure.itemValue),
            tr("%1 / read-only SAVE.DAT").arg(treasure.slotName),
        };
        const QList<QVariant> sortValues = {
            treasure.slot, treasure.itemName, latitudeSigned, longitudeRemainder * 5,
            treasure.sectorX, treasure.sectorY, treasure.subX, treasure.subY, treasure.itemValue, treasure.slotName,
        };
        for (int column = 0; column < values.size(); ++column) {
            m_treasuresTable->setItem(row, column, new SortableTableItem(values.at(column), sortValues.at(column)));
        }
        m_treasuresTable->item(row, 0)->setData(Qt::UserRole, index);
        const QJsonObject grid = m_packageMetadata.value(QStringLiteral("grid")).toObject();
        const int cellWidth = grid.value(QStringLiteral("cell_pixel_width")).toInt();
        const int cellHeight = grid.value(QStringLiteral("cell_pixel_height")).toInt();
        const qreal centerX = (treasure.sectorX * 14 + treasure.subX) * cellWidth + cellWidth / 2.0;
        const qreal centerY = (treasure.sectorY * 14 + treasure.subY) * cellHeight + cellHeight / 2.0;
        auto *marker = addRoundOverlayMarker(m_scene, centerX, centerY,
                                              QPen(QColor(70, 220, 255), 1.5), QBrush(QColor(0, 80, 220, 170)),
                                              displayPixelAspectX(m_packageMetadata));
        marker->setToolTip(tr("Active treasure: %1").arg(treasure.itemName));
        marker->setZValue(30);
        m_overlayItems.append(marker);
        m_treasureOverlayItems.append(marker);
    }

    m_portsTable->setSortingEnabled(true);
    m_treasuresTable->setSortingEnabled(true);
    applyTableFilter(m_treasuresTable, m_treasuresFilter->text());
    m_dataTabs->setTabText(0, tr("Ports (%1)").arg(m_packagePorts.size()));
    if (m_selectedSaveSlot >= 0) {
        const auto slot = std::find_if(m_saveSlots.cbegin(), m_saveSlots.cend(), [this](const SaveSlot &candidate) {
            return candidate.slot == m_selectedSaveSlot;
        });
        const QString slotName = slot != m_saveSlots.cend() && !slot->slotName.isEmpty()
            ? slot->slotName : tr("unnamed");
        m_dataTabs->setTabText(1, tr("Treasures (Slot %1: %2)").arg(m_selectedSaveSlot + 1).arg(slotName));
    } else {
        m_dataTabs->setTabText(1, tr("Treasures (%1 active)").arg(m_activeTreasures.size()));
    }
}

void MainWindow::openPortNameSearchDialog()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("portNameSearchDialog"));
    dialog.setWindowTitle(tr("Find Port"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    auto *prompt = new QLabel(tr("Port name (case-insensitive, partial match):"), &dialog);
    auto *search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("portNameSearchDialogInput"));
    search->setClearButtonEnabled(true);
    search->setText(m_lastPortSearchTerm);
    auto *feedback = new QLabel(&dialog);
    feedback->setObjectName(QStringLiteral("portNameSearchFeedback"));
    feedback->setWordWrap(true);
    feedback->setStyleSheet(QStringLiteral("color: #b00020;"));
    feedback->hide();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *findButton = buttons->button(QDialogButtonBox::Ok);
    findButton->setText(tr("Find"));
    // Enter belongs solely to the search field below.  Do not let the dialog
    // also treat it as a default-button click, which can submit a failed query
    // more than once on some Qt5 platform styles.
    findButton->setAutoDefault(false);
    findButton->setDefault(false);
    layout->addWidget(prompt);
    layout->addWidget(search);
    layout->addWidget(feedback);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Keep feedback inside this modal.  A nested QMessageBox can re-enter the
    // Enter-key handling path on Qt5 and create repeated failure popups.
    connect(search, &QLineEdit::textChanged, feedback, [feedback] {
        feedback->clear();
        feedback->hide();
    });
    const auto submitSearch = [this, &dialog, search, feedback] {
        const QString needle = search->text().trimmed();
        if (needle.isEmpty()) {
            feedback->setText(tr("Enter a port name to search."));
            feedback->show();
            return;
        }

        for (int portIndex = 0; portIndex < m_packagePorts.size(); ++portIndex) {
            const QJsonObject port = m_packagePorts.at(portIndex).toObject();
            if (!port.value(QStringLiteral("name")).toString().contains(needle, Qt::CaseInsensitive)) continue;

            const QJsonObject position = port.value(QStringLiteral("position")).toObject();
            m_lastPortSearchTerm = needle;
            m_lastPortSearchIndex = portIndex;
            m_dataTabs->setCurrentIndex(0);
            selectMapPosition(QPointF(position.value(QStringLiteral("pixel_center_x")).toInt(),
                                      position.value(QStringLiteral("pixel_center_y")).toInt()), true);
            dialog.accept();
            return;
        }
        feedback->setText(tr("No port name matches “%1”.").arg(needle));
        feedback->show();
    };
    connect(search, &QLineEdit::returnPressed, &dialog, submitSearch);
    connect(findButton, &QPushButton::clicked, &dialog, submitSearch);

    search->setFocus(Qt::ShortcutFocusReason);
    search->selectAll();
    dialog.exec();
}

void MainWindow::findNextPort()
{
    if (m_lastPortSearchTerm.isEmpty() || m_lastPortSearchIndex < 0) {
        statusBar()->showMessage(tr("No successful port search yet. Press Ctrl+F to search by name."), 5000);
        return;
    }
    if (m_packagePorts.isEmpty()) {
        statusBar()->showMessage(tr("No generated map package is open."), 5000);
        return;
    }

    int candidateIndex = m_lastPortSearchIndex;
    for (int examined = 0; examined < m_packagePorts.size(); ++examined) {
        ++candidateIndex;
        if (candidateIndex == m_packagePorts.size()) candidateIndex = 0;
        const QJsonObject port = m_packagePorts.at(candidateIndex).toObject();
        if (!port.value(QStringLiteral("name")).toString().contains(m_lastPortSearchTerm, Qt::CaseInsensitive)) continue;

        m_lastPortSearchIndex = candidateIndex;
        const QJsonObject position = port.value(QStringLiteral("position")).toObject();
        m_dataTabs->setCurrentIndex(0);
        selectMapPosition(QPointF(position.value(QStringLiteral("pixel_center_x")).toInt(),
                                  position.value(QStringLiteral("pixel_center_y")).toInt()), true);
        statusBar()->showMessage(tr("Next port matching “%1”: %2").arg(
            m_lastPortSearchTerm, port.value(QStringLiteral("name")).toString()), 4000);
        return;
    }
    statusBar()->showMessage(tr("No port name matches the previous search “%1”. Press Ctrl+F to search again.")
                                 .arg(m_lastPortSearchTerm), 5000);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && !QApplication::activeModalWidget()) {
        auto *widget = qobject_cast<QWidget *>(watched);
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (widget && widget->window() == this && keyEvent->modifiers() == Qt::NoModifier) {
            if (keyEvent->key() == Qt::Key_F3) {
                findNextPort();
                return true;
            }
            if (keyEvent->key() == Qt::Key_F4) {
                openFleetStatusDialog();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::openSaveSlotDialog()
{
    if (m_saveSlots.isEmpty()) {
        QMessageBox::information(this, tr("No Loaded SAVE.DAT"),
                                 tr("No SAVE.DAT with an allocated save slot is currently loaded.\n\n"
                                    "Use File → Set Optional SAVE.DAT… first."));
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("saveSlotDialog"));
    dialog.setWindowTitle(tr("Select SAVE.DAT Slot"));
    dialog.setModal(true);
    auto *layout = new QVBoxLayout(&dialog);
    auto *prompt = new QLabel(tr("Loaded read-only SAVE.DAT: %1\nSelect a save slot:")
                                  .arg(QFileInfo(m_saveDataPath).fileName()), &dialog);
    prompt->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto *slotList = new QListWidget(&dialog);
    slotList->setObjectName(QStringLiteral("saveSlotList"));
    slotList->setSelectionMode(QAbstractItemView::SingleSelection);
    slotList->setMinimumWidth(360);
    for (const SaveSlot &saveSlot : m_saveSlots) {
        const QString slotName = saveSlot.slotName.isEmpty() ? tr("unnamed save") : saveSlot.slotName;
        const QString treasureState = saveSlot.hasActiveTreasure
            ? tr(" — active treasure: %1").arg(saveSlot.activeTreasure.itemName.isEmpty()
                                                 ? tr("unnamed") : saveSlot.activeTreasure.itemName)
            : tr(" — no active treasure");
        auto *item = new QListWidgetItem(tr("Slot %1: %2%3").arg(saveSlot.slot + 1).arg(slotName, treasureState), slotList);
        item->setData(Qt::UserRole, saveSlot.slot);
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        if (saveSlot.slot == m_selectedSaveSlot) slotList->setCurrentItem(item);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(prompt);
    layout->addWidget(slotList);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(slotList, &QListWidget::itemClicked, &dialog, [this, &dialog](QListWidgetItem *item) {
        if (!item) return;
        if (activateSaveSlot(item->data(Qt::UserRole).toInt())) dialog.accept();
    });
    slotList->setFocus(Qt::ShortcutFocusReason);
    dialog.exec();
}

void MainWindow::openFleetStatusDialog()
{
    if (m_selectedSaveSlot < 0 || m_saveSlots.isEmpty()) {
        QMessageBox::information(this, tr("No Loaded SAVE.DAT"),
                                 tr("Fleet status requires an allocated SAVE.DAT slot.\n\n"
                                    "Use File → Set Optional SAVE.DAT… and select a slot with F2."));
        return;
    }
    const auto saveSlot = std::find_if(m_saveSlots.cbegin(), m_saveSlots.cend(), [this](const SaveSlot &candidate) {
        return candidate.slot == m_selectedSaveSlot;
    });
    if (saveSlot == m_saveSlots.cend()) {
        QMessageBox::warning(this, tr("Fleet Status Unavailable"),
                             tr("The selected SAVE.DAT slot is no longer available."));
        return;
    }

    const PlayerFleetStatus &fleet = saveSlot->playerFleet;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("fleetStatusDialog"));
    dialog.setWindowTitle(tr("Fleet Status"));
    dialog.setModal(true);
    dialog.resize(960, 620);
    auto *layout = new QVBoxLayout(&dialog);
    auto *summary = new QLabel(
        tr("Hero: %1 · SAVE.DAT slot %2: %3 · Gold: %4 · Active ships: %5")
            .arg(fleet.heroName.isEmpty() ? tr("unnamed hero") : fleet.heroName)
            .arg(saveSlot->slot + 1)
            .arg(saveSlot->slotName.isEmpty() ? tr("unnamed save") : saveSlot->slotName)
            .arg(fleet.gold)
            .arg(fleet.ships.size()), &dialog);
    summary->setObjectName(QStringLiteral("fleetStatusSummary"));
    summary->setWordWrap(true);
    layout->addWidget(summary);

    if (fleet.ships.isEmpty()) {
        auto *empty = new QLabel(tr("No active player ships are present in this SAVE.DAT slot."), &dialog);
        empty->setWordWrap(true);
        layout->addWidget(empty);
    } else {
        auto *tabs = new QTabWidget(&dialog);
        tabs->setObjectName(QStringLiteral("fleetStatusTabs"));
        auto *fleetTable = new QTableWidget(fleet.ships.size(), 10, tabs);
        fleetTable->setObjectName(QStringLiteral("fleetSummaryTable"));
        fleetTable->setHorizontalHeaderLabels({tr("Slot"), tr("Ship"), tr("Captain"), tr("Crew"),
                                               tr("Hull"), tr("Sails"), tr("Speed"), tr("Guns"),
                                               tr("Morale"), tr("Cargo")});
        fleetTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        fleetTable->setSelectionMode(QAbstractItemView::NoSelection);
        fleetTable->setAlternatingRowColors(true);
        fleetTable->verticalHeader()->setVisible(false);
        for (int column = 0; column < fleetTable->columnCount(); ++column) {
            fleetTable->horizontalHeader()->setSectionResizeMode(column,
                column == 1 || column == 2 ? QHeaderView::Stretch : QHeaderView::ResizeToContents);
        }
        for (int row = 0; row < fleet.ships.size(); ++row) {
            const PlayerShipStatus &ship = fleet.ships.at(row);
            const QStringList values = {
                QString::number(ship.slot + 1), ship.shipName, ship.captainName,
                tr("%1 / %2").arg(ship.crew).arg(ship.maxCrew), QString::number(ship.hull),
                QString::number(ship.sails), QString::number(ship.speed), QString::number(ship.guns),
                QString::number(ship.morale), tr("%1 / %2").arg(ship.cargoUsed).arg(ship.cargoCapacity),
            };
            for (int column = 0; column < values.size(); ++column) {
                auto *item = new QTableWidgetItem(values.at(column));
                if (column != 1 && column != 2) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                fleetTable->setItem(row, column, item);
            }
        }
        tabs->addTab(fleetTable, tr("Fleet (%1)").arg(fleet.ships.size()));

        for (const PlayerShipStatus &ship : fleet.ships) {
            auto *shipPage = new QWidget(tabs);
            auto *shipLayout = new QVBoxLayout(shipPage);
            auto *details = new QLabel(
                tr("Slot: %1\nShip: %2\nCaptain: %3\nCrew: %4 / %5\n"
                   "Hull: %6 · Sails: %7 · Speed: %8 · Guns: %9 · Morale: %10\n"
                   "Cargo: %11 / %12")
                    .arg(ship.slot + 1).arg(ship.shipName, ship.captainName)
                    .arg(ship.crew).arg(ship.maxCrew).arg(ship.hull).arg(ship.sails)
                    .arg(ship.speed).arg(ship.guns).arg(ship.morale)
                    .arg(ship.cargoUsed).arg(ship.cargoCapacity), shipPage);
            details->setWordWrap(true);
            shipLayout->addWidget(details);
            auto *stockTable = new QTableWidget(ship.stock.size(), 2, shipPage);
            stockTable->setObjectName(QStringLiteral("fleetStockTable_%1").arg(ship.slot));
            stockTable->setHorizontalHeaderLabels({tr("Cargo / Stock"), tr("Amount")});
            stockTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
            stockTable->setSelectionMode(QAbstractItemView::NoSelection);
            stockTable->setAlternatingRowColors(true);
            stockTable->verticalHeader()->setVisible(false);
            stockTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
            stockTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
            for (int row = 0; row < ship.stock.size(); ++row) {
                const ShipStock &stock = ship.stock.at(row);
                stockTable->setItem(row, 0, new QTableWidgetItem(stock.itemName));
                auto *amount = new QTableWidgetItem(QString::number(stock.amount));
                amount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                stockTable->setItem(row, 1, amount);
            }
            shipLayout->addWidget(stockTable);
            tabs->addTab(shipPage, tr("%1: %2").arg(ship.slot + 1).arg(ship.shipName));
        }
        layout->addWidget(tabs);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->setObjectName(QStringLiteral("fleetStatusCloseButtons"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.setFocus(Qt::ShortcutFocusReason);
    dialog.exec();
}

void MainWindow::openMarketPricesDialog()
{
    if (m_selectedPortIndex < 0 || m_selectedPortIndex >= m_packagePorts.size()) {
        QMessageBox::information(this, tr("No Port Selected"),
                                 tr("Select a port on the map or in the port list before opening market prices."));
        return;
    }
    if (m_selectedSaveSlot < 0 || m_saveSlots.isEmpty()) {
        QMessageBox::information(this, tr("No Loaded SAVE.DAT"),
                                 tr("Market prices require an allocated SAVE.DAT slot.\n\n"
                                    "Use File → Set Optional SAVE.DAT… and select a slot with F2."));
        return;
    }

    const auto saveSlot = std::find_if(m_saveSlots.cbegin(), m_saveSlots.cend(), [this](const SaveSlot &candidate) {
        return candidate.slot == m_selectedSaveSlot;
    });
    if (saveSlot == m_saveSlots.cend()) {
        QMessageBox::warning(this, tr("Market Price Data Unavailable"),
                             tr("The selected SAVE.DAT slot is no longer available."));
        return;
    }

    const QJsonObject port = m_packagePorts.at(m_selectedPortIndex).toObject();
    const int portId = port.value(QStringLiteral("id")).toInt(-1);
    const auto portMarket = std::find_if(saveSlot->portMarkets.cbegin(), saveSlot->portMarkets.cend(),
                                         [portId](const PortMarket &candidate) {
        return candidate.portId == portId;
    });
    if (portMarket == saveSlot->portMarkets.cend()) {
        QMessageBox::information(this, tr("Market Price Data Unavailable"),
                                 tr("The original SAVE.DAT format has no market-economy record for %1 "
                                    "(port record ID %2).\n\n"
                                    "Only port record IDs 0–49 have original price data; this viewer does not "
                                    "estimate prices for the remaining map ports.")
                                     .arg(port.value(QStringLiteral("name")).toString())
                                     .arg(portId));
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("marketPricesDialog"));
    dialog.setWindowTitle(tr("시세 — %1").arg(port.value(QStringLiteral("name")).toString()));
    dialog.setModal(true);
    dialog.resize(920, 432);
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        tr("SAVE.DAT slot %1: %2\nBuy: player buys from the port · Sell: player sells to the port")
            .arg(saveSlot->slot + 1)
            .arg(saveSlot->slotName.isEmpty() ? tr("unnamed save") : saveSlot->slotName), &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    constexpr int priceSetWidth = 3;
    constexpr int priceSetsPerRow = 3;
    const int tableRows = (portMarket->prices.size() + priceSetsPerRow - 1) / priceSetsPerRow;
    auto *table = new QTableWidget(tableRows, priceSetWidth * priceSetsPerRow, &dialog);
    table->setObjectName(QStringLiteral("marketPricesTable"));
    QStringList headers;
    for (int set = 0; set < priceSetsPerRow; ++set) {
        headers << tr("Item") << tr("Buy") << tr("Sell");
    }
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (int set = 0; set < priceSetsPerRow; ++set) {
        const int column = set * priceSetWidth;
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(column + 1, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(column + 2, QHeaderView::ResizeToContents);
    }
    for (int index = 0; index < portMarket->prices.size(); ++index) {
        const int row = index / priceSetsPerRow;
        const int column = (index % priceSetsPerRow) * priceSetWidth;
        const MarketPrice &price = portMarket->prices.at(index);
        auto *name = new QTableWidgetItem(price.goodsName);
        name->setData(Qt::UserRole, price.goodsId);
        if (price.purchasePrice >= 0) {
            name->setForeground(QColor(0, 66, 170));
            QFont availableItemFont = name->font();
            availableItemFont.setBold(true);
            name->setFont(availableItemFont);
            name->setToolTip(tr("Available for purchase from this port."));
        }
        table->setItem(row, column, name);
        auto *purchase = new QTableWidgetItem(price.purchasePrice < 0
                                                   ? QStringLiteral("—") : QString::number(price.purchasePrice));
        purchase->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, column + 1, purchase);
        auto *sale = new QTableWidgetItem(QString::number(price.salePrice));
        sale->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, column + 2, sale);
    }
    layout->addWidget(table);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->setObjectName(QStringLiteral("marketPricesCloseButtons"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.setFocus(Qt::ShortcutFocusReason);
    dialog.exec();
}

bool MainWindow::activateSaveSlot(int slotNumber)
{
    const auto slot = std::find_if(m_saveSlots.cbegin(), m_saveSlots.cend(), [slotNumber](const SaveSlot &candidate) {
        return candidate.slot == slotNumber;
    });
    if (slot == m_saveSlots.cend()) return false;

    m_selectedSaveSlot = slot->slot;
    m_activeTreasures.clear();
    if (slot->hasActiveTreasure) m_activeTreasures.append(slot->activeTreasure);
    populatePackageUi();
    if (m_selectionItem) {
        selectMapPosition(m_selectionItem->rect().center(), false);
    } else {
        m_treasureDetailsLabel->setText(slot->hasActiveTreasure
            ? tr("Selected SAVE.DAT slot %1: %2").arg(slot->slot + 1).arg(slot->slotName)
            : tr("Selected SAVE.DAT slot %1: no active treasure.").arg(slot->slot + 1));
    }
    saveUserState();
    statusBar()->showMessage(tr("Selected SAVE.DAT slot %1: %2 (%3 active treasure location(s), read-only).").arg(
        slot->slot + 1).arg(slot->slotName.isEmpty() ? tr("unnamed") : slot->slotName).arg(m_activeTreasures.size()));
    return true;
}

void MainWindow::selectMapPosition(const QPointF &scenePosition, bool centerView)
{
    if (!m_tileMapItem) return;
    const QJsonObject grid = m_packageMetadata.value(QStringLiteral("grid")).toObject();
    const int cellWidth = grid.value(QStringLiteral("cell_pixel_width")).toInt();
    const int cellHeight = grid.value(QStringLiteral("cell_pixel_height")).toInt();
    const int cellsPerSector = grid.value(QStringLiteral("sub_cells_per_sector")).toInt();
    const int imageWidth = m_mapImageSize.width();
    const int imageHeight = m_mapImageSize.height();
    if (cellWidth <= 0 || cellHeight <= 0 || cellsPerSector <= 0) return;

    const int pixelX = qBound(0, static_cast<int>(scenePosition.x()), imageWidth - 1);
    const int pixelY = qBound(0, static_cast<int>(scenePosition.y()), imageHeight - 1);
    const int cellX = pixelX / cellWidth;
    const int cellY = pixelY / cellHeight;
    const int sectorX = cellX / cellsPerSector;
    const int sectorY = cellY / cellsPerSector;
    const int subX = cellX % cellsPerSector;
    const int subY = cellY % cellsPerSector;

    const QJsonObject terrainModel = m_packageMetadata.value(QStringLiteral("terrain")).toObject();
    const QByteArray terrainBytes = QByteArray::fromBase64(
        terrainModel.value(QStringLiteral("data_base64")).toString().toLatin1());
    const int terrainIndex = ((sectorY * 72 + sectorX) * 49) + (subY / 2) * 7 + (subX / 2);
    const int terrainId = terrainIndex >= 0 && terrainIndex < terrainBytes.size()
        ? static_cast<unsigned char>(terrainBytes.at(terrainIndex)) : -1;

    const QJsonObject treasureModel = m_packageMetadata.value(QStringLiteral("treasure_model")).toObject();
    const QByteArray candidateBits = QByteArray::fromBase64(
        treasureModel.value(QStringLiteral("cell_bitset_base64")).toString().toLatin1());
    const int candidateIndex = cellY * 1008 + cellX;
    const bool isTreasureCandidate = candidateIndex >= 0 && candidateIndex / 8 < candidateBits.size()
        && (static_cast<unsigned char>(candidateBits.at(candidateIndex / 8)) & (1u << (candidateIndex % 8)));

    if (m_selectionItem) {
        m_scene->removeItem(m_selectionItem);
        delete m_selectionItem;
    }
    m_selectionItem = m_scene->addRect(cellX * cellWidth, cellY * cellHeight, cellWidth, cellHeight,
                                        QPen(QColor(255, 255, 255), 1.5), Qt::NoBrush);
    m_selectionItem->setData(1, QStringLiteral("mapCellSelection"));
    m_selectionItem->setZValue(40);

    int matchedPort = -1;
    QString matchedPortName;
    for (int row = 0; row < m_packagePorts.size(); ++row) {
        const QJsonObject position = m_packagePorts.at(row).toObject().value(QStringLiteral("position")).toObject();
        if (position.value(QStringLiteral("sector_x")).toInt() == sectorX
            && position.value(QStringLiteral("sector_y")).toInt() == sectorY
            && position.value(QStringLiteral("sub_x")).toInt() == subX
            && position.value(QStringLiteral("sub_y")).toInt() == subY) {
            matchedPort = row;
            matchedPortName = m_packagePorts.at(row).toObject().value(QStringLiteral("name")).toString();
            break;
        }
    }

    int matchedTreasure = -1;
    for (int index = 0; index < m_activeTreasures.size(); ++index) {
        const ActiveTreasure &treasure = m_activeTreasures.at(index);
        if (treasure.sectorX == sectorX && treasure.sectorY == sectorY
            && treasure.subX == subX && treasure.subY == subY) {
            matchedTreasure = index;
            break;
        }
    }

    if (matchedPort >= 0) {
        m_selectedPortIndex = matchedPort;
        m_portSelectionColorIndex = 0;
        updatePortSelectionHighlight();
        m_portSelectionAnimationTimer->start();
    } else {
        m_selectedPortIndex = -1;
        m_portSelectionAnimationTimer->stop();
    }

    const int latitudeSigned = sectorY < 18 ? 90 - 5 * sectorY : -(5 * sectorY - 90);
    const int longitudeRemainder = (sectorX + 38) % 72;
    const QString latitude = QStringLiteral("%1%2").arg(qAbs(latitudeSigned)).arg(latitudeSigned >= 0 ? "N" : "S");
    const QString longitude = QStringLiteral("%1%2").arg(longitudeRemainder < 36 ? longitudeRemainder * 5 : (72 - longitudeRemainder) * 5)
                                .arg(longitudeRemainder < 36 ? "E" : "W");
    QString selection = tr("Pixel (%1, %2)\nCell (%3, %4)\nSector (%5, %6), sub (%7, %8)\n"
                           "Terrain: %9%10\n%11, %12")
                            .arg(pixelX).arg(pixelY).arg(cellX).arg(cellY)
                            .arg(sectorX).arg(sectorY).arg(subX).arg(subY)
                            .arg(terrainId < 0 ? tr("not packaged") : QString::number(terrainId))
                            .arg(isTreasureCandidate ? tr(" (quest treasure candidate)") : QString())
                            .arg(latitude, longitude);
    if (matchedPort >= 0) selection += tr("\nPort: %1").arg(matchedPortName);
    if (matchedTreasure >= 0) {
        const ActiveTreasure &treasure = m_activeTreasures.at(matchedTreasure);
        selection += tr("\nActive treasure (slot %1): %2, value %3")
                         .arg(treasure.slot + 1)
                         .arg(treasure.itemName.isEmpty() ? tr("unnamed") : treasure.itemName)
                         .arg(treasure.itemValue);
    }
    m_selectionLabel->setText(selection);
    if (matchedPort >= 0) {
        const QJsonObject port = m_packagePorts.at(matchedPort).toObject();
        const QJsonObject politicalStatus = port.value(QStringLiteral("political_status")).toObject();
        const bool courtAvailable = politicalStatus.value(QStringLiteral("court_available")).toBool();
        const bool rulerAvailable = politicalStatus.value(QStringLiteral("ruler_available")).toBool();
        const QString rulerTitle = politicalStatus.value(QStringLiteral("ruler_title")).toString();
        const QString courtText = courtAvailable
            ? (rulerAvailable ? tr("Available (%1)").arg(rulerTitle) : tr("Available"))
            : tr("Not available in the initial scenario");
        m_portDetailsLabel->setText(
            tr("Name: %1\nInitial sovereignty: %2\nPolitical status: %3\nCourt / ruler: %4\n"
               "Guild: %5\nInitially discovered: %6\nPort record ID: %7")
                .arg(port.value(QStringLiteral("name")).toString(),
                     politicalStatus.value(QStringLiteral("sovereignty")).toString(),
                     politicalStatus.value(QStringLiteral("independent")).toBool()
                         ? tr("Independent") : tr("National territory"),
                     courtText,
                     politicalStatus.value(QStringLiteral("guild_available")).toBool()
                         ? tr("Available") : tr("Not available"),
                     port.value(QStringLiteral("attributes")).toObject()
                         .value(QStringLiteral("discovered_initially")).toBool()
                         ? tr("Yes") : tr("No"),
                     QString::number(port.value(QStringLiteral("id")).toInt())));
    } else {
        m_portDetailsLabel->setText(tr("No port is located in this tile."));
    }
    if (matchedTreasure >= 0) {
        const ActiveTreasure &treasure = m_activeTreasures.at(matchedTreasure);
        m_treasureDetailsLabel->setText(
            tr("Active SAVE.DAT treasure\nSlot: %1 (%2)\nItem: %3\nValue: %4\n"
               "Location: sector (%5, %6), sub (%7, %8)")
                .arg(treasure.slot + 1)
                .arg(treasure.slotName.isEmpty() ? tr("unnamed save slot") : treasure.slotName)
                .arg(treasure.itemName.isEmpty() ? tr("unnamed treasure") : treasure.itemName)
                .arg(treasure.itemValue).arg(treasure.sectorX).arg(treasure.sectorY)
                .arg(treasure.subX).arg(treasure.subY));
    } else if (isTreasureCandidate) {
        m_treasureDetailsLabel->setText(
            tr("Possible quest-treasure cell\nThis terrain/quadrant is eligible for a quest target. "
               "Load SAVE.DAT to see an active treasure's item, value, and exact slot."));
    } else {
        m_treasureDetailsLabel->setText(tr("No active treasure is located in this tile."));
    }
    if (matchedPort >= 0 && !m_syncingSelection) {
        m_syncingSelection = true;
        for (int row = 0; row < m_portsTable->rowCount(); ++row) {
            if (m_portsTable->item(row, 0)->data(Qt::UserRole).toInt() == matchedPort) {
                m_portsTable->selectRow(row);
                break;
            }
        }
        m_syncingSelection = false;
    }
    if (matchedTreasure >= 0 && !m_syncingSelection) {
        m_syncingSelection = true;
        for (int row = 0; row < m_treasuresTable->rowCount(); ++row) {
            if (m_treasuresTable->item(row, 0)->data(Qt::UserRole).toInt() == matchedTreasure) {
                m_treasuresTable->selectRow(row);
                break;
            }
        }
        m_syncingSelection = false;
    }
    if (centerView) m_mapView->centerOn(cellX * cellWidth + cellWidth / 2, cellY * cellHeight + cellHeight / 2);
}

void MainWindow::updatePortSelectionHighlight()
{
    if (!m_selectionItem || m_selectedPortIndex < 0) return;

    static const QColor rainbowColors[] = {
        QColor(255, 72, 72), QColor(255, 156, 58), QColor(255, 230, 62), QColor(92, 235, 105),
        QColor(62, 220, 255), QColor(82, 126, 255), QColor(213, 92, 255),
    };
    QPen pen(rainbowColors[m_portSelectionColorIndex], 2.0);
    pen.setCosmetic(true);
    m_selectionItem->setPen(pen);
}

void MainWindow::selectPortRow(int row)
{
    if (m_syncingSelection || row < 0 || row >= m_portsTable->rowCount()) return;
    const int portIndex = m_portsTable->item(row, 0)->data(Qt::UserRole).toInt();
    if (portIndex < 0 || portIndex >= m_packagePorts.size()) return;
    const QJsonObject position = m_packagePorts.at(portIndex).toObject().value(QStringLiteral("position")).toObject();
    selectMapPosition(QPointF(position.value(QStringLiteral("pixel_center_x")).toInt(),
                              position.value(QStringLiteral("pixel_center_y")).toInt()), true);
}

void MainWindow::selectTreasureRow(int row)
{
    if (m_syncingSelection || row < 0 || row >= m_treasuresTable->rowCount()) return;
    const int treasureIndex = m_treasuresTable->item(row, 0)->data(Qt::UserRole).toInt();
    if (treasureIndex < 0 || treasureIndex >= m_activeTreasures.size()) return;
    const ActiveTreasure &treasure = m_activeTreasures.at(treasureIndex);
    const QJsonObject grid = m_packageMetadata.value(QStringLiteral("grid")).toObject();
    const int cellWidth = grid.value(QStringLiteral("cell_pixel_width")).toInt();
    const int cellHeight = grid.value(QStringLiteral("cell_pixel_height")).toInt();
    selectMapPosition(QPointF((treasure.sectorX * 14 + treasure.subX) * cellWidth + cellWidth / 2.0,
                              (treasure.sectorY * 14 + treasure.subY) * cellHeight + cellHeight / 2.0), true);
}

void MainWindow::updateVisibleTiles(const QRectF &sceneRect, qreal verticalSceneScale)
{
    if (m_tileMapItem) m_tileMapItem->updateVisibleRegion(sceneRect, verticalSceneScale);
}

void MainWindow::applyTableFilter(QTableWidget *table, const QString &text)
{
    if (!table) return;
    const QString needle = text.trimmed();
    for (int row = 0; row < table->rowCount(); ++row) {
        bool match = needle.isEmpty();
        for (int column = 0; !match && column < table->columnCount(); ++column) {
            const QTableWidgetItem *item = table->item(row, column);
            match = item && item->text().contains(needle, Qt::CaseInsensitive);
        }
        table->setRowHidden(row, !match);
    }
}

void MainWindow::setOptionalSaveData()
{
    const QString startPath = m_saveDataPath.isEmpty() ? QDir::homePath() : m_saveDataPath;
    const QString path = QFileDialog::getOpenFileName(this, tr("Select original SAVE.DAT"), startPath,
                                                       tr("Water1 save data (SAVE.DAT);;All files (*)"));
    if (path.isEmpty()) return;

    loadOptionalSaveData(path, true);
}

bool MainWindow::loadOptionalSaveData(const QString &path, bool showError)
{
    if (!m_tileMapItem) {
        if (showError) {
            QMessageBox::information(this, tr("No Map Package"),
                                     tr("Open a generated map package before selecting SAVE.DAT."));
        }
        return false;
    }
    const QFileInfo saveFile(path);
    if (!saveFile.isFile()) {
        if (showError) QMessageBox::warning(this, tr("Cannot Read SAVE.DAT"), tr("SAVE.DAT does not exist: %1").arg(path));
        return false;
    }
    QList<SaveSlot> saveSlots;
    QString error;
    if (!SaveDataReader::readSaveSlots(saveFile.absoluteFilePath(), &saveSlots, &error)) {
        if (showError) QMessageBox::warning(this, tr("Cannot Read SAVE.DAT"), error);
        return false;
    }
    m_saveDataPath = saveFile.absoluteFilePath();
    m_saveSlots = saveSlots;
    m_selectedSaveSlot = -1;
    m_activeTreasures.clear();
    if (!m_saveSlots.isEmpty()) {
        activateSaveSlot(m_saveSlots.first().slot);
    } else {
        populatePackageUi();
        saveUserState();
    }
    statusBar()->showMessage(tr("Loaded %1 allocated SAVE.DAT slot(s) from %2 (read-only).")
                                 .arg(m_saveSlots.size()).arg(saveFile.fileName()));
    return true;
}

void MainWindow::setOriginalDataDirectory()
{
    const QString startDirectory = m_originalDataDirectory.isEmpty()
        ? QDir::homePath() : m_originalDataDirectory;
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Select Water1 Original Data Directory"), startDirectory,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.isEmpty()) return;

    applyReport(SourceDirectory::validate(path), true);
}

void MainWindow::validateOriginalDataDirectory()
{
    if (m_originalDataDirectory.isEmpty()) {
        QMessageBox::information(this, tr("No Original Data Directory"),
                                 tr("Choose File → Set Original Data Directory… first."));
        return;
    }
    applyReport(SourceDirectory::validate(m_originalDataDirectory), true);
}

void MainWindow::generateMapPackage()
{
    if (!m_sourceReport.valid) {
        QMessageBox::warning(this, tr("Source Data Not Ready"),
                             tr("Validate a recognized original data directory before generation."));
        return;
    }
    if (m_generator) return;

    QString outputStartDirectory = m_outputDirectory.isEmpty() ? QDir::homePath() : m_outputDirectory;
    if (!QDir(outputStartDirectory).exists() && !QDir().mkpath(outputStartDirectory)) {
        QMessageBox::warning(this, tr("Cannot Create Default Output Directory"),
                             tr("Cannot create the configured output directory:\n%1").arg(outputStartDirectory));
        outputStartDirectory = QDir::homePath();
    }
    const QString outputDirectory = QFileDialog::getExistingDirectory(
        this, tr("Select Output Directory for Map Package"), outputStartDirectory,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (outputDirectory.isEmpty()) return;
    m_outputDirectory = QFileInfo(outputDirectory).absoluteFilePath();
    saveUserState();

    m_progressDialog = new QProgressDialog(tr("Generating the 24,192 × 6,048 world map…"),
                                            tr("Cancel"), 0, 72 * 36, this);
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setAutoClose(false);
    m_progressDialog->setAutoReset(false);

    m_generator = new WorldMapGenerator(GenerationRequest{m_sourceReport, outputDirectory}, this);
    connect(m_progressDialog, &QProgressDialog::canceled, m_generator, &QThread::requestInterruption);
    connect(m_generator, &WorldMapGenerator::generationProgress, this,
            [this](int complete, int total) {
                if (m_progressDialog) {
                    m_progressDialog->setMaximum(total);
                    m_progressDialog->setValue(complete);
                }
            });
    connect(m_generator, &WorldMapGenerator::generationSucceeded, this, [this](const QString &packagePath) {
        if (m_progressDialog) {
            m_progressDialog->setValue(m_progressDialog->maximum());
            m_progressDialog->deleteLater();
            m_progressDialog = nullptr;
        }
        statusBar()->showMessage(tr("Generated map package: %1").arg(packagePath));
        QString openError;
        if (!openMapPackage(QDir(packagePath).filePath(QStringLiteral("world_map.json")), &openError)) {
            QMessageBox::warning(this, tr("Map Package Generated, But Could Not Open It"), openError);
        }
    });
    connect(m_generator, &WorldMapGenerator::generationFailed, this, [this](const QString &error) {
        if (m_progressDialog) {
            m_progressDialog->deleteLater();
            m_progressDialog = nullptr;
        }
        QMessageBox::warning(this, tr("Map Generation Failed"), error);
    });
    connect(m_generator, &QThread::finished, this, [this] {
        if (m_generator) {
            m_generator->deleteLater();
            m_generator = nullptr;
        }
        updateWindowState();
    });
    m_progressDialog->show();
    m_generator->start();
}

void MainWindow::applyReport(const SourceDirectoryReport &report, bool showDialog)
{
    m_sourceReport = report;
    if (!report.rootPath.isEmpty()) m_originalDataDirectory = report.rootPath;

    saveUserState();

    updateWindowState();
    if (showDialog) {
        QMessageBox box(report.valid ? QMessageBox::Information : QMessageBox::Warning,
                        report.valid ? tr("Original Data Directory Valid")
                                     : tr("Original Data Directory Not Ready"),
                        report.toText(), QMessageBox::Ok, this);
        box.setTextFormat(Qt::PlainText);
        box.exec();
    }
}

void MainWindow::tryAutomaticStartupLoad()
{
    if (!m_tileMapItem && m_autoOpenGeneratedPackage) {
        QString metadataPath = m_lastPackagePath;
        if (metadataPath.isEmpty() && m_sourceReport.valid && !m_outputDirectory.isEmpty()) {
            metadataPath = QDir(m_outputDirectory).filePath(
                QStringLiteral("uw1-eng-dos-%1/world_map.json").arg(m_sourceReport.profileId));
        }
        if (QFileInfo(metadataPath).isFile()) {
            QString error;
            if (!openMapPackage(metadataPath, &error)) {
                statusBar()->showMessage(tr("Automatic map-package load skipped: %1").arg(error), 10000);
            }
        } else if (!metadataPath.isEmpty()) {
            statusBar()->showMessage(tr("Automatic map-package load skipped: world_map.json was not found."), 6000);
        }
    }
    if (m_tileMapItem && m_autoLoadSaveData && !m_saveDataPath.isEmpty()) {
        if (!loadOptionalSaveData(m_saveDataPath, false)) {
            statusBar()->showMessage(tr("Automatic SAVE.DAT load skipped: file is absent or invalid."), 6000);
        }
    }
}

void MainWindow::saveUserState() const
{
    if (m_userSettingsPath.isEmpty()) return;
    QSettings settings(m_userSettingsPath, QSettings::IniFormat);
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(QString::fromLatin1(kOriginalDataDirectoryKey), m_originalDataDirectory);
    settings.setValue(QString::fromLatin1(kOutputDirectoryKey), m_outputDirectory);
    settings.setValue(QString::fromLatin1(kLastPackagePathKey), m_lastPackagePath);
    settings.setValue(QString::fromLatin1(kOptionalSaveDataPathKey), m_saveDataPath);
    settings.setValue(QStringLiteral("autoOpenGeneratedPackage"), m_autoOpenGeneratedPackage);
    settings.setValue(QStringLiteral("autoLoadSaveData"), m_autoLoadSaveData);
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    if (m_verticalSplitter) {
        settings.setValue(QStringLiteral("verticalSplitterSizes"), splitterSizesForSettings(m_verticalSplitter->sizes()));
    }
    if (m_upperSplitter) {
        settings.setValue(QStringLiteral("upperSplitterSizes"), splitterSizesForSettings(m_upperSplitter->sizes()));
    }
    settings.endGroup();
    settings.sync();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveUserState();
    QMainWindow::closeEvent(event);
}

void MainWindow::updateWindowState()
{
    const bool valid = m_sourceReport.valid;
    m_generateAction->setEnabled(valid && !m_generator);
    m_setSaveAction->setEnabled(m_tileMapItem != nullptr && !m_generator);
    m_dataDirectoryLabel->setText(m_originalDataDirectory.isEmpty()
                                  ? tr("Not set") : m_originalDataDirectory);
    m_profileLabel->setText(valid ? m_sourceReport.profileId
                                  : tr("Not ready — validate a recognized source profile."));

    if (valid) {
        statusBar()->showMessage(tr("Source profile %1 is valid. Map generation is the next implementation milestone.")
                                     .arg(m_sourceReport.profileId));
    } else if (m_originalDataDirectory.isEmpty()) {
        statusBar()->showMessage(tr("Choose an original data directory to begin."));
    } else {
        statusBar()->showMessage(tr("The selected original data directory is not ready; use Validate for details."));
    }
}
