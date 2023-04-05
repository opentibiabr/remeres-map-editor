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

#include "main.h"

#include "selection.h"
#include "tile.h"
#include "monster.h"
#include "npc.h"
#include "item.h"
#include "editor.h"
#include "gui.h"

Selection::Selection(Editor& editor) :
	busy(false),
	editor(editor),
	session(nullptr),
	subsession(nullptr)
{
	////
}

Selection::~Selection()
{
	tiles.clear();

	delete subsession;
	delete session;
}

Position Selection::minPosition() const
{
	Position minPos(0x10000, 0x10000, 0x10);
	for(const Tile* tile : tiles) {
		if(!tile) continue;
		const Position& pos = tile->getPosition();
		if(minPos.x > pos.x)
			minPos.x = pos.x;
		if(minPos.y > pos.y)
			minPos.y = pos.y;
		if(minPos.z > pos.z)
			minPos.z = pos.z;
	}
	return minPos;
}

Position Selection::maxPosition() const
{
	Position maxPos;
	for(const Tile* tile : tiles) {
		if(!tile) continue;
		const Position& pos = tile->getPosition();
		if(maxPos.x < pos.x)
			maxPos.x = pos.x;
		if(maxPos.y < pos.y)
			maxPos.y = pos.y;
		if(maxPos.z < pos.z)
			maxPos.z = pos.z;
	}
	return maxPos;
}

void Selection::add(Tile* tile, Item* item)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(item);

	if(item->isSelected()) return;

	// Make a copy of the tile with the item selected
	item->select();
	Tile* new_tile = tile->deepCopy(editor.map);
	item->deselect();

	if(g_settings.getInteger(Config::BORDER_IS_GROUND))
		if(item->isBorder())
			new_tile->selectGround();

	subsession->addChange(newd Change(new_tile));
}

void Selection::add(Tile* tile, SpawnMonster* spawnMonster)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(spawnMonster);

	if(spawnMonster->isSelected()) return;

	// Make a copy of the tile with the item selected
	spawnMonster->select();
	Tile* new_tile = tile->deepCopy(editor.map);
	spawnMonster->deselect();

	subsession->addChange(newd Change(new_tile));
}

void Selection::add(Tile* tile, SpawnNpc* spawnNpc)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(spawnNpc);

	if(spawnNpc->isSelected()) return;

	// Make a copy of the tile with the item selected
	spawnNpc->select();
	Tile* new_tile = tile->deepCopy(editor.map);
	spawnNpc->deselect();

	subsession->addChange(newd Change(new_tile));
}

void Selection::add(Tile* tile, Monster* monster)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(monster);

	if(monster->isSelected()) return;

	// Make a copy of the tile with the item selected
	monster->select();
	Tile* new_tile = tile->deepCopy(editor.map);
	monster->deselect();

	subsession->addChange(newd Change(new_tile));
}

void Selection::add(Tile* tile, Npc* npc)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(npc);

	if(npc->isSelected()) return;

	// Make a copy of the tile with the item selected
	npc->select();
	Tile* new_tile = tile->deepCopy(editor.map);
	npc->deselect();

	subsession->addChange(newd Change(new_tile));
}

void Selection::add(Tile* tile)
{
	ASSERT(subsession);
	ASSERT(tile);

	Tile* new_tile = tile->deepCopy(editor.map);
	new_tile->select();

	subsession->addChange(newd Change(new_tile));
}

void Selection::remove(Tile* tile, Item* item)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(item);

	bool selected = item->isSelected();
	item->deselect();
	Tile* new_tile = tile->deepCopy(editor.map);
	if(selected) item->select();
	if(item->isBorder() && g_settings.getInteger(Config::BORDER_IS_GROUND)) new_tile->deselectGround();

	subsession->addChange(newd Change(new_tile));
}

void Selection::remove(Tile* tile, SpawnMonster* spawnMonster)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(spawnMonster);

	bool selected = spawnMonster->isSelected();
	spawnMonster->deselect();
	Tile* new_tile = tile->deepCopy(editor.map);
	if(selected) spawnMonster->select();

	subsession->addChange(newd Change(new_tile));
}

void Selection::remove(Tile* tile, SpawnNpc* spawnNpc)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(spawnNpc);

	bool selected = spawnNpc->isSelected();
	spawnNpc->deselect();
	Tile* new_tile = tile->deepCopy(editor.map);
	if(selected) spawnNpc->select();

	subsession->addChange(newd Change(new_tile));
}

void Selection::remove(Tile* tile, Monster* monster)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(monster);

	bool selected = monster->isSelected();
	monster->deselect();
	Tile* new_tile = tile->deepCopy(editor.map);
	if(selected) monster->select();

	subsession->addChange(newd Change(new_tile));
}

void Selection::remove(Tile* tile, Npc* npc)
{
	ASSERT(subsession);
	ASSERT(tile);
	ASSERT(npc);

	bool selected = npc->isSelected();
	npc->deselect();
	Tile* new_tile = tile->deepCopy(editor.map);
	if(selected) npc->select();

	subsession->addChange(newd Change(new_tile));
}

void Selection::remove(Tile* tile)
{
	ASSERT(subsession);

	Tile* new_tile = tile->deepCopy(editor.map);
	new_tile->deselect();

	subsession->addChange(newd Change(new_tile));
}

void Selection::addInternal(Tile* tile)
{
	ASSERT(tile);

	tiles.insert(tile);
}

void Selection::removeInternal(Tile* tile)
{
	ASSERT(tile);
	tiles.erase(tile);
}

void Selection::clear()
{
	if(session) {
		for(TileSet::iterator it = tiles.begin(); it != tiles.end(); it++) {
			Tile* new_tile = (*it)->deepCopy(editor.map);
			new_tile->deselect();
			subsession->addChange(newd Change(new_tile));
		}
	} else {
		for(TileSet::iterator it = tiles.begin(); it != tiles.end(); it++) {
			(*it)->deselect();
		}
		tiles.clear();
	}
}

void Selection::start(SessionFlags flags)
{
	if(!(flags & INTERNAL)) {
		if(flags & SUBTHREAD) {
			;
		} else {
			session = editor.actionQueue->createBatch(ACTION_SELECT);
		}
		subsession = editor.actionQueue->createAction(ACTION_SELECT);
	}
	busy = true;
}

void Selection::commit()
{
	if(session) {
		ASSERT(subsession);
		// We need to step out of the session before we do the action, else peril awaits us!
		BatchAction* batch = session;
		session = nullptr;

		// Do the action
		batch->addAndCommitAction(subsession);

		// Create a newd action for subsequent selects
		subsession = editor.actionQueue->createAction(ACTION_SELECT);
		session = batch;
	}
}

void Selection::finish(SessionFlags flags)
{
	if(!(flags & INTERNAL)) {
		if(flags & SUBTHREAD) {
			ASSERT(subsession);
			subsession = nullptr;
		} else {
			ASSERT(session);
			ASSERT(subsession);
			// We need to exit the session before we do the action, else peril awaits us!
			BatchAction* batch = session;
			session = nullptr;

			batch->addAndCommitAction(subsession);
			editor.addBatch(batch, 2);

			session = nullptr;
			subsession = nullptr;
		}
	}
	busy = false;
}

void Selection::updateSelectionCount()
{
	if(size() > 0) {
		wxString ss;
		if(size() == 1) {
			ss << "One tile selected.";
		} else {
			ss << size() << " tiles selected.";
		}
		g_gui.SetStatusText(ss);
	}
}

void Selection::join(SelectionThread* thread)
{
	thread->Wait();

	ASSERT(session);
	session->addAction(thread->result);
	thread->selection.subsession = nullptr;

	delete thread;
}

SelectionThread::SelectionThread(Editor& editor, Position start, Position end) :
	wxThread(wxTHREAD_JOINABLE),
	editor(editor),
	start(start),
	end(end),
	selection(editor),
	result(nullptr)
{
	////
}

SelectionThread::~SelectionThread()
{
	////
}

void SelectionThread::Execute()
{
	Create();
	Run();
}

wxThread::ExitCode SelectionThread::Entry()
{
	selection.start(Selection::SUBTHREAD);
	for(int z = start.z; z >= end.z; --z) {
		for(int x = start.x; x <= end.x; ++x) {
			for(int y = start.y; y <= end.y; ++y) {
				Tile* tile = editor.map.getTile(x, y, z);
				if(!tile)
					continue;

				selection.add(tile);
			}
		}
		if(z <= GROUND_LAYER && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
			++start.x; ++start.y;
			++end.x; ++end.y;
		}
	}
	result = selection.subsession;
	selection.finish(Selection::SUBTHREAD);

	return nullptr;
}
