#pragma once
#include <array>
#include <string>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  Block IDs — stored as uint16_t in chunk palette indices
// ─────────────────────────────────────────────────────────────────────────────
enum class BlockType : uint16_t {
    Air        = 0,
    Grass      = 1,
    Dirt       = 2,
    Stone      = 3,
    Sand       = 4,
    Water      = 5,
    Wood       = 6,
    Leaves     = 7,
    Snow       = 8,
    Ice        = 9,
    Gravel     = 10,
    Bedrock    = 11,
    COUNT
};

// ─────────────────────────────────────────────────────────────────────────────
//  Per-block properties (read-only, stored in a static registry)
// ─────────────────────────────────────────────────────────────────────────────
struct BlockDef {
    std::string name;
    bool        solid        = true;
    bool        transparent  = false;
    bool        liquid       = false;
    float       hardness     = 1.0f;
    // Atlas UV tile coordinates (col, row) for each face: +X -X +Y -Y +Z -Z
    // Each face indexes into a 16x16 texture atlas (256 tiles)
    std::array<uint8_t, 6> faceTextures = {0,0,0,0,0,0};
};

// ─────────────────────────────────────────────────────────────────────────────
//  BlockRegistry — compile-time initialised, global access via instance()
// ─────────────────────────────────────────────────────────────────────────────
class BlockRegistry {
public:
    static const BlockRegistry& instance();

    [[nodiscard]] const BlockDef& get(BlockType t) const noexcept {
        return defs_[static_cast<uint16_t>(t)];
    }
    [[nodiscard]] bool isSolid(BlockType t) const noexcept {
        return get(t).solid;
    }
    [[nodiscard]] bool isTransparent(BlockType t) const noexcept {
        return !get(t).solid || get(t).transparent;
    }
    [[nodiscard]] bool isLiquid(BlockType t) const noexcept {
        return get(t).liquid;
    }

private:
    BlockRegistry();
    static constexpr size_t kCount = static_cast<size_t>(BlockType::COUNT);
    std::array<BlockDef, kCount> defs_;
};
