#include "MapCanvas.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>
#include <QtMath>

namespace {

constexpr int kSectorsX = 72;
constexpr int kSectorsY = 36;
constexpr qreal kSectorSceneWidth = 336.0;  // 14 cells × 24 scene pixels
constexpr qreal kSectorSceneHeight = 168.0; // 14 cells × 12 scene pixels
constexpr int kRulerTopHeight = 24;
constexpr int kRulerLeftWidth = 46;
constexpr qreal kMinimumLabelSpacing = 72.0;
constexpr QColor kRulerBackground(20, 29, 40);
constexpr QColor kRulerBorder(105, 130, 156);
constexpr QColor kRulerText(232, 240, 248);

int majorSectorStep(qreal screenPixelsPerSector, int sectorCount)
{
    constexpr int candidates[] = {1, 2, 3, 6, 9, 18, 36, 72};
    for (const int step : candidates) {
        if (step <= sectorCount && screenPixelsPerSector * step >= kMinimumLabelSpacing) return step;
    }
    return sectorCount;
}

int firstAlignedSector(int firstSector, int step)
{
    return ((firstSector + step - 1) / step) * step;
}

QString longitudeLabel(int sectorX)
{
    const int eastDegrees = ((sectorX + 38) % kSectorsX) * 5;
    if (eastDegrees == 0) return QStringLiteral("0°");
    if (eastDegrees < 180) return QStringLiteral("%1°E").arg(eastDegrees);
    return QStringLiteral("%1°W").arg(360 - eastDegrees);
}

QString latitudeLabel(int sectorY)
{
    const int signedDegrees = sectorY < 18 ? 90 - sectorY * 5 : -(sectorY * 5 - 90);
    if (signedDegrees == 0) return QStringLiteral("0°");
    return QStringLiteral("%1°%2").arg(qAbs(signedDegrees)).arg(signedDegrees > 0 ? "N" : "S");
}

} // namespace

MapCanvas::MapCanvas(QGraphicsScene *scene, QWidget *parent)
    : QGraphicsView(scene, parent)
{
    setObjectName(QStringLiteral("mapView"));
    setDragMode(QGraphicsView::ScrollHandDrag);
    setRenderHint(QPainter::Antialiasing, false);
    setRenderHint(QPainter::SmoothPixmapTransform, false);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

void MapCanvas::resetToFit()
{
    if (!scene() || scene()->sceneRect().isEmpty()) return;
    const QRectF rect = scene()->sceneRect();
    const QSize viewportSize = viewport()->size();
    if (viewportSize.isEmpty()) return;
    const qreal scaleX = viewportSize.width() / (rect.width() * m_pixelAspectX);
    const qreal scaleY = viewportSize.height() / rect.height();
    const qreal fitScale = qMin(scaleX, scaleY);
    QTransform corrected;
    corrected.scale(fitScale * m_pixelAspectX, fitScale);
    setTransform(corrected);
    m_zoom = 1.0;
    notifyVisibleSceneRect();
}

void MapCanvas::resetToNativePixels()
{
    QTransform corrected;
    corrected.scale(m_pixelAspectX, 1.0);
    setTransform(corrected);
    m_zoom = 1.0;
    notifyVisibleSceneRect();
}

void MapCanvas::setDisplayPixelAspect(qreal horizontalOverVertical)
{
    if (horizontalOverVertical <= 0.0) return;
    m_pixelAspectX = horizontalOverVertical;
    viewport()->update();
}

void MapCanvas::wheelEvent(QWheelEvent *event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    const QPoint pointer = event->position().toPoint();
    const QPointF sceneBeforeScale = mapToScene(pointer);
    const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const qreal target = m_zoom * factor;
    if (target < 0.02 || target > 64.0 || event->angleDelta().y() == 0) {
        event->accept();
        return;
    }
    scale(factor, factor);
    const QPointF sceneAfterScale = mapToScene(pointer);
    horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                   + qRound((sceneBeforeScale.x() - sceneAfterScale.x()) * transform().m11()));
    verticalScrollBar()->setValue(verticalScrollBar()->value()
                                 + qRound((sceneBeforeScale.y() - sceneAfterScale.y()) * transform().m22()));
    m_zoom = target;
    notifyVisibleSceneRect();
    event->accept();
}

void MapCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_leftPress = true;
        m_pressPosition = event->pos();
    }
    QGraphicsView::mousePressEvent(event);
}

void MapCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_leftPress && (event->pos() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
        m_leftPress = false;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void MapCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    const bool clicked = m_leftPress && event->button() == Qt::LeftButton;
    QGraphicsView::mouseReleaseEvent(event);
    if (clicked) emit mapCellClicked(mapToScene(event->pos()));
    if (event->button() == Qt::LeftButton) m_leftPress = false;
}

void MapCanvas::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    notifyVisibleSceneRect();
}

void MapCanvas::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    notifyVisibleSceneRect();
}

void MapCanvas::drawForeground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawForeground(painter, rect);
    if (!scene() || viewport()->size().isEmpty()) return;

    const QSize viewportSize = viewport()->size();
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    const int firstSectorX = qBound(0, qFloor(visible.left() / kSectorSceneWidth) - 1, kSectorsX - 1);
    const int lastSectorX = qBound(0, qCeil(visible.right() / kSectorSceneWidth) + 1, kSectorsX - 1);
    const int firstSectorY = qBound(0, qFloor(visible.top() / kSectorSceneHeight) - 1, kSectorsY - 1);
    const int lastSectorY = qBound(0, qCeil(visible.bottom() / kSectorSceneHeight) + 1, kSectorsY - 1);
    const qreal sectorScreenWidth = qAbs(transform().m11()) * kSectorSceneWidth;
    const qreal sectorScreenHeight = qAbs(transform().m22()) * kSectorSceneHeight;
    const int horizontalStep = majorSectorStep(sectorScreenWidth, kSectorsX);
    const int verticalStep = majorSectorStep(sectorScreenHeight, kSectorsY);

    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->fillRect(QRect(0, 0, viewportSize.width(), kRulerTopHeight), kRulerBackground);
    painter->fillRect(QRect(0, 0, kRulerLeftWidth, viewportSize.height()), kRulerBackground);
    painter->setPen(kRulerBorder);
    painter->drawLine(kRulerLeftWidth, kRulerTopHeight, viewportSize.width(), kRulerTopHeight);
    painter->drawLine(kRulerLeftWidth, kRulerTopHeight, kRulerLeftWidth, viewportSize.height());
    QFont rulerFont = painter->font();
    rulerFont.setPointSize(8);
    painter->setFont(rulerFont);
    painter->setPen(kRulerText);

    painter->save();
    painter->setClipRect(QRect(kRulerLeftWidth, 0, viewportSize.width() - kRulerLeftWidth, kRulerTopHeight));
    for (int sector = firstAlignedSector(firstSectorX, horizontalStep); sector <= lastSectorX; sector += horizontalStep) {
        const int x = mapFromScene(QPointF(sector * kSectorSceneWidth, 0)).x();
        painter->drawLine(x, kRulerTopHeight - 7, x, kRulerTopHeight);
        painter->drawText(QRect(x - 34, 1, 68, kRulerTopHeight - 8), Qt::AlignHCenter | Qt::AlignTop,
                          longitudeLabel(sector));
    }
    painter->restore();

    painter->save();
    painter->setClipRect(QRect(0, kRulerTopHeight, kRulerLeftWidth, viewportSize.height() - kRulerTopHeight));
    for (int sector = firstAlignedSector(firstSectorY, verticalStep); sector <= lastSectorY; sector += verticalStep) {
        const int y = mapFromScene(QPointF(0, sector * kSectorSceneHeight)).y();
        painter->drawLine(kRulerLeftWidth - 7, y, kRulerLeftWidth, y);
        painter->drawText(QRect(1, y - 8, kRulerLeftWidth - 9, 16), Qt::AlignRight | Qt::AlignVCenter,
                          latitudeLabel(sector));
    }
    painter->restore();
    painter->restore();
}

void MapCanvas::notifyVisibleSceneRect()
{
    viewport()->update();
    QTimer::singleShot(0, this, [this] {
        if (!scene() || viewport()->size().isEmpty()) return;
        emit visibleSceneRectChanged(mapToScene(viewport()->rect()).boundingRect(), transform().m22());
    });
}
