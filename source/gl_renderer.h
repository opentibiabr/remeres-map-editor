#ifndef RME_GL_RENDERER_H_
#define RME_GL_RENDERER_H_

#include <string>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <array>

// Minimal GL type forward declarations — full GL comes from glad in gl_renderer.cpp
using GLuint = unsigned int;
using GLint = int;

struct GLColor {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

class GLRenderer {
public:
	void init();
	void shutdown();

	void drawTexturedQuad(float x, float y, float w, float h, GLuint textureId, const GLColor &color);
	void drawColoredQuad(float x, float y, float w, float h, const GLColor &color);

	void drawRect(float x, float y, float w, float h, const GLColor &color, float lineWidth = 1.0f);
	void drawRoundedRect(float x, float y, float w, float h, float radius, const GLColor &fill);
	void drawRoundedRectOutline(float x, float y, float w, float h, float radius, const GLColor &color, float lineWidth = 1.0f);

	void drawLine(float x1, float y1, float x2, float y2, const GLColor &color, float width = 1.0f);
	void drawLines(const float* vertices, int pairCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float width = 1.0f);
	void drawStippledLines(const float* vertices, int pairCount, const GLColor &color, float width = 1.0f, int factor = 2, uint16_t pattern = 0xAAAA);

	void drawPolygon(const float* vertices, int vertexCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
	void drawTriangleFan(const float* vertices, int vertexCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

	void drawText(float x, float y, const std::string &text, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
	float getCharWidth(char c);
	float getLineHeight() const;
	float getAscent() const;
	void setRasterPos(float x, float y);
	void drawBitmapChar(char c);
	void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

	void setOrtho(float left, float right, float bottom, float top);

	void flush();
	static void invalidateTexture(GLuint id);

private:
	static std::vector<GLRenderer*> s_instances;
	bool initialized = false;
	GLuint vao = 0;
	GLuint vbo = 0;
	GLuint program = 0;
	GLint loc_projection = -1;
	GLint loc_useTexture = -1;
	GLint loc_texture = -1;
	GLint loc_stipple = -1;

	struct Vertex {
		float x;
		float y;
		float u;
		float v;
		uint8_t r;
		uint8_t g;
		uint8_t b;
		uint8_t a;
	};

	std::vector<Vertex> batch;
	GLuint current_texture = 0;

	void flushBatch();
	void drawThickLineSegment(float x1, float y1, float x2, float y2, float width, const GLColor &color);

	struct GlyphInfo {
		float u0;
		float v0;
		float u1;
		float v1; // UV coords in texture (normalized)
		float xoff;
		float yoff; // offset from cursor pos (pixels)
		float w;
		float h; // glyph size (pixels)
		float advance; // horizontal advance (pixels)
	};

	struct FontData {
		GLuint texture = 0;
		int texW = 0;
		int texH = 0;
		float fontSize = 0;
		float ascent = 0;
		float descent = 0;
		float lineHeight = 0;
		bool loaded = false;
		std::array<GlyphInfo, 96> glyphs {};
		std::array<float, 96> advances {};
	};

	FontData font;
	void initFontAtlas();
	void initFontAtlasFallback();

	float cursorX = 0;
	float cursorY = 0;
	GLColor textColor { 255, 255, 255, 255 };
};

#endif
