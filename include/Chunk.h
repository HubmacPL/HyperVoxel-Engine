#pragma once
#include <array>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>
#include "BlockRegistry.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Chunk dimensions
// ─────────────────────────────────────────────────────────────────────────────
inline constexpr int CHUNK_W  = 16;   // X
inline constexpr int CHUNK_H  = 256;  // Y
inline constexpr int CHUNK_D  = 16;   // Z
inline constexpr int CHUNK_VOL = CHUNK_W * CHUNK_H * CHUNK_D;

// Flat index: Y is outermost for vertical cache locality during meshing
// Layout: [y * CHUNK_W * CHUNK_D + z * CHUNK_W + x]
inline constexpr int blockIndex(int x, int y, int z) noexcept {
    return y * (CHUNK_W * CHUNK_D) + z * CHUNK_W + x;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Chunk Palette — compresses block data when chunk has few unique types
//
//  Strategy:
//    ≤  2 unique types → 1 bit  per block  (32 KB → 512 B)
//    ≤  4 unique types → 2 bits per block  (1 KB)
//    ≤ 16 unique types → 4 bits per block  (2 KB)
//    ≤256 unique types → 8 bits per block  (4 KB)  ← most common
//    >256 unique types → 16 bits direct    (128 KB) ← fallback
// ─────────────────────────────────────────────────────────────────────────────
struct ChunkPalette {
    std::vector<BlockType>   palette;     // palette index → BlockType
    std::vector<uint8_t>     data8;       // 8-bit indices (≤256 palette)
    std::vector<uint16_t>    data16;      // 16-bit direct (>256 palette / fallback)
    uint8_t                  bitsPerBlock = 8;

    void build(const std::array<BlockType, CHUNK_VOL>& raw);
    [[nodiscard]] BlockType get(int idx) const noexcept;
    void set(int idx, BlockType bt);
    void repack();  // called after many set() operations
};

// ─────────────────────────────────────────────────────────────────────────────
//  Mesh vertex — now includes explicit normal (glm::vec3)
//  We keep a compact integer prefix for positions/uv/ao and append a
//  3×float normal so shaders receive a normal directly at location=2.
// ─────────────────────────────────────────────────────────────────────────────
struct ChunkVertex {
    // Position relative to chunk origin (0-15, 0-255, 0-15) → fits in uint8
    uint8_t  x, y_lo, y_hi, z;   // y split across two bytes (0-255)
    // UV within atlas tile: 0 or 1 on each axis (corner of 16x16 tile)
    uint8_t  u, v;
    uint8_t  tileX, tileY;        // atlas tile coordinates (0-15)
    // Lighting: ao in [0-3]
    uint8_t  ao;                  // 0=darkest, 3=brightest
    // Padding to align following float3
    uint8_t  pad[3];
    // Face normal as floats (glm::vec3)
    glm::vec3 normal;
};
static_assert(sizeof(ChunkVertex) == 24, "ChunkVertex must be 24 bytes");

struct ChunkMesh {
    std::vector<ChunkVertex> vertices;
    std::vector<uint32_t>    indices;

    // OpenGL handles (0 = not uploaded)
    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t ebo = 0;

    // Cached index count so CPU vectors can be freed after GPU upload
    int indexCount = 0;

    bool dirty    = true;   // needs re-upload to GPU
    bool uploaded = false;

    void clear() { vertices.clear(); indices.clear(); indexCount = 0; dirty = true; uploaded = false; }

    // Upload to GPU — must be called from GL thread
    void uploadToGPU();
    void freeGPU();
};

// ─────────────────────────────────────────────────────────────────────────────
//  Chunk states (atomic for lock-free reads)
// ─────────────────────────────────────────────────────────────────────────────
enum class ChunkState : uint8_t {
    Empty          = 0,   // no data
    Generating     = 1,   // terrain gen in progress (background thread)
    TerrainGenerated = 2, // terrain ready, awaiting decoration
    Decorated      = 3,   // decoration completed, awaiting remesh
    NeedsMeshing   = 4,   // terrain/decoration changed, needs mesh rebuild
    Meshing        = 5,   // mesh being built (background thread)
    Ready          = 6,   // mesh ready, needs GPU upload
    Uploaded       = 7,   // on GPU, visible
};

// ─────────────────────────────────────────────────────────────────────────────
//  Chunk
// ─────────────────────────────────────────────────────────────────────────────
class Chunk {
public:
    explicit Chunk(glm::ivec2 pos);
    ~Chunk();

    // Non-copyable, moveable
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&) = default;
    Chunk& operator=(Chunk&&) = default;

    // ── Block access ─────────────────────────────────────────────────────────
    [[nodiscard]] BlockType getBlock(int x, int y, int z) const noexcept;
    void setBlock(int x, int y, int z, BlockType type);

    // Raw array access for the mesher (avoids palette overhead in tight loops)
    // Returns pointer to flat uint16_t array, valid for lifetime of Chunk
    [[nodiscard]] const uint16_t* rawBlocks() const noexcept { return rawData_.data(); }

    // Raw light data access (1 byte per voxel: low nibble = skylight, high nibble = blocklight)
    [[nodiscard]] const uint8_t* rawLightData() const noexcept { return lightData_.data(); }
    [[nodiscard]] uint8_t* rawLightData() noexcept { return lightData_.data(); }

    // Thread-safe snapshot of light data — use in meshing thread to avoid data race
    [[nodiscard]] std::array<uint8_t, CHUNK_VOL> rawLightDataCopy() const {
        std::unique_lock lock(blockMutex_);
        return lightData_;
    }

    // Reset all skylight data to 0 (before a full recomputation)
    void resetSkylight() noexcept { lightData_.fill(0); }

    // Simple per-voxel helpers (coords must be in-range)
    [[nodiscard]] uint8_t getSkylight(int x, int y, int z) const noexcept {
        return static_cast<uint8_t>(lightData_[blockIndex(x,y,z)] & 0x0Fu);
    }
    void setSkylight(int x, int y, int z, uint8_t v) noexcept {
        int idx = blockIndex(x,y,z);
        uint8_t high = static_cast<uint8_t>(lightData_[idx] & 0xF0u);
        lightData_[idx] = static_cast<uint8_t>(high | (v & 0x0Fu));
    }

    // Thread-safe snapshot of block data — use in meshing thread to avoid data race
    [[nodiscard]] std::array<uint16_t, CHUNK_VOL> rawDataCopy() const {
        std::unique_lock lock(blockMutex_);
        return rawData_;
    }

    // ── Position ──────────────────────────────────────────────────────────────
    [[nodiscard]] glm::ivec2 chunkPos()  const noexcept { return chunkPos_; }
    [[nodiscard]] glm::vec3  worldOrigin() const noexcept {
        return { chunkPos_.x * CHUNK_W, 0, chunkPos_.y * CHUNK_D };
    }

    // ── State ─────────────────────────────────────────────────────────────────
    [[nodiscard]] ChunkState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    void setState(ChunkState s) noexcept {
        state_.store(s, std::memory_order_release);
    }

    // ── Mesh ─────────────────────────────────────────────────────────────────
    ChunkMesh& mesh() noexcept { return mesh_; }
    const ChunkMesh& mesh() const noexcept { return mesh_; }

    // ── Dirty flag (block modified, needs remesh) ─────────────────────────────
    bool isDirty() const noexcept { return dirty_.load(std::memory_order_relaxed); }
    void markDirty() noexcept     { dirty_.store(true, std::memory_order_relaxed); }
    void clearDirty() noexcept    { dirty_.store(false, std::memory_order_relaxed); }

    // ── Light dirty flag (block changed → need full skylight recompute) ───────
    bool isLightDirty() const noexcept { return lightDirty_.load(std::memory_order_relaxed); }
    void markLightDirty() noexcept     { lightDirty_.store(true,  std::memory_order_relaxed); }
    void clearLightDirty() noexcept    { lightDirty_.store(false, std::memory_order_relaxed); }

    // ── Heightmap (precomputed during gen, used for AO + tree placement) ──────
    std::array<uint8_t, CHUNK_W * CHUNK_D> heightmap{};

private:
    glm::ivec2               chunkPos_;
    // ── Block storage ─────────────────────────────────────────────────────────
    // We keep BOTH a flat uint16_t array (for fast mesher reads)
    // AND a palette for RAM-efficient storage / serialization.
    // The rawData_ is the "hot" path; palette_ is used for save/load.
    std::array<uint16_t, CHUNK_VOL> rawData_;  // 128 KB per chunk
    // Light data: 1 byte per voxel. Low nibble = skylight (0-15), high nibble = blocklight (0-15)
    std::array<uint8_t, CHUNK_VOL>  lightData_{}; // 64 KB per chunk
    ChunkPalette                    palette_;   // compressed copy

    std::atomic<ChunkState>  state_{ChunkState::Empty};
    std::atomic<bool>        dirty_{false};
    std::atomic<bool>        lightDirty_{false};
    ChunkMesh                mesh_;
    mutable std::mutex       blockMutex_;  // for setBlock from main thread
};

using ChunkPtr = std::shared_ptr<Chunk>;
