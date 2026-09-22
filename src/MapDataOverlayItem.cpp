#include "MapDataOverlayItem.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

namespace {

constexpr int kCellWidth = 24;
constexpr int kCellHeight = 12;
constexpr int kCellsPerSector = 14;
constexpr int kCellsX = 72 * kCellsPerSector;
constexpr int kCellsY = 36 * kCellsPerSector;

} // namespace

MapDataOverlayItem::MapDataOverlayItem(QSize imageSize, QByteArray treasureCandidateBits, QGraphicsItem *parent)
    : QGraphicsItem(parent), m_imageSize(imageSize), m_candidateBits(std::move(treasureCandidateBits))
{
    setZValue(10);
}

QRectF MapDataOverlayItem::boundingRect() const
{
    return QRectF(0, 0, m_imageSize.width(), m_imageSize.height());
}

void MapDataOverlayItem::setGridVisible(bool visible)
{
    if (m_gridVisible == visible) return;
    m_gridVisible = visible;
    update();
}

void MapDataOverlayItem::setTreasureCandidatesVisible(bool visible)
{
    if (m_candidatesVisible == visible) return;
    m_candidatesVisible = visible;
    update();
}

bool MapDataOverlayItem::candidateAt(int cellX, int cellY) const
{
    if (cellX < 0 || cellX >= kCellsX || cellY < 0 || cellY >= kCellsY) return false;
    const int index = cellY * kCellsX + cellX;
    return index / 8 < m_candidateBits.size()
        && (static_cast<unsigned char>(m_candidateBits.at(index / 8)) & (1u << (index % 8)));
}

void MapDataOverlayItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    const QRectF exposed = option->exposedRect.intersected(boundingRect());
    if (m_candidatesVisible) {
        const int firstX = qMax(0, static_cast<int>(exposed.left() / kCellWidth));
        const int firstY = qMax(0, static_cast<int>(exposed.top() / kCellHeight));
        const int lastX = qMin(kCellsX - 1, static_cast<int>(exposed.right() / kCellWidth));
        const int lastY = qMin(kCellsY - 1, static_cast<int>(exposed.bottom() / kCellHeight));
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 215, 0, 110));
        for (int cellY = firstY; cellY <= lastY; ++cellY) {
            for (int cellX = firstX; cellX <= lastX; ++cellX) {
                if (candidateAt(cellX, cellY)) {
                    painter->drawRect(cellX * kCellWidth, cellY * kCellHeight, kCellWidth, kCellHeight);
                }
            }
        }
    }
    if (m_gridVisible) {
        QPen pen(QColor(235, 245, 255, 125));
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        const int sectorWidth = kCellWidth * kCellsPerSector;
        const int sectorHeight = kCellHeight * kCellsPerSector;
        const int firstColumn = qMax(0, static_cast<int>(exposed.left() / sectorWidth));
        const int firstRow = qMax(0, static_cast<int>(exposed.top() / sectorHeight));
        const int lastColumn = qMin(72, static_cast<int>(exposed.right() / sectorWidth) + 1);
        const int lastRow = qMin(36, static_cast<int>(exposed.bottom() / sectorHeight) + 1);
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const qreal x = column * sectorWidth;
            painter->drawLine(QPointF(x, exposed.top()), QPointF(x, exposed.bottom()));
        }
        for (int row = firstRow; row <= lastRow; ++row) {
            const qreal y = row * sectorHeight;
            painter->drawLine(QPointF(exposed.left(), y), QPointF(exposed.right(), y));
        }
    }
}
