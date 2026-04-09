#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <memory>
#include "World.h"
#include "Shader.h"
#include "Camera.h"
#include "GuiRenderer.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer — manages all draw calls for the world
//
//  Optimisation strategy:
//  • One VAO+VBO per chunk (opaque pass), one for transparent pass
//  • Frustum culling: skip chunks whose AABB is outside view frustum → 
//    reduces draw calls from O(total) to O(visible) ≈ 30-50% fewer calls
//  • Sort opaque chunks front-to-back (early Z rejection by GPU)
//  • Sort transparent chunks back-to-front (correct alpha blending)
//  • All state changes batched: bind shader once, iterate, unbind
// ─────────────────────────────────────────────────────────────────────────────

struct Frustum {
    // 6 planes: {normal.xyz, distance}
    glm::vec4 planes[6];
    void update(const glm::mat4& vp) noexcept;
    bool intersectsAABB(const glm::vec3& min, const glm::vec3& max) const noexcept;
};

class SkyRenderer;

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Call once after GL context is available
    void init();

    // Render all uploaded chunks
    // dt: seconds since last frame (used for day/night cycle)
    // viewportW/H: current framebuffer size (used for aspect ratio)
    void renderWorld(const World& world, const Camera& camera, float dt,
                     int viewportW, int viewportH);

    // Debug HUD overlay
    void renderDebugInfo(const Camera& camera, int fps, int visibleChunks,
                         int totalChunks, int drawCalls);

    void setWireframe(bool on) noexcept { wireframe_ = on; }
    bool wireframe() const noexcept { return wireframe_; }

private:
    void bindAtlas(int slot = 0);
    void renderOpaquePass(const World& world, const Camera& camera,
                          const Frustum& frustum, const glm::mat4& vp,
                          int& drawCalls, int& visibleChunks);
    void renderTransparentPass(const World& world, const Camera& camera,
                                const Frustum& frustum, const glm::mat4& vp,
                                int& drawCalls);

    std::unique_ptr<Shader>  chunkShader_;
    std::unique_ptr<Shader>  waterShader_;
    std::unique_ptr<SkyRenderer> skyRenderer_;
    std::unique_ptr<GuiRenderer> guiRenderer_;

    // Day/night state (computed once per frame)
    float dayTime_ = 0.0f;
    glm::vec3 sunDir_{0.6f, 1.0f, 0.3f};
    glm::vec3 moonDir_{-0.6f, -1.0f, -0.3f};
    glm::vec3 skyColor_{0.53f, 0.81f, 0.98f};

    GLuint atlasTexture_ = 0;
    bool   wireframe_    = false;
    bool   initialised_  = false;
public:
    // Access to GUI renderer so callers can submit draw commands between begin()/end()
    GuiRenderer* gui() { return guiRenderer_.get(); }
};
