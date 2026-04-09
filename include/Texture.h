#pragma once
#include <string>
#include <GL/glew.h>

class Texture {
public:
    explicit Texture(const std::string& path);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void bind(int slot = 0) const;
    GLuint id() const noexcept { return id_; }
    // Transfer ownership of GL texture handle (caller is responsible for glDeleteTextures)
    GLuint release() noexcept { GLuint tmp = id_; id_ = 0; return tmp; }
    int width()  const noexcept { return w_; }
    int height() const noexcept { return h_; }

private:
    GLuint id_ = 0;
    int w_ = 0, h_ = 0;
};
