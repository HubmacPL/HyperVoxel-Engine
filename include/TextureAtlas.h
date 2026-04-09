#pragma once
#include "Texture.h"
#include <memory>

// TextureAtlas wraps a single 256x256 PNG (16x16 tiles of 16px each)
// and provides uv helpers.
class TextureAtlas {
public:
    explicit TextureAtlas(const std::string& path)
        : texture_(std::make_unique<Texture>(path)) {}

    void bind(int slot = 0) const { texture_->bind(slot); }

    // Returns UV origin (bottom-left) of tile (col,row) in [0,1] atlas space
    static constexpr float tileSize() { return 1.f / 16.f; }

private:
    std::unique_ptr<Texture> texture_;
};
