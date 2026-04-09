#include "Chunk.h"
#include "BlockRegistry.h"
#include <GL/glew.h>
#include <algorithm>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
//  ChunkPalette implementation
// ─────────────────────────────────────────────────────────────────────────────
void ChunkPalette::build(const std::array<BlockType, CHUNK_VOL>& raw) {
    palette.clear();
    data8.clear();
    data16.clear();

    // Collect unique block types
    std::array<bool, 65536> seen{};
    for (auto bt : raw) {
        uint16_t id = static_cast<uint16_t>(bt);
        if (!seen[id]) {
            seen[id] = true;
            palette.push_back(bt);
        }
    }

    if (palette.size() <= 256) {
        bitsPerBlock = 8;
        data8.resize(CHUNK_VOL);
        // Build reverse lookup
        std::array<uint8_t, 65536> lookup{};
        for (size_t i = 0; i < palette.size(); ++i)
            lookup[static_cast<uint16_t>(palette[i])] = static_cast<uint8_t>(i);
        for (int i = 0; i < CHUNK_VOL; ++i)
            data8[i] = lookup[static_cast<uint16_t>(raw[i])];
    } else {
        // Fallback: direct 16-bit storage
        bitsPerBlock = 16;
        data16.resize(CHUNK_VOL);
        for (int i = 0; i < CHUNK_VOL; ++i)
            data16[i] = static_cast<uint16_t>(raw[i]);
    }
}

BlockType ChunkPalette::get(int idx) const noexcept {
    if (bitsPerBlock == 8) {
        return palette[data8[idx]];
    } else {
        return static_cast<BlockType>(data16[idx]);
    }
}

void ChunkPalette::set(int idx, BlockType bt) {
    if (bitsPerBlock == 8) {
        // Ensure data8 is allocated before first use
        if (data8.empty()) {
            data8.resize(CHUNK_VOL, 0);
            if (palette.empty()) palette.push_back(BlockType::Air);
        }
        // Find or add to palette
        auto it = std::find(palette.begin(), palette.end(), bt);
        uint8_t paletteIdx;
        if (it == palette.end()) {
            if (palette.size() >= 256) {
                // Palette overflow → repack to 16-bit
                repack();
                set(idx, bt);
                return;
            }
            paletteIdx = static_cast<uint8_t>(palette.size());
            palette.push_back(bt);
        } else {
            paletteIdx = static_cast<uint8_t>(std::distance(palette.begin(), it));
        }
        data8[idx] = paletteIdx;
    } else {
        data16[idx] = static_cast<uint16_t>(bt);
    }
}

void ChunkPalette::repack() {
    // Rebuild entire palette from current data
    std::array<BlockType, CHUNK_VOL> raw;
    for (int i = 0; i < CHUNK_VOL; ++i)
        raw[i] = get(i);
    build(raw);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Chunk
// ─────────────────────────────────────────────────────────────────────────────
Chunk::Chunk(glm::ivec2 pos) : chunkPos_(pos) {
    rawData_.fill(static_cast<uint16_t>(BlockType::Air));
}

Chunk::~Chunk() {
    mesh_.freeGPU();
}

BlockType Chunk::getBlock(int x, int y, int z) const noexcept {
    if (x < 0 || x >= CHUNK_W || y < 0 || y >= CHUNK_H || z < 0 || z >= CHUNK_D)
        return BlockType::Air;
    return static_cast<BlockType>(rawData_[blockIndex(x, y, z)]);
}

void Chunk::setBlock(int x, int y, int z, BlockType type) {
    if (x < 0 || x >= CHUNK_W || y < 0 || y >= CHUNK_H || z < 0 || z >= CHUNK_D)
        return;
    std::unique_lock lock(blockMutex_);
    int idx = blockIndex(x, y, z);
    rawData_[idx] = static_cast<uint16_t>(type);
    palette_.set(idx, type);
    dirty_.store(true, std::memory_order_relaxed);
}

// ─────────────────────────────────────────────────────────────────────────────
//  ChunkMesh GPU management
// ─────────────────────────────────────────────────────────────────────────────
void ChunkMesh::uploadToGPU() {
    if (vertices.empty()) { dirty = false; return; }

    if (vao == 0) glGenVertexArrays(1, &vao);
    if (vbo == 0) glGenBuffers(1, &vbo);
    if (ebo == 0) glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    // Upload vertices
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(ChunkVertex),
                 vertices.data(),
                 GL_STATIC_DRAW);

    // Upload indices
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(uint32_t),
                 indices.data(),
                 GL_STATIC_DRAW);

    // ── Vertex attribute layout ───────────────────────────────────────────────
    // ChunkVertex (24 bytes):
    //  [0]  x, y_lo, y_hi, z            (uint8 ×4)   offset 0
    //  [4]  u, v, tileX, tileY          (uint8 ×4)   offset 4
    //  [8]  ao                          (uint8)      offset 8
    //  [9]  pad[3]                      (uint8 ×3)   offset 9
    //  [12] normal                       (float ×3)   offset 12

    // Attribute 0: packed position (x, y_lo, y_hi, z) as uvec4
    glEnableVertexAttribArray(0);
    glVertexAttribIPointer(0, 4, GL_UNSIGNED_BYTE, sizeof(ChunkVertex),
                           reinterpret_cast<void*>(0));

    // Attribute 1: UV + tile coords (u, v, tileX, tileY) as uvec4
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, sizeof(ChunkVertex),
                           reinterpret_cast<void*>(4));

    // Attribute 2: normal (vec3 floats) at offset 12
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex),
                          reinterpret_cast<void*>(12));

    // Attribute 3: AO (single unsigned byte) at offset 8
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_BYTE, sizeof(ChunkVertex),
                           reinterpret_cast<void*>(8));

    glBindVertexArray(0);

    indexCount = static_cast<GLsizei>(indices.size());
    dirty    = false;
    uploaded = true;

    // Free CPU copies — GPU now owns the data
    std::vector<ChunkVertex>{}.swap(vertices);
    std::vector<uint32_t>{}.swap(indices);
}

void ChunkMesh::freeGPU() {
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
    if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (ebo) { glDeleteBuffers(1, &ebo); ebo = 0; }
    uploaded = false;
}
