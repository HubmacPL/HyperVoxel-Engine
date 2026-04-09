#include "Renderer.h"
#include "Shader.h"
#include "Camera.h"
#include "World.h"
#include "Texture.h"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
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
}

void Renderer::renderWorld(const World& world, const Camera& camera) {
    if (!initialised_) return;

    const float aspect = 16.f / 9.f;
    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 proj = camera.projMatrix(aspect, 70.f, 0.05f, 800.f);
    const glm::mat4 vp   = proj * view;

    Frustum frustum;
    frustum.update(vp);

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

    chunkShader_->setInt  ("u_Atlas",    0);
    chunkShader_->setMat4 ("u_MVP",      vp);
    chunkShader_->setVec3 ("u_SunDir",   glm::normalize(glm::vec3(0.6f, 1.f, 0.3f)));
    chunkShader_->setVec3 ("u_SkyColor", glm::vec3(0.53f, 0.81f, 0.98f));
    chunkShader_->setVec3 ("u_SunColor", glm::vec3(1.0f, 0.95f, 0.8f));
    chunkShader_->setFloat("u_Ambient",  0.35f);
    chunkShader_->setFloat("u_DayNight", 1.0f);
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
        glDrawElements(GL_TRIANGLES,
                       static_cast<GLsizei>(mesh.indices.size()),
                       GL_UNSIGNED_INT, nullptr);

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
