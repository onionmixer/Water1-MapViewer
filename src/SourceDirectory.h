#ifndef WATER1_MAPVIEWER_SOURCE_DIRECTORY_H
#define WATER1_MAPVIEWER_SOURCE_DIRECTORY_H

#include <QMap>
#include <QString>
#include <QStringList>

struct SourceFileStatus {
    QString canonicalName;
    QString resolvedPath;
    qint64 expectedBytes = -1;
    qint64 actualBytes = -1;
    QString sha256;
    bool present = false;
    bool sizeMatches = false;
};

struct SourceDirectoryReport {
    QString rootPath;
    QMap<QString, SourceFileStatus> files;
    QStringList errors;
    QStringList warnings;
    QString profileId;
    bool valid = false;

    QString toText() const;
};

class SourceDirectory final {
public:
    static SourceDirectoryReport validate(const QString &directoryPath);
};

#endif
