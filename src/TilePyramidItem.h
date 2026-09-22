#ifndef WATER1_MAPVIEWER_TILE_PYRAMID_ITEM_H
#define WATER1_MAPVIEWER_TILE_PYRAMID_ITEM_H

#include <QCache>
#include <QGraphicsObject>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QSet>
#include <QSize>

class TilePyramidItem final : public QGraphicsObject {
    Q_OBJECT

public:
    explicit TilePyramidItem(QSize fullImageSize, QJsonObject pyramid, QJsonObject manifest,
                             QString packageDirectory, QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void updateVisibleRegion(const QRectF &sceneRect, qreal verticalSceneScale);
    void setMemoryBudgetMiB(int mebibytes);

    struct TileDescriptor {
        QString key;
        QString absolutePath;
        QString sha256;
        int level = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    /* Invoked only on the GUI thread after a worker decoded and hash-checked a tile. */
    void acceptLoadedTile(QString key, QImage image, QString error);

signals:
    void loadingProgress(int ready, int total);
    void tileLoadWarning(QString message);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    static QString keyFor(int level, int x, int y);
    void scheduleTile(const TileDescriptor &descriptor);
    void emitVisibleProgress();

    QSize m_fullImageSize;
    int m_tileSize = 512;
    int m_maxLevel = 0;
    QHash<QString, TileDescriptor> m_tiles;
    QCache<QString, QPixmap> m_cache;
    QSet<QString> m_pending;
    QSet<QString> m_visibleKeys;
};

#endif
