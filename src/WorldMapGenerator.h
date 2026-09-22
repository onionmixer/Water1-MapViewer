#ifndef WATER1_MAPVIEWER_WORLD_MAP_GENERATOR_H
#define WATER1_MAPVIEWER_WORLD_MAP_GENERATOR_H

#include "SourceDirectory.h"

#include <QThread>

struct GenerationRequest {
    SourceDirectoryReport source;
    QString outputDirectory;
};

class WorldMapGenerator final : public QThread {
    Q_OBJECT

public:
    explicit WorldMapGenerator(GenerationRequest request, QObject *parent = nullptr);

signals:
    void generationProgress(int completedSectors, int totalSectors);
    void generationSucceeded(QString packageDirectory);
    void generationFailed(QString error);

protected:
    void run() override;

private:
    GenerationRequest m_request;
};

#endif
