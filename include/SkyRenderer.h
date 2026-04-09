#pragma once
#include <memory>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "Shader.h"
#include "Camera.h"

class SkyRenderer {
public:
    SkyRenderer();
    ~SkyRenderer();

    void init();
    void render(const Camera& camera,
                const glm::vec3& playerPos,
                const glm::vec3& sunDir,
                const glm::vec3& moonDir,
                const glm::mat4& vp);

private:
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};
