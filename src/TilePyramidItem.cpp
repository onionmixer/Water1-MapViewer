#include "TilePyramidItem.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

namespace {

QString sha256ForFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() && file.error() != QFile::NoError) {
            *error = file.errorString();
            return {};
        }
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}

class TileLoadTask final : public QRunnable {
public:
    TileLoadTask(QPointer<TilePyramidItem> target, TilePyramidItem::TileDescriptor descriptor)
        : m_target(target), m_descriptor(std::move(descriptor))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QString error;
        const QString actualHash = sha256ForFile(m_descriptor.absolutePath, &error);
        QImage image;
        if (actualHash.isEmpty()) {
            error = QStringLiteral("Cannot read tile %1: %2").arg(m_descriptor.key, error);
        } else if (actualHash != m_descriptor.sha256) {
            error = QStringLiteral("Tile checksum mismatch: %1").arg(m_descriptor.key);
        } else {
            QImageReader reader(m_descriptor.absolutePath);
            reader.setAutoTransform(false);
            image = reader.read();
            if (image.isNull() || image.width() != m_descriptor.width || image.height() != m_descriptor.height) {
                error = QStringLiteral("Tile image is corrupt or has wrong dimensions: %1").arg(m_descriptor.key);
                image = QImage();
            }
        }
        const QPointer<TilePyramidItem> target = m_target;
        const QString key = m_descriptor.key;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [target, key, image, error] {
            if (target) target->acceptLoadedTile(key, image, error);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<TilePyramidItem> m_target;
    TilePyramidItem::TileDescriptor m_descriptor;
};

} // namespace

TilePyramidItem::TilePyramidItem(QSize fullImageSize, QJsonObject pyramid, QJsonObject manifest,
                                 QString packageDirectory, QGraphicsItem *parent)
    : QGraphicsObject(parent), m_fullImageSize(fullImageSize)
{
    m_tileSize = pyramid.value(QStringLiteral("tile_size")).toInt(512);
    const QJsonArray levels = pyramid.value(QStringLiteral("levels")).toArray();
    m_maxLevel = qMax(0, levels.size() - 1);
    const QJsonArray entries = manifest.value(QStringLiteral("tiles")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        TileDescriptor descriptor;
        descriptor.level = entry.value(QStringLiteral("level")).toInt();
        descriptor.x = entry.value(QStringLiteral("x")).toInt();
        descriptor.y = entry.value(QStringLiteral("y")).toInt();
        descriptor.key = keyFor(descriptor.level, descriptor.x, descriptor.y);
        descriptor.absolutePath = QDir(packageDirectory).filePath(entry.value(QStringLiteral("path")).toString());
        descriptor.sha256 = entry.value(QStringLiteral("sha256")).toString();
        descriptor.width = entry.value(QStringLiteral("width")).toInt();
        descriptor.height = entry.value(QStringLiteral("height")).toInt();
        m_tiles.insert(descriptor.key, descriptor);
    }
    setMemoryBudgetMiB(96);
    setZValue(0);
}

QRectF TilePyramidItem::boundingRect() const
{
    return QRectF(0, 0, m_fullImageSize.width(), m_fullImageSize.height());
}

void TilePyramidItem::setMemoryBudgetMiB(int mebibytes)
{
    const int safeMebibytes = qBound(16, mebibytes, 512);
    m_cache.setMaxCost(safeMebibytes * 1024); // QCache cost is KiB.
}

QString TilePyramidItem::keyFor(int level, int x, int y)
{
    return QStringLiteral("%1/%2/%3").arg(level).arg(x).arg(y);
}

void TilePyramidItem::updateVisibleRegion(const QRectF &sceneRect, qreal verticalSceneScale)
{
    if (verticalSceneScale <= 0.0) return;
    int level = 0;
    qreal sampledPixelsPerSourcePixel = verticalSceneScale;
    while (sampledPixelsPerSourcePixel < 0.75 && level < m_maxLevel) {
        sampledPixelsPerSourcePixel *= 2.0;
        ++level;
    }
    const int sourceScale = 1 << level;
    const qreal tileSceneSize = m_tileSize * sourceScale;
    const QRectF expanded = sceneRect.adjusted(-tileSceneSize, -tileSceneSize, tileSceneSize, tileSceneSize)
                                  .intersected(boundingRect());
    const int firstX = qMax(0, static_cast<int>(expanded.left() / tileSceneSize));
    const int firstY = qMax(0, static_cast<int>(expanded.top() / tileSceneSize));
    const int lastX = qMax(firstX, static_cast<int>(expanded.right() / tileSceneSize));
    const int lastY = qMax(firstY, static_cast<int>(expanded.bottom() / tileSceneSize));

    m_visibleKeys.clear();
    for (int y = firstY; y <= lastY; ++y) {
        for (int x = firstX; x <= lastX; ++x) {
            const QString key = keyFor(level, x, y);
            const auto it = m_tiles.constFind(key);
            if (it == m_tiles.cend()) continue;
            m_visibleKeys.insert(key);
            if (!m_cache.object(key) && !m_pending.contains(key)) scheduleTile(it.value());
        }
    }
    emitVisibleProgress();
    update();
}

void TilePyramidItem::scheduleTile(const TileDescriptor &descriptor)
{
    m_pending.insert(descriptor.key);
    QThreadPool::globalInstance()->start(new TileLoadTask(QPointer<TilePyramidItem>(this), descriptor));
}

void TilePyramidItem::acceptLoadedTile(QString key, QImage image, QString error)
{
    m_pending.remove(key);
    if (!error.isEmpty()) {
        emit tileLoadWarning(error);
    } else if (!image.isNull()) {
        const int costKiB = qMax(1, (image.width() * image.height() * 4 + 1023) / 1024);
        m_cache.insert(key, new QPixmap(QPixmap::fromImage(image, Qt::NoFormatConversion)), costKiB);
    }
    emitVisibleProgress();
    update();
}

void TilePyramidItem::emitVisibleProgress()
{
    int ready = 0;
    for (const QString &key : m_visibleKeys) {
        if (m_cache.object(key)) ++ready;
    }
    emit loadingProgress(ready, m_visibleKeys.size());
}

void TilePyramidItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const QString &key : m_visibleKeys) {
        const QPixmap *pixmap = m_cache.object(key);
        const auto descriptor = m_tiles.constFind(key);
        if (!pixmap || descriptor == m_tiles.cend()) continue;
        const int sourceScale = 1 << descriptor->level;
        const QRectF destination(descriptor->x * m_tileSize * sourceScale,
                                 descriptor->y * m_tileSize * sourceScale,
                                 descriptor->width * sourceScale,
                                 descriptor->height * sourceScale);
        painter->drawPixmap(destination, *pixmap, QRectF(0, 0, pixmap->width(), pixmap->height()));
    }
}
