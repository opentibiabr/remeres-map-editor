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

#include "app/main.h"

#ifdef __WINDOWS__
	#include <windows.h>
	#include <psapi.h>
	#pragma comment(lib, "psapi.lib")
#else
	#include <unistd.h>
	#include <cstring>
#endif

#include <cstdint>
#include <thread>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <format>
#include <array>

#include "editor/editor.h"
#include "ui/gui.h" // KNOWN_VIOLATION
#include "rendering/sprites.h"
#include "rendering/map_drawer.h"
#include <unordered_set>
#include "rendering/map_display.h"
#include "editor/copybuffer.h"
#include "live/live_socket.h"
#include "rendering/graphics.h"

#include "brushes/doodad_brush.h" // KNOWN_VIOLATION
#include "brushes/monster_brush.h" // KNOWN_VIOLATION
#include "brushes/house_exit_brush.h" // KNOWN_VIOLATION
#include "brushes/house_brush.h" // KNOWN_VIOLATION
#include "brushes/spawn_monster_brush.h" // KNOWN_VIOLATION
#include "rendering/sprite_appearances.h"
#include "brushes/npc_brush.h" // KNOWN_VIOLATION
#include "brushes/spawn_npc_brush.h" // KNOWN_VIOLATION
#include "brushes/wall_brush.h" // KNOWN_VIOLATION
#include "brushes/carpet_brush.h" // KNOWN_VIOLATION
#include "brushes/raw_brush.h" // KNOWN_VIOLATION
#include "brushes/table_brush.h" // KNOWN_VIOLATION
#include "brushes/waypoint_brush.h" // KNOWN_VIOLATION
#include "brushes/zone_brush.h" // KNOWN_VIOLATION
#include "rendering/light_drawer.h"
#include "rendering/gl_renderer.h"

std::pair<float, float> MapDrawer::MeasureTooltipText(const MapTooltip &tp) {
	float lineH = renderer->getLineHeight();
	float maxWidth = 0.0f;
	int totalLines = 0;

	auto getLineWidth = [this](const std::string &s) {
		float w = 0.0f;
		for (char c : s) {
			if (!iscntrl(c)) {
				w += renderer->getCharWidth(c);
			}
		}
		return w;
	};

	for (const auto &entry : tp.entries) {
		float labelW = getLineWidth(entry.label);

		std::string val = entry.value;

		size_t pos = 0;
		while (pos < val.size()) {
			size_t next_nl = val.find('\n', pos);
			std::string line = val.substr(pos, (next_nl == std::string::npos ? std::string::npos : next_nl - pos));

			float valW = getLineWidth(line);

			maxWidth = std::max(maxWidth, labelW + valW);
			totalLines++;

			if (next_nl == std::string::npos) {
				break;
			}
			pos = next_nl + 1;
		}

		if (val.empty()) {
			maxWidth = std::max(maxWidth, labelW);
			totalLines++;
		}
	}

	float width = maxWidth + 8.0f;
	float height = static_cast<float>(totalLines) * lineH + 4.0f;
	return { width, height };
}

void MapDrawer::RenderTooltipText(const MapTooltip &tp, float startx, float starty, float fade) {
	float lineH = renderer->getLineHeight();
	float x = startx + 4.0f;
	float y = starty + renderer->getAscent() + 2.0f;

	float lum = 0.299f * tp.r + 0.587f * tp.g + 0.114f * tp.b;
	uint8_t valR;
	uint8_t valG;
	uint8_t valB;
	uint8_t lblR;
	uint8_t lblG;
	uint8_t lblB;
	if (lum > 128.0f) {
		valR = valG = valB = 0;
		lblR = lblG = lblB = 100;
	} else {
		valR = valG = valB = 255;
		lblR = lblG = lblB = 180;
	}
	auto fa = static_cast<uint8_t>(fade * 255);

	auto drawString = [this](const std::string &s) {
		for (char c : s) {
			if (!iscntrl(c)) {
				renderer->drawBitmapChar(c);
			}
		}
	};

	for (const auto &entry : tp.entries) {
		renderer->setRasterPos(x, y);

		float labelW = 0.0f;
		renderer->setColor(lblR, lblG, lblB, fa);
		for (char c : entry.label) {
			labelW += renderer->getCharWidth(c);
			renderer->drawBitmapChar(c);
		}

		renderer->setColor(valR, valG, valB, fa);
		std::string val = entry.value;
		size_t pos = 0;
		bool firstLine = true;
		while (pos < val.size()) {
			size_t next_nl = val.find('\n', pos);
			std::string line = val.substr(pos, (next_nl == std::string::npos ? std::string::npos : next_nl - pos));

			if (!firstLine) {
				y += lineH;
				renderer->setRasterPos(x + labelW, y);
			}

			drawString(line);

			firstLine = false;
			if (next_nl == std::string::npos) {
				break;
			}
			pos = next_nl + 1;
		}

		y += lineH;
	}
}

static uint64_t tooltipKey(int x, int y, int z) {
	return (static_cast<uint64_t>(x) << 40) | (static_cast<uint64_t>(y) << 16) | static_cast<uint64_t>(z);
}

void MapDrawer::DrawTooltips() {
	float fadeSpeed = 0.02f;
	if (options.isTooltips()) {
		globalTooltipFade = std::min(globalTooltipFade + fadeSpeed, 1.0f);
	} else {
		globalTooltipFade = std::max(globalTooltipFade - fadeSpeed, 0.0f);
	}

	if (globalTooltipFade <= 0.0f || tooltips.empty()) {
		return;
	}

	renderer->flush();
	renderer->setOrtho(0, static_cast<float>(screensize_x), static_cast<float>(screensize_y), 0);

	for (const auto &tp : tooltips) {
		auto [width, height] = MeasureTooltipText(tp);

		int screen_x;
		int screen_y;
		getDrawPosition(Position(tp.map_x, tp.map_y, tp.map_z), screen_x, screen_y);

		float x = static_cast<float>(screen_x + rme::TileSize / 2) / zoom;
		float y = static_cast<float>(screen_y + rme::TileSize / 2) / zoom;
		float center = width / 2.0f;
		float space = 7.0f;
		float startx = x - center;
		float endx = x + center;
		float starty = y - (height + space);
		float endy = y - space;

		// drop shadow
		float radius = 4.0f;
		float shadowOff = 3.0f;
		auto shadowAlpha = static_cast<uint8_t>(globalTooltipFade * 70);
		renderer->drawRoundedRect(startx + shadowOff, starty + shadowOff, endx - startx, endy - starty, radius, { 0, 0, 0, shadowAlpha });
		std::array<float, 6> shadowArrow = { x + space + shadowOff, endy + shadowOff, x + shadowOff, y + shadowOff, x - space + shadowOff, endy + shadowOff };
		renderer->drawPolygon(shadowArrow.data(), 3, 0, 0, 0, shadowAlpha);

		// background (rounded rect body + arrow triangle)
		auto bgAlpha = static_cast<uint8_t>(globalTooltipFade * 200);
		renderer->drawRoundedRect(startx, starty, endx - startx, endy - starty, radius, { tp.r, tp.g, tp.b, bgAlpha });

		std::array<float, 6> arrow = { x + space, endy, x, y, x - space, endy };
		renderer->drawPolygon(arrow.data(), 3, tp.r, tp.g, tp.b, bgAlpha);

		// border (rounded rect outline + arrow lines)
		auto borderAlpha = static_cast<uint8_t>(globalTooltipFade * 180);
		renderer->drawRoundedRectOutline(startx, starty, endx - startx, endy - starty, radius, { 0, 0, 0, borderAlpha }, 1.0f);

		std::array<float, 16> arrowLines = {
			x + space,
			endy,
			x,
			y,
			x,
			y,
			x - space,
			endy,
		};
		renderer->drawLines(arrowLines.data(), 2, 0, 0, 0, borderAlpha, 1.0f);

		RenderTooltipText(tp, startx, starty, globalTooltipFade);
	}

	renderer->flush();

	std::array<int, 4> vPort {};
	glGetIntegerv(GL_VIEWPORT, vPort.data());
	renderer->setOrtho(0, vPort[2] * zoom, vPort[3] * zoom, 0);
}

void MapDrawer::UpdateRAMUsage() {
#ifdef __WINDOWS__
	PROCESS_MEMORY_COUNTERS pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
		current_ram = pmc.WorkingSetSize / (1024 * 1024);
	}
#else
	std::ifstream file("/proc/self/statm");
	if (file.is_open()) {
		uint32_t size = 0;
		uint32_t rss = 0;
		file >> size >> rss;
		current_ram = (rss * sysconf(_SC_PAGESIZE)) / (1024 * 1024);
	}
#endif
}

void MapDrawer::UpdateCPUUsage() {
#ifdef __WINDOWS__
	FILETIME ftime, fsys, fuser;
	ULARGE_INTEGER now, sys, user;

	GetSystemTimeAsFileTime(&ftime);
	memcpy(&now, &ftime, sizeof(FILETIME));

	GetProcessTimes(GetCurrentProcess(), &ftime, &ftime, &fsys, &fuser);
	memcpy(&sys, &fsys, sizeof(FILETIME));
	memcpy(&user, &fuser, sizeof(FILETIME));

	if (last_now_time.QuadPart != 0) {
		double process_diff = (double)((sys.QuadPart - last_sys_time.QuadPart) + (user.QuadPart - last_cpu_time.QuadPart));
		double system_diff = (double)(now.QuadPart - last_now_time.QuadPart);

		if (system_diff > 0) {
			current_cpu = (process_diff / system_diff) * 100.0;
			unsigned int num_cores = std::thread::hardware_concurrency();
			if (num_cores > 0) {
				current_cpu = current_cpu / num_cores;
			}
			if (current_cpu > 100.0) {
				current_cpu = 100.0;
			}
		}
	}

	last_cpu_time = user;
	last_sys_time = sys;
	last_now_time = now;
#else
	std::ifstream file("/proc/self/stat");
	if (!file.is_open()) {
		return;
	}

	std::string buffer;
	if (!std::getline(file, buffer)) {
		return;
	}

	size_t pos = buffer.find(')');
	if (pos == std::string::npos) {
		return;
	}

	uint64_t utime = 0;
	uint64_t stime = 0;
	std::istringstream iss(buffer.substr(pos + 2));
	std::string dummy;
	char state;
	int pid;
	int ppid;
	int pgrp;
	int session;
	int tty_nr;
	int tpgid;
	unsigned int flags;
	unsigned int minflt;
	unsigned int cminflt;
	unsigned int majflt;

	if (unsigned int cmajflt = 0; !(iss >> state >> pid >> ppid >> pgrp >> session >> tty_nr >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime)) {
		return;
	}

	uint64_t process_time = utime + stime;
	std::ifstream stat_file("/proc/stat");
	if (!stat_file.is_open()) {
		return;
	}

	uint64_t user = 0;
	uint64_t nice = 0;
	uint64_t system = 0;
	uint64_t idle = 0;
	uint64_t iowait = 0;
	uint64_t irq = 0;
	uint64_t softirq = 0;
	uint64_t steal = 0;

	std::string cpu_label;
	stat_file >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

	if (cpu_label == "cpu") {
		uint64_t total_time = user + nice + system + idle + iowait + irq + softirq + steal;
		if (last_total_time != 0) {
			uint64_t total_diff = total_time - last_total_time;
			uint64_t process_diff = process_time - last_process_time;

			if (total_diff > 0) {
				current_cpu = (100.0 * process_diff) / total_diff;
			}
		}

		last_total_time = total_time;
		last_process_time = process_time;
	}
#endif
}

std::string MapDrawer::FormatPerformanceStats() const {
	return std::format("FPS: {:.1f} | CPU: {:.1f}% | RAM: {} MB", current_fps, current_cpu, current_ram);
}

void MapDrawer::DrawPerformanceStats() {
	frame_count++;
	long elapsed = perf_update_timer.Time();
	if (elapsed >= 500) {
		current_fps = (frame_count * 1000.0) / elapsed;
		frame_count = 0;
		UpdateRAMUsage();
		UpdateCPUUsage();
		perf_update_timer.Start();
	}

	std::string stats_text = FormatPerformanceStats();

	renderer->flush();
	renderer->setOrtho(0, static_cast<float>(screensize_x), static_cast<float>(screensize_y), 0);

	renderer->drawText(10.0f, 20.0f, stats_text, 255, 255, 0, 255);

	renderer->flush();

	std::array<int, 4> vPort {};
	glGetIntegerv(GL_VIEWPORT, vPort.data());
	renderer->setOrtho(0, vPort[2] * zoom, vPort[3] * zoom, 0);
}

void MapDrawer::DrawLight() const {
	// draw in-game light
	light_drawer->draw(start_x, start_y, end_x, end_y, view_scroll_x, view_scroll_y, renderer.get());
}

MapTooltip &MapDrawer::MakeTooltip(int map_x, int map_y, int map_z, uint8_t r, uint8_t g, uint8_t b) {
	tooltips.emplace_back(map_x, map_y, map_z, r, g, b);
	return tooltips.back();
}

void MapDrawer::AddLight(TileLocation* location) {
	if (!options.show_lights || !location) {
		return;
	}

	auto tile = location->get();
	if (!tile) {
		return;
	}

	auto &position = location->getPosition();

	if (tile->ground) {
		if (tile->ground->hasLight()) {
			light_drawer->addLight(position.x, position.y, position.z, tile->ground->getLight());
		}
	}

	bool hidden = options.hide_items_when_zoomed && zoom > 10.f;
	if (!hidden && !tile->items.empty()) {
		for (auto item : tile->items) {
			if (item->hasLight()) {
				light_drawer->addLight(position.x, position.y, position.z, item->getLight());
			}
		}
	}
}

void MapDrawer::getColor(Brush* brush, const Position &position, uint8_t &r, uint8_t &g, uint8_t &b) {
	if (brush->canDraw(&editor.getMap(), position)) {
		if (brush->isWaypoint()) {
			r = 0x00;
			g = 0xff, b = 0x00;
		} else {
			r = 0x00;
			g = 0x00, b = 0xff;
		}
	} else {
		r = 0xff;
		g = 0x00, b = 0x00;
	}
}

void MapDrawer::TakeScreenshot(uint8_t* screenshot_buffer) {
	glPixelStorei(GL_PACK_ALIGNMENT, 1); // 1 byte alignment

	for (int i = 0; i < screensize_y; ++i) {
		glReadPixels(0, screensize_y - i, screensize_x, 1, GL_RGB, GL_UNSIGNED_BYTE, (GLubyte*)(screenshot_buffer) + 3 * screensize_x * i);
	}
}

void MapDrawer::ShowPositionIndicator(const Position &position) {
	pos_indicator = position;
	pos_indicator_timer.Start();
}

void MapDrawer::glBlitTexture(int sx, int sy, int textureId, const GLColor &color, const BlitOptions &opts) {
	if (textureId <= 0) {
		return;
	}

	auto width = rme::TileSize;
	auto height = rme::TileSize;
	// Adjusts the offset of normal sprites
	if (!opts.isEditorSprite) {
		SpriteSheetPtr sheet = g_spriteAppearances.getSheetBySpriteId(opts.spriteId > 0 ? opts.spriteId : textureId);
		if (!sheet) {
			return;
		}

		width = sheet->getSpriteSize().width;
		height = sheet->getSpriteSize().height;

		// If the sprite is an outfit and the size is 64x64, adjust the offset
		if (width == 64 && height == 64 && (opts.outfit.lookType > 0 || opts.outfit.lookItem > 0)) {
			GameSprite* spr = g_gui.gfx.getCreatureSprite(opts.outfit.lookType);
			if (spr && spr->getDrawOffset().x == 8 && spr->getDrawOffset().y == 8) {
				sx -= width / 2;
				sy -= height / 2;
			}
		}
	}

	// Adjust zoom if necessary
	if (opts.adjustZoom) {
		if (zoom < 1.0f) {
			float offset = 10 / (10 * zoom);
			width = std::max<int>(16, static_cast<int>(width * zoom));
			height = std::max<int>(16, static_cast<int>(height * zoom));
			sx += offset;
			sy += offset;
		} else if (zoom > 1.f) {
			float offset = (10 * zoom);
			width += static_cast<int>(offset);
			height += static_cast<int>(offset);
			sx -= offset;
			sy -= offset;
		}
	}

	if (opts.outfit.lookType > 0) {
		spdlog::debug("Blitting outfit {} at ({}, {})", opts.outfit.name, sx, sy);
	}

	renderer->drawTexturedQuad(sx, sy, width, height, textureId, color, opts.uv.u0, opts.uv.v0, opts.uv.u1, opts.uv.v1);
}

void MapDrawer::glBlitSquare(int x, int y, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha, int size /* = rme::TileSize */) const {
	renderer->drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(size), static_cast<float>(size), { red, green, blue, alpha });
}

void MapDrawer::glBlitSquare(int x, int y, const wxColor &color, int size /* = rme::TileSize */) const {
	renderer->drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(size), static_cast<float>(size), { color.Red(), color.Green(), color.Blue(), color.Alpha() });
}

void MapDrawer::getBrushColor(MapDrawer::BrushColor color, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) {
	switch (color) {
		case COLOR_BRUSH:
			r = g_settings.getInteger(Config::CURSOR_RED);
			g = g_settings.getInteger(Config::CURSOR_GREEN);
			b = g_settings.getInteger(Config::CURSOR_BLUE);
			a = g_settings.getInteger(Config::CURSOR_ALPHA);
			break;

		case COLOR_FLAG_BRUSH:
		case COLOR_HOUSE_BRUSH:
			r = g_settings.getInteger(Config::CURSOR_ALT_RED);
			g = g_settings.getInteger(Config::CURSOR_ALT_GREEN);
			b = g_settings.getInteger(Config::CURSOR_ALT_BLUE);
			a = g_settings.getInteger(Config::CURSOR_ALT_ALPHA);
			break;

		case COLOR_SPAWN_BRUSH:
		case COLOR_SPAWN_NPC_BRUSH:
		case COLOR_ERASER:
		case COLOR_INVALID:
			r = 166;
			g = 0;
			b = 0;
			a = 128;
			break;

		case COLOR_VALID:
			r = 0;
			g = 166;
			b = 0;
			a = 128;
			break;

		default:
			r = 255;
			g = 255;
			b = 255;
			a = 128;
			break;
	}
}

void MapDrawer::getCheckColor(Brush* brush, const Position &pos, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) {
	if (brush->canDraw(&editor.getMap(), pos)) {
		getBrushColor(COLOR_VALID, r, g, b, a);
	} else {
		getBrushColor(COLOR_INVALID, r, g, b, a);
	}
}

void MapDrawer::drawRect(int x, int y, int w, int h, const wxColor &color, float width) {
	renderer->drawRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), { color.Red(), color.Green(), color.Blue(), color.Alpha() }, width);
}

void MapDrawer::drawFilledRect(int x, int y, int w, int h, const wxColor &color) {
	renderer->drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), { color.Red(), color.Green(), color.Blue(), color.Alpha() });
}

void MapDrawer::getDrawPosition(const Position &position, int &x, int &y) {
	int offset;
	if (position.z <= rme::MapGroundLayer) {
		offset = (rme::MapGroundLayer - position.z) * rme::TileSize;
	} else {
		offset = rme::TileSize * (floor - position.z);
	}

	x = ((position.x * rme::TileSize) - view_scroll_x) - offset;
	y = ((position.y * rme::TileSize) - view_scroll_y) - offset;
}
