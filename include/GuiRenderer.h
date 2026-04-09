#pragma once
#include <vector>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Texture.h"

class GuiRenderer {
public:
    GuiRenderer();
    ~GuiRenderer();

    // Initialise GL objects and shaders (call after GL context is ready)
    void init();

    // Begin a new GUI frame: clears internal buffers and sets GL state
    void begin();

    // End frame: upload batched vertex/index buffers and draw
    void end();

    // API
    void drawRect(float x, float y, float w, float h, const glm::vec4& color);
    void drawTexture(Texture* tex, float x, float y, float w, float h, const glm::vec4& tint = glm::vec4(1.0f));
    void drawText(const std::string& text, float x, float y, float scale, const glm::vec4& color);
    void drawNineSlice(Texture* tex, float x, float y, float w, float h, int border, const glm::vec4& tint = glm::vec4(1.0f));

private:
    struct GuiVertex {
        glm::vec2 pos;
        glm::vec2 uv;
        glm::vec4 color;
    };

    std::vector<GuiVertex> vertices_;
    std::vector<uint32_t> indices_;

    struct Batch { GLuint texture; uint32_t indexStart; uint32_t indexCount; };
    std::vector<Batch> batches_;

    // GL objects
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;

    std::unique_ptr<Shader> shader_;

    // Fallback 1x1 white texture (GL handle)
    GLuint whiteTex_ = 0;

    // Simple bitmap font (optional owner)
    std::unique_ptr<Texture> fontTexOwner_;
    Texture* fontTex_ = nullptr;
    int fontCols_ = 16;
    int fontRows_ = 16;
    int glyphW_ = 8;
    int glyphH_ = 8;

    // current batching state
    GLuint currentTexture_ = 0;
    uint32_t currentIndexStart_ = 0;
    bool begun_ = false;

    void flushIfTextureChange(GLuint tex);
    void pushQuad(GLuint tex, float x, float y, float w, float h,
                  float u0, float v0, float u1, float v1, const glm::vec4& color);
};
