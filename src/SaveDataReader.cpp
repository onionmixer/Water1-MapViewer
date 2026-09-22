#include "SaveDataReader.h"

#include <QFile>
#include <QtGlobal>

namespace {

constexpr int kSlotCount = 5;
constexpr int kSlotNameOffset = 5;
constexpr int kSlotNameSize = 11;
constexpr int kSlotOffset = 0x0100;
constexpr int kSlotAllocation = 0x3000;
constexpr int kSlotDataSize = 0x1DAA;
constexpr int kExpectedFileSize = 61756;

constexpr int kItemsOffset = 0x00A8;
constexpr int kItemSize = 16;
constexpr int kTreasureItem = 0x1F;
constexpr int kItemNameSize = 14;
constexpr int kItemPriceOffset = 0x0E;
constexpr int kEncounterOffset = 0x1DA5;
constexpr int kTreasureFlagsOffset = 0x1DA9;
constexpr unsigned char kTreasureActiveFlag = 0x04;

constexpr int kPortDetailOffset = 0x0DBA;
constexpr int kPortDetailSize = 18;
constexpr int kPortDetailCount = 50;
constexpr int kPortFacilityOffset = 0x113E;
constexpr int kPortFacilitySize = 5;
constexpr int kPortProsperityOffset = 0x0B;
constexpr int kPortMarketValueOffset = 0x0C;
constexpr int kGoodsCount = 26;

constexpr int kMalesOffset = 0x0920;
constexpr int kMaleRecordSize = 20;
constexpr int kMaleNameSize = 15;
constexpr int kFleetOffset = 0x0C54;
constexpr int kFleetGoldOffset = 0x09;
constexpr int kCargoOffset = 0x126E;
constexpr int kCargoRecordSize = 24;
constexpr int kCargoCaptainOffset = 0x00;
constexpr int kCargoGoodsIdsOffset = 0x01;
constexpr int kCargoGoodsQuantitiesOffset = 0x06;
constexpr int kCargoFoodOffset = 0x10;
constexpr int kCargoWaterOffset = 0x12;
constexpr int kCargoLumberOffset = 0x14;
constexpr int kCargoMoraleOffset = 0x16;
constexpr int kPlayerShipCount = 5;
constexpr int kCargoGoodsSlots = 5;
constexpr int kSupplyOffset = 0x14A0;
constexpr int kSupplyRecordSize = 7;
constexpr int kSupplyHullOffset = 0x00;
constexpr int kSupplySailsOffset = 0x01;
constexpr int kSupplySpeedOffset = 0x02;
constexpr int kSupplyGunsOffset = 0x03;
constexpr int kSupplyCrewOffset = 0x04;
constexpr int kSupplyFlagsOffset = 0x06;
constexpr unsigned char kSupplyActiveFlag = 0x40;
constexpr int kShipTypeOffset = 0x12E6;
constexpr int kShipTypeRecordSize = 21;
constexpr int kShipTypeCount = 11;
constexpr int kShipTypeNameSize = 15;
constexpr int kShipTypeMaxCrewOffset = 0x0F;
constexpr int kShipTypeCapacityOffset = 0x12;

struct RegionBuySlot {
    int goodsId;
    int populationThreshold;
    int factor;
};

constexpr const char *kGoodsNames[kGoodsCount] = {
    "Pepper", "Cinnamon", "Nutmeg", "Pimento", "Cloves", "Olive oil", "Wine", "Sugar",
    "Cheese", "Grain", "Gold", "Silver", "Quartz", "Coral", "Ivory", "Pearl",
    "Cotton", "Raw silk", "Wool", "Cloth", "Silk", "Firearms", "Wood", "Porcelain",
    "Artwork", "Carpet",
};

/* MAIN.EXE's DS:0x2528 table: factors used by the original Buying Rate column. */
constexpr int kRegionSellFactors[8][kGoodsCount] = {
    {80,70,100,110,105,20,28,45,40,20,1000,120,320,280,300,220,80,120,65,40,140,75,60,105,400,320},
    {110,80,105,120,108,55,62,49,17,8,1000,120,325,275,350,220,110,130,55,40,150,70,58,40,400,340},
    {52,32,30,35,42,60,30,50,45,35,400,80,220,290,200,170,15,40,12,84,45,140,35,40,30,140},
    {35,43,28,30,29,38,30,50,40,42,300,280,98,260,195,160,11,40,16,89,48,190,10,35,45,120},
    {64,40,32,25,30,58,25,48,50,40,250,100,230,270,220,175,10,50,21,80,50,170,18,45,300,115},
    {42,30,28,15,30,18,20,50,58,9,890,380,300,350,290,230,10,80,14,85,50,230,60,40,150,30},
    {20,20,20,30,20,2,40,42,50,5,900,200,320,120,140,110,42,90,10,92,25,240,15,10,120,50},
    {2,3,3,4,4,10,15,45,62,6,1050,110,330,105,120,120,8,85,16,16,70,205,10,35,160,75},
};

constexpr RegionBuySlot kRegionBuySlots[8][9] = {
    {{5,1,42},{6,2,38},{19,2,60},{21,7,100},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},
    {{8,1,35},{9,0,12},{19,5,62},{21,6,105},{23,6,60},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},
    {{10,0,750},{16,4,30},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},
    {{12,2,180},{16,0,25},{22,0,28},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},
    {{10,3,400},{16,0,22},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},
    {{5,0,30},{9,1,18},{16,2,20},{18,2,20},{25,6,70},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},
    {{15,3,170},{20,6,45},{23,0,25},{24,6,140},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},
    {{9,0,10},{16,2,15},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0}},
};

QString latin1Field(const QByteArray &bytes)
{
    QByteArray value = bytes;
    const int nul = value.indexOf('\0');
    if (nul >= 0) value.truncate(nul);
    return QString::fromLatin1(value).trimmed();
}

int readLe16(const QByteArray &bytes, int offset)
{
    return static_cast<unsigned char>(bytes.at(offset))
        | (static_cast<unsigned char>(bytes.at(offset + 1)) << 8);
}

int clampOriginalMarketPrice(int value)
{
    return qBound(1, value, 9999);
}

QString goodsName(int goodsId)
{
    return goodsId >= 0 && goodsId < kGoodsCount
        ? QString::fromLatin1(kGoodsNames[goodsId])
        : QStringLiteral("Unknown goods (0x%1)").arg(goodsId, 2, 16, QLatin1Char('0')).toUpper();
}

PlayerFleetStatus readPlayerFleet(const QByteArray &slotData)
{
    PlayerFleetStatus fleet;
    fleet.heroName = latin1Field(slotData.mid(kMalesOffset, kMaleNameSize));
    fleet.gold = readLe16(slotData, kFleetOffset + kFleetGoldOffset);
    fleet.ships.reserve(kPlayerShipCount);
    for (int slot = 0; slot < kPlayerShipCount; ++slot) {
        const int supply = kSupplyOffset + slot * kSupplyRecordSize;
        if (!(static_cast<unsigned char>(slotData.at(supply + kSupplyFlagsOffset)) & kSupplyActiveFlag)) continue;
        const int cargo = kCargoOffset + slot * kCargoRecordSize;
        const int typeId = static_cast<unsigned char>(slotData.at(supply + kSupplyFlagsOffset)) & 0x0F;
        PlayerShipStatus ship;
        ship.slot = slot;
        ship.hull = static_cast<unsigned char>(slotData.at(supply + kSupplyHullOffset));
        ship.sails = static_cast<unsigned char>(slotData.at(supply + kSupplySailsOffset));
        ship.speed = static_cast<unsigned char>(slotData.at(supply + kSupplySpeedOffset));
        ship.guns = static_cast<unsigned char>(slotData.at(supply + kSupplyGunsOffset));
        ship.crew = readLe16(slotData, supply + kSupplyCrewOffset);
        ship.morale = static_cast<unsigned char>(slotData.at(cargo + kCargoMoraleOffset));
        if (typeId < kShipTypeCount) {
            const int type = kShipTypeOffset + typeId * kShipTypeRecordSize;
            ship.shipName = latin1Field(slotData.mid(type, kShipTypeNameSize));
            ship.maxCrew = readLe16(slotData, type + kShipTypeMaxCrewOffset);
            ship.cargoCapacity = readLe16(slotData, type + kShipTypeCapacityOffset);
        } else {
            ship.shipName = QStringLiteral("Unknown ship type (%1)").arg(typeId);
        }
        const int captainId = static_cast<unsigned char>(slotData.at(cargo + kCargoCaptainOffset));
        if (captainId == 0) {
            ship.captainName = fleet.heroName.isEmpty() ? QStringLiteral("Hero")
                                                         : QStringLiteral("%1 (Hero)").arg(fleet.heroName);
        } else if (captainId < 41) {
            ship.captainName = latin1Field(slotData.mid(kMalesOffset + captainId * kMaleRecordSize, kMaleNameSize));
        }
        if (ship.captainName.isEmpty()) ship.captainName = QStringLiteral("Unassigned");

        const qint16 foodRaw = static_cast<qint16>(readLe16(slotData, cargo + kCargoFoodOffset));
        const qint16 waterRaw = static_cast<qint16>(readLe16(slotData, cargo + kCargoWaterOffset));
        const int lumber = readLe16(slotData, cargo + kCargoLumberOffset);
        // Keep universal ship supplies at the top, followed by the five
        // original trade-cargo slots in their stored order.
        ship.stock.append({QStringLiteral("Food"), foodRaw / 10});
        ship.stock.append({QStringLiteral("Water"), waterRaw / 10});
        ship.stock.append({QStringLiteral("Lumber"), lumber});
        quint16 cargoUsed = 0;
        for (int cargoSlot = 0; cargoSlot < kCargoGoodsSlots; ++cargoSlot) {
            const int quantity = readLe16(slotData, cargo + kCargoGoodsQuantitiesOffset + cargoSlot * 2);
            cargoUsed = static_cast<quint16>(cargoUsed + quantity);
            const int goodsId = static_cast<unsigned char>(slotData.at(cargo + kCargoGoodsIdsOffset + cargoSlot));
            if (goodsId != 0xFF && quantity > 0) ship.stock.append({goodsName(goodsId), quantity});
        }
        cargoUsed = static_cast<quint16>(cargoUsed + foodRaw / 10);
        cargoUsed = static_cast<quint16>(cargoUsed + waterRaw / 10);
        cargoUsed = static_cast<quint16>(cargoUsed + lumber);
        ship.cargoUsed = cargoUsed;
        fleet.ships.append(ship);
    }
    return fleet;
}

QList<PortMarket> readPortMarkets(const QByteArray &slotData)
{
    QList<PortMarket> markets;
    markets.reserve(kPortDetailCount);
    for (int portId = 0; portId < kPortDetailCount; ++portId) {
        const int detail = kPortDetailOffset + portId * kPortDetailSize;
        const int facility = kPortFacilityOffset + portId * kPortFacilitySize;
        PortMarket market;
        market.portId = portId;
        market.population = readLe16(slotData, detail);
        market.prosperity = static_cast<unsigned char>(slotData.at(detail + kPortProsperityOffset));
        market.marketValue = readLe16(slotData, detail + kPortMarketValueOffset);
        market.region = static_cast<unsigned char>(slotData.at(facility));
        if (market.region < 0 || market.region > 7) market.region = 0;
        const int localGoodsId = static_cast<unsigned char>(slotData.at(facility + 1));
        const int localThreshold = static_cast<unsigned char>(slotData.at(facility + 2));
        const bool localProductionMature = localGoodsId >= 0 && localGoodsId < kGoodsCount
            && localThreshold * 50 <= market.population;
        const int rateBase = market.prosperity + 50;
        market.prices.reserve(kGoodsCount);

        for (int goodsId = 0; goodsId < kGoodsCount; ++goodsId) {
            MarketPrice price;
            price.goodsId = goodsId;
            price.goodsName = QString::fromLatin1(kGoodsNames[goodsId]);

            /* Original Sale Rate column: first matching mature regional slot,
             * then a mature local specialty; otherwise dashes. */
            for (const RegionBuySlot &slot : kRegionBuySlots[market.region]) {
                if (slot.goodsId < 0) break;
                if (slot.goodsId == goodsId && slot.populationThreshold * 50 <= market.population) {
                    price.purchasePrice = rateBase * slot.factor / 100;
                    break;
                }
            }
            if (price.purchasePrice < 0 && localProductionMature && localGoodsId == goodsId) {
                price.purchasePrice = rateBase * market.marketValue / 100;
            }

            /* Original Buying Rate column: the regional table, overridden by a
             * mature local specialty at half market rate plus one. */
            int salePrice = rateBase * kRegionSellFactors[market.region][goodsId] / 100;
            if (localProductionMature && localGoodsId == goodsId) {
                salePrice = (rateBase * market.marketValue / 100) / 2 + 1;
            }
            price.salePrice = clampOriginalMarketPrice(salePrice);
            market.prices.append(price);
        }
        markets.append(market);
    }
    return markets;
}

} // namespace

bool SaveDataReader::readSaveSlots(const QString &savePath, QList<SaveSlot> *saveSlots, QString *error)
{
    if (!saveSlots || !error) return false;
    saveSlots->clear();

    QFile file(savePath);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot open SAVE.DAT: %1").arg(file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.size() != kExpectedFileSize) {
        *error = QStringLiteral("Unexpected SAVE.DAT size: %1 bytes (expected %2 for the original format).")
                     .arg(data.size()).arg(kExpectedFileSize);
        return false;
    }

    for (int slot = 0; slot < kSlotCount; ++slot) {
        if (static_cast<unsigned char>(data.at(slot)) == 0) continue;
        const int slotDataOffset = kSlotOffset + slot * kSlotAllocation;
        if (slotDataOffset + kSlotDataSize > data.size()) {
            *error = QStringLiteral("SAVE.DAT slot %1 exceeds the file boundary.").arg(slot);
            return false;
        }
        const QByteArray slotData = data.mid(slotDataOffset, kSlotDataSize);
        SaveSlot saveSlot;
        saveSlot.slot = slot;
        saveSlot.slotName = latin1Field(data.mid(kSlotNameOffset + slot * kSlotNameSize, kSlotNameSize));
        saveSlot.portMarkets = readPortMarkets(slotData);
        saveSlot.playerFleet = readPlayerFleet(slotData);
        if (static_cast<unsigned char>(slotData.at(kTreasureFlagsOffset)) & kTreasureActiveFlag) {
            ActiveTreasure treasure;
            treasure.slot = slot;
            treasure.slotName = saveSlot.slotName;
            treasure.sectorX = static_cast<unsigned char>(slotData.at(kEncounterOffset));
            treasure.sectorY = static_cast<unsigned char>(slotData.at(kEncounterOffset + 1));
            treasure.subX = static_cast<unsigned char>(slotData.at(kEncounterOffset + 2));
            treasure.subY = static_cast<unsigned char>(slotData.at(kEncounterOffset + 3));
            treasure.itemName = latin1Field(slotData.mid(kItemsOffset + kTreasureItem * kItemSize, kItemNameSize));
            treasure.itemValue = readLe16(slotData, kItemsOffset + kTreasureItem * kItemSize + kItemPriceOffset);

            if (treasure.sectorX >= 72 || treasure.sectorY >= 36 || treasure.subX >= 14 || treasure.subY >= 14) {
                *error = QStringLiteral("SAVE.DAT slot %1 has an active treasure flag but invalid coordinates (%2, %3; %4, %5).")
                             .arg(slot).arg(treasure.sectorX).arg(treasure.sectorY)
                             .arg(treasure.subX).arg(treasure.subY);
                return false;
            }
            saveSlot.hasActiveTreasure = true;
            saveSlot.activeTreasure = treasure;
        }
        saveSlots->append(saveSlot);
    }
    return true;
}

bool SaveDataReader::readActiveTreasures(const QString &savePath, QList<ActiveTreasure> *treasures,
                                         QString *error)
{
    if (!treasures || !error) return false;
    QList<SaveSlot> saveSlots;
    if (!readSaveSlots(savePath, &saveSlots, error)) return false;

    treasures->clear();
    for (const SaveSlot &slot : saveSlots) {
        if (slot.hasActiveTreasure) treasures->append(slot.activeTreasure);
    }
    return true;
}
