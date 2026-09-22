#ifndef WATER1_MAPVIEWER_MAP_DATA_OVERLAY_ITEM_H
#define WATER1_MAPVIEWER_MAP_DATA_OVERLAY_ITEM_H

#include <QGraphicsItem>
#include <QSize>

class MapDataOverlayItem final : public QGraphicsItem {
public:
    MapDataOverlayItem(QSize imageSize, QByteArray treasureCandidateBits, QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    void setGridVisible(bool visible);
    void setTreasureCandidatesVisible(bool visible);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    bool candidateAt(int cellX, int cellY) const;

    QSize m_imageSize;
    QByteArray m_candidateBits;
    bool m_gridVisible = false;
    bool m_candidatesVisible = false;
};

#endif
