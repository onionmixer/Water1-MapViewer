#include "MapPackage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>

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
        const QByteArray data = file.read(1024 * 1024);
        if (data.isEmpty() && file.error() != QFile::NoError) {
            *error = file.errorString();
            return {};
        }
        hash.addData(data);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool isSafeRelativePath(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    return !cleaned.isEmpty() && !QDir::isAbsolutePath(cleaned)
        && cleaned != QLatin1String("..") && !cleaned.startsWith(QLatin1String("../"));
}

} // namespace

bool MapPackage::load(const QString &metadataPath, LoadedMapPackage *package, QString *error)
{
    if (!package) {
        *error = QStringLiteral("Internal error: missing output package.");
        return false;
    }
    QFile metadataFile(metadataPath);
    if (!metadataFile.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot open metadata: %1").arg(metadataFile.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("world_map.json is not a valid JSON object: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject metadata = document.object();
    if (metadata.value(QStringLiteral("format")).toString() != QLatin1String("water1-world-map")
        || metadata.value(QStringLiteral("schema_version")).toInt() != 6) {
        *error = QStringLiteral("Unsupported world-map metadata format or schema version.");
        return false;
    }
    const QJsonObject imageInfo = metadata.value(QStringLiteral("image")).toObject();
    const QString imageName = imageInfo.value(QStringLiteral("path")).toString();
    if (!isSafeRelativePath(imageName)) {
        *error = QStringLiteral("Metadata image path must be a simple package-relative filename.");
        return false;
    }
    const QString imagePath = QDir(QFileInfo(metadataPath).absolutePath()).filePath(imageName);
    QString hashError;
    const QString actualHash = sha256ForFile(imagePath, &hashError);
    if (actualHash.isEmpty()) {
        *error = QStringLiteral("Cannot read image: %1").arg(hashError);
        return false;
    }
    if (actualHash != imageInfo.value(QStringLiteral("sha256")).toString()) {
        *error = QStringLiteral("Image SHA-256 does not match world_map.json.");
        return false;
    }
    QImageReader reader(imagePath);
    reader.setAutoTransform(false);
    const QSize imageSize = reader.size();
    if (!imageSize.isValid()) {
        *error = QStringLiteral("Cannot read map image dimensions: %1").arg(reader.errorString());
        return false;
    }
    if (imageSize.width() != imageInfo.value(QStringLiteral("width")).toInt()
        || imageSize.height() != imageInfo.value(QStringLiteral("height")).toInt()) {
        *error = QStringLiteral("Decoded image dimensions do not match world_map.json.");
        return false;
    }
    const QJsonObject grid = metadata.value(QStringLiteral("grid")).toObject();
    if (grid.value(QStringLiteral("sectors_x")).toInt() != 72
        || grid.value(QStringLiteral("sectors_y")).toInt() != 36
        || grid.value(QStringLiteral("sub_cells_per_sector")).toInt() != 14
        || grid.value(QStringLiteral("cell_pixel_width")).toInt() != 24
        || grid.value(QStringLiteral("cell_pixel_height")).toInt() != 12) {
        *error = QStringLiteral("Unsupported or inconsistent map grid dimensions.");
        return false;
    }
    const QJsonObject pixelAspect = metadata.value(QStringLiteral("display_pixel_aspect")).toObject();
    if (pixelAspect.value(QStringLiteral("horizontal")).toInt() != 5
        || pixelAspect.value(QStringLiteral("vertical")).toInt() != 12) {
        *error = QStringLiteral("Unsupported or missing original EGA display pixel-aspect metadata.");
        return false;
    }
    if (metadata.value(QStringLiteral("terrain_palette")).toString()
        != QLatin1String("main-exe-ega-bright-ac-mapping-rgb")) {
        *error = QStringLiteral("Unsupported or missing original EGA terrain palette metadata.");
        return false;
    }
    const QJsonObject portMarker = metadata.value(QStringLiteral("port_marker")).toObject();
    const QString portMarkerName = portMarker.value(QStringLiteral("path")).toString();
    if (!isSafeRelativePath(portMarkerName) || portMarker.value(QStringLiteral("width")).toInt() != 24
        || portMarker.value(QStringLiteral("height")).toInt() != 12
        || portMarker.value(QStringLiteral("plane_order")).toString() != QLatin1String("1,4,2")) {
        *error = QStringLiteral("Original port marker metadata is missing or invalid.");
        return false;
    }
    const QString portMarkerPath = QDir(QFileInfo(metadataPath).absolutePath()).filePath(portMarkerName);
    const QString portMarkerHash = sha256ForFile(portMarkerPath, &hashError);
    if (portMarkerHash.isEmpty() || portMarkerHash != portMarker.value(QStringLiteral("sha256")).toString()) {
        *error = QStringLiteral("Extracted port marker SHA-256 does not match world_map.json.");
        return false;
    }
    QImageReader portMarkerReader(portMarkerPath);
    portMarkerReader.setAutoTransform(false);
    const QImage portMarkerImage = portMarkerReader.read();
    if (portMarkerImage.isNull() || portMarkerImage.width() != 24 || portMarkerImage.height() != 12) {
        *error = QStringLiteral("Extracted port marker cannot be decoded as 24x12 pixels.");
        return false;
    }
    const QJsonObject tilePyramid = metadata.value(QStringLiteral("tile_pyramid")).toObject();
    if (tilePyramid.value(QStringLiteral("format")).toString() != QLatin1String("nearest-neighbor-tile-pyramid")
        || tilePyramid.value(QStringLiteral("tile_size")).toInt() != 512
        || tilePyramid.value(QStringLiteral("levels")).toArray().size() != 7) {
        *error = QStringLiteral("Tile pyramid metadata is missing or unsupported.");
        return false;
    }
    const QString tileManifestName = tilePyramid.value(QStringLiteral("manifest_path")).toString();
    if (!isSafeRelativePath(tileManifestName)) {
        *error = QStringLiteral("Tile manifest path is unsafe.");
        return false;
    }
    const QString tileManifestPath = QDir(QFileInfo(metadataPath).absolutePath()).filePath(tileManifestName);
    const QString tileManifestHash = sha256ForFile(tileManifestPath, &hashError);
    if (tileManifestHash.isEmpty() || tileManifestHash != tilePyramid.value(QStringLiteral("manifest_sha256")).toString()) {
        *error = QStringLiteral("Tile manifest SHA-256 does not match world_map.json.");
        return false;
    }
    QFile tileManifestFile(tileManifestPath);
    if (!tileManifestFile.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot open tile manifest: %1").arg(tileManifestFile.errorString());
        return false;
    }
    QJsonParseError tileManifestError;
    const QJsonDocument tileManifestDocument = QJsonDocument::fromJson(tileManifestFile.readAll(), &tileManifestError);
    if (tileManifestError.error != QJsonParseError::NoError || !tileManifestDocument.isObject()) {
        *error = QStringLiteral("Tile manifest is invalid JSON: %1").arg(tileManifestError.errorString());
        return false;
    }
    const QJsonObject tileManifest = tileManifestDocument.object();
    if (tileManifest.value(QStringLiteral("format")).toString() != QLatin1String("water1-map-tile-manifest")
        || tileManifest.value(QStringLiteral("schema_version")).toInt() != 1
        || tileManifest.value(QStringLiteral("tile_size")).toInt() != 512) {
        *error = QStringLiteral("Tile manifest format is unsupported.");
        return false;
    }
    const QJsonArray tileEntries = tileManifest.value(QStringLiteral("tiles")).toArray();
    if (tileEntries.size() != 774) {
        *error = QStringLiteral("Tile manifest must contain exactly 774 baseline tiles.");
        return false;
    }
    for (const QJsonValue &value : tileEntries) {
        const QJsonObject tile = value.toObject();
        if (!isSafeRelativePath(tile.value(QStringLiteral("path")).toString())
            || tile.value(QStringLiteral("width")).toInt() <= 0 || tile.value(QStringLiteral("height")).toInt() <= 0
            || tile.value(QStringLiteral("sha256")).toString().size() != 64) {
            *error = QStringLiteral("Tile manifest contains an invalid tile record.");
            return false;
        }
    }
    const QJsonObject terrain = metadata.value(QStringLiteral("terrain")).toObject();
    const QByteArray terrainBytes = QByteArray::fromBase64(
        terrain.value(QStringLiteral("data_base64")).toString().toLatin1());
    if (terrain.value(QStringLiteral("encoding")).toString() != QLatin1String("base64-sector-row-major-7x7-terrain-id")
        || terrainBytes.size() != 72 * 36 * 49) {
        *error = QStringLiteral("Terrain data is missing or has an invalid length.");
        return false;
    }
    const QJsonObject treasureModel = metadata.value(QStringLiteral("treasure_model")).toObject();
    const QByteArray candidateBits = QByteArray::fromBase64(
        treasureModel.value(QStringLiteral("cell_bitset_base64")).toString().toLatin1());
    if (treasureModel.value(QStringLiteral("cell_bitset_encoding")).toString() != QLatin1String("base64-lsb-first-row-major")
        || candidateBits.size() != (72 * 14 * 36 * 14) / 8) {
        *error = QStringLiteral("Treasure candidate index is missing or has an invalid length.");
        return false;
    }
    const QJsonArray ports = metadata.value(QStringLiteral("ports")).toArray();
    if (ports.size() != 70) {
        *error = QStringLiteral("The baseline package must contain exactly 70 active port records.");
        return false;
    }
    for (const QJsonValue &value : ports) {
        const QJsonObject politicalStatus = value.toObject().value(QStringLiteral("political_status")).toObject();
        const int ownershipCode = politicalStatus.value(QStringLiteral("ownership_code")).toInt(-1);
        if (ownershipCode < 0 || ownershipCode > 3
            || politicalStatus.value(QStringLiteral("sovereignty")).toString().isEmpty()
            || politicalStatus.value(QStringLiteral("independent")).toBool() != (ownershipCode == 0)
            || politicalStatus.value(QStringLiteral("court_available")).toBool()
                   != politicalStatus.value(QStringLiteral("ruler_available")).toBool()) {
            *error = QStringLiteral("Port political-status metadata is missing or inconsistent.");
            return false;
        }
    }
    *package = LoadedMapPackage{QFileInfo(metadataPath).canonicalFilePath(), imagePath, imageSize, portMarkerImage,
                                metadata, tilePyramid, tileManifest, ports};
    if (package->metadataPath.isEmpty()) package->metadataPath = QFileInfo(metadataPath).absoluteFilePath();
    return true;
}
