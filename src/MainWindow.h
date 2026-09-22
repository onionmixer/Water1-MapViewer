#ifndef WATER1_MAPVIEWER_MAIN_WINDOW_H
#define WATER1_MAPVIEWER_MAIN_WINDOW_H

#include "SourceDirectory.h"
#include "SaveDataReader.h"

#include <QMainWindow>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>

class QAction;
class QLabel;
class QGraphicsScene;
class MapCanvas;
class QGraphicsRectItem;
class QGraphicsItem;
class TilePyramidItem;
class MapDataOverlayItem;
class QLineEdit;
class QTabWidget;
class QSplitter;
class QCloseEvent;
class QTableWidget;
class QProgressDialog;
class QShortcut;
class QTimer;
class QPushButton;
class WorldMapGenerator;

class MainWindow final : public QMainWindow {
public:
    MainWindow();
    bool openMapPackage(const QString &metadataPath, QString *error = nullptr);

private:
    void createActions();
    void createLayout();
    void setOriginalDataDirectory();
    void validateOriginalDataDirectory();
    void generateMapPackage();
    void openGeneratedMapPackage();
    void setOptionalSaveData();
    bool loadOptionalSaveData(const QString &path, bool showError);
    void tryAutomaticStartupLoad();
    void saveUserState() const;
    void populatePackageUi();
    void selectMapPosition(const QPointF &scenePosition, bool centerView);
    void selectPortRow(int row);
    void selectTreasureRow(int row);
    void openPortNameSearchDialog();
    void findNextPort();
    void openSaveSlotDialog();
    void openFleetStatusDialog();
    void openMarketPricesDialog();
    bool activateSaveSlot(int slotNumber);
    void updatePortSelectionHighlight();
    void updateVisibleTiles(const QRectF &sceneRect, qreal verticalSceneScale);
    void applyTableFilter(QTableWidget *table, const QString &text);
    void applyReport(const SourceDirectoryReport &report, bool showDialog);
    void updateWindowState();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString m_originalDataDirectory;
    QString m_outputDirectory;
    QString m_lastPackagePath;
    QString m_userSettingsPath;
    bool m_autoOpenGeneratedPackage = false;
    bool m_autoLoadSaveData = false;
    SourceDirectoryReport m_sourceReport;

    QAction *m_setDataDirectoryAction = nullptr;
    QAction *m_validateDataDirectoryAction = nullptr;
    QAction *m_generateAction = nullptr;
    QAction *m_openPackageAction = nullptr;
    QAction *m_setSaveAction = nullptr;
    QAction *m_findPortAction = nullptr;
    QShortcut *m_findPortShortcut = nullptr;
    QAction *m_findNextPortAction = nullptr;
    QShortcut *m_findNextPortShortcut = nullptr;
    QAction *m_selectSaveSlotAction = nullptr;
    QShortcut *m_selectSaveSlotShortcut = nullptr;
    QAction *m_fleetStatusAction = nullptr;
    QShortcut *m_fleetStatusShortcut = nullptr;
    QAction *m_showPortsAction = nullptr;
    QAction *m_showTreasureCandidatesAction = nullptr;
    QAction *m_showGridAction = nullptr;

    QGraphicsScene *m_scene = nullptr;
    MapCanvas *m_mapView = nullptr;
    QSplitter *m_verticalSplitter = nullptr;
    QSplitter *m_upperSplitter = nullptr;
    QLabel *m_dataDirectoryLabel = nullptr;
    QLabel *m_profileLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QLabel *m_portDetailsLabel = nullptr;
    QLabel *m_treasureDetailsLabel = nullptr;
    QPushButton *m_marketPricesButton = nullptr;
    QTableWidget *m_portsTable = nullptr;
    QTableWidget *m_treasuresTable = nullptr;
    QLineEdit *m_treasuresFilter = nullptr;
    QTabWidget *m_dataTabs = nullptr;
    TilePyramidItem *m_tileMapItem = nullptr;
    MapDataOverlayItem *m_mapDataOverlayItem = nullptr;
    QSize m_mapImageSize;
    QImage m_portMarkerImage;
    QGraphicsRectItem *m_selectionItem = nullptr;
    QTimer *m_portSelectionAnimationTimer = nullptr;
    int m_selectedPortIndex = -1;
    int m_portSelectionColorIndex = 0;
    QList<QGraphicsItem *> m_overlayItems;
    QList<QGraphicsItem *> m_portOverlayItems;
    QList<QGraphicsItem *> m_treasureOverlayItems;
    QJsonObject m_packageMetadata;
    QJsonArray m_packagePorts;
    QString m_lastPortSearchTerm;
    int m_lastPortSearchIndex = -1;
    QList<SaveSlot> m_saveSlots;
    QList<ActiveTreasure> m_activeTreasures;
    QString m_saveDataPath;
    int m_selectedSaveSlot = -1;
    bool m_syncingSelection = false;
    WorldMapGenerator *m_generator = nullptr;
    QProgressDialog *m_progressDialog = nullptr;
};

#endif
