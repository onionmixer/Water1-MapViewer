#ifndef WATER1_MAPVIEWER_SAVE_DATA_READER_H
#define WATER1_MAPVIEWER_SAVE_DATA_READER_H

#include <QList>
#include <QString>

struct ActiveTreasure {
    int slot = -1;
    QString slotName;
    QString itemName;
    int itemValue = 0;
    int sectorX = 0;
    int sectorY = 0;
    int subX = 0;
    int subY = 0;
};

/* A displayed price row from the original port market screen.  A negative
 * purchasePrice means that the port does not offer that good for purchase. */
struct MarketPrice {
    int goodsId = -1;
    QString goodsName;
    int purchasePrice = -1;
    int salePrice = 0;
};

/* SAVE.DAT contains market economy records for port IDs 0 through 49 only.
 * The map has more ports, so callers must treat a missing record as unknown,
 * rather than inventing a price for it. */
struct PortMarket {
    int portId = -1;
    int population = 0;
    int prosperity = 0;
    int marketValue = 0;
    int region = 0;
    QList<MarketPrice> prices;
};

struct ShipStock {
    QString itemName;
    int amount = 0;
};

struct PlayerShipStatus {
    int slot = -1;
    QString shipName;
    QString captainName;
    int crew = 0;
    int maxCrew = 0;
    int hull = 0;
    int sails = 0;
    int speed = 0;
    int guns = 0;
    int morale = 0;
    int cargoUsed = 0;
    int cargoCapacity = 0;
    QList<ShipStock> stock;
};

struct PlayerFleetStatus {
    QString heroName;
    int gold = 0;
    QList<PlayerShipStatus> ships;
};

struct SaveSlot {
    int slot = -1;
    QString slotName;
    bool hasActiveTreasure = false;
    ActiveTreasure activeTreasure;
    QList<PortMarket> portMarkets;
    PlayerFleetStatus playerFleet;
};

/* Reader for the original, fixed-layout SAVE.DAT.  It never writes the save. */
class SaveDataReader final {
public:
    // Reads every allocated slot, including slots without an active treasure.
    static bool readSaveSlots(const QString &savePath, QList<SaveSlot> *saveSlots, QString *error);
    static bool readActiveTreasures(const QString &savePath, QList<ActiveTreasure> *treasures,
                                    QString *error);
};

#endif
