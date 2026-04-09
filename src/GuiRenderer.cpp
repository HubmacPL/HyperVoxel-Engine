#include "GuiRenderer.h"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

GuiRenderer::GuiRenderer() = default;

GuiRenderer::~GuiRenderer() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (whiteTex_) glDeleteTextures(1, &whiteTex_);
}

void GuiRenderer::init() {
    shader_ = std::make_unique<Shader>("shaders/gui.vert", "shaders/gui.frag");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    // attribute layout: 0=pos(vec2), 1=uv(vec2), 2=color(vec4)
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(GuiVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(GuiVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(GuiVertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(GuiVertex, color)));

    glBindVertexArray(0);

    // Create 1x1 white texture fallback
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    unsigned char white[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Try to load optional bitmap font (font.png). Non-fatal.
    try {
        fontTexOwner_ = std::make_unique<Texture>("font.png");
        fontTex_ = fontTexOwner_.get();
        if (fontTex_) {
            glyphW_ = fontTex_->width() / fontCols_;
            glyphH_ = fontTex_->height() / fontRows_;
        }
    } catch (const std::exception&) {
        // no font available; fallback to white rectangles
        fontTexOwner_.reset();
        fontTex_ = nullptr;
    }
}

void GuiRenderer::begin() {
    vertices_.clear();
    indices_.clear();
    batches_.clear();
    currentTexture_ = whiteTex_;
    currentIndexStart_ = 0;
    begun_ = true;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GuiRenderer::flushIfTextureChange(GLuint tex) {
    if (tex == currentTexture_) return;
    // Close previous batch
    uint32_t count = static_cast<uint32_t>(indices_.size()) - currentIndexStart_;
    if (count > 0) {
        batches_.push_back({ currentTexture_, currentIndexStart_, count });
    }
    currentTexture_ = tex;
    currentIndexStart_ = static_cast<uint32_t>(indices_.size());
}

void GuiRenderer::pushQuad(GLuint tex, float x, float y, float w, float h,
                          float u0, float v0, float u1, float v1, const glm::vec4& color)
{
    // Ensure batch
    flushIfTextureChange(tex);

    uint32_t base = static_cast<uint32_t>(vertices_.size());
    GuiVertex vert0{ { x,     y },     { u0, v0 }, color };
    GuiVertex vert1{ { x,     y + h }, { u0, v1 }, color };
    GuiVertex vert2{ { x + w, y + h }, { u1, v1 }, color };
    GuiVertex vert3{ { x + w, y     }, { u1, v0 }, color };
    vertices_.push_back(vert0);
    vertices_.push_back(vert1);
    vertices_.push_back(vert2);
    vertices_.push_back(vert3);

    // Two triangles: 0,1,2 and 0,2,3
    indices_.push_back(base + 0);
    indices_.push_back(base + 1);
    indices_.push_back(base + 2);
    indices_.push_back(base + 0);
    indices_.push_back(base + 2);
    indices_.push_back(base + 3);
}

void GuiRenderer::end() {
    if (!begun_) return;
    // Close final batch
    uint32_t count = static_cast<uint32_t>(indices_.size()) - currentIndexStart_;
    if (count > 0) {
        batches_.push_back({ currentTexture_, currentIndexStart_, count });
    }

    // Upload once
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (!vertices_.empty()) {
        glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(GuiVertex), vertices_.data(), GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    if (!indices_.empty()) {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(uint32_t), indices_.data(), GL_DYNAMIC_DRAW);
    }

    // Build projection from current viewport (pixel coords, origin top-left)
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    float vw = static_cast<float>(vp[2]);
    float vh = static_cast<float>(vp[3]);
    glm::mat4 proj = glm::ortho(0.0f, vw, vh, 0.0f);

    shader_->use();
    shader_->setMat4("u_ProjMatrix", proj);
    shader_->setInt("u_Texture", 0);

    // Single vertex/index upload, then multiple draw calls per texture
    for (const auto& b : batches_) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b.texture);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(b.indexCount), GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<uintptr_t>(b.indexStart * sizeof(uint32_t))));
    }

    shader_->unuse();
    glBindVertexArray(0);

    // Restore simple state
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    // Clear buffers for next frame
    vertices_.clear();
    indices_.clear();
    batches_.clear();
    begun_ = false;
}

void GuiRenderer::drawRect(float x, float y, float w, float h, const glm::vec4& color) {
    if (!begun_) return;
    pushQuad(whiteTex_, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void GuiRenderer::drawTexture(Texture* tex, float x, float y, float w, float h, const glm::vec4& tint) {
    if (!begun_) return;
    GLuint tid = tex ? tex->id() : whiteTex_;
    pushQuad(tid, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

void GuiRenderer::drawNineSlice(Texture* tex, float x, float y, float w, float h, int border, const glm::vec4& tint) {
    if (!begun_) return;
    if (!tex) { drawRect(x,y,w,h,tint); return; }
    GLuint tid = tex->id();
    float tw = static_cast<float>(tex->width());
    float th = static_cast<float>(tex->height());

    float b = static_cast<float>(border);
    float left = std::min(b, w/2.0f);
    float right = left;
    float top = std::min(b, h/2.0f);
    float bottom = top;

    float ux0 = 0.0f;
    float ux1 = left / tw;
    float ux2 = 1.0f - right / tw;
    float ux3 = 1.0f;

    float uy0 = 0.0f;
    float uy1 = top / th;
    float uy2 = 1.0f - bottom / th;
    float uy3 = 1.0f;

    float x0 = x;
    float x1 = x + left;
    float x2 = x + w - right;
    float x3 = x + w;

    float y0 = y;
    float y1 = y + top;
    float y2 = y + h - bottom;
    float y3 = y + h;

    // 3x3 quads
    pushQuad(tid, x0, y0, left, top, ux0, uy0, ux1, uy1, tint); // TL
    pushQuad(tid, x1, y0, x2 - x1, top, ux1, uy0, ux2, uy1, tint); // T
    pushQuad(tid, x2, y0, right, top, ux2, uy0, ux3, uy1, tint); // TR

    pushQuad(tid, x0, y1, left, y2 - y1, ux0, uy1, ux1, uy2, tint); // L
    pushQuad(tid, x1, y1, x2 - x1, y2 - y1, ux1, uy1, ux2, uy2, tint); // C
    pushQuad(tid, x2, y1, right, y2 - y1, ux2, uy1, ux3, uy2, tint); // R

    pushQuad(tid, x0, y2, left, bottom, ux0, uy2, ux1, uy3, tint); // BL
    pushQuad(tid, x1, y2, x2 - x1, bottom, ux1, uy2, ux2, uy3, tint); // B
    pushQuad(tid, x2, y2, right, bottom, ux2, uy2, ux3, uy3, tint); // BR
}

void GuiRenderer::drawText(const std::string& text, float x, float y, float scale, const glm::vec4& color) {
    if (!begun_) return;
    if (!fontTex_) {
        // fallback: draw simple rectangles for each char
        float adv = 8.0f * scale;
        for (char c : text) {
            drawRect(x, y, adv, 8.0f*scale, color);
            x += adv;
        }
        return;
    }

    GLuint tid = fontTex_->id();
    float tw = static_cast<float>(fontTex_->width());
    float th = static_cast<float>(fontTex_->height());
    float uStep = 1.0f / static_cast<float>(fontCols_);
    float vStep = 1.0f / static_cast<float>(fontRows_);
    float px = x;
    float py = y;
    float gw = static_cast<float>(glyphW_) * scale;
    float gh = static_cast<float>(glyphH_) * scale;

    for (unsigned char uc : std::vector<unsigned char>(text.begin(), text.end())) {
        int code = static_cast<int>(uc);
        int tx = code % fontCols_;
        int ty = code / fontCols_;
        // Invert row to match stb_image vertical flip used elsewhere
        ty = (fontRows_ - 1) - ty;
        float u0 = tx * uStep;
        float v0 = ty * vStep;
        float u1 = u0 + uStep;
        float v1 = v0 + vStep;
        pushQuad(tid, px, py, gw, gh, u0, v0, u1, v1, color);
        px += gw;
    }
}
