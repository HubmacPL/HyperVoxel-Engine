#include "Renderer.h"
#include "Shader.h"
#include "Camera.h"
#include "World.h"
#include "Texture.h"
#include "SkyRenderer.h"
#include "GuiRenderer.h"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <vector>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
//  Frustum
// ─────────────────────────────────────────────────────────────────────────────
void Frustum::update(const glm::mat4& vp) noexcept {
    // Hartmann/Gribbs method — extract planes from combined VP matrix
    const float* m = &vp[0][0];

    auto makePlane = [](float a, float b, float c, float d) -> glm::vec4 {
        float len = std::sqrt(a*a + b*b + c*c);
        return {a/len, b/len, c/len, d/len};
    };

    planes[0] = makePlane(m[3]-m[0], m[7]-m[4], m[11]-m[8],  m[15]-m[12]); // right
    planes[1] = makePlane(m[3]+m[0], m[7]+m[4], m[11]+m[8],  m[15]+m[12]); // left
    planes[2] = makePlane(m[3]-m[1], m[7]-m[5], m[11]-m[9],  m[15]-m[13]); // top
    planes[3] = makePlane(m[3]+m[1], m[7]+m[5], m[11]+m[9],  m[15]+m[13]); // bottom
    planes[4] = makePlane(m[3]-m[2], m[7]-m[6], m[11]-m[10], m[15]-m[14]); // far
    planes[5] = makePlane(m[3]+m[2], m[7]+m[6], m[11]+m[10], m[15]+m[14]); // near
}

bool Frustum::intersectsAABB(const glm::vec3& mn, const glm::vec3& mx) const noexcept {
    for (const auto& plane : planes) {
        // Find the positive vertex (furthest along the plane normal)
        glm::vec3 pv{
            plane.x > 0 ? mx.x : mn.x,
            plane.y > 0 ? mx.y : mn.y,
            plane.z > 0 ? mx.z : mn.z
        };
        if (glm::dot(glm::vec3(plane), pv) + plane.w < 0)
            return false;  // outside this plane → cull
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Renderer
// ─────────────────────────────────────────────────────────────────────────────
Renderer::Renderer() = default;
Renderer::~Renderer() {
    if (atlasTexture_) glDeleteTextures(1, &atlasTexture_);
}

void Renderer::init() {
    chunkShader_ = std::make_unique<Shader>("shaders/chunk.vert", "shaders/chunk.frag");
    waterShader_ = std::make_unique<Shader>("shaders/water.vert", "shaders/water.frag");

    // Load texture atlas — expects terrain.png next to the .exe
    // Format: 256x256 PNG, 16x16 grid of 16px tiles
    try {
        Texture atlas("terrain.png");
        atlasTexture_ = atlas.id();
        atlas.release(); // transfer ownership to atlasTexture_ (raw GL handle)
    } catch (const std::exception& e) {
        // Fallback: 1×1 white texture so the game still runs without a PNG
        std::cerr << "[Renderer] " << e.what() << " — using white placeholder\n";
        glGenTextures(1, &atlasTexture_);
        glBindTexture(GL_TEXTURE_2D, atlasTexture_);
        uint32_t white = 0xFFFFFFFF;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    initialised_ = true;
    // Start dayTime so the sun begins roughly in front of the camera
    dayTime_ = 0.0f; // corresponds to dayLength*0.5 where dayLength ~= 120s
    // Sky renderer for sun/moon quads
    skyRenderer_ = std::make_unique<SkyRenderer>();
    skyRenderer_->init();

    // GUI renderer (overlay)
    guiRenderer_ = std::make_unique<GuiRenderer>();
    guiRenderer_->init();
}

void Renderer::renderWorld(const World& world, const Camera& camera, float dt,
                           int viewportW, int viewportH) {
    if (!initialised_) return;

    const float aspect = (viewportH > 0)
        ? static_cast<float>(viewportW) / static_cast<float>(viewportH)
        : 16.f / 9.f;
    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 proj = camera.projMatrix(aspect, 70.f, 0.05f, 800.f);
    const glm::mat4 vp   = proj * view;

    Frustum frustum;
    frustum.update(vp);

    // --- Day/night cycle (advance once per frame) ---
    const float dayLength = 120.0f; // seconds per full day
    dayTime_ = fmod(dayTime_ + dt, dayLength);  // keep value small to preserve float precision
    float phase = dayTime_ / dayLength; // 0..1
    float theta = phase * glm::pi<float>() * 2.0f;
    // Sun moves in a vertical arc across X (east-west) and Y (elevation).
    // Keep a small negative Z bias so the sun is generally in front of the camera.
    sunDir_ = glm::normalize(glm::vec3(cos(theta), sin(theta), -0.05f));
    moonDir_ = -sunDir_;

    // Day/night factor [0..1] from sun height. Use smoothstep over a narrow
    // window around the horizon so dusk and dawn last meaningfully (gives the
    // shader's `sunAbove` term something to interpolate against) instead of
    // the old linear sunY*0.5+0.5 which made midnight just a dim version of
    // noon.
    float dayNight = glm::smoothstep(-0.15f, 0.25f, sunDir_.y);

    // Sky colour blending: night <-> dusk <-> day
    const glm::vec3 dayColor(0.53f, 0.81f, 0.98f);
    const glm::vec3 duskColor(1.0f, 0.45f, 0.5f);
    const glm::vec3 nightColor(0.02f, 0.03f, 0.12f);
    if (dayNight > 0.5f) {
        float u = (dayNight - 0.5f) * 2.0f;
        skyColor_ = glm::mix(duskColor, dayColor, u);
    } else {
        float u = dayNight * 2.0f;
        skyColor_ = glm::mix(nightColor, duskColor, u);
    }

    // Clear to dynamic sky colour
    glClearColor(skyColor_.r, skyColor_.g, skyColor_.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Render sun/moon quads first (no depth write/test inside). Use alpha
    // blending so we can render round discs, and disable face culling so
    // billboards aren't accidentally backface-culled.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    if (skyRenderer_) skyRenderer_->render(camera, camera.position(), sunDir_, moonDir_, vp);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    int drawCalls = 0, visibleChunks = 0;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    renderOpaquePass(world, camera, frustum, vp, drawCalls, visibleChunks);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    renderTransparentPass(world, camera, frustum, vp, drawCalls);

    // Draw GUI overlay (flush any GUI draw calls previously recorded)
    if (guiRenderer_) guiRenderer_->end();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void Renderer::renderOpaquePass(const World& world,
                                 const Camera& camera,
                                 const Frustum& frustum,
                                 const glm::mat4& vp,
                                 int& drawCalls, int& visibleChunks)
{
    chunkShader_->use();

    // Warm sun tint at dawn/dusk, neutral-warm at noon. Drives the orange
    // glow on west-facing cliffs as the sun drops.
    float warm = 1.0f - glm::smoothstep(0.05f, 0.35f, sunDir_.y);
    glm::vec3 sunColor = glm::mix(
        glm::vec3(1.00f, 0.95f, 0.80f),  // noon — slightly warm white
        glm::vec3(1.00f, 0.55f, 0.30f),  // horizon — saturated orange
        warm);
    float dayNight = glm::smoothstep(-0.15f, 0.25f, sunDir_.y);

    chunkShader_->setInt  ("u_Atlas",    0);
    chunkShader_->setMat4 ("u_MVP",      vp);
    chunkShader_->setVec3 ("u_SunDir",   glm::normalize(sunDir_));
    chunkShader_->setVec3 ("u_SkyColor", skyColor_);
    chunkShader_->setVec3 ("u_SunColor", sunColor);
    chunkShader_->setFloat("u_DayNight", dayNight);
    chunkShader_->setFloat("u_FogStart", static_cast<float>(world.renderDistance()-2) * CHUNK_W);
    chunkShader_->setFloat("u_FogEnd",   static_cast<float>(world.renderDistance())   * CHUNK_W);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTexture_);

    const glm::vec3 camPos = camera.position();
    struct DrawCmd { float dist; const Chunk* chunk; };
    std::vector<DrawCmd> visible;
    visible.reserve(512);

    for (const auto& [cp, chunkPtr] : world.chunks()) {
        // Render any chunk that currently has a valid uploaded GPU mesh.
        // This avoids a visual "blink" where the chunk state is set to
        // Meshing/NeedsMeshing and the old mesh is hidden while a new
        // mesh is being generated in the background.
        const ChunkMesh& mesh = chunkPtr->mesh();
        if (!mesh.uploaded || mesh.vao == 0) continue;

        glm::vec3 origin = chunkPtr->worldOrigin();
        glm::vec3 mn = origin;
        glm::vec3 mx = origin + glm::vec3(CHUNK_W, CHUNK_H, CHUNK_D);

        if (!frustum.intersectsAABB(mn, mx)) continue;

        float dx = origin.x + CHUNK_W/2.f - camPos.x;
        float dz = origin.z + CHUNK_D/2.f - camPos.z;
        visible.push_back({ dx*dx + dz*dz, chunkPtr.get() });
    }

    // Front-to-back sort (minimises fragment overdraw)
    std::sort(visible.begin(), visible.end(),
              [](const DrawCmd& a, const DrawCmd& b){ return a.dist < b.dist; });

    for (const auto& cmd : visible) {
        const Chunk* chunk = cmd.chunk;
        const ChunkMesh& mesh = chunk->mesh();

        // Per-chunk uniform: model matrix is just a translation
        chunkShader_->setVec3("u_ChunkOrigin", chunk->worldOrigin());

        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);

        ++drawCalls;
        ++visibleChunks;
    }

    glBindVertexArray(0);
    chunkShader_->unuse();
}

void Renderer::renderTransparentPass(const World& world,
                                      const Camera& /*camera*/,
                                      const Frustum& frustum,
                                      const glm::mat4& /*vp*/,
                                      int& drawCalls)
{
    // Water rendering — same mesh VAO, different shader + blending
    // (In a full impl, water faces would be in a separate mesh buffer)
    // Placeholder: skipped for brevity
    (void)world; (void)frustum; (void)drawCalls;
}
