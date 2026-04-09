#include "Shader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

Shader::Shader(const std::string& vertPath, const std::string& fragPath) {
    GLuint vert = compile(GL_VERTEX_SHADER,   loadFile(vertPath));
    GLuint frag = compile(GL_FRAGMENT_SHADER, loadFile(fragPath));

    id_ = glCreateProgram();
    glAttachShader(id_, vert);
    glAttachShader(id_, frag);
    glLinkProgram(id_);

    GLint ok = 0;
    glGetProgramiv(id_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(id_, sizeof(log), nullptr, log);
        glDeleteProgram(id_); id_ = 0;
        throw std::runtime_error(std::string("Shader link error:\n") + log);
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
}

Shader::~Shader() { glDeleteProgram(id_); }

GLuint Shader::compile(GLenum type, const std::string& src) {
    GLuint sh = glCreateShader(type);
    const char* ptr = src.c_str();
    glShaderSource(sh, 1, &ptr, nullptr);
    glCompileShader(sh);

    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        glDeleteShader(sh);
        throw std::runtime_error(std::string("Shader compile error:\n") + log);
    }
    return sh;
}

std::string Shader::loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open shader: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void Shader::setInt  (const char* n, int v)              const { glUniform1i (glGetUniformLocation(id_,n), v); }
void Shader::setFloat(const char* n, float v)            const { glUniform1f (glGetUniformLocation(id_,n), v); }
void Shader::setVec3 (const char* n, const glm::vec3& v) const { glUniform3fv(glGetUniformLocation(id_,n), 1, glm::value_ptr(v)); }
void Shader::setVec4 (const char* n, const glm::vec4& v) const { glUniform4fv(glGetUniformLocation(id_,n), 1, glm::value_ptr(v)); }
void Shader::setMat4 (const char* n, const glm::mat4& m) const { glUniformMatrix4fv(glGetUniformLocation(id_,n), 1, GL_FALSE, glm::value_ptr(m)); }
void Shader::setMat3 (const char* n, const glm::mat3& m) const { glUniformMatrix3fv(glGetUniformLocation(id_,n), 1, GL_FALSE, glm::value_ptr(m)); }
