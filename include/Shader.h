#pragma once
#include <string>
#include <GL/glew.h>
#include <glm/glm.hpp>

class Shader {
public:
    Shader() = default;
    Shader(const std::string& vertPath, const std::string& fragPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& o) noexcept : id_(o.id_) { o.id_ = 0; }
    Shader& operator=(Shader&& o) noexcept {
        if (this != &o) { glDeleteProgram(id_); id_ = o.id_; o.id_ = 0; }
        return *this;
    }

    void use() const { glUseProgram(id_); }
    void unuse() const { glUseProgram(0); }

    void setInt  (const char* name, int v)               const;
    void setFloat(const char* name, float v)             const;
    void setVec3 (const char* name, const glm::vec3& v)  const;
    void setVec4 (const char* name, const glm::vec4& v)  const;
    void setMat4 (const char* name, const glm::mat4& m)  const;
    void setMat3 (const char* name, const glm::mat3& m)  const;

    GLuint id() const noexcept { return id_; }

private:
    GLuint compile(GLenum type, const std::string& src);
    std::string loadFile(const std::string& path);
    GLuint id_ = 0;
};
