// Standalone test for source/duplicate_items_policy.h. The policy does not depend on the editor, so this builds
// without wxWidgets or vcpkg:
//
//   g++ -std=c++23 -Wall -Wextra -Isource tests/duplicate_items_policy_test.cpp -o duplicate_items_policy_test
//   ./duplicate_items_policy_test

#include "duplicate_items_policy.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using DuplicateItems::ItemFacts;
using DuplicateItems::Report;
using DuplicateItems::TileRef;

namespace {

	int failures = 0;

	void check(bool condition, const std::string &what) {
		if (!condition) {
			++failures;
			std::cerr << "FAILED: " << what << '\n';
		}
	}

	using Tile = std::vector<ItemFacts>;

	const auto identity = [](const ItemFacts &facts) { return facts; };

	// Ids 100-199 are always-on-top items, 200-299 ordinary items, 300 an elevated item.
	ItemFacts overlay(uint16_t id, bool plain = true) {
		return ItemFacts { id, true, plain, false };
	}

	ItemFacts ordinary(uint16_t id, bool plain = true) {
		return ItemFacts { id, false, plain, false };
	}

	ItemFacts elevated(uint16_t id) {
		return ItemFacts { id, false, true, true };
	}

	std::vector<size_t> redundant(const Tile &tile) {
		std::vector<size_t> indices;
		const size_t count = DuplicateItems::visitRedundantCopies(tile, identity, [&indices](size_t index) { indices.push_back(index); });
		check(count == indices.size(), "the returned count matches the callbacks");
		return indices;
	}

	std::vector<uint16_t> ids(const Tile &tile) {
		std::vector<uint16_t> result;
		for (const auto &facts : tile) {
			result.push_back(facts.id);
		}
		return result;
	}

	using Ids = std::vector<uint16_t>;
	using Indices = std::vector<size_t>;
	using Keys = std::vector<std::string>;

	void redundantCopies() {
		check(redundant({}).empty(), "an empty tile has nothing redundant");
		check(redundant({ overlay(100) }).empty(), "a single item is never redundant");
		check(redundant({ overlay(100), overlay(101) }).empty(), "different ids are not copies");
		check(redundant({ overlay(100), overlay(100) }) == Indices { 0 }, "of two copies the first goes and the last stays");
		check(redundant({ overlay(100), overlay(100), overlay(100) }) == Indices { 0, 1 }, "of three copies only the last stays");
		check(redundant({ overlay(100), overlay(101), overlay(100) }) == Indices { 0 }, "copies need not be neighbours");
		check(redundant({ overlay(100), overlay(101), overlay(100), overlay(101) }) == Indices { 0, 1 }, "each id keeps its own last copy");

		// An item that carries data is a different thing, even when the id matches.
		check(redundant({ overlay(100), overlay(100, false) }).empty(), "a later copy with attributes is not a plain duplicate");
		check(redundant({ overlay(100, false), overlay(100) }).empty(), "an earlier copy with attributes is never removed");
		check(redundant({ overlay(100), overlay(100, false), overlay(100) }) == Indices { 0 }, "the copy with attributes between two plain ones stays");
		check(redundant({ overlay(100, false), overlay(100, false) }).empty(), "two copies with attributes stay");

		// Only always-on-top items count: ordinary repeats and elevated piles are decoration.
		check(redundant({ ordinary(200), ordinary(200) }).empty(), "repeated ordinary items are not redundant");
		check(redundant({ elevated(300), elevated(300) }).empty(), "repeated elevated items are not redundant");
		check(redundant({ overlay(100), ordinary(200), overlay(100), ordinary(200) }) == Indices { 0 }, "ordinary items between copies do not matter");
		check(redundant({ ItemFacts {}, ItemFacts {} }).empty(), "empty slots are ignored");
	}

	void erasing() {
		auto erase = [](Tile tile, size_t* released = nullptr) {
			size_t releasedCount = 0;
			const size_t erased = DuplicateItems::eraseRedundantCopies(tile, identity, [&releasedCount](const ItemFacts &) { ++releasedCount; });
			check(erased == releasedCount, "every erased item is released exactly once");
			if (released) {
				*released = releasedCount;
			}
			return ids(tile);
		};

		size_t released = 0;
		check(erase({ overlay(100), overlay(101), overlay(100) }, &released) == Ids { 101, 100 } && released == 1, "the first copy is erased and the order of the rest is kept");
		check(erase({ overlay(100), overlay(100), overlay(100) }) == Ids { 100 }, "three copies end as one");
		check(erase({ overlay(100), overlay(101), overlay(100), overlay(101), overlay(102) }) == Ids { 100, 101, 102 }, "several ids are cleaned in one pass");
		check(erase({ overlay(100), overlay(100, false), ordinary(200), ordinary(200) }) == Ids { 100, 100, 200, 200 }, "attributes and decoration survive");
		check(erase({ ordinary(200), overlay(100), ordinary(201), overlay(100), ordinary(200) }) == Ids { 200, 201, 100, 200 }, "items that are not copies keep their place");

		// Cleaning a tile leaves nothing more to clean.
		Tile tile { overlay(100), overlay(100), overlay(101), overlay(100) };
		DuplicateItems::eraseRedundantCopies(tile, identity, [](const ItemFacts &) { });
		check(redundant(tile).empty(), "a cleaned tile has no redundant copies left");
	}

	void repeatedOrdinary() {
		auto repeated = [](const Tile &tile) { return DuplicateItems::hasRepeatedOrdinaryItem(tile, identity); };
		check(!repeated({}), "an empty tile has no repeats");
		check(!repeated({ ordinary(200) }), "a single ordinary item is not a repeat");
		check(repeated({ ordinary(200), ordinary(200) }), "two ordinary items with the same id are a repeat");
		check(repeated({ ordinary(200), ordinary(201), ordinary(200) }), "repeats need not be neighbours");
		check(repeated({ ordinary(200, false), ordinary(200, false) }), "ordinary repeats count even with attributes");
		check(!repeated({ overlay(100), overlay(100) }), "always-on-top copies are not ordinary repeats");
		check(!repeated({ elevated(300), elevated(300) }), "elevated piles are not repeats");
		check(!repeated({ ordinary(200), ordinary(201) }), "different ordinary items are not repeats");
		check(!repeated({ ItemFacts {}, ItemFacts {} }), "empty slots are not repeats");
	}

	void report() {
		Report report;
		check(report.empty(), "a new report is empty");

		report.addTile({ 1, 2, 7 }, Tile { overlay(100) }, identity);
		report.addTile({ 1, 3, 7 }, Tile {}, identity);
		report.addTile({ 1, 4, 7 }, Tile { overlay(100), overlay(101) }, identity);
		check(report.empty(), "tiles without repeats leave the report empty");

		report.addTile({ 10, 20, 7 }, Tile { overlay(100), overlay(100), overlay(100) }, identity);
		check(report.redundantTiles == 1 && report.redundantCopies == 2 && report.repeatedOrdinaryTiles == 0, "a tile with three copies adds two redundant ones");
		check(report.listedTiles.size() == 1 && report.listedTiles[0].x == 10 && report.listedTiles[0].y == 20 && report.listedTiles[0].z == 7, "the tile is listed with its position");

		report.addTile({ 11, 20, 7 }, Tile { ordinary(200), ordinary(200) }, identity);
		check(report.redundantTiles == 1 && report.repeatedOrdinaryTiles == 1 && report.listedTiles.size() == 1, "ordinary repeats are counted but not listed");

		report.addTile({ 12, 20, 7 }, Tile { overlay(100), ordinary(200), overlay(100), ordinary(200) }, identity);
		check(report.redundantTiles == 2 && report.redundantCopies == 3 && report.repeatedOrdinaryTiles == 2, "a tile with both kinds counts in both");
		check(!report.empty(), "a report with findings is not empty");

		Report crowded;
		for (size_t index = 0; index < Report::MaxListedTiles + 5; ++index) {
			crowded.addTile({ static_cast<int32_t>(index), 0, 7 }, Tile { overlay(100), overlay(100) }, identity);
		}
		check(crowded.redundantTiles == Report::MaxListedTiles + 5 && crowded.redundantCopies == Report::MaxListedTiles + 5, "counts keep growing past the list limit");
		check(crowded.listedTiles.size() == Report::MaxListedTiles, "the list stops at the limit");
		check(crowded.listedTiles.back().x == static_cast<int32_t>(Report::MaxListedTiles - 1), "the list keeps the first tiles found");
	}

	// The Thais tile 32379,32212,7 of the develop map (ground 17464): ten clip borders in one stack, 17502 three times and
	// 4534 twice, so the ground and its borders filled the ten things a client tile holds and left no slot for a creature.
	void realTile() {
		Tile tile;
		for (const uint16_t id : { 19291, 17502, 4534, 9539, 308, 4534, 17502, 17492, 17485, 17502 }) {
			tile.push_back(overlay(id));
		}

		check(redundant(tile) == Indices { 1, 2, 6 }, "the real tile has three redundant copies");
		DuplicateItems::eraseRedundantCopies(tile, identity, [](const ItemFacts &) { });
		check(ids(tile) == Ids { 19291, 9539, 308, 4534, 17492, 17485, 17502 }, "the real tile keeps the last copy of each border in order");
		check(tile.size() + 1 <= 9, "with its ground the real tile fits the nine things that leave a slot for a creature");
	}

	// The same rules through a getter that is not the identity, the way the editor reads its items.
	void throughAGetter() {
		struct Stored {
			uint16_t id;
			bool overlay;
			bool plain;
		};
		const std::vector<Stored> items { { 100, true, true }, { 200, false, true }, { 100, true, true } };
		const auto factsOf = [](const Stored &stored) { return ItemFacts { stored.id, stored.overlay, stored.plain, false }; };
		Indices found;
		DuplicateItems::visitRedundantCopies(items, factsOf, [&found](size_t index) { found.push_back(index); });
		check(found == Indices { 0 }, "a custom facts getter drives the same rules");
	}

	// The save warning is switched off per map: the setting holds the paths of the maps that did, and nothing else.
	void mutedMaps() {
		namespace Muted = DuplicateItems::MutedMaps;
		const std::string world = "c:\\maps\\world.otbm";
		const std::string other = "c:\\maps\\other.otbm";

		check(!Muted::contains("", world), "an empty list mutes no map");
		check(Muted::split("").empty(), "an empty list has no entries");

		const std::string one = Muted::add("", world);
		check(Muted::contains(one, world), "a map that was muted is muted");
		check(!Muted::contains(one, other), "another map is not muted");
		check(!Muted::contains(one, "c:\\maps\\world.otbm2"), "a longer path that starts the same is another map");
		check(!Muted::contains(one, "c:\\maps\\world"), "a shorter path that starts the same is another map");

		const std::string two = Muted::add(one, other);
		check(Muted::contains(two, world) && Muted::contains(two, other), "muting a second map keeps the first");
		check(Muted::split(two) == Keys { world, other }, "the maps are kept in the order they were muted");
		check(Muted::add(two, world) == two, "muting a map twice changes nothing");

		const std::string back = Muted::remove(two, world);
		check(!Muted::contains(back, world) && Muted::contains(back, other), "turning the warning back on affects that map only");
		check(Muted::remove(back, world) == back, "turning on a warning that was on changes nothing");
		check(Muted::remove("", world).empty(), "removing from an empty list leaves it empty");
		check(Muted::remove(Muted::remove(two, world), other).empty(), "removing every map empties the list");

		// Paths that cannot be told apart in the stored string are refused.
		check(Muted::add("", "").empty(), "an empty key is not stored");
		check(Muted::add(one, "") == one, "an empty key leaves the list alone");
		check(Muted::add(one, "a|b") == one, "a key with the separator is not stored");
		check(!Muted::contains(one, "a|b") && !Muted::contains("a|b", "a|b"), "a key with the separator is never muted");
		check(Muted::split("a||b|") == Keys { "a", "b" }, "empty segments are ignored");

		// The list does not grow without bound: the oldest maps are forgotten first.
		std::string crowded;
		for (size_t index = 0; index < Muted::MaxEntries + 5; ++index) {
			crowded = Muted::add(crowded, "map" + std::to_string(index));
		}
		check(Muted::split(crowded).size() == Muted::MaxEntries, "the list stops at the limit");
		check(!Muted::contains(crowded, "map0") && !Muted::contains(crowded, "map4"), "the oldest maps are dropped");
		check(Muted::contains(crowded, "map5") && Muted::contains(crowded, "map" + std::to_string(Muted::MaxEntries + 4)), "the newest maps stay");
	}

}

int main() {
	redundantCopies();
	erasing();
	repeatedOrdinary();
	report();
	realTile();
	throughAGetter();
	mutedMaps();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "duplicate items policy scenarios passed\n";
	return EXIT_SUCCESS;
}
