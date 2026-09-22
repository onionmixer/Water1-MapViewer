#ifndef WATER1_MAPVIEWER_MAP_PACKAGE_H
#define WATER1_MAPVIEWER_MAP_PACKAGE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QSize>
#include <QString>

struct LoadedMapPackage {
    QString metadataPath;
    QString imagePath;
    QSize imageSize;
    QImage portMarkerImage;
    QJsonObject metadata;
    QJsonObject tilePyramid;
    QJsonObject tileManifest;
    QJsonArray ports;
};

class MapPackage final {
public:
    static bool load(const QString &metadataPath, LoadedMapPackage *package, QString *error);
};

#endif
