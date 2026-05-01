#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <Windows.h>
#elif defined(__APPLE__)
	#include <dlfcn.h>
#else
	#include <dlfcn.h>
#endif

#include <glad/glad.h>

#include "main.h"
#include "gl_renderer.h"
#include <array>
#include <cstring>
#include <cmath>
#include <numbers>
#include <fstream>
#include <stb_truetype.h>

#ifdef _WIN32
static void* rmeGetGLProc(const char* name) {
	auto p = (void*)wglGetProcAddress(name);
	if (p == nullptr || p == (void*)0x1 || p == (void*)0x2 || p == (void*)0x3 || p == (void*)-1) {
		static HMODULE gl = LoadLibraryA("opengl32.dll");
		p = (void*)GetProcAddress(gl, name);
	}
	return p;
}
#elif defined(__APPLE__)
static void* rmeGetGLProc(const char* name) {
	static void* lib = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY);
	return lib ? dlsym(lib, name) : nullptr;
}
#else
typedef void (*__GLXextFuncPtr)(void);
extern "C" __GLXextFuncPtr glXGetProcAddressARB(const unsigned char*);
static void* rmeGetGLProc(const char* name) {
	void* p = (void*)glXGetProcAddressARB((const GLubyte*)name);
	if (!p) {
		static void* lib = dlopen("libGL.so.1", RTLD_LAZY);
		if (lib) {
			p = dlsym(lib, name);
		}
	}
	return p;
}
#endif

std::vector<GLRenderer*> GLRenderer::s_instances;

static const char* const vertSrc = R"(
#version 330
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
uniform mat4 uProjection;
out vec2 vUV;
out vec4 vColor;
void main(){
	gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
	vUV = aUV;
	vColor = aColor;
}
)";

static const char* const fragSrc = R"(  
#version 330  
in vec2 vUV;  
in vec4 vColor;  
uniform sampler2D uTexture;  
uniform int uStipple;  
out vec4 FragColor;  
void main() {  
    if (uStipple != 0) {  
        float p = gl_FragCoord.x + gl_FragCoord.y;  
        if (mod(p, 4.0) < 2.0) discard;  
    }  
    FragColor = texture(uTexture, vUV) * vColor;  
}  
)";

void GLRenderer::initFontAtlas() {
	// Load TTF font
	const float fontSize = 14.0f;
	std::string fontPath;

#ifdef _WIN32
	fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
#elif defined(__APPLE__)
	fontPath = "/System/Library/Fonts/Helvetica.ttc";
#else
	fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif

	// Try bundled font first
	std::string bundledPath = "data/fonts/DejaVuSans.ttf";
	std::ifstream testFile(bundledPath, std::ios::binary);
	if (testFile.good()) {
		fontPath = bundledPath;
	}
	testFile.close();

	std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		// Fallback to old wxBitmap method
		initFontAtlasFallback();
		return;
	}

	auto fileSize = file.tellg();
	file.seekg(0);
	std::vector<uint8_t> ttfData(fileSize);
	file.read(reinterpret_cast<char*>(ttfData.data()), fileSize);
	file.close();

	stbtt_fontinfo stbFont;
	if (!stbtt_InitFont(&stbFont, ttfData.data(), 0)) {
		initFontAtlasFallback();
		return;
	}

	float scale = stbtt_ScaleForPixelHeight(&stbFont, fontSize);

	int ascent;
	int descent;
	int lineGap;
	stbtt_GetFontVMetrics(&stbFont, &ascent, &descent, &lineGap);
	font.ascent = ascent * scale;
	font.lineHeight = (ascent - descent + lineGap) * scale;

	// Bake glyphs into atlas
	const int texW = 512;
	const int texH = 512;
	std::vector<uint8_t> bitmap(texW * texH, 0);

	int penX = 1;
	int penY = 1;
	int rowH = 0;

	for (int i = 0; i < 96; i++) {
		const auto ch = 32 + i;
		int x0;
		int y0;
		int x1;
		int y1;
		stbtt_GetCodepointBitmapBox(&stbFont, ch, scale, scale, &x0, &y0, &x1, &y1);

		int gw = x1 - x0;
		int gh = y1 - y0;

		if (penX + gw + 1 >= texW) {
			penX = 1;
			penY += rowH + 1;
			rowH = 0;
		}

		if (penY + gh + 1 >= texH) {
			break; // atlas full
		}

		stbtt_MakeCodepointBitmap(&stbFont, &bitmap[penY * texW + penX], gw, gh, texW, scale, scale, ch);

		font.glyphs[i].u0 = static_cast<float>(penX) / texW;
		font.glyphs[i].v0 = static_cast<float>(penY) / texH;
		font.glyphs[i].u1 = static_cast<float>(penX + gw) / texW;
		font.glyphs[i].v1 = static_cast<float>(penY + gh) / texH;
		font.glyphs[i].xoff = static_cast<float>(x0);
		font.glyphs[i].yoff = static_cast<float>(y0);
		font.glyphs[i].w = static_cast<float>(gw);
		font.glyphs[i].h = static_cast<float>(gh);

		int advW;
		int lsb;
		stbtt_GetCodepointHMetrics(&stbFont, ch, &advW, &lsb);
		font.glyphs[i].advance = advW * scale;
		font.advances[i] = advW * scale;

		penX += gw + 1;
		if (gh + 1 > rowH) {
			rowH = gh + 1;
		}
	}

	// Upload as RGBA (white + alpha)
	std::vector<uint8_t> pixels(texW * texH * 4);
	for (int i = 0; i < texW * texH; i++) {
		pixels[i * 4] = 255;
		pixels[i * 4 + 1] = 255;
		pixels[i * 4 + 2] = 255;
		pixels[i * 4 + 3] = bitmap[i];
	}

	glGenTextures(1, &font.texture);
	glBindTexture(GL_TEXTURE_2D, font.texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	font.texW = texW;
	font.texH = texH;
	font.loaded = true;
}

void GLRenderer::initFontAtlasFallback() {
	const int glyphW = 10;
	const int glyphH = 16;
	const int cols = 16;
	const int rows = 6;
	const int texW = cols * glyphW;
	const int texH = rows * glyphH;

	wxBitmap bmp(texW, texH, 24);
	wxMemoryDC dc(bmp);
	dc.SetBackground(*wxBLACK_BRUSH);
	dc.Clear();
	dc.SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	dc.SetTextForeground(*wxWHITE);
	for (int i = 0; i < 96; i++) {
		int col = i % cols;
		int row = i / cols;
		const auto ch = static_cast<char>(32 + i);
		dc.DrawText(wxString(ch), col * glyphW, row * glyphH);
	}
	dc.SelectObject(wxNullBitmap);
	wxImage img = bmp.ConvertToImage();
	std::vector<uint8_t> pixels(texW * texH * 4);
	for (int y = 0; y < texH; y++) {
		for (int x = 0; x < texW; x++) {
			int si = (y * texW + x) * 3;
			int di = (y * texW + x) * 4;
			uint8_t lum = img.GetData()[si];
			pixels[di] = 255;
			pixels[di + 1] = 255;
			pixels[di + 2] = 255;
			pixels[di + 3] = lum;
		}
	}

	glGenTextures(1, &font.texture);
	glBindTexture(GL_TEXTURE_2D, font.texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	font.ascent = 12.0f;
	font.lineHeight = 16.0f;
	for (int i = 0; i < 96; i++) {
		int col = i % cols;
		int row = i / cols;
		font.glyphs[i].u0 = static_cast<float>(col * glyphW) / texW;
		font.glyphs[i].v0 = static_cast<float>(row * glyphH) / texH;
		font.glyphs[i].u1 = static_cast<float>((col + 1) * glyphW) / texW;
		font.glyphs[i].v1 = static_cast<float>((row + 1) * glyphH) / texH;
		font.glyphs[i].xoff = 0;
		font.glyphs[i].yoff = -12.0f;
		font.glyphs[i].w = static_cast<float>(glyphW);
		font.glyphs[i].h = static_cast<float>(glyphH);
		font.glyphs[i].advance = static_cast<float>(glyphW);
		font.advances[i] = static_cast<float>(glyphW);
	}

	font.texW = texW;
	font.texH = texH;
	font.loaded = true;
}

void GLRenderer::init() {
	if (std::find(s_instances.begin(), s_instances.end(), this) == s_instances.end()) {
		s_instances.push_back(this);
	}
	if (initialized) {
		return;
	}

	if (!gladLoadGLLoader((GLADloadproc)rmeGetGLProc)) {
		wxLogError("GLRenderer::init — gladLoadGLLoader failed");
		return;
	}

	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &vertSrc, nullptr);
	glCompileShader(vs);
	{
		GLint ok = 0;
		glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			std::array<char, 512> log {};
			glGetShaderInfoLog(vs, log.size(), nullptr, log.data());
			wxLogError("GLRenderer::init — vertex shader compile error: %s", log.data());
			glDeleteShader(vs);
			return;
		}
	}

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &fragSrc, nullptr);
	glCompileShader(fs);
	{
		GLint ok = 0;
		glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			std::array<char, 512> log {};
			glGetShaderInfoLog(fs, log.size(), nullptr, log.data());
			wxLogError("GLRenderer::init — fragment shader compile error: %s", log.data());
			glDeleteShader(vs);
			glDeleteShader(fs);
			return;
		}
	}

	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	{
		GLint ok = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok) {
			std::array<char, 512> log {};
			glGetProgramInfoLog(program, log.size(), nullptr, log.data());
			wxLogError("GLRenderer::init — program link error: %s", log.data());
			glDeleteProgram(program);
			program = 0;
			glDeleteShader(vs);
			glDeleteShader(fs);
			return;
		}
	}

	glDeleteShader(vs);
	glDeleteShader(fs);

	loc_projection = glGetUniformLocation(program, "uProjection");
	loc_texture = glGetUniformLocation(program, "uTexture");
	loc_stipple = glGetUniformLocation(program, "uStipple");

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, STREAM_VBO_CAPACITY * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, STREAM_EBO_CAPACITY * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, r));

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	initFontAtlas();

	std::array<uint8_t, 4> white = { 255, 255, 255, 255 };
	glGenTextures(1, &whitePixelTexture);
	glBindTexture(GL_TEXTURE_2D, whitePixelTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	initialized = true;
}

void GLRenderer::shutdown() {
	current_texture = 0;
	std::erase(s_instances, this);
	if (!initialized) {
		return;
	}
	destroyFBO();
	if (program) {
		glDeleteProgram(program);
		program = 0;
	}
	if (vbo) {
		glDeleteBuffers(1, &vbo);
		vbo = 0;
	}
	if (ebo) {
		glDeleteBuffers(1, &ebo);
		ebo = 0;
	}
	if (vao) {
		glDeleteVertexArrays(1, &vao);
		vao = 0;
	}
	if (whitePixelTexture) {
		glDeleteTextures(1, &whitePixelTexture);
		whitePixelTexture = 0;
	}
	if (font.texture) {
		glDeleteTextures(1, &font.texture);
		font.texture = 0;
	}
	initialized = false;
}

void GLRenderer::setOrtho(float left, float right, float bottom, float top) {
	std::array<float, 16> m {};
	m[0] = 2.0f / (right - left);
	m[5] = 2.0f / (top - bottom);
	m[10] = -1.0f;
	m[12] = -(right + left) / (right - left);
	m[13] = -(top + bottom) / (top - bottom);
	m[15] = 1.0f;

	glUseProgram(program);
	glUniformMatrix4fv(loc_projection, 1, GL_FALSE, m.data());
	glUseProgram(0);
}

void GLRenderer::flushBatch() {
	if (batch.empty() || !initialized) {
		return;
	}

	size_t vertexCount = batch.size();
	size_t indexCount = indexBatch.size();
	size_t vertexBytes = vertexCount * sizeof(Vertex);
	size_t indexBytes = indexCount * sizeof(GLuint);

	glUseProgram(program);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

	if (vertexCount > STREAM_VBO_CAPACITY || indexCount > STREAM_EBO_CAPACITY) {
		batch.clear();
		indexBatch.clear();
		glBindVertexArray(0);
		glUseProgram(0);
		return;
	}

	if (vboOffset + vertexCount > STREAM_VBO_CAPACITY || eboOffset + indexCount > STREAM_EBO_CAPACITY) {
		glBufferData(GL_ARRAY_BUFFER, STREAM_VBO_CAPACITY * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, STREAM_EBO_CAPACITY * sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
		vboOffset = 0;
		eboOffset = 0;
	}

	void* vboPtr = glMapBufferRange(GL_ARRAY_BUFFER, vboOffset * sizeof(Vertex), vertexBytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
	if (!vboPtr) {
		batch.clear();
		indexBatch.clear();
		glBindVertexArray(0);
		glUseProgram(0);
		return;
	}
	std::memcpy(vboPtr, batch.data(), vertexBytes);
	glUnmapBuffer(GL_ARRAY_BUFFER);

	void* eboPtr = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, eboOffset * sizeof(GLuint), indexBytes, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
	if (!eboPtr) {
		batch.clear();
		indexBatch.clear();
		glBindVertexArray(0);
		glUseProgram(0);
		return;
	}
	std::memcpy(eboPtr, indexBatch.data(), indexBytes);
	glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, current_texture);
	glUniform1i(loc_texture, 0);

	glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)indexCount, GL_UNSIGNED_INT, (void*)(eboOffset * sizeof(GLuint)), (GLint)vboOffset);

	vboOffset += vertexCount;
	eboOffset += indexCount;

	glBindVertexArray(0);
	glUseProgram(0);
	batch.clear();
	indexBatch.clear();
}

void GLRenderer::drawTexturedQuad(float x, float y, float w, float h, GLuint textureId, const GLColor &color, float u0, float v0_, float u1, float v1_) {
	DrawCommand cmd;
	cmd.state.textureId = textureId;
	cmd.state.blendSrc = activeBlendSrc;
	cmd.state.blendDst = activeBlendDst;
	cmd.isQuadBatch = true;
	cmd.vertices = {
		{ x, y, u0, v0_, color.r, color.g, color.b, color.a },
		{ x + w, y, u1, v0_, color.r, color.g, color.b, color.a },
		{ x + w, y + h, u1, v1_, color.r, color.g, color.b, color.a },
		{ x, y + h, u0, v1_, color.r, color.g, color.b, color.a },
	};
	commandList.push_back(std::move(cmd));
}

void GLRenderer::drawColoredQuad(float x, float y, float w, float h, const GLColor &color) {
	DrawCommand cmd;
	cmd.state.textureId = whitePixelTexture;
	cmd.state.blendSrc = activeBlendSrc;
	cmd.state.blendDst = activeBlendDst;
	cmd.isQuadBatch = true;
	cmd.vertices = {
		{ x, y, 0, 0, color.r, color.g, color.b, color.a },
		{ x + w, y, 0, 0, color.r, color.g, color.b, color.a },
		{ x + w, y + h, 0, 0, color.r, color.g, color.b, color.a },
		{ x, y + h, 0, 0, color.r, color.g, color.b, color.a },
	};
	commandList.push_back(std::move(cmd));
}

void GLRenderer::drawThickLineSegment(float x1, float y1, float x2, float y2, float width, const GLColor &color) {
	float dx = x2 - x1;
	float dy = y2 - y1;
	float len = sqrtf(dx * dx + dy * dy);
	if (len < 1e-6f) {
		return;
	}
	float nx = (-dy / len) * (width * 0.5f);
	float ny = (dx / len) * (width * 0.5f);

	DrawCommand cmd;
	cmd.state.textureId = whitePixelTexture;
	cmd.state.blendSrc = activeBlendSrc;
	cmd.state.blendDst = activeBlendDst;
	cmd.isQuadBatch = true;
	cmd.vertices = {
		{ x1 + nx, y1 + ny, 0, 0, color.r, color.g, color.b, color.a },
		{ x1 - nx, y1 - ny, 0, 0, color.r, color.g, color.b, color.a },
		{ x2 - nx, y2 - ny, 0, 0, color.r, color.g, color.b, color.a },
		{ x2 + nx, y2 + ny, 0, 0, color.r, color.g, color.b, color.a },
	};
	commandList.push_back(std::move(cmd));
}

void GLRenderer::drawRect(float x, float y, float w, float h, const GLColor &color, float lineWidth) {
	drawThickLineSegment(x, y, x + w, y, lineWidth, color);
	drawThickLineSegment(x + w, y, x + w, y + h, lineWidth, color);
	drawThickLineSegment(x + w, y + h, x, y + h, lineWidth, color);
	drawThickLineSegment(x, y + h, x, y, lineWidth, color);
}

void GLRenderer::drawRoundedRect(float x, float y, float w, float h, float radius, const GLColor &fill) {
	const int segments = 8;

	float cx = x + w * 0.5f;
	float cy = y + h * 0.5f;
	Vertex center = { cx, cy, 0, 0, fill.r, fill.g, fill.b, fill.a };

	std::array<std::array<float, 2>, 4> corners = { {
		{ x + radius, y + radius },
		{ x + w - radius, y + radius },
		{ x + w - radius, y + h - radius },
		{ x + radius, y + h - radius },
	} };

	constexpr float pi = std::numbers::pi_v<float>;
	std::array<float, 4> startAngle = { pi, 1.5f * pi, 0.0f, 0.5f * pi };

	std::vector<Vertex> perimeter;
	for (int c = 0; c < 4; ++c) {
		for (int s = 0; s <= segments; ++s) {
			float angle = startAngle[c] + (s / static_cast<float>(segments)) * (pi * 0.5f);
			float px = corners[c][0] + cosf(angle) * radius;
			float py = corners[c][1] + sinf(angle) * radius;
			perimeter.push_back({ px, py, 0, 0, fill.r, fill.g, fill.b, fill.a });
		}
	}

	DrawCommand cmd;
	cmd.state.textureId = whitePixelTexture;
	cmd.state.blendSrc = activeBlendSrc;
	cmd.state.blendDst = activeBlendDst;
	cmd.isQuadBatch = false;
	for (size_t i = 0; i < perimeter.size(); ++i) {
		size_t next = (i + 1) % perimeter.size();
		cmd.vertices.push_back(center);
		cmd.vertices.push_back(perimeter[i]);
		cmd.vertices.push_back(perimeter[next]);
	}
	commandList.push_back(std::move(cmd));
}

void GLRenderer::drawRoundedRectOutline(float x, float y, float w, float h, float radius, const GLColor &color, float lineWidth) {
	const int segments = 8;

	std::array<std::array<float, 2>, 4> corners = { {
		{ x + radius, y + radius },
		{ x + w - radius, y + radius },
		{ x + w - radius, y + h - radius },
		{ x + radius, y + h - radius },
	} };

	constexpr float pi = std::numbers::pi_v<float>;
	std::array<float, 4> startAngle = { pi, 1.5f * pi, 0.0f, 0.5f * pi };

	std::vector<std::array<float, 2>> perimeter;
	for (int c = 0; c < 4; ++c) {
		for (int s = 0; s <= segments; ++s) {
			float angle = startAngle[c] + (s / static_cast<float>(segments)) * (pi * 0.5f);
			float px = corners[c][0] + cosf(angle) * radius;
			float py = corners[c][1] + sinf(angle) * radius;
			perimeter.push_back({ px, py });
		}
	}

	for (size_t i = 0; i < perimeter.size(); ++i) {
		size_t next = (i + 1) % perimeter.size();
		drawThickLineSegment(perimeter[i][0], perimeter[i][1], perimeter[next][0], perimeter[next][1], lineWidth, color);
	}
}

void GLRenderer::drawLine(float x1, float y1, float x2, float y2, const GLColor &color, float width) {
	drawThickLineSegment(x1, y1, x2, y2, width, color);
}

void GLRenderer::drawLines(const float* vertices, int pairCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float width) {
	GLColor c = { r, g, b, a };
	for (int i = 0; i < pairCount; ++i) {
		float x1 = vertices[i * 4];
		float y1 = vertices[i * 4 + 1];
		float x2 = vertices[i * 4 + 2];
		float y2 = vertices[i * 4 + 3];
		drawThickLineSegment(x1, y1, x2, y2, width, c);
	}
}

void GLRenderer::drawStippledLines(const float* vertices, int pairCount, const GLColor &color, float width, int factor, uint16_t pattern) {
	for (int i = 0; i < pairCount; ++i) {
		float x1 = vertices[i * 4];
		float y1 = vertices[i * 4 + 1];
		float x2 = vertices[i * 4 + 2];
		float y2 = vertices[i * 4 + 3];

		float dx = x2 - x1;
		float dy = y2 - y1;
		float len = sqrtf(dx * dx + dy * dy);
		if (len < 1e-6f) {
			continue;
		}

		float dirX = dx / len;
		float dirY = dy / len;
		auto step = static_cast<float>(factor);
		int bit = 0;
		float pos = 0.0f;

		while (pos < len) {
			float segEnd = pos + step;
			if (segEnd > len) {
				segEnd = len;
			}

			if (pattern & (1 << (bit & 15))) {
				float sx = x1 + dirX * pos;
				float sy = y1 + dirY * pos;
				float ex = x1 + dirX * segEnd;
				float ey = y1 + dirY * segEnd;
				drawThickLineSegment(sx, sy, ex, ey, width, color);
			}

			pos = segEnd;
			bit++;
		}
	}
}

void GLRenderer::drawPolygon(const float* vertices, int vertexCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (vertexCount < 3) {
		return;
	}
	DrawCommand cmd;
	cmd.state.textureId = whitePixelTexture;
	cmd.state.blendSrc = activeBlendSrc;
	cmd.state.blendDst = activeBlendDst;
	cmd.isQuadBatch = false;
	for (int i = 1; i < vertexCount - 1; ++i) {
		cmd.vertices.push_back({ vertices[0], vertices[1], 0, 0, r, g, b, a });
		cmd.vertices.push_back({ vertices[i * 2], vertices[i * 2 + 1], 0, 0, r, g, b, a });
		cmd.vertices.push_back({ vertices[(i + 1) * 2], vertices[(i + 1) * 2 + 1], 0, 0, r, g, b, a });
	}
	commandList.push_back(std::move(cmd));
}

void GLRenderer::drawTriangleFan(const float* vertices, int vertexCount, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	if (vertexCount < 3) {
		return;
	}
	DrawCommand cmd;
	cmd.state.textureId = whitePixelTexture;
	cmd.state.blendSrc = activeBlendSrc;
	cmd.state.blendDst = activeBlendDst;
	cmd.isQuadBatch = false;
	for (int i = 1; i < vertexCount - 1; ++i) {
		cmd.vertices.push_back({ vertices[0], vertices[1], 0, 0, r, g, b, a });
		cmd.vertices.push_back({ vertices[i * 2], vertices[i * 2 + 1], 0, 0, r, g, b, a });
		cmd.vertices.push_back({ vertices[(i + 1) * 2], vertices[(i + 1) * 2 + 1], 0, 0, r, g, b, a });
	}
	commandList.push_back(std::move(cmd));
}

void GLRenderer::drawText(float x, float y, const std::string &text, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	font.textColor = { r, g, b, a };
	setRasterPos(x, y);
	for (char c : text) {
		drawBitmapChar(c);
	}
}

float GLRenderer::getCharWidth(char c) {
	if (c < 32 || c > 127) {
		return 0.0f;
	}
	return font.advances[c - 32];
}

float GLRenderer::getLineHeight() const {
	return font.lineHeight;
}

float GLRenderer::getAscent() const {
	return font.ascent;
}

void GLRenderer::setRasterPos(float x, float y) {
	font.cursorX = x;
	font.cursorY = y;
}

void GLRenderer::drawBitmapChar(char c) {
	if (c < 32 || c > 127) {
		return;
	}
	int idx = c - 32;
	const auto &g = font.glyphs[idx];
	float qx = font.cursorX + g.xoff;
	float qy = font.cursorY + g.yoff;
	float qw = g.w;
	float qh = g.h;
	DrawCommand cmd;
	cmd.state.textureId = font.texture;
	cmd.state.blendSrc = activeBlendSrc;
	cmd.state.blendDst = activeBlendDst;
	cmd.isQuadBatch = true;
	cmd.vertices = {
		{ qx, qy, g.u0, g.v0, font.textColor.r, font.textColor.g, font.textColor.b, font.textColor.a },
		{ qx + qw, qy, g.u1, g.v0, font.textColor.r, font.textColor.g, font.textColor.b, font.textColor.a },
		{ qx + qw, qy + qh, g.u1, g.v1, font.textColor.r, font.textColor.g, font.textColor.b, font.textColor.a },
		{ qx, qy + qh, g.u0, g.v1, font.textColor.r, font.textColor.g, font.textColor.b, font.textColor.a },
	};
	commandList.push_back(std::move(cmd));
	font.cursorX += g.advance;
}

void GLRenderer::setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	font.textColor = { r, g, b, a };
}

void GLRenderer::mergeCommands() {
	if (commandList.size() <= 1) {
		return;
	}
	size_t write = 0;
	for (size_t read = 1; read < commandList.size(); ++read) {
		if (commandList[write].state == commandList[read].state && commandList[write].isQuadBatch == commandList[read].isQuadBatch) {
			auto &src = commandList[read].vertices;
			auto &dst = commandList[write].vertices;
			dst.insert(dst.end(), src.begin(), src.end());
		} else {
			++write;
			if (write != read) {
				commandList[write] = std::move(commandList[read]);
			}
		}
	}
	commandList.resize(write + 1);
}

void GLRenderer::flushCommands() {
	mergeCommands();

	unsigned int currentBlendSrc = 0;
	unsigned int currentBlendDst = 0;

	for (auto &cmd : commandList) {
		bool textureChanged = current_texture != cmd.state.textureId;
		bool blendChanged = cmd.state.blendSrc != currentBlendSrc || cmd.state.blendDst != currentBlendDst;

		if ((textureChanged || blendChanged) && !batch.empty()) {
			flushBatch();
		}

		if (blendChanged) {
			currentBlendSrc = cmd.state.blendSrc;
			currentBlendDst = cmd.state.blendDst;
			if (currentBlendSrc != 0) {
				glBlendFunc(currentBlendSrc, currentBlendDst);
			} else {
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			}
		}

		current_texture = cmd.state.textureId;
		if (cmd.isQuadBatch) {
			auto base = (GLuint)batch.size();
			batch.insert(batch.end(), cmd.vertices.begin(), cmd.vertices.end());
			for (size_t q = 0; q < cmd.vertices.size(); q += 4) {
				GLuint b = base + (GLuint)q;
				indexBatch.push_back(b);
				indexBatch.push_back(b + 1);
				indexBatch.push_back(b + 2);
				indexBatch.push_back(b);
				indexBatch.push_back(b + 2);
				indexBatch.push_back(b + 3);
			}
		} else {
			auto base = (GLuint)batch.size();
			batch.insert(batch.end(), cmd.vertices.begin(), cmd.vertices.end());
			for (GLuint i = 0; i < (GLuint)cmd.vertices.size(); ++i) {
				indexBatch.push_back(base + i);
			}
		}
	}
	commandList.clear();
	flushBatch();

	if (currentBlendSrc != 0) {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}

void GLRenderer::setBlendMode(unsigned int src, unsigned int dst) {
	activeBlendSrc = src;
	activeBlendDst = dst;
}

void GLRenderer::resetBlendMode() {
	activeBlendSrc = 0;
	activeBlendDst = 0;
}

void GLRenderer::flush() {
	flushCommands();
}

void GLRenderer::invalidateTexture(GLuint id) {
	for (auto* inst : s_instances) {
		if (inst->current_texture == id) {
			inst->current_texture = 0;
		}
	}
}

void GLRenderer::ensureFBO(int w, int h) {
	if (fboData.fbo != 0 && fboData.width == w && fboData.height == h) {
		return;
	}
	destroyFBO();

	glGenFramebuffers(1, &fboData.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fboData.fbo);

	glGenTextures(1, &fboData.texture);
	glBindTexture(GL_TEXTURE_2D, fboData.texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboData.texture, 0);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		spdlog::error("[GLRenderer::ensureFBO] Framebuffer incomplete");
		glDeleteTextures(1, &fboData.texture);
		glDeleteFramebuffers(1, &fboData.fbo);
		fboData.fbo = 0;
		fboData.texture = 0;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	fboData.width = w;
	fboData.height = h;
}

void GLRenderer::destroyFBO() {
	if (fboData.texture != 0) {
		glDeleteTextures(1, &fboData.texture);
		fboData.texture = 0;
	}
	if (fboData.fbo != 0) {
		glDeleteFramebuffers(1, &fboData.fbo);
		fboData.fbo = 0;
	}
	fboData.width = 0;
	fboData.height = 0;
}

void GLRenderer::beginFBO() {
	if (fboData.fbo != 0) {
		glBindFramebuffer(GL_FRAMEBUFFER, fboData.fbo);
	}
}

void GLRenderer::endFBO() {
	if (fboData.fbo != 0) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}

void GLRenderer::blitFBO(float w, float h) {
	if (fboData.fbo == 0) {
		return;
	}
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	drawTexturedQuad(0, 0, w, h, fboData.texture, { 255, 255, 255, 255 }, 0.f, 1.f, 1.f, 0.f);
	flush();
}
