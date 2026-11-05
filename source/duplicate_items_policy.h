//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_DUPLICATE_ITEMS_POLICY_H_
#define RME_DUPLICATE_ITEMS_POLICY_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// Rules for items that a map stacks more than once on the same tile. They only see the facts below, never the editor
// types, so tests/duplicate_items_policy_test.cpp can exercise them on their own.
namespace DuplicateItems {

	struct ItemFacts {
		uint16_t id = 0;
		// What the editor keeps right above the ground: clip, bottom and top items, and the borders of ground brushes. The
		// client draws them over the ground, ahead of creatures and ordinary items.
		bool alwaysOnTop = false;
		// Nothing but its id: no attributes, count, container, door, depot or teleport data. Two plain copies of an item
		// are the same thing.
		bool plain = false;
		// Elevated items (tables and the like) are meant to be piled up.
		bool elevated = false;
	};

	// Calls onRedundant(index) for each non-elevated, always-on-top plain item with a later plain copy and returns how
	// many there are. The last copy is the one that stays: the client draws it over the earlier ones, so
	// the tile looks the same and the order of the other items is kept. Indices are passed in ascending order.
	template <typename Items, typename FactsOf, typename OnRedundant>
	size_t visitRedundantCopies(const Items &items, FactsOf factsOf, OnRedundant onRedundant) {
		const size_t count = std::size(items);
		size_t found = 0;
		for (size_t index = 0; index + 1 < count; ++index) {
			const ItemFacts item = factsOf(items[index]);
			if (!item.alwaysOnTop || !item.plain || item.elevated) {
				continue;
			}

			for (size_t later = index + 1; later < count; ++later) {
				const ItemFacts other = factsOf(items[later]);
				if (other.id == item.id && other.plain) {
					onRedundant(index);
					++found;
					break;
				}
			}
		}
		return found;
	}

	// Takes the redundant copies out of a vector-like container of items and hands each one to release (which owns
	// deleting it). Returns how many were taken out.
	template <typename Items, typename FactsOf, typename Release>
	size_t eraseRedundantCopies(Items &items, FactsOf factsOf, Release release) {
		std::vector<size_t> redundant;
		visitRedundantCopies(items, factsOf, [&redundant](size_t index) { redundant.push_back(index); });

		// Back to front, so the indices still to be erased stay valid.
		for (auto it = redundant.rbegin(); it != redundant.rend(); ++it) {
			auto element = std::move(items[*it]);
			items.erase(items.begin() + static_cast<std::ptrdiff_t>(*it));
			release(element);
		}
		return redundant.size();
	}

	// An ordinary item (not always on top, not elevated) whose id is listed twice on the tile. Mappers repeat these on
	// purpose to decorate, so they are only reported and never removed.
	template <typename Items, typename FactsOf>
	bool hasRepeatedOrdinaryItem(const Items &items, FactsOf factsOf) {
		const size_t count = std::size(items);
		for (size_t index = 0; index + 1 < count; ++index) {
			const ItemFacts item = factsOf(items[index]);
			if (item.id == 0 || item.alwaysOnTop || item.elevated) {
				continue;
			}

			for (size_t later = index + 1; later < count; ++later) {
				if (factsOf(items[later]).id == item.id) {
					return true;
				}
			}
		}
		return false;
	}

	struct TileRef {
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;
	};

	// What a pass over a map found. A tile with both kinds of repeats counts in both.
	struct Report {
		// The list of tiles stops here so a badly cluttered map does not flood the search window.
		static constexpr size_t MaxListedTiles = 2000;

		uint64_t redundantTiles = 0;
		uint64_t redundantCopies = 0;
		uint64_t repeatedOrdinaryTiles = 0;
		std::vector<TileRef> listedTiles;

		bool empty() const {
			return redundantTiles == 0 && repeatedOrdinaryTiles == 0;
		}

		template <typename Items, typename FactsOf>
		void addTile(const TileRef &where, const Items &items, FactsOf factsOf) {
			if (std::size(items) < 2) {
				return;
			}

			const size_t copies = visitRedundantCopies(items, factsOf, [](size_t) {});
			if (copies != 0) {
				++redundantTiles;
				redundantCopies += copies;
				if (listedTiles.size() < MaxListedTiles) {
					listedTiles.push_back(where);
				}
			}

			if (hasRepeatedOrdinaryItem(items, factsOf)) {
				++repeatedOrdinaryTiles;
			}
		}
	};

	// The maps whose save warning the mapper switched off. The editor keeps them in one setting, so this works on that
	// string: the keys joined by Separator, oldest first. A key identifies a map file; whoever calls normalizes the path so
	// the same file always gives the same key.
	namespace MutedMaps {

		constexpr char Separator = '|';
		// Past this the oldest entries are dropped, so the setting cannot grow without bound.
		constexpr size_t MaxEntries = 100;

		inline std::vector<std::string> split(const std::string &stored) {
			std::vector<std::string> keys;
			size_t start = 0;
			while (start <= stored.size()) {
				size_t end = stored.find(Separator, start);
				if (end == std::string::npos) {
					end = stored.size();
				}
				if (end > start) {
					keys.push_back(stored.substr(start, end - start));
				}
				start = end + 1;
			}
			return keys;
		}

		inline std::string join(const std::vector<std::string> &keys) {
			std::string stored;
			for (const std::string &key : keys) {
				if (!stored.empty()) {
					stored += Separator;
				}
				stored += key;
			}
			return stored;
		}

		// A key the list can hold: not empty and without the separator.
		inline bool usable(const std::string &key) {
			return !key.empty() && key.find(Separator) == std::string::npos;
		}

		inline bool contains(const std::string &stored, const std::string &key) {
			if (!usable(key)) {
				return false;
			}
			const std::vector<std::string> keys = split(stored);
			return std::find(keys.begin(), keys.end(), key) != keys.end();
		}

		// A key that cannot be held leaves the list as it was.
		inline std::string add(const std::string &stored, const std::string &key) {
			if (!usable(key)) {
				return stored;
			}
			std::vector<std::string> keys = split(stored);
			if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
				return stored;
			}
			keys.push_back(key);
			if (keys.size() > MaxEntries) {
				keys.erase(keys.begin(), keys.begin() + static_cast<std::ptrdiff_t>(keys.size() - MaxEntries));
			}
			return join(keys);
		}

		inline std::string remove(const std::string &stored, const std::string &key) {
			std::vector<std::string> keys = split(stored);
			keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
			return join(keys);
		}

	}

}

#endif
