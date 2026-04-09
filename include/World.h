#pragma once
#include <unordered_map>
#include <memory>
#include <optional>
#include <glm/glm.hpp>
#include "Chunk.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Custom hash for glm::ivec2 — fast, low-collision
//  Uses a Cantor-like pairing function avoiding division
// ─────────────────────────────────────────────────────────────────────────────
struct IVec2Hash {
    std::size_t operator()(const glm::ivec2& v) const noexcept {
        // Interleave bits (partial Morton code) for spatial locality
        uint32_t ux = static_cast<uint32_t>(v.x);
        uint32_t uy = static_cast<uint32_t>(v.y);
        // Spread bits: keep only 16 bits of each, interleave
        ux = (ux | (ux << 8u)) & 0x00FF00FFu;
        ux = (ux | (ux << 4u)) & 0x0F0F0F0Fu;
        ux = (ux | (ux << 2u)) & 0x33333333u;
        ux = (ux | (ux << 1u)) & 0x55555555u;
        uy = (uy | (uy << 8u)) & 0x00FF00FFu;
        uy = (uy | (uy << 4u)) & 0x0F0F0F0Fu;
        uy = (uy | (uy << 2u)) & 0x33333333u;
        uy = (uy | (uy << 1u)) & 0x55555555u;
        return static_cast<std::size_t>(ux | (uy << 1u));
    }
};

struct IVec2Equal {
    bool operator()(const glm::ivec2& a, const glm::ivec2& b) const noexcept {
        return a.x == b.x && a.y == b.y;
    }
};

template<typename V>
using ChunkMap = std::unordered_map<glm::ivec2, V, IVec2Hash, IVec2Equal>;

// ─────────────────────────────────────────────────────────────────────────────
//  World
// ─────────────────────────────────────────────────────────────────────────────
class ChunkManager;
class TerrainGenerator;

class World {
public:
    explicit World(int seed = 12345);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // ── Tick — called each frame from main thread ─────────────────────────────
    void update(const glm::vec3& playerPos);

    // ── Block access (world coordinates) ─────────────────────────────────────
    [[nodiscard]] BlockType getBlock(int wx, int wy, int wz) const;
    void setBlock(int wx, int wy, int wz, BlockType type);

    // Convenience overload
    [[nodiscard]] BlockType getBlock(const glm::ivec3& p) const {
        return getBlock(p.x, p.y, p.z);
    }

    // ── Chunk access ──────────────────────────────────────────────────────────
    [[nodiscard]] ChunkPtr  getChunk(glm::ivec2 cp) const;
    [[nodiscard]] ChunkPtr  getChunkAt(int wx, int wz) const;
    [[nodiscard]] bool      isChunkLoaded(glm::ivec2 cp) const;

    // ── Chunks ready to render (state == Uploaded) ────────────────────────────
    [[nodiscard]] const ChunkMap<ChunkPtr>& chunks() const noexcept { return chunks_; }

    // ── Coordinate helpers ────────────────────────────────────────────────────
    static glm::ivec2 worldToChunkPos(int wx, int wz) noexcept {
        // Floor division — correct for negative coords
        return { wx < 0 ? (wx - CHUNK_W + 1) / CHUNK_W : wx / CHUNK_W,
                 wz < 0 ? (wz - CHUNK_D + 1) / CHUNK_D : wz / CHUNK_D };
    }
    static glm::ivec3 worldToLocalPos(int wx, int wy, int wz) noexcept {
        return { ((wx % CHUNK_W) + CHUNK_W) % CHUNK_W,
                  wy,
                 ((wz % CHUNK_D) + CHUNK_D) % CHUNK_D };
    }

    int seed() const noexcept { return seed_; }
    int renderDistance() const noexcept { return renderDistance_; }
    void setRenderDistance(int d) noexcept { renderDistance_ = d; }

private:
    void loadChunksAround(const glm::vec3& pos);
    void unloadFarChunks(const glm::vec3& pos);
    void promoteReadyChunks();   // upload finished meshes to GPU (main thread)

    int  seed_;
    int  renderDistance_ = 10;   // in chunks

    ChunkMap<ChunkPtr>              chunks_;
    std::unique_ptr<ChunkManager>   chunkManager_;
    std::unique_ptr<TerrainGenerator> terrainGen_;

    glm::ivec2 lastPlayerChunk_{INT_MAX, INT_MAX};
};
