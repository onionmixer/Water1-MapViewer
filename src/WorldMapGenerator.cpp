#include "WorldMapGenerator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <array>
#include <cstring>

namespace {

constexpr int kSectorsX = 72;
constexpr int kSectorsY = 36;
constexpr int kSubCellsPerSector = 14;
constexpr int kCellWidth = 24;
constexpr int kCellHeight = 12;
constexpr int kImageWidth = kSectorsX * kSubCellsPerSector * kCellWidth;
constexpr int kImageHeight = kSectorsY * kSubCellsPerSector * kCellHeight;
constexpr int kBlockRecords = 288;
constexpr int kBlockRecordSize = 5;
constexpr int kDescriptorCount = 223;
constexpr int kDescriptorSize = 18;
constexpr int kChunkCount = 1331;
constexpr int kChunkSize = 49;
constexpr int kBlockOffset = 0x0004;
constexpr int kDescriptorOffset = 0x05A4;
constexpr int kChunkOffset = 0x1552;
constexpr int kTerrainOffset = 0x01B0;
constexpr int kTextureSize = 0x6c;
constexpr int kTexturesPerBand = 18;
constexpr int kTerrainBands = 4;
constexpr int kPortRecordOffset = 0x1786;
constexpr int kPortRecordSize = 20;
constexpr int kActivePortRecords = 70;
constexpr int kColorCimPortMarkerOffset = 0x0120;
constexpr int kPortMarkerWidth = 24;
constexpr int kPortMarkerHeight = 12;
constexpr int kTileSize = 512;
constexpr int kPyramidLevels = 7;

constexpr std::array<QRgb, 8> kEgaPalette = {
    /* MAIN.EXE DS:0x2C48: active EGA AC mapping, 0..7.  COLOR.CIM is sprite data, not a palette. */
    qRgba(0x00, 0x00, 0x00, 0xFF), qRgba(0x55, 0x55, 0xFF, 0xFF),
    qRgba(0x55, 0xFF, 0x55, 0xFF), qRgba(0x55, 0xFF, 0xFF, 0xFF),
    qRgba(0xFF, 0x55, 0x55, 0xFF), qRgba(0xFF, 0x55, 0xFF, 0xFF),
    qRgba(0xFF, 0xFF, 0x55, 0xFF), qRgba(0xFF, 0xFF, 0xFF, 0xFF),
};

constexpr std::array<unsigned char, 53 * 4> kTerrainLut = {
    0x00,0x00,0x00,0x00, 0x0F,0x0F,0x0F,0x0F, 0x09,0x09,0x09,0x09, 0x0E,0x0E,0x0E,0x0E,
    0x11,0x11,0x11,0x11, 0x00,0x00,0x00,0x03, 0x00,0x00,0x04,0x00, 0x00,0x01,0x00,0x00,
    0x02,0x00,0x00,0x00, 0x08,0x00,0x08,0x00, 0x00,0x07,0x00,0x07, 0x00,0x00,0x05,0x05,
    0x06,0x06,0x00,0x00, 0x09,0x06,0x08,0x00, 0x06,0x09,0x00,0x07, 0x08,0x00,0x09,0x05,
    0x00,0x07,0x05,0x09, 0x03,0x09,0x09,0x09, 0x09,0x04,0x09,0x09, 0x09,0x09,0x01,0x09,
    0x09,0x09,0x09,0x02, 0x09,0x02,0x02,0x00, 0x01,0x09,0x00,0x01, 0x04,0x00,0x09,0x04,
    0x00,0x03,0x03,0x09, 0x09,0x08,0x09,0x08, 0x07,0x09,0x07,0x09, 0x09,0x09,0x06,0x06,
    0x05,0x05,0x09,0x09, 0x00,0x0A,0x00,0x00, 0x0B,0x00,0x00,0x00, 0x00,0x00,0x0D,0x00,
    0x0C,0x00,0x00,0x00, 0x09,0x02,0x08,0x00, 0x01,0x09,0x00,0x07, 0x08,0x00,0x09,0x04,
    0x00,0x07,0x03,0x09, 0x09,0x06,0x02,0x00, 0x06,0x09,0x00,0x01, 0x04,0x00,0x09,0x05,
    0x00,0x03,0x05,0x09, 0x09,0x10,0x09,0x09, 0x10,0x09,0x09,0x09, 0x09,0x09,0x09,0x10,
    0x09,0x09,0x10,0x09, 0x09,0x0E,0x09,0x09, 0x0E,0x09,0x09,0x09, 0x09,0x09,0x09,0x0E,
    0x09,0x09,0x0E,0x09, 0x00,0x11,0x00,0x00, 0x11,0x00,0x00,0x00, 0x00,0x00,0x00,0x11,
    0x00,0x00,0x11,0x00,
};

constexpr std::array<unsigned char, 22> kTreasureQuadrantMask = {
    0x0F,0xFF,0x01,0x24,0x8A,0x53,0xCE,0xDB,0x7F,0xFF,0xFE,
    0xDB,0x7F,0xFF,0xF4,0x82,0x8E,0xDB,0x7E,0xDB,0x7F,0xFF,
};

struct Coordinate {
    int sectorX = 0;
    int sectorY = 0;
    int subX = 0;
    int subY = 0;
};

struct LatitudeLongitude {
    QString latitudeHemisphere;
    int latitudeDegrees = 0;
    int latitudeSigned = 0;
    QString longitudeHemisphere;
    int longitudeDegrees = 0;
    int longitudeEast = 0;
};

class NewGameReader final {
public:
    bool load(const QString &path, QString *error)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("Cannot read NEWGAME.DAT: %1").arg(file.errorString());
            return false;
        }
        m_bytes = file.readAll();
        if (m_bytes.size() != 70677) {
            *error = QStringLiteral("NEWGAME.DAT length is %1, expected 70677.").arg(m_bytes.size());
            return false;
        }
        if (u8(0) != 0xA4 || u8(1) != 0x05 || u8(2) != 0x52 || u8(3) != 0x15) {
            *error = QStringLiteral("NEWGAME.DAT header does not identify the supported hierarchy.");
            return false;
        }
        for (int block = 0; block < kBlockRecords; ++block) {
            if (u16(kBlockOffset + block * kBlockRecordSize) >= kDescriptorCount) {
                *error = QStringLiteral("NEWGAME.DAT block %1 references an invalid descriptor.").arg(block);
                return false;
            }
        }
        for (int descriptor = 0; descriptor < kDescriptorCount; ++descriptor) {
            for (int slot = 0; slot < 9; ++slot) {
                if (u16(kDescriptorOffset + descriptor * kDescriptorSize + slot * 2) >= kChunkCount) {
                    *error = QStringLiteral("NEWGAME.DAT descriptor %1 references an invalid terrain chunk.")
                                 .arg(descriptor);
                    return false;
                }
            }
        }
        return true;
    }

    unsigned char terrainAt(int sectorX, int sectorY, int terrainX, int terrainY) const
    {
        if (sectorY < 0 || sectorY >= kSectorsY || terrainX < 0 || terrainX >= 7 || terrainY < 0 || terrainY >= 7) {
            return 0;
        }
        sectorX = ((sectorX % kSectorsX) + kSectorsX) % kSectorsX;
        const int blockX = sectorX / 3;
        const int blockY = sectorY / 3;
        const int slot = (sectorY % 3) * 3 + (sectorX % 3);
        const unsigned short descriptor = u16(kBlockOffset + (blockY * 24 + blockX) * kBlockRecordSize);
        const unsigned short chunk = u16(kDescriptorOffset + descriptor * kDescriptorSize + slot * 2);
        return u8(kChunkOffset + chunk * kChunkSize + terrainY * 7 + terrainX);
    }

private:
    unsigned char u8(int offset) const { return static_cast<unsigned char>(m_bytes.at(offset)); }
    unsigned short u16(int offset) const { return static_cast<unsigned short>(u8(offset) | (u8(offset + 1) << 8)); }
    QByteArray m_bytes;
};

class MapPutReader final {
public:
    bool load(const QString &path, QString *error)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("Cannot read MAP.PUT: %1").arg(file.errorString());
            return false;
        }
        m_bytes = file.readAll();
        if (m_bytes.size() != 9072) {
            *error = QStringLiteral("MAP.PUT length is %1, expected 9072.").arg(m_bytes.size());
            return false;
        }
        return true;
    }

    QRgb pixel(int band, int texture, int x, int y) const
    {
        const int offset = kTerrainOffset + (band * kTexturesPerBand + texture) * kTextureSize
                         + y * 9 + (x / 8) * 3;
        const int bit = 7 - (x % 8);
        const unsigned char p0 = byte(offset);
        const unsigned char p2 = byte(offset + 1);
        const unsigned char p1 = byte(offset + 2);
        const int color = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2);
        return kEgaPalette.at(static_cast<size_t>(color));
    }

private:
    unsigned char byte(int offset) const { return static_cast<unsigned char>(m_bytes.at(offset)); }
    QByteArray m_bytes;
};

int latitudeBand(int sectorY)
{
    const int q = sectorY / 3;
    const int t = q < 6 ? 5 - q : q - 6;
    return t < 4 ? qAbs(t) / 2 : t - 2;
}

LatitudeLongitude coordinatesFor(const Coordinate &position)
{
    LatitudeLongitude result;
    result.latitudeSigned = position.sectorY < 18 ? 90 - 5 * position.sectorY : -(5 * position.sectorY - 90);
    result.latitudeHemisphere = result.latitudeSigned >= 0 ? QStringLiteral("N") : QStringLiteral("S");
    result.latitudeDegrees = qAbs(result.latitudeSigned);

    const int remainder = (position.sectorX + 38) % 72;
    result.longitudeEast = remainder * 5;
    if (remainder < 36) {
        result.longitudeHemisphere = QStringLiteral("E");
        result.longitudeDegrees = remainder * 5;
    } else {
        result.longitudeHemisphere = QStringLiteral("W");
        result.longitudeDegrees = (72 - remainder) * 5;
    }
    return result;
}

QJsonObject positionJson(const Coordinate &position)
{
    QJsonObject result;
    result.insert(QStringLiteral("sector_x"), position.sectorX);
    result.insert(QStringLiteral("sector_y"), position.sectorY);
    result.insert(QStringLiteral("sub_x"), position.subX);
    result.insert(QStringLiteral("sub_y"), position.subY);
    const int cellX = position.sectorX * kSubCellsPerSector + position.subX;
    const int cellY = position.sectorY * kSubCellsPerSector + position.subY;
    result.insert(QStringLiteral("cell_x"), cellX);
    result.insert(QStringLiteral("cell_y"), cellY);
    result.insert(QStringLiteral("pixel_x"), cellX * kCellWidth);
    result.insert(QStringLiteral("pixel_y"), cellY * kCellHeight);
    result.insert(QStringLiteral("pixel_center_x"), cellX * kCellWidth + kCellWidth / 2);
    result.insert(QStringLiteral("pixel_center_y"), cellY * kCellHeight + kCellHeight / 2);
    return result;
}

QJsonObject geographicJson(const Coordinate &position)
{
    const LatitudeLongitude coordinate = coordinatesFor(position);
    QJsonObject latitude;
    latitude.insert(QStringLiteral("hemisphere"), coordinate.latitudeHemisphere);
    latitude.insert(QStringLiteral("degrees"), coordinate.latitudeDegrees);
    latitude.insert(QStringLiteral("signed_degrees"), coordinate.latitudeSigned);
    QJsonObject longitude;
    longitude.insert(QStringLiteral("hemisphere"), coordinate.longitudeHemisphere);
    longitude.insert(QStringLiteral("degrees"), coordinate.longitudeDegrees);
    longitude.insert(QStringLiteral("east_degrees"), coordinate.longitudeEast);
    QJsonObject result;
    result.insert(QStringLiteral("latitude"), latitude);
    result.insert(QStringLiteral("longitude"), longitude);
    return result;
}

QJsonObject politicalStatusJson(int rawAttributes)
{
    const int ownershipCode = rawAttributes & 0x03;
    QString sovereignty;
    switch (ownershipCode) {
    case 0: sovereignty = QStringLiteral("Independent / neutral"); break;
    case 1: sovereignty = QStringLiteral("Portugal"); break;
    case 2: sovereignty = QStringLiteral("Spain"); break;
    case 3: sovereignty = QStringLiteral("Ottoman Turkey"); break;
    default: sovereignty = QStringLiteral("Unknown"); break;
    }
    const bool courtAvailable = (rawAttributes & 0x10) != 0;
    QString rulerTitle;
    if (courtAvailable && ownershipCode == 3) rulerTitle = QStringLiteral("Sultan");
    else if (courtAvailable && ownershipCode != 0) rulerTitle = QStringLiteral("King");
    return QJsonObject{{QStringLiteral("ownership_code"), ownershipCode},
                       {QStringLiteral("sovereignty"), sovereignty},
                       {QStringLiteral("independent"), ownershipCode == 0},
                       {QStringLiteral("court_available"), courtAvailable},
                       {QStringLiteral("ruler_available"), !rulerTitle.isEmpty()},
                       {QStringLiteral("ruler_title"), rulerTitle},
                       {QStringLiteral("guild_available"), (rawAttributes & 0x04) != 0}};
}

QString sha256ForPath(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            *error = file.errorString();
            return {};
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool writeJsonAtomically(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool writePngAtomically(const QString &path, const QImage &image, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

class PackageStaging final {
public:
    PackageStaging(QDir outputRoot, QString finalName)
        : m_outputRoot(std::move(outputRoot)), m_finalName(std::move(finalName)),
          m_stagingName(m_finalName + QStringLiteral(".partial-")
                        + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
    }

    ~PackageStaging()
    {
        if (!m_published) QDir(m_outputRoot.filePath(m_stagingName)).removeRecursively();
    }

    QString stagingPath() const { return m_outputRoot.filePath(m_stagingName); }

    bool create(QString *error) const
    {
        if (m_outputRoot.mkpath(m_stagingName)) return true;
        *error = QStringLiteral("Cannot create staging map-package directory.");
        return false;
    }

    bool publish(QString *error)
    {
        const QString finalPath = m_outputRoot.filePath(m_finalName);
        const bool hadExistingPackage = QFileInfo(finalPath).exists();
        const QString backupName = m_finalName + QStringLiteral(".previous-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (hadExistingPackage && !m_outputRoot.rename(m_finalName, backupName)) {
            *error = QStringLiteral("Cannot preserve the previous generated map package.");
            return false;
        }
        if (!m_outputRoot.rename(m_stagingName, m_finalName)) {
            if (hadExistingPackage) m_outputRoot.rename(backupName, m_finalName);
            *error = QStringLiteral("Cannot publish the completed map package.");
            return false;
        }
        m_published = true;
        if (hadExistingPackage) QDir(m_outputRoot.filePath(backupName)).removeRecursively();
        return true;
    }

private:
    QDir m_outputRoot;
    QString m_finalName;
    QString m_stagingName;
    bool m_published = false;
};

int ceilDiv(int dividend, int divisor)
{
    return dividend / divisor + (dividend % divisor != 0 ? 1 : 0);
}

bool writeTilePyramid(const QImage &image, const QString &packagePath, const WorldMapGenerator *generator,
                      QJsonObject *pyramid, QString *error)
{
    QJsonArray levels;
    QJsonArray tiles;
    const QDir packageDirectory(packagePath);
    for (int level = 0; level < kPyramidLevels; ++level) {
        const int sourceScale = 1 << level;
        const int levelWidth = ceilDiv(image.width(), sourceScale);
        const int levelHeight = ceilDiv(image.height(), sourceScale);
        const int columns = ceilDiv(levelWidth, kTileSize);
        const int rows = ceilDiv(levelHeight, kTileSize);
        const QString relativeDirectory = QStringLiteral("tiles/L%1").arg(level);
        if (!packageDirectory.mkpath(relativeDirectory)) {
            *error = QStringLiteral("Cannot create tile pyramid directory: %1").arg(relativeDirectory);
            return false;
        }
        levels.append(QJsonObject{{QStringLiteral("level"), level},
                                  {QStringLiteral("scale"), sourceScale},
                                  {QStringLiteral("width"), levelWidth},
                                  {QStringLiteral("height"), levelHeight},
                                  {QStringLiteral("columns"), columns},
                                  {QStringLiteral("rows"), rows}});
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < columns; ++x) {
                if (generator->isInterruptionRequested()) {
                    *error = QStringLiteral("Map generation cancelled while writing the tile pyramid.");
                    return false;
                }
                const int destinationX = x * kTileSize;
                const int destinationY = y * kTileSize;
                const int destinationWidth = qMin(kTileSize, levelWidth - destinationX);
                const int destinationHeight = qMin(kTileSize, levelHeight - destinationY);
                const QRect sourceRect(destinationX * sourceScale, destinationY * sourceScale,
                                        qMin(destinationWidth * sourceScale, image.width() - destinationX * sourceScale),
                                        qMin(destinationHeight * sourceScale, image.height() - destinationY * sourceScale));
                QImage tile = image.copy(sourceRect);
                if (sourceScale != 1) {
                    tile = tile.scaled(destinationWidth, destinationHeight, Qt::IgnoreAspectRatio,
                                       Qt::FastTransformation);
                }
                const QString relativePath = relativeDirectory + QStringLiteral("/%1_%2.png").arg(x).arg(y);
                const QString absolutePath = packageDirectory.filePath(relativePath);
                if (!writePngAtomically(absolutePath, tile, error)) {
                    *error = QStringLiteral("Cannot write tile %1: %2").arg(relativePath, *error);
                    return false;
                }
                QString hashError;
                const QString hash = sha256ForPath(absolutePath, &hashError);
                if (hash.isEmpty()) {
                    *error = QStringLiteral("Cannot hash tile %1: %2").arg(relativePath, hashError);
                    return false;
                }
                tiles.append(QJsonObject{{QStringLiteral("level"), level}, {QStringLiteral("x"), x},
                                         {QStringLiteral("y"), y}, {QStringLiteral("path"), relativePath},
                                         {QStringLiteral("width"), destinationWidth},
                                         {QStringLiteral("height"), destinationHeight},
                                         {QStringLiteral("sha256"), hash}});
            }
        }
    }
    const QString manifestPath = packageDirectory.filePath(QStringLiteral("tile_manifest.json"));
    const QJsonObject manifest{{QStringLiteral("format"), QStringLiteral("water1-map-tile-manifest")},
                               {QStringLiteral("schema_version"), 1}, {QStringLiteral("tile_size"), kTileSize},
                               {QStringLiteral("levels"), levels}, {QStringLiteral("tiles"), tiles}};
    if (!writeJsonAtomically(manifestPath, manifest, error)) return false;
    QString hashError;
    const QString manifestHash = sha256ForPath(manifestPath, &hashError);
    if (manifestHash.isEmpty()) {
        *error = QStringLiteral("Cannot hash tile manifest: %1").arg(hashError);
        return false;
    }
    *pyramid = QJsonObject{{QStringLiteral("format"), QStringLiteral("nearest-neighbor-tile-pyramid")},
                           {QStringLiteral("tile_size"), kTileSize}, {QStringLiteral("levels"), levels},
                           {QStringLiteral("manifest_path"), QStringLiteral("tile_manifest.json")},
                           {QStringLiteral("manifest_sha256"), manifestHash}};
    return true;
}

bool treasureTerrainValid(unsigned char terrain)
{
    return (terrain > 0x04 && terrain < 0x11)
        || (terrain > 0x14 && terrain < 0x19)
        || (terrain > 0x1C && terrain < 0x29);
}

bool treasureQuadrantValid(unsigned char terrain, int quadrantX, int quadrantY)
{
    const unsigned char packed = kTreasureQuadrantMask.at(static_cast<size_t>(terrain >> 1));
    const unsigned char mask = (terrain & 1) ? (packed & 0x0f) : ((packed >> 4) & 0x0f);
    const unsigned char bit = static_cast<unsigned char>(8u >> (quadrantY * 2 + quadrantX));
    return (mask & bit) != 0;
}

QJsonArray portRecords(const QString &path, int firstRecord, int recordCount,
                       bool requireValidCoordinates, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read SHINARIO.CIM: %1").arg(file.errorString());
        return {};
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() != 7595 || kPortRecordOffset + 78 * kPortRecordSize > bytes.size()) {
        *error = QStringLiteral("SHINARIO.CIM does not contain the expected port record region.");
        return {};
    }

    QJsonArray ports;
    for (int listIndex = 0; listIndex < recordCount; ++listIndex) {
        const int sourceRecordIndex = firstRecord + listIndex;
        const int offset = kPortRecordOffset + sourceRecordIndex * kPortRecordSize;
        const QByteArray rawName = bytes.mid(offset, 14);
        const int nul = rawName.indexOf('\0');
        const QString name = QString::fromLatin1(nul >= 0 ? rawName.left(nul) : rawName).trimmed();
        Coordinate position;
        position.sectorX = static_cast<unsigned char>(bytes.at(offset + 15));
        position.sectorY = static_cast<unsigned char>(bytes.at(offset + 16));
        position.subX = static_cast<unsigned char>(bytes.at(offset + 17));
        position.subY = static_cast<unsigned char>(bytes.at(offset + 18));
        const bool coordinatesValid = position.sectorX < kSectorsX && position.sectorY < kSectorsY
                                   && position.subX < kSubCellsPerSector && position.subY < kSubCellsPerSector;
        if (requireValidCoordinates && !coordinatesValid) {
            *error = QStringLiteral("Port record %1 has out-of-range world coordinates.").arg(sourceRecordIndex);
            return {};
        }

        QJsonObject attributes;
        const int rawAttributes = static_cast<unsigned char>(bytes.at(offset + 19));
        attributes.insert(QStringLiteral("raw"), rawAttributes);
        attributes.insert(QStringLiteral("discovered_initially"), (rawAttributes & 0x08) != 0);
        attributes.insert(QStringLiteral("record_byte_0x0e"), static_cast<unsigned char>(bytes.at(offset + 14)));

        QJsonObject port;
        port.insert(QStringLiteral("id"), sourceRecordIndex);
        port.insert(QStringLiteral("source_record_index"), sourceRecordIndex);
        port.insert(QStringLiteral("name"), name);
        port.insert(QStringLiteral("raw_record_hex"), QString::fromLatin1(bytes.mid(offset, kPortRecordSize).toHex()));
        port.insert(QStringLiteral("coordinates_valid"), coordinatesValid);
        if (coordinatesValid) {
            port.insert(QStringLiteral("position"), positionJson(position));
            port.insert(QStringLiteral("coordinates"), geographicJson(position));
        }
        port.insert(QStringLiteral("political_status"), politicalStatusJson(rawAttributes));
        port.insert(QStringLiteral("attributes"), attributes);
        ports.append(port);
    }
    return ports;
}

QJsonObject treasureModel(const NewGameReader &newgame)
{
    constexpr int cellsX = kSectorsX * kSubCellsPerSector;
    constexpr int cellsY = kSectorsY * kSubCellsPerSector;
    QByteArray bits((cellsX * cellsY + 7) / 8, '\0');
    int count = 0;
    for (int sectorY = 0; sectorY < kSectorsY; ++sectorY) {
        for (int sectorX = 0; sectorX < kSectorsX; ++sectorX) {
            for (int subY = 0; subY < kSubCellsPerSector; ++subY) {
                for (int subX = 0; subX < kSubCellsPerSector; ++subX) {
                    const unsigned char terrain = newgame.terrainAt(sectorX, sectorY, subX / 2, subY / 2);
                    if (!treasureTerrainValid(terrain)
                        || !treasureQuadrantValid(terrain, subX & 1, subY & 1)) {
                        continue;
                    }
                    const int index = (sectorY * kSubCellsPerSector + subY) * cellsX
                                    + sectorX * kSubCellsPerSector + subX;
                    bits[index / 8] = static_cast<char>(bits.at(index / 8) | (1 << (index % 8)));
                    ++count;
                }
            }
        }
    }

    QJsonArray terrainRanges;
    terrainRanges.append(QJsonArray{5, 16});
    terrainRanges.append(QJsonArray{21, 24});
    terrainRanges.append(QJsonArray{29, 40});
    QJsonObject result;
    result.insert(QStringLiteral("kind"), QStringLiteral("quest_generated_candidates"));
    result.insert(QStringLiteral("candidate_count"), count);
    result.insert(QStringLiteral("cell_bitset_encoding"), QStringLiteral("base64-lsb-first-row-major"));
    result.insert(QStringLiteral("cell_bitset_base64"), QString::fromLatin1(bits.toBase64()));
    result.insert(QStringLiteral("terrain_id_inclusive_ranges"), terrainRanges);
    result.insert(QStringLiteral("quadrant_mask_hex"), QString::fromLatin1(
        QByteArray(reinterpret_cast<const char *>(kTreasureQuadrantMask.data()),
                   static_cast<int>(kTreasureQuadrantMask.size())).toHex()));
    result.insert(QStringLiteral("active_target_source"), QStringLiteral("optional SAVE.DAT; not included in static extraction"));
    return result;
}

QJsonObject terrainModel(const NewGameReader &newgame)
{
    QByteArray terrain;
    terrain.reserve(kSectorsX * kSectorsY * 49);
    for (int sectorY = 0; sectorY < kSectorsY; ++sectorY) {
        for (int sectorX = 0; sectorX < kSectorsX; ++sectorX) {
            for (int tileY = 0; tileY < 7; ++tileY) {
                for (int tileX = 0; tileX < 7; ++tileX) {
                    terrain.append(static_cast<char>(newgame.terrainAt(sectorX, sectorY, tileX, tileY)));
                }
            }
        }
    }
    QJsonObject result;
    result.insert(QStringLiteral("encoding"), QStringLiteral("base64-sector-row-major-7x7-terrain-id"));
    result.insert(QStringLiteral("bytes"), terrain.size());
    result.insert(QStringLiteral("data_base64"), QString::fromLatin1(terrain.toBase64()));
    return result;
}

QImage portMarkerImage(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read COLOR.CIM: %1").arg(file.errorString());
        return {};
    }
    const QByteArray data = file.readAll();
    if (data.size() != 2568 || kColorCimPortMarkerOffset + kPortMarkerHeight * 9 > data.size()) {
        *error = QStringLiteral("COLOR.CIM does not contain the expected 24x12 port marker sprite.");
        return {};
    }
    QImage marker(kPortMarkerWidth, kPortMarkerHeight, QImage::Format_ARGB32);
    for (int y = 0; y < kPortMarkerHeight; ++y) {
        auto *line = reinterpret_cast<QRgb *>(marker.scanLine(y));
        for (int x = 0; x < kPortMarkerWidth; ++x) {
            const int offset = kColorCimPortMarkerOffset + y * 9 + (x / 8) * 3;
            const int bit = 7 - (x % 8);
            const unsigned char p0 = static_cast<unsigned char>(data.at(offset));
            const unsigned char p2 = static_cast<unsigned char>(data.at(offset + 1));
            const unsigned char p1 = static_cast<unsigned char>(data.at(offset + 2));
            const int color = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2);
            line[x] = kEgaPalette.at(static_cast<size_t>(color));
        }
    }
    return marker;
}

} // namespace

WorldMapGenerator::WorldMapGenerator(GenerationRequest request, QObject *parent)
    : QThread(parent), m_request(std::move(request))
{
}

void WorldMapGenerator::run()
{
    if (!m_request.source.valid) {
        emit generationFailed(QStringLiteral("A validated, recognized source profile is required."));
        return;
    }

    const QString newgamePath = m_request.source.files.value(QStringLiteral("NEWGAME.DAT")).resolvedPath;
    const QString mapPutPath = m_request.source.files.value(QStringLiteral("MAP.PUT")).resolvedPath;
    const QString shinarioPath = m_request.source.files.value(QStringLiteral("SHINARIO.CIM")).resolvedPath;
    const QString colorCimPath = m_request.source.files.value(QStringLiteral("COLOR.CIM")).resolvedPath;
    if (newgamePath.isEmpty() || mapPutPath.isEmpty() || shinarioPath.isEmpty() || colorCimPath.isEmpty()) {
        emit generationFailed(QStringLiteral("The validated source profile does not expose all required paths."));
        return;
    }

    QString error;
    NewGameReader newgame;
    MapPutReader mapPut;
    if (!newgame.load(newgamePath, &error) || !mapPut.load(mapPutPath, &error)) {
        emit generationFailed(error);
        return;
    }
    const QJsonArray ports = portRecords(shinarioPath, 0, kActivePortRecords, true, &error);
    if (!error.isEmpty()) {
        emit generationFailed(error);
        return;
    }
    const QJsonArray unclassifiedPorts = portRecords(shinarioPath, kActivePortRecords, 8, false, &error);
    if (!error.isEmpty()) {
        emit generationFailed(error);
        return;
    }
    if (isInterruptionRequested()) {
        emit generationFailed(QStringLiteral("Map generation cancelled; no package was published."));
        return;
    }

    QDir outputRoot(m_request.outputDirectory);
    if (!outputRoot.exists() && !QDir().mkpath(m_request.outputDirectory)) {
        emit generationFailed(QStringLiteral("Cannot create output directory: %1").arg(m_request.outputDirectory));
        return;
    }
    const QString packageName = QStringLiteral("uw1-eng-dos-%1").arg(m_request.source.profileId);
    PackageStaging staging(outputRoot, packageName);
    if (!staging.create(&error)) {
        emit generationFailed(error);
        return;
    }
    const QString packagePath = staging.stagingPath();
    const QString imagePath = QDir(packagePath).filePath(QStringLiteral("world_map.png"));
    const QString portMarkerPath = QDir(packagePath).filePath(QStringLiteral("port_marker.png"));
    const QString metadataPath = QDir(packagePath).filePath(QStringLiteral("world_map.json"));
    const QString manifestPath = QDir(packagePath).filePath(QStringLiteral("manifest.json"));

    const qint64 requiredBytes = static_cast<qint64>(kImageWidth) * kImageHeight * 4;
    if (requiredBytes <= 0 || requiredBytes > static_cast<qint64>(std::numeric_limits<int>::max()) * 8) {
        emit generationFailed(QStringLiteral("The required image allocation is outside this Qt build's safe range."));
        return;
    }
    /* QRgb is native-endian 0xAARRGGBB, so this matching native format is required.
     * Writing QRgb directly to Format_RGBA8888 reverses red/blue on little-endian hosts. */
    QImage image(kImageWidth, kImageHeight, QImage::Format_ARGB32);
    if (image.isNull()) {
        emit generationFailed(QStringLiteral("Cannot allocate %1 MiB for the full world image.")
                                  .arg(requiredBytes / (1024 * 1024)));
        return;
    }
    /* PNG pHYs metadata: pixel width:height = 5:12.  Dots-per-meter is inverse physical size,
     * therefore X:Y = 12:5.  The raw 24×12 EGA grid remains lossless while capable viewers can
     * display its original 4:3 CRT physical aspect without our JSON metadata. */
    image.setDotsPerMeterX(12000);
    image.setDotsPerMeterY(5000);
    image.fill(kEgaPalette[0]);

    for (int sectorY = 0; sectorY < kSectorsY; ++sectorY) {
        const int band = latitudeBand(sectorY);
        for (int sectorX = 0; sectorX < kSectorsX; ++sectorX) {
            if (isInterruptionRequested()) {
                emit generationFailed(QStringLiteral("Map generation cancelled; no package was published."));
                return;
            }
            for (int subY = 0; subY < kSubCellsPerSector; ++subY) {
                for (int subX = 0; subX < kSubCellsPerSector; ++subX) {
                    const unsigned char terrain = newgame.terrainAt(sectorX, sectorY, subX / 2, subY / 2);
                    if (terrain >= 53) {
                        emit generationFailed(QStringLiteral("Terrain ID %1 is out of range at sector (%2,%3).")
                                                  .arg(terrain).arg(sectorX).arg(sectorY));
                        return;
                    }
                    const int texture = kTerrainLut[terrain * 4 + ((subY & 1) * 2 + (subX & 1))];
                    const int destinationX = (sectorX * kSubCellsPerSector + subX) * kCellWidth;
                    const int destinationY = (sectorY * kSubCellsPerSector + subY) * kCellHeight;
                    for (int py = 0; py < kCellHeight; ++py) {
                        auto *line = reinterpret_cast<QRgb *>(image.scanLine(destinationY + py));
                        for (int px = 0; px < kCellWidth; ++px) {
                            line[destinationX + px] = mapPut.pixel(band, texture, px, py);
                        }
                    }
                }
            }
            emit generationProgress(sectorY * kSectorsX + sectorX + 1, kSectorsX * kSectorsY);
        }
    }

    if (!writePngAtomically(imagePath, image, &error)) {
        emit generationFailed(QStringLiteral("Cannot write world_map.png: %1").arg(error));
        return;
    }

    QString hashError;
    const QString imageHash = sha256ForPath(imagePath, &hashError);
    if (imageHash.isEmpty()) {
        emit generationFailed(QStringLiteral("Cannot hash generated image: %1").arg(hashError));
        return;
    }
    const QImage portMarker = portMarkerImage(colorCimPath, &error);
    if (portMarker.isNull() || !writePngAtomically(portMarkerPath, portMarker, &error)) {
        emit generationFailed(QStringLiteral("Cannot write extracted port marker: %1").arg(error));
        return;
    }
    const QString portMarkerHash = sha256ForPath(portMarkerPath, &hashError);
    if (portMarkerHash.isEmpty()) {
        emit generationFailed(QStringLiteral("Cannot hash extracted port marker: %1").arg(hashError));
        return;
    }
    QJsonObject tilePyramid;
    if (!writeTilePyramid(image, packagePath, this, &tilePyramid, &error)) {
        emit generationFailed(error);
        return;
    }

    QJsonObject grid;
    grid.insert(QStringLiteral("sectors_x"), kSectorsX);
    grid.insert(QStringLiteral("sectors_y"), kSectorsY);
    grid.insert(QStringLiteral("sub_cells_per_sector"), kSubCellsPerSector);
    grid.insert(QStringLiteral("cell_pixel_width"), kCellWidth);
    grid.insert(QStringLiteral("cell_pixel_height"), kCellHeight);
    grid.insert(QStringLiteral("x_wraps"), true);
    grid.insert(QStringLiteral("y_wraps"), false);
    grid.insert(QStringLiteral("pixel_origin"), QStringLiteral("top-left; x grows east in sector space, y grows south"));

    QJsonObject metadata;
    metadata.insert(QStringLiteral("format"), QStringLiteral("water1-world-map"));
    metadata.insert(QStringLiteral("schema_version"), 6);
    metadata.insert(QStringLiteral("profile"), QJsonObject{{QStringLiteral("id"), m_request.source.profileId},
                                                            {QStringLiteral("newgame_sha256"), m_request.source.files.value(QStringLiteral("NEWGAME.DAT")).sha256},
                                                            {QStringLiteral("map_put_sha256"), m_request.source.files.value(QStringLiteral("MAP.PUT")).sha256},
                                                            {QStringLiteral("shinario_sha256"), m_request.source.files.value(QStringLiteral("SHINARIO.CIM")).sha256},
                                                            {QStringLiteral("color_cim_sha256"), m_request.source.files.value(QStringLiteral("COLOR.CIM")).sha256},
                                                            {QStringLiteral("main_exe_sha256"), m_request.source.files.value(QStringLiteral("MAIN.EXE")).sha256}});
    metadata.insert(QStringLiteral("image"), QJsonObject{{QStringLiteral("path"), QStringLiteral("world_map.png")},
                                                          {QStringLiteral("width"), kImageWidth},
                                                          {QStringLiteral("height"), kImageHeight},
                                                          {QStringLiteral("pixel_format"), QStringLiteral("ARGB32-native-endian")},
                                                          {QStringLiteral("dots_per_meter_x"), image.dotsPerMeterX()},
                                                          {QStringLiteral("dots_per_meter_y"), image.dotsPerMeterY()},
                                                          {QStringLiteral("sha256"), imageHash}});
    metadata.insert(QStringLiteral("grid"), grid);
    metadata.insert(QStringLiteral("terrain"), terrainModel(newgame));
    metadata.insert(QStringLiteral("coordinate_convention"), QJsonObject{
        {QStringLiteral("latitude"), QStringLiteral("sector_y: 0=90N, +5 degrees per sector, 18=0, 35=85S")},
        {QStringLiteral("longitude"), QStringLiteral("lon_rem=(sector_x+38)%72; 0..35=E, 36..71=W; degrees=5-step")}});
    metadata.insert(QStringLiteral("terrain_lut"), QJsonObject{
        {QStringLiteral("source_file"), QStringLiteral("MAIN.EXE")},
        {QStringLiteral("source_logical_address"), QStringLiteral("DS:0x2414")},
        {QStringLiteral("derivation"), QStringLiteral("baseline reverse-engineering literal; raw packed-file offset is not used")},
        {QStringLiteral("entries"), 53 * 4},
        {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(
            QByteArray(reinterpret_cast<const char *>(kTerrainLut.data()), static_cast<int>(kTerrainLut.size())),
            QCryptographicHash::Sha256).toHex())}});
    metadata.insert(QStringLiteral("terrain_palette"), QStringLiteral("main-exe-ega-bright-ac-mapping-rgb"));
    metadata.insert(QStringLiteral("display_pixel_aspect"), QJsonObject{
        {QStringLiteral("horizontal"), 5}, {QStringLiteral("vertical"), 12},
        {QStringLiteral("rationale"), QStringLiteral("DOS EGA 640x200 logical pixels displayed in a 4:3 CRT frame")}});
    metadata.insert(QStringLiteral("tile_pyramid"), tilePyramid);
    metadata.insert(QStringLiteral("port_marker"), QJsonObject{
        {QStringLiteral("path"), QStringLiteral("port_marker.png")},
        {QStringLiteral("width"), kPortMarkerWidth}, {QStringLiteral("height"), kPortMarkerHeight},
        {QStringLiteral("sha256"), portMarkerHash}, {QStringLiteral("source_file"), QStringLiteral("COLOR.CIM")},
        {QStringLiteral("source_offset"), QStringLiteral("0x0120")},
        {QStringLiteral("plane_order"), QStringLiteral("1,4,2")}});
    metadata.insert(QStringLiteral("ports"), ports);
    metadata.insert(QStringLiteral("unclassified_port_records"), unclassifiedPorts);
    metadata.insert(QStringLiteral("treasure_model"), treasureModel(newgame));
    metadata.insert(QStringLiteral("active_treasures"), QJsonArray{});
    if (!writeJsonAtomically(metadataPath, metadata, &error)) {
        emit generationFailed(QStringLiteral("Cannot write world_map.json: %1").arg(error));
        return;
    }

    const QString metadataHash = sha256ForPath(metadataPath, &hashError);
    if (metadataHash.isEmpty()) {
        emit generationFailed(QStringLiteral("Cannot hash generated metadata: %1").arg(hashError));
        return;
    }
    QJsonObject sourceFiles;
    for (auto it = m_request.source.files.cbegin(); it != m_request.source.files.cend(); ++it) {
        const SourceFileStatus &status = it.value();
        sourceFiles.insert(it.key(), QJsonObject{{QStringLiteral("bytes"), static_cast<double>(status.actualBytes)},
                                                 {QStringLiteral("sha256"), status.sha256}});
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("format"), QStringLiteral("water1-map-package-manifest"));
    manifest.insert(QStringLiteral("schema_version"), 5);
    manifest.insert(QStringLiteral("profile_id"), m_request.source.profileId);
    manifest.insert(QStringLiteral("generator_version"), QStringLiteral("0.2.0"));
    manifest.insert(QStringLiteral("source_files"), sourceFiles);
    manifest.insert(QStringLiteral("outputs"), QJsonObject{
        {QStringLiteral("image"), QJsonObject{{QStringLiteral("path"), QStringLiteral("world_map.png")},
                                                 {QStringLiteral("width"), kImageWidth}, {QStringLiteral("height"), kImageHeight},
                                                 {QStringLiteral("sha256"), imageHash}}},
        {QStringLiteral("port_marker"), QJsonObject{{QStringLiteral("path"), QStringLiteral("port_marker.png")},
                                                       {QStringLiteral("width"), kPortMarkerWidth},
                                                       {QStringLiteral("height"), kPortMarkerHeight},
                                                       {QStringLiteral("sha256"), portMarkerHash}}},
        {QStringLiteral("metadata"), QJsonObject{{QStringLiteral("path"), QStringLiteral("world_map.json")},
                                                    {QStringLiteral("sha256"), metadataHash}}},
        {QStringLiteral("tile_manifest"), QJsonObject{{QStringLiteral("path"), tilePyramid.value(QStringLiteral("manifest_path"))},
                                                         {QStringLiteral("sha256"), tilePyramid.value(QStringLiteral("manifest_sha256"))}}}});
    if (!writeJsonAtomically(manifestPath, manifest, &error)) {
        emit generationFailed(QStringLiteral("Cannot write manifest.json: %1").arg(error));
        return;
    }

    if (isInterruptionRequested()) {
        emit generationFailed(QStringLiteral("Map generation cancelled; no package was published."));
        return;
    }
    if (!staging.publish(&error)) {
        emit generationFailed(error);
        return;
    }

    emit generationSucceeded(outputRoot.filePath(packageName));
}
