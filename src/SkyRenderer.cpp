#include "SkyRenderer.h"
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

SkyRenderer::SkyRenderer() = default;
SkyRenderer::~SkyRenderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

void SkyRenderer::init() {
    shader_ = std::make_unique<Shader>("shaders/sky.vert", "shaders/sky.frag");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // reserve small dynamic buffer (6 verts per quad × (3+2) floats)
    glBufferData(GL_ARRAY_BUFFER, 6 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // layout: vec3 position, vec2 uv
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));

    glBindVertexArray(0);
}

void SkyRenderer::render(const Camera& camera,
                         const glm::vec3& playerPos,
                         const glm::vec3& sunDir,
                         const glm::vec3& moonDir,
                         const glm::mat4& vp)
{
    if (!shader_) return;

    // Common parameters
    const float sunDistance = 400.0f;
    const float sunSize = 36.0f;
    const float moonDistance = 380.0f;
    const float moonSize = 20.0f;

    glm::vec3 right = camera.right();
    glm::vec3 up = camera.up();

    shader_->use();
    shader_->setMat4("u_VP", vp);

    // Disable depth/write so sky quads don't occlude anything
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // --- Sun quad ---
    glm::vec3 centerSun = playerPos + sunDir * sunDistance;
    glm::vec3 rS = right * sunSize;
    glm::vec3 uS = up * sunSize;

    std::vector<float> verts;
    verts.reserve(6 * 5);

    auto pushQuad = [&](const glm::vec3& c, const glm::vec3& r, const glm::vec3& u) {
        glm::vec3 p0 = c - r - u;
        glm::vec3 p1 = c - r + u;
        glm::vec3 p2 = c + r + u;
        glm::vec3 p3 = c + r - u;
        // triangle 0: p0, p1, p2
        // triangle 1: p2, p3, p0
        auto push = [&](const glm::vec3& p, float ux, float uy) {
            verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
            verts.push_back(ux);  verts.push_back(uy);
        };
        push(p0, 0.0f, 0.0f);
        push(p1, 0.0f, 1.0f);
        push(p2, 1.0f, 1.0f);
        push(p2, 1.0f, 1.0f);
        push(p3, 1.0f, 0.0f);
        push(p0, 0.0f, 0.0f);
    };

    pushQuad(centerSun, rS, uS);
    // upload sun vertices
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    shader_->setVec3("u_Color", glm::vec3(1.0f, 0.95f, 0.7f));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 5));

    // --- Moon quad ---
    verts.clear();
    glm::vec3 centerMoon = playerPos + moonDir * moonDistance;
    glm::vec3 rM = right * moonSize;
    glm::vec3 uM = up * moonSize;
    pushQuad(centerMoon, rM, uM);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    shader_->setVec3("u_Color", glm::vec3(0.85f, 0.9f, 1.0f) * 0.9f);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 5));

    glBindVertexArray(0);

    // Restore depth state
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    shader_->unuse();
}
