#include "Texture.h"
#include <stdexcept>
#include <string>

// ── Single-header image loader ────────────────────────────────────────────────
// Place stb_image.h in /include or /vendor.
// #define STB_IMAGE_IMPLEMENTATION in exactly one .cpp
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"  // https://github.com/nothings/stb

Texture::Texture(const std::string& path) {
    stbi_set_flip_vertically_on_load(true);
    int channels;
    unsigned char* data = stbi_load(path.c_str(), &w_, &h_, &channels, STBI_rgb_alpha);
    if (!data)
        throw std::runtime_error("Failed to load texture: " + path);

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w_, h_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    // Nearest-neighbour for pixel art atlas
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
}

Texture::~Texture() {
    glDeleteTextures(1, &id_);
}

void Texture::bind(int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, id_);
}
