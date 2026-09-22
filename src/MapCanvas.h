#ifndef WATER1_MAPVIEWER_MAP_CANVAS_H
#define WATER1_MAPVIEWER_MAP_CANVAS_H

#include <QGraphicsView>

class MapCanvas final : public QGraphicsView {
    Q_OBJECT

public:
    explicit MapCanvas(QGraphicsScene *scene, QWidget *parent = nullptr);

    void resetToFit();
    void resetToNativePixels();
    void setDisplayPixelAspect(qreal horizontalOverVertical);

signals:
    void mapCellClicked(QPointF scenePosition);
    void visibleSceneRectChanged(QRectF sceneRect, qreal verticalSceneScale);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    qreal m_zoom = 1.0;  // User zoom relative to the corrected-aspect fit/native transform.
    qreal m_pixelAspectX = 1.0;
    QPoint m_pressPosition;
    bool m_leftPress = false;
    void notifyVisibleSceneRect();
};

#endif
