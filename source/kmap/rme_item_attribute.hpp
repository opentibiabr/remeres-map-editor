#ifndef REMERES_MAP_EDITOR_RME_ITEM_ATTRIBUTE_H
#define REMERES_MAP_EDITOR_RME_ITEM_ATTRIBUTE_H

class RMEItemAttribute : public Kmap::IItemAttribute
{
public:
	RMEItemAttribute() {};
	RMEItemAttribute(Item* item) : item(item) {};

	[[nodiscard]] uint16_t Count() const override {
		return item != nullptr ? item->getCount() : 0;
	}

	[[nodiscard]] uint16_t ActionId() const override {
		return item != nullptr ? item->getActionID() : 0;
	}

	[[nodiscard]] uint16_t UniqueId() const override {
		return item != nullptr ? item->getUniqueID() : 0;
	}

	[[nodiscard]] const std::string& Text() const override {
		return item != nullptr ? item->getText() : "";
	}

	[[nodiscard]] const std::string& Description() const override {
		return item != nullptr ? item->getDescription() : "";
	}

private:
	Item* item;
};

#endif
