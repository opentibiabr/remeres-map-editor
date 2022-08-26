#ifndef REMERES_MAP_EDITOR_RME_TILE_H
#define REMERES_MAP_EDITOR_RME_TILE_H
class RMETile : public Kmap::ITile {
public:
	RMETile(const Tile& tile) : tile(tile) {};

	[[nodiscard]] uint8_t X() const override {
		return tile.getX() & 0xFF;
	}

	[[nodiscard]] uint8_t Y() const override {
		return tile.getY() & 0xFF;
	}

	[[nodiscard]] uint32_t Flags() const override {
		return tile.getMapFlags();
	}

	[[nodiscard]] uint32_t HouseId() const override {
		return tile.getHouseID();
	}

	[[nodiscard]] uint16_t GroundId() const override {
		return hasGround() ? tile.ground->getID() : 0;
	}

	[[nodiscard]] const IItemDetail &GroundDetail() const override {
		return RMEItemDetail(tile.ground);
	}

	[[nodiscard]] const IItemAttribute &GroundAttribute() const override {
		return RMEItemAttribute(tile.ground);
	}

	[[nodiscard]] bool hasGround() override {
		if (tile.ground == nullptr || tile.ground->isMetaItem()) {
			return false;
		}

		for (Item *item: tile.items) {
			if (!ground->hasBorderEquivalent()) return true;
			if (item->getGroundEquivalent() == ground->getID()) {
				return false;
			}
		}

		return true;
	}

private:
	const Tile& tile;
};

#endif
