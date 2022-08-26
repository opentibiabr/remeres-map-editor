#ifndef REMERES_MAP_EDITOR_RME_ITEM_DETAIL_H
#define REMERES_MAP_EDITOR_RME_ITEM_DETAIL_H

class RMEItemDetail : public Kmap::IItemDetail
{
public:
	RMEItemDetail() {};
	RMEItemDetail(Item* item) : item(item) {};

	[[nodiscard]] uint16_t DepotId() const override {
		if (item == nullptr) return 0;

		Depot *depot = dynamic_cast<Depot*>(item);
		return depot ? depot->getDepotID() : 0;
	}

	[[nodiscard]] uint16_t DoorId() const override {
		if (item == nullptr) return 0;

		Door *door = dynamic_cast<Door*>(item);
		return door ? door->getDoorID() : 0;
	}

	[[nodiscard]] const IPosition& TeleportPosition() const override {
		if (item == nullptr) return Position(0, 0, 0);

		Teleport *teleport = dynamic_cast<Teleport*>(item);
		return teleport ? teleport->getDestination() : Position(0, 0, 0);
	}

private:
	Item* item;
};

#endif
